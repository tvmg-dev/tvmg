#include <cJSON.h>
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
      m_sensors[ i ].m_isRemote = false;
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
            setSensorName( THERM,tempSensor->m_data.m_id,name );

            // now we depend on type, openweather, remote or local DS18B20's
            if ( cJSON_GetObjectItem( sensor,"openweather" ) )
            {
               OpenWeatherSensor *openSensor = &m_sensors[ m_numSensors ].m_openWeather;

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
                     tempSensor->m_isDs18b20 = false;
                     m_numSensors++;
                  }
               }
            }
            else if ( cJSON_GetObjectItem( sensor,"remote" ) )
            {
               tempSensor->m_isRemote = true;
               m_haveRemoteSensors = true;

               TVMG_DEBUG( "Remote Therm: name %s",name.c_str() );
               TVMG_DEBUG( "Id %u, feed %u",tempSensor->m_data.m_id,tempSensor->m_data.m_emonFeedId );

               tempSensor->m_isValid = true;
               tempSensor->m_isDs18b20 = true;
               m_numSensors++;
            }
            else
            {
               DS1820BSensor *dsSensor = &m_sensors[ m_numSensors ].m_ds18b20;

               strncpy( dsSensor->m_addressStr,getStringFromcJSON( sensor,"address" ).c_str(),sizeof( dsSensor->m_addressStr ) - 1 );
               dsSensor->m_calibrationOffset = getFloatFromcJSON( sensor,"calibration",0 );

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
               tempSensor->m_isDs18b20 = true;
               m_numSensors++;
            }
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

void  TemperatureModule::initialise()
{
   if ( hwConfig->OneWireGPIO == -1 )
   {
      TVMG_DEBUG( "TemperatureModule::initialise() - fake" );
   }
   else
   {
      TVMG_DEBUG( "TemperatureModule::initialise()" );
      TVMG_MSG( "Initialising temperature sensors" );

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
         if ( m_sensors[ i ].m_isValid && m_sensors[ i ].m_isDs18b20 && !m_sensors[ i ].m_isRemote )
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
         if ( m_sensors[ i ].m_isValid && m_sensors[ i ].m_isDs18b20 && ! m_sensors[ i ].m_isRemote )
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
      if ( !&m_sensors[ index ].m_isRemote )
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
                     if ( tempSensor->m_isValid && tempSensor->m_isRemote && tempSensor->m_data.m_id == id )
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

   for ( int i = 0; i < m_numSensors; i++ )
   {
      PrivateSensor *sensor = &m_sensors[ i ];

      if ( sensor->m_isDs18b20 && !sensor->m_isRemote )
      {
         haveLocalSensors = true;
      }
      else if ( !sensor->m_isDs18b20 )
      {
         haveOWSensors = true;
      }
   }

   if ( haveOWSensors )
   {
      START_TIMING( "OpenWeather Acquisition" );

      Indicator::Scoped guard( m_indicator );

      for ( int i = 0; i < m_numSensors; i++ )
      {
         if ( !m_sensors[ i ].m_isDs18b20 )
         {
            const char *name = getSensorName( THERM,m_sensors[ i ].m_data.m_id ).c_str();
            m_sensors[ i ].m_data.m_temp = fetchOpenWeather( m_sensors[ i ].m_openWeather.m_url );

            TVMG_DEBUG( "Raw temperature of %s : %.2f",name,m_sensors[ i ].m_data.m_temp );
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
      if ( m_sensors[ i ].m_isValid && m_sensors[ i ].m_isDs18b20 && ! m_sensors[ i ].m_isRemote )
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
         if ( tempSensor->m_isValid && tempSensor->m_isDs18b20 && ! tempSensor->m_isRemote )
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
MIIGGTCCBAGgAwIBAgIQE31TnKp8MamkM3AZaIR6jTANBgkqhkiG9w0BAQwFADCB
iDELMAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0pl
cnNleSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNV
BAMTJVVTRVJUcnVzdCBSU0EgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTgx
MTAyMDAwMDAwWhcNMzAxMjMxMjM1OTU5WjCBlTELMAkGA1UEBhMCR0IxGzAZBgNV
BAgTEkdyZWF0ZXIgTWFuY2hlc3RlcjEQMA4GA1UEBxMHU2FsZm9yZDEYMBYGA1UE
ChMPU2VjdGlnbyBMaW1pdGVkMT0wOwYDVQQDEzRTZWN0aWdvIFJTQSBPcmdhbml6
YXRpb24gVmFsaWRhdGlvbiBTZWN1cmUgU2VydmVyIENBMIIBIjANBgkqhkiG9w0B
AQEFAAOCAQ8AMIIBCgKCAQEAnJMCRkVKUkiS/FeN+S3qU76zLNXYqKXsW2kDwB0Q
9lkz3v4HSKjojHpnSvH1jcM3ZtAykffEnQRgxLVK4oOLp64m1F06XvjRFnG7ir1x
on3IzqJgJLBSoDpFUd54k2xiYPHkVpy3O/c8Vdjf1XoxfDV/ElFw4Sy+BKzL+k/h
fGVqwECn2XylY4QZ4ffK76q06Fha2ZnjJt+OErK43DOyNtoUHZZYQkBuCyKFHFEi
rsTIBkVtkuZntxkj5Ng2a4XQf8dS48+wdQHgibSov4o2TqPgbOuEQc6lL0giE5dQ
YkUeCaXMn2xXcEAG2yDoG9bzk4unMp63RBUJ16/9fAEc2wIDAQABo4IBbjCCAWow
HwYDVR0jBBgwFoAUU3m/WqorSs9UgOHYm8Cd8rIDZsswHQYDVR0OBBYEFBfZ1iUn
Z/kxwklD2TA2RIxsqU/rMA4GA1UdDwEB/wQEAwIBhjASBgNVHRMBAf8ECDAGAQH/
AgEAMB0GA1UdJQQWMBQGCCsGAQUFBwMBBggrBgEFBQcDAjAbBgNVHSAEFDASMAYG
BFUdIAAwCAYGZ4EMAQICMFAGA1UdHwRJMEcwRaBDoEGGP2h0dHA6Ly9jcmwudXNl
cnRydXN0LmNvbS9VU0VSVHJ1c3RSU0FDZXJ0aWZpY2F0aW9uQXV0aG9yaXR5LmNy
bDB2BggrBgEFBQcBAQRqMGgwPwYIKwYBBQUHMAKGM2h0dHA6Ly9jcnQudXNlcnRy
dXN0LmNvbS9VU0VSVHJ1c3RSU0FBZGRUcnVzdENBLmNydDAlBggrBgEFBQcwAYYZ
aHR0cDovL29jc3AudXNlcnRydXN0LmNvbTANBgkqhkiG9w0BAQwFAAOCAgEAThNA
lsnD5m5bwOO69Bfhrgkfyb/LDCUW8nNTs3Yat6tIBtbNAHwgRUNFbBZaGxNh10m6
pAKkrOjOzi3JKnSj3N6uq9BoNviRrzwB93fVC8+Xq+uH5xWo+jBaYXEgscBDxLmP
bYox6xU2JPti1Qucj+lmveZhUZeTth2HvbC1bP6mESkGYTQxMD0gJ3NR0N6Fg9N3
OSBGltqnxloWJ4Wyz04PToxcvr44APhL+XJ71PJ616IphdAEutNCLFGIUi7RPSRn
R+xVzBv0yjTqJsHe3cQhifa6ezIejpZehEU4z4CqN2mLYBd0FUiRnG3wTqN3yhsc
SPr5z0noX0+FCuKPkBurcEya67emP7SsXaRfz+bYipaQ908mgWB2XQ8kd5GzKjGf
FlqyXYwcKapInI5v03hAcNt37N3j0VcFcC3mSZiIBYRiBXBWdoY5TtMibx3+bfEO
s2LEPMvAhblhHrrhFYBZlAyuBbuMf1a+HNJav5fyakywxnB2sJCNwQs2uRHY1ihc
6k/+JLcYCpsM0MF8XPtpvcyiTcaQvKZN8rG61ppnW5YCUtCC+cQKXA0o4D/I+pWV
idWkvklsQLI+qGu41SWyxP7x09fn1txDAXYw+zuLXfdKiXyaNb78yvBXAfCNP6CH
MntHWpdLgtJmwsQt6j8k9Kf5qLnjatkYYaA7jBU=
-----END CERTIFICATE----- )rawliteral";


// A debug class to access the connect() method of HTTPClient

class PeteHTTP : public HTTPClient
{
public:
   PeteHTTP();
   ~PeteHTTP();
   bool connect();
};

PeteHTTP::PeteHTTP()
        : HTTPClient()
{
}

PeteHTTP::~PeteHTTP()
{
}

bool PeteHTTP::connect()
{
   return HTTPClient::connect();
}

// Some long duration requests to get weather data from openweather HTTP API
// so added timeouts to see if that helps - more testing required

float TemperatureModule::fetchOpenWeather( const String &url )
{
   float temperature = TEMPERATURE_INVALID;
   static uint timeoutMS = 2000;

   static WiFiClientSecure  *client = nullptr;

   if ( !client )
   {
      TVMG_MSG( "New WiFiSecure for OpenWeather" );
      client = new WiFiClientSecure;

      if ( GET_REGISTRY_INT( OPENWEATHER_INSECURE ) == 1 )
      {
         TVMG_WARN( "Setting OpenWeather WiFi client to insecure mode" );
         client->setInsecure();
      }
      else
      {

         client->setCACert( sectigoCert );
         client->setHandshakeTimeout( timeoutMS / 1000 );      // in seconds !
      }
   }

   if ( client )
   {
      // We have the network mutex as we're in a sample measurement, so
      // can safely release the webclient used for emoncms

      Networking::releaseWebClient();

      // Create a new client

      PeteHTTP http;
      http.begin( *client,url );

      http.setConnectTimeout( timeoutMS );  // for the connection
      http.setTimeout( timeoutMS );         // for the HTTP response
      http.setReuse( true );

#if 0
      START_TIMING( "OW Connecting" );
      bool connected = http.connect();

      END_TIMING;
#endif

      START_TIMING( "OW GET" );

      int httpResponse = http.GET();
      if ( httpResponse > 0 )
      {
         String resp = http.getString();

         resp.replace( ":true",":1" );
         resp.replace( ":false",":0" );

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

      END_TIMING;
      http.end();
   }

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
