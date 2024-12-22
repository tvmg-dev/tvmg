#include <time.h>
#include <WiFi.h>
#include <AsyncUDP.h>
#include <mutex>

#include <cJSON.h>

#include <SD.h>
#include <FS.h>

#include "utils.h"
#include "Config.h"

#include "Networking.h"
#include "LGHeatPump.h"

bool isBootSerialEnabled = true;

static char buffer[ 4096 ];

static bool  isTrueVal = true;
static bool  isFalseVal = false;

static bool *isTrue = &isTrueVal;
static bool *isFalse = &isFalseVal;

static bool *serialLoggingEnabled = nullptr;
static bool *logToUDP = nullptr;
static bool *debugLevelEnabled = nullptr;
static bool *hpModBusEnabled = nullptr;
static bool *logTimestamps = nullptr;
static bool *logToFile = nullptr;
static bool *logTiming = nullptr;
static bool *logMemStats = nullptr;

static bool logFileOk = true;

static IPAddress   subNet;
static uint16_t    UDPDebugPort = 0;

std::mutex  loggingMutex;

bool  isDebugEnabled()
{
   if ( debugLevelEnabled == isTrue )
   {
      return true;
   }

   return false;
}

void msgLog( LOGGING_LEVEL level,const char *format,... )
{
   // 1st check to see if we have configured yet, use the serialLoggingEnabled
   // to determine our debug options once (after config is available).

   if ( !serialLoggingEnabled && Config::instance() )
   {
      serialLoggingEnabled = isFalse;
      debugLevelEnabled = isFalse;
      hpModBusEnabled = isFalse;
      logTimestamps = isFalse;
      logToFile = isFalse;
      logTiming = isFalse;
      logToUDP = isFalse;
      logMemStats = isFalse;

      if ( GET_REGISTRY_INT( ENABLE_SERIAL_LOGGING ) == 1 )
      {
         serialLoggingEnabled = isTrue;
      }

      if ( GET_REGISTRY_INT( DEBUG_LEVEL_ENABLED ) == 1 )
      {
         debugLevelEnabled = isTrue;
      }

      if ( GET_REGISTRY_INT( LOG_TIMESTAMP ) == 1 )
      {
         logTimestamps = isTrue;
      }

      if ( GET_REGISTRY_INT( LOG_TO_FILE ) == 1 )
      {
         if( GET_REGISTRY_INT( BOARD_TYPE ) != MASTER_BOARD )
         {
            Serial.println( "Can't debug log to file, no SD" );
         }
         else
         {
            logToFile = isTrue;
         }
      }

      if ( GET_REGISTRY_INT( LOG_TIMING ) == 1 )
      {
         logTiming = isTrue;
      }

      if ( GET_REGISTRY_INT( LOG_MEMSTATS ) == 1 )
      {
         logMemStats = isTrue;
      }

      if ( GET_REGISTRY_INT( LOG_HP_MODBUS ) == 1 )
      {
         if( GET_REGISTRY_INT( BOARD_TYPE ) != MASTER_BOARD )
         {
            Serial.println( "Can't debug log HP modbus to file, no SD" );
         }
         else
         {
            hpModBusEnabled = isTrue;
         }
      }

      if ( GET_REGISTRY_INT( LOG_TO_UDP_PORT ) > 0 )
      {
         logToUDP = isTrue;
         UDPDebugPort = GET_REGISTRY_INT( LOG_TO_UDP_PORT );
      }
   }

   // Now check to see if we're logging or not
   // We also return if the serial port has been disabled by h/w and we
   // would otherwise have been sending debug to the serial port

   if ( ( (isBootSerialEnabled == false || serialLoggingEnabled == isFalse) && logToFile == isFalse && logToUDP == isFalse )
                  || (level == LOGGING_LEVEL::DEBUG && debugLevelEnabled == isFalse)
                  || (level == LOGGING_LEVEL::TIMING && logTiming == isFalse)
                  || (level == LOGGING_LEVEL::HP_MODBUS && hpModBusEnabled == isFalse) )
   {
      return;
   }

   String  debugString;

   if ( logTimestamps == isTrue )
   {
      struct tm      timeInfo;
      struct timeval tv_now;
      char           line[ 64 ],msStr[ 32 ];

      gettimeofday( &tv_now, NULL );

      uint32_t ms = tv_now.tv_usec / 1000;
      time_t now = tv_now.tv_sec;

      localtime_r( &now,&timeInfo );

      strftime( line,20,"%H:%M:%S",&timeInfo );
      sprintf( msStr,".%03u - ",ms );
      strcat( line,msStr );

      debugString += line;
   }

   if ( logMemStats == isTrue )
   {
      char line[ 64 ];

      // get stack watermark for current task, current core and free heap

      sprintf( line,"[%u,%u,%u] - ",xPortGetCoreID(),uxTaskGetStackHighWaterMark( NULL ),ESP.getFreeHeap() / 1024 );

      debugString += line;
   }

   if ( level == LOGGING_LEVEL::DEBUG )
   {
      debugString += "DBG: ";
   }
   else if ( level == LOGGING_LEVEL::WARNING )
   {
      debugString += "WARN: ";
   }
   else if ( level == LOGGING_LEVEL::ERROR )
   {
      debugString += "ERROR: ";
   }
   else if ( level == LOGGING_LEVEL::TIMING )
   {
      debugString += "TIMING: ";
   }
   else if ( level == LOGGING_LEVEL::HP_MODBUS )
   {
      debugString += "HPMOD: ";
   }

   // we should really take the network mutex here for UDP, beware of deadly
   // embrace, i.e. we can't in the one core lock network & logging and
   // the other core lock logging, then network.
   // this could introduce significant hold off's in UDP enabled runtime
   // as some activity when networking lock is held can be several seconds
   // So for now - we don't take the NW mutex and hope UDP broadcast on 1
   // core doesn't affect IP activity on the other...

   //std::lock_guard<std::mutex> lock(networkingMutex);

   std::lock_guard<std::mutex> lock(loggingMutex);

   va_list args;
   va_start( args,format );
   vsprintf( buffer,format,args );
   debugString += buffer;
   va_end( args );

   // Only output to serial if enabled in config and serial boot behaviour
   // is to have serial enabled

   if ( serialLoggingEnabled == isTrue && isBootSerialEnabled )
   {
      Serial.println( debugString.c_str() );
   }

   if ( logToUDP == isTrue && UDPDebugPort && Networking::getUDP() )
   {
      if ( subNet[ 3 ] == 0 )
      {
         subNet = WiFi.localIP();
         subNet[ 3 ] = 255;
      }

      uint16_t len = strlen(debugString.c_str());
      if ( len > 2048 )
      {
         len = 2048;
         String tooBig = "Message is too big for UDP, limiting";
         (void) Networking::getUDP()->writeTo( (const uint8_t *) tooBig.c_str(),strlen(tooBig.c_str()),subNet,UDPDebugPort );
      }

      (void) Networking::getUDP()->writeTo( (const uint8_t *) debugString.c_str(),len,subNet,UDPDebugPort );
   }

   if ( logToFile == isTrue )
   {
     File file = SD.open( DEBUG_LOG,FILE_APPEND );
     if ( file )
     {
         file.println( debugString.c_str() );
         file.close();
     }
   }

   if ( level == LOGGING_LEVEL::HP_MODBUS )
   {
      File file = SD.open( LGMODBUS_LOG,FILE_APPEND );
      if ( file )
      {
         file.println( debugString.c_str() );
         file.close();
      }
   }
}

Timing::Timing( const String &name )
   : m_name( name ),
     m_startMillis(0)
{
   m_startMillis = millis();
}

Timing::~Timing()
{
   String timing( millis() - m_startMillis,DEC );
   PW_TIMING( "%s : %s",m_name.c_str(),timing.c_str() );
}

int   getIntFromcJSON( cJSON *node,const char *key, int defaultValue )
{
   int   value = defaultValue;

   cJSON *obj = cJSON_GetObjectItem( node,key );
   if ( cJSON_IsNumber( obj ) )
   {
      value = obj->valueint;
   }

   return value;
}

float   getFloatFromcJSON( cJSON *node,const char *key, float defaultValue )
{
   float   value = defaultValue;

   cJSON *obj = cJSON_GetObjectItem( node,key );
   if ( cJSON_IsNumber( obj ) )
   {
      value = static_cast<float> (obj->valuedouble);
   }

   return value;
}

String   getStringFromcJSON( cJSON *node,const char *key, const String &defaultValue )
{
   String   value = defaultValue;

   cJSON *obj = cJSON_GetObjectItem( node,key );
   if ( cJSON_IsString( obj ) )
   {
      value = obj->valuestring;
   }

   return value;
}

int strcmpcJSON( cJSON *node,const char *key, const char *string )
{
   int ret = -1;

   cJSON *obj = cJSON_GetObjectItem( node,key );
   if ( cJSON_IsString( obj ) )
   {
      ret = strcmp( obj->valuestring,string );
   }

   return ret;
}

bool  isSensorRequired( const char *sensorName )
{
   bool  isReq = false;

   fs::SPIFFSFS *spiffs = Config::instance()->getSPIFFS();
   File file = spiffs->open( "/sensors.dat",FILE_READ );
   if ( !file )
   {
      PW_WARN( "/sensors.dat is missing" );
   }
   else
   {
      String data = file.readStringUntil( '@' );

      cJSON *root = cJSON_Parse( data.c_str() );
      cJSON *sensor;

      if ( cJSON_IsArray( root ) )
      {
         cJSON_ArrayForEach( sensor,root )
         {
            if ( strcmpcJSON( sensor,"type",sensorName ) == 0 )
            {
               isReq = true;
               break;
            }
         }
      }

      cJSON_Delete( root );
      close( file );
   }

   if ( isReq )
   {
      PW_DEBUG( "%s required",sensorName );
   }

   return isReq;
}

