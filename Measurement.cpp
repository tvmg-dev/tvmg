#include "utils.h"
#include "config.h"

#include "Measurement.h"
#include "Storage.h"
#include "TemperatureModule.h"
#include "PowerModule.h"
#include "PowerModule.h"
#include "Networking.h"

Measurement::Sample::Sample()
{
   m_sampleTime = 0;
   for ( int i = 0; i < MAX_TEMP_SENSORS + 1; i++ )
   {
      m_tempSensors[ i ] = nullptr;
   }
   for ( int i = 0; i < MAX_POWER_SENSORS + 1; i++ )
   {
      m_powerSensors[ i ] = nullptr;
   }
   for ( int i = 0; i < MAX_HP_REGISTERS; i++ )
   {
      m_lgRegisters[ i ] = nullptr;
   }
   for ( int i = 0; i < MAX_HEAT_METERS; i++ )
   {
      m_heatMeterSensors[ i ] = nullptr;
   }
}

Measurement::Sample::Sample( const Measurement::Sample &other )
{
   m_sampleTime = other.m_sampleTime;
   for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
   {
      m_tempSensors[ i ] = other.m_tempSensors[ i ];
      m_actualTemps[ i ] = other.m_actualTemps[ i ];
   }
   for ( int i = 0; i < MAX_POWER_SENSORS; i++ )
   {
      m_powerSensors[ i ] = other.m_powerSensors[ i ];
   }
   for ( int i = 0; i < MAX_HP_REGISTERS; i++ )
   {
      m_lgRegisters[ i ] = other.m_lgRegisters[ i ];
   }
   for ( int i = 0; i < MAX_HEAT_METERS; i++ )
   {
      m_heatMeterSensors[ i ] = other.m_heatMeterSensors[ i ];
   }
}

Measurement::Sample & Measurement::Sample::operator=(const Measurement::Sample &other )
{
   if ( this != &other )
   {
      m_sampleTime = other.m_sampleTime;
      for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
      {
         m_tempSensors[ i ] = other.m_tempSensors[ i ];
         m_actualTemps[ i ] = other.m_actualTemps[ i ];
      }
      for ( int i = 0; i <  MAX_POWER_SENSORS; i++ )
      {
         m_powerSensors[ i ] = other.m_powerSensors[ i ];
      }
      for ( int i = 0; i < MAX_HP_REGISTERS; i++ )
      {
         m_lgRegisters[ i ] = other.m_lgRegisters[ i ];
      }
      for ( int i = 0; i < MAX_HEAT_METERS; i++ )
      {
         m_heatMeterSensors[ i ] = other.m_heatMeterSensors[ i ];
      }
   }

   return( *this );
}

Measurement::Measurement( TemperatureModule *tempModule, PowerModule *powerModule,LGHeatPump *heatPump,
                                          HeatMeterModule *hmModule, Storage *storage )
           : m_tempModule( tempModule ),
             m_powerModule( powerModule ),
             m_heatPump( heatPump ),
             m_heatMeterModule( hmModule ),
             m_storageModule( storage ),
             m_networking( nullptr ),
             m_lastSample(),
             m_millisLastAquisition( 0 )
{
   PW_DEBUG( "Measurement::Measurement()" );
   PW_MSG( "Measurement Module Startup" );
}

Measurement::~Measurement()
{
   PW_DEBUG( "Measurement::~Measurement()" );
}

void  Measurement::initialise( void )
{
   PW_DEBUG( "Measurement::initialise" );
}

void  Measurement::takeSample( void )
{
   PW_DEBUG( "Measurement::takeSample" );
   uint     start;
   float_t  hpKW = 1;

   Sample   newSample;

   start = millis();
   time( &newSample.m_sampleTime );

   // Get all temperature sensor data, then power.

   uint8_t     i = 0;
   TempSensor *tempSensor;

   while ( ( tempSensor = m_tempModule->readNextSensor( i ) ) != nullptr )
   {
      newSample.m_tempSensors[ i ] = tempSensor;

      if ( tempSensor->m_isRemote )
      {
         // Take mutex as we copy temperature across, UDP could be updating

         std::lock_guard<std::mutex> lock(tempSensorMutex);
         newSample.m_actualTemps[ i ] = *tempSensor;
      }
      else
      {
         newSample.m_actualTemps[ i ] = *tempSensor;
      }

      tempSensor = &newSample.m_actualTemps[ i ];

      PW_DEBUG( "%s [%u] feed %u temp %.2f",tempSensor->m_name,tempSensor->m_id,tempSensor->m_emonFeedId,tempSensor->m_temp );

      i++;
   }

   i = 0;
   PowerSensor *powerSensor;
   while ( ( powerSensor = m_powerModule->readNextSensor( i ) ) )
   {
      newSample.m_powerSensors[ i++ ] = powerSensor;

      PW_DEBUG( "%s [%u] feed %u power %.0f energy %.0f",powerSensor->m_name,powerSensor->m_id,powerSensor->m_emonFeedId,powerSensor->m_power,powerSensor->m_energy );

      if ( powerSensor->m_id == HEAT_PUMP_ID )
      {
         hpKW = powerSensor->m_power;
         if ( m_heatPump )
         {
            m_heatPump->setCurrentKW( hpKW );
         }
      }
   }

   if ( m_heatPump )
   {
      i = 0;
      LGRegister *lgRegister;
      while ( ( lgRegister = m_heatPump->readNextSensor( i ) ) )
      {
         newSample.m_lgRegisters[ i++ ] = lgRegister;
 //     PW_DEBUG( "LG: %s %.1f",lgRegister->m_name,lgRegister->m_name,lgRegister->m_value );
      }
      PW_DEBUG( "Retrieved %d LG registers",i );
   }

   if ( m_heatMeterModule )
   {
      i = 0;
      HeatMeterSensor *heatMeterSensor;

      while ( ( heatMeterSensor = m_heatMeterModule->readNextSensor( i ) ) )
      {
         newSample.m_heatMeterSensors[ i++ ] = heatMeterSensor;

         PW_DEBUG( "%s %.1f %.1f",heatMeterSensor->m_name,heatMeterSensor->m_power,heatMeterSensor->m_flowRate );
      }
   }

   m_lastSample = newSample;

   // We only store data at the sample period, we may be taking measurements
   // more often than that.

   if ( start - m_millisLastAquisition >= SAMPLING_PERIOD_MS )
   {
      m_millisLastAquisition = start;

      saveLastSample();
   }
   else
   {
      PW_DEBUG( "Measured, but not saved" );
   }
}

void  Measurement::saveLastSample( void )
{
   if ( m_storageModule )
   {
      m_storageModule->storeSample( m_lastSample );
   }
}

Measurement::Sample Measurement::getLastSample( void )
{
   return m_lastSample;
}
