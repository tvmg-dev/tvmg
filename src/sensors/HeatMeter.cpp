#include <cJSON.h>

#include "src/config/Config.h"

#include "src/sensors/HeatMeter.h"

#define HM_MIN_SAMPLING_PERIOD_MS 15000

HeatMeterModule::HeatMeterModule( TemperatureModule *tempModule )
               : m_tempModule( tempModule ),
                 m_isOk( false ),
                 m_sensors(),
                 m_numLocalSensors( 0 ),
                 m_millisLastAquisition( -HM_MIN_SAMPLING_PERIOD_MS ),
                 m_indicator( nullptr )
{
   TVMG_DEBUG( "HeatMeterModule::HeatMeterModule()" );
   TVMG_MSG( "Heat Meter Module Startup" );

   for ( int i = 0; i < MAX_HEAT_METERS; i++ )
   {
      m_sensors[ i ] = nullptr;
   }

   cJSON *root = getAllSensorJSON();

   if ( root && isSensorRequired( HEATMETER_SENSOR_NAME ) )
   {
      cJSON *sensor;
      cJSON_ArrayForEach( sensor,root )
      {
         if ( strcmpcJSON( sensor,"type",HEATMETER_SENSOR_NAME ) == 0 && strcmpcJSON( sensor,"class","UPS3" ) == 0)
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

            TVMG_DEBUG( "Found UPS3 : %s",name.c_str() );

            m_sensors[ m_numLocalSensors ] = new HeatMeter( new GrundfosUPS3( gpio,mode.c_str() ),m_tempModule,
                                                      name,id,emonFlowId,emonPowerId,flowTempId,returnTempId,shc );
            m_sensors[ m_numLocalSensors ]->initialise();

            m_numLocalSensors++;
         }
      }
   }

   if ( m_numLocalSensors )
   {
      TVMG_MSG( "Registered %d heat meters",m_numLocalSensors );
      m_indicator = Indicator::getIndicator( Indicator::HEATMETER,0 );
   }
}

HeatMeterModule::~HeatMeterModule()
{
   TVMG_DEBUG( "HeatMeterModule::~HeatMeterModule()" );
}

void HeatMeterModule::initialise()
{
   TVMG_DEBUG( "HeatMeterModule::initialise()" );
}

void HeatMeterModule::sample()
{
   // Only really sample data every X ms

   if ( m_numLocalSensors && millis() - m_millisLastAquisition > HM_MIN_SAMPLING_PERIOD_MS )
   {
      START_TIMING( "HeatMeter Sample" );

      for ( int i = 0; i < m_numLocalSensors; i++ )
      {
         Indicator::Scoped guard( m_indicator );
         m_sensors[ i ]->takeMeasurement();
         if ( i != m_numLocalSensors - 1 )
         {
            delay( 100 );
         }
      }

      m_millisLastAquisition = millis();

      END_TIMING
   }
}

HeatMeterSensor  *HeatMeterModule::readNextSensor( uint8_t index )
{
   if ( index < m_numLocalSensors )
   {
      return( m_sensors[ index ]->getHeatMeterSensor() );
   }

   return( nullptr );
}

bool  HeatMeterModule::isMeterAvailable()
{
   return( m_numLocalSensors > 0 );
}

HeatMeter::HeatMeter( GrundfosUPS3 *pump,TemperatureModule *tempModule,const String &name,uint8_t id,
                           uint32_t emonFlowId, uint32_t emonPowerId, uint8_t flowTempId,
                           uint8_t returnTempId, float_t shc )
         : m_flowMeter( pump ),
           m_tempModule( tempModule ),
           m_sensor(),
           m_flowTempId( flowTempId ),
           m_returnTempId( returnTempId ),
           m_shc( shc )
{
   TVMG_DEBUG( "HeatMeter::HeatMeter %d",id );

   String sensorName( name );
   sensorName += " : ";
   sensorName += m_flowMeter->getMode();

   setSensorName( HEATMETER,id,sensorName );

   m_sensor.m_id = id;
   m_sensor.m_emonPowerId = emonPowerId;
   m_sensor.m_emonFlowId = emonFlowId;
   m_sensor.m_power = 0;
   m_sensor.m_flowRate = 0;
   m_sensor.m_flowTemp = 0;
   m_sensor.m_returnTemp = 0;
   m_sensor.m_powerConsumed = 0;
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
      const char *name = getSensorName( HEATMETER,m_sensor.m_id ).c_str();
      TVMG_MSG( "Sampling %s",name );

      m_flowMeter->sample();
      m_sensor.m_flowRate = m_flowMeter->getFlowRate();

      if ( m_sensor.m_flowRate == FLOW_RATE_ERROR )
      {
         m_sensor.m_power = HM_POWER_ERROR;
         m_sensor.m_powerConsumed = 0;
      }
      else
      {
         m_sensor.m_flowTemp = m_tempModule->getTemperature( m_flowTempId );
         m_sensor.m_returnTemp = m_tempModule->getTemperature( m_returnTempId );

         m_sensor.m_power = m_sensor.m_flowRate * m_shc * (m_sensor.m_flowTemp - m_sensor.m_returnTemp ) / 60.0;
         m_sensor.m_power *= 1000;

         TVMG_DEBUG( "%s flow %.1f ret %.1f, %.1f l/min",name,m_sensor.m_flowTemp,m_sensor.m_returnTemp,m_sensor.m_flowRate );

         if ( m_sensor.m_power < 1 )
         {
            m_sensor.m_power = 0;
         }

         m_sensor.m_powerConsumed = m_flowMeter->getPowerConsumed();
      }

      TVMG_DEBUG( "%s %.0f W %.1f l/min (consumed %.1f)",name,m_sensor.m_power,m_sensor.m_flowRate,m_sensor.m_powerConsumed );
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
