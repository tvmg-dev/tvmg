#include "utils.h"

#include "config.h"

#include "TemperatureModule.h"

TemperatureModule::TemperatureModule()
         : m_oneWireController( new OneWire( ONE_WIRE_GPIO ) ),
           m_dallasController( new DallasTemperature( m_oneWireController ) ),
           m_isOk( true ),
           m_sensors(),
           m_numSensors( 0 ),
           m_millisLastAquisition( -TEMPERATURE_MIN_SAMPLING_PERIOD_MS )
{
   PW_DEBUG( "TemperatureModule::TemperatureModule()" );
   PW_MSG( "Temperature Module Startup" );

   // start the DallasTemperature object and reset the sensors - until we
   // register them.

   m_dallasController->begin();

   for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
   {
      m_sensors[ i ].m_isValid = false;
      m_sensors[ i ].m_busIndex = MAX_TEMP_SENSORS;
   }
}

TemperatureModule::~TemperatureModule()
{
   PW_DEBUG( "TemperatureModule::~TemperatureModule()" );

   delete m_dallasController;
   delete m_oneWireController;
}

void  TemperatureModule::registerSensor( uint8_t index, DeviceAddress deviceAddress,char *name, float calibrationOffset )

{
   if ( index > MAX_TEMP_SENSORS - 1 )
   {
      PW_WARN( "Not registering sensor - out of range" );
      return;
   }
   else if ( m_sensors[ index ].m_isValid == true )
   {
      PW_WARN( "Not registering sensor - index in use" );
      return;
   }

   for ( int i = 0; i < sizeof( DeviceAddress ); i++ )
   {
      m_sensors[ index ].m_address[ i ] = deviceAddress[ i ];
   }

   strncpy( m_sensors[ index ].m_name,name,MAX_TEMP_NAME );
   m_sensors[ index ].m_temp = DEVICE_DISCONNECTED_C;
   m_sensors[ index ].m_calibrationOffset = calibrationOffset;
   m_sensors[ index ].m_isValid = true;

   m_numSensors++;

   PW_MSG( "Added sensor '%s' at index %u",name,index );

#if DEBUG_ENABLED == 1
   char addrString[ TEMP_ADDR_STRLEN ];

   getAddressString( deviceAddress,addrString );

   PW_DEBUG( "   Address [%s]",addrString );
   PW_DEBUG( "   Calibration offset %.2f",calibrationOffset );
#endif
}

void  TemperatureModule::initialise()
{
   PW_DEBUG( "TemperatureModule::initialise()" );
   PW_MSG( "Initialising temperature sensors" );

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
      char           addrString[ TEMP_ADDR_STRLEN ];

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

   // Globally set the resolution to 9 bit per device

   if ( m_isOk )
   {
      PW_DEBUG( "Setting %u bit precision for sensors",TEMPERATURE_PRECISION );

      m_dallasController->setResolution( TEMPERATURE_PRECISION );

      PW_MSG( "Dallas setup completed OK" );
   }
}

bool TemperatureModule::getTemperature( uint8_t index, float *temp )
{
   if ( index < MAX_TEMP_SENSORS && m_sensors[ index ].m_isValid )
   {
      if ( millis() - m_millisLastAquisition > TEMPERATURE_MIN_SAMPLING_PERIOD_MS )
      {
         getTemperatures();
         m_millisLastAquisition = millis();
      }

      PW_MSG( "%s : %.2f",m_sensors[ index ].m_name,m_sensors[ index ].m_temp );

      if ( m_sensors[ index ].m_temp > DEVICE_DISCONNECTED_C )
      {
         *temp = m_sensors[ index ].m_temp;
         return true;
      }
   }

   *temp = TEMPERATURE_INVALID;
   return false;

}

bool TemperatureModule::getTemperatures( void )
{
   PW_DEBUG( "TemperatureModule::getTemperatures()" );

   // Request temperatures of all devices on the bus.  This may block so is not
   // an ideal way to obtain temperatures...

#if DEBUG_ENABLED == 1
   unsigned long start;
   start = millis();
#endif
   m_dallasController->requestTemperatures();

   // Now get the temperatures from the scratch pad used by the Dallas library

   for ( int i = 0; i < MAX_TEMP_SENSORS; i++ )
   {
      if ( m_sensors[ i ].m_isValid )
      {
         m_sensors[ i ].m_temp = m_dallasController->getTempCByIndex( m_sensors[ i ].m_busIndex );
         if ( m_sensors[ i ].m_temp != DEVICE_DISCONNECTED_C )
         {
            PW_DEBUG( "Raw temperature of %s : %.2f",m_sensors[ i ].m_name,m_sensors[ i ].m_temp );
            m_sensors[ i ].m_temp += m_sensors[ i ].m_calibrationOffset;
         }
         else
         {
            PW_WARN( "Failed to obtain temperature for %s",m_sensors[ i ].m_name );
         }
      }
   }

   // Using %ul as format specifier fails - can Serial.println to see value too
   // It appears to take ~ 520 ms if only code running

#if DEBUG_ENABLED == 1
   PW_DEBUG( "Took %u ms to request temperatures", millis() - start );
#endif

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

