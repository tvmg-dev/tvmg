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

      PW_DEBUG( "Set Registry : %s : %s",key,Config::m_entries[ Config::numRegistryEntries ].value );

      Config::numRegistryEntries++;
   }
}

int32_t getRegistryInt( char *key )
{
   int index = findKey( key );

   if ( index == -1 )
   {
      PW_WARN( "No entry found for %s",key );
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
        m_configFileName(),
        m_registryAvailable( false )
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

Config   *Config::instance()
{
   // prevent recursion as the logging uses the Config registry to
   // determine whether to o/p anything.

   static bool isCreating = false;

   if ( !s_instance && !isCreating )
   {
      isCreating = true;
      s_instance = new Config( "/config.dat" );

      isCreating = false;
   }

   return( s_instance );
}


void Config::initialise()
{
   readRegistryFromFile();
   m_registryAvailable = true;
}

bool Config::isRegistryAvailable()
{
   return( Config::numRegistryEntries > 0 );
}

fs::SPIFFSFS *Config::getSPIFFS()
{
   return( m_spiffs );
}

void  Config::writeRegistryToFile()
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
      PW_WARN( "Config: %s not present",m_configFileName );
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
