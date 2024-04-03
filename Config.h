#ifndef __CONFIG_H
#define __CONFIG_H

#include <SPIFFS.h>

#include "utils.h"

#define VERSION_STR        "v8.01d"

#define TEMPERATURE_BOARD  1
#define MASTER_BOARD       2

#define SAMPLING_PERIOD_MS 30000

#define MAX_REGISTRY_ENTRIES 32
#define MAX_KEY_LENGTH       64
#define MAX_VALUE_LENGTH     64

extern void    setRegistryEntry( char *key,char *value );
extern int32_t getRegistryInt( char *key );
extern char    *getRegistryString( char *key );

#define CONFIG_DEF_TO_STR( x ) #x

#define SET_REGISTRY( x,y )      setRegistryEntry( CONFIG_DEF_TO_STR( x ),CONFIG_DEF_TO_STR( y ) )
#define GET_REGISTRY_INT( x )    getRegistryInt( CONFIG_DEF_TO_STR( x ) )
#define GET_REGISTRY_STRING( x ) getRegistryString( CONFIG_DEF_TO_STR( x ) )

typedef struct {
   uint8_t  keyInt;
   char     key[ MAX_KEY_LENGTH ];
   char     value[ MAX_VALUE_LENGTH ];
} KeyValue;

class Config
{
public:
   Config( char *fileName );
   ~Config();

   bool  isRegistryAvailable();
   bool  getInt( char *key,int32_t *intValue );
   bool  getString( char *key,char *strValue );
   fs::SPIFFSFS   *getSPIFFS();

   static Config     *instance();
   static uint8_t    numRegistryEntries;
   static KeyValue   m_entries[ MAX_REGISTRY_ENTRIES ];

private:
   void initialise();
   void writeRegistryToFile();
   bool readRegistryFromFile();

   fs::SPIFFSFS  *m_spiffs;
   char           m_configFileName[ MAX_FILENAME + 1 ];
};

#endif
