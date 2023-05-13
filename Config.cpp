#include "Config.h"

#include "Storage.h"

#include <SD.h>
#include <FS.h>

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

void  setRegistryEntry( char *key,char *value )
{
   if ( Config::numRegistryEntries < MAX_REGISTRY_ENTRIES -1 )
   {
      strcpy( Config::m_entries[ Config::numRegistryEntries ].key,key );
      strcpy( Config::m_entries[ Config::numRegistryEntries ].value,value );

      stripOutQuotes( Config::m_entries[ Config::numRegistryEntries ].value );

      PW_MSG( "New Registry : %s : %s",key,Config::m_entries[ Config::numRegistryEntries ].value );

      Config::numRegistryEntries++;
   }
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

Config::Config( char *fileName,Storage *storage )
      : m_configFileName(),
        m_storageModule( storage )
{
   strncpy( m_configFileName,fileName,MAX_FILENAME );
}

Config::~Config()
{
}

void Config::initialise( void )
{
   populateRegistry();
   readFromFile();
}

void Config::populateRegistry( void )
{
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
}

void  Config::readFromFile( void )
{
   File file = SD.open( m_configFileName,FILE_READ );
   if ( !file )
   {
      PW_WARN( "%s not present, using registry defaults",m_configFileName );
      return;
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
               int index;

               stripOutQuotes( keyVal.value );

               index = findKey( keyVal.key );

               if ( index >= -1 )
               {
#if CONFIG_FILE_PRECENDENCE == 1
                  replaceRegistryValue( index,keyVal.value );
#else
                  setRegistryEntry( keyVal.key,keyVal.value );
#endif
               }
               else
               {
                  setRegistryEntry( keyVal.key,keyVal.value );
               }

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
}

bool  Config::getInt( char *key,int32_t *intValue )
{

}

bool  Config::getString( char *key,char *strValue )
{
}
