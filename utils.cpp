#include <time.h>

#include <SD.h>
#include <FS.h>

#include "utils.h"
#include "Config.h"

static char buffer[ 4096 ];

static bool  isTrueVal = true;
static bool  isFalseVal = false;

static bool *isTrue = &isTrueVal;
static bool *isFalse = &isFalseVal;

static bool *serialLoggingEnabled = nullptr;
static bool *debugLevelEnabled = nullptr;
static bool *logTimestamps = nullptr;
static bool *logToFile = nullptr;
static bool *logTiming = nullptr;

static bool logFileOk = true;

void msgLog( LOGGING_LEVEL level,const char *format,... )
{
   // 1st check to see if we have configured yet, use the serialLoggingEnabled
   // to determine our debug options once (after config is available).

   if ( !serialLoggingEnabled && Config::instance() )
   {
      serialLoggingEnabled = isFalse;
      debugLevelEnabled = isFalse;
      logTimestamps = isFalse;
      logToFile = isFalse;
      logTiming = isFalse;

      if ( GET_REGISTRY_INT( DISABLED_SERIAL_LOGGING ) != 1 )
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
         if( GET_REGISTRY_INT( BOARD_TYPE ) == TEMPERATURE_BOARD )
         {
            Serial.println( "Can't debug log to file on TBoards" );
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

   }

   // Now check to see if we're logging or not

   if ( (serialLoggingEnabled == isFalse && logToFile == isFalse) || (level == LOGGING_LEVEL::DEBUG && debugLevelEnabled == isFalse)
                  || (level == LOGGING_LEVEL::TIMING && logTiming == isFalse) )
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

      // Can get the stack free effectively, seem to have about 2.5k left
      // with the 512 byte buffer used in sending daily update - starts at
      // about 7k and we're at around 3.5k when loop() entered.

      // uint32_t wmark = uxTaskGetStackHighWaterMark( NULL );

      strftime( line,20,"%H:%M:%S",&timeInfo );
      sprintf( msStr,".%03u - ",ms );
      strcat( line,msStr );

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
   vsprintf( buffer,format,args );
   debugString += buffer;
   va_end( args );

   if ( serialLoggingEnabled != isFalse )
   {
      Serial.println( debugString.c_str() );
   }

   if ( logToFile == isTrue )
   {
     File file = SD.open( "/debug.log",FILE_APPEND );
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
