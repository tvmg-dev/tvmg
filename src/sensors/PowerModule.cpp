#include <cJSON.h>

#include <SD.h>

#include "PowerModule.h"

#include "src/config/hwconfig.h"
#include "src/config/Config.h"

#define POWER_MIN_SAMPLING_PERIOD_MS 15000

PowerModule::PowerModule( ModbusMaster *modbus )
           : m_modbus( modbus ),
             m_sensors(),
             m_numLocalSensors( 0 ),
             m_millisLastAquisition( -POWER_MIN_SAMPLING_PERIOD_MS ),
             m_fakeMeasurements( false )
{
   PW_DEBUG( "PowerModule::PowerModule()" );
   PW_MSG( "Power Module Startup" );

   for ( int i = 0; i < MAX_POWER_SENSORS; i++ )
   {
      m_sensors[ i ].m_isValid = false;
      m_sensors[ i ].m_data.m_power = POWER_INVALID;
      m_sensors[ i ].m_data.m_energy = ENERGY_INVALID;
   }

   cJSON *root = getAllSensorJSON();

   if ( root && isSensorRequired( POWER_SENSOR_NAME ) )
   {
      cJSON *sensor;
      cJSON_ArrayForEach( sensor,root )
      {
         if ( strcmpcJSON( sensor,"type",POWER_SENSOR_NAME ) == 0 )
         {
            PrivateSensor *pwrSensor;
            String name = getStringFromcJSON( sensor,"name" );

            pwrSensor = &m_sensors[ m_numLocalSensors ];

            pwrSensor->m_address = getIntFromcJSON( sensor,"address",m_numLocalSensors );
            pwrSensor->m_data.m_emonFeedId = getIntFromcJSON( sensor,"emonFeedId",0 );
            pwrSensor->m_data.m_id = getIntFromcJSON( sensor,"id",m_numLocalSensors );

            pwrSensor->m_data.m_power = POWER_INVALID;
            pwrSensor->m_data.m_energy = ENERGY_INVALID;
            pwrSensor->m_isValid = true;

            // Add name to sensor name map
            setSensorName( POWER,pwrSensor->m_data.m_id,name );

            PW_DEBUG( "Power: name %s address %u",name.c_str(),pwrSensor->m_address );
            PW_DEBUG( "Id %u,  feed %u",pwrSensor->m_data.m_id,pwrSensor->m_data.m_emonFeedId );
            m_numLocalSensors++;
         }
      }
   }

   if ( m_numLocalSensors )
   {
      PW_MSG( "Registered %d power sensors",m_numLocalSensors );
   }

   if ( GET_REGISTRY_INT( FAKE_MEASUREMENTS ) == 1 )
   {
      m_fakeMeasurements = true;
   }
}

PowerModule::~PowerModule()
{
   PW_DEBUG( "PowerModule::~PowerModule()" );
}

ModbusMaster *PowerModule::getModbus()
{
   return m_modbus;
}

void PowerModule::initialise()
{
   if ( !m_modbus )
   {
      PW_DEBUG( "PowerModule::initialise() - no modbus, fake" );
   }
}

void PowerModule::sample()
{
   if ( millis() - m_millisLastAquisition > POWER_MIN_SAMPLING_PERIOD_MS )
   {
      START_TIMING( "PowerModule Sample" );

      for ( int i = 0; i < m_numLocalSensors; i++ )
      {
         (void) getPower( i );
      }

      m_millisLastAquisition = millis();

      END_TIMING;
   }
}

PowerSensor  *PowerModule::readNextSensor( uint8_t index )
{
   if ( index < m_numLocalSensors )
   {
      return( &m_sensors[ index ].m_data );
   }

   return( nullptr );
}

/*
  RegAddr Description                 Resolution
  0x0000  Voltage value               1LSB correspond to 0.1V
  0x0001  Current value low 16 bits   1LSB correspond to 0.001A
  0x0002  Current value high 16 bits
  0x0003  Power value low 16 bits     1LSB correspond to 0.1W
  0x0004  Power value high 16 bits
  0x0005  Energy value low 16 bits    1LSB correspond to 1Wh
  0x0006  Energy value high 16 bits
  0x0007  Frequency value             1LSB correspond to 0.1Hz
  0x0008  Power factor value          1LSB correspond to 0.01
  0x0009  Alarm status  0xFFFF is alarm，0x0000is not alarm
*/

bool PowerModule::getPower( uint8_t index )
{
   uint8_t  modbusResult;

   if ( m_fakeMeasurements )
   {
      if ( index < m_numLocalSensors )
      {
         if ( m_sensors[ index ].m_data.m_energy == POWER_INVALID )
         {
            m_sensors[ index ].m_data.m_energy = index;
            m_sensors[ index ].m_data.m_power = index;
         }

         m_sensors[ index ].m_data.m_energy += 1;
         m_sensors[ index ].m_data.m_power += 2;
      }

      return true;
   }

   if ( index < m_numLocalSensors && m_sensors[ index ].m_isValid && m_modbus )
   {
      const char *name = getSensorName( POWER,m_sensors[ index ].m_data.m_id ).c_str();

      // force a short delay if necessary
      if ( hwConfig->ModBusMsgDelay > -1 )
      {
         delay( hwConfig->ModBusMsgDelay );
      }

      m_modbus->setSlaveId( m_sensors[ index ].m_address );

      // Read the 9 registers of the PZEM-16
      modbusResult = m_modbus->readInputRegisters( 0x0,9 );

      if ( modbusResult != ModbusMaster::ku8MBSuccess )
      {
         PW_WARN( "Failed to obtain power info for %s",name );
         m_sensors[ index ].m_data.m_energy = ENERGY_INVALID;
         m_sensors[ index ].m_data.m_power = POWER_INVALID;
      }
      else
      {
         uint32_t reg32;
         float_t  power, energy;

         float voltage = m_modbus->getResponseBuffer( 0 ) / 10.0;  //get the 16bit value for the voltage, divide it by 10 and cast in the float variable

         reg32 =  (m_modbus->getResponseBuffer( 2 ) << 16) + m_modbus->getResponseBuffer( 1 );  // Get the 2 16bits registers and combine them to an unsigned 32bit
         float current = reg32 / 1000.0;   // Divide the unsigned 32bit by 1000 and put in the current float variable

         reg32 =  (m_modbus->getResponseBuffer( 4 ) << 16) + m_modbus->getResponseBuffer( 3 );
         power = reg32 / 10.0;

         reg32 =  (m_modbus->getResponseBuffer( 6 ) << 16) + m_modbus->getResponseBuffer( 5 );
         energy = reg32;

         float hz = m_modbus->getResponseBuffer( 7 ) / 10.0;
         float pf = m_modbus->getResponseBuffer( 8 ) / 100.00;

         PW_DEBUG( "%s : %.0f W : %.0f Whr",name,power,energy );

         m_sensors[ index ].m_data.m_energy = energy;
         m_sensors[ index ].m_data.m_power = power;

         PW_DEBUG( "I [%.1f] : V [%.1f] : Freq [%.1f] : PowerFactor [%.1f]",current, voltage, hz, pf );
         return true;
      }
   }

   return false;
}

