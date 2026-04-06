/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#include <SD.h>

#include <Preferences.h>
#include <map>
#include <rtc.h>
#include <soc/rtc.h>
#include <hal/wdt_hal.h>
#include <rtc_wdt.h>

#include "src/config/Config.h"

#include "src/userio/Indicator.h"



// For modbus, what are the transmit & receive pins - using h/w serial #2
// The RS485 - TTL module has its TX connected to ESP's RX, and obviously
// its RX to ESP's TX.  PZEM-16 takes ~ 30 ms to act on receipt of the
// register read request.  And we introduce a delay between modbus requests.
// Also require an enable/disable for the MAX3485 485 converter.
//
// We appear to need a 10k (?) pullup too on the RX GPIO.  Tried the
// internal pull up to no avail.

HardwareConfig MasterDevice =
{
   27,            // OneWireGPIO
   2,             // ModBusSerial
   9600,          // ModBusBaudRate
   SERIAL_8N1,    // ModBusSerialFormat
   16,            // ModBusRxGPIO
   17,            // ModBusTxGPIO
   50,            // ModBusMsgDelay
   4,             // ModBus485EnGPIO
   32,            // OLEDClkGPIO
   33,            // OLEDDataGPIO
   -1,            // TouchButton1
   -1,            // TouchButton2
   -1,            // PWM GPIO
   true           // has SD card
};

// T-Nodes use the integrated ESP32/OLED module
// The don't support modbus transceiver module but may use modbus via
// a modbusTCP adapter, set in config.dat & sensors file

HardwareConfig TemperatureNode =
{
   26,            // OneWireGPIO
   -1,            // ModBusSerial
   -1,            // ModBusBaudRate
   -1,            // ModBusSerialFormat
   -1,            // ModBusRxGPIO
   -1,            // ModBusTxGPIO
   -1,            // ModBusMsgDelay
   -1,            // ModBus485EnGPIO
   4,             // OLEDClkGPIO
   5,             // OLEDDataGPIO
   -1,            // TouchButton1
   -1,            // TouchButton2
   25,            // PWM GPIO
   false          // has SD card
};

// External board is a Temperature Bord but with different
// address for One Wire, and no PWM support

HardwareConfig ExternalBoard =
{
   13,            // OneWireGPIO
   -1,            // ModBusSerial
   -1,            // ModBusBaudRate
   -1,            // ModBusSerialFormat
   -1,            // ModBusRxGPIO
   -1,            // ModBusTxGPIO
   -1,            // ModBusMsgDelay
   -1,            // ModBus485EnGPIO
   4,             // OLEDClkGPIO
   5,             // OLEDDataGPIO
   -1,            // TouchButton1
   -1,            // TouchButton2
   -1,            // PWM GPIO
   false          // has SD card
};


// Monitor uses the integrated ESP32/OLED module and also have modbus
// transceiver module available and PWM support.

HardwareConfig MonitorBoard =
{
   13,            // OneWireGPIO
   0,             // ModBusSerial
   9600,          // ModBusBaudRate
   SERIAL_8N1,    // ModBusSerialFormat
   3,             // ModBusRxGPIO
   1,             // ModBusTxGPIO
   50,            // ModBusMsgDelay
   12,            // ModBus485EnGPIO
   4,             // OLEDClkGPIO
   5,             // OLEDDataGPIO
   -1,            // TouchButton1 (esp32 touch 2)
   -1,            // TouchButton2
   16,            // PWM GPIO
   false          // has SD card
};

// The waveshare 43B LCD panel.  Uses hardware serial for modbus.  Only
// opto isoloated digital IO so no 1-wire support or PWM.  Touch will be
// via lvgl graphics buttons.

HardwareConfig WaveshareLCD =
{
   -1,            // OneWireGPIO
   1,             // ModBusSerial
   9600,          // ModBusBaudRate
   SERIAL_8N1,    // ModBusSerialFormat
   43,            // ModBusRxGPIO
   44,            // ModBusTxGPIO
   50,            // ModBusMsgDelay
   -1,            // ModBus485EnGPIO
   -1,            // OLEDClkGPIO
   -1,            // OLEDDataGPIO
   -1,            // TouchButton1 (esp32 touch 2)
   -1,            // TouchButton2
   -1,            // PWM GPIO
   false          // has SD card
};

HardwareConfig WaveshareRelay =
{
   -1,            // OneWireGPIO
   1,             // ModBusSerial
   9600,          // ModBusBaudRate
   SERIAL_8N1,    // ModBusSerialFormat
   18,            // ModBusRxGPIO
   17,            // ModBusTxGPIO
   50,            // ModBusMsgDelay
   21,            // ModBus485EnGPIO
   -1,            // OLEDClkGPIO
   -1,            // OLEDDataGPIO
   -1,            // TouchButton1 (esp32 touch 2)
   -1,            // TouchButton2
   -1,            // PWM GPIO
   false          // has SD card
};

HardwareConfig ESP32S3Rs485 =
{
   10,            // OneWireGPIO
   1,             // ModBusSerial - use port 1 for real serial
   9600,          // ModBusBaudRate
   SERIAL_8N1,    // ModBusSerialFormat
   18,            // ModBusRxGPIO
   17,            // ModBusTxGPIO
   50,            // ModBusMsgDelay
   15,            // ModBus485EnGPIO
   -1,            // OLEDClkGPIO
   -1,            // OLEDDataGPIO
   -1,            // TouchButton1
   -1,            // TouchButton2
   4,             // PWM GPIO
   false          // has SD card
};

#define  TNODE_BOARD_FILE     "/tnode.hid"
#define  MASTER_BOARD_FILE    "/master.hid"
#define  EXTERNAL_BOARD_FILE  "/external.hid"
#define  MONITOR_BOARD_FILE   "/monitor.hid"
#define  WAVESHARE_LCD_FILE   "/lcd.hid"
#define  ESP32S3_FILE         "/esp32s3.hid"

HardwareConfig *hwConfig = nullptr;

void  selectHardware()
{
   TVMG_MSG( "Board selection..." );

#if defined(TVMG_WAVESHARE_LCDB)
   TVMG_MSG( "Waveshare LCD" );
   hwConfig = &WaveshareLCD;
#elif defined(TVMG_WAVESHARE_RELAY)
   TVMG_MSG( "Waveshare Relay" );
   hwConfig = &WaveshareRelay;
#elif defined(TVMG_ESP32S3) && defined(TVMG_RS485)
   TVMG_MSG( "ESP32S3 with RS485" );
   hwConfig = &ESP32S3Rs485;
#else
   // Must be an older ESP32 board - runtime detect based on file
   // in filesystem

   if ( !Config::instance() || ! tvmgFileSys )
   {
      TVMG_WARN( "No Config available" );
   }
   else if ( tvmgFileSys.exists( MASTER_BOARD_FILE ) )
   {
      TVMG_MSG( "Master Device" );
      hwConfig = &MasterDevice;
   }
   else if ( tvmgFileSys.exists( EXTERNAL_BOARD_FILE ) )
   {
      TVMG_MSG( "External Board" );
      hwConfig = &ExternalBoard;
   }
   else if ( tvmgFileSys.exists( MONITOR_BOARD_FILE ) )
   {
      TVMG_MSG( "Monitor Board" );
      hwConfig = &MonitorBoard;
   }
   else if ( tvmgFileSys.exists( TNODE_BOARD_FILE ) )
   {
      TVMG_MSG( "TNode" );
      hwConfig = &TemperatureNode;
   }
#endif

#ifdef TVMG_OLED
   if ( !hwConfig )
   {
      TVMG_MSG( "No board file - default to TNode" );
      hwConfig = &TemperatureNode;
   }
#else
   if ( !hwConfig )
   {
      TVMG_MSG( "No board file - default to ESP32S3" );
      hwConfig = &ESP32S3Rs485;
   }
#endif

   // If we have a modbus enable then bring it low as soon as possible.

   if ( hwConfig->ModBus485EnGPIO != -1 )
   {
      pinMode( hwConfig->ModBus485EnGPIO,OUTPUT );
      digitalWrite( hwConfig->ModBus485EnGPIO,0 );
   }
}

bool  boardHasSDCard()
{
   if ( hwConfig )
   {
      return hwConfig->hasSDCard;
   }

   return( false );
}

//------------------------------------------------------------------------------
// For reboot management

// mapping reboot codes to strings
static std::map<RebootType,String> s_appRebootMap = {
   { POWER_CYCLE,"Power Cycle" },
   { BOOT_NO_CONFIG,"No Config" },
   { BOOT_NO_WIFI,"No WiFi" },
   { BOOT_NO_NTP,"No NTP" },
   { BOOT_IN_SETUP,"During Setup" },
   { LOST_WIFI,"Lost WiFi" },
   { SERVER_REBOOT,"Server Reboot" },
   { SERVER_RESET,"Server Reset" },
   { SERVER_OTA_UPDATE,"OTA Update" },
   { LOOP_MUTEX,"Mutex Failure" },
   { ESP32_PANIC,"ESP32 Panic" },
   { ESP32_WATCHDOG,"ESP32 Watchdog" },
   { APP_24D_RESET,"24 day reset" },
   { UNKNOWN,"Unknown" }
};

static std::map<esp_reset_reason_t,String> s_esp32RebooMap = {
   { ESP_RST_POWERON,"Power Cycle" },
   { ESP_RST_EXT,"External Pin" },
   { ESP_RST_PWR_GLITCH,"Power Glitch" },
   { ESP_RST_PANIC,"Core Panic" },
   { ESP_RST_INT_WDT,"Interrupt Watcdog" },
   { ESP_RST_TASK_WDT,"Task Watchdog" },
   { ESP_RST_WDT,"Other Watchdog" },
   { ESP_RST_SW,"App Reset" },
};

// The RTC_NOINIT_ATTR is a data region that is not reset on soft reboot so can
// be used to store data across such reboots.

#define  MINIMUM_RUNTIME_SECS (10 * 60)
#define  ALLOWED_FAST_RESETS  10

RTC_NOINIT_ATTR   uint32_t s_softResets;
RTC_NOINIT_ATTR   uint32_t s_lastResetSeconds;

static const char k_nvsNamespace[] = "sysinfo";

// An early reset is one where the device has simply not managed to survive long enough
// for the standard fast reboot logic to start, so we set the allowed count greater
// than the 'normal' allowed fast reboots so if that is exceeded then there's a
// real issue.

#define  ALLOWED_EARLY_RESETS (ALLOWED_FAST_RESETS + 5)
#define  MAGIC_WORD  0xFACEFEED

RTC_NOINIT_ATTR   uint32_t s_earlyResets;
RTC_NOINIT_ATTR   uint32_t s_magicResetWord;

// flag kept across calls to wasFastReboot()
static bool s_wasFastReboot = false;

// persistent helpers

bool isFactoryReset()
{
   int32_t rebootReason;
   getPersistentInt( k_rebootType,&rebootReason,0 );
   return ( rebootReason == SERVER_RESET );
}

bool didRebootNoWiFi()
{
   int32_t rebootReason;
   getPersistentInt( k_rebootType,&rebootReason,0 );
   return ( rebootReason == BOOT_NO_WIFI );
}

void setFactoryReset()
{
   Preferences pref;
   if ( !pref.begin( k_nvsNamespace ) )
   {
      TVMG_ERROR( "Failed to start nvs %s",k_nvsNamespace );
   }
   else
   {
      pref.clear();
      pref.end();
      setPersistentInt( k_rebootType,SERVER_RESET );
   }

   Indicator *apModeIndicator = Indicator::getIndicator( Indicator::SYSTEM,SYSTEM_AP_ID );
   if (apModeIndicator)
   {
      apModeIndicator->off();
   }
}

bool getPersistentInt( const String &key,int32_t *value,int32_t defValue )
{
   bool ok = false;
   Preferences pref;
   *value = defValue;
   if ( !pref.begin( k_nvsNamespace ) )
   {
      TVMG_ERROR( "Failed to start nvs %s",k_nvsNamespace );
   }
   else
   {
      *value = pref.getInt( key.c_str(),defValue );
      pref.end();
      ok = (*value != defValue );
   }
   return ok;
}

void setPersistentInt( const String &key,int32_t value )
{
   Preferences pref;
   if ( !pref.begin( k_nvsNamespace ) )
   {
      TVMG_ERROR( "Failed to start nvs %s",k_nvsNamespace );
   }
   else
   {
      size_t ret = pref.putInt( key.c_str(),value );
      if ( ret != 4 )
      {
         TVMG_ERROR( "Failed to write %d to %s",value,key.c_str() );
      }
      pref.end();
   }
}

String getESPRebootReason( esp_reset_reason_t code )
{
   auto it = s_esp32RebooMap.find( code );
   if ( it == s_esp32RebooMap.end() )
   {
      return String("ESP32 unknown reset cause");
   }
   return it->second;
}

String getAppRebootReason( RebootType code )
{
   auto it = s_appRebootMap.find( code );
   if ( it == s_appRebootMap.end() )
   {
      return String("App unknown reset cause");
   }
   return it->second;
}

String getRebootReason( RebootType *type )
{
   RebootType reboot;
   int32_t rebootReason;
   String appReason;

   esp_reset_reason_t espReason = esp_reset_reason();
   (void) getPersistentInt( k_rebootType,&rebootReason );
   if ( rebootReason == -1 )
      rebootReason = SERVER_OTA_UPDATE;

   uint64_t us = esp_rtc_get_time_us();
   uint32_t secs = static_cast<uint32_t>(us / 1000000UL);
   int32_t lastCycleSecs = secs - s_lastResetSeconds;

   reboot = static_cast<RebootType>(rebootReason);
   appReason = getAppRebootReason(reboot);

   TVMG_MSG("\n---------INITIAL------------");
   TVMG_MSG("ESP32 Reset    : %s",getESPRebootReason( espReason ).c_str());
   TVMG_MSG("ESP32 Code     : %d",espReason);
   TVMG_MSG("Current Time   : %u",secs);
   TVMG_MSG("Last Duration  : %d\n",lastCycleSecs);
   TVMG_MSG("App Reset      : %s",appReason.c_str());
   TVMG_MSG("Soft Resets    : %u",s_softResets);
   TVMG_MSG("Last Reset @   : %u",s_lastResetSeconds);
   TVMG_MSG("-----------------------------\n");

   switch( espReason )
   {
      case ESP_RST_POWERON:
      case ESP_RST_EXT:
      case ESP_RST_PWR_GLITCH:
            reboot = POWER_CYCLE;
            break;
      case ESP_RST_PANIC:
            reboot = ESP32_PANIC;
            break;
      case ESP_RST_INT_WDT:
      case ESP_RST_TASK_WDT:
      case ESP_RST_WDT:
            if ( reboot != SERVER_OTA_UPDATE )
            {
               reboot = ESP32_WATCHDOG;
            }
            break;
      case ESP_RST_SW:
            break;
      default:
            reboot = UNKNOWN;
            break;
   }

   if ( reboot == POWER_CYCLE || reboot == SERVER_OTA_UPDATE || reboot == SERVER_REBOOT  || reboot == SERVER_RESET )
   {
      TVMG_DEBUG( "Resetting soft reboot data" );
      s_softResets = 0;
      s_earlyResets = 0;
      lastCycleSecs = 0;
   }

   s_lastResetSeconds = secs;

   if ( lastCycleSecs > MINIMUM_RUNTIME_SECS )
   {
      TVMG_DEBUG( "Long last cycle, resetting data" );
      s_softResets = 0;
      s_earlyResets = 0;
   }

   s_softResets++;

   TVMG_MSG("\n-----------NEW---------------");
   TVMG_MSG("ESP32 Reset    : %s",getESPRebootReason( espReason ).c_str());
   TVMG_MSG("ESP32 Code     : %d",espReason);
   TVMG_MSG("Current Time   : %u",secs);
   TVMG_MSG("Last Duration  : %d\n",lastCycleSecs);
   TVMG_MSG("App Reset      : %s",appReason.c_str());
   TVMG_MSG("Soft Resets    : %u",s_softResets);
   TVMG_MSG("Last Reset @   : %u",s_lastResetSeconds);
   TVMG_MSG("Early Resets   : %u",s_earlyResets);
   TVMG_MSG("-----------------------------\n");

   if ( s_softResets >= ALLOWED_FAST_RESETS )
   {
      TVMG_ERROR( "Too many fast resets" );
      s_wasFastReboot = true;
   }

   *type = reboot;

   appReason += " (ESP32 - ";
   appReason += getESPRebootReason( espReason );
   appReason += ")";

   return appReason;
}

bool wasFastReboot()
{
   return s_wasFastReboot;
}
// A fault has arisen such that we never entered the reboot detection logic that would
// ordinarily place the system in AP mode.  There must have been some very early
// fault that's causing continuous rebooting.
//
// So all we can do is simply stop continuously rebooting by looping indefinately

bool boardHalt()
{
#if defined(TVMG_WAVESHARE_RELAY)
   bool on = true;
   pinMode( 17,OUTPUT );
   while ( 1 )
   {
      digitalWrite( 17,HIGH );
      delay( 1000 );
      digitalWrite( 17,LOW );
      delay( 1000 );
   }
#else
   while( 1 )
   {
      delay( 5000 );
   }
#endif
}

static bool s_isRebootRequired = false;


void hwReset()
{
   TVMG_MSG( "HW Reset" );

   // Code essentially from https://github.com/espressif/arduino-esp32/issues/10795
   // we trigger a RTC watchdog in 250 ms

   wdt_hal_context_t rwdt_ctx;

   rwdt_ctx.inst = WDT_RWDT;
   rwdt_ctx.rwdt_dev = RWDT_DEV_GET();

   wdt_hal_init(&rwdt_ctx, WDT_RWDT, 0, false);
   uint32_t stage_timeout_ticks = (uint32_t)((uint64_t)250 * rtc_clk_slow_freq_get_hz() / 1000);
   wdt_hal_write_protect_disable(&rwdt_ctx);
   wdt_hal_config_stage(&rwdt_ctx, WDT_STAGE0, stage_timeout_ticks, WDT_STAGE_ACTION_RESET_RTC);
   wdt_hal_enable(&rwdt_ctx);
   wdt_hal_write_protect_enable(&rwdt_ctx);

   delay( 500 );

   TVMG_DEBUG( "out hwReset" );
}

void reboot()
{
   setPersistentInt( k_rebootType,SERVER_REBOOT );

   // We turn off any AP mode indicator here, as we may be soft resetting 

   Indicator *apModeIndicator = Indicator::getIndicator( Indicator::SYSTEM,SYSTEM_AP_ID );
   if (apModeIndicator)
   {
      apModeIndicator->off();
   }

   delay( 500 );
   ESP.restart();
}


void    setRebootRequired()
{
   s_isRebootRequired = true;
}

bool isRebootRequired()
{
   return s_isRebootRequired;
}

void  checkEarlyRebootFailure()
{
   if ( s_magicResetWord != MAGIC_WORD )
   {
      s_earlyResets = 0;
      s_magicResetWord = MAGIC_WORD;
   }
   
   s_earlyResets++;

   // halt the board if real trouble

   if ( s_earlyResets >= ALLOWED_EARLY_RESETS )
   {
      boardHalt();
   }
}
