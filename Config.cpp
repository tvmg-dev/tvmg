#include "Config.h"

#include "Storage.h"

#include <SD.h>

Config   *s_instance = nullptr;

uint8_t Config::numRegistryEntries = 0;

KeyValue  Config::m_entries[ MAX_REGISTRY_ENTRIES ];

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

      PW_MSG( "New Registry : %s : %s",key,Config::m_entries[ Config::numRegistryEntries ].value );

      Config::numRegistryEntries++;
   }
}

int32_t getRegistryInt( char *key )
{
   int index = findKey( key );

   if ( index == -1 )
   {
      PW_WARN( "No entry found for %s",key );
      return 0;
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
      PW_WARN( "No entry found for %s",key );
      return nullptr;
   }
   else
   {
      return( Config::m_entries[ index ].value );
   }
}

Config::Config( char *fileName )
      : m_spiffs( new SPIFFSFS() ),
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
      m_spiffs->begin( true );
      PW_MSG( "Config() SPIFFS : %d %d",m_spiffs->totalBytes(),m_spiffs->usedBytes() );
      PW_MSG( "Config() Chip Model : %s [%d]", ESP.getChipModel(),ESP.getChipRevision() );
   }

   strncpy( m_configFileName,fileName,MAX_FILENAME );
}

Config::~Config()
{
}

Config   *Config::instance()
{
   if ( !s_instance )
   {
      s_instance = new Config( "/config.dat" );
      s_instance->initialise();
   }

   return( s_instance );
}


void Config::initialise( void )
{
   if ( !readRegistryFromFile() )
   {
      populateRegistry();
   }
}

fs::SPIFFSFS *Config::getSPIFFS()
{
   return( m_spiffs );
}

void Config::populateRegistry( void )
{
   PW_WARN( "Populating Default Registry" );

   // Need to populate the registry, clear it first

   for ( int i = 0; i < MAX_REGISTRY_ENTRIES; i++ )
   {
      m_entries[ i ].key[ 0 ] = 0;
      m_entries[ i ].value[ 0 ] = 0;
   }

   SET_REGISTRY( BOOT_DELAY,3000 );
#if PW_WIFI == 1
   SET_REGISTRY( WIFI_SSID,"BTHub6-6P5C-5G" );
   SET_REGISTRY( WIFI_PASSWORD,"***REMOVED***" );
#else
   SET_REGISTRY( WIFI_SSID,"PLUSNET-NSC395" );
   SET_REGISTRY( WIFI_PASSWORD,"***REMOVED***" );
#endif
   SET_REGISTRY( WIFI_CONNECT_TIMEOUT,60000 );
   SET_REGISTRY( NTP_UPDATE_TIMEOUT,60000 );
   SET_REGISTRY( SMTP_HOST,"send.one.com" );
   SET_REGISTRY( SMTP_PORT,465 );
   SET_REGISTRY( ACCOUNT_EMAIL,"heatpump@dyllysplace.com" );
   SET_REGISTRY( ACCOUNT_PASSWORD,"***REMOVED***" );
   SET_REGISTRY( RECIPIENT_EMAIL,"heatpump@dyllysplace.com" );

   writeRegistryToFile();
}

void  Config::writeRegistryToFile( void )
{
   if ( !m_spiffs )
   {
      PW_WARN( "writeRegistryToFile() : No SPIFFS !" );
      return;
   }

   File file = m_spiffs->open( m_configFileName,FILE_WRITE );
   if ( !file )
   {
      PW_WARN( "Cannot write to %s",m_configFileName );
      return;
   }

   PW_MSG( "Writing registry to %s",m_configFileName );

   for ( int i = 0; i < Config::numRegistryEntries; i++ )
   {
      char msg[ 128 ];
      sprintf( msg,"%s \"%s\"",Config::m_entries[ i ].key,Config::m_entries[ i ].value );

      if ( ! file.println( msg ) )
      {
         PW_WARN( "Failed to write to config : %s",msg );
      }
   }

   file.close();
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
      PW_WARN( "%s not present, using registry defaults",m_configFileName );
      return false;
   }

   bool     fileOk = true;
   char     line[ 128 ];
   KeyValue keyVal;
   uint8_t  numLines = 0;
   uint8_t  length;

   // scan the configuration file, replace any default values for keys
   // found that have already been registered - or create a new registry entry

   while ( fileOk && numLines < MAX_REGISTRY_ENTRIES )
   {
      length = file.readBytesUntil( '\n',line,128 );
      if ( length && length < 127 )
      {
         // we skip lines of single length or that start with / or #

         if ( length > 1 && ( line [ 0 ] != '#' && line [ 0 ] != '/') )
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
      else
      {
         fileOk = false;
      }
   }

   file.close();

   if ( numLines == MAX_REGISTRY_ENTRIES )
   {
      PW_WARN( "Read maximum %d entries from %s",numLines,m_configFileName );
   }

   return( numLines > 0 );
}

bool  Config::getInt( char *key,int32_t *intValue )
{

}

bool  Config::getString( char *key,char *strValue )
{
}
