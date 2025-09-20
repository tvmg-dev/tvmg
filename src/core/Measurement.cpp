#include <mutex>

#include "src/config/config.h"

#include "Measurement.h"
#include "Storage.h"
#include "src/network/Networking.h"

#define INVALID_UPDATE_HOUR  25

static std::mutex copyMutex;

SemaphoreHandle_t Measurement::s_sampleMutex = nullptr;
uint32_t Measurement::s_mutexAcquiredMillis;

int Measurement::takeSampleMutex( int ms )
{
   if ( ! s_sampleMutex )
   {
      PW_WARN( "No sample mutex" );
      return -1;
   }

   uint32_t startMillis;

   PW_DEBUG( "Take sample mutex" );
   startMillis = millis();
   int ok = xSemaphoreTakeRecursive( s_sampleMutex,ms * portTICK_PERIOD_MS);

   if ( ok != pdTRUE )
   {
      PW_WARN( "Failed to take sample mutex" );
   }
   else
   {
      s_mutexAcquiredMillis = millis();
      PW_DEBUG( "sample mutex took %d ms",s_mutexAcquiredMillis - startMillis );
   }

   return( ok == pdTRUE );
}

void  Measurement::releaseSampleMutex()
{
   if ( s_sampleMutex )
   {
      PW_DEBUG( "sample mutex held for %d",millis() - s_mutexAcquiredMillis );
      xSemaphoreGiveRecursive( s_sampleMutex );
   }
}

Measurement::Sample::Sample() :
             m_tempSensors(),
             m_powerSensors(),
             m_lgRegisters(),
             m_heatMeterSensors()
{
   m_sampleTime = 0;
}

Measurement::Sample::Sample( const Measurement::Sample &other )
{
   m_sampleTime = other.m_sampleTime;

   m_tempSensors = other.m_tempSensors;
   m_powerSensors = other.m_powerSensors;
   m_lgRegisters = other.m_lgRegisters;
   m_heatMeterSensors = other.m_heatMeterSensors;
}

Measurement::Sample & Measurement::Sample::operator=(const Measurement::Sample &other )
{
   if ( this != &other )
   {
      m_sampleTime = other.m_sampleTime;

      m_tempSensors = other.m_tempSensors;
      m_powerSensors = other.m_powerSensors;
      m_lgRegisters = other.m_lgRegisters;
      m_heatMeterSensors = other.m_heatMeterSensors;
   }

   return( *this );
}

Measurement::Measurement( TemperatureModule *tempModule, PowerModule *powerModule,LGHeatPump *heatPump,
                                          HeatMeterModule *hmModule, Storage *storage,Networking *networking )
           : m_tempModule( tempModule ),
             m_powerModule( powerModule ),
             m_heatPump( heatPump ),
             m_heatMeterModule( hmModule ),
             m_storageModule( storage ),
             m_networking( networking ),
             m_lastSample(),
             m_newSample(),
             m_millisLastAquisition( 0 ),
             m_dailyUpdated( false ),
             m_dailyUpdateHour( INVALID_UPDATE_HOUR ),
             m_dailyModbusSent( 0 ),
             m_dailyModbusFailed( 0 ),
             m_dailyEmonSent( 0 ),
             m_dailyEmonFailed( 0 ),
             m_dailySamples( 0 ),
             m_dailySamplesFailed( 0 )
{
   PW_DEBUG( "Measurement::Measurement()" );
   PW_MSG( "Measurement Module Startup" );

   int updateHour = GET_REGISTRY_INT( DAILY_EMAIL_HOUR );
   if ( updateHour >=0 && updateHour <= 23 )
   {
      m_dailyUpdateHour = updateHour;
      PW_DEBUG( "Setting update hour to %u",m_dailyUpdateHour );
   }
}

Measurement::~Measurement()
{
   PW_DEBUG( "Measurement::~Measurement()" );
}

void  Measurement::initialise( void )
{
   PW_DEBUG( "Measurement::initialise" );

   if ( !s_sampleMutex )
   {
      s_sampleMutex = xSemaphoreCreateRecursiveMutex();
   }
}

void  Measurement::takeSample( void )
{
   PW_DEBUG( "Measurement::takeSample" );
   static uint sensorIndex = 0;

   if ( takeSampleMutex( 100 ) != 1 )
   {
      m_dailySamplesFailed++;
      PW_ERROR( "Failed sample %d %d",m_dailySamples,m_dailySamplesFailed );
      return;
   }

   m_dailySamples++;

   uint32_t currentMS = millis();
   uint8_t  i;

   // Reset the new sample if 1st item to sample
   if ( !sensorIndex )
   {
      time( &m_newSample.m_sampleTime );

      m_newSample.m_tempSensors.clear();
      m_newSample.m_powerSensors.clear();
      m_newSample.m_lgRegisters.clear();
      m_newSample.m_heatMeterSensors.clear();
   }

   // we get temps, power, LG and heat meter - but only 1 type per invocation so we're not
   // performing max processing in one call
   if ( sensorIndex == 0 )
   {
      PW_MSG( "Sample : temperatures" );

      i = 0;
      TempSensor *tempSensor;

      m_tempModule->sample();
      while ( ( tempSensor = m_tempModule->readNextSensor( i++ ) ) )
      {
         const char *name = getSensorName( THERM,tempSensor->m_id ).c_str();

         m_newSample.m_tempSensors.push_back( *tempSensor );

         PW_MSG( "%s [%u] feed %u temp %.2f",name,tempSensor->m_id,tempSensor->m_emonFeedId,tempSensor->m_temp );
         PW_MSG( "temp size %d",m_newSample.m_tempSensors.size() );
      }
  }
   else if ( sensorIndex == 1 )
   {
      PW_MSG( "Sample : power" );

      i = 0;
      PowerSensor *powerSensor;

      m_powerModule->sample();
      while ( ( powerSensor = m_powerModule->readNextSensor( i++ ) ) )
      {
         const char *name = getSensorName( POWER,powerSensor->m_id ).c_str();

         m_newSample.m_powerSensors.push_back( *powerSensor );

         if ( powerSensor->m_id == HEAT_PUMP_ID && m_heatPump )
         {
            m_heatPump->setCurrentKW( powerSensor->m_power );
         }

         PW_MSG( "%s [%u] feed %u power %.0f energy %.0f",name,powerSensor->m_id,powerSensor->m_emonFeedId,powerSensor->m_power,powerSensor->m_energy );
      }
   }
   else if ( sensorIndex == 2 && m_heatPump )
   {
      PW_MSG( "Sample : heat pump" );

      i = 0;
      LGRegister *lgRegister;

      m_heatPump->sample();
      while ( ( lgRegister = m_heatPump->readNextSensor( i++ ) ) )
      {
         const char *name = getSensorName( HEATPUMP,lgRegister->m_id ).c_str();

         m_newSample.m_lgRegisters.push_back( *lgRegister );

         if ( lgRegister->m_isValid )
         {
            PW_DEBUG( "LG: %s %.1f",name,lgRegister->m_value );
         }
         else
         {
            PW_DEBUG( "LG: %s invalid",name );
         }
      }

      PW_MSG( "Retrieved %d LG registers",i - 1 );
   }
   else if ( sensorIndex == 3 && m_heatMeterModule )
   {
      PW_MSG( "Sample : heat meter" );

      i = 0;
      HeatMeterSensor *heatMeterSensor;

      m_heatMeterModule->sample();
      while ( ( heatMeterSensor = m_heatMeterModule->readNextSensor( i++ ) ) )
      {
         const char *name = getSensorName( HEATMETER,heatMeterSensor->m_id ).c_str();

         m_newSample.m_heatMeterSensors.push_back( *heatMeterSensor );

         PW_MSG( "%s %.1f %.1f",name,heatMeterSensor->m_power,heatMeterSensor->m_flowRate );
      }
   }

   // if last sample item, then copy the new sample to the last sample
   // member, protect races for last sample access
   if ( sensorIndex == 3 )
   {
      std::lock_guard<std::mutex> lock( copyMutex );
      m_lastSample = m_newSample;
   }

   // We only process data at the sample period, we may be taking measurements
   // more often than that.  We will store the sample and update emon.  Also
   // we will send a daily update if due.
   //
   // As we're currently measuring a sensor at 5s intervals and 'sampling' at 30s
   // then we may have a sample being late by this 5s period, so use a 2s offset
   // to try and avoid drift.

   if ( currentMS - m_millisLastAquisition >= (SAMPLING_PERIOD_MS - 2000) )
   {
      m_millisLastAquisition = currentMS;

      if ( m_storageModule )
      {
         m_storageModule->storeSample( m_lastSample );
      }

      updateEmon();

      if ( shouldSendDailyUpdate() )
      {
         sendUpdate();
      }
   }
   else
   {
      PW_DEBUG( "Measured, but not saved" );
   }

   sensorIndex = (sensorIndex + 1) % 4;

   releaseSampleMutex();
}

const Measurement::Sample &Measurement::getLastSample( void )
{
   std::lock_guard<std::mutex> lock( copyMutex );

   return m_lastSample;
}

void  Measurement::updateEmon()
{
   static   float k_errorTemp = 75.0f;

   // Can't update if no network
   if ( ! m_networking )
   {
      return;
   }

   // send any temperatures, power, heat pump and heat meter data

   for ( int i = 0; i < m_lastSample.m_tempSensors.size(); i++ )
   {
      const TempSensor &sensor = m_lastSample.m_tempSensors[ i ];

      // temps have to be > invalid and < error temp - seen the DS's return +128
      // when master monitor has not retrieved sensible values
      if ( sensor.m_emonFeedId && sensor.m_temp > TEMPERATURE_INVALID &&
                        sensor.m_temp < k_errorTemp )
      {
         m_networking->sendToEmonCMS( sensor.m_emonFeedId,sensor.m_temp );
      }
   }

   for ( int i = 0; i < m_lastSample.m_powerSensors.size(); i++ )
   {
      const PowerSensor &sensor = m_lastSample.m_powerSensors[ i ];

      if ( sensor.m_emonFeedId && sensor.m_power > POWER_INVALID  )
      {
         m_networking->sendToEmonCMS( sensor.m_emonFeedId,sensor.m_power );
      }
   }

   for ( int i = 0; i < m_lastSample.m_lgRegisters.size(); i++ )
   {
      const LGRegister &sensor = m_lastSample.m_lgRegisters[ i ];

      if ( sensor.m_emonFeedId && sensor.m_isValid )
      {
         m_networking->sendToEmonCMS( sensor.m_emonFeedId,sensor.m_value );
      }
   }

   for ( int i = 0; i < m_lastSample.m_heatMeterSensors.size(); i++ )
   {
      const HeatMeterSensor &sensor = m_lastSample.m_heatMeterSensors[ i ];

      if ( sensor.m_emonPowerId && sensor.m_emonFlowId )
      {
         float_t flowRate, power;

         if ( sensor.m_power == HM_POWER_ERROR )
         {
            flowRate = 0;
            power = -1;
         }
         else
         {
            flowRate = sensor.m_flowRate;
            power = sensor.m_power;
         }

         m_networking->sendToEmonCMS( sensor.m_emonFlowId,flowRate );
         m_networking->sendToEmonCMS( sensor.m_emonPowerId,power );
      }
   }
}

bool Measurement::shouldSendDailyUpdate()
{
   if ( m_dailyUpdateHour != INVALID_UPDATE_HOUR )
   {
      struct tm timeInfo;
      localtime_r( &m_lastSample.m_sampleTime,&timeInfo );

      // if the dailyUpdate has been sent and the time is no longer in the
      // hour, then reset the update flag for next time

      if ( m_dailyUpdated && timeInfo.tm_hour != m_dailyUpdateHour )
      {
         PW_DEBUG( "Resetting daily update flag" );
         m_dailyUpdated = false;
      }
      else if ( timeInfo.tm_hour == m_dailyUpdateHour && !m_dailyUpdated && m_networking )
      {
         return true;
      }
   }

   return false;
}

void  Measurement::sendUpdate()
{
   char     line[ 128 ];
   String   thermometerStr,powerStr,lgStr,commsStr;

   PW_MSG( "Sending daily update" );

   for ( int i = 0; i < m_lastSample.m_tempSensors.size();i++ )
   {
      const TempSensor &sensor = m_lastSample.m_tempSensors[ i ];
      const char *name = getSensorName( THERM,sensor.m_id ).c_str();

      PW_DEBUG( "TS %s %f %d",name,sensor.m_temp,sensor.m_emonFeedId );
      if ( sensor.m_temp > TEMPERATURE_INVALID && sensor.m_emonFeedId != 0 )
      {
         snprintf( line,sizeof(line),"%-30s : %4.1f\n",name,sensor.m_temp );
         thermometerStr += line;
      }
   }

   for ( int i = 0; i < m_lastSample.m_powerSensors.size();i++ )
   {
      const PowerSensor &sensor = m_lastSample.m_powerSensors[ i ];
      const char *name = getSensorName( POWER,sensor.m_id ).c_str();

      PW_DEBUG( "PWR %s %f %d",name,sensor.m_power,sensor.m_emonFeedId );
      if ( sensor.m_power > POWER_INVALID && sensor.m_emonFeedId != 0 )
      {
         snprintf( line,sizeof(line),"%-30s : Power [%5.1f W] Energy [%5.1f kWhr]\n",name,sensor.m_power, sensor.m_energy / 1000.0 );
         powerStr += line;
      }
   }

   // modbus, then emon stats

   uint32_t   sends,fails;
   float_t    percentOk = 100;

   getModbusStats( &sends,&fails );

   // m_dailyModbusSent is total 'daily' send value, e.g. Monday 1000, Tuesday 1200, today 800
   // the m_dailyModbusSent would be 2200 when this hits today, the stats from modbus are totals
   // so would be 3000.  We therefore sent today 3000 - 2200 = 800 which is used for the daily
   // stats and we set the new m_dailyModbusSent to 3000 ready for tomorrow.

   sends -= m_dailyModbusSent;
   fails -= m_dailyModbusFailed;

   m_dailyModbusSent += sends;
   m_dailyModbusFailed += fails;

   if ( sends )
   {
      if ( fails )
      {
         percentOk = (100.0 * ( sends - fails )) / sends;
      }

      snprintf( line,sizeof(line),"\nModbus Requests: %u, Failed: %u - (%.1f %% Ok)\n",sends,fails,percentOk );
      commsStr += line;
   }

   Networking::Status state = m_networking->getStatus();

   state.emonSent -= m_dailyEmonSent;
   state.emonFails -= m_dailyEmonFailed;

   m_dailyEmonSent += state.emonSent;
   m_dailyEmonFailed += state.emonFails;

   if ( state.emonSent )
   {
      percentOk = 100;
      if ( state.emonFails )
      {
         percentOk = (100.0 *  (state.emonSent - state.emonFails)) / state.emonSent;
      }

      snprintf( line,sizeof(line),"EmonCMS Submitted: %u, Failed: %u - (%.1f %% Ok)\n",state.emonSent,state.emonFails,percentOk );
      commsStr += line;
   }

   if( m_dailySamples )
   {
      percentOk = 100;
      if ( m_dailySamplesFailed )
      {
         percentOk = (100.0 * (m_dailySamples - m_dailySamplesFailed)) / m_dailySamples;
      }

      snprintf( line,sizeof(line),"Sampled: %u, Failed: %u - (%.1f %% Ok)\n",m_dailySamples,m_dailySamplesFailed,percentOk );
      commsStr += line;
   }

   m_dailyUpdated = true;
   String updateStr;

   char subject[ 64 ];

   snprintf( subject,sizeof(subject),"Daily Update : %s [%s]",m_networking->getLocalMDNSName().c_str(),m_networking->getIPAddress().c_str() );
   snprintf( line,sizeof(line),"Version : %s\n\n",VERSION_STR );

   updateStr += line;
   updateStr += thermometerStr;
   updateStr += powerStr;
   updateStr += commsStr;
   updateStr += "\n\n";

   // Send LG data if we have it, otherwise simple email
   if ( Config::instance()->getSPIFFS()->exists ( LGSTATUS_LOG ) )
   {
      if ( m_networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,updateStr,LGSTATUS_LOG,true ) )
      {
         Config::instance()->getSPIFFS()->remove( LGSTATUS_LOG );
      }
   }
   else
   {
      m_networking->sendEmail( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,updateStr );
   }
}

bool  Measurement::didDailyUpdate()
{
   return m_dailyUpdated;
}

bool Measurement::getTemperature( uint8_t id,float *temp )
{
   bool found = false;

   if ( temp )
   {
      std::lock_guard<std::mutex> lock( copyMutex );

      for ( int i = 0; i < m_lastSample.m_tempSensors.size(); i++ )
      {
         const TempSensor &sensor = m_lastSample.m_tempSensors[ i ];

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

bool Measurement::isTemperatureDataAvailable()
{
   std::lock_guard<std::mutex> lock( copyMutex );

   PW_DEBUG( "last sample temp size %d",m_lastSample.m_tempSensors.size() );
   return ( m_lastSample.m_tempSensors.size() > 0 );
}

bool Measurement::isPowerDataAvailable()
{
   std::lock_guard<std::mutex> lock( copyMutex );

   PW_DEBUG( "last sample pwr size %d",m_lastSample.m_powerSensors.size() );
   return ( m_lastSample.m_powerSensors.size() > 0 );
}

bool Measurement::isHeatMeterDataAvailable()
{
   std::lock_guard<std::mutex> lock( copyMutex );

   PW_DEBUG( "last sample hm size %d",m_lastSample.m_heatMeterSensors.size() );
   return ( m_lastSample.m_heatMeterSensors.size() > 0 );
}
