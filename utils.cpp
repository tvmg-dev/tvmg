#include <time.h>

#include <SD.h>
#include <FS.h>

#include "Utils.h"

#define  LOG_TIMESTAMP  1

static char buffer[ 4096 ];

void msgLog( LOGGING_LEVEL level,const char *format,... )
{
  va_list args;

#if LOG_TIMESTAMP == 1
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

   Serial.print( line );
#endif

  if ( level == LOGGING_LEVEL::DEBUG )
  {
    Serial.printf( "DBG: ");
  }
  else if ( level == LOGGING_LEVEL::WARNING )
  {
    Serial.printf( "WARN: ");
  }
  else if ( level == LOGGING_LEVEL::ERROR )
  {
    Serial.printf( "ERROR: " );
  }

  va_start( args,format );
  vsprintf( buffer,format,args );
  Serial.printf( "%s\n", buffer);
  va_end( args );

#if DEBUG_LOGGING == 1
  File file = SD.open( "/debug.log",FILE_APPEND );
  if ( file )
  {
      file.println( buffer );
      file.close();

  }
#endif
}

