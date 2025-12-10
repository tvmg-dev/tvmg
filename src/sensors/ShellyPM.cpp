
#include <HTTPClient.h>
#include <cJSON.h>

#include "ShellyPM.h"
#include "PowerModule.h"

#include "src/config/hwconfig.h"
#include "src/config/Config.h"

#define POWER_MIN_SAMPLING_PERIOD_MS 15000

#define SHELLY_MINI_PMG3   "MiniPMG3"
#define SHELLY_EM          "EM"

ShellyPowerModule::ShellyPowerModule()
           : m_sensors(),
             m_numSensors( 0 ),
             m_millisLastAquisition( -POWER_MIN_SAMPLING_PERIOD_MS )
{
   PW_DEBUG( "ShellyPowerModule::ShellyPowerModule()" );
   PW_MSG( "Shelly Power Module Startup" );

   for ( int i = 0; i < MAX_POWER_SENSORS; i++ )
   {
      m_sensors[ i ].m_isValid = false;
      m_sensors[ i ].m_model = UNKNOWN;
      m_sensors[ i ].m_data.m_power = POWER_INVALID;
      m_sensors[ i ].m_data.m_energy = ENERGY_INVALID;
   }

   cJSON *root = getAllSensorJSON();

   if ( root && isSensorRequired( SHELLY_SENSOR_NAME ) )
   {
      cJSON *sensor;
      cJSON_ArrayForEach( sensor,root )
      {
         if ( strcmpcJSON( sensor,"type",SHELLY_SENSOR_NAME ) == 0 )
         {
            PrivateSensor *pwrSensor = &m_sensors[ m_numSensors ];

            String model = getStringFromcJSON( sensor,"model" );

            if ( model == SHELLY_EM )
            {
               pwrSensor->m_model = EM;
            }
            else if ( model == SHELLY_MINI_PMG3 )
            {
               pwrSensor->m_model = PMG3;
            }
            else
            {
               PW_ERROR( "Unknown Shelly model" );
            }

            if ( pwrSensor->m_model != UNKNOWN )
            {
               String name = getStringFromcJSON( sensor,"name" );

               pwrSensor->m_data.m_emonFeedId = getIntFromcJSON( sensor,"emonFeedId",0 );
               pwrSensor->m_data.m_id = getIntFromcJSON( sensor,"id",m_numSensors );
               pwrSensor->m_meter = getIntFromcJSON( sensor,"meter",INVALID_EM_METER );

               if ( ! pwrSensor->m_ipAddress.fromString( getStringFromcJSON( sensor,"ipAddress" ) ) )
               {
                  PW_ERROR( "Failed to get Shelly IP address" );
               }
               else if ( pwrSensor->m_model == EM && pwrSensor->m_meter == INVALID_EM_METER )
               {
                  PW_ERROR( "Shelly EM meter invalid" );
               }
               else
               {
                  pwrSensor->m_isValid = true;

                  pwrSensor->m_data.m_power = POWER_INVALID;
                  pwrSensor->m_data.m_energy = ENERGY_INVALID;

                  // Add name to sensor name map
                  setSensorName( SHELLYPM,pwrSensor->m_data.m_id,name );

                  PW_DEBUG( "Power: name %s model %u at %s",name.c_str(),pwrSensor->m_model,pwrSensor->m_ipAddress.toString().c_str() );
                  PW_DEBUG( "Id %u,  feed %u",pwrSensor->m_data.m_id,pwrSensor->m_data.m_emonFeedId );
                  m_numSensors++;
               }
            }
         }
      }
   }

   if ( m_numSensors )
   {
      PW_MSG( "Registered %d Shelly power sensors",m_numSensors );
   }
}

ShellyPowerModule::~ShellyPowerModule()
{
   PW_DEBUG( "ShellyPowerModule::~ShellyPowerModule()" );
}

void ShellyPowerModule::initialise()
{
}

void ShellyPowerModule::sample()
{
   if ( millis() - m_millisLastAquisition > POWER_MIN_SAMPLING_PERIOD_MS )
   {
      START_TIMING( "ShellyPowerModule Sample" );

      for ( int i = 0; i < m_numSensors; i++ )
      {
         (void) getPower( i );
      }

      m_millisLastAquisition = millis();

      END_TIMING;
   }
}

ShellyPowerSensor  *ShellyPowerModule::readNextSensor( uint8_t index )
{
   if ( index < m_numSensors )
   {
      return( &m_sensors[ index ].m_data );
   }

   return( nullptr );
}

bool ShellyPowerModule::getPower( uint8_t index )
{
   if ( index > m_numSensors - 1 )
   {
      return false;
   }

   PrivateSensor  *sensor = &m_sensors[ index ];

   if ( !sensor )
   {
      return false;
   }

   sensor->m_data.m_power = POWER_INVALID;
   sensor->m_data.m_energy = ENERGY_INVALID;

   if ( sensor->m_model == PMG3 )
   {
      return getPMG3( sensor );
   }
   else
   {
      return getEM( sensor );
   }
}

bool ShellyPowerModule::getEM( PrivateSensor *sensor )
{
   bool retVal = false;

   String restQuery = "http://host/emeter/";
   String meter( sensor->m_meter );
   restQuery += meter;

   restQuery.replace( "host",sensor->m_ipAddress.toString() );

   HTTPClient http;
   http.begin( restQuery );

   // Allow 4s to get data from the Shelly
   http.setTimeout( 4000 );

   int httpResponse = http.GET();
   if ( httpResponse > 0 )
   {
      String resp = http.getString();
      resp.replace( ":true",":1" );
      resp.replace( ":false",":0" );

      cJSON *json = cJSON_Parse( resp.c_str() );
      if ( json )
      {
         float power = getFloatFromcJSON( json,"power",-100 );

         // put a 3W lower limit in place, seen -ve values returned
         if ( power > -100 && power < 3 )
         {
            power = 0;
         }

         sensor->m_data.m_power = power;
         sensor->m_data.m_energy = getFloatFromcJSON( json,"total",ENERGY_INVALID );
         if ( sensor->m_data.m_power != POWER_INVALID && sensor->m_data.m_energy != ENERGY_INVALID )
         {
            retVal = true;
         }
         cJSON_Delete( json );
      }
   }

   if ( !retVal )
   {
      PW_ERROR( "Failed to get shelly %s",getSensorName( SHELLYPM,sensor->m_data.m_id ).c_str() );
   }

   return retVal;
}

bool ShellyPowerModule::getPMG3( PrivateSensor *sensor )
{
   bool retVal = false;
   String restQuery = "http://host/rpc/PM1.GetStatus?id=0";

   restQuery.replace( "host",sensor->m_ipAddress.toString() );

   HTTPClient http;
   http.begin( restQuery );

   // Allow 4s to get data from the Shelly
   http.setTimeout( 4000 );

   int httpResponse = http.GET();
   if ( httpResponse > 0 )
   {
      String resp = http.getString();
      resp.replace( ":true",":1" );
      resp.replace( ":false",":0" );

      cJSON *root = cJSON_Parse( resp.c_str() );
      if ( root )
      {
         float power = getFloatFromcJSON( root,"apower",-100 );

         // put a 3W lower limit in place, seen -ve values returned
         if ( power > -100 && power < 3 )
         {
            power = 0;
         }
         sensor->m_data.m_power = power;

         cJSON *aenergy = cJSON_GetObjectItem( root,"aenergy" );
         if ( aenergy )
         {
            sensor->m_data.m_energy = getFloatFromcJSON( aenergy,"total",ENERGY_INVALID );
         }

         if ( sensor->m_data.m_power != POWER_INVALID && sensor->m_data.m_energy != ENERGY_INVALID )
         {
            retVal = true;
         }
         cJSON_Delete( root );
      }
   }

   if ( !retVal )
   {
      PW_ERROR( "Failed to get shelly %s",getSensorName( SHELLYPM,sensor->m_data.m_id ).c_str() );
   }

   return retVal;
}

