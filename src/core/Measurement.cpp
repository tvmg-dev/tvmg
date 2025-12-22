#include <mutex>

#include <cJSON.h>

#include "src/config/config.h"

#include "Measurement.h"
#include "Storage.h"
#include "src/network/Networking.h"
#include "src/network/WebServer.h"

#define INVALID_UPDATE_HOUR  25

static std::recursive_mutex sampleMutex;

static Measurement *s_instance = nullptr;

Measurement::Sample::Sample() :
             m_tempSensors(),
             m_powerSensors(),
             m_shellyPowerSensors(),
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
   m_shellyPowerSensors = other.m_shellyPowerSensors;
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
      m_shellyPowerSensors = other.m_shellyPowerSensors;
      m_lgRegisters = other.m_lgRegisters;
      m_heatMeterSensors = other.m_heatMeterSensors;
   }

   return( *this );
}

Measurement::Measurement( TemperatureModule *tempModule, PowerModule *powerModule,ShellyPowerModule *shellyModule,LGHeatPump *heatPump,
                                          HeatMeterModule *hmModule, Storage *storage,Networking *networking )
           : m_tempModule( tempModule ),
             m_powerModule( powerModule ),
             m_shellyPowerModule( shellyModule ),
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

   s_instance = this;
}

Measurement::~Measurement()
{
   PW_DEBUG( "Measurement::~Measurement()" );
}

Measurement   *Measurement::instance()
{
   return s_instance;
}

void  Measurement::initialise( void )
{
   PW_DEBUG( "Measurement::initialise" );
}

void  Measurement::takeSample( void )
{
   PW_DEBUG( "Measurement::takeSample" );
   static uint sensorIndex = 0;

   uint32_t currentMS = millis();
   uint8_t  i;

   // Reset the new sample if 1st item to sample
   if ( !sensorIndex )
   {
      time( &m_newSample.m_sampleTime );

      m_newSample.m_tempSensors.clear();
      m_newSample.m_powerSensors.clear();
      m_newSample.m_shellyPowerSensors.clear();
      m_newSample.m_lgRegisters.clear();
      m_newSample.m_heatMeterSensors.clear();
   }

   // we get temps, power (including Shelly's), LG and heat meter - but only 1 type per invocation so we're not
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

      i = 0;
      ShellyPowerSensor *shellySensor;

      m_shellyPowerModule->sample();
      while ( ( shellySensor = m_shellyPowerModule->readNextSensor( i++ ) ) )
      {
         const char *name = getSensorName( SHELLYPM,shellySensor->m_id ).c_str();

         m_newSample.m_shellyPowerSensors.push_back( *shellySensor );

         if ( shellySensor->m_id == HEAT_PUMP_ID && m_heatPump )
         {
            m_heatPump->setCurrentKW( shellySensor->m_power );
         }

         PW_MSG( "%s [%u] feed %u power %.0f energy %.0f",name,shellySensor->m_id,shellySensor->m_emonFeedId,shellySensor->m_power,shellySensor->m_energy );
      }

   }
   else if ( sensorIndex == 2 && m_heatPump )
   {
      PW_MSG( "Sample : heat pump" );

      i = 0;
      int regsOk = 0;
      LGRegister *lgRegister;

      m_heatPump->sample();
      while ( ( lgRegister = m_heatPump->readNextSensor( i++ ) ) )
      {
         const char *name = getSensorName( HEATPUMP,lgRegister->m_id ).c_str();

         m_newSample.m_lgRegisters.push_back( *lgRegister );

         if ( lgRegister->m_isValid )
         {
            PW_DEBUG( "LG: %s %.1f",name,lgRegister->m_value );
            regsOk++;
         }
         else
         {
            PW_DEBUG( "LG: %s invalid",name );
         }
      }

      PW_MSG( "Read %d of %d LG registers",regsOk,i - 1 );
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

         PW_MSG( "%s %.1f W %.1f l/min",name,heatMeterSensor->m_power,heatMeterSensor->m_flowRate );
      }
   }

   // if last sample item, then copy the new sample to the last sample
   // member, protect races for last sample access
   if ( sensorIndex == 3 )
   {
      std::lock_guard<std::recursive_mutex> lock( sampleMutex );
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
         m_storageModule->storeSample( m_lastSample,shouldSendDailyUpdate() );
      }

      updateEmon();

      WebServer *server = m_networking->getWebServer();
      if ( server )
      {
         char *json = getSampleJSON();
         if ( json )
         {
            server->updateClients( json );
            free( json );
         }
      }

      if ( shouldSendDailyUpdate() )
      {
         sendDailyUpdate();
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
   std::lock_guard<std::recursive_mutex> lock( sampleMutex );

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

   for ( int i = 0; i < m_lastSample.m_shellyPowerSensors.size(); i++ )
   {
      const ShellyPowerSensor &sensor = m_lastSample.m_shellyPowerSensors[ i ];

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
         if ( sensor.m_power == HM_POWER_ERROR )
         {
            return;
         }

         m_networking->sendToEmonCMS( sensor.m_emonFlowId,sensor.m_flowRate );
         m_networking->sendToEmonCMS( sensor.m_emonPowerId,sensor.m_power );
      }
   }
}

#define CJSON_CHECK_PTR(ptr) do if (!(ptr)) { failed = true; goto exit; } while( 0 )

#define CJSON_ADD_NUM( obj,name,val,isValid ) \
do { \
   cJSON *node = nullptr; \
   if ( isValid && !std::isnan(val) && !std::isinf(val) ) \
   { \
      char buf[ 16 ]; \
      snprintf( buf,sizeof(buf),"%.1f",val ); \
      node = cJSON_CreateRaw( buf ); \
   } \
   else \
   { \
      node  = cJSON_CreateNull(); \
   } \
   CJSON_CHECK_PTR( node ); \
   cJSON_AddItemToObject( obj,name,node ); \
} while(0)

char * Measurement::getSampleJSON()
{
   bool failed = false;
   char *jsonString = nullptr;
   cJSON *parent = nullptr;
   cJSON *child = nullptr;
   static float k_errorTemp = 75.0f;

   // Take the mutex to protect the sample, may be called from webserver

   std::lock_guard<std::recursive_mutex> lock( sampleMutex );

   struct tm timeinfo;
   localtime_r( &m_lastSample.m_sampleTime,&timeinfo );

   char timeBuf[ 32 ];
   strftime( timeBuf,sizeof(timeBuf), "%d/%m/%Y - %H:%M:%S", &timeinfo );

   cJSON *root = cJSON_CreateObject();
   CJSON_CHECK_PTR( root );

   CJSON_CHECK_PTR( cJSON_AddStringToObject( root, "time",timeBuf ) );

   if ( m_lastSample.m_tempSensors.size() )
   {
      parent = root;    // it's at the top level - so we're not testing for remotes/openweather
      CJSON_CHECK_PTR( child = cJSON_AddObjectToObject( parent,"temperatures" ) );

      for ( int i = 0; i < m_lastSample.m_tempSensors.size(); i++ )
      {
         const TempSensor &sensor = m_lastSample.m_tempSensors[ i ];
         const char *name = getSensorName( THERM,sensor.m_id ).c_str();

         float temp = sensor.m_temp;
         CJSON_ADD_NUM( child,name,temp,(temp > TEMPERATURE_INVALID && temp < k_errorTemp ) );
      }
   }

   if ( m_lastSample.m_powerSensors.size() || m_lastSample.m_shellyPowerSensors.size() )
   {
      CJSON_CHECK_PTR( parent = cJSON_AddObjectToObject( root,"power" ) );

      for ( int i = 0; i < m_lastSample.m_powerSensors.size(); i++ )
      {
         const PowerSensor &sensor = m_lastSample.m_powerSensors[ i ];
         const char *name = getSensorName( POWER,sensor.m_id ).c_str();

         CJSON_CHECK_PTR( child = cJSON_AddObjectToObject( parent,name ) );

         CJSON_ADD_NUM( child,"power",sensor.m_power,(sensor.m_power > POWER_INVALID) );
         CJSON_ADD_NUM( child,"energy",sensor.m_energy,(sensor.m_power > POWER_INVALID) );
      }

      for ( int i = 0; i < m_lastSample.m_shellyPowerSensors.size(); i++ )
      {
         const ShellyPowerSensor &sensor = m_lastSample.m_shellyPowerSensors[ i ];
         const char *name = getSensorName( SHELLYPM,sensor.m_id ).c_str();

         CJSON_CHECK_PTR( child = cJSON_AddObjectToObject( parent,name ) );

         CJSON_ADD_NUM( child,"power",sensor.m_power,(sensor.m_power > POWER_INVALID) );
         CJSON_ADD_NUM( child,"energy",sensor.m_energy,(sensor.m_power > POWER_INVALID) );
      }
   }

   if ( m_lastSample.m_heatMeterSensors.size() )
   {
      CJSON_CHECK_PTR( parent = cJSON_AddObjectToObject( root,"HeatMeter" ) );
      for ( int i = 0; i < m_lastSample.m_heatMeterSensors.size(); i++ )
      {
         const HeatMeterSensor &sensor = m_lastSample.m_heatMeterSensors[ i ];
         const char *name = getSensorName( HEATMETER,sensor.m_id ).c_str();

         bool isValid = (sensor.m_power != HM_POWER_ERROR );

         CJSON_CHECK_PTR( child = cJSON_AddObjectToObject( parent,name ) );

         CJSON_ADD_NUM( child,"lpm",sensor.m_flowRate,isValid );
         CJSON_ADD_NUM( child,"power",sensor.m_power,isValid );
         CJSON_ADD_NUM( child,"flow",sensor.m_flowTemp,isValid );
         CJSON_ADD_NUM( child,"return",sensor.m_returnTemp,isValid );
         CJSON_ADD_NUM( child,"watts",sensor.m_powerConsumed,isValid );
      }
   }

   if ( m_lastSample.m_lgRegisters.size() )
   {
      CJSON_CHECK_PTR( parent = cJSON_AddObjectToObject( root,"LG" ) );

      for ( int i = 0; i < m_lastSample.m_lgRegisters.size(); i++ )
      {
         const LGRegister &lgReg = m_lastSample.m_lgRegisters[ i ];
         const char *name = getSensorName( HEATPUMP,lgReg.m_id ).c_str();

         if ( lgReg.m_type == COIL || lgReg.m_type == DISCRETE )
         {
            if ( lgReg.m_value > 0 )
            {
               CJSON_CHECK_PTR( cJSON_AddTrueToObject( parent,name ) );
            }
            else
            {
               CJSON_CHECK_PTR( cJSON_AddFalseToObject( parent,name ) );
            }
         }
         else
         {
            CJSON_ADD_NUM( parent,name,lgReg.m_value,true );
         }
      }
   }

   jsonString = cJSON_PrintUnformatted(root);

   PW_DEBUG( "JSON: %s",jsonString );

exit:
   if ( failed )
   {
      PW_ERROR( "Failed to generate sample json" );
   }

   cJSON_Delete(root);

   return( jsonString );
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

void  Measurement::sendDailyUpdate()
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

   m_dailyUpdated = true;
   String updateStr;

   char subject[ 64 ];

   snprintf( subject,sizeof(subject),"Daily Update : %s [%s]",m_networking->getLocalMDNSName().c_str(),m_networking->getIPAddress().c_str() );
   snprintf( line,sizeof(line),"Version : %s\n\n",k_versionStr );

   updateStr += line;
   updateStr += thermometerStr;
   updateStr += powerStr;
   updateStr += commsStr;
   updateStr += "\n\n";

   // Send LG data if we have it (finalise the HTML first), otherwise simple email
   if ( m_heatPump && Config::instance()->getSPIFFS()->exists ( LGSTATUS_LOG_HTML ) )
   {
      m_heatPump->finaliseHTML();

      if ( m_networking->sendEmailWithAttachment( GET_REGISTRY_STRING( RECIPIENT_EMAIL ),subject,updateStr,LGSTATUS_LOG_HTML,true ) )
      {
         // remove yesterday's and we rename current status to yesterday's.

         Config::instance()->getSPIFFS()->remove( LGSTATUS_YESTERDAY );
         Config::instance()->getSPIFFS()->rename( LGSTATUS_LOG_HTML,LGSTATUS_YESTERDAY );
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
