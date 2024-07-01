#include <Arduino.h>

#include "GrundfosUPS3.h"

#include "config.h"
#include "UserIO.h"

GrundfosUPS3::GrundfosUPS3( uint8_t pwmGPIO )
            : m_pwmGPIO( pwmGPIO )
{
}

GrundfosUPS3::~GrundfosUPS3()
{
}

void  GrundfosUPS3::initialise()
{
}

void  GrundfosUPS3::test()
{
   int64_t  start = esp_timer_get_time();
   int64_t  s1,s2,s3;
   int64_t  d1,d2,d3;

   PW_MSG( "Test1 %d",m_pwmGPIO );

   while( esp_timer_get_time() - start < 500000)
   {
      bool pinStart = digitalRead( m_pwmGPIO );

      s1 = esp_timer_get_time();
      s2 = s1;
      while( digitalRead( m_pwmGPIO ) == pinStart && s2 - s1 < 500000 )
         s2 = esp_timer_get_time();
      d1 = s2 -s1;

      s1 = s2;
      while( digitalRead( m_pwmGPIO ) == !pinStart && s2 - s1 < 500000 )
         s2 = esp_timer_get_time();
      d2 = s2 -s1;

      s1 = s2;
      while( digitalRead( m_pwmGPIO ) == pinStart && s2 - s1 < 500000 )
         s2 = esp_timer_get_time();
      d3 = s2 -s1;

      PW_MSG( "%lld %lld %lld",d1,d2,d3);
   }
}

static uint8_t s_pwmGPIO = 0;

static uint16_t positiveCount,negativeCount;

static int64_t  lastPositiveEdge;
static int64_t  timeBetweenPositiveEdges;
static int64_t  timeHigh;
static int      expectedLevel;

static uint16_t   levelDiscards;
static uint16_t   timeDiscards;


void  IRAM_ATTR   handleEdge()
{
   int64_t  currentTime = esp_timer_get_time();

   int pin = digitalRead( s_pwmGPIO );

   // discard if not at the expected level, reset to search for high
   if ( pin != expectedLevel )
   {
      levelDiscards++;
      expectedLevel = HIGH;
      lastPositiveEdge = 0;
      return;
   }

   // If its the first positive edge then we start the search
   if ( lastPositiveEdge == 0 && pin == HIGH )
   {
      lastPositiveEdge = currentTime;
      expectedLevel = LOW;
      return;
   }

   // We expect at most 13333 microsecs (75 Hz) from +ve edges
   // add some allowed overhead, otherwise we reset the search

   if ( (currentTime - lastPositiveEdge > 15000) )
   {
      timeDiscards++;
      lastPositiveEdge = 0;
      expectedLevel = HIGH;
      return;
   }

   if ( pin == HIGH )
   {
      positiveCount++;
      timeBetweenPositiveEdges += currentTime - lastPositiveEdge;
      lastPositiveEdge = currentTime;
      expectedLevel = LOW;
   }
   else
   {
      negativeCount++;
      timeHigh += currentTime - lastPositiveEdge;
      expectedLevel = HIGH;
   }
}


void  GrundfosUPS3::test2()
{
   PW_MSG( "Test2 %u",m_pwmGPIO );
   expectedLevel = HIGH;

   s_pwmGPIO = m_pwmGPIO;

   positiveCount = 0;
   negativeCount = 0;
   lastPositiveEdge = 0;
   timeBetweenPositiveEdges = 0;
   timeHigh = 0;

   levelDiscards = 0;
   timeDiscards = 0;

   attachInterrupt( m_pwmGPIO,handleEdge,CHANGE );
   delay( 500 );
   detachInterrupt( m_pwmGPIO );

   PW_MSG( "+ve %u -ve %u", positiveCount,negativeCount );
   PW_MSG( "Discards: time %u level %u", levelDiscards,timeDiscards );

if ( positiveCount )
{
   uint32_t average =  ( static_cast<uint32_t>(timeBetweenPositiveEdges) ) / positiveCount;
   PW_MSG( "Total us %lld, average %u",timeBetweenPositiveEdges,average );
}

if ( negativeCount )
{
   uint32_t highAverage = ( static_cast<uint32_t>(timeHigh) ) / negativeCount;
   PW_MSG( "High us %lld, average %u",timeHigh,highAverage );
}
}
