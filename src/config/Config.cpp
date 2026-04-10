/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#include "src/config/Config.h"

#include <cJSON.h>

const char* k_versionStr = "v26.04.01j";

static Config* s_instance = nullptr;

#define MAX_CONFIG_FILESIZE ( 8 * 1024 )
#define JSON_CONFIG_FILENAME "/config.json"

//----------------------------------------------------------------------
// The original registry was in a flat file, some devices may need to
// migrate to new json format

#define FLAT_CONFIG_FILENAME "/config.dat"

static bool migrateFromFlatFile();

//----------------------------------------------------------------------
// Registry key to JSON path mapping
// Maps flat registry keys to their hierarchical JSON location

struct KeyPathMapping
{
   const char* flatKey;  // Used in GET/SET REGISTRY macros
   const char* parent1;  // First level (e.g., "network")
   const char* parent2;  // Second level (e.g., "wifi")
   const char* jsonKey;  // Key name in JSON (usually same as flatKey)
   const char* type;     // "string", "int", or "bool"
};

static const KeyPathMapping k_keyMappings[] = {
   // update
   { "OTA_MANIFEST_URL",     "update", "delivery", "OTA_MANIFEST_URL", "string" },
   { "UPDATE_MODE",          "update", "delivery", "UPDATE_MODE", "string" },
   { "UPDATE_GROUP",         "update", "delivery", "UPDATE_GROUP", "string" },
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
   { "USERIO_SCREENSAVER",    "ui", "display", "USERIO_SCREENSAVER", "bool" },

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

static const int k_numMappings = std::size( k_keyMappings ) - 1;

//----------------------------------------------------------------------
// Registry utilities - should really replace with cJSON

// As we use "a value" to refer to strings, as is it more conventional, then
// we have to strip out these " characters otherwise they are part of the string

static void stripOutQuotes( String& str )
{
   str.replace( "\"", "" );
}

Config::Config()
    : m_numRegistryEntries( 0 ),
      m_isRegistryOk( false )
{
   // Nothing in the registry yet, so initialise it

   Config::m_numRegistryEntries = 0;

   for ( int i = 0; i < MAX_REGISTRY_ENTRIES; i++ )
   {
      KeyValue& entry = m_entries[i];
      entry.key = "";
      entry.value = "";
      entry.volatileValue = "";
   }

   // Start FS, may format filesystem if new board - need some thought on this
   // Alert the user and request a format.

   tvmgFileSys.begin( true );

   if ( tvmgFileSys )
   {
      TVMG_MSG( "Filesystem is ok" );
      // clear any previous boot log so we start fresh each run
      tvmgFileSys.remove( EARLY_BOOT_LOGFILE );
   }

   TVMG_MSG( "Config():" );
   TVMG_MSG( "  FS : used %d of %d", tvmgFileSys.usedBytes(), tvmgFileSys.totalBytes() );
   TVMG_MSG( "  Chip Model : %s [%d]", ESP.getChipModel(), ESP.getChipRevision() );
   TVMG_MSG( "  Firmware %s", k_versionStr );
}

Config::~Config()
{
}

Config* Config::instance( bool create )
{
   if ( create && !s_instance )
   {
      Config* newConfig = new Config();
      s_instance = newConfig;
   }

   return ( s_instance );
}

void Config::initialise()
{
   m_isRegistryOk = readRegistryFromFile();
}

bool Config::isRegistryAvailable()
{
   return ( m_isRegistryOk );
}

// Persist current in‑memory registry state to JSON file.  Returns true on
// success, false otherwise; callers will typically log failure but otherwise
// ignore it.  The implementation mirrors loadFromJSON() but in reverse.

bool Config::writeRegistryToJSON()
{
   if ( !tvmgFileSys )
   {
      TVMG_WARN( "writeRegistryToJSON() : No filesystem !" );
      return false;
   }

   cJSON* root = cJSON_CreateObject();
   if ( !root )
   {
      TVMG_ERROR( "writeRegistryToJSON() : Failed to create root JSON" );
      return false;
   }

   for ( int i = 0; k_keyMappings[i].flatKey != nullptr; i++ )
   {
      const KeyPathMapping* mapping = &k_keyMappings[i];
      int idx = findKey( (char*)mapping->flatKey );

      if ( idx < 0 )
      {
         continue;  // entry not present
      }

      // Don't write out a purely volatile entry, i.e. no value assigned - otheriwse
      // we write out the persisted value.

      KeyValue& entry = m_entries[idx];
      if ( entry.value.length() == 0 )
      {
         TVMG_DEBUG( "Ignoring empty %s (v=%s) (vol=%s)", entry.key.c_str(), entry.value.c_str(), entry.volatileValue.c_str() );
         continue;
      }

      // ensure parent objects exist
      cJSON* parent1 = cJSON_GetObjectItem( root, mapping->parent1 );
      if ( !parent1 )
      {
         parent1 = cJSON_CreateObject();
         cJSON_AddItemToObject( root, mapping->parent1, parent1 );
      }

      cJSON* parent2 = cJSON_GetObjectItem( parent1, mapping->parent2 );
      if ( !parent2 )
      {
         parent2 = cJSON_CreateObject();
         cJSON_AddItemToObject( parent1, mapping->parent2, parent2 );
      }

      const char* val = entry.value.c_str();

      if ( !strcmp( mapping->type, "int" ) )
      {
         cJSON_AddNumberToObject( parent2, mapping->jsonKey, atoi( val ) );
      }
      else if ( !strcmp( mapping->type, "bool" ) )
      {
         int boolVal = atoi( val );
         cJSON_AddBoolToObject( parent2, mapping->jsonKey, boolVal != 0 );
      }
      else
      {
         cJSON_AddStringToObject( parent2, mapping->jsonKey, val );
      }
   }

   char* jsonString = cJSON_PrintUnformatted( root );
   if ( !jsonString )
   {
      cJSON_Delete( root );
      TVMG_ERROR( "writeRegistryToJSON() : Failed to serialize JSON" );
      return false;
   }

   File dstFile = tvmgFileSys.open( JSON_CONFIG_FILENAME, FILE_WRITE );
   if ( !dstFile )
   {
      cJSON_free( jsonString );
      cJSON_Delete( root );
      TVMG_ERROR( "writeRegistryToJSON() : Can't open %s for writing", JSON_CONFIG_FILENAME );
      return false;
   }

   size_t written = dstFile.print( jsonString );
   dstFile.close();

   cJSON_free( jsonString );
   cJSON_Delete( root );

   if ( written == 0 )
   {
      TVMG_ERROR( "writeRegistryToJSON() : zero bytes written" );
      return false;
   }

   TVMG_MSG( "writeRegistryToJSON() : wrote %u bytes", (unsigned)written );
   return true;
}

bool Config::loadFromJSON()
{
   cJSON* root = readJSONFromFile( JSON_CONFIG_FILENAME );

   if ( !root )
   {
      return false;
   }

   // Iterate through all mapped keys and extract from JSON
   for ( int i = 0; k_keyMappings[i].flatKey != nullptr; i++ )
   {
      const KeyPathMapping* mapping = &k_keyMappings[i];

      // Navigate the JSON hierarchy
      cJSON* parent1 = cJSON_GetObjectItem( root, mapping->parent1 );
      if ( !parent1 )
         continue;

      cJSON* parent2 = cJSON_GetObjectItem( parent1, mapping->parent2 );
      if ( !parent2 )
         continue;

      cJSON* item = cJSON_GetObjectItem( parent2, mapping->jsonKey );
      if ( !item )
         continue;

      char strValue[MAX_VALUE_LENGTH];

      // Extract value based on type
      if ( !strcmp( mapping->type, "int" ) )
      {
         snprintf( strValue, sizeof( strValue ), "%d", item->valueint );
      }
      else if ( !strcmp( mapping->type, "bool" ) )
      {
         snprintf( strValue, sizeof( strValue ), "%d", item->valueint );
      }
      else
      {
         if ( item->valuestring )
         {
            snprintf( strValue, sizeof( strValue ), "\"%s\"", item->valuestring );
         }
         else
         {
            continue;
         }
      }

      TVMG_DEBUG( "key %s value %s", mapping->flatKey, strValue );
      // Add to registry
      setRegistryEntry( (char*)mapping->flatKey, strValue, false );
   }

   cJSON_Delete( root );

   if ( m_numRegistryEntries > 0 )
   {
      TVMG_MSG( "loadFromJSON() : Successfully loaded %d keys from /config.json", m_numRegistryEntries );
      return true;
   }

   return false;
}

bool Config::readRegistryFromFile()
{
   if ( !tvmgFileSys )
   {
      TVMG_WARN( "readFromFile() : No filesystem !" );
      return false;
   }

   // First, try to load from JSON config (preferred format)
   if ( tvmgFileSys.exists( JSON_CONFIG_FILENAME ) )
   {
      TVMG_MSG( "readRegistryFromFile() : Found %s, loading from JSON", JSON_CONFIG_FILENAME );
      if ( loadFromJSON() )
      {
         return true;
      }
      TVMG_WARN( "readRegistryFromFile() : JSON load failed, falling back to flat file" );
   }

   // If config.json doesn't exist, check if config.dat exists
   if ( tvmgFileSys.exists( FLAT_CONFIG_FILENAME ) )
   {
      TVMG_MSG( "readRegistryFromFile() : Found %s", FLAT_CONFIG_FILENAME );

      // Load from the flat file first
      bool flatFileOk = readRegistryFromFlatFile();

      if ( flatFileOk )
      {
         // Try to migrate to JSON for future use
         TVMG_MSG( "readRegistryFromFile() : Flat file loaded successfully, migrating to JSON..." );
         if ( migrateFromFlatFile() )
         {
            TVMG_MSG( "readRegistryFromFile() : Migration to %s successful", JSON_CONFIG_FILENAME );
         }
         else
         {
            TVMG_WARN( "readRegistryFromFile() : Migration to JSON failed, but flat file is available" );
         }

         return true;
      }
      else
      {
         TVMG_ERROR( "readRegistryFromFile() : Failed to load from flat file" );
         return false;
      }
   }

   TVMG_ERROR( "readRegistryFromFile() : Neither %s nor %s found", JSON_CONFIG_FILENAME, FLAT_CONFIG_FILENAME );
   return false;
}

bool Config::readRegistryFromFlatFile()
{
   // Original flat file reading logic, refactored into separate function

   if ( !tvmgFileSys )
   {
      TVMG_WARN( "readRegistryFromFlatFile() : No filesystem !" );
      return false;
   }

   File file = tvmgFileSys.open( FLAT_CONFIG_FILENAME, FILE_READ );

   if ( !file )
   {
      TVMG_WARN( "%s can't open", FLAT_CONFIG_FILENAME );
      return false;
   }

   char line[128];
   char key[MAX_KEY_LENGTH];
   char value[MAX_VALUE_LENGTH];

   uint8_t numLines = 0;
   uint8_t length;
   uint32_t size = file.size();

   // scan the configuration file, replace any default values for keys
   // found that have already been registered - or create a new registry entry

   while ( file.position() < size && numLines < MAX_REGISTRY_ENTRIES )
   {
      length = file.readBytesUntil( '\n', line, 128 );

      // The length would be zero if not CRLF, whilst 1 if just CR,
      // editing a file via HTML form editor will ensure CRLF endings,
      // so we skip lines of length less than 2.
      if ( length > 1 && length < 127 )
      {
         // skip lines that start with / or #

         if ( ( line[0] != '#' && line[0] != '/' ) )
         {
            line[length] = 0;
            if ( sscanf( line, "%s %s", key, value ) == 2 )
            {
               String valueStr = value;
               stripOutQuotes( valueStr );
               setRegistryEntry( key, valueStr.c_str(), false );
               numLines++;
            }
         }
      }
   }

   file.close();

   if ( numLines == MAX_REGISTRY_ENTRIES )
   {
      TVMG_WARN( "Read maximum %d entries from %s", numLines, FLAT_CONFIG_FILENAME );
   }

   return ( numLines > 0 );
}

int Config::findKey( const char* key )
{
   for ( int i = 0; i < m_numRegistryEntries; i++ )
   {
      if ( m_entries[i].key == key )
      {
         return i;
      }
   }

   return -1;
}

void Config::setRegistryEntry( const char* key, const char* value, bool isVolatile )
{
   if ( !s_instance )
   {
      TVMG_ERROR( "No Config instance to set %s", key );
      return;
   }

   bool addingEntry = false;

   // If the key doesn't exist then add to the registry is space available

   int index = s_instance->findKey( key );

   if ( index == -1 )
   {
      if ( s_instance->m_numRegistryEntries == MAX_REGISTRY_ENTRIES )
      {
         TVMG_ERROR( "Registry is full, can't add %s", key );
         return;
      }
      index = s_instance->m_numRegistryEntries;
      s_instance->m_numRegistryEntries++;
      addingEntry = true;
   }

   KeyValue& entry = s_instance->m_entries[index];

   // If adding then set the key for the entry
   if ( addingEntry )
   {
      entry.key = key;
   }

   // Are we updating the volatile value or the persisting value, if persisting then clear
   // any volatile value

   String& currentValue = isVolatile ? entry.volatileValue : entry.value;
   if ( !isVolatile && entry.volatileValue.length() > 0 )
   {
      entry.volatileValue = "";
   }

   // Don't need to update if the same value
   if ( currentValue == value )
   {
      TVMG_DEBUG( "Ignore registry change, same value (%s) for %s", value, key );
      return;
   }
   // Now update the relevant value, stripping out any quotes

   currentValue = value;
   stripOutQuotes( currentValue );
   TVMG_DEBUG( "Set Registry %s: %s : %s", ( isVolatile ? "(volatile)" : "" ),
               key, currentValue.c_str() );

   // Only save the (non-volatile) value to file if the registry is ok

   if ( !isVolatile && s_instance->m_isRegistryOk )
   {
      s_instance->writeRegistryToJSON();
   }
}

void Config::setRegistryInt( const char* key, int32_t value, bool shouldPersist )
{
   if ( s_instance )
   {
      char valueStr[12];
      itoa( value, valueStr, 10 );
      setRegistryEntry( key, valueStr, shouldPersist );
   }
}

int32_t Config::getRegistryInt( const char* key )
{
   if ( s_instance )
   {
      const char* str = getRegistryString( key );

      if ( strlen( str ) )
      {
         return ( atoi( str ) );
      }
   }

   return -1;
}

const char* Config::getRegistryString( const char* key )
{
   static char defaultConfigString[] = "";

   if ( !s_instance )
   {
      return defaultConfigString;
   }

   int index = s_instance->findKey( key );

   if ( index == -1 )
   {
      TVMG_DEBUG( "Registry: no entry for %s", key );
      return defaultConfigString;
   }
   else
   {
      // A volatile value overrides a persisted value

      KeyValue& entry = s_instance->m_entries[index];
      if ( entry.volatileValue.length() > 0 )
      {
         return entry.volatileValue.c_str();
      }
      return entry.value.c_str();
   }
}

//------------------------------------------------------------------------------
// JSON migration - should only need to migrate once, the flat config file will be removed on completion.

bool migrateFromFlatFile()
{
   if ( !tvmgFileSys )
   {
      TVMG_WARN( "migrateFromFlatFile() : No filesystem !" );
      return false;
   }

   File srcFile = tvmgFileSys.open( FLAT_CONFIG_FILENAME, FILE_READ );
   if ( !srcFile )
   {
      TVMG_WARN( "migrateFromFlatFile() : Can't open %s", FLAT_CONFIG_FILENAME );
      return false;
   }

   // Create root JSON object
   cJSON* root = cJSON_CreateObject();
   if ( !root )
   {
      srcFile.close();
      TVMG_ERROR( "migrateFromFlatFile() : Failed to create root JSON" );
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

      if ( length > 2 && line[0] != '#' && line[0] != '/' )
      {
         line[ length ] = 0;
         if ( sscanf( line, "%s %s", key, value ) == 2 )
         {
            String valueStr = value;
            stripOutQuotes( valueStr );

            // Find the mapping for this key
            const KeyPathMapping* mapping = nullptr;
            for ( int i = 0; k_keyMappings[i].flatKey != nullptr; i++ )
            {
               if ( !strcmp( k_keyMappings[ i ].flatKey, key ) )
               {
                  mapping = &k_keyMappings[i];
                  break;
               }
            }

            if ( mapping )
            {
               // Navigate/create the JSON hierarchy
               cJSON* parent1 = cJSON_GetObjectItem( root, mapping->parent1 );
               if ( !parent1 )
               {
                  parent1 = cJSON_CreateObject();
                  cJSON_AddItemToObject( root, mapping->parent1, parent1 );
               }

               cJSON* parent2 = cJSON_GetObjectItem( parent1, mapping->parent2 );
               if ( !parent2 )
               {
                  parent2 = cJSON_CreateObject();
                  cJSON_AddItemToObject( parent1, mapping->parent2, parent2 );
               }

               // Add the value based on type
               if ( !strcmp( mapping->type, "int" ) )
               {
                  cJSON_AddNumberToObject( parent2, mapping->jsonKey, atoi( valueStr.c_str() ) );
               }
               else if ( !strcmp( mapping->type, "bool" ) )
               {
                  int boolVal = atoi( valueStr.c_str() );
                  cJSON_AddBoolToObject( parent2, mapping->jsonKey, boolVal != 0 );
               }
               else
               {
                  cJSON_AddStringToObject( parent2, mapping->jsonKey, valueStr.c_str() );
               }

               numMigrated++;
               TVMG_DEBUG( "Migrated: %s → %s.%s", key, mapping->parent1, mapping->parent2 );
            }
            else
            {
               TVMG_WARN( "migrateFromFlatFile(): Unknown key: %s", key );
            }
         }
      }
   }

   srcFile.close();

   // Write JSON to config.json
   char* jsonString = cJSON_PrintUnformatted( root );
   if ( !jsonString )
   {
      cJSON_Delete( root );
      TVMG_ERROR( "migrateFromFlatFile() : Failed to serialize JSON" );
      return false;
   }

   File dstFile = tvmgFileSys.open( JSON_CONFIG_FILENAME, FILE_WRITE );
   if ( !dstFile )
   {
      cJSON_free( jsonString );
      cJSON_Delete( root );
      TVMG_ERROR( "migrateFromFlatFile() : Can't create %s", JSON_CONFIG_FILENAME );
      return false;
   }

   size_t written = dstFile.print( jsonString );
   dstFile.close();

   cJSON_free( jsonString );
   cJSON_Delete( root );

   if ( written > 0 )
   {
      TVMG_MSG( "migrateFromFlatFile() : Successfully migrated %d keys to %s", numMigrated, JSON_CONFIG_FILENAME );
      tvmgFileSys.remove( FLAT_CONFIG_FILENAME );
      return true;
   }
   else
   {
      TVMG_ERROR( "migrateFromFlatFile() : Failed to write JSON file" );
      return false;
   }
}
