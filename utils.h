#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <stdarg.h>

#define MAX_FILENAME    32

extern void  GetRunTimeInfo();
extern uint32_t largestFreeInternalBlock();

extern bool  isBootSerialEnabled;
extern bool  isDebugEnabled();

class cJSON;

extern bool    isSensorRequired( const char *sensorName );
extern int     getIntFromcJSON( cJSON *node,const char *key, int defaultValue = -1 );
extern float   getFloatFromcJSON( cJSON *node,const char *key, float defaultValue = 0 );
extern String  getStringFromcJSON( cJSON *node,const char *key, const String &defaultValue = "" );
extern int     strcmpcJSON( cJSON *node,const char *key, const char *item );

#define START_DEBUG \
do \
if ( isDebugEnabled() ) {

#define END_DEBUG \
} while( 0 );

enum LOGGING_LEVEL { MSG, DEBUG, WARNING, ERROR, TIMING, HP_MODBUS };

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
