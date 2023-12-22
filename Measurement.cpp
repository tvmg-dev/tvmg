#include "utils.h"
#include "config.h"

#include "Measurement.h"
#include "Storage.h"
#include "TemperatureModule.h"
#include "PowerModule.h"
#include "Networking.h"

// If we're sampling at 30 seconds, then 10 samples would be 5 minutes
// so we use this to not only limit RAM use but trigger sending to emoncms

#define MAX_MEASUREMENTS_IN_RAM 10

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

   m_samples = static_cast<Sample *>(malloc( sizeof( Sample ) * MAX_MEASUREMENTS_IN_RAM ) );
   if ( !m_samples )
   {
      PW_ERROR( "Insufficient memory for %u samples", MAX_MEASUREMENTS_IN_RAM );
   }
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

   start = millis();
   time( &m_lastSample.m_sampleTime );

   m_tempModule->getTemperature( HEATING_FLOW_THERM,&m_lastSample.m_flowHeating );
   m_tempModule->getTemperature( HEATING_RETURN_THERM,&m_lastSample.m_returnHeating );

   if ( GET_REGISTRY_INT( BOARD_TYPE ) == MASTER_BOARD )
   {
      m_tempModule->getTemperature( HEATPUMP_FLOW_THERM,&m_lastSample.m_flowHP );
      m_tempModule->getTemperature( HEATPUMP_RETURN_THERM,&m_lastSample.m_returnHP );
      m_tempModule->getTemperature( OUTSIDE_THERM,&m_lastSample.m_outside );

      m_powerModule->getPower( HEATPUMP_POWER,&m_lastSample.m_powerHP,&m_lastSample.m_energyHP );
      m_powerModule->getPower( IMMERSION_POWER,&m_lastSample.m_powerImmersion,&m_lastSample.m_energyImmersion );
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
         PW_WARN( "TODO : Should send to emoncms as bulk" );
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

Measurement::Sample   Measurement::getLastSample( void )
{
   return m_lastSample;
}

void  Measurement::dumpMeasurements( void )
{
   uint16_t i;

   // Skip until we have saved something..

   if ( m_write == MAX_MEASUREMENTS_IN_RAM )
   {
      return;
   }

   // Have we wrapped ?
   if ( m_numSamples >= MAX_MEASUREMENTS_IN_RAM )
   {
      i = m_write;
      while ( i < MAX_MEASUREMENTS_IN_RAM )
      {
         displayMeasurement( i );
         i++;
      }
   }

   i = 0;
   while ( i < m_write )
   {
      displayMeasurement( i );
      i++;
   }
}

void  Measurement::displayMeasurement( uint16_t index )
{
   if ( m_samples && index < MAX_MEASUREMENTS_IN_RAM )
   {
      Sample sample = m_samples[ index ];
      struct tm   timeInfo;
      char   line[ 32 ];

      localtime_r( &sample.m_sampleTime,&timeInfo );
      strftime( line,20,"%d/%m/%y : %H:%M:%S",&timeInfo );
      PW_DEBUG( "index %u : %s ",index,line );
      PW_DEBUG( "%.1f %.1f %.1f %.1f",sample.m_flowHP,sample.m_returnHP,sample.m_flowHeating,sample.m_returnHeating );
      PW_DEBUG( "%.1f %.1f",sample.m_powerHP,sample.m_powerImmersion );
      PW_DEBUG( "%.1f %.1f",sample.m_energyHP,sample.m_energyImmersion );
   }
}
