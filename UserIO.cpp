#include <U8g2lib.h>
#include <Wire.h>

#include <WiFi.h>
#include <SD.h>

#include <time.h>

#include "Config.h"
#include "hwconfig.h"
#include "utils.h"

#include "UserIO.h"
#include "Storage.h"

UserIO::UserIO()
      : m_display( nullptr ),
        m_currentScreen( NONE ),
        m_currentLines(),
        m_measurement( nullptr ),
        m_networking( nullptr ),
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

void  UserIO::initialise()
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

void  UserIO::storeLine( uint8_t lineNum,char *line )
{
   if ( lineNum < MAX_OLED_ROWS )
   {
      strncpy( m_currentLines[ lineNum ],line,MAX_OLED_COLUMNS );
   }
}


void  UserIO::clear()
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

void  UserIO::setNetworking( Networking *network )
{
   m_networking = network;
}

void  UserIO::showNetwork()
{
   char        line[ MAX_OLED_COLUMNS ];
   char        sdCardStatus[ MAX_OLED_COLUMNS ];
   struct tm   timeInfo;

   // get Wifi status
   if ( WiFi.status() != WL_CONNECTED )
   {
      storeLine( 0,"IP : not connected" );
   }
   else if ( m_networking )
   {
      snprintf( line,MAX_OLED_COLUMNS,"%s",m_networking->getLocalMDNSName().c_str() );
      storeLine( 0,line );

      snprintf( line,MAX_OLED_COLUMNS,"IP %s",m_networking->getIPAddress().c_str() );
      storeLine( 1,line );

      snprintf( line,MAX_OLED_COLUMNS,"RSSI : %d dBm",WiFi.RSSI() );
      storeLine( 2,line );

      if ( !m_networking->didAcquireNTP() )
      {
         storeLine( 4,"No NTP" );
      }
      else
      {
         getLocalTime( &timeInfo );
         strftime( m_currentLines[ 4 ],20,"%d/%m/%y : %H:%M:%S",&timeInfo );
         sprintf( m_currentLines[ 5 ],"Uptime (s) : %u", millis() / 1000);
      }
   }

   show( m_currentLines );
}

void  UserIO::showStorage()
{
   char  line[ MAX_OLED_COLUMNS ];

   if ( !Storage::isSDCardOk() )
   {
      storeLine( 0,"SD Card Fault" );
   }
   else
   {
      uint32_t totalMiB, usedMiB, freeMiB;
      totalMiB = SD.totalBytes() / (1024 * 1024);
      usedMiB = SD.usedBytes() / (1024 * 1024);

      snprintf( line,MAX_OLED_COLUMNS,"SD Used %u of %u MiB",usedMiB,totalMiB );
      storeLine( 0,line );
   }

   uint32_t freeHeap = ESP.getFreeHeap();
   snprintf( line,MAX_OLED_COLUMNS,"Free : %u KiB",freeHeap / 1024 );
   storeLine( 2,line );

   fs::SPIFFSFS *spiffs = Config::instance()->getSPIFFS();
   storeLine( 4,"SPIFFS" );
   snprintf( line,MAX_OLED_COLUMNS,"%u of %u KiB",spiffs->usedBytes()/1024, spiffs->totalBytes()/1024 );
   storeLine( 5,line );

   show( m_currentLines );
}

void  UserIO::showEnergy()
{
   char  line[ MAX_OLED_COLUMNS ];

   int i = 0;
   while( m_sample.m_powerSensors[ i ] )
   {
      storeLine( i * 2,m_sample.m_powerSensors[ i ]->m_name );
      snprintf( line,MAX_OLED_COLUMNS,"%.0f W %.0f kWh",m_sample.m_powerSensors[ i ]->m_power,m_sample.m_powerSensors[ i ]->m_energy / 1000.0 );
      storeLine( 1 + i * 2,line );
      i++;
   }
   show( m_currentLines );
}

TempSensor *UserIO::findTempSensor( uint8_t id )
{
   TempSensor *sensor = nullptr;

   int i = 0;
   while ( m_sample.m_tempSensors[ i ] )
   {
      if ( m_sample.m_tempSensors[ i ]->m_id == id )
      {
         sensor = m_sample.m_tempSensors[ i ];
         break;
      }
      i++;
   }

   return( sensor );
}

void  UserIO::showLocalTemps()
{
   char  line[ MAX_OLED_COLUMNS ];

   TempSensor *hpFlow = findTempSensor( HEAT_PUMP_FLOW );
   TempSensor *hpReturn = findTempSensor( HEAT_PUMP_RETURN );

   TempSensor *UFHFlow = findTempSensor( UFH_FLOW );
   TempSensor *UFHReturn = findTempSensor( UFH_RETURN );

   TempSensor *outside = findTempSensor( OUTSIDE );

   if ( hpFlow && hpReturn )
   {
      snprintf( line,MAX_OLED_COLUMNS,"HP  : %3.1f %3.1f : %3.1f",hpFlow->m_temp,hpReturn->m_temp,hpFlow->m_temp - hpReturn->m_temp );
      storeLine( 0,line );
   }

   if ( UFHFlow && UFHReturn )
   {
      snprintf( line,MAX_OLED_COLUMNS,"UFH : %3.1f %3.1f : %3.1f",UFHFlow->m_temp,UFHReturn->m_temp,UFHFlow->m_temp - UFHReturn->m_temp );
      storeLine( 1,line );
   }

   if ( outside )
   {
      snprintf( line,MAX_OLED_COLUMNS,"Out : %3.1f",outside->m_temp );
      storeLine( 2,line );
   }

   show( m_currentLines );
}

void  UserIO::showRemoteTemps()
{
}

void UserIO::showAllTemps()
{
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
      case NETWORK_STATUS:
         showNetwork();
         break;
      case STORAGE_STATUS:
         showStorage();
         break;
      case ENERGY:
         showEnergy();
         break;
      case LOCAL_TEMP:
         showLocalTemps();
         break;
      case REMOTE_TEMP:
         showRemoteTemps();
         break;
      case ALL_TEMP:
         showRemoteTemps();
         break;
      default :
         PW_WARN( "Unknown display type" );
   }
}

void  UserIO::showNext()
{
   switch ( m_currentScreen )
   {
      case NETWORK_STATUS:
         m_currentScreen = STORAGE_STATUS;
         break;
      case STORAGE_STATUS:
         m_currentScreen = ENERGY;
         break;
      case ENERGY:
         m_currentScreen = LOCAL_TEMP;
         break;
      case LOCAL_TEMP:
//         m_currentScreen = REMOTE_TEMP;
         m_currentScreen = NETWORK_STATUS;
         break;
      case REMOTE_TEMP:
         m_currentScreen = ALL_TEMP;
         break;
      case ALL_TEMP:
         m_currentScreen = NETWORK_STATUS;
         break;
      default:
         m_currentScreen = LOCAL_TEMP;
         break;
   }

   show( m_currentScreen );
}

void  UserIO::update()
{
   if ( m_measurement )
   {
      m_sample = m_measurement->getLastSample();
   }
}
