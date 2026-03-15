/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#include <time.h>
#include <WiFi.h>
#include <AsyncUDP.h>
#include <mutex>
#include <map>

#include <cJSON.h>

#include <SD.h>
#include <FS.h>

#include "src/config/Config.h"

#include "src/network/Networking.h"
#include "src/sensors/LGHeatPump.h"

//----------------------------------------------------------------------

uint8_t  scratchBuffer[ SCRATCH_BUFFER_SIZE ];

//----------------------------------------------------------------------

bool isBootSerialEnabled = true;

//----------------------------------------------------------------------

static char buffer[ 2 * 1024 ];

static bool  isTrueVal = true;
static bool  isFalseVal = false;

static bool *isTrue = &isTrueVal;
static bool *isFalse = &isFalseVal;

static bool *serialLoggingEnabled = nullptr;
static bool *logToUDP = nullptr;
static bool *debugLevelEnabled = nullptr;
static bool *hpModBusEnabled = nullptr;
static bool *logTimestamps = nullptr;
static bool *logTiming = nullptr;
static bool *logMemStats = nullptr;
static bool *logEarlyBoot = nullptr;


// early boot logging state
static uint32_t s_bootLogStartMS = 0;
static bool     logFileOk = true;

static IPAddress   subNet;
static uint16_t    UDPDebugPort = 0;

std::mutex  loggingMutex;

//----------------------------------------------------------------------
// cJSON/file helpers

static   cJSON *sensorJSON = nullptr;

static int   numChars( char findChar,const char *str )
{
   int   num = 0;

   if ( str )
   {
      while ( *str )
      {
         if ( *str == findChar )
         {
            num++;
         }
         str++;
      }
   }

   return num;
}

void  replaceFile( const String &origFile,const String &newFile )
{
   if ( !tvmgFileSys )
   {
      TVMG_WARN( "No FS, can't replace file" );
      return;
   }

   // First remove the original file, then we'll copy from the new file
   // back to the original

   tvmgFileSys.remove( origFile );

   File ipFile = tvmgFileSys.open( newFile,"r" );
   if ( ipFile )
   {
      File opFile = tvmgFileSys.open( origFile,"w" );
      if ( opFile )
      {
         TVMG_MSG( "Replacing %s with %s",origFile,newFile );

         int count;
         while( ( count = ipFile.read( scratchBuffer,SCRATCH_BUFFER_SIZE ) ) > 0 )
         {
            TVMG_DEBUG( "from %s read %d",newFile,count );
            opFile.write( scratchBuffer,count );
         }
         opFile.close();
      }

      ipFile.close();
   }
}

cJSON * readJSONFromFile( const String &fileName )
{
   if ( !tvmgFileSys )
   {
      TVMG_WARN( "No filesystem !" );
      return nullptr;
   }

   File file = tvmgFileSys.open( fileName,FILE_READ );
   if ( !file )
   {
      TVMG_DEBUG( "readJSON %s not found",fileName.c_str() );
      return nullptr;
   }

   size_t size = file.size();
   char *buffer = (char *)malloc( size + 1 );
   if ( !buffer )
   {
      file.close();
      TVMG_ERROR( "readJSON failed to allocate buffer for %s",fileName.c_str() );
      return nullptr;
   }

   file.readBytes( buffer, size );
   file.close();
   buffer[ size ] = 0;

   // legacy files may have a terminating '@' - so remove it before parsing.
   if ( buffer[ size - 1 ] == '@' )
   {
      buffer[ size -1 ] = 0;
   }

   cJSON *root = cJSON_Parse( buffer );
   free( buffer );

   if ( !root )
   {
      TVMG_ERROR( "Failed to parse JSON from %s",fileName.c_str() );
   }
   
   return root;
}

cJSON *getAllSensorJSON()
{
   if ( sensorJSON )
   {
      return sensorJSON;
   }

   sensorJSON = readJSONFromFile( SENSORS_FILENAME );

   if ( sensorJSON && cJSON_IsArray( sensorJSON ) )
   {
      TVMG_MSG( "cJSON array read ok" );
   }
   else if ( sensorJSON )
   {
      cJSON_Delete( sensorJSON );
      sensorJSON = nullptr;

      TVMG_ERROR( "cJSON from %s was not an array",SENSORS_FILENAME );
   }

   if ( sensorJSON )
   {
      TVMG_DEBUG( "JSON at 0x%x",sensorJSON );
   }

   return sensorJSON;
}

void  releaseSensorJSON()
{
   if ( sensorJSON )
   {
      TVMG_MSG( "Releasing JSON" );

      cJSON_Delete( sensorJSON );
      sensorJSON = nullptr;
   }
}

bool  isSensorRequired( const char *sensorName )
{
   bool isReq = false;

   cJSON *root = getAllSensorJSON();

   if ( root )
   {
      cJSON *sensor;
      cJSON_ArrayForEach( sensor,root )
      {
         if ( strcmpcJSON( sensor,"type",sensorName ) == 0 )
         {
            isReq = true;
            break;
         }
      }
   }

   if ( isReq )
   {
      TVMG_DEBUG( "%s required",sensorName );
   }

   return isReq;
}

bool   getBoolFromcJSON( cJSON *node,const char *key, bool defaultValue )
{
   bool   value = defaultValue;

   cJSON *obj = cJSON_GetObjectItem( node,key );
   if ( cJSON_IsBool( obj ) )
   {
      value = cJSON_IsTrue( obj );
   }

   return value;
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

//----------------------------------------------------------------------
// Memory info

// Heap info taken from esp32/hardware/esp32/3.1.0/cores/esp32/chip-debug-report.cpp
// caps can be MALLOC_CAP_INTERNAL or MALLOC_CAP_SPIRAM

uint32_t largestFreeInternalBlock()
{
   multi_heap_info_t info;

   heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);

   return( info.largest_free_block );
}

uint32_t freeKiB()
{
   multi_heap_info_t info;

   heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);

   return( info.total_free_bytes / 1024 );
}

#define b2kb(b)                ((float)b / 1024.0)
#define b2mb(b)                ((float)b / (1024.0 * 1024.0))

static void printMemCapsInfo(uint32_t caps, const char *caps_str)
{
   multi_heap_info_t info;
   size_t total = heap_caps_get_total_size(caps);
   heap_caps_get_info(&info, caps);
   TVMG_MSG("%s Memory Info:", caps_str);
   TVMG_MSG("------------------------------------------");
   TVMG_MSG("  Total Size        : %8d B (%6.1f KB)", total, b2kb(total));
   TVMG_MSG("  Free Bytes        : %8d B (%6.1f KB)", info.total_free_bytes, b2kb(info.total_free_bytes));
   TVMG_MSG("  Allocated Bytes   : %8d B (%6.1f KB)", info.total_allocated_bytes, b2kb(info.total_allocated_bytes));
   TVMG_MSG("  Minimum Free Bytes: %8d B (%6.1f KB)", info.minimum_free_bytes, b2kb(info.minimum_free_bytes));
   TVMG_MSG("  Largest Free Block: %8d B (%6.1f KB)", info.largest_free_block, b2kb(info.largest_free_block));
   TVMG_MSG("------------------------------------------");
}

// Debug for finding stack depth, dervied from
// https://www.freertos.org/Documentation/02-Kernel/04-API-references/03-Task-utilities/01-uxTaskGetSystemState

#define  MAX_TASKS   25
TaskStatus_t taskStatusArray[ MAX_TASKS ];

void getRunTimeInfo()
{
   volatile UBaseType_t numTasks;
   unsigned long ulTotalRunTime, ulStatsAsPercentage;

   // Some memory info

   printMemCapsInfo( MALLOC_CAP_INTERNAL,"INTERNAL" );
   printMemCapsInfo( MALLOC_CAP_SPIRAM,"PSRAM" );
   TVMG_MSG( "Largest Free %d",largestFreeInternalBlock() );

   // How many current tasks, could change as we execute

   numTasks = uxTaskGetNumberOfTasks();

   TVMG_MSG( "Total tasks %d",numTasks );

   if ( numTasks > MAX_TASKS )
   {
      TVMG_WARN( "Exceeded number of tasks to process" );
      return;
   }

   START_TIMING( "TASK STATS" );

   numTasks = uxTaskGetSystemState( taskStatusArray,
                              numTasks,
                              &ulTotalRunTime );
   END_TIMING;

   TVMG_MSG( "runtime %ul %d %d %d",ulTotalRunTime,millis(),configTICK_RATE_HZ,portTICK_PERIOD_MS );

   ulTotalRunTime /= 100UL;

   if( ulTotalRunTime > 0 )
   {
      for( int t = 0; t < numTasks; t++ )
      {
         // Output stats, runtime % rounded down to nearest integer

         if ( t < MAX_TASKS )
         {
            TaskStatus_t *task = &taskStatusArray[ t ];

            ulStatsAsPercentage = task->ulRunTimeCounter / ulTotalRunTime;
            TVMG_MSG( "%s tt %d %% %d - stk %d Core %d",
                              task->pcTaskName,
                              task->ulRunTimeCounter,
                              ulStatsAsPercentage,
                              task->usStackHighWaterMark,
                              task->xCoreID
                              );
         }
      }
   }
}

//----------------------------------------------------------------------

// A map to store the sensor data, using a combined key for uniqueness.
// We'll use a 16-bit integer for the key, as it's a good size for combining enums and a uint8_t.
static std::map<uint32_t, String> sensorMap;

// Function to generate a unique key from sensor type and id
static uint32_t generateKey( SensorType type, uint32_t id )
{
   if ( id > 0xFFFFFF )
   {
      TVMG_ERROR( "Invalid id - truncating" );
      id &= 0xFFFFFF;
   }

   return (static_cast<uint32_t>(type) << 24) | id;
}

static const char *sensorTypeName( SensorType type )
{
   switch (type)
   {
      case THERM:
         return "Thermometer";
      case POWER:
         return "Power";
      case HEATMETER:
         return "HeatMeter";
      case HEATPUMP:
         return "HeatPump";
      case SHELLYPM:
         return "ShellyPM";
      default:
         return "Unknown";
   }
}

// Sets the name for a sensor, identified by its type and ID
void setSensorName( SensorType type, uint32_t id, const String &name )
{
   uint32_t key = generateKey(type, id);

   auto it = sensorMap.find( key );
   if ( it != sensorMap.end() )
   {
      TVMG_ERROR( "sensor map: Id %d already exists for %s",id,sensorTypeName(type) );
      return;
   }

   sensorMap[ key ] = name;
}

// Retrieves the name of a sensor
const String &getSensorName(SensorType type, uint32_t id)
{
   uint32_t key = generateKey(type, id);

   auto it = sensorMap.find( key );
   if (it != sensorMap.end())
   {
      return it->second;
   }

   static const String emptyString = "";
   return emptyString;
}

void debugSensorNameMap()
{
   const SensorType allTypes[] = {THERM, POWER, HEATMETER, HEATPUMP, SHELLYPM};

   for (SensorType type : allTypes)
   {
      bool typeHeaderPrinted = false;

      // Loop through the entire map to find entries of the current type
      for ( const auto& pair : sensorMap )
      {
         // Extract the type from the unique key
         SensorType entryType = static_cast<SensorType>(pair.first >> 24);
         uint32_t id = pair.first & 0xFFFFFF;

         if (entryType == type)
         {
            if (!typeHeaderPrinted)
            {
               TVMG_MSG( "Sensor Type : %s", sensorTypeName( type ) );
               typeHeaderPrinted = true;
            }

            TVMG_MSG( "  Id : %3d - Name : %s",id,pair.second.c_str() );
         }
      }
   }
}

//----------------------------------------------------------------------

bool  isDebugEnabled()
{
   return( debugLevelEnabled == isTrue );
}

// Is called via the webserver on checkbox selection - can only happen if there
// is a defined UDP port, so we can enable disable accordingly.

void setUdpDebugState( DebugState state )
{
   {
      // Need to take the lock here, but not when we set registry as that will
      // be calling into the debug system and the loggingMutex isn't recursive 

      std::lock_guard<std::mutex> lock(loggingMutex);

      if ( state == DEBUG_ON )
      {
         logToUDP = isTrue;
      }
      else
      {
         logToUDP = isFalse;
      }
   }

   SET_REGISTRY_INT( UDP_LOGGING_ENABLE,(logToUDP == isTrue ? true : false) );
}

DebugState getUdpDebugState()
{
   std::lock_guard<std::mutex> lock(loggingMutex);

   return ( logToUDP == isTrue ? DEBUG_ON : DEBUG_OFF );
}

void msgLog( LOGGING_LEVEL level,const char *format,... )
{
   // 1st check to see if we have configured yet, use the serialLoggingEnabled
   // to determine our debug options once (after config is available).

   if ( !serialLoggingEnabled && Config::instance() && Config::instance()->isRegistryAvailable() )
   {
      serialLoggingEnabled = isFalse;
      debugLevelEnabled = isFalse;
      hpModBusEnabled = isFalse;
      logTimestamps = isFalse;
      logTiming = isFalse;
      logToUDP = isFalse;
      logMemStats = isFalse;
      logEarlyBoot = isFalse;

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
         if( ! boardHasSDCard() )
         {
            Serial.println( "Can't debug log HP modbus to file, no SD" );
         }
         else
         {
            hpModBusEnabled = isTrue;
         }
      }

      int32_t port = GET_REGISTRY_INT( LOG_TO_UDP_PORT );
      if ( port > 0 )
      {
         UDPDebugPort = static_cast<uint16_t>(port);
      }

      if ( GET_REGISTRY_INT( UDP_LOGGING_ENABLE ) > 0 )
      {
         logToUDP = isTrue;
      }
      
      if ( GET_REGISTRY_INT( EARLY_BOOT_LOG ) > 0 )
      {
         logEarlyBoot = isTrue;
         s_bootLogStartMS = millis();
      }
   }

   // Now check to see if we're logging or not
   // We also return if the serial port has been disabled by h/w and we
   // would otherwise have been sending debug to the serial port

   if ( ( (isBootSerialEnabled == false || serialLoggingEnabled == isFalse) && logToUDP == isFalse )
                  || (level == LOGGING_LEVEL::DEBUG && debugLevelEnabled == isFalse)
                  || (level == LOGGING_LEVEL::TIMING && logTiming == isFalse) )
   {
      return;
   }

   // we should really take the network mutex here for UDP, beware of deadly
   // embrace, i.e. we can't in the one task lock network & logging and
   // the other task lock logging, then network.
   // this could introduce significant hold off's in UDP enabled runtime
   // as some activity when networking lock is held can be several seconds
   // So for now - we don't take the NW mutex and hope UDP broadcast on 1
   // core doesn't affect IP activity on the other...

   //std::lock_guard<std::mutex> lock(networkingMutex);

   std::lock_guard<std::mutex> lock(loggingMutex);

   String  debugString;

   if ( logTimestamps == isTrue || !logTimestamps )
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

   if ( logMemStats == isTrue || !logMemStats )
   {
      char line[ 64 ];
      TaskStatus_t   taskStatus;

      vTaskGetInfo( NULL,&taskStatus,pdTRUE,eInvalid );

      // get stack watermark for current task, current core and largest block
      // available from the heap (which will be less than the free heap size)

//      sprintf( line,"[%u,%s,%u,%u] - ",xPortGetCoreID(),taskStatus.pcTaskName,taskStatus.usStackHighWaterMark,largestFreeInternalBlock() / 1024 );
      sprintf( line,"[%u,%s,%u,%u] - ",xPortGetCoreID(),taskStatus.pcTaskName,taskStatus.usStackHighWaterMark,freeKiB() );

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

   va_list args;
   va_start( args,format );
   vsnprintf( buffer,sizeof(buffer),format,args );
   debugString += buffer;
   va_end( args );

   // Only output to serial if enabled in config, or config hasn't yet
   // been determined - but only if boot serial is active

   if ( ( !serialLoggingEnabled || serialLoggingEnabled == isTrue) && isBootSerialEnabled )
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

      uint16_t len = debugString.length();
      if ( len > CONFIG_TCP_MSS )
      {
         len = CONFIG_TCP_MSS;
         String tooBig = "Message is too big for UDP, limiting";
         (void) Networking::getUDP()->writeTo( (const uint8_t *) tooBig.c_str(),strlen(tooBig.c_str()),subNet,UDPDebugPort );
      }

      (void) Networking::getUDP()->writeTo( (const uint8_t *) debugString.c_str(),len,subNet,UDPDebugPort );
   }

   // append to boot log file if requested and we're still within early period
   if (logEarlyBoot == isTrue && (millis() - s_bootLogStartMS) < EARLY_BOOT_LOG_TIMEOUT_MS)
   {
      if (tvmgFileSys)
      {
         File f = tvmgFileSys.open(EARLY_BOOT_LOGFILE, FILE_APPEND);
         if (f)
         {
            f.println(debugString);
            f.close();
         }
         else
         {
            // if we can't open the file once, avoid further attempts
            logFileOk = false;
         }
      }
   }
}

// indicates we should attempt to send the boot log (timeout + margin elapsed, enabled)
bool shouldSendBootLog()
{
   bool send = false;

   if ( logEarlyBoot == isTrue )
   {
      send = millis() > s_bootLogStartMS + EARLY_BOOT_LOG_TIMEOUT_MS + EARLY_BOOT_LOG_SEND_MARGIN_MS;
   }

   return send;
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
   TVMG_TIMING( "%s : %s",m_name.c_str(),timing.c_str() );
}
