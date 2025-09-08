
#include "src/config/config.h"

#include "Measurement.h"
#include "Storage.h"
#include "src/network/Networking.h"

#define INVALID_UPDATE_HOUR  25

Measurement::Sample::Sample()
{
   m_sampleTime = 0;

   // we use a nullptr to terminate the sensors when we iterate over them,
   // so the arrays are actually sized with +1.

   for ( int i = 0; i < MAX_TEMP_SENSORS + 1; i++ )
   {
      m_tempSensors[ i ] = nullptr;
   }
   for ( int i = 0; i < MAX_POWER_SENSORS + 1; i++ )
   {
      m_powerSensors[ i ] = nullptr;
   }
   for ( int i = 0; i < MAX_HP_REGISTERS + 1; i++ )
   {
      m_lgRegisters[ i ] = nullptr;
   }
   for ( int i = 0; i < MAX_HEAT_METERS + 1; i++ )
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
             m_dailyEmonFailed( 0 )
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
}

void  Measurement::takeSample( void )
{
   PW_DEBUG( "Measurement::takeSample" );
   static uint sensorIndex = 0;

   uint     currentMS = millis();
   uint8_t  i = 0;

   // get the current sample time
   if ( !sensorIndex )
   {
      time( &m_newSample.m_sampleTime );
   }

   // we get temps, power, LG and heat meter - but only 1 type per invocation so we're not
   // performing max processing in one call
   if ( sensorIndex == 0 )
   {
      TempSensor *tempSensor;

      TemperatureModule::takeMutex();
      while ( ( tempSensor = m_tempModule->readNextSensor( i ) ) != nullptr )
      {
         m_newSample.m_tempSensors[ i ] = tempSensor;

         PW_MSG( "%s [%u] feed %u temp %.2f",tempSensor->m_name,tempSensor->m_id,tempSensor->m_emonFeedId,tempSensor->m_temp );
         i++;
      }
      TemperatureModule::releaseMutex();
   }
   else if ( sensorIndex == 1 )
   {
      i = 0;
      PowerSensor *powerSensor;
      while ( ( powerSensor = m_powerModule->readNextSensor( i ) ) )
      {
         m_newSample.m_powerSensors[ i++ ] = powerSensor;

         PW_MSG( "%s [%u] feed %u power %.0f energy %.0f",powerSensor->m_name,powerSensor->m_id,powerSensor->m_emonFeedId,powerSensor->m_power,powerSensor->m_energy );

         if ( powerSensor->m_id == HEAT_PUMP_ID && m_heatPump )
         {
            m_heatPump->setCurrentKW( powerSensor->m_power );
         }
      }
   }
   else if ( sensorIndex == 2 )
   {
      if ( m_heatPump )
      {
         i = 0;
         LGRegister *lgRegister;
         while ( ( lgRegister = m_heatPump->readNextSensor( i ) ) )
         {
            m_newSample.m_lgRegisters[ i++ ] = lgRegister;
    //     PW_DEBUG( "LG: %s %.1f",lgRegister->m_name,lgRegister->m_name,lgRegister->m_value );
         }
         PW_MSG( "Retrieved %d LG registers",i );
      }
   }
   else if ( sensorIndex == 3 )
   {
      if ( m_heatMeterModule )
      {
         i = 0;
         HeatMeterSensor *heatMeterSensor;

         while ( ( heatMeterSensor = m_heatMeterModule->readNextSensor( i ) ) )
         {
            m_newSample.m_heatMeterSensors[ i++ ] = heatMeterSensor;

            PW_MSG( "%s %.1f %.1f",heatMeterSensor->m_name,heatMeterSensor->m_power,heatMeterSensor->m_flowRate );
         }
      }
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
      m_lastSample = m_newSample;

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
}

const Measurement::Sample &Measurement::getLastSample( void )
{
   return m_lastSample;
}

void  Measurement::updateEmon()
{
   static   float k_errorTemp = 75.0f;
   char     line[ 128 ];
   String   thermometerStr, powerStr,lgStr;

   // Can't update if no network

   if ( ! m_networking )
   {
      return;
   }

   // send any temperatures, power, heat pump and heat meter data

   int i = 0;
   const TempSensor  *tsensor;

   // temps have to be > invalid and < error temp - seen the DS's return +128
   // when master monitor has not retrieved sensible values

   TemperatureModule::takeMutex();
   while ( (tsensor = m_lastSample.m_tempSensors[ i++ ] ) )
   {
      if ( tsensor->m_emonFeedId != 0 && tsensor->m_temp > TEMPERATURE_INVALID &&
                        tsensor->m_temp < k_errorTemp )
      {
         m_networking->sendToEmonCMS( tsensor->m_emonFeedId,tsensor->m_temp );
      }
   }
   TemperatureModule::releaseMutex();

   i = 0;
   const PowerSensor *sensor;
   while( ( sensor = m_lastSample.m_powerSensors[ i++ ] ) )
   {
      if ( sensor->m_power > POWER_INVALID && sensor->m_emonFeedId != 0 )
      {
         m_networking->sendToEmonCMS( sensor->m_emonFeedId,sensor->m_power );
      }
   }

   i = 0;
   const LGRegister *lgReg;
   while( ( lgReg = m_lastSample.m_lgRegisters[ i++ ] ) )
   {
      if ( lgReg->m_emonFeedId != 0 && m_networking )
      {
         m_networking->sendToEmonCMS( lgReg->m_emonFeedId,lgReg->m_value );
      }
   }

   i = 0;
   const HeatMeterSensor *hmSensor;
   while( ( hmSensor = m_lastSample.m_heatMeterSensors[ i++ ] ) )
   {
      if ( hmSensor->m_emonPowerId && hmSensor->m_emonFlowId )
      {
         float_t flowRate, power;

         if ( hmSensor->m_power == HM_POWER_ERROR )
         {
            flowRate = 0;
            power = -1;
         }
         else
         {
            flowRate = hmSensor->m_flowRate;
            power = hmSensor->m_power;
         }

         m_networking->sendToEmonCMS( hmSensor->m_emonFlowId,flowRate );
         m_networking->sendToEmonCMS( hmSensor->m_emonPowerId,power );
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

   int i = 0;
   TemperatureModule::takeMutex();
   while ( m_lastSample.m_tempSensors[ i ] )
   {
      const TempSensor  *sensor = m_lastSample.m_tempSensors[ i ];

      PW_DEBUG( "TS %p %s %f %d",sensor,sensor->m_name,sensor->m_temp,sensor->m_emonFeedId );
      if ( sensor->m_temp > TEMPERATURE_INVALID && sensor->m_emonFeedId != 0 )
      {
         snprintf( line,sizeof(line),"%-30s : %4.1f\n",sensor->m_name,sensor->m_temp );
         thermometerStr += line;
      }

      i++;
   }
   TemperatureModule::releaseMutex();

   i = 0;
   const PowerSensor *sensor;
   while( ( sensor = m_lastSample.m_powerSensors[ i++ ] ) )
   {
      if ( sensor->m_power > POWER_INVALID && sensor->m_emonFeedId != 0 )
      {
         snprintf( line,sizeof(line),"%-30s : Power [%5.1f W] Energy [%5.1f kWhr]\n",sensor->m_name,sensor->m_power, sensor->m_energy / 1000.0 );
         powerStr += line;
      }
   }

   // modbus, then emon stats

   uint32_t   sends,fails;
   float_t    percentSent = 100;

   getModbusStats( &sends,&fails );

   sends -= m_dailyModbusSent;
   fails -= m_dailyModbusFailed;

   m_dailyModbusSent += sends;
   m_dailyModbusFailed += fails;

   if ( sends )
   {
      if ( fails )
      {
         percentSent = (100.0 * ( sends - fails )) / sends;
      }

      snprintf( line,sizeof(line),"\nModbus Requests: %u, Failed: %u - (%.1f %% Ok)\n",sends,fails,percentSent );
      commsStr += line;
   }

   Networking::Status state = m_networking->getStatus();

   state.emonSent -= m_dailyEmonSent;
   state.emonFails -= m_dailyEmonFailed;

   m_dailyEmonSent += state.emonSent;
   m_dailyEmonFailed += state.emonFails;

   if ( state.emonSent )
   {
      percentSent = 100;
      if ( state.emonFails )
      {
         percentSent = (100.0 * state.emonSent) / (state.emonFails + state.emonSent);
      }

      snprintf( line,sizeof(line),"EmonCMS Sent: %u, Failed: %u - (%.1f %% Ok)\n",state.emonSent,state.emonFails,percentSent );

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
