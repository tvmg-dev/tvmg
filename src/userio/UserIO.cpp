#include <Wire.h>
#include <WiFi.h>
#include <SD.h>

#include <time.h>

#include "src/core/utils.h"
#include "src/core/Storage.h"

#include "src/config/Config.h"
#include "src/config/hwconfig.h"

#ifndef TVMG_OLED
   #include "LCDDisplay.h"
#else
   #include "OledDisplay.h"
#endif

#include "UserIO.h"

#undef   LG_VALUE_DEBUG

extern Storage *storageModule;
extern bool userIOHoldScreen;    // in ThermaV.ino - touch pins for now

static TaskHandle_t  threadHandle = NULL;

#define  USERIO_PERIOD_MS  5000

void  updateThread( void *params )
{
   static uint32_t targetMillis = 0,deltaMillis,currentMillis;
   UserIO *userIO = static_cast<UserIO *>(params);

   bool didSetScreenSaver = false;

   if ( ! targetMillis )
   {
      targetMillis = millis();
   }

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

      // our target MS is our original millis at entry of this loop, plus
      // our sampling delay

      targetMillis += USERIO_PERIOD_MS;
      currentMillis = millis();

      // We may need to skip  we've executed too long in this loop

      while ( currentMillis >= targetMillis )
      {
         targetMillis += USERIO_PERIOD_MS;
      }

      deltaMillis = targetMillis - currentMillis;

      delay( deltaMillis );
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

#ifndef TVMG_OLED
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
   PW_MSG( "Show : Network" );

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

void  UserIO::showCommsStatus()
{
   PW_MSG( "Show : Comms Status" );

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

      PW_DEBUG( "modbus stats %u %u",sends,fails );
   }

   show( m_currentLines );
}

void  UserIO::showStorage()
{
   PW_MSG( "Show : Storage" );

   char  line[ MAX_DISPLAY_COLUMNS ];

   snprintf( line,MAX_DISPLAY_COLUMNS,"Version : %s",k_versionStr );
   storeLine( 0,line );

   if ( storageModule )
   {
      storageModule->getStatus( line,MAX_DISPLAY_COLUMNS );
      storeLine( 1,line );
   }

   uint32_t freeHeap = ESP.getFreeHeap();

   snprintf( line,MAX_DISPLAY_COLUMNS,"Heap Free" );
   storeLine( 2,line );
   snprintf( line,MAX_DISPLAY_COLUMNS," %u [ %u ] KiB",freeHeap / 1024,largestFreeInternalBlock() / 1024 );
   storeLine( 3,line );

   storeLine( 4,"Filesys" );
   snprintf( line,MAX_DISPLAY_COLUMNS,"Used %u of %u KiB",tvmgFileSys.usedBytes()/1024,tvmgFileSys.totalBytes() / 1024 );
   storeLine( 5,line );

   show( m_currentLines );
}

void  UserIO::showEnergy()
{
   PW_MSG( "Show : Energy" );

   char  line[ MAX_DISPLAY_COLUMNS ];

   int lineNum = 0;
   for ( int i = 0; i < m_sample.m_powerSensors.size(); i++ )
   {
      const PowerSensor &sensor = m_sample.m_powerSensors[ i ];
      const char *name = getSensorName( POWER,sensor.m_id ).c_str();
      storeLine( lineNum * 2,name );
      snprintf( line,MAX_DISPLAY_COLUMNS,"%.0f W %.0f kWh",sensor.m_power,sensor.m_energy / 1000.0 );
      storeLine( 1 + lineNum * 2,line );
      lineNum++;
   }

   for ( int i = 0; i < m_sample.m_shellyPowerSensors.size(); i++ )
   {
      const ShellyPowerSensor &sensor = m_sample.m_shellyPowerSensors[ i ];
      const char *name = getSensorName( SHELLYPM,sensor.m_id ).c_str();
      storeLine( lineNum * 2,name );
      snprintf( line,MAX_DISPLAY_COLUMNS,"%.0f W %.0f kWh",sensor.m_power,sensor.m_energy / 1000.0 );
      storeLine( 1 + lineNum * 2,line );
      lineNum++;
   }

   show( m_currentLines );
}

bool UserIO::getTemperature( uint8_t id,float *temp )
{
   bool found = false;

   if ( temp )
   {
      for ( int i = 0; i < m_sample.m_tempSensors.size(); i++ )
      {
         const TempSensor &sensor = m_sample.m_tempSensors[ i ];

         if ( sensor.m_id == id )
         {
            *temp = sensor.m_temp;
            found = true;
            break;
         }
      }
   }

   return found;
}

void  UserIO::showTemps()
{
   PW_MSG( "Show : Temperature" );

   char  line[ MAX_DISPLAY_COLUMNS ];

   if ( m_sample.m_tempSensors.size() > 0 )
   {
      float flowT,returnT;

      if ( getTemperature( HEAT_PUMP_FLOW,&flowT ) && getTemperature( HEAT_PUMP_RETURN,&returnT ) )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"HP: %3.1f %3.1f (%3.1f)",flowT,returnT,flowT - returnT );
         storeLine( 0,line );
      }

      if ( getTemperature( HEATING_FLOW,&flowT ) && getTemperature( HEATING_RETURN,&returnT ) )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"UF: %3.1f %3.1f (%3.1f)",flowT,returnT,flowT - returnT );
         storeLine( 1,line );
      }

      if ( getTemperature( OUTSIDE,&flowT ) )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"OS: %3.1f",flowT );
         storeLine( 2,line );
      }

      if ( getTemperature( LOFT_FLOW,&flowT ) && getTemperature( LOFT_RETURN,&returnT ) )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"2: %3.1f %3.1f (%3.1f)",flowT,returnT,flowT - returnT );
         storeLine( 3,line );
      }

      if ( getTemperature( FIRST_FLOW,&flowT ) && getTemperature( FIRST_RETURN,&returnT ) )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"1: %3.1f %3.1f (%3.1f)",flowT,returnT,flowT - returnT );
         storeLine( 4,line );
      }

      if ( getTemperature( GND_FLOW,&flowT ) && getTemperature( GND_RETURN,&returnT ) )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"0: %3.1f %3.1f (%3.1f)",flowT,returnT,flowT - returnT );
         storeLine( 5,line );
      }

      show( m_currentLines );
   }
}

void  UserIO::showHeatMeter()
{
   PW_MSG( "Show : HeatMeter" );

   if ( m_heatMeter && m_sample.m_heatMeterSensors.size() == 1 )
   {
      clear();

      char line[ MAX_DISPLAY_COLUMNS ];

      const HeatMeterSensor &sensor = m_sample.m_heatMeterSensors[ 0 ];

      snprintf( line,MAX_DISPLAY_COLUMNS,"%s",getSensorName( HEATMETER,sensor.m_id ).c_str() );
      storeLine( 0,line );

      snprintf( line,MAX_DISPLAY_COLUMNS,"Watts : %.1f",sensor.m_powerConsumed );
      storeLine( 1,line );

      snprintf( line,MAX_DISPLAY_COLUMNS,"Temp : %3.1f %3.1f",sensor.m_flowTemp,sensor.m_returnTemp );
      storeLine( 4,line );

      if ( sensor.m_power == HM_POWER_ERROR )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"Overflow power" );
         storeLine( 2,line );
      }
      else
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"Flow : %3.1f l/min",sensor.m_flowRate );
         storeLine( 2,line );
         snprintf( line,MAX_DISPLAY_COLUMNS,"Heat : %.0f W",sensor.m_power );
         storeLine( 5,line );
      }

      show( m_currentLines );
   }
}

void UserIO::getLGValue( uint32_t parameter,float_t *value )
{
   if ( value )
   {
      int8_t index = m_heatPump->getRegisterIndex( parameter );

      if ( index != -1 && index < m_sample.m_lgRegisters.size() )
      {
         const char *name = getSensorName( HEATPUMP,m_sample.m_lgRegisters[ index ].m_id ).c_str();
         *value = m_sample.m_lgRegisters[ index ].m_value;
#ifdef LG_VALUE_DEBUG
         PW_DEBUG( "lgvalue %s [%x] %.1f",name,parameter,*value );
#endif
      }
      else
      {
         *value = 0;
      }
   }
}

void  UserIO::showLGStatus()
{
   PW_MSG( "Show : LGStatus" );

   if ( !m_heatPump || m_sample.m_lgRegisters.size() == 0 )
   {
      return;
   }

   char line[ MAX_DISPLAY_COLUMNS ];

   clear();
   do
   {
      if ( !m_sample.m_lgRegisters[ 0 ].m_isValid )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"Modbus Err" );
         storeLine( 0,line );
         break;
      }

      float_t  flowRate,targetTemp;
      getLGValue( FLOW_RATE,&flowRate );
      getLGValue( TARGET_TEMP,&targetTemp );
      snprintf( line,MAX_DISPLAY_COLUMNS,"%.1f l/m. t: %.1f",flowRate,targetTemp );
      storeLine( 0,line );

      float_t inlet,outlet;
      getLGValue( INLET_TEMP,&inlet );
      getLGValue( OUTLET_TEMP,&outlet );
      snprintf( line,MAX_DISPLAY_COLUMNS,"i: %.1f o: %.1f",inlet,outlet );
      storeLine( 1,line );

      float_t compressorStatus;
      getLGValue( COMPRESSOR_STATUS,&compressorStatus );
      if ( compressorStatus < 0.2f )
      {
         snprintf( line,MAX_DISPLAY_COLUMNS,"Compress: OFF" );
         storeLine( 3,line );
         break;
      }

      /* Try and find current power usage */
      float powerConsumed = POWER_INVALID;
      for ( int i = 0; i < m_sample.m_powerSensors.size(); i++ )
      {
         const PowerSensor &sensor = m_sample.m_powerSensors[ i ];
         if ( sensor.m_id == HEAT_PUMP_ID )
         {
            powerConsumed = sensor.m_power;
         }
      }

      float pwr;
      getLGValue( HEATING_POWER,&pwr );
      snprintf( line,MAX_DISPLAY_COLUMNS,"%.0f [%.0f]",pwr,powerConsumed );
      storeLine( 2,line );

      float_t cop,carnotCOP,copRatio;
      float_t highT,lowT;
      getLGValue( COP,&cop );
      getLGValue( LOW_PRESS_TEMP,&lowT );
      getLGValue( HIGH_PRESS_TEMP,&highT );

      if ( highT - lowT > 1.0F )
      {
         carnotCOP = (273 + highT) / ( highT - lowT );
         copRatio = 100.0 * (cop / carnotCOP);
      }
      else
      {
         carnotCOP = 1;
         copRatio = 1;
      }

      PW_DEBUG( "HP COP %.1f %.1f %.0f%",cop,carnotCOP,copRatio );

      snprintf( line,MAX_DISPLAY_COLUMNS,"%.1f %.1f %.0f",cop,carnotCOP,copRatio );
      storeLine( 3,line );

      float_t silent;
      String powerStr;
      getLGValue( SILENT_STATUS,&silent );

      if ( silent > 0.2 )
      {
         powerStr = "[s]";
      }

      float_t cr,compressHz;
      getLGValue( COMPRESSION_RATIO,&cr );
      getLGValue( COMPRESSOR_HZ,&compressHz );

      snprintf( line,MAX_DISPLAY_COLUMNS,"%.0f Hz,%.1f %s",compressHz,cr,powerStr.c_str() );
      storeLine( 4,line );

      snprintf( line,MAX_DISPLAY_COLUMNS,"Evap %.1f cond %.1f",lowT,highT );
      storeLine( 5,line );
   }
   while( 0 );

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
            retVal = (m_sample.m_tempSensors.size() > 0 );
            break;
         case ENERGY:
            retVal = (m_sample.m_powerSensors.size() > 0 || m_sample.m_shellyPowerSensors.size() > 0 );
            break;
         case COMMS_STATUS:
            if ( GET_REGISTRY_INT( UPDATE_EMONCMS ) == 1 || m_modbus )
            {
               retVal = true;
            }
            break;
         case LG_STATUS:
            retVal = (m_sample.m_lgRegisters.size() > 0 );
            break;
         case HEAT_METERS:
            retVal = (m_sample.m_heatMeterSensors.size() > 0 );
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

