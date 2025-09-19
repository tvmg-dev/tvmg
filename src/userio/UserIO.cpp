#include <Wire.h>
#include <WiFi.h>
#include <SD.h>

#include <time.h>

#include "src/core/utils.h"
#include "src/core/Storage.h"

#include "src/config/Config.h"
#include "src/config/hwconfig.h"

#if PW_LCD
   #include "LcdDisplay.h"
#else
   #include "OledDisplay.h"
#endif

#include "UserIO.h"

extern Storage *storageModule;
extern bool userIOHoldScreen;    // in ThermaV.ino - touch pins for now

static TaskHandle_t  threadHandle = NULL;

void  updateThread( void *params )
{
   UserIO *userIO = static_cast<UserIO *>(params);
   int i = 0;
   bool didSetScreenSaver = false;

   while( true )
   {
      // If we're 5 minutes into the boot cycle then turn the screen saver on
      // unless explicitly set in the config

      if ( !didSetScreenSaver && millis() > (5 * 60 * 1000) )
      {
         didSetScreenSaver = true;
         if ( GET_REGISTRY_INT( USERIO_SCREENSAVER ) == -1 )
         {
            SET_REGISTRY( USERIO_SCREENSAVER,1 );
         }
      }

      if ( userIO )
      {
         START_TIMING( "UserIO Show Screen" );
         if ( userIOHoldScreen )
         {
            userIO->refresh();
         }
         else
         {
            userIO->showNext();
         }
         END_TIMING;
      }

      delay( 5000 );
   }
}

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
   PW_MSG( "UserIO Module Startup" );

#ifdef PW_LCD
   m_display = new LcdDisplay;
#else
   m_display = new OledDisplay;
#endif

   for ( int i = 1; i < MAX_DISPLAY_ROWS; i++ )
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

   m_display->initialise();

   xTaskCreatePinnedToCore(
      updateThread,  // thread fn
      "UserIO",      // Name of the task
      (4 * 1024),    // Stack size in bytes
      this,          // no input params
      0,             // Priority
      &threadHandle, // handle
      0 );           // Assign to core 0, core 1 used for main loop
}

void  UserIO::setMeasurement( Measurement *measurement )
{
   m_measurement = measurement;
}

void  UserIO::updateLine( uint8_t lineNum,const char *line,bool isForLog )
{
   if ( lineNum < MAX_DISPLAY_ROWS )
   {
      strncpy( m_currentLines[ lineNum ],line,MAX_DISPLAY_COLUMNS );
      if ( isForLog )
      {
         PW_MSG( line );
      }
   }

   show( m_currentLines );
}

void  UserIO::storeLine( uint8_t lineNum,const char *line )
{
   if ( lineNum < MAX_DISPLAY_ROWS )
   {
      strncpy( m_currentLines[ lineNum ],line,MAX_DISPLAY_COLUMNS );
   }
}


void  UserIO::clear()
{
   for ( int i = 0; i < MAX_DISPLAY_ROWS; i++ )
   {
      m_currentLines[ i ][ 0 ] = 0;
   }

   show( m_currentLines );
}

void  UserIO::show( DisplayLine lines[] )
{
   m_display->show( lines );
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
   char        line[ MAX_DISPLAY_COLUMNS ];
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

      snprintf( line,MAX_DISPLAY_COLUMNS,"%s",state.mdnsName.c_str() );
      storeLine( 0,line );

      snprintf( line,MAX_DISPLAY_COLUMNS,"IP %s",state.ipAddr.c_str() );
      storeLine( 1,line );

      int   rsi = WiFi.RSSI();
      snprintf( line,MAX_DISPLAY_COLUMNS,"RSSI : %d dBm",rsi );
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
   char  line[ MAX_DISPLAY_COLUMNS ];

   snprintf( line,MAX_DISPLAY_COLUMNS,"Version : %s",VERSION_STR );
   storeLine( 0,line );

   if ( storageModule )
   {
      storageModule->getStatus( line );
      storeLine( 1,line );
   }

   uint32_t freeHeap = ESP.getFreeHeap();

   snprintf( line,MAX_DISPLAY_COLUMNS,"Heap Free" );
   storeLine( 2,line );
   snprintf( line,MAX_DISPLAY_COLUMNS," %u [ %u ] KiB",freeHeap / 1024,largestFreeInternalBlock() / 1024 );
   storeLine( 3,line );

   fs::SPIFFSFS *spiffs = Config::instance()->getSPIFFS();
   storeLine( 4,"SPIFFS" );
   snprintf( line,MAX_DISPLAY_COLUMNS,"Used %u of %u KiB",spiffs->usedBytes()/1024, spiffs->totalBytes()/1024 );
   storeLine( 5,line );

   show( m_currentLines );
}

void  UserIO::showEnergy()
{
   char  line[ MAX_DISPLAY_COLUMNS ];
   const PowerSensor *sensor;

   int i = 0;
   while( ( sensor = m_sample.m_powerSensors[ i ] ) )
   {
      const char *name = getSensorName( POWER,sensor->m_id ).c_str();
      storeLine( i * 2,name );
      snprintf( line,MAX_DISPLAY_COLUMNS,"%.0f W %.0f kWh",sensor->m_power,sensor->m_energy / 1000.0 );
      storeLine( 1 + i * 2,line );
      i++;
   }
   show( m_currentLines );
}

void  UserIO::showTemps()
{
   static int k_waitMutexMS = 100;

   char  line[ MAX_DISPLAY_COLUMNS ];

   if ( m_measurement && m_measurement->takeSampleMutex( k_waitMutexMS ) == 1 )
   {
      float flowT,returnT;

      if ( m_measurement->getTemperature( HEAT_PUMP_FLOW,&flowT ) &&
                     m_measurement->getTemperature( HEAT_PUMP_RETURN,&returnT ) )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"HP: %3.1f %3.1f (%3.1f)",flowT,returnT,flowT - returnT );
         storeLine( 0,line );
      }

      if ( m_measurement->getTemperature( HEATING_FLOW,&flowT ) &&
                     m_measurement->getTemperature( HEATING_RETURN,&returnT ) )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"UF: %3.1f %3.1f (%3.1f)",flowT,returnT,flowT - returnT );
         storeLine( 1,line );
      }

      if ( m_measurement->getTemperature( OUTSIDE,&flowT ) )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"OS: %3.1f",flowT );
         storeLine( 2,line );
      }

      if ( m_measurement->getTemperature( LOFT_FLOW,&flowT ) &&
                     m_measurement->getTemperature( LOFT_RETURN,&returnT ) )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"2: %3.1f %3.1f (%3.1f)",flowT,returnT,flowT - returnT );
         storeLine( 3,line );
      }

      if ( m_measurement->getTemperature( FIRST_FLOW,&flowT ) &&
                     m_measurement->getTemperature( FIRST_RETURN,&returnT ) )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"1: %3.1f %3.1f (%3.1f)",flowT,returnT,flowT - returnT );
         storeLine( 4,line );
      }

      if ( m_measurement->getTemperature( GND_FLOW,&flowT ) &&
                     m_measurement->getTemperature( GND_RETURN,&returnT ) )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"0: %3.1f %3.1f (%3.1f)",flowT,returnT,flowT - returnT );
         storeLine( 5,line );
      }

      show( m_currentLines );

      m_measurement->releaseSampleMutex();
   }
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
   char  line[ MAX_DISPLAY_COLUMNS ];

   if ( m_networking )
   {
      Networking::Status nwState = m_networking->getStatus();

      snprintf( line,MAX_DISPLAY_COLUMNS,"EMON: tx %u",nwState.emonSent );
      storeLine( 0,line );

      snprintf( line,MAX_DISPLAY_COLUMNS,"[QF,SF] %u,%u",nwState.emonQFails,nwState.emonFails );
      storeLine( 1,line );
   }

   if ( m_modbus )
   {
      uint32_t sends,fails;

      m_modbus->getTransactionCounts( &sends,&fails );

      snprintf( line,MAX_DISPLAY_COLUMNS,"MB: tx %u", sends );
      storeLine( 3,line );

      snprintf( line,MAX_DISPLAY_COLUMNS," Err: %u",fails );
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
   m_currentScreen = type;

   // If we're in screensaver mode then simply update that

   if ( GET_REGISTRY_INT( USERIO_SCREENSAVER ) == 1 )
   {
      m_display->updateScreensaver();
      return;
   }

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
      case OTA_UPDATE:
         // Do nothing, networking is updating directly
         break;
      default :
         PW_WARN( "Unknown display type" );
   }
}

bool  UserIO::setNextScreen()
{
   bool  retVal = false;

   // if OTA update then we don't make any changes
   if ( m_currentScreen == OTA_UPDATE )
   {
      return true;
   }

   // advance the current screen
   switch( m_currentScreen )
   {
      case NETWORK_STATUS:
         m_currentScreen = STORAGE_STATUS;
         break;
      case STORAGE_STATUS:
         m_currentScreen = COMMS_STATUS;
         break;
      case COMMS_STATUS:
         m_currentScreen = ENERGY;
         break;
      case ENERGY:
         m_currentScreen = TEMPERATURES;
         break;
      case TEMPERATURES:
         m_currentScreen = HEAT_METERS;
         break;
      case HEAT_METERS:
         m_currentScreen = LG_STATUS;
         break;
      case LG_STATUS:
      case NONE:
         m_currentScreen = NETWORK_STATUS;
         break;
   }

   // Now check if possible, first check for non-measurement related screens
   switch ( m_currentScreen )
   {
      case NETWORK_STATUS:
      case STORAGE_STATUS:
         retVal = true;
         break;
      default:
         retVal = false;
   }

   // Check we need to find measurement info
   if ( !retVal && m_measurement )
   {
      m_sample = m_measurement->getLastSample();
      switch ( m_currentScreen )
      {
         case TEMPERATURES:
            retVal = m_measurement->isTemperatureDataAvailable();
            break;
         case ENERGY:
            retVal = m_measurement->isPowerDataAvailable();
            break;
         case COMMS_STATUS:
            if ( GET_REGISTRY_INT( UPDATE_EMONCMS ) == 1 || m_modbus )
            {
               retVal = true;
            }
            break;
         case LG_STATUS:
            if ( m_heatPump )
            {
               retVal = true;
            }
            break;
         case HEAT_METERS:
            retVal = m_measurement->isHeatMeterDataAvailable();
            break;
         default:
            break;
      }
   }

   return (retVal);
}

void  UserIO::showNext()
{
   // Don't update if we're updating or not have a measurement yet

   if ( m_currentScreen == OTA_UPDATE  || ! m_measurement )
   {
      return;
   }

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
