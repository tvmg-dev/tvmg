#include <cJSON.h>
#include <WiFi.h>
#include "AsyncUDP.h"
#include <mutex>

#include "src/core/utils.h"
#include "src/core/Measurement.h"

#include "src/config/Config.h"
#include "src/config/hwconfig.h"
#include "src/network/Networking.h"

#include "TemperatureModule.h"

#define TEMPERATURE_PRECISION                11
#define TEMPERATURE_MIN_SAMPLING_PERIOD_MS   15000

extern Networking *networking;

static std::mutex copyMutex;

char  s_udpPacket[ 1024 ];

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
   PW_ERROR( "Invalid char %c",a );
   return 17;
}

TemperatureModule::PrivateSensor::PrivateSensor()
             : m_public(),
               m_address(),
               m_busIndex(),
               m_calibrationOffset( 0.0 ),
               m_addressStr()
{
   PW_DEBUG( "Temp PrivateSensor()" );
   for ( int i = 0; i < 8; i++ )
   {
      m_address[ i ] = 0;
   }
   m_addressStr[ 0 ] = '\0';
}

static uint32_t psc=0;

TemperatureModule::PrivateSensor::PrivateSensor( const TemperatureModule::PrivateSensor &other )
             : m_public( other.m_public ),
               m_address(),
               m_busIndex( other.m_busIndex ),
               m_calibrationOffset( other.m_calibrationOffset )
{
   PW_DEBUG( "Temp PrivateSensor::(copy) %u",psc++ );
   for ( int i = 0; i < 8; i++ )
   {
      m_address[ i ] = other.m_address[ i ];
   }

   strncpy( m_addressStr,other.m_addressStr,sizeof(m_addressStr) );
}

TemperatureModule::PrivateSensor & TemperatureModule::PrivateSensor::operator=(const TemperatureModule::PrivateSensor &other )
{
   if ( this != &other )
   {
      PW_DEBUG( "Temp PrivateSensor::operator=" );
      m_public = other.m_public;
      m_busIndex = other.m_busIndex;
      m_calibrationOffset = other.m_calibrationOffset;

      for ( int i = 0; i < 8; i++ )
      {
         m_address[ i ] = other.m_address[ i ];
      }

      strncpy( m_addressStr,other.m_addressStr,sizeof(m_addressStr) );
   }
}

TemperatureModule::TemperatureModule()
         : m_oneWireController( nullptr ),
           m_dallasController( nullptr ),
           m_udp( nullptr ),
           m_isOk( true ),
           m_sensors(),
           m_numLocalSensors( 0 ),
           m_numRemoteSensors( 0 ),
           m_sendPort( -1 ),
           m_millisLastAquisition( -TEMPERATURE_MIN_SAMPLING_PERIOD_MS ),
           m_fakeMeasurements( false )
{
   PW_MSG( "Temperature Module Startup" );

   cJSON *root = getAllSensorJSON();

   if ( root && isSensorRequired( TEMPERATURE_SENSOR_NAME ) )
   {
      int   sensorNum = 1;
      cJSON *sensor;

      cJSON_ArrayForEach( sensor,root )
      {
         if ( strcmpcJSON( sensor,"type",TEMPERATURE_SENSOR_NAME ) == 0 )
         {
            PrivateSensor newSensor;

            String name = getStringFromcJSON( sensor,"name" );

            TempSensor publicSensor;

            publicSensor.m_id = getIntFromcJSON( sensor,"id",sensorNum );
            publicSensor.m_emonFeedId = getIntFromcJSON( sensor,"emonFeedId",0 );
            publicSensor.m_temp = TEMPERATURE_INVALID;
            setSensorName( THERM,publicSensor.m_id,name );   // add name to the sensor map

            if ( cJSON_GetObjectItem( sensor,"remote" ) )
            {
               publicSensor.m_isRemote = true;
               m_numRemoteSensors++;

               PW_DEBUG( "Remote Therm: name %s",name.c_str() );
               PW_DEBUG( "Id %u, feed %u",publicSensor.m_id,publicSensor.m_emonFeedId );
            }
            else
            {
               strncpy( newSensor.m_addressStr,getStringFromcJSON( sensor,"address" ).c_str(),sizeof( newSensor.m_addressStr ) - 1 );
               newSensor.m_calibrationOffset = getFloatFromcJSON( sensor,"calibration",0 );
               publicSensor.m_isRemote = false;

               for ( int i = 0; i < 8; i++ )
               {
                  uint8_t  byte;
                  byte = toHex( newSensor.m_addressStr[ i * 2 ] );
                  byte <<= 4;
                  byte |= toHex( newSensor.m_addressStr[ (i * 2) + 1 ] );
                  newSensor.m_address[ i ] = byte;
               }
               char addr[ 32 ];
               getAddressString( newSensor.m_address,addr );

               m_numLocalSensors++;

               PW_DEBUG( "Local Therm: name %s address %s",name.c_str(),addr );
               PW_DEBUG( "Id %u, feed %u, cal %.2f ",publicSensor.m_id,publicSensor.m_emonFeedId,newSensor.m_calibrationOffset );
            }

            newSensor.m_public = publicSensor;
            m_sensors.push_back( newSensor );
         }
      }

      PW_MSG( "Registered %d local thermometers",m_numLocalSensors );
      PW_MSG( "Registered %d remote thermometers",m_numRemoteSensors );
   }

   if ( GET_REGISTRY_INT( FAKE_MEASUREMENTS ) == 1 )
   {
      m_fakeMeasurements = true;
   }

}

TemperatureModule::~TemperatureModule()
{
   PW_DEBUG( "TemperatureModule::~TemperatureModule()" );

   delete m_dallasController;
   delete m_oneWireController;
}

void  TemperatureModule::initialise()
{
   if ( hwConfig->OneWireGPIO == -1 )
   {
      PW_DEBUG( "TemperatureModule::initialise() - fake" );
   }
   else
   {
      PW_MSG( "Initialising temperature sensors" );

      m_oneWireController = new OneWire( hwConfig->OneWireGPIO );
      m_dallasController = new DallasTemperature( m_oneWireController );

      DeviceAddress  locatedAddresses[ MAX_TEMP_SENSORS ];

      // start the DallasTemperature object

      m_dallasController->begin();

      // Confirm all devices located on the bus, and that we have power

      uint8_t devices = m_dallasController->getDeviceCount();

      if ( devices == m_numLocalSensors )
      {
         PW_DEBUG( "%u sensors detected on the OneWire bus ",devices );
      }
      else
      {
         PW_ERROR( "Located %u of %u sensors.",devices,m_numLocalSensors );

         if ( devices == 0 )
         {
            m_isOk = false;
         }
      }

      if ( m_isOk && m_dallasController->isParasitePowerMode() )
      {
         m_isOk = false;
         PW_ERROR( "Dallas Controller operating with no power ?" );
      }

      char addrString[ 1 + sizeof( DeviceAddress ) * 2 ];
      for ( int i = 0; i < devices; i++ )
      {
         m_dallasController->getAddress( locatedAddresses[ i ],i );
         getAddressString( locatedAddresses[ i ],addrString );
         PW_MSG( "DS18B20 : %s",addrString );
      }

      // Now check for the sensors being located, this is to find the index
      // on the bus.

      if( m_isOk )
      {
         // Find the device address at bus index values

         for ( int i = 0; i < m_sensors.size(); i++ )
         {
            if ( ! m_sensors[ i ].m_public.m_isRemote )
            {
               const char *name = getSensorName( THERM,m_sensors[ i ].m_public.m_id ).c_str();
               PW_DEBUG( "Locating %s",name );
               for ( int j = 0; j < devices; j++ )
               {
                  if ( !memcmp( locatedAddresses[ j ],m_sensors[ i ].m_address,sizeof( DeviceAddress ) ) )
                  {
                     PW_DEBUG( "...at bus index %u",j );
                     m_sensors[ i ].m_busIndex = j;
                     break;
                  }
               }

               if ( m_sensors[ i ].m_busIndex == MAX_TEMP_SENSORS )
               {
                  PW_ERROR( "Failed to locate %s on the bus",name );
                  m_isOk = false;
               }
            }
         }
      }

      // Globally set the resolution to 11 bits per device

      if ( m_isOk && m_numLocalSensors > 0 )
      {
         PW_DEBUG( "Setting %u bit precision for sensors",TEMPERATURE_PRECISION );

         m_dallasController->setResolution( TEMPERATURE_PRECISION );

         PW_MSG( "Dallas setup completed OK" );
      }
   }

   // Get UDP for broadcast rx/tx

   m_udp = Networking::getUDP();

   // are we broadcasting data ?
   m_sendPort = GET_REGISTRY_INT( BROADCAST_UDP_PORT );

   // are we listening ?
   if ( m_numRemoteSensors && m_udp )
   {
      addUDPListener();
   }
}

void TemperatureModule::sample()
{
   if ( m_numLocalSensors && millis() - m_millisLastAquisition > TEMPERATURE_MIN_SAMPLING_PERIOD_MS )
   {
      START_TIMING( "Temperature Sample" );

      getTemperatures();
      m_millisLastAquisition = millis();

      // Now copy the public sensor data, take mutex to avoid invalidating
      // data if readNextSensor occurs

      std::lock_guard<std::mutex> lock( copyMutex );

      for ( int i = 0; i < m_sensors.size(); i++ )
      {
         if ( m_samples.size() < i + 1 )
         {
            m_samples.push_back( m_sensors[ i ].m_public );
         }
         else
         {
            m_samples[ i ] = m_sensors[ i ].m_public;
         }
      }

      END_TIMING;
   }
}

TempSensor *TemperatureModule::readNextSensor( uint8_t index )
{
   // Get the sample data, taking mutex to avoid race

   std::lock_guard<std::mutex> lock( copyMutex );
   if ( index < m_samples.size() )
   {
      return( &m_samples[ index ] );
   }

   return( nullptr );
}

void TemperatureModule::addUDPListener()
{
   uint16_t  listenPort = GET_REGISTRY_INT( LISTEN_UDP_PORT );
   static int k_waitMuxexMs = 2000;  // wait up to 2s to get the sample mutex

   PW_MSG( "Adding UDP listener %d",listenPort );
   if( listenPort != -1 && m_udp && m_udp->listen( listenPort ) ) {
      m_udp->onPacket([ & ](AsyncUDPPacket packet) {
         if ( packet.length() < sizeof( s_udpPacket ) - 1 )
         {
            strncpy( s_udpPacket,reinterpret_cast<const char *>(packet.data()),packet.length() );
            s_udpPacket[ packet.length() ] = 0;

            cJSON *root = cJSON_Parse( s_udpPacket );
            if ( root )
            {
               cJSON *sensors = cJSON_GetObjectItem( root,"sensors" );
               cJSON *sensor;
               int   sensorNum = 1;

               cJSON_ArrayForEach( sensor,sensors )
               {
                  uint8_t  id = getIntFromcJSON( sensor,"id",sensorNum++ );
                  float_t  value = getFloatFromcJSON( sensor,"value",TEMPERATURE_INVALID );

                  for ( int i = 0; i < m_sensors.size(); i++ )
                  {
                     const PrivateSensor &sensor = m_sensors[ i ];
                     if ( sensor.m_public.m_isRemote && sensor.m_public.m_id == id )
                     {
                        PW_MSG( "UDP: Assign remote temp ID %d %.1f",id,value );
                        if ( Measurement::takeSampleMutex( k_waitMuxexMs ) == 1 )
                        {
                           m_sensors[ i ].m_public.m_temp = value;
                           Measurement::releaseSampleMutex();
                        }
                        else
                        {
                           PW_ERROR( "UDP: Failed to get sample mutex in %d ms",k_waitMuxexMs );
                        }
                     }
                  }
               }

               cJSON_Delete( root );
            }
         }
      });
   }
}

bool TemperatureModule::getTemperatures()
{
   // If faking, then incremenent local temperatures and also broadcast
   if ( m_fakeMeasurements )
   {
      for ( int i = 0; i < m_sensors.size(); i++ )
      {
         if ( !m_sensors[ i ].m_public.m_isRemote )
         {
            if ( m_sensors[ i ].m_public.m_temp < (TEMPERATURE_INVALID + 1.0f) )
            {
               m_sensors[ i ].m_public.m_temp = i;
            }
            m_sensors[ i ].m_public.m_temp += 0.1;
         }
      }

      localBroadcastData();
      return true;
   }

   if ( !m_dallasController || ! m_numLocalSensors )
   {
      return false;
   }

   // Request temperatures of all devices on the bus.  This may block so is not
   // an ideal way to obtain temperatures...

   m_dallasController->requestTemperatures();

   // Now get the temperatures from the scratch pad used by the Dallas library

   for ( int i = 0; i < m_sensors.size(); i++ )
   {
      m_sensors[ i ].m_public.m_temp = TEMPERATURE_INVALID;    // initially invalidate the temperature

      if ( ! m_sensors[ i ].m_public.m_isRemote )
      {
         const char *name = getSensorName( THERM,m_sensors[ i ].m_public.m_id ).c_str();

         m_sensors[ i ].m_public.m_temp = m_dallasController->getTempC( m_sensors[ i ].m_address );

         if ( m_sensors[ i ].m_public.m_temp != DEVICE_DISCONNECTED_C )
         {
            PW_DEBUG( "Raw temperature of %s : %.2f",name,m_sensors[ i ].m_public.m_temp );
            m_sensors[ i ].m_public.m_temp += m_sensors[ i ].m_calibrationOffset;
         }
         else
         {
            PW_WARN( "Failed to obtain temperature for %s",name );
         }
      }
   }

   // Now broadcast on the network

   localBroadcastData();

   return true;
}

float_t TemperatureModule::getTemperature( uint8_t tempId )
{
   float_t  temp = TEMPERATURE_INVALID;

   for ( int i = 0; i < m_sensors.size(); i++ )
   {
      if ( m_sensors[ i ].m_public.m_id == tempId )
      {
         temp = m_sensors[ i ].m_public.m_temp;
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
   else if ( !m_numLocalSensors )
   {
      PW_WARN( "No local temp sensors to broadcast" );
      return;
   }
   else if ( ! m_udp )
   {
      PW_WARN( "No UDP broadcast" );
      return;
   }

   root = cJSON_CreateObject();
   if ( ! root )
   {
      PW_WARN( "No root cJSON object" );
      return;
   }

   START_TIMING( "UDP broadcast temperatures" );

   cJSON_AddStringToObject( root,"name",GET_REGISTRY_STRING( MDNS_NAME ) );
   array = cJSON_AddArrayToObject( root,"sensors" );
   if ( array )
   {
      for ( int i = 0; i < m_sensors.size(); i++ )
      {
         if ( ! m_sensors[ i ].m_public.m_isRemote )
         {
            cJSON *sensor = cJSON_CreateObject();
            if ( sensor )
            {
               cJSON_AddNumberToObject( sensor,"id", m_sensors[ i ].m_public.m_id ) ;
               cJSON_AddNumberToObject( sensor,"value", m_sensors[ i ].m_public.m_temp );
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

         PW_DEBUG( "Broadcast: %s",str );

         free( str );
      }
   }

   END_TIMING;

   cJSON_Delete( root );
}
