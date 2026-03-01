#include <Preferences.h>

#include <map>
#include <rtc.h>
#include <soc/rtc.h>
#include <hal/wdt_hal.h>
#include <rtc_wdt.h>
#include <cJSON.h>

#include "Config.h"

#include "src/userio/Indicator.h"
#include "src/core/utils.h"

const char *k_versionStr = "v26.03.01";

static Config   *s_instance = nullptr;
static char  defaultConfigString[] = "unknown";

#define MAX_CONFIG_FILESIZE   (8 * 1024)
#define JSON_CONFIG_FILENAME  "/config.json"
#define FLAT_CONFIG_FILENAME  "/config.dat"

uint8_t  Config::numRegistryEntries = 0;
KeyValue Config::m_entries[ MAX_REGISTRY_ENTRIES ];

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

// info under system namespace in non-volatile store partition

static const char k_nvsNamespace[] = "sysinfo";

// For *very* early fault detection.
// The s_earlyResets will get incremented as first part of setup() and if this
// count exceeds our fast resets (+ margin) then our code is not robustly handing
// some very early fault code (e.g. a fault when setting up serial port).
// In which case we enter a recovery mode of some description.
//
// If the magicResetWord isn't our MAGIC_WORD then it's a power cycle start

#define  ALLOWED_EARLY_RESETS (ALLOWED_FAST_RESETS + 5)
#define  MAGIC_WORD  0xFACEFEED

RTC_NOINIT_ATTR   uint32_t s_earlyResets;
RTC_NOINIT_ATTR   uint32_t s_magicResetWord;       

//----------------------------------------------------------------------
// Registry key to JSON path mapping
// Maps flat registry keys to their hierarchical JSON location

struct KeyPathMapping {
   const char *flatKey;    // Used in GET/SET REGISTRY macros
   const char *parent1;    // First level (e.g., "network")
   const char *parent2;    // Second level (e.g., "wifi")
   const char *jsonKey;    // Key name in JSON (usually same as flatKey)
   const char *type;       // "string", "int", or "bool"
};

static const KeyPathMapping k_keyMappings[] = {
   // network
   //    wifi
   { "WIFI_SSID",             "network", "wifi", "WIFI_SSID", "string" },
   { "WIFI_PASSWORD",         "network", "wifi", "WIFI_PASSWORD", "string" },
   { "WIFI_CONNECT_TIMEOUT",  "network", "wifi", "WIFI_CONNECT_TIMEOUT", "int" },
   //    mdns
   { "MDNS_NAME",             "network", "mdns", "MDNS_NAME", "string" },
   //    ntp
   { "NTP_UPDATE_TIMEOUT",    "network", "ntp", "NTP_UPDATE_TIMEOUT", "int" },
   //    udp
   { "BROADCAST_UDP_PORT",    "network", "udp", "BROADCAST_UDP_PORT", "int" },
   { "LISTEN_UDP_PORT",       "network", "udp", "LISTEN_UDP_PORT", "int" },

   // email
   //    smtp
   { "SMTP_HOST",             "email", "smtp", "SMTP_HOST", "string" },
   { "SMTP_PORT",             "email", "smtp", "SMTP_PORT", "int" },
   //    account
   { "ACCOUNT_EMAIL",         "email", "account", "ACCOUNT_EMAIL", "string" },
   { "ACCOUNT_PASSWORD",      "email", "account", "ACCOUNT_PASSWORD", "string" },
   //    delivery
   { "SEND_EMAILS",           "email", "delivery", "SEND_EMAILS", "bool" },
   { "RECIPIENT_EMAIL",       "email", "delivery", "RECIPIENT_EMAIL", "string" },
   { "DAILY_EMAIL_HOUR",      "email", "delivery", "DAILY_EMAIL_HOUR", "int" },

   // logging
   //    output
   { "ENABLE_SERIAL_LOGGING", "logging", "output", "ENABLE_SERIAL_LOGGING", "bool" },
   { "UDP_LOGGING_ENABLE",    "logging", "output", "UDP_LOGGING_ENABLE", "bool" },
   { "LOG_TO_UDP_PORT",       "logging", "output", "LOG_TO_UDP_PORT", "int" },
   //    levels
   { "DEBUG_LEVEL_ENABLED",   "logging", "levels", "DEBUG_LEVEL_ENABLED", "bool" },
   { "LOG_TIMESTAMP",         "logging", "levels", "LOG_TIMESTAMP", "bool" },
   { "LOG_TIMING",            "logging", "levels", "LOG_TIMING", "bool" },
   { "LOG_MEMSTATS",          "logging", "levels", "LOG_MEMSTATS", "bool" },
   { "LOG_HP_MODBUS",         "logging", "levels", "LOG_HP_MODBUS", "bool" },

   // integrations
   //    emoncms
   { "UPDATE_EMONCMS",        "integrations", "emoncms", "UPDATE_EMONCMS", "bool" },
   { "EMONCMS_APIKEY",        "integrations", "emoncms", "EMONCMS_APIKEY", "string" },
   { "EMON_INSECURE",         "integrations", "emoncms", "EMON_INSECURE", "bool" },
   //    openweather
   { "OPENWEATHER_INSECURE",  "integrations", "openweather", "OPENWEATHER_INSECURE", "bool" },

   // ui
   //    display
   { "USERIO_SCREENSAVER",    "ui", "display", "USERIO_SCREENSAVER", "int" },

   // debug
   //    web
   { "HIDDEN_WEB_PAGE",       "debug", "web", "HIDDEN_WEB_PAGE", "string" },
   { "WEBPAGE_DEBUG_SECTION", "debug", "web", "WEBPAGE_DEBUG_SECTION", "bool" },
   { "SHOW_ALL_FILES",        "debug", "web", "SHOW_ALL_FILES", "bool" },
   // debug
   //    hardware
   { "DEBUGPAGE_HWRESET",     "debug", "hardware", "DEBUGPAGE_HWRESET", "bool" },
   { "DEBUGPAGE_CPU0_TASKWDT","debug", "hardware", "DEBUGPAGE_CPU0_TASKWDT", "bool" },
   // debug
   //    diagnostics
   { "HEAP_TEST_SIZE",        "debug", "diagnostics", "HEAP_TEST_SIZE", "int" },
   { "ASSERT_FOR_FAST_BOOT",  "debug", "diagnostics", "ASSERT_FOR_FAST_BOOT", "int" },
   { "EARLY_BOOT_LOG",        "debug", "diagnostics", "EARLY_BOOT_LOG", "bool" },
   { "SEND_BOOT_LOG",         "debug", "diagnostics", "SEND_BOOT_LOG", "bool" },
   { nullptr, nullptr, nullptr, nullptr, nullptr }  // Sentinel
};

static const int k_numMappings = std::size(k_keyMappings) - 1;

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
//      PW_DEBUG( "Registry: no entry for %s",key );
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
//      PW_DEBUG( "Registry: no entry for %s",key );
      return defaultConfigString;
   }
   else
   {
      return( Config::m_entries[ index ].value );
   }
}

Config::Config()
      : m_isRegistryOk( false ),
        m_wasFastReboot( false )
{
   // Nothing in the registry yet...

   Config::numRegistryEntries = 0;

   // Start FS, may format filesystem if new board - need some thought on this
   // Alert the user and request a format.

   tvmgFileSys.begin( true );

   if ( tvmgFileSys )
   {
      PW_MSG( "Filesystem is ok" );
      // clear any previous boot log so we start fresh each run
      tvmgFileSys.remove( EARLY_BOOT_LOGFILE );
   }

   PW_MSG( "Config():" );
   PW_MSG( "  FS : used %d of %d",tvmgFileSys.usedBytes(),tvmgFileSys.totalBytes() );
   PW_MSG( "  Chip Model : %s [%d]", ESP.getChipModel(),ESP.getChipRevision() );
   PW_MSG( "  Firmware %s",k_versionStr );
}

Config::~Config()
{
}

Config   *Config::instance( bool create )
{
   if ( create && !s_instance )
   {
      Config   *newConfig = new Config();
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

// JSON migration and loading functions - should only need to migrate once, the
// flat config file will be removed on completion.

bool Config::migrateFromFlatFile()
{
   if ( !tvmgFileSys )
   {
      PW_WARN( "migrateFromFlatFile() : No filesystem !" );
      return false;
   }

   File srcFile = tvmgFileSys.open( FLAT_CONFIG_FILENAME, FILE_READ );
   if ( !srcFile )
   {
      PW_WARN( "migrateFromFlatFile() : Can't open %s",FLAT_CONFIG_FILENAME );
      return false;
   }

   // Create root JSON object
   cJSON *root = cJSON_CreateObject();
   if ( !root )
   {
      srcFile.close();
      PW_ERROR( "migrateFromFlatFile() : Failed to create root JSON" );
      return false;
   }

   // Read flat file line by line
   char line[ 128 ];
   char key[ MAX_KEY_LENGTH ];
   char value[ MAX_VALUE_LENGTH ];
   uint32_t size = srcFile.size();
   int numMigrated = 0;

   while ( srcFile.position() < size )
   {
      int length = srcFile.readBytesUntil( '\n', line, 128 );

      if ( length > 2 && line[ 0 ] != '#' && line[ 0 ] != '/' )
      {
         line[ length ] = 0;
         if ( sscanf( line, "%s %s", key, value ) == 2 )
         {
            stripOutQuotes( value );

            // Find the mapping for this key
            const KeyPathMapping *mapping = nullptr;
            for ( int i = 0; k_keyMappings[ i ].flatKey != nullptr; i++ )
            {
               if ( !strcmp( k_keyMappings[ i ].flatKey, key ) )
               {
                  mapping = &k_keyMappings[ i ];
                  break;
               }
            }

            if ( mapping )
            {
               // Navigate/create the JSON hierarchy
               cJSON *parent1 = cJSON_GetObjectItem( root, mapping->parent1 );
               if ( !parent1 )
               {
                  parent1 = cJSON_CreateObject();
                  cJSON_AddItemToObject( root, mapping->parent1, parent1 );
               }

               cJSON *parent2 = cJSON_GetObjectItem( parent1, mapping->parent2 );
               if ( !parent2 )
               {
                  parent2 = cJSON_CreateObject();
                  cJSON_AddItemToObject( parent1, mapping->parent2, parent2 );
               }

               // Add the value based on type
               if ( !strcmp( mapping->type, "int" ) )
               {
                  cJSON_AddNumberToObject( parent2, mapping->jsonKey, atoi( value ) );
               }
               else if ( !strcmp( mapping->type, "bool" ) )
               {
                  int boolVal = atoi( value );
                  cJSON_AddBoolToObject( parent2, mapping->jsonKey, boolVal != 0 );
               }
               else
               {
                  cJSON_AddStringToObject( parent2, mapping->jsonKey, value );
               }

               numMigrated++;
               PW_DEBUG( "Migrated: %s → %s.%s", key, mapping->parent1, mapping->parent2 );
            }
            else
            {
               PW_WARN( "migrateFromFlatFile(): Unknown key: %s", key );
            }
         }
      }
   }

   srcFile.close();

   // Write JSON to config.json
   char *jsonString = cJSON_PrintUnformatted( root );
   if ( !jsonString )
   {
      cJSON_Delete( root );
      PW_ERROR( "migrateFromFlatFile() : Failed to serialize JSON" );
      return false;
   }

   File dstFile = tvmgFileSys.open( JSON_CONFIG_FILENAME, FILE_WRITE );
   if ( !dstFile )
   {
      cJSON_free( jsonString );
      cJSON_Delete( root );
      PW_ERROR( "migrateFromFlatFile() : Can't create %s",JSON_CONFIG_FILENAME );
      return false;
   }

   size_t written = dstFile.print( jsonString );
   dstFile.close();

   cJSON_free( jsonString );
   cJSON_Delete( root );

   if ( written > 0 )
   {
      PW_MSG( "migrateFromFlatFile() : Successfully migrated %d keys to %s", numMigrated,JSON_CONFIG_FILENAME );
      tvmgFileSys.remove( FLAT_CONFIG_FILENAME );
      return true;
   }
   else
   {
      PW_ERROR( "migrateFromFlatFile() : Failed to write JSON file" );
      return false;
   }
}

// Read config.json and populate the registry
// Returns true if successful

bool Config::loadFromJSON()
{
   cJSON *root = readJSONFromFile( JSON_CONFIG_FILENAME );

   if ( !root )
   {
      return false;
   }

   int numLoaded = 0;

   // Iterate through all mapped keys and extract from JSON
   for ( int i = 0; k_keyMappings[ i ].flatKey != nullptr; i++ )
   {
      const KeyPathMapping *mapping = &k_keyMappings[ i ];

      // Navigate the JSON hierarchy
      cJSON *parent1 = cJSON_GetObjectItem( root, mapping->parent1 );
      if ( !parent1 )
         continue;

      cJSON *parent2 = cJSON_GetObjectItem( parent1, mapping->parent2 );
      if ( !parent2 )
         continue;

      cJSON *item = cJSON_GetObjectItem( parent2, mapping->jsonKey );
      if ( !item )
         continue;

      char strValue[ MAX_VALUE_LENGTH ];

      // Extract value based on type
      if ( !strcmp( mapping->type, "int" ) )
      {
         snprintf( strValue, MAX_VALUE_LENGTH, "%d", item->valueint );
      }
      else if ( !strcmp( mapping->type, "bool" ) )
      {
         snprintf( strValue, MAX_VALUE_LENGTH, "%d", item->valueint );
      }
      else
      {
         if ( item->valuestring )
         {
            snprintf( strValue, MAX_VALUE_LENGTH, "\"%s\"", item->valuestring );
         }
         else
         {
            continue;
         }
      }

      // Add to registry
      setRegistryEntry( (char *)mapping->flatKey, strValue );
      numLoaded++;
   }

   cJSON_Delete( root );

   if ( numLoaded > 0 )
   {
      PW_MSG( "loadFromJSON() : Successfully loaded %d keys from /config.json", numLoaded );
      return true;
   }

   return false;
}

bool  Config::readRegistryFromFile( void )
{
   if ( !tvmgFileSys )
   {
      PW_WARN( "readFromFile() : No filesystem !" );
      return false;
   }

   // First, try to load from JSON config (preferred format)
   if ( tvmgFileSys.exists( JSON_CONFIG_FILENAME ) )
   {
      PW_MSG( "readRegistryFromFile() : Found %s, loading from JSON",JSON_CONFIG_FILENAME );
      if ( loadFromJSON() )
      {
         return true;
      }
      PW_WARN( "readRegistryFromFile() : JSON load failed, falling back to flat file" );
   }

   // If config.json doesn't exist, check if config.dat exists
   if ( tvmgFileSys.exists( FLAT_CONFIG_FILENAME ) )
   {
      PW_MSG( "readRegistryFromFile() : Found %s",FLAT_CONFIG_FILENAME );

      // Load from the flat file first
      bool flatFileOk = readRegistryFromFlatFile();

      if ( flatFileOk )
      {
         // Try to migrate to JSON for future use
         PW_MSG( "readRegistryFromFile() : Flat file loaded successfully, migrating to JSON..." );
         if ( migrateFromFlatFile() )
         {
            PW_MSG( "readRegistryFromFile() : Migration to %s successful",JSON_CONFIG_FILENAME );
         }
         else
         {
            PW_WARN( "readRegistryFromFile() : Migration to JSON failed, but flat file is available" );
         }

         return true;
      }
      else
      {
         PW_ERROR( "readRegistryFromFile() : Failed to load from flat file" );
         return false;
      }
   }

   PW_ERROR( "readRegistryFromFile() : Neither %s nor %s found",JSON_CONFIG_FILENAME,FLAT_CONFIG_FILENAME );
   return false;
}

bool  Config::readRegistryFromFlatFile( void )
{
   // Original flat file reading logic, refactored into separate function

   if ( !tvmgFileSys )
   {
      PW_WARN( "readRegistryFromFlatFile() : No filesystem !" );
      return false;
   }

   File file = tvmgFileSys.open( FLAT_CONFIG_FILENAME, FILE_READ );

   if ( !file )
   {
      PW_WARN( "%s can't open", FLAT_CONFIG_FILENAME );
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
      PW_WARN( "Read maximum %d entries from %s", numLines, FLAT_CONFIG_FILENAME );
   }

   return( numLines > 0 );
}

bool  Config::isFactoryReset()
{
   int32_t rebootReason;

   getPersistentInt( k_rebootType,&rebootReason,0 );

   return ( rebootReason == SERVER_RESET );
}

bool  Config::didRebootNoWiFi()
{
   int32_t rebootReason;

   getPersistentInt( k_rebootType,&rebootReason,0 );

   return ( rebootReason == BOOT_NO_WIFI );
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

   // We turn off any AP mode indicator here, as we may be soft resetting 

   Indicator *apModeIndicator = Indicator::getIndicator( Indicator::SYSTEM,SYSTEM_AP_ID );
   if (apModeIndicator)
   {
      apModeIndicator->off();
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
   // soft reset counters and last cycle time.  We also reset if we've had an
   // OTA update or server reboot

   if ( reboot == POWER_CYCLE || reboot == SERVER_OTA_UPDATE || reboot == SERVER_REBOOT  || reboot == SERVER_RESET )
   {
      PW_DEBUG( "Resetting soft reboot data" );
      s_softResets = 0;
      s_earlyResets = 0;
      lastCycleSecs = 0;
   }

   // Set the lastResetSeconds to this cycle start time

   s_lastResetSeconds = secs;

   // if the time since the last soft reset was greater than a minimum we reset
   // soft reset counters again

   if ( lastCycleSecs > MINIMUM_RUNTIME_SECS )
   {
      PW_DEBUG( "Long last cycle, resetting data" );
      s_softResets = 0;
      s_earlyResets = 0;
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
   PW_MSG( "Early Resets   : %u",s_earlyResets );
   PW_MSG( "-----------------------------\n" );

   if ( s_softResets >= ALLOWED_FAST_RESETS )
   {
      PW_ERROR( "Too many fast resets" );
      m_wasFastReboot = true;
   }

   *type = reboot;

   appReason += " (ESP32 - ";
   appReason += getESPRebootReason( espReason );
   appReason += ")";

   return( appReason );
}

bool Config::wasFastReboot()
{
   return m_wasFastReboot;
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

   delay( 500 );

   PW_DEBUG( "out hwReset" );
}

void reboot()
{
   Config::instance()->setPersistentInt( k_rebootType,SERVER_REBOOT );

   // We turn off any AP mode indicator here, as we may be soft resetting 

   Indicator *apModeIndicator = Indicator::getIndicator( Indicator::SYSTEM,SYSTEM_AP_ID );
   if (apModeIndicator)
   {
      apModeIndicator->off();
   }

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
