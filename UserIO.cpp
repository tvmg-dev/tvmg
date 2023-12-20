#include <U8g2lib.h>
#include <Wire.h>

#include <WiFi.h>

#include <time.h>

#include "hwconfig.h"
#include "utils.h"

#include "UserIO.h"
#include "Storage.h"

UserIO::UserIO()
      : m_display( nullptr ),
        m_currentScreen( NONE ),
        m_currentLines(),
        m_measurement( nullptr ),
        m_sample()
{
   PW_DEBUG( "UserIO::UserIO()" );
   PW_MSG( "UserIO Module Startup" );

   m_display = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C( U8G2_R0,U8X8_PIN_NONE,hwConfig->OLEDClkGPIO,hwConfig->OLEDDataGPIO );

   for ( int i = 1; i < MAX_OLED_ROWS; i++ )
   {
      m_currentLines[ i ][ 0 ] = 0;
   }
}

UserIO::~UserIO()
{
   PW_DEBUG( "UserIO::~UserIO()" );

   delete m_display;
}

void  UserIO::initialise( void )
{
   PW_DEBUG( "UserIO::initialise" );

   m_display->begin();

   m_display->setFont(u8g2_font_6x10_tf);
   m_display->setFontRefHeightExtendedText();
   m_display->setDrawColor(1);
   m_display->setFontPosTop();
   m_display->setFontDirection(0);
}

void  UserIO::setMeasurement( Measurement *measurement )
{
   m_measurement = measurement;
}

void  UserIO::updateLine( uint8_t lineNum,char *line,bool isForLog )
{
   if ( lineNum < MAX_OLED_ROWS )
   {
      strncpy( m_currentLines[ lineNum ],line,MAX_OLED_COLUMNS );
      if ( isForLog )
      {
         PW_MSG( line );
      }
   }

   show( m_currentLines );
}

void  UserIO::clear( void )
{
   for ( int i = 0; i < MAX_OLED_ROWS; i++ )
   {
      m_currentLines[ i ][ 0 ] = 0;
   }

   show( m_currentLines );
}

void  UserIO::show( OLEDDisplayLine lines[] )
{
   m_display->clearBuffer();

   for ( int row = 0; row < MAX_OLED_ROWS; row++ )
   {
      m_display->drawStr( 0,row * 10, lines[ row ] );
   }

   m_display->sendBuffer();
}

void  UserIO::show( ScreenType type )
{
   // clear lines
   for ( int i = 0; i < 6; i++ )
   {
      m_currentLines[ i ][ 0 ] = 0;
   }

   switch( type )
   {
      case TEMPERATURES :
               sprintf( m_currentLines[ 0 ],"Heat Pump DT : %.1f",m_sample.m_flowHP - m_sample.m_returnHP );
               sprintf( m_currentLines[ 1 ]," %.1f : %.1f",m_sample.m_flowHP,m_sample.m_returnHP );
               sprintf( m_currentLines[ 2 ],"Heating DT : %.1f",m_sample.m_flowHeating - m_sample.m_returnHeating );
               sprintf( m_currentLines[ 3 ]," %.1f : %.1f",m_sample.m_flowHeating,m_sample.m_returnHeating );
               sprintf( m_currentLines[ 4 ],"Outside :" );
               sprintf( m_currentLines[ 5 ]," %.1f",m_sample.m_outside );
               show( m_currentLines );
               break;
      case ENERGY :
               sprintf( m_currentLines[ 0 ],"Heat Pump :" );
               sprintf( m_currentLines[ 1 ], " %.0f W : %.0f kWHr",m_sample.m_powerHP,m_sample.m_energyHP / 1000 );
               sprintf( m_currentLines[ 2 ],"Immersion :" );
               sprintf( m_currentLines[ 3 ], " %.0f W : %.0f kWHr",m_sample.m_powerImmersion,m_sample.m_energyImmersion / 1000 );
               show( m_currentLines );
               break;
      case STATUS :
               char        wifiStatus[ 32 ];
               char        sdCardStatus[ MAX_OLED_COLUMNS ];
               struct tm   timeInfo;
               uint32_t    freeHeap;

               // get Wifi status
               if ( WiFi.status() == WL_CONNECTED )
               {
                  sprintf( wifiStatus,"IP : %s",WiFi.localIP().toString() );
               }
               else
               {
                  strcpy( wifiStatus,"IP : not connected" );
               }
               sprintf( m_currentLines[ 0 ],wifiStatus );

               freeHeap = ESP.getFreeHeap();
               sprintf( m_currentLines[ 1 ],"Free : %u KiB", freeHeap / 1024 );

               if ( Storage::isSDCardOk() )
               {
                  strncpy( m_currentLines[ 2 ],"SD Card Ok",MAX_OLED_COLUMNS - 1 );
               }
               else
               {
                  strncpy( m_currentLines[ 2 ],"SD Card Fault",MAX_OLED_COLUMNS - 1 );
               }

               getLocalTime( &timeInfo );
               strftime( m_currentLines[ 4 ],20,"%d/%m/%y : %H:%M:%S",&timeInfo );
               sprintf( m_currentLines[ 5 ],"Uptime (s) : %u", millis() / 1000);
               show( m_currentLines );
               break;
      default :
         PW_WARN( "Unknown display type" );
   }
}

void  UserIO::showNext( void )
{
   switch ( m_currentScreen )
   {
      case TEMPERATURES:
         m_currentScreen = ENERGY;
         break;
      case ENERGY:
         m_currentScreen = STATUS;
         break;
      case STATUS:
         m_currentScreen = TEMPERATURES;
         break;
      default:
         m_currentScreen = TEMPERATURES;
         break;
   }

   show( m_currentScreen );
}

void  UserIO::update( void )
{
   if ( m_measurement )
   {
      m_sample = m_measurement->getLastSample();
   }
}
