#include <cJSON.h>

#include "config.h"
#include "UserIO.h"
#include "hwconfig.h"

#include "HeatMeter.h"

HeatMeterModule::HeatMeterModule( TemperatureModule *tempModule )
               : m_tempModule( tempModule ),
                 m_isOk( false ),
                 m_sensors(),
                 m_numLocalSensors( 0 ),
                 m_millisLastAquisition( 0 )
{
   PW_DEBUG( "HeatMeterModule::HeatMeterModule()" );
   PW_MSG( "Heat Meter Module Startup" );

   for ( int i = 0; i < MAX_HEAT_METERS; i++ )
   {
      m_sensors[ i ] = nullptr;
   }

   // Parse the /sensors.dat file for heat meters

   fs::SPIFFSFS *spiffs = Config::instance()->getSPIFFS();
   File file = spiffs->open( "/sensors.dat",FILE_READ );
   if ( !file )
   {
      PW_WARN( "/sensors.dat is missing" );
   }
   else
   {
      String data = file.readStringUntil( '@' );

      cJSON *root = cJSON_Parse( data.c_str() );
      cJSON *sensor;

      if ( cJSON_IsArray( root ) )
      {
         cJSON_ArrayForEach( sensor,root )
         {
            if ( strcmpcJSON( sensor,"type","HEATMETER" ) == 0 && strcmpcJSON( sensor,"class","UPS3" ) == 0)
            {
               String name = getStringFromcJSON( sensor,"name" );
               String mode = getStringFromcJSON( sensor,"mode" );

               uint8_t gpio = hwConfig->PWMGPIO;
               uint8_t id = getIntFromcJSON( sensor,"id",m_numLocalSensors );
               uint32_t emonFlowId = getIntFromcJSON( sensor,"emonFlowId",0 );
               uint32_t emonPowerId = getIntFromcJSON( sensor,"emonPowerId",0 );

               uint8_t flowTempId = getIntFromcJSON( sensor,"flowTempId",m_numLocalSensors + 1 );
               uint8_t returnTempId = getIntFromcJSON( sensor,"returnTempId",m_numLocalSensors + 2 );

               float_t shc = getFloatFromcJSON( sensor,"shc",4.2 );

               PW_DEBUG( "Found UPS3 : %s",name.c_str() );

               m_sensors[ m_numLocalSensors ] = new HeatMeter( new GrundfosUPS3( gpio,mode.c_str() ),m_tempModule,
                                                         name.c_str(),id,emonFlowId,emonPowerId,flowTempId,returnTempId,shc );
               m_sensors[ m_numLocalSensors ]->initialise();

               m_numLocalSensors++;
            }
         }
      }

      cJSON_Delete( root );
      close( file );

      if ( m_numLocalSensors )
      {
         PW_MSG( "Registered %d heat meters",m_numLocalSensors );
      }
      else
      {
         PW_WARN( "No heat meters registered" );
      }
   }
}

HeatMeterModule::~HeatMeterModule()
{
   PW_DEBUG( "HeatMeterModule::~HeatMeterModule()" );
}

void  HeatMeterModule::initialise()
{
   PW_DEBUG( "HeatMeterModule::initialise()" );
}

HeatMeterSensor  *HeatMeterModule::readNextSensor( uint8_t index )
{
   if ( index < m_numLocalSensors && m_sensors[ index ] )
   {
      m_sensors[ index ]->takeMeasurement();
      return( m_sensors[ index ]->getHeatMeterSensor() );
   }

   return( nullptr );
}

bool  HeatMeterModule::isMeterAvailable()
{
   return( m_numLocalSensors > 0 );
}

void  HeatMeterModule::updateUserIO( UserIO *userIO )
{
   char line[ MAX_OLED_COLUMNS ];

   HeatMeter *meter = m_sensors[ 0 ];
   if ( !meter )
   {
      snprintf( line,MAX_OLED_COLUMNS,"No Heat Meter" );
      userIO->storeLine( 0,line );
      return;
   }

   HeatMeterSensor *sensor;
   sensor = meter->getHeatMeterSensor();

   String str = meter->getMode();
   snprintf( line,MAX_OLED_COLUMNS,"Mode  : %s",str.c_str() );
   userIO->storeLine( 0,line );

   str = meter->getBasicData();
   snprintf( line,MAX_OLED_COLUMNS,"Watts : %s",str.c_str() );
   userIO->storeLine( 1,line );

   snprintf( line,MAX_OLED_COLUMNS,"Temp : %3.1f %3.1f",sensor->m_flowTemp,sensor->m_returnTemp );
   userIO->storeLine( 4,line );

   if ( sensor->m_power == HM_POWER_ERROR )
   {
      snprintf( line,MAX_OLED_COLUMNS,"Overflow power" );
      userIO->storeLine( 2,line );
   }
   else
   {
      snprintf( line,MAX_OLED_COLUMNS,"Flow : %3.1f l/min",sensor->m_flowRate );
      userIO->storeLine( 2,line );
      snprintf( line,MAX_OLED_COLUMNS,"Heat : %.0f W",sensor->m_power );
      userIO->storeLine( 5,line );
   }
}

HeatMeter::HeatMeter( GrundfosUPS3 *pump,TemperatureModule *tempModule,const String &name,uint8_t id,
                           uint32_t emonFlowId, uint32_t emonPowerId, uint8_t flowTempId,
                           uint8_t returnTempId, float_t shc )
         : m_flowMeter( pump ),
           m_tempModule( tempModule ),
           m_sensor(),
           m_name(),
           m_flowTempId( flowTempId ),
           m_returnTempId( returnTempId ),
           m_shc( shc )
{
   PW_DEBUG( "HeatMeter::HeatMeter %d",id );

   strncpy( m_name,name.c_str(),MAX_HM_NAME );

   m_sensor.m_id = id;
   m_sensor.m_name = m_name;
   m_sensor.m_emonPowerId = emonPowerId;
   m_sensor.m_emonFlowId = emonFlowId;
   m_sensor.m_power = 0;
   m_sensor.m_flowRate = 0;
   m_sensor.m_flowTemp = 0;
   m_sensor.m_returnTemp = 0;
}

HeatMeter::~HeatMeter()
{
}

void  HeatMeter::initialise()
{
   if ( m_flowMeter )
   {
      m_flowMeter->initialise();
   }
}

void  HeatMeter::takeMeasurement()
{
   if ( m_flowMeter && m_tempModule )
   {
      PW_MSG( "Sampling %s",m_name );

      m_flowMeter->sample();
      m_sensor.m_flowRate = m_flowMeter->getFlowRate();

      if ( m_sensor.m_flowRate == FLOW_RATE_ERROR )
      {
         m_sensor.m_power = HM_POWER_ERROR;
      }
      else
      {
         m_sensor.m_flowTemp = m_tempModule->getTemperature( m_flowTempId );
         m_sensor.m_returnTemp = m_tempModule->getTemperature( m_returnTempId );

         m_sensor.m_power = m_sensor.m_flowRate * m_shc * (m_sensor.m_flowTemp - m_sensor.m_returnTemp ) / 60.0;
         m_sensor.m_power *= 1000;

         PW_DEBUG( "%s flow %.1f ret %.1f, %.1f l/min",m_name,m_sensor.m_flowTemp,m_sensor.m_returnTemp,m_sensor.m_flowRate );

         if ( m_sensor.m_power < 0 )
         {
            m_sensor.m_power = 0;
         }
      }

      PW_MSG( "%s %.0f W %.1f l/min",m_name,m_sensor.m_power,m_sensor.m_flowRate );
   }
}

HeatMeterSensor *HeatMeter::getHeatMeterSensor()
{
   return &m_sensor;
}

String   HeatMeter::getMode()
{
   if ( m_flowMeter )
   {
      return( m_flowMeter->getMode() );
   }

   return String();
}

// get some information for the HM, in this
// case we return the underlying power consumed by the UPS3

String HeatMeter::getBasicData()
{
   String ret;

   if ( m_flowMeter )
   {
      float_t  watts = m_flowMeter->getPowerConsumed();
      ret = String( watts,1 );
   }
   return ret;
}
