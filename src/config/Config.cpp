#include <Preferences.h>
#include <map>
#include <rtc.h>
#include <soc/rtc.h>
#include <hal/wdt_hal.h>
#include <rtc_wdt.h>

#include "Config.h"

Config   *s_instance = nullptr;

const char *k_versionStr = "v25.11.01";

char  defaultConfigString[] = "unknown";

uint8_t Config::numRegistryEntries = 0;

KeyValue  Config::m_entries[ MAX_REGISTRY_ENTRIES ];

//----------------------------------------------------------------------
// For mapping reboot codes to strings

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

#define  MINIMUM_RUNTIME_SECS (10 * 60)
#define  ALLOWED_FAST_RESETS  10

// This data will be set to zero on hard chip reset but persists across soft reboots

RTC_NOINIT_ATTR   uint32_t s_softResets;
RTC_NOINIT_ATTR   uint32_t s_lastResetSeconds;

bool  s_isFast = false;

// info under system namespace in non-volatile store partition

const char k_nvsNamespace[] = "sysinfo";

//----------------------------------------------------------------------
// Registry utilities - should really replace with cJSON

// As we use "a value" to refer to strings, as is it more conventional, then
// we have to strip out these " characters otherwise they are part of the string

void  stripOutQuotes( char *str )
{
   int len = strlen(str);

   for ( int i = 0 ; i < len; i++ )
   {
      if ( str[i] == '"' )
      {
         for ( int j = i; j < len; j++ )
         {
             str[ j ] = str[ j + 1 ];
         }
         len--;
         i--;
      }
   }
}

int findKey( char *key )
{
   for ( int i = 0; i < Config::numRegistryEntries; i++ )
   {
      if ( !strcmp( Config::m_entries[ i ].key,key ) )
      {
         return i;
      }
   }

   return -1;
}

void  replaceRegistryValue( uint8_t index,char *value )
{
   if ( index < Config::numRegistryEntries && strcmp( Config::m_entries[ index ].value,value ) != 0 )
   {
      PW_MSG( "Replace registry :  %s : overriding %s with %s",Config::m_entries[ index ].key,
                     Config::m_entries[ index ].value, value );

      strcpy( Config::m_entries[ index ].value,value );
   }
}

void  setRegistryEntry( char *key,char *value )
{
   int index = findKey( key );

   if ( index > -1 )
   {
      replaceRegistryValue( index,value );
   }
   else if ( Config::numRegistryEntries < MAX_REGISTRY_ENTRIES -1 )
   {
      strcpy( Config::m_entries[ Config::numRegistryEntries ].key,key );
      strcpy( Config::m_entries[ Config::numRegistryEntries ].value,value );

      stripOutQuotes( Config::m_entries[ Config::numRegistryEntries ].value );

      PW_DEBUG( "Set Registry : %s : %s",key,Config::m_entries[ Config::numRegistryEntries ].value );

      Config::numRegistryEntries++;
   }
}

int32_t getRegistryInt( char *key )
{
   int index = findKey( key );

   if ( index == -1 )
   {
      PW_DEBUG( "Registry: no entry for %s",key );
      return -1;
   }
   else
   {
      return( atoi( Config::m_entries[ index ].value ) );
   }
}

char *getRegistryString( char *key )
{
   int index = findKey( key );

   if ( index == -1 )
   {
      PW_DEBUG( "Registry: no entry for %s",key );
      return defaultConfigString;
   }
   else
   {
      return( Config::m_entries[ index ].value );
   }
}

Config::Config( char *fileName )
      : m_isRegistryOk( false ),
        m_spiffs( &SPIFFS ),
        m_configFileName()
{
   // Nothing in the registry yet...

   Config::numRegistryEntries = 0;

   // Instantiate spiffs for config file
   if ( !m_spiffs )
   {
      PW_ERROR( "No SPIFFS available" );
   }
   else
   {
      // Start spiffs, may format filesystem if new board

      m_spiffs->begin( true );
      PW_MSG( "Config():" );
      PW_MSG( "  SPIFFS : used %d of %d",m_spiffs->usedBytes(),m_spiffs->totalBytes() );
      PW_MSG( "  Chip Model : %s [%d]", ESP.getChipModel(),ESP.getChipRevision() );
      PW_MSG( "  Firmware %s",k_versionStr );
   }

   strncpy( m_configFileName,fileName,MAX_FILENAME );
}

Config::~Config()
{
  m_spiffs->end();
  delete m_spiffs;
}

Config   *Config::instance( bool create )
{
   if ( create && !s_instance )
   {
      Config   *newConfig = new Config( "/config.dat" );
      s_instance = newConfig;
   }

   return( s_instance );
}


void Config::initialise()
{
   m_isRegistryOk = readRegistryFromFile();
}

bool Config::isRegistryAvailable()
{
   return( m_isRegistryOk );
}

fs::SPIFFSFS *Config::getSPIFFS()
{
   return( m_spiffs );
}

bool  Config::readRegistryFromFile( void )
{
   if ( !m_spiffs )
   {
      PW_WARN( "readFromFile() : No SPIFFS !" );
      return false;
   }

   File file = m_spiffs->open( m_configFileName,FILE_READ );

   if ( !file )
   {
      PW_WARN( "%s can't open",m_configFileName );
      return false;
   }

   char     line[ 128 ];
   KeyValue keyVal;
   uint8_t  numLines = 0;
   uint8_t  length;
   uint32_t size = file.size();

   // scan the configuration file, replace any default values for keys
   // found that have already been registered - or create a new registry entry

   while ( file.position() < size && numLines < MAX_REGISTRY_ENTRIES )
   {
      length = file.readBytesUntil( '\n',line,128 );

      // The length would be zero if not CRLF, whilst 1 if just CR,
      // editing a file via HTML form editor will ensure CRLF endings,
      // so we skip lines of length less than 2.
      if ( length > 1 && length < 127 )
      {
         // skip lines that start with / or #

         if ( ( line [ 0 ] != '#' && line [ 0 ] != '/') )
         {
            line[ length ] = 0;
            if ( sscanf( line,"%s %s",keyVal.key,keyVal.value ) == 2 )
            {
               stripOutQuotes( keyVal.value );
               setRegistryEntry( keyVal.key,keyVal.value );
               numLines++;
            }
         }
      }
   }

   file.close();

   if ( numLines == MAX_REGISTRY_ENTRIES )
   {
      PW_WARN( "Read maximum %d entries from %s",numLines,m_configFileName );
   }

   return( numLines > 0 );
}

bool  Config::isFactoryReset()
{
   int32_t rebootReason;

   getPersistentInt( k_rebootType,&rebootReason,0 );

   return ( rebootReason == SERVER_RESET );
}

void  Config::setFactoryReset()
{
   // Clear preferences, not quite the same as formatting it, and set
   // reboot type to server reset

   Preferences pref;
   if ( !pref.begin( k_nvsNamespace ) )
   {
      PW_ERROR( "Failed to start nvs %s",k_nvsNamespace );
   }
   else
   {
      pref.clear();
      pref.end();
      setPersistentInt( k_rebootType,SERVER_RESET );
   }
}

bool  Config::getPersistentInt( const String &key,int32_t *value,int32_t defValue )
{
   bool ok = false;
   Preferences pref;

   *value = defValue;

   if ( !pref.begin( k_nvsNamespace ) )
   {
      PW_ERROR( "Failed to start nvs %s",k_nvsNamespace );
   }
   else
   {
      *value = pref.getInt( key.c_str(),defValue );

      pref.end();

      ok = (*value != defValue );
   }

   return ok;
}

void  Config::setPersistentInt( const String &key,int32_t value )
{
   Preferences pref;

   if ( !pref.begin( k_nvsNamespace ) )
   {
      PW_ERROR( "Failed to start nvs %s",k_nvsNamespace );
   }
   else
   {
      // should write 4 bytes
      size_t   ret = pref.putInt( key.c_str(),value );

      if ( ret != 4 )
      {
         PW_ERROR( "Failed to write %d to %s",value,key.c_str() );
      }

      pref.end();
   }
}

String Config::getESPRebootReason( esp_reset_reason_t code )
{
   std::map<esp_reset_reason_t,String>::const_iterator it = s_esp32RebooMap.find( code );
   if ( it == s_esp32RebooMap.end() )
   {
      return( String( "ESP32 unknown reset cause" ) );
   }
   else
   {
      return( it->second );
   }
}

String Config::getAppRebootReason( RebootType code )
{
   std::map<RebootType,String>::const_iterator it = s_appRebootMap.find( code );
   if ( it == s_appRebootMap.end() )
   {
      return( String( "App unknown reset cause" ) );
   }
   else
   {
      return( it->second );
   }
}

String  Config::getRebootReason( RebootType *type )
{
   RebootType reboot;
   int32_t  rebootReason;
   int32_t  watchdogCause;
   String   appReason;

   // get the ESP reason for reboot, and get our reboot marker and watchdog
   // cause from nvs (we trigger RTC watchdog for restart after OTA).

   esp_reset_reason_t espReason = esp_reset_reason();
   (void) getPersistentInt( k_rebootType,&rebootReason );

   // if the nvs data isn't set then we will default it here

   if ( rebootReason == -1 )
   {
      rebootReason = SERVER_OTA_UPDATE;
   }

   // get timing information from the RTC and data preserved across soft reboots

   uint64_t us = esp_rtc_get_time_us();
   uint32_t secs = static_cast<uint32_t> (us / 1000000UL);
   int32_t  lastCycleSecs = secs - s_lastResetSeconds;

   // Display initial information - unfiltered

   reboot = static_cast<RebootType> (rebootReason);

   appReason = getAppRebootReason( reboot );

   PW_MSG( "\n---------INITIAL------------" );
   PW_MSG( "ESP32 Reset    : %s",getESPRebootReason( espReason ).c_str() );
   PW_MSG( "ESP32 Code     : %d",espReason );
   PW_MSG( "Current Time   : %u",secs );
   PW_MSG( "Last Duration  : %d\n",lastCycleSecs );
   PW_MSG( "App Reset      : %s",appReason.c_str() );
   PW_MSG( "Soft Resets    : %u",s_softResets );
   PW_MSG( "Last Reset @   : %u",s_lastResetSeconds );
   PW_MSG( "-----------------------------\n" );

   // now set adjust our reboot marker for this cycle based on ESP code

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
            // Already assigned reboot
            break;
      default:
            reboot = UNKNOWN;
            break;
   }

   // power cycle doesn't necessarily clear the RTC RAM, that appears to happen
   // if we h/w reset via the RTC watchdog.  But in both cases we will reset our
   // soft reset counter and last cycle time.  We also reset if we've had an
   // OTA update or server reboot

   if ( reboot == POWER_CYCLE || reboot == SERVER_OTA_UPDATE || reboot == SERVER_REBOOT  || reboot == SERVER_RESET )
   {
      PW_DEBUG( "Resetting soft reboot data" );
      s_softResets = 0;
      lastCycleSecs = 0;
   }

   // Set the lastResetSeconds to this cycle start time

   s_lastResetSeconds = secs;

   // if the time since the last soft reset was greater than a minimum we reset
   // soft reset counter again

   if ( lastCycleSecs > MINIMUM_RUNTIME_SECS )
   {
      PW_DEBUG( "Long last cycle, resetting data" );
      s_softResets = 0;
   }

   // increment our reboot counter, and re-display info:

   s_softResets++;

   PW_MSG( "\n-----------NEW---------------" );
   PW_MSG( "ESP32 Reset    : %s",getESPRebootReason( espReason ).c_str() );
   PW_MSG( "ESP32 Code     : %d",espReason );
   PW_MSG( "Current Time   : %u",secs );
   PW_MSG( "Last Duration  : %d\n",lastCycleSecs );
   PW_MSG( "App Reset      : %s",appReason.c_str() );
   PW_MSG( "Soft Resets    : %u",s_softResets );
   PW_MSG( "Last Reset @   : %u",s_lastResetSeconds );
   PW_MSG( "-----------------------------\n" );

   if ( s_softResets >= ALLOWED_FAST_RESETS )
   {
      PW_ERROR( "Too many fast resets" );
      s_isFast = true;
   }

   *type = reboot;

   appReason += " (ESP32 - ";
   appReason += getESPRebootReason( espReason );
   appReason += ")";

   return( appReason );
}

bool Config::isFastReset()
{
   return s_isFast;
}

void hwReset()
{
   PW_MSG( "HW Reset" );

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

   PW_DEBUG( "out hwReset" );
}

void reboot()
{
   Config::instance()->setPersistentInt( k_rebootType,SERVER_REBOOT );

   delay( 500 );
   ESP.restart();
}


static bool s_isRebootRequired = false;
void    setRebootRequired()
{
   s_isRebootRequired = true;
}

bool isRebootRequired()
{
   return s_isRebootRequired;
}

