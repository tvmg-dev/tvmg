/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#include <Arduino.h>

#include <map>

#include "driver/mcpwm_cap.h"

#include "src/sensors/GrundfosUPS3.h"
#include "src/sensors/HeatMeter.h"

// pin we need to read to get the level, needed by ISR

static uint8_t s_pwmGPIO = 0;

// map strings to operating modes

static std::map<String,GrundfosUPS3::Mode> ups3ModeMap = {
   { "CS1",GrundfosUPS3::CONSTANT_SPEED1 },
   { "CS2",GrundfosUPS3::CONSTANT_SPEED2 },
   { "CS3",GrundfosUPS3::CONSTANT_SPEED3 },
   { "CP1",GrundfosUPS3::CONSTANT_PRESSURE1 },
   { "CP2",GrundfosUPS3::CONSTANT_PRESSURE2 },
   { "PP1",GrundfosUPS3::PROPORTIONAL_PRESSURE1 },
   { "PP2",GrundfosUPS3::PROPORTIONAL_PRESSURE2 }
};

// We calculate flow from power via quadratic, so 3 params needed
// x2 would be for the square of x.  Also need the min & max power
// that can be measured - if we go below min then obviously flow is
// zero, above we have to limit to some level TBD.
//
// The parameters obtained by manually using the Grundfos UPS3 pump
// selector online chart where head/flow duty point can be inserted
// and the power obtained.  These params are for m3/hr.

typedef struct {
   float_t  min,max;
   float_t  x0,x1,x2;
} UPS3FlowCoefficients;

static std::map <GrundfosUPS3::Mode,UPS3FlowCoefficients> UPS3Coeffs = {
   { GrundfosUPS3::CONSTANT_SPEED3,{ 30.4,60,-1.07,0.0405,-0.0000613 } },
   { GrundfosUPS3::CONSTANT_SPEED2,{ 22.1,45,-1.02,0.054,-0.000148 } },
   { GrundfosUPS3::CONSTANT_SPEED1,{ 16.8,28,-1.1,0.0796,-0.000477 } },
   { GrundfosUPS3::CONSTANT_PRESSURE2,{ 18.84,52,-1.02,0.0641,-0.00025 } },
   { GrundfosUPS3::CONSTANT_PRESSURE1,{ 11.3,33,-0.969,0.104,-0.000768 } },
   { GrundfosUPS3::PROPORTIONAL_PRESSURE2,{ 6.2,33,-0.42,0.0944,-0.00101 } },
   { GrundfosUPS3::PROPORTIONAL_PRESSURE1,{ 4.7,17,-1.04,0.306,-0.00901 } }
};

//------------------------------------------------------------------------------
// MCPWM capture processing
// Using the capture block as it latches the clock value (80 MHz) on edge changes.

static mcpwm_cap_timer_handle_t   s_captureTimer = NULL;
static mcpwm_cap_channel_handle_t s_captureChannel = NULL;

// Data variables (volatile because they are modified in ISR)

static volatile uint32_t lastRisingEdgeTick = 0;
static volatile uint32_t totalPeriodTicks = 0;
static volatile uint32_t totalHighTicks = 0;
static volatile uint32_t lastTick = 0;
static volatile uint32_t highCount = 0;
static volatile uint32_t lowCount = 0;

// ISR Callback: Runs on every edge 
static bool IRAM_ATTR captureCallback(mcpwm_cap_channel_handle_t capChannel, const mcpwm_capture_event_data_t *edata, void *user_data)
{
   uint32_t now = edata->cap_value;

   if ( now - lastTick < 40000 )  // 500us glitch filter
   {
      return false;
   }

   // Spurious edges detected, possibly worse on slow slew rates ?
   // https://esp32.com/viewtopic.php?t=38478
   // So don't rely on edge from the capture block, i.e. edata->cap_edge == MCPWM_CAP_EDGE_POS

   bool isHigh = ( gpio_get_level( static_cast<gpio_num_t>(s_pwmGPIO) ) == 1 ? true : false );

   lastTick = now;

   if ( isHigh )
   {
      highCount++;

      // Calculate period from previous rising edge

      totalPeriodTicks = now - lastRisingEdgeTick;
      lastRisingEdgeTick = now;
   }
   else
   {
      lowCount++;
      // Calculate high pulse width

      totalHighTicks = now - lastRisingEdgeTick;
   }
   return false;
}

GrundfosUPS3::GrundfosUPS3( uint8_t pwmGPIO,const char *mode )
            : m_pwmGPIO( pwmGPIO ),
              m_power( 0.0 ),
              m_quality( 0 ),
              m_mode( CONSTANT_SPEED1 ),
              m_modeString( mode )
{
   const char *actualMode = "CS1";
   s_pwmGPIO = m_pwmGPIO;

   std::map<String,GrundfosUPS3::Mode>::const_iterator it = ups3ModeMap.find( String( mode ) );
   if ( it == ups3ModeMap.end() )
   {
      TVMG_ERROR( "Not found %s mode for UPS3",mode );
   }
   else
   {
      m_mode = it->second;
      actualMode = it->first.c_str();
   }

   TVMG_MSG( "UPS3 - gpio(%u), mode %d (%s)",m_pwmGPIO,m_mode,actualMode );
}

GrundfosUPS3::~GrundfosUPS3()
{
}

void  GrundfosUPS3::initialise()
{
   pinMode( m_pwmGPIO,INPUT_PULLUP );

   mcpwm_capture_timer_config_t timer_conf = {
       .group_id = 0,
       .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
   };

   if ( mcpwm_new_capture_timer(&timer_conf, &s_captureTimer) != ESP_OK )
   {
      TVMG_ERROR( "Failed to get new capture timer");
      return;
   }

   // Initialize Capture Channel with our GPIO - capturing both edges in the ISR

   mcpwm_capture_channel_config_t channelConfig = {};
   channelConfig.gpio_num = m_pwmGPIO;
   channelConfig.prescale = 1;
   channelConfig.flags.neg_edge = 1;
   channelConfig.flags.pos_edge = 1;
   channelConfig.flags.pull_up = 1;

   if ( mcpwm_new_capture_channel(s_captureTimer, &channelConfig, &s_captureChannel) != ESP_OK )
   {
      TVMG_ERROR( "Failed to get a capture channel" );
      return;
   }

   // Register our callback (ISR)
   mcpwm_capture_event_callbacks_t cbs = { .on_cap = captureCallback, };

   if ( mcpwm_capture_channel_register_event_callbacks(s_captureChannel, &cbs, NULL) != ESP_OK )
   {
      TVMG_ERROR( "Failed to register callback with capture channel" );
      return;
   }

   if ( mcpwm_capture_timer_enable(s_captureTimer) != ESP_OK )
   {
      TVMG_ERROR( "Failed to enable the capture timer" );
      return;
   }

   if ( mcpwm_capture_timer_start(s_captureTimer) != ESP_OK )
   {
      TVMG_ERROR( "Failed to start the capture timer" );
      return;
   }
}

void GrundfosUPS3::sample()
{
   TVMG_DEBUG( "UPS3 Sample (pin %u)",m_pwmGPIO );

   highCount = 0;
   lowCount = 0;
   totalPeriodTicks = 0;
   totalHighTicks = 0;
   lastRisingEdgeTick = 0;
   lastTick = 0;

   // Start the hardware capture
   mcpwm_capture_channel_enable( s_captureChannel );

   // Give it 670ms to gather data - ~ 100 edges at 75 Hz
   delay( 670 );

   // Stop the hardware capture (ISRs cease immediately), wait 30ms (2x75Hz periods) to settle
   mcpwm_capture_channel_disable( s_captureChannel );
   delay( 30 );

   TVMG_DEBUG( "Counts : high %u, low %u", highCount,lowCount );
   TVMG_DEBUG( "Ticks : period %u, high %u", totalPeriodTicks,totalHighTicks );

   // quality is 0 - 100, we expect ~ 50 samples, roughly an equal number
   // of high and low counts.  A quality of < 5 suggests pump is off

   m_quality = highCount + lowCount;
   m_power = 0;

   if ( m_quality < 5 )
   {
      TVMG_DEBUG( "Pump is not running" );
      return;
   }

   // We allow some problematic samples, 70% ?
   if ( m_quality < 70 || ! highCount || ! lowCount || !totalPeriodTicks)
   {
      TVMG_WARN( "Poor quality from UPS3 - quality %u",m_quality );
      m_power = HM_POWER_ERROR;
      return;
   }

   float freq = 80000000.0 / totalPeriodTicks;
   m_power = (totalHighTicks * 100.0) / totalPeriodTicks;

   TVMG_MSG( "Frequency %.1f Hz, UPS3 Power %.2f W",freq,m_power );
}

float_t  GrundfosUPS3::getFlowRate()
{
   UPS3FlowCoefficients coeffs;
   float_t flowRate = 0;
   float_t power = m_power;

   if ( !UPS3Coeffs.count( m_mode ) )
   {
      TVMG_ERROR( "Flow Rate : no coeffs for current mode !" );
   }
   else if ( m_power == HM_POWER_ERROR )
   {
      TVMG_WARN( "Failed to read power consumed" );
      flowRate = FLOW_RATE_ERROR;
   }
   else if ( m_power > 1.0f )       // m_power will be zero if the pump is off, that's valid
   {
      coeffs = UPS3Coeffs.at( m_mode );

      if ( m_power < coeffs.min )
      {
         TVMG_WARN( "%.1f W is below UPS3 mode min (%.1f)",m_power,coeffs.min );
      }
      else if ( m_power > coeffs.max )
      {
         TVMG_WARN( "%.1f W is above UPS3 mode max (%.1f) limiting",m_power,coeffs.max );
         flowRate = FLOW_RATE_ERROR;
      }
      else
      {
         // Get m3/hr, then convert to l/min

         flowRate = (coeffs.x0) + (coeffs.x1 * power) + (coeffs.x2 * pow( power,2 ));
         flowRate /= 0.06;

         TVMG_DEBUG( "%d %.1f W = flowRate %.1f l/min",m_mode,power,flowRate );
      }
   }

   return flowRate;
}

String  GrundfosUPS3::getMode()
{
   return m_modeString;
}

float_t  GrundfosUPS3::getPowerConsumed()
{
   return m_power;
}
