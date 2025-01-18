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
        m_heatPump( nullptr ),
        m_heatMeter( nullptr ),
        m_modbus( nullptr ),
        m_sample(),
        m_startTime(0)
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
   if ( m_networking )
   {
      m_networking->setUserIO( this );
   }
}

void  UserIO::setLGHeatPump( LGHeatPump *heatpump )
{
   m_heatPump = heatpump;
}

void  UserIO::setHeatMeter( HeatMeterModule *heatMeter )
{
   m_heatMeter = heatMeter;
}

void  UserIO::setModBus( ModbusMaster *modbus )
{
   m_modbus = modbus;
}

void  UserIO::showNetwork()
{
   char        line[ MAX_OLED_COLUMNS ];
   struct tm   timeInfo;
   time_t      currentTime;

   // get Wifi status
   if ( WiFi.status() != WL_CONNECTED )
   {
      storeLine( 0,"IP : not connected" );
   }
   else if ( m_networking )
   {
      Networking::Status   state;

      state = m_networking->getStatus();

      snprintf( line,MAX_OLED_COLUMNS,"%s",state.mdnsName.c_str() );
      storeLine( 0,line );

      snprintf( line,MAX_OLED_COLUMNS,"IP %s",state.ipAddr.c_str() );
      storeLine( 1,line );

      int   rsi = WiFi.RSSI();
      snprintf( line,MAX_OLED_COLUMNS,"RSSI : %d dBm",rsi );
      storeLine( 2,line );
      PW_MSG( "RSSI : %d dBm",rsi );

      if ( !m_networking->didAcquireNTP() )
      {
         storeLine( 4,"No NTP" );
      }
      else
      {
         time( &currentTime );

         uint32_t secondsDiff = difftime( currentTime,state.startTime );

         getLocalTime( &timeInfo );
         strftime( m_currentLines[ 4 ],20,"%d/%m/%y : %H:%M:%S",&timeInfo );

         sprintf( m_currentLines[ 5 ],"Uptime %u:%02u:%02u.%02u",secondsDiff / ( 24 * 3600 ), (secondsDiff / 3600) % 24, (secondsDiff / 60) % 60, secondsDiff % 60 );
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

   snprintf( line,MAX_OLED_COLUMNS,"Heap Free" );
   storeLine( 2,line );
   snprintf( line,MAX_OLED_COLUMNS," %u [ %u ] KiB",freeHeap / 1024,largestFreeInternalBlock() / 1024 );
   storeLine( 3,line );

   fs::SPIFFSFS *spiffs = Config::instance()->getSPIFFS();
   storeLine( 4,"SPIFFS" );
   snprintf( line,MAX_OLED_COLUMNS,"Used %u of %u KiB",spiffs->usedBytes()/1024, spiffs->totalBytes()/1024 );
   storeLine( 5,line );

   show( m_currentLines );
}

void  UserIO::showEnergy()
{
   char  line[ MAX_OLED_COLUMNS ];
   const PowerSensor *sensor;

   int i = 0;
   while( ( sensor = m_sample.m_powerSensors[ i ] ) )
   {
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
         sensor = m_sample.m_tempSensors[ i ];
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

   TempSensor *HeatingFlow = findTempSensor( HEATING_FLOW );
   TempSensor *HeatingReturn = findTempSensor( HEATING_RETURN );

   TempSensor *outside = findTempSensor( OUTSIDE );

   if ( hpFlow && hpReturn )
   {
      TemperatureModule::takeMutex();
      snprintf( line,MAX_OLED_COLUMNS,"HP: %3.1f %3.1f (%3.1f)",hpFlow->m_temp,hpReturn->m_temp,hpFlow->m_temp - hpReturn->m_temp );
      TemperatureModule::releaseMutex();
      storeLine( 0,line );
   }

   if ( HeatingFlow && HeatingReturn )
   {
      TemperatureModule::takeMutex();
      snprintf( line,MAX_OLED_COLUMNS,"UF: %3.1f %3.1f (%3.1f)",HeatingFlow->m_temp,HeatingReturn->m_temp,HeatingFlow->m_temp - HeatingReturn->m_temp );
      TemperatureModule::releaseMutex();
      storeLine( 1,line );
   }

   if ( outside )
   {
      TemperatureModule::takeMutex();
      snprintf( line,MAX_OLED_COLUMNS,"OS: %3.1f",outside->m_temp );
      TemperatureModule::releaseMutex();
      storeLine( 2,line );
   }

   TempSensor *loftFlow = findTempSensor( LOFT_FLOW );
   TempSensor *loftReturn = findTempSensor( LOFT_RETURN );

   if ( loftFlow && loftReturn )
   {
      TemperatureModule::takeMutex();
      snprintf( line,MAX_OLED_COLUMNS,"2 : %3.1f %3.1f (%3.1f)",loftFlow->m_temp,loftReturn->m_temp,loftFlow->m_temp - loftReturn->m_temp );
      TemperatureModule::releaseMutex();
      storeLine( 3,line );
   }

   TempSensor *firstFlow = findTempSensor( FIRST_FLOW );
   TempSensor *firstReturn = findTempSensor( FIRST_RETURN );

   if ( firstFlow && firstReturn )
   {
      TemperatureModule::takeMutex();
      snprintf( line,MAX_OLED_COLUMNS,"1 : %3.1f %3.1f (%3.1f)",firstFlow->m_temp,firstReturn->m_temp,firstFlow->m_temp - firstReturn->m_temp );
      TemperatureModule::releaseMutex();
      storeLine( 4,line );
   }

   TempSensor *groundFlow = findTempSensor( GND_FLOW );
   TempSensor *groundReturn = findTempSensor( GND_RETURN );

   if ( groundFlow && groundReturn )
   {
      TemperatureModule::takeMutex();
      snprintf( line,MAX_OLED_COLUMNS,"0 : %3.1f %3.1f (%3.1f)",groundFlow->m_temp,groundReturn->m_temp,groundFlow->m_temp - groundReturn->m_temp );
      TemperatureModule::releaseMutex();
      storeLine( 5,line );
   }

   show( m_currentLines );
}

void  UserIO::showHeatMeter()
{
   if ( m_heatMeter )
   {
      clear();
      m_heatMeter->updateUserIO( this );
   }

   show( m_currentLines );
}

void  UserIO::showCommsStatus()
{
   char  line[ MAX_OLED_COLUMNS ];

   if ( m_networking )
   {
      Networking::Status nwState = m_networking->getStatus();

      snprintf( line,MAX_OLED_COLUMNS,"EMON: tx %u",nwState.emonSent );
      storeLine( 0,line );

      snprintf( line,MAX_OLED_COLUMNS,"[QF,SF] %u,%u",nwState.emonQFails,nwState.emonFails );
      storeLine( 1,line );
   }

   if ( m_modbus )
   {
      uint32_t sends,fails;

      m_modbus->getTransactionCounts( &sends,&fails );

      snprintf( line,MAX_OLED_COLUMNS,"MB: tx %u", sends );
      storeLine( 3,line );

      snprintf( line,MAX_OLED_COLUMNS," Err: %u",fails );
      storeLine( 4,line );

      PW_DEBUG( "modbus info %u %u",sends,fails );
   }

   show( m_currentLines );
}

void  UserIO::showLGStatus()
{
   if ( m_heatPump )
   {
      clear();
      m_heatPump->updateUserIO( this );
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
      case HEAT_METERS:
         showHeatMeter();
         break;
      case COMMS_STATUS:
         showCommsStatus();
         break;
      case LG_STATUS:
         showLGStatus();
         break;
      default :
         PW_WARN( "Unknown display type" );
   }
}

bool  UserIO::setNextScreen()
{
   bool  retVal = true;

   // advance the current screen
   switch( m_currentScreen )
   {
      case NETWORK_STATUS:
         m_currentScreen = STORAGE_STATUS;
         break;
      case STORAGE_STATUS:
         m_currentScreen = ENERGY;
         break;
      case ENERGY:
         m_currentScreen = TEMPERATURES;
         break;
      case TEMPERATURES:
         m_currentScreen = HEAT_METERS;
         break;
      case HEAT_METERS:
         m_currentScreen = COMMS_STATUS;
         break;
      case COMMS_STATUS:
         m_currentScreen = LG_STATUS;
         break;
      case LG_STATUS:
      case NONE:
         m_currentScreen = NETWORK_STATUS;
         break;
   }

   // Now check if possible
   switch ( m_currentScreen )
   {
      case TEMPERATURES:
         retVal = isTemperatureDataAvailable();
         break;
      case ENERGY:
         retVal = isPowerDataAvailable();
         break;
      case COMMS_STATUS:
         if ( GET_REGISTRY_INT( UPDATE_EMONCMS ) != 1 && !m_modbus )
         {
            retVal = false;
         }
         break;
      case LG_STATUS:
         if ( !m_heatPump )
         {
            retVal = false;
         }
         break;
      case HEAT_METERS:
         retVal = isHeatMeterDataAvailable();
         break;
      default:
         break;
   }

   return (retVal);
}

void  UserIO::showNext()
{
   while ( ! setNextScreen() )
   {
      PW_DEBUG( "Try screen %d",m_currentScreen );
   }

   show( m_currentScreen );
}
void  UserIO::refresh()
{
   show( m_currentScreen );
}

void  UserIO::update()
{
   if ( m_measurement )
   {
      m_sample = m_measurement->getLastSample();
   }
}

bool  UserIO::isTemperatureDataAvailable()
{
   for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
   {
      if ( m_sample.m_tempSensors[ i ] )
      {
         return true;
      }
   }

   return false;
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

bool  UserIO::isHeatMeterDataAvailable()
{
   if ( m_heatMeter && m_heatMeter->isMeterAvailable() )
   {
      return true;
   }

   return false;
}
