/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#include <mutex>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <AsyncUDP.h>

#include "src/config/Config.h"
#include "src/core/Measurement.h"

#include "src/config/hwconfig.h"
#include "src/network/Networking.h"

#include "src/sensors/TemperatureModule.h"
#include "src/sensors/ShellyPM.h"

#define TEMPERATURE_PRECISION                11
#define TEMPERATURE_MIN_SAMPLING_PERIOD_MS   15000

char  s_udpPacket[ 512 ];

static std::mutex remoteMutex;

uint8_t toHex( char a )
{
   int8_t n;

   if ( a < 48 )
      goto error;

   n = a - 48;
   if ( n <= 9 )
      return static_cast<uint8_t>( n );

   if ( n < 17 | n > 23 )
      goto error;

   return static_cast<uint8_t>( n - 7 );

error:
   TVMG_ERROR( "Invalid char %c",a );
   return 17;
}

TemperatureModule::TemperatureModule()
         : m_oneWireController( nullptr ),
           m_dallasController( nullptr ),
           m_udp( nullptr ),
           m_sensors(),
           m_numSensors( 0 ),
           m_haveRemoteSensors( false ),
           m_sendPort( -1 ),
           m_millisLastAquisition( -TEMPERATURE_MIN_SAMPLING_PERIOD_MS ),
           m_indicator( nullptr )

{
   TVMG_DEBUG( "TemperatureModule::TemperatureModule()" );
   TVMG_MSG( "Temperature Module Startup" );

   for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
   {
      m_sensors[ i ].m_isValid = false;
      m_sensors[ i ].m_type = DS18B20;
      m_sensors[ i ].m_data.m_id = 255;
      m_sensors[ i ].m_data.m_emonFeedId = 0;
      m_sensors[ i ].m_data.m_temp = TEMPERATURE_INVALID;
   }

   cJSON *root = getAllSensorJSON();

   if ( root && isSensorRequired( TEMPERATURE_SENSOR_NAME ) )
   {
      int   sensorNum = 1;
      cJSON *sensor;

      cJSON_ArrayForEach( sensor,root )
      {
         if ( strcmpcJSON( sensor,"type",TEMPERATURE_SENSOR_NAME ) == 0 )
         {
            PrivateSensor *tempSensor = &m_sensors[ m_numSensors ];
            String name = getStringFromcJSON( sensor,"name" );

            tempSensor->m_data.m_id = getIntFromcJSON( sensor,"id",sensorNum++ );
            tempSensor->m_data.m_emonFeedId = getIntFromcJSON( sensor,"emonFeedId",0 );

            // Add the name to the sensor name map

            TVMG_DEBUG( "Attempting to add %s",name.c_str() );
            setSensorName( THERM,tempSensor->m_data.m_id,name );

            // Now add the sensor depending on type
            bool added = addOpenWeather( sensor );
            if ( !added )
            {
               added = addRemote( sensor );
            }

            if ( !added )
            {
               added = addShellyAddOn( sensor );
            }

            addLocal( sensor );
         }
      }

      if ( m_numSensors )
      {
         TVMG_MSG( "Registered %d thermometers",m_numSensors );
         m_indicator = Indicator::getIndicator( Indicator::THERM,0 );
      }
   }
}

TemperatureModule::~TemperatureModule()
{
   TVMG_DEBUG( "TemperatureModule::~TemperatureModule()" );

   delete m_dallasController;
   delete m_oneWireController;
}

bool TemperatureModule::addOpenWeather( cJSON *sensor )
{
   bool added = false;

   if ( getBoolFromcJSON( sensor,"openweather",false ) )
   {
      PrivateSensor *tempSensor = &m_sensors[ m_numSensors ];
      OpenWeatherSensor *openSensor = &m_sensors[ m_numSensors ].m_openWeather;

      String name = getSensorName( THERM,tempSensor->m_data.m_id );

      String lattitude = getStringFromcJSON( sensor,"lat" );
      String longitude = getStringFromcJSON( sensor,"lon" );
      String appid = getStringFromcJSON( sensor,"appid" );

      if ( lattitude.length() && longitude.length() && appid.length() )
      {
         String url = "https://api.openweathermap.org/data/2.5/weather?lat=LAT&lon=LONG&appid=APPID";
         url.replace( "LAT",lattitude );
         url.replace( "LONG",longitude );
         url.replace( "APPID",appid );

         openSensor->m_url = static_cast<char *> (malloc( url.length() + 1 ));
         if ( openSensor->m_url )
         {
            strcpy( openSensor->m_url,url.c_str() );

            TVMG_DEBUG( "OpenWeather: name %s at %s",name.c_str(),openSensor->m_url );
            TVMG_DEBUG( "Id %u, feed %u",tempSensor->m_data.m_id,tempSensor->m_data.m_emonFeedId );

            tempSensor->m_isValid = true;
            tempSensor->m_type = OPENWEATHERAPI;
            m_numSensors++;

            added = true;
         }
      }
   }

   return added;
}

bool TemperatureModule::addRemote( cJSON *sensor )
{
   bool added = false;

   if ( getBoolFromcJSON( sensor,"remote",false ) )
   {
      PrivateSensor *tempSensor = &m_sensors[ m_numSensors ];

      String name = getSensorName( THERM,tempSensor->m_data.m_id );

      m_haveRemoteSensors = true;

      TVMG_DEBUG( "Remote Therm: name %s",name.c_str() );
      TVMG_DEBUG( "Id %u, feed %u",tempSensor->m_data.m_id,tempSensor->m_data.m_emonFeedId );

      tempSensor->m_isValid = true;
      tempSensor->m_type = REMOTE;
      m_numSensors++;

      added = true;
   }

   return added;
}

bool TemperatureModule::addShellyAddOn( cJSON *sensor )
{
   bool added = false;

   if ( getBoolFromcJSON( sensor,"shellyAddOn",false ) )
   {
      PrivateSensor *tempSensor = &m_sensors[ m_numSensors ];
      ShellyAddOnSensor *shellySensor = &m_sensors[ m_numSensors ].m_shelly;

      String name = getSensorName( THERM,tempSensor->m_data.m_id );

      shellySensor->m_shellyParentId = getIntFromcJSON( sensor,"shellyParentId");
      shellySensor->m_shellyId = getIntFromcJSON( sensor,"shellyId");

      if ( shellySensor->m_shellyId != -1 && shellySensor->m_shellyParentId != -1 )
      {
         if ( !ShellyPowerModule::isValidSensor( shellySensor->m_shellyParentId ) )
         {
            TVMG_WARN( "No valid ShellyPM with id %d",shellySensor->m_shellyParentId );
         }
         else
         {
            tempSensor->m_isValid = true;
            tempSensor->m_type = SHELLYADDON;

            TVMG_DEBUG( "Shelly Therm: name %s",name.c_str() );
            TVMG_DEBUG( "Id %u, feed %u",tempSensor->m_data.m_id,tempSensor->m_data.m_emonFeedId );
            TVMG_DEBUG( "Shelly parent id %d, device id %d",shellySensor->m_shellyParentId,shellySensor->m_shellyId );

            m_numSensors++;
            added = true;
         }
      }
   }

   return added;
}

bool TemperatureModule::addLocal( cJSON *sensor )
{
   bool added = false;

   PrivateSensor *tempSensor = &m_sensors[ m_numSensors ];
   DS1820BSensor *dsSensor = &m_sensors[ m_numSensors ].m_ds18b20;

   String name = getSensorName( THERM,tempSensor->m_data.m_id );

   String addrString = getStringFromcJSON( sensor,"address" );
   if ( addrString.length() == 16 )
   {
      strncpy( dsSensor->m_addressStr,addrString.c_str(),sizeof( dsSensor->m_addressStr ) - 1 );
      dsSensor->m_calibrationOffset = getFloatFromcJSON( sensor,"calibration",0 );

      TVMG_DEBUG( "address string %s",addrString.c_str() );
      for ( int i = 0; i < 8; i++ )
      {
         uint8_t  byte;
         byte = toHex( dsSensor->m_addressStr[ i * 2 ] );
         byte <<= 4;
         byte |= toHex( dsSensor->m_addressStr[ (i * 2) + 1 ] );
         dsSensor->m_address[ i ] = byte;
      }
      char addr[ 32 ];
      getAddressString( dsSensor->m_address,addr );

      TVMG_DEBUG( "Local Therm: name %s address %s",name.c_str(),addr );
      TVMG_DEBUG( "Id %u, feed %u, cal %.2f ",tempSensor->m_data.m_id,tempSensor->m_data.m_emonFeedId,dsSensor->m_calibrationOffset );

      tempSensor->m_isValid = true;
      tempSensor->m_type = DS18B20;
      m_numSensors++;

      added = true;
   }

   return added;
}

void  TemperatureModule::initialise()
{
   bool haveLocalSensors = false;

   for ( int i = 0; i < m_numSensors; i++ )
   {
      if ( m_sensors[ i ].m_type == DS18B20 )
      {
         haveLocalSensors = true;
         break;
      }
   }

   TVMG_DEBUG( "TemperatureModule::initialise()" );

   if ( hwConfig->OneWireGPIO == -1 )
   {
      TVMG_DEBUG( "TemperatureModule::initialise() - No 1-Wire GPIO" );
   }
   else if ( haveLocalSensors )
   {
      TVMG_MSG( "Initialising Local DS18B20 sensors" );

      m_oneWireController = new OneWire( hwConfig->OneWireGPIO );
      m_dallasController = new DallasTemperature( m_oneWireController );

      DeviceAddress  locatedAddresses[ MAX_TEMP_SENSORS ];

      // start the DallasTemperature object

      m_dallasController->begin();

      // Confirm all devices located on the bus, and that we have power

      uint8_t devices = m_dallasController->getDeviceCount();

      int numLocalDS1820 = 0;
      for ( int i = 0; i < m_numSensors; i++ )
      {
         if ( m_sensors[ i ].m_isValid && m_sensors[ i ].m_type == DS18B20 )
         {
            numLocalDS1820++;
         }
      }

      if ( devices == numLocalDS1820 )
      {
         TVMG_DEBUG( "%u sensors detected on the OneWire bus ",devices );
      }
      else
      {
         TVMG_ERROR( "Located %u of %u sensors.",devices,numLocalDS1820 );
      }

      if ( m_dallasController->isParasitePowerMode() )
      {
         TVMG_WARN( "Dallas Controller operating with no power ?" );
      }

      char addrString[ 1 + sizeof( DeviceAddress ) * 2 ];
      for ( int i = 0; i < devices; i++ )
      {
         m_dallasController->getAddress( locatedAddresses[ i ],i );
         getAddressString( locatedAddresses[ i ],addrString );
         TVMG_MSG( "DS18B20 : %s",addrString );
      }

      // Now check for the sensors being located, this is to find the index
      // on the bus.

      for ( int i = 0; i < m_numSensors; i++ )
      {
         if ( m_sensors[ i ].m_isValid && m_sensors[ i ].m_type == DS18B20 )
         {
            const char *name = getSensorName( THERM,m_sensors[ i ].m_data.m_id ).c_str();
            m_sensors[ i ].m_ds18b20.m_busIndex = MAX_TEMP_SENSORS;

            TVMG_DEBUG( "Locating %s",name );
            for ( int j = 0; j < devices; j++ )
            {
               if ( !memcmp( locatedAddresses[ j ],m_sensors[ i ].m_ds18b20.m_address,sizeof( DeviceAddress ) ) )
               {
                  TVMG_DEBUG( "...at bus index %u",j );
                  m_sensors[ i ].m_ds18b20.m_busIndex = j;
                  break;
               }
            }

            if ( m_sensors[ i ].m_ds18b20.m_busIndex == MAX_TEMP_SENSORS )
            {
               TVMG_ERROR( "Failed to locate %s on the bus",name );
               m_sensors[ i ].m_isValid = false;
            }
         }
      }

      // Globally set the resolution to 11 bits per device

      if ( m_numSensors > 0 )
      {
         TVMG_DEBUG( "Setting %u bit precision for sensors",TEMPERATURE_PRECISION );

         m_dallasController->setResolution( TEMPERATURE_PRECISION );

         TVMG_MSG( "Dallas setup completed OK" );
      }
   }

   // Get UDP for broadcast rx/tx

   m_udp = Networking::getUDP();

   // are we broadcasting data ?
   m_sendPort = GET_REGISTRY_INT( BROADCAST_UDP_PORT );

   // are we listening ?
   if ( m_haveRemoteSensors && Networking::getListenUDP() )
   {
      addUDPListener();
   }
}

TempSensor *TemperatureModule::readNextSensor( uint8_t index )
{
   if ( index < m_numSensors )
   {
      if ( m_sensors[ index ].m_type != REMOTE )
      {
         return( &m_sensors[ index ].m_data );
      }

      // We'll hold the remote mutex sensor to prevent an update to the remotes

      std::lock_guard<std::mutex> lock( remoteMutex );
      return( &m_sensors[ index ].m_data );
   }

   return( nullptr );
}

void TemperatureModule::sample()
{
   if ( m_numSensors && millis() - m_millisLastAquisition > TEMPERATURE_MIN_SAMPLING_PERIOD_MS )
   {
      START_TIMING( "Temperature Sample" );

      getTemperatures();
      m_millisLastAquisition = millis();

      END_TIMING;
   }
}

void TemperatureModule::addUDPListener()
{
   uint16_t  listenPort = GET_REGISTRY_INT( LISTEN_UDP_PORT );

   AsyncUDP *udp = Networking::getListenUDP();
   if ( listenPort == -1 || ! udp )
   {
      TVMG_ERROR( "Can't listen as no port & UDP device" );
      return;
   }

   if( !udp->listen( listenPort ) )
   {
      TVMG_ERROR( "Failed to setup UDP listener on port %d",listenPort );
   }
   else
   {
      TVMG_MSG( "Adding UDP listener %d",listenPort );
      udp->onPacket([ & ](AsyncUDPPacket packet) {
         if ( packet.length() < sizeof( s_udpPacket ) - 1 )
         {
            strncpy( s_udpPacket,reinterpret_cast<const char *>(packet.data()),packet.length() );
            s_udpPacket[ packet.length() ] = '\0';

            cJSON *root = cJSON_Parse( s_udpPacket );
            if ( root )
            {
               cJSON *sensors = cJSON_GetObjectItem( root,"sensors" );
               cJSON *sensor;
               int   sensorNum = 1;

               int numAssigned = 0;
               cJSON_ArrayForEach( sensor,sensors )
               {
                  uint8_t  id = getIntFromcJSON( sensor,"id",sensorNum++ );
                  float_t  value = getFloatFromcJSON( sensor,"value",TEMPERATURE_INVALID );

                  std::lock_guard<std::mutex> lock( remoteMutex );

                  for ( int i = 0; i < m_numSensors; i++ )
                  {
                     PrivateSensor *tempSensor = &m_sensors[ i ];
                     if ( tempSensor->m_isValid && tempSensor->m_type == REMOTE && tempSensor->m_data.m_id == id )
                     {
                        TVMG_DEBUG( "UDP: Assign remote temp ID %d %.1f",id,value );
                        tempSensor->m_data.m_temp = value;
                        numAssigned++;
                     }
                  }
               }

               if ( numAssigned )
               {
                  TVMG_MSG( "Assigned %d remote temperatures",numAssigned );
               }

               cJSON_Delete( root );
            }
         }
      });
   }
}

bool TemperatureModule::getTemperatures()
{
   // Let's see what we have
   bool haveLocalSensors = false;
   bool haveOWSensors = false;
   bool haveShellySensors = false;

   for ( int i = 0; i < m_numSensors; i++ )
   {
      PrivateSensor *sensor = &m_sensors[ i ];

      switch( sensor->m_type )
      {
         case DS18B20:
            haveLocalSensors = true;
            break;
         case OPENWEATHERAPI:
            haveOWSensors = true;
            break;
         case SHELLYADDON:
            haveShellySensors = true;
            break;
         default:
            break;
      }
   }

   if ( haveOWSensors )
   {
      START_TIMING( "OpenWeather Acquisition" );

      Indicator::Scoped guard( m_indicator );

      for ( int i = 0; i < m_numSensors; i++ )
      {
         if ( m_sensors[ i ].m_type == OPENWEATHERAPI )
         {
            const char *name = getSensorName( THERM,m_sensors[ i ].m_data.m_id ).c_str();
            m_sensors[ i ].m_data.m_temp = fetchOpenWeather( m_sensors[ i ].m_openWeather.m_url );

            TVMG_DEBUG( "Raw temperature of %s : %.2f",name,m_sensors[ i ].m_data.m_temp );
         }
      }
      END_TIMING;
      delay( 100 );
   }

   if ( haveShellySensors )
   {
      START_TIMING( "Shelly Add-On Temps" );

      Indicator::Scoped guard( m_indicator );

      for ( int i = 0; i < m_numSensors; i++ )
      {
         if ( m_sensors[ i ].m_type == SHELLYADDON )
         {
            const char *name = getSensorName( THERM,m_sensors[ i ].m_data.m_id ).c_str();
            ShellyAddOnSensor shellySensor = m_sensors[ i ].m_shelly;

            float temp = TEMPERATURE_INVALID;
            if ( ShellyPowerModule::getTemperature( shellySensor.m_shellyParentId, 
                                                      shellySensor.m_shellyId,&temp ) )
            {
               m_sensors[ i ].m_data.m_temp = temp;
               TVMG_DEBUG( "Raw temperature of %s : %.2f",name,m_sensors[ i ].m_data.m_temp );
            }
         }
      }
      END_TIMING;
      delay( 100 );
   }


   if ( !haveLocalSensors )
   {
      return true;
   }

   if ( !m_dallasController )
   {
      TVMG_ERROR( "No Dallas controller for local sensors" );
      return false;
   }

   START_TIMING( "1-Wire Acquisition" );

   Indicator::Scoped guard( m_indicator );

   // Now get the temperatures from the scratch pad used by the Dallas library

   bool dallasAcquired = false;
   for ( int i = 0; i < m_numSensors; i++ )
   {
      if ( m_sensors[ i ].m_isValid && m_sensors[ i ].m_type == DS18B20 )
      {
         // Request temperatures of all devices on the bus just once.  This may block so is not
         // an ideal way to obtain temperatures...

         if ( !dallasAcquired )
         {
            m_dallasController->requestTemperatures();
            dallasAcquired = true;
         }

         const char *name = getSensorName( THERM,m_sensors[ i ].m_data.m_id ).c_str();

         m_sensors[ i ].m_data.m_temp = m_dallasController->getTempC( m_sensors[ i ].m_ds18b20.m_address );

         if ( m_sensors[ i ].m_data.m_temp != DEVICE_DISCONNECTED_C )
         {
            TVMG_DEBUG( "Raw temperature of %s : %.2f",name,m_sensors[ i ].m_data.m_temp );
            m_sensors[ i ].m_data.m_temp += m_sensors[ i ].m_ds18b20.m_calibrationOffset;
         }
         else
         {
            m_sensors[ i ].m_data.m_temp = TEMPERATURE_INVALID;
            TVMG_WARN( "Failed to obtain temperature for %s",name );
         }
      }
   }

   END_TIMING;

   // Now broadcast on the network

   localBroadcastData();

   return true;
}

float_t TemperatureModule::getTemperature( uint8_t tempId )
{
   float_t  temp = TEMPERATURE_INVALID;

   for ( int i = 0; i < m_numSensors; i++ )
   {
      if ( m_sensors[ i ].m_isValid && m_sensors[ i ].m_data.m_id == tempId )
      {
         temp = m_sensors[ i ].m_data.m_temp;
      }
   }

   return( temp );
}


void  TemperatureModule::getAddressString( DeviceAddress addr,char *addrString )
{
   for (int i = 0; i < sizeof( DeviceAddress ); i++ )
   {
      sprintf( &addrString[ i * 2 ],"%02X",addr[ i ] );
   }
   addrString[ sizeof( DeviceAddress ) * 2 ] = 0;
}

void  TemperatureModule::localBroadcastData()
{
   cJSON *root,*array;

   // if send port is 65535, i.e. -1 for 16 bit unsigned, then exit

   if ( m_sendPort == 65535 )
   {
      return;
   }
   else if ( !m_numSensors )
   {
      TVMG_WARN( "No local temp sensors to broadcast" );
      return;
   }
   else if ( ! m_udp )
   {
      TVMG_WARN( "No UDP broadcast" );
      return;
   }

   root = cJSON_CreateObject();
   if ( ! root )
   {
      TVMG_WARN( "No root cJSON object" );
      return;
   }

   START_TIMING( "UDP broadcast temperatures" );

   cJSON_AddStringToObject( root,"name",GET_REGISTRY_STRING( MDNS_NAME ) );
   array = cJSON_AddArrayToObject( root,"sensors" );
   if ( array )
   {
      for ( int i = 0; i < m_numSensors; i++ )
      {
         PrivateSensor *tempSensor = &m_sensors[ i ];
         if ( tempSensor->m_isValid && tempSensor->m_type == DS18B20 )
         {
            cJSON *sensor = cJSON_CreateObject();
            if ( sensor )
            {
               cJSON_AddNumberToObject( sensor,"id", tempSensor->m_data.m_id ) ;
               cJSON_AddNumberToObject( sensor,"value", tempSensor->m_data.m_temp );
               cJSON_AddItemToArray( array,sensor );
            }
         }
      }

      char *str = cJSON_PrintUnformatted( root );
      if ( str )
      {
         // broadcast address, not 255.255.255.255 but IP x.x.x.255
         IPAddress   subNet = WiFi.localIP();
         subNet[ 3 ] = 255;

         // if we broadcast via m_udp->broadcastTo( (uint8_t *) str,strlen(str),sendPort );
         // then that will be a 255.255.255.255 broadcast, so lets limit to the subnet

         (void) m_udp->writeTo( (const uint8_t *) str,strlen(str),subNet,m_sendPort );

         TVMG_DEBUG( "Broadcast: %s",str );

         free( str );
      }
   }

   END_TIMING;

   cJSON_Delete( root );
}

// Sectigo RSA Organization Validation Secure Server CA cert for
// openweather, expires 30/12/2030

const char sectigoCert[] = R"rawliteral(
-----BEGIN CERTIFICATE-----
MIIGTDCCBDSgAwIBAgIQLBo8dulD3d3/GRsxiQrtcTANBgkqhkiG9w0BAQwFADBf
MQswCQYDVQQGEwJHQjEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQD
Ey1TZWN0aWdvIFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBSNDYw
HhcNMjEwMzIyMDAwMDAwWhcNMzYwMzIxMjM1OTU5WjBgMQswCQYDVQQGEwJHQjEY
MBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTcwNQYDVQQDEy5TZWN0aWdvIFB1Ymxp
YyBTZXJ2ZXIgQXV0aGVudGljYXRpb24gQ0EgT1YgUjM2MIIBojANBgkqhkiG9w0B
AQEFAAOCAY8AMIIBigKCAYEApkMtJ3R06jo0fceI0M52B7K+TyMeGcv2BQ5AVc3j
lYt76TvHIu/nNe22W/RJXX9rWUD/2GE6GF5x0V4bsY7K3IeJ8E7+KzG/TGboySfD
u+F52jqQBbY62ofhYjMeiAbLI02+FqwHeM8uIrUtcX8b2RCxF358TB0NHVccAXZc
FYgZndZCeXxjuca7pJJ20LLUnXtgXcjAE1vY4WvbReW0W6mkeZyNGdmpTcFs5Y+s
yy6LtE5Zocji9J9NlNnReox2RWVyEXpA1ChZ4gqN+ZpVSIQ0HBorVFbBKyhdZyEX
gZgNSNtBRwxqwIzJePJhYd4ZUhO1vk+/uP3nwDk0p95q/j7naXNCSvESnrHPypaB
WRK066nKfPRPi9m9kIOhMdYfS8giFRTcdgL24Ycilj7ecAK9Trh0VbjwouJ4WH+x
bt47u68ZFCD/ac55I0DNHkCpaPruj6e9Rmr7K46wZDAYXuEAqB7tGG/jd6JAA+H2
O44CV98NRsU213f1kScIZntNAgMBAAGjggGBMIIBfTAfBgNVHSMEGDAWgBRWc1hk
lfmSGrASKgRieaFAFYghSTAdBgNVHQ4EFgQU42Z0u3BojSxdTg6mSo+bNyKcgpIw
DgYDVR0PAQH/BAQDAgGGMBIGA1UdEwEB/wQIMAYBAf8CAQAwHQYDVR0lBBYwFAYI
KwYBBQUHAwEGCCsGAQUFBwMCMBsGA1UdIAQUMBIwBgYEVR0gADAIBgZngQwBAgIw
VAYDVR0fBE0wSzBJoEegRYZDaHR0cDovL2NybC5zZWN0aWdvLmNvbS9TZWN0aWdv
UHVibGljU2VydmVyQXV0aGVudGljYXRpb25Sb290UjQ2LmNybDCBhAYIKwYBBQUH
AQEEeDB2ME8GCCsGAQUFBzAChkNodHRwOi8vY3J0LnNlY3RpZ28uY29tL1NlY3Rp
Z29QdWJsaWNTZXJ2ZXJBdXRoZW50aWNhdGlvblJvb3RSNDYucDdjMCMGCCsGAQUF
BzABhhdodHRwOi8vb2NzcC5zZWN0aWdvLmNvbTANBgkqhkiG9w0BAQwFAAOCAgEA
BZXWDHWC3cubb/e1I1kzi8lPFiK/ZUoH09ufmVOrc5ObYH/XKkWUexSPqRkwKFKr
7r8OuG+p7VNB8rifX6uopqKAgsvZtZsq7iAFw04To6vNcxeBt1Eush3cQ4b8nbQR
MQLChgEAqwhuXp9P48T4QEBSksYav7+aFjNySsLYlPzNqVM3RNwvBdvp6vgDtGwc
xlKQZVuuNVIaoYyls8swhxDeSHKpRdxRauTLZ+pl+wGvy0pnrLEJGSz9mOEmfbod
e/XopR2NGqaHJ6bIjyxPu6UtyQGI26En7UAEozACrHz06Nx2jTAY9E6NeB6XuobE
wLK025ZRmvglcURG1BrV24tGHHTgxCe8M3oGlpUSMTKQ2dkgljZVYt+gKdFtWELZ
MuRdi+X3XsrR8LFz+aLUiDRfQqhmw3RxjIyVKvvu9UPYY1nsvxYmFnUSeM+2q1z/
iPUry+xDY9MC6+IhleKT094VKdFVp7LXH42+wvU+17lRolQ2mK2N/nBLVBwaIhib
QXw4VYKwB86Bc6eS6iqsc94KEgD/U4VsjmgfhK+Xp4NM+VYzTTa3QeV3p8xOM0cw
q1p8oZFA+OBcz3FYWpDIe5j0NWKlw9hXsTyPY/HeZUV59akskSOSRSmDfe8wJDPX
58uB9/7lud0G3x0pxQAcffP0ayKavNwDTw4UfJ34cEw=
-----END CERTIFICATE----- )rawliteral";

// Some long duration requests to get weather data from openweather HTTP API
// so added timeouts to see if that helps - more testing required

float TemperatureModule::fetchOpenWeather( const String &url )
{
   float temperature = TEMPERATURE_INVALID;
   static uint timeoutMS = 2000;
   static bool openWeatherInsecure = (( GET_REGISTRY_INT( OPENWEATHER_INSECURE ) == 1 ) ? true : false );

   WiFiClientSecure client;

   if ( openWeatherInsecure )
   {
      client.setInsecure();
   }
   else
   {
      client.setCACert( sectigoCert );
      client.setHandshakeTimeout( timeoutMS / 1000 );      // in seconds !
   }

   // We have the network mutex as we're in a sample measurement, so
   // can safely release the webclient used for emoncms

   Networking::releaseWebClient();

   // Create a new client

   HTTPClient http;
   http.begin( client,url );

   http.setConnectTimeout( timeoutMS );  // for the connection
   http.setTimeout( timeoutMS );         // for the HTTP response
   http.setReuse( true );

   int httpResponse = http.GET();
   if ( httpResponse == HTTP_CODE_OK  )
   {
      String resp = http.getString();

      cJSON *root = cJSON_Parse( resp.c_str() );
      if ( root )
      {
         cJSON *main = cJSON_GetObjectItem( root,"main" );
         if ( main )
         {
            temperature = getFloatFromcJSON( main,"temp",TEMPERATURE_INVALID );
         }
         cJSON_Delete( root );
      }
   }
   else
   {
      TVMG_ERROR( "Failed HTTP GET %d",httpResponse );
   }

   http.end();


   if ( temperature != TEMPERATURE_INVALID )
   {
      temperature -= 273.15;
   }
   else
   {
      TVMG_WARN( "Failed to obtain OpenWeather data" );
   }

   return temperature;
}
