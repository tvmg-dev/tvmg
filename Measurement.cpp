#include "utils.h"
#include "config.h"

#include "Measurement.h"
#include "Storage.h"
#include "TemperatureModule.h"
#include "PowerModule.h"
#include "PowerModule.h"
#include "Networking.h"

// If we're sampling at 30 seconds, then 10 samples would be 5 minutes
// so we use this to not only limit RAM use but trigger sending to emoncms

#define MAX_MEASUREMENTS_IN_RAM 10

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
}

Measurement::Sample::Sample( const Measurement::Sample &other )
{
   m_sampleTime = other.m_sampleTime;
   for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
   {
      m_tempSensors[ i ] = other.m_tempSensors[ i ];
   }
   for ( int i = 0; i < MAX_POWER_SENSORS; i++ )
   {
      m_powerSensors[ i ] = other.m_powerSensors[ i ];
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
      }
      for ( int i = 0; i <  MAX_POWER_SENSORS; i++ )
      {
         m_powerSensors[ i ] = other.m_powerSensors[ i ];
      }
   }

   return( *this );
}

Measurement::Measurement( TemperatureModule *tempModule, PowerModule *powerModule,Storage *storage )
           : m_tempModule( tempModule ),
             m_powerModule( powerModule ),
             m_storageModule( storage ),
             m_networking( nullptr ),
             m_samples( nullptr ),
             m_numSamples( 0 ),
             m_read( MAX_MEASUREMENTS_IN_RAM ),m_write( MAX_MEASUREMENTS_IN_RAM ),
             m_lastSample(),
             m_millisLastAquisition( 0 )
{
   PW_DEBUG( "Measurement::Measurement()" );
   PW_MSG( "Measurement Module Startup" );

   m_samples = new Sample[ MAX_MEASUREMENTS_IN_RAM ];
}

Measurement::~Measurement()
{
   PW_DEBUG( "Measurement::~Measurement()" );

   free( static_cast<void *> ( m_samples ) );

   delete m_storageModule;
}

void  Measurement::initialise( void )
{
   PW_DEBUG( "Measurement::initialise" );
}

void  Measurement::takeSample( void )
{
   PW_DEBUG( "Measurement::takeSample" );
   uint  start;

   m_lastSample = Sample();

   start = millis();
   time( &m_lastSample.m_sampleTime );

   // Get all temperature sensor data, then power.

   uint8_t     i = 0;
   TempSensor *tempSensor;
   while ( ( tempSensor = m_tempModule->readNextSensor( i ) ) != nullptr )
   {
      m_lastSample.m_tempSensors[ i ] = tempSensor;

      tempSensor = m_lastSample.m_tempSensors[ i ];
      PW_DEBUG( "%s [%u] feed %u temp %.2f",tempSensor->m_name,tempSensor->m_id,tempSensor->m_emonFeedId,tempSensor->m_temp );

      i++;
   }

   i = 0;
   PowerSensor *powerSensor;
   while ( ( powerSensor = m_powerModule->readNextSensor( i ) ) != nullptr )
   {
      m_lastSample.m_powerSensors[ i ] = powerSensor;

      powerSensor = m_lastSample.m_powerSensors[ i ];
      PW_DEBUG( "%s [%u] feed %u power %.0f energy %.0f",powerSensor->m_name,powerSensor->m_id,powerSensor->m_emonFeedId,powerSensor->m_power,powerSensor->m_energy );

      i++;
   }

   // We only store data at the sample period, we may be taking measurements
   // more often than that.

   if ( start - m_millisLastAquisition >= SAMPLING_PERIOD_MS && m_samples )
   {
      m_write = m_write % MAX_MEASUREMENTS_IN_RAM;

      PW_DEBUG( "%u Samples, Writing to %u",m_numSamples + 1,m_write );
      m_samples[ m_write ] = m_lastSample;

      if ( m_millisLastAquisition && !(m_write % MAX_MEASUREMENTS_IN_RAM) )
      {
         PW_DEBUG( "TODO : Should send to emoncms as bulk" );
      }

      m_millisLastAquisition = start;

      saveLastSample();

      m_write++;
      m_numSamples++;
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
