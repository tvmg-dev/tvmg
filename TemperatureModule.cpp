#include <cJSON.h>
#include "AsyncUDP.h"

#include "utils.h"
#include "Config.h"
#include "hwconfig.h"
#include "Networking.h"

#include "TemperatureModule.h"

#define TEMPERATURE_PRECISION                11
#define TEMPERATURE_MIN_SAMPLING_PERIOD_MS   15000

extern Networking *networking;

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

TemperatureModule::TemperatureModule()
         : m_oneWireController( nullptr ),
           m_dallasController( nullptr ),
           m_isOk( true ),
           m_sensors(),
           m_numSensors( 0 ),
           m_millisLastAquisition( -TEMPERATURE_MIN_SAMPLING_PERIOD_MS )
{
   PW_DEBUG( "TemperatureModule::TemperatureModule()" );
   PW_MSG( "Temperature Module Startup" );

   for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
   {
      m_sensors[ i ].m_isValid = false;
      m_sensors[ i ].m_busIndex = MAX_TEMP_SENSORS;
      m_sensors[ i ].m_sensor.m_name = nullptr;
      m_sensors[ i ].m_sensor.m_temp = TEMPERATURE_INVALID;
   }

   // Parse the /sensors.dat file for thermometers

   fs::SPIFFSFS *spiffs = Config::instance()->getSPIFFS();
   File file = spiffs->open( "/sensors.dat",FILE_READ );
   if ( !file )
   {
      PW_WARN( "/sensors.dat is missing" );
   }
   else
   {
      String data = file.readStringUntil( '@' );

      cJSON *root = cJSON_Parse( data.c_str() );
      cJSON *sensor;

      if ( cJSON_IsArray( root ) )
      {
         cJSON_ArrayForEach( sensor,root )
         {
            if ( strcmp( "THERM",cJSON_GetObjectItem( sensor,"type" )->valuestring ) == 0 )
            {
               PrivateSensor *tempSensor = &m_sensors[ m_numSensors ];

               strncpy( tempSensor->m_name,cJSON_GetObjectItem( sensor,"name" )->valuestring,MAX_TEMP_NAME );
               strncpy( tempSensor->m_addressStr,cJSON_GetObjectItem( sensor,"address" )->valuestring,sizeof( tempSensor->m_addressStr ) - 1 );
               tempSensor->m_calibrationOffset = static_cast<float_t> (cJSON_GetObjectItem( sensor,"calibration" )->valuedouble );
               tempSensor->m_sensor.m_emonFeedId = cJSON_GetObjectItem( sensor,"emonFeedId" )->valueint;
               tempSensor->m_sensor.m_id = cJSON_GetObjectItem( sensor,"id" )->valueint;
               tempSensor->m_sensor.m_name = tempSensor->m_name;
               tempSensor->m_sensor.m_temp = DEVICE_DISCONNECTED_C;
               tempSensor->m_isValid = true;

               for ( int i = 0; i < 8; i++ )
               {
                  uint8_t  byte;
                  byte = toHex( tempSensor->m_addressStr[ i * 2 ] );
                  byte <<= 4;
                  byte |= toHex( tempSensor->m_addressStr[ (i * 2) + 1 ] );
                  tempSensor->m_address[ i ] = byte;
               }
               char addr[ 32 ];
               getAddressString( tempSensor->m_address,addr );

               PW_DEBUG( "Therm: name %s address %s",tempSensor->m_name,addr );
               PW_DEBUG( "Id %u, feed %u, cal %.2f ",tempSensor->m_sensor.m_id,tempSensor->m_sensor.m_emonFeedId,tempSensor->m_calibrationOffset );
               m_numSensors++;
            }
         }
      }

      cJSON_Delete( root );
      close( file );

      if ( m_numSensors )
      {
         PW_MSG( "Registered %d thermometers",m_numSensors );
      }
      else
      {
         PW_ERROR( "No thermometers registered !" );
      }
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
      PW_DEBUG( "TemperatureModule::initialise()" );
      PW_MSG( "Initialising temperature sensors" );

      m_oneWireController = new OneWire( hwConfig->OneWireGPIO );
      m_dallasController = new DallasTemperature( m_oneWireController );

      // start the DallasTemperature object

      m_dallasController->begin();

      // Confirm all devices located on the bus, and that we have power

      uint8_t devices = m_dallasController->getDeviceCount();

      if ( devices == m_numSensors )
      {
         PW_DEBUG( "%u sensors detected on the OneWire bus ",devices );
      }
      else
      {
         PW_ERROR( "Only located %u of %u sensors.",devices,m_numSensors );

         if ( devices == 0 )
         {
            m_isOk = false;
         }
      }

      if ( m_isOk && m_dallasController->isParasitePowerMode() )
      {
         m_isOk = false;
         PW_ERROR( "DS m_dallasController->operating with no power ?" );
      }

      // Now check for the sensors being located, this is to find the index
      // on the bus.

      if( m_isOk )
      {
         DeviceAddress  locatedAddresses[ devices ];
         char           addrString[ 1 + sizeof( DeviceAddress ) * 3 ];

         /* Find the device address at bus index values */

         for ( int i = 0; i < devices; i++ )
         {
            m_dallasController->getAddress( locatedAddresses[ i ],i );
            getAddressString( locatedAddresses[ i ],addrString );
            PW_DEBUG( "On bus : %s",addrString );
         }

         for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
         {
            if ( m_sensors[ i ].m_isValid )
            {
               PW_DEBUG( "Locating %s",m_sensors[ i ].m_name );
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
                  PW_ERROR( "Failed to locate %s on the bus",m_sensors[ i ].m_name );
                  m_isOk = false;
               }
            }
         }
      }

      // Globally set the resolution to 11 bits per device

      if ( m_isOk )
      {
         PW_DEBUG( "Setting %u bit precision for sensors",TEMPERATURE_PRECISION );

         m_dallasController->setResolution( TEMPERATURE_PRECISION );

         PW_MSG( "Dallas setup completed OK" );
      }
   }
}

TempSensor  *TemperatureModule::readNextSensor( uint8_t index )
{
   if ( index < m_numSensors )
   {
      if ( millis() - m_millisLastAquisition > TEMPERATURE_MIN_SAMPLING_PERIOD_MS )
      {
         getTemperatures();
         m_millisLastAquisition = millis();
      }

      return( &m_sensors[ index ].m_sensor );
   }

   return( nullptr );
}

bool TemperatureModule::getTemperatures()
{
   if ( GET_REGISTRY_INT( FAKE_MEASUREMENTS ) == 1 )
   {
      for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
      {
         if ( m_sensors[ i ].m_isValid )
         {
            if ( m_sensors[ i ].m_sensor.m_temp < (TEMPERATURE_INVALID + 1.0f) )
            {
               m_sensors[ i ].m_sensor.m_temp = i;
            }
            m_sensors[ i ].m_sensor.m_temp += 0.1;
         }
      }

      localBroadcastData();
      return true;
   }

   if ( !m_dallasController )
   {
      return false;
   }

   unsigned long start;
   if ( GET_REGISTRY_INT( DEBUG_LEVEL_ENABLED ) == 1 )
   {
      start = millis();
   }

   // Request temperatures of all devices on the bus.  This may block so is not
   // an ideal way to obtain temperatures...

   m_dallasController->requestTemperatures();

   // Now get the temperatures from the scratch pad used by the Dallas library

   for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
   {
      if ( m_sensors[ i ].m_isValid )
      {
         m_sensors[ i ].m_sensor.m_temp = m_dallasController->getTempCByIndex( m_sensors[ i ].m_busIndex );
         if ( m_sensors[ i ].m_sensor.m_temp != DEVICE_DISCONNECTED_C )
         {
            PW_DEBUG( "Raw temperature of %s : %.2f",m_sensors[ i ].m_name,m_sensors[ i ].m_sensor.m_temp );
            m_sensors[ i ].m_sensor.m_temp += m_sensors[ i ].m_calibrationOffset;
         }
         else
         {
            PW_WARN( "Failed to obtain temperature for %s",m_sensors[ i ].m_name );
         }
      }
   }

   // Using %ul as format specifier fails - can Serial.println to see value too
   // It appears to take ~ 520 ms if only code running

   if ( GET_REGISTRY_INT( DEBUG_LEVEL_ENABLED ) == 1 )
   {
      PW_DEBUG( "Took %u ms to request temperatures", millis() - start );
   }

   localBroadcastData();

   return true;
}

void  TemperatureModule::getAddressString( DeviceAddress addr,char *addrString )
{
   for (int i = 0; i < sizeof( DeviceAddress ); i++ )
   {
      sprintf( &addrString[ i * 3 ],"%02X-",addr[ i ] );
   }
   addrString[ -1 + sizeof( DeviceAddress ) * 3 ] = 0;
}

void  TemperatureModule::localBroadcastData()
{
   cJSON *root,*array;

   if ( !m_numSensors )
   {
      PW_WARN( "No temp sensors to broadcast" );
      return;
   }
   else if ( ! Networking::getUDP() )
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

   cJSON_AddStringToObject( root,"name",GET_REGISTRY_STRING( ACCESS_POINT_NAME ) );
   array = cJSON_AddArrayToObject( root,"sensors" );
   if ( array )
   {
      for ( int i = 0; i < m_numSensors; i++ )
      {
         PrivateSensor *tempSensor = &m_sensors[ i ];
         if ( tempSensor->m_isValid )
         {
            cJSON *sensor = cJSON_CreateObject();
            if ( sensor )
            {
               cJSON_AddNumberToObject( sensor,"id", tempSensor->m_sensor.m_id ) ;
               cJSON_AddNumberToObject( sensor,"value", tempSensor->m_sensor.m_temp );
               cJSON_AddItemToArray( array,sensor );
            }
         }
      }

      char *str = cJSON_PrintUnformatted( root );
      if ( str )
      {
         uint16_t  sendPort = GET_REGISTRY_INT( BROADCAST_UDP_PORT );
         if ( sendPort != -1 )
         {
            (void) Networking::getUDP()->writeTo( (const uint8_t *) str,strlen(str),IPAddress(192,168,1,255),sendPort );
         }

         free( str );
      }
   }

   cJSON_Delete( root );
}

