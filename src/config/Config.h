#ifndef __CONFIG_H
#define __CONFIG_H

#include <SPIFFS.h>

#include "src/core/utils.h"
#include "hwconfig.h"

extern const char *k_versionStr;

#define SAMPLING_PERIOD_MS 30000

#define MAX_REGISTRY_ENTRIES 32
#define MAX_KEY_LENGTH       32
#define MAX_VALUE_LENGTH     48

#define DEBUG_LOG          "/debug.log"

extern void    setRegistryEntry( char *key,char *value );
extern int32_t getRegistryInt( char *key );
extern char    *getRegistryString( char *key );
extern void    hwReset();
extern void    reboot();
extern void    setRebootRequired();
extern bool    isRebootRequired();

// for nvs data

inline constexpr  char k_rebootCounter[] = "rebootCount";
inline constexpr  char k_rebootType[] = "rebootType";
inline constexpr  char k_watchdogCause[] = "wdogReason";
inline constexpr  char k_noNetworkCounter[] = "noNetwork";

#define CONFIG_DEF_TO_STR( x ) #x

#define SET_REGISTRY( x,y )      setRegistryEntry( CONFIG_DEF_TO_STR( x ),CONFIG_DEF_TO_STR( y ) )
#define GET_REGISTRY_INT( x )    getRegistryInt( CONFIG_DEF_TO_STR( x ) )
#define GET_REGISTRY_STRING( x ) getRegistryString( CONFIG_DEF_TO_STR( x ) )

enum RebootType {
   POWER_CYCLE = 0,
   BOOT_NO_CONFIG,
   BOOT_NO_WIFI,
   BOOT_NO_NTP,
   BOOT_IN_SETUP,
   LOST_WIFI,
   SERVER_REBOOT,
   SERVER_RESET,
   SERVER_OTA_UPDATE,
   LOOP_MUTEX,
   ESP32_PANIC,
   ESP32_WATCHDOG,
   APP_24D_RESET,
   UNKNOWN
};

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

   void  initialise();
   bool  isRegistryAvailable();
   fs::SPIFFSFS   *getSPIFFS();

   bool    isFactoryReset();
   void    setFactoryReset();

   String  getESPRebootReason( esp_reset_reason_t code );
   String  getAppRebootReason( RebootType code );
   String  getRebootReason( RebootType *type );
   bool    wasFastReboot();
   bool    didRebootNoWiFi();

   bool    getPersistentInt( const String &key,int32_t *value,int32_t defValue = -1 );
   void    setPersistentInt( const String &key,int32_t value );

   static Config  * instance( bool create = false );
   static uint8_t   numRegistryEntries;
   static KeyValue  m_entries[ MAX_REGISTRY_ENTRIES ];

private:
   bool readRegistryFromFile();

   bool           m_isRegistryOk;
   bool           m_wasFastReboot;
   fs::SPIFFSFS * m_spiffs;
   char           m_configFileName[ MAX_FILENAME + 1 ];
};

#endif
