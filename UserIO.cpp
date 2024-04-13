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

extern Storage *storageModule;

UserIO::UserIO()
      : m_display( nullptr ),
        m_currentScreen( NONE ),
        m_currentLines(),
        m_measurement( nullptr ),
        m_networking( nullptr ),
        m_sample(),
        m_firmwareUpdateInProgress( false ),
        m_startTime()
{
   PW_DEBUG( "UserIO::UserIO()" );
   PW_MSG( "UserIO Module Startup" );

   m_display = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C( U8G2_R0,U8X8_PIN_NONE,hwConfig->OLEDClkGPIO,hwConfig->OLEDDataGPIO );

   for ( int i = 1; i < MAX_OLED_ROWS; i++ )
   {
      m_currentLines[ i ][ 0 ] = 0;
   }

   time( &m_startTime );
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
   time_t      currentTime;

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
         time( &currentTime );
         uint32_t secondsDiff = difftime( currentTime,m_startTime );

         getLocalTime( &timeInfo );
         strftime( m_currentLines[ 4 ],20,"%d/%m/%y : %H:%M:%S",&timeInfo );

         sprintf( m_currentLines[ 5 ],"Uptime %u:%02u:%02u.%02u",secondsDiff / ( 24 * 3600 ), secondsDiff / 3600, secondsDiff / 60, secondsDiff % 60 );

         PW_MSG( "Time diff %u %s",secondsDiff,m_currentLines[ 5 ] );
      }
   }

   show( m_currentLines );
}

void  UserIO::showStorage()
{
   char  line[ MAX_OLED_COLUMNS ];

   snprintf( line,MAX_OLED_COLUMNS,"Version : %s",VERSION_STR );
   storeLine( 0,line );

   if ( storageModule )
   {
      storageModule->getStatus( line );
      storeLine( 1,line );
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
      const PowerSensor  *sensor;
      sensor = &m_sample.m_actualPowers[ i ];

      storeLine( i * 2,sensor->m_name );
      snprintf( line,MAX_OLED_COLUMNS,"%.0f W %.0f kWh",sensor->m_power,sensor->m_energy / 1000.0 );
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
         sensor = &m_sample.m_actualTemps[ i ];
         break;
      }
      i++;
   }

   return( sensor );
}

void  UserIO::showTemps()
{
   char  line[ MAX_OLED_COLUMNS ];

   TempSensor *hpFlow = findTempSensor( HEAT_PUMP_FLOW );
   TempSensor *hpReturn = findTempSensor( HEAT_PUMP_RETURN );

   TempSensor *UFHFlow = findTempSensor( UFH_FLOW );
   TempSensor *UFHReturn = findTempSensor( UFH_RETURN );

   TempSensor *outside = findTempSensor( OUTSIDE );

   if ( hpFlow && hpReturn )
   {
      snprintf( line,MAX_OLED_COLUMNS,"HP: %3.1f %3.1f (%3.1f)",hpFlow->m_temp,hpReturn->m_temp,hpFlow->m_temp - hpReturn->m_temp );
      storeLine( 0,line );
   }

   if ( UFHFlow && UFHReturn )
   {
      snprintf( line,MAX_OLED_COLUMNS,"UF: %3.1f %3.1f (%3.1f)",UFHFlow->m_temp,UFHReturn->m_temp,UFHFlow->m_temp - UFHReturn->m_temp );
      storeLine( 1,line );
   }

   if ( outside )
   {
      snprintf( line,MAX_OLED_COLUMNS,"OS: %3.1f",outside->m_temp );
      storeLine( 2,line );
   }

   TempSensor *loftFlow = findTempSensor( LOFT_FLOW );
   TempSensor *loftReturn = findTempSensor( LOFT_RETURN );

   if ( loftFlow && loftReturn )
   {
      snprintf( line,MAX_OLED_COLUMNS,"2 : %3.1f %3.1f (%3.1f)",loftFlow->m_temp,loftReturn->m_temp,loftFlow->m_temp - loftReturn->m_temp );
      storeLine( 3,line );
   }

   TempSensor *firstFlow = findTempSensor( FIRST_FLOW );
   TempSensor *firstReturn = findTempSensor( FIRST_RETURN );

   if ( firstFlow && firstReturn )
   {
      snprintf( line,MAX_OLED_COLUMNS,"1 : %3.1f %3.1f (%3.1f)",firstFlow->m_temp,firstReturn->m_temp,firstFlow->m_temp - firstReturn->m_temp );
      storeLine( 4,line );
   }

   TempSensor *groundFlow = findTempSensor( GND_FLOW );
   TempSensor *groundReturn = findTempSensor( GND_RETURN );

   if ( groundFlow && groundReturn )
   {
      snprintf( line,MAX_OLED_COLUMNS,"0 : %3.1f %3.1f (%3.1f)",groundFlow->m_temp,groundReturn->m_temp,groundFlow->m_temp - groundReturn->m_temp );
      storeLine( 5,line );
   }

   show( m_currentLines );
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
      case TEMPERATURES:
         showTemps();
         break;
      default :
         PW_WARN( "Unknown display type" );
   }
}

void  UserIO::showNext()
{
   if ( m_firmwareUpdateInProgress )
   {
      PW_DEBUG( "Update in progres.." );
      return;
   }

   switch ( m_currentScreen )
   {
      case NETWORK_STATUS:
         m_currentScreen = STORAGE_STATUS;
         break;
      case STORAGE_STATUS:
         if ( isPowerDataAvailable() )
         {
            m_currentScreen = ENERGY;
         }
         else
         {
            m_currentScreen = TEMPERATURES;
         }
         break;
      case ENERGY:
         m_currentScreen = TEMPERATURES;
         break;
      case TEMPERATURES:
         m_currentScreen = NETWORK_STATUS;
         break;
      default:
         m_currentScreen = TEMPERATURES;
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

void  UserIO::setFirmwareUpdateInProgress( bool progress )
{
   m_firmwareUpdateInProgress = progress;
}

bool  UserIO::isFirmwareUpdateInProgress()
{
   return m_firmwareUpdateInProgress;
}

bool  UserIO::isPowerDataAvailable()
{
   for ( int i = 0; i < MAX_POWER_SENSORS; i++ )
   {
      if ( m_sample.m_powerSensors[ i ] )
      {
         return true;
      }
   }

   return false;
}
