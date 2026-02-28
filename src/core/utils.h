#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <stdarg.h>

#define MAX_FILENAME    32

#define SENSORS_FILENAME   "/sensors.json"

// Early boot logging support
#define EARLY_BOOT_LOGFILE             "/bootlog.txt"
#define EARLY_BOOT_LOG_TIMEOUT_MS      (150 * 1000UL)   // 2.5 minutes
#define EARLY_BOOT_LOG_SEND_MARGIN_MS  (60UL * 1000UL)  // add one minute before sending


//----------------------------------------------------------------------
// General buffer - also used by the external email sender library
// this is used to try and keep RAM usage to a minimum

#define SCRATCH_BUFFER_SIZE   4096
extern  uint8_t   scratchBuffer[];

//----------------------------------------------------------------------
// Usage info

extern void  getRunTimeInfo();
extern uint32_t largestFreeInternalBlock();
extern void  getModbusStats( uint32_t *requests,uint32_t *fails );

//----------------------------------------------------------------------
// for mapping names of sensors

enum SensorType { THERM,POWER,HEATMETER,HEATPUMP,MODBUSTCP,SHELLYPM };

extern void setSensorName( SensorType type, uint32_t id, const String &name );
extern const String &getSensorName( SensorType type, uint32_t id );
extern void debugSensorNameMap();

//----------------------------------------------------------------------

enum DebugState { DEBUG_ON, DEBUG_OFF };

extern bool  isBootSerialEnabled;
extern bool  isDebugEnabled();
extern void  setUdpDebugState( DebugState state );
extern DebugState getUdpDebugState();

// helpers for early boot logging
extern bool shouldSendBootLog();     // boot log period + margin and bootlog enabled

//----------------------------------------------------------------------
// Reading cJSON fields etc

class cJSON;

extern cJSON * readJSONFromFile( const String &fileName );

extern cJSON * getAllSensorJSON();
extern void    releaseSensorJSON();

extern bool    isSensorRequired( const char *sensorName );
extern bool    getBoolFromcJSON( cJSON *node,const char *key, bool defaultValue );
extern int     getIntFromcJSON( cJSON *node,const char *key, int defaultValue = -1 );
extern float   getFloatFromcJSON( cJSON *node,const char *key, float defaultValue = 0 );
extern String  getStringFromcJSON( cJSON *node,const char *key, const String &defaultValue = "" );
extern int     strcmpcJSON( cJSON *node,const char *key, const char *item );

extern void    replaceFile( const String &origFile,const String &newFile );

//----------------------------------------------------------------------
// Debugging macros/helper

#define START_DEBUG \
do \
if ( isDebugEnabled() ) {

#define END_DEBUG \
} while( 0 );

enum LOGGING_LEVEL { MSG, DEBUG, WARNING, ERROR, TIMING };

extern void msgLog( LOGGING_LEVEL level,const char *format,... );

#define PW_DEBUG(...)      msgLog( LOGGING_LEVEL::DEBUG, __VA_ARGS__ )
#define PW_MSG(...)        msgLog( LOGGING_LEVEL::MSG, __VA_ARGS__ )
#define PW_WARN(...)       msgLog( LOGGING_LEVEL::WARNING, __VA_ARGS__ )
#define PW_ERROR(...)      msgLog( LOGGING_LEVEL::ERROR, __VA_ARGS__ )
#define PW_TIMING(...)     msgLog( LOGGING_LEVEL::TIMING, __VA_ARGS__ )
#define PW_HP_MODBUS(...)  msgLog( LOGGING_LEVEL::HP_MODBUS, __VA_ARGS__ )

class Timing
{
public:
   Timing( const String &name );
   ~Timing();

private:
   String   m_name;
   uint32_t m_startMillis;
};

#define  START_TIMING( x ) \
do \
   { Timing timeThis( x );

#define  END_TIMING \
 } while( 0 );


#endif
