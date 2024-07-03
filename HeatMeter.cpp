#include <cJSON.h>

#include "config.h"

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
            if ( strcmp( "HEATMETER",cJSON_GetObjectItem( sensor,"type" )->valuestring ) == 0 )
            {
               if ( strcmp( "UPS3",cJSON_GetObjectItem( sensor,"class" )->valuestring ) == 0 )
               {
                  String name = cJSON_GetObjectItem( sensor,"name" )->valuestring;
                  uint8_t id = static_cast<uint8_t>(cJSON_GetObjectItem( sensor,"id" )->valueint);
                  String mode = cJSON_GetObjectItem( sensor,"mode" )->valuestring;
                  uint8_t gpio = static_cast<uint8_t>(cJSON_GetObjectItem( sensor,"gpio" )->valueint);
                  uint32_t emonFlowId = cJSON_GetObjectItem( sensor,"emonFlowId" )->valueint;
                  uint32_t emonPowerId = cJSON_GetObjectItem( sensor,"emonPowerId" )->valueint;
                  uint8_t flowTempId = static_cast<uint8_t>(cJSON_GetObjectItem( sensor,"flowTempId" )->valueint);
                  uint8_t returnTempId = static_cast<uint8_t>(cJSON_GetObjectItem( sensor,"returnTempId" )->valueint);
                  float_t shc = static_cast<float>(cJSON_GetObjectItem( sensor,"shc" )->valuedouble);

                  PW_DEBUG( "Found UPS3 : %s",name.c_str() );

                  m_sensors[ m_numLocalSensors ] = new HeatMeter( new GrundfosUPS3( gpio,mode.c_str() ),m_tempModule,
                                                            name.c_str(),id,emonFlowId,emonPowerId,flowTempId,returnTempId,shc );
               }

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

HeatMeter::HeatMeter( GrundfosUPS3 *pump,TemperatureModule *tempModule,const String &name,uint8_t id,
                           uint32_t emonFlowId, uint32_t emonPowerId, uint8_t flowTempId,
                           uint8_t returnTempId, float_t shc )
         : m_flowMeter( pump ),
           m_tempModule( tempModule ),
           m_sensor(),
           m_name(),
           m_flowTempId( flowTempId ),
           m_returnTempId( returnTempId ),
           m_shc( shc ),
           m_flowTemp( 0.0 ),
           m_returnTemp( 0.0 )
{
   PW_DEBUG( "HeatMeter::HeatMeter %d",id );

   strncpy( m_name,name.c_str(),MAX_HM_NAME );

   m_sensor.m_id = id;
   m_sensor.m_name = m_name;
   m_sensor.m_emonPowerId = emonPowerId;
   m_sensor.m_emonFlowId = emonFlowId;
   m_sensor.m_power = 0;
   m_sensor.m_flowRate = 0;

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
      PW_MSG( "Sampling %s HM",m_name );

      m_flowMeter->sample();
      m_sensor.m_flowRate = m_flowMeter->getFlowRate();

      m_flowTemp = m_tempModule->getTemperature( m_flowTempId );
      m_returnTemp = m_tempModule->getTemperature( m_returnTempId );

      m_sensor.m_power = m_sensor.m_flowRate * m_shc * (m_flowTemp - m_returnTemp ) / 60.0;
      if ( m_sensor.m_power < 0 )
      {
         m_sensor.m_power = 0;
      }

      PW_MSG( "%s %.1f kW %.1f l/min",m_name,m_sensor.m_power,m_sensor.m_flowRate );
      PW_DEBUG( "%s %.1f %.1f",m_name,m_flowTemp,m_returnTemp );
   }
}

HeatMeterSensor *HeatMeter::getHeatMeterSensor()
{
   return &m_sensor;
}
