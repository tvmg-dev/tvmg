#include <Preferences.h>
#include <map>

#include "Config.h"

Config   *s_instance = nullptr;

char  defaultConfigString[] = "unknown";

uint8_t Config::numRegistryEntries = 0;

KeyValue  Config::m_entries[ MAX_REGISTRY_ENTRIES ];

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
      : m_spiffs( &SPIFFS ),
        m_configFileName()
{
   // Nothing in the registry yet...

   Config::numRegistryEntries = 0;

   // Instantiate spiffs for config file
   if ( !m_spiffs )
   {
      PW_WARN( "No SPIFFS instantiated" );
   }
   else
   {
      // Start spiffs, may format filesystem if new board

      m_spiffs->begin( true );
      PW_MSG( "Config():" );
      PW_MSG( "  SPIFFS : used %d of %d",m_spiffs->usedBytes(),m_spiffs->totalBytes() );
      PW_MSG( "  Chip Model : %s [%d]", ESP.getChipModel(),ESP.getChipRevision() );
      PW_MSG( "  Firmware %s",VERSION_STR );
   }

   strncpy( m_configFileName,fileName,MAX_FILENAME );

   initialise();
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
   readRegistryFromFile();
}

bool Config::isRegistryAvailable()
{
   return( Config::numRegistryEntries > 0 );
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

#define  FACTORY_RESET_MARKER "/factory.res"

bool  Config::isFactoryReset()
{
   bool  isReset = false;

   if ( m_spiffs && m_spiffs->exists( FACTORY_RESET_MARKER ) )
   {
      isReset = true;
   }

   return isReset;
}

void  Config::clearFactoryReset()
{
   if ( m_spiffs )
   {
      m_spiffs->remove( FACTORY_RESET_MARKER );
   }
}

void  Config::setFactoryReset()
{
   if ( m_spiffs )
   {
      File resetFile = m_spiffs->open( FACTORY_RESET_MARKER,"w" );
      if ( resetFile )
      {
         resetFile.println( "reset" );
         resetFile.close();
      }

      // we also reset the reboot counts

      setPersistentInt( k_rebootCounter,0 );
      setPersistentInt( k_noNetworkCounter,0 );
   }
}

// info under system namespace in non-volatile store
//
// rebootCount
// lastRebootType
//

const char k_nvsNamespace[] = "sysinfo";

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

static std::map<RebootType,String> resetMap = {
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
   { UNKNOWN,"Unknown" }
};

RTC_NOINIT_ATTR   uint32_t s_fastResets;

String  Config::getRebootReason( RebootType *type )
{
   RebootType reboot;
   int32_t  rebootReason;

   // get the ESP reason for reboot, and get out reboot marker from nvs

   esp_reset_reason_t espReason = esp_reset_reason();
   (void) getPersistentInt( k_rebootType,&rebootReason );

   // now set our reboot marker for this cycle

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
            reboot = ESP32_WATCHDOG;
            break;
      case ESP_RST_SW:
            reboot = static_cast<RebootType> (rebootReason);
            break;
      default:
            reboot = UNKNOWN;
            break;
   }

   *type = reboot;
   std::map<RebootType,String>::const_iterator it = resetMap.find( reboot );
   if ( it == resetMap.end() )
   {
      return( String( "reason not mapped" ) );
   }
   else
   {
      return( it->second );
   }
}

bool Config::isFastReset()
{
   bool isFast = false;

   return isFast;
}
