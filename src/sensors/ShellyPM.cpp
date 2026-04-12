/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */


#include <HTTPClient.h>
#include <cstring>

#include "src/sensors/PowerModule.h"

#include "src/sensors/ShellyPM.h"

#define POWER_MIN_SAMPLING_PERIOD_MS 15000

// Supported devices

#define SHELLY_EM          "EM"
#define SHELLY_MINI_PMG3   "MiniPMG3"
#define SHELLY_PM1G3       "PM1G3"
#define SHELLY_EMG3        "EMG3"

// The 3rd generation devices use the Gen2+ device API.  The status of all componenent of a
// device are retrieved (as JSON) via <url>/Shelly.GetStatus.  The response can then be
// parsed to find the appropriate node with id (e.g. switch:0) and obtain its data.

static ShellyPowerModule *s_shellyInstance = nullptr;

ShellyPowerModule::ShellyPowerModule()
           : m_sensors(),
             m_numSensors( 0 ),
             m_millisLastAquisition( -POWER_MIN_SAMPLING_PERIOD_MS ),
             m_indicator( nullptr )
{
   TVMG_DEBUG( "ShellyPowerModule::ShellyPowerModule()" );
   TVMG_MSG( "Shelly Power Module Startup" );

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
            else if ( model == SHELLY_EMG3 )
            {
               pwrSensor->m_model = EMG3;
            }
            else if ( model == SHELLY_MINI_PMG3 )
            {
               pwrSensor->m_model = PMG3;
            }
            else if ( model == SHELLY_PM1G3)
            {
               pwrSensor->m_model = PM1G3;
            }
            else
            {
               TVMG_ERROR( "Unknown Shelly model" );
            }

            if ( pwrSensor->m_model != UNKNOWN )
            {
               String name = getStringFromcJSON( sensor,"name" );

               pwrSensor->m_data.m_emonFeedId = getIntFromcJSON( sensor,"emonFeedId",0 );
               pwrSensor->m_data.m_id = getIntFromcJSON( sensor,"id",m_numSensors );
               pwrSensor->m_meter = getIntFromcJSON( sensor,"meter",INVALID_EM_METER );

               if ( ! pwrSensor->m_ipAddress.fromString( getStringFromcJSON( sensor,"ipAddress" ) ) )
               {
                  TVMG_ERROR( "Failed to get Shelly IP address" );
               }
               else if ( pwrSensor->m_model == EM && pwrSensor->m_meter == INVALID_EM_METER )
               {
                  TVMG_ERROR( "Shelly EM meter invalid" );
               }
               else
               {
                  pwrSensor->m_isValid = true;

                  pwrSensor->m_data.m_power = POWER_INVALID;
                  pwrSensor->m_data.m_energy = ENERGY_INVALID;

                  // Add name to sensor name map
                  setSensorName( SHELLYPM,pwrSensor->m_data.m_id,name );

                  TVMG_DEBUG( "Power: name %s model %u at %s",name.c_str(),pwrSensor->m_model,pwrSensor->m_ipAddress.toString().c_str() );
                  TVMG_DEBUG( "Id %u,  feed %u",pwrSensor->m_data.m_id,pwrSensor->m_data.m_emonFeedId );
                  m_numSensors++;
               }
            }
         }
      }
   }

   if ( m_numSensors )
   {
      TVMG_MSG( "Registered %d Shelly power sensors",m_numSensors );
      m_indicator = Indicator::getIndicator( Indicator::POWER,1 );
      s_shellyInstance = this;
   }
}

ShellyPowerModule::~ShellyPowerModule()
{
   TVMG_DEBUG( "ShellyPowerModule::~ShellyPowerModule()" );
}

void ShellyPowerModule::initialise()
{
}

void ShellyPowerModule::sample()
{
   if ( m_numSensors && millis() - m_millisLastAquisition > POWER_MIN_SAMPLING_PERIOD_MS )
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

bool ShellyPowerModule::isValidSensor( uint8_t id )
{
   bool isOk = false;

   if ( s_shellyInstance )
   {
      for ( int i = 0; i < s_shellyInstance->m_numSensors; i++ )
      {
         if ( s_shellyInstance->m_sensors[ i ].m_data.m_id == id )
         {
            isOk = true;
            break;
         }
      }
   }

   return isOk;
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

   // regardless of model, clear any previous temperature readings so that
   // consumers don't see stale data when this is not host an add-on accessory.
   sensor->m_temps.clear();

   delay( 100 );
   Indicator::Scoped guard( m_indicator );

   switch( sensor->m_model )
   {
      case EM:
         return getEM( sensor );
         break;
      case PMG3:
         return getPMMiniG3( sensor );
         break;
      case PM1G3:
         return getPM1G3( sensor );
         break;
      case EMG3:
         return getEMG3( sensor );
         break;
      default:
         TVMG_ERROR( "Unknown Shelly type" );
         break;
   }

   return false;
}

cJSON *ShellyPowerModule::getData( const String &query )
{
   cJSON *json = nullptr;

   START_TIMING( query );

   HTTPClient http;
   http.begin( query );

   // Allow 4s to get data from the Shelly

   http.setTimeout( 4000 );

   int httpResponse = http.GET();
   if ( httpResponse == HTTP_CODE_OK  )
   {
      String resp = http.getString();

      if ( resp.length() )
      {
         json = cJSON_Parse( resp.c_str() );
      }
   }

   END_TIMING;

   return( json );
}

void ShellyPowerModule::getTemperatures( PrivateSensor* sensor, cJSON* json )
{
   if ( !json || !sensor )
   {
      return;
   }

   /* parse any temperature:N blocks at the top level */
   for ( cJSON *child = json->child; child; child = child->next )
   {
      if ( child->string && strncmp( child->string,"temperature:",12 ) == 0 )
      {
         cJSON *idNode = cJSON_GetObjectItem( child,"id" );
         cJSON *tempNode = cJSON_GetObjectItem( child,"tC" );
         if ( idNode && tempNode && (idNode->type == cJSON_Number) && (tempNode->type == cJSON_Number) )
         {
            ShellyPowerModule::ShellyTemp temp;
            temp.m_tempId = (uint8_t)idNode->valueint;
            temp.m_value = (float)tempNode->valuedouble;
            sensor->m_temps.push_back( temp );

            TVMG_DEBUG( "located temp id %d, value %.1f", temp.m_tempId,temp.m_value );
         }
      }
   }
}

bool ShellyPowerModule::getEM( PrivateSensor *sensor )
{
   bool retVal = false;

   String restQuery = "http://host/emeter/";
   String meter( sensor->m_meter );
   restQuery += meter;

   restQuery.replace( "host",sensor->m_ipAddress.toString() );

   cJSON *json = getData( restQuery );
   if ( json )
   {
      float power = getFloatFromcJSON( json,"power",POWER_INVALID);

      // put a 3W lower limit in place, seen -ve values returned
      if ( power > POWER_INVALID && power < 3 )
      {
         power = 0;
      }

      sensor->m_data.m_power = power;
      sensor->m_data.m_energy = getFloatFromcJSON( json,"total",ENERGY_INVALID );
      if ( sensor->m_data.m_power > POWER_INVALID && sensor->m_data.m_energy > ENERGY_INVALID )
      {
         retVal = true;
      }
      cJSON_Delete( json );
   }

   TVMG_DEBUG( "EM: %s - %.1f %.0f",getSensorName( SHELLYPM,sensor->m_data.m_id ).c_str(),
                                     sensor->m_data.m_power,sensor->m_data.m_energy );

   if ( !retVal )
   {
      TVMG_ERROR( "Failed to get shelly %s",getSensorName( SHELLYPM,sensor->m_data.m_id ).c_str() );
   }

   return retVal;
}

bool ShellyPowerModule::getPMMiniG3( PrivateSensor *sensor )
{
   bool retVal = false;
   String restQuery = "http://host/rpc/Shelly.GetStatus";

   restQuery.replace( "host",sensor->m_ipAddress.toString() );

   cJSON *json = getData( restQuery );
   if ( json )
   {
      cJSON *node = cJSON_GetObjectItem( json,"pm1:0" );
      if ( node )
      {
         float power = getFloatFromcJSON( node,"apower",POWER_INVALID );

         // put a 3W lower limit in place, seen -ve values returned
         if ( power > POWER_INVALID && power < 3 )
         {
            power = 0;
         }
         sensor->m_data.m_power = power;

         cJSON *aenergy = cJSON_GetObjectItem( node,"aenergy" );
         if ( aenergy )
         {
            sensor->m_data.m_energy = getFloatFromcJSON( aenergy,"total",ENERGY_INVALID );
         }

         if ( sensor->m_data.m_power > POWER_INVALID && sensor->m_data.m_energy > ENERGY_INVALID )
         {
            retVal = true;
         }
      }

      cJSON_Delete( json );
   }

   TVMG_DEBUG( "PMMiniG3: %s - %.1f %.0f",getSensorName( SHELLYPM,sensor->m_data.m_id ).c_str(),
                                     sensor->m_data.m_power,sensor->m_data.m_energy );

   if ( !retVal )
   {
      TVMG_ERROR( "Failed to get shelly %s",getSensorName( SHELLYPM,sensor->m_data.m_id ).c_str() );
   }

   return retVal;

}

bool ShellyPowerModule::getEMG3( PrivateSensor *sensor )
{
   bool retVal = false;
   String restQuery = "http://host/rpc/Shelly.GetStatus";

   restQuery.replace( "host",sensor->m_ipAddress.toString() );

   cJSON *json = getData( restQuery );
   if ( json )
   {
      // get EM1 info for power.

      char nodeName[ 16 ];
      snprintf( nodeName,sizeof(nodeName),"em1:%d",sensor->m_meter );
      cJSON *node = cJSON_GetObjectItem( json,nodeName );
      if ( node )
      {
         float power = getFloatFromcJSON( node,"act_power",POWER_INVALID );

         // put a 3W lower limit in place, seen -ve values returned
         if ( power > POWER_INVALID && power < 3 )
         {
            power = 0;
         }
         sensor->m_data.m_power = power;
      }

      // get EM1Data info for energy.

      snprintf( nodeName,sizeof(nodeName),"em1data:%d",sensor->m_meter );
      node = cJSON_GetObjectItem( json,nodeName );
      if ( node )
      {
         sensor->m_data.m_energy = getFloatFromcJSON( node,"total_act_energy",ENERGY_INVALID );

         if ( sensor->m_data.m_power > POWER_INVALID && sensor->m_data.m_energy > ENERGY_INVALID )
         {
            retVal = true;
         }
      }

      // This model supports add-on accessory, so check for any temperatures

      getTemperatures( sensor,json );

      cJSON_Delete( json );
   }

   TVMG_DEBUG( "EMG3: %s - %.1f %.0f",getSensorName( SHELLYPM,sensor->m_data.m_id ).c_str(),
                                     sensor->m_data.m_power,sensor->m_data.m_energy );

   if ( !retVal )
   {
      TVMG_ERROR( "Failed to get shelly %s",getSensorName( SHELLYPM,sensor->m_data.m_id ).c_str() );
   }

   return retVal;
}

bool ShellyPowerModule::getPM1G3( PrivateSensor *sensor )
{
   bool retVal = false;
   String restQuery = "http://host/rpc/Shelly.GetStatus";

   restQuery.replace( "host",sensor->m_ipAddress.toString() );

   cJSON *json = getData( restQuery );
   if ( json )
   {
      cJSON *node = cJSON_GetObjectItem( json,"switch:0" );
      if ( node )
      {
         float power = getFloatFromcJSON( node,"apower",POWER_INVALID );

         // put a 3W lower limit in place, seen -ve values returned
         if ( power > POWER_INVALID && power < 3 )
         {
            power = 0;
         }
         sensor->m_data.m_power = power;

         cJSON *aenergy = cJSON_GetObjectItem( node,"aenergy" );
         if ( aenergy )
         {
            sensor->m_data.m_energy = getFloatFromcJSON( aenergy,"total",ENERGY_INVALID );
         }

         if ( sensor->m_data.m_power > POWER_INVALID && sensor->m_data.m_energy > ENERGY_INVALID )
         {
            retVal = true;
         }

         // This model supports add-on accessory, so check for any temperatures

         getTemperatures( sensor,json );

         cJSON_Delete( json );
      }
   }

   TVMG_DEBUG( "PM1G3: %s - %.1f %.0f",getSensorName( SHELLYPM,sensor->m_data.m_id ).c_str(),
                                     sensor->m_data.m_power,sensor->m_data.m_energy );

   if ( !retVal )
   {
      TVMG_ERROR( "Failed to get shelly %s",getSensorName( SHELLYPM,sensor->m_data.m_id ).c_str() );
   }

   return retVal;
}

bool ShellyPowerModule::getTemperature( uint8_t shellyId,uint8_t tempId,float *val )
{
   if ( !s_shellyInstance )
   {
      return false;
   }

   for ( int i = 0; i < s_shellyInstance->m_numSensors; i++ )
   {
      if ( s_shellyInstance->m_sensors[ i ].m_data.m_id == shellyId )
      {
         const PrivateSensor &sensor = s_shellyInstance->m_sensors[ i ];
         for ( const auto& tempEntry : sensor.m_temps ) 
         {
            if ( tempEntry.m_tempId == tempId ) 
            {
               *val = tempEntry.m_value;
               return true;
            }
         }
      }
   }

   return false;
}
