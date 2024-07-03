#include <Arduino.h>

#include "GrundfosUPS3.h"

#include "config.h"
#include "UserIO.h"

// pin we need to read to get the level, needed by ISR

static uint8_t s_pwmGPIO = 0;

// times in microsecs from ESP high res timer

static int64_t  lastPositiveEdge;
static int64_t  timeBetweenPositiveEdges;
static int64_t  timeHigh;

// HIGH or LOW level expected in the ISR.

static int        expectedLevel;

// Counters for errors and how many edges processed

static uint16_t   highCount,lowCount;
static uint16_t   levelDiscards,timeDiscards;

// handle change in GPIO
// time between +ve edges should be 75Hz as this is the UPS3 PMW
// frequency, and the duty cycle can be realistically as low as 1%
// (133 us) - up to 70%, with 1 percent being 1W of power.  Above 75%
// represents error condition for the pump.

// We start a duty cycle measurement looking for +ve edge, then time
// how long to a negative edge.  When the next +ve edge occurs we
// measure the time since the last +ve edge and discard if > 75Hz (+ margin)

void  IRAM_ATTR   handleEdge()
{
   int64_t  currentTime = esp_timer_get_time();

   int pin = digitalRead( s_pwmGPIO );

   // discard if not at the expected level, reset to search for high
   if ( pin != expectedLevel )
   {
      levelDiscards++;
      expectedLevel = HIGH;
      lastPositiveEdge = 0;
      return;
   }

   // If its the first positive edge then we start the search, so
   // record the time and set the next level as LOW then exit

   if ( lastPositiveEdge == 0 && pin == HIGH )
   {
      lastPositiveEdge = currentTime;
      expectedLevel = LOW;
      return;
   }

   // We expect at most 13333 microsecs (75 Hz) from the last positive
   // edge, add some allowed overhead, otherwise we reset the search

   if ( (currentTime - lastPositiveEdge > 15000) )
   {
      timeDiscards++;
      lastPositiveEdge = 0;
      expectedLevel = HIGH;
      return;
   }

   if ( pin == HIGH )
   {
      highCount++;
      timeBetweenPositiveEdges += currentTime - lastPositiveEdge;
      lastPositiveEdge = currentTime;
      expectedLevel = LOW;
   }
   else
   {
      // level dropped LOW, so the time represents how long in high state
      lowCount++;
      timeHigh += currentTime - lastPositiveEdge;
      expectedLevel = HIGH;
   }
}

// map strings to operating modes

static std::map<String,GrundfosUPS3::Mode> ups3ModeMap = {
   { "CS1",GrundfosUPS3::CONSTANT_SPEED1 },
   { "CS2",GrundfosUPS3::CONSTANT_SPEED2 },
   { "CS3",GrundfosUPS3::CONSTANT_SPEED3 },
   { "CP1",GrundfosUPS3::CONSTANT_PRESSURE1 },
   { "CP2",GrundfosUPS3::CONSTANT_PRESSURE2 },
   { "PP1",GrundfosUPS3::PROPORTIONAL_PRESSURE1 },
   { "PP2",GrundfosUPS3::PROPORTIONAL_PRESSURE2 },
};

GrundfosUPS3::GrundfosUPS3( uint8_t pwmGPIO,const char *mode )
            : m_pwmGPIO( pwmGPIO ),
              m_power( 0.0 ),
              m_quality( 0 ),
              m_mode( CONSTANT_SPEED1 )
{
   const char *actualMode = "CS1";
   s_pwmGPIO = m_pwmGPIO;

   std::map<String,GrundfosUPS3::Mode>::const_iterator it = ups3ModeMap.find( String( mode ) );
   if ( it == ups3ModeMap.end() )
   {
      PW_ERROR( "Not found %s mode for UPS3",mode );
   }
   else
   {
      m_mode = it->second;
      actualMode = it->first.c_str();
   }

   PW_MSG( "UPS3 - gpio(%u), mode %d (%s)",m_pwmGPIO,m_mode,actualMode );
}

GrundfosUPS3::~GrundfosUPS3()
{
}

void  GrundfosUPS3::initialise()
{
   pinMode( m_pwmGPIO,INPUT_PULLUP );
}

void  GrundfosUPS3::sample()
{
   PW_MSG( "UPS3 Sample (pin %u)",m_pwmGPIO );

   // start the sample looking for +ve edge, i.e. HIGH pin level

   expectedLevel = HIGH;

   // Reset counters

   highCount = 0;
   lowCount = 0;
   lastPositiveEdge = 0;
   timeBetweenPositiveEdges = 0;
   timeHigh = 0;
   levelDiscards = 0;
   timeDiscards = 0;

   // Attach the interrupt handler for our pin, looking for any change
   // and sample for 670ms (~ 50 samples at 75Hz),  then detach the interrupt.

   attachInterrupt( m_pwmGPIO,handleEdge,CHANGE );
   delay( 670 );
   detachInterrupt( m_pwmGPIO );

   PW_DEBUG( "high %u low %u", highCount,lowCount );
   PW_DEBUG( "Discards: time %u level %u", levelDiscards,timeDiscards );

   // quality is 0 - 100, we expect ~ 50 samples, roughly an equal number
   // of high and low counts

   m_quality = highCount + lowCount;
   m_power = 0;
   if ( m_quality < 85 || !timeBetweenPositiveEdges || ! highCount || ! lowCount )
   {
      PW_WARN( "Poor quality from UPS3 - quality %u",m_quality );
      return;
   }

   uint32_t averagePulse =  ( static_cast<uint32_t>(timeBetweenPositiveEdges) ) / highCount;
   uint32_t highAverage = ( static_cast<uint32_t>(timeHigh) ) / lowCount;

   PW_DEBUG( "Total duration %llu : pulse %u : high %u",timeBetweenPositiveEdges,averagePulse,highAverage );

   m_power = 100.0 * highAverage / averagePulse;
   PW_MSG( "UPS3 power %.1f W",m_power );
}

float_t  GrundfosUPS3::getFlowRate()
{
   return m_power;
}
