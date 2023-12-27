#include <cJSON.h>

#include "PowerModule.h"

#include "hwconfig.h"
#include "config.h"

void preTransmission()
{
  digitalWrite( hwConfig->ModBus485EnGPIO,1 );
}

void postTransmission()
{
  digitalWrite( hwConfig->ModBus485EnGPIO,0 );
}

PowerModule::PowerModule()
           : m_serial( nullptr ),
             m_master( nullptr ),
             m_sensors(),
             m_numSensors( 0 ),
             m_masterStarted( false )
{
   PW_DEBUG( "PowerModule::PowerModule()" );
   PW_MSG( "Power Module Startup" );

   for ( int i = 0; i < MAX_POWER_SENSORS; i++ )
   {
      m_sensors[ i ].m_isValid = false;
      m_sensors[ i ].m_sensor.m_name = nullptr;
      m_sensors[ i ].m_sensor.m_power = POWER_INVALID;
      m_sensors[ i ].m_sensor.m_energy = ENERGY_INVALID;
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
            if ( strcmp( "POWER",cJSON_GetObjectItem( sensor,"type" )->valuestring ) == 0 )
            {
               PrivateSensor *pwrSensor;

               pwrSensor = &m_sensors[ m_numSensors ];
               strncpy( pwrSensor->m_name,cJSON_GetObjectItem( sensor,"name" )->valuestring,MAX_POWER_NAME );
               pwrSensor->m_address = cJSON_GetObjectItem( sensor,"address" )->valueint;
               pwrSensor->m_sensor.m_emonFeedId = cJSON_GetObjectItem( sensor,"emonFeedId" )->valueint;
               pwrSensor->m_sensor.m_id = cJSON_GetObjectItem( sensor,"id" )->valueint;
               pwrSensor->m_sensor.m_name = pwrSensor->m_name;
               pwrSensor->m_sensor.m_power = POWER_INVALID;
               pwrSensor->m_sensor.m_energy = ENERGY_INVALID;
               pwrSensor->m_isValid = true;

               PW_DEBUG( "Power: name %s address %u",pwrSensor->m_name,pwrSensor->m_address );
               PW_DEBUG( "Id %u,  feed %u",pwrSensor->m_sensor.m_id,pwrSensor->m_sensor.m_emonFeedId );
               m_numSensors++;
            }
         }
      }

      cJSON_Delete( root );
      close( file );

      if ( m_numSensors )
      {
         PW_MSG( "Registered %d power sensors",m_numSensors );
      }
      else
      {
         PW_ERROR( "No power sensors registered !" );
      }
   }
}

PowerModule::~PowerModule()
{
   PW_DEBUG( "PowerModule::~PowerModule()" );

   delete m_master;
   delete m_serial;
}

void PowerModule::initialise( void )
{
   if ( hwConfig->ModBusSerial != -1 )
   {
      PW_DEBUG( "PowerModule::initialise() - h/w" );

      m_serial = new HardwareSerial( hwConfig->ModBusSerial );
      m_master = new ModbusMaster;

      PW_MSG( "Starting MODBUS port %u",hwConfig->ModBusSerial );
      PW_DEBUG( "   Baudrate %u, Rx pin [%u], Tx pin [%u]",hwConfig->ModBusBaudRate,hwConfig->ModBusRxGPIO,hwConfig->ModBusTxGPIO );

      // setup the MAX3485 device, need to set the device enable high for transmit to slaves
      // and low for receive.  The ModbusMaster has callbacks to facilitate that.

      pinMode( hwConfig->ModBus485EnGPIO,OUTPUT );
      m_master->preTransmission( preTransmission );
      m_master->postTransmission( postTransmission );

      m_serial->begin( hwConfig->ModBusBaudRate,hwConfig->ModBusSerialFormat,hwConfig->ModBusRxGPIO,hwConfig->ModBusTxGPIO );
   }
   else
   {
      PW_DEBUG( "PowerModule::initialise() - fake" );
   }
}

PowerSensor  *PowerModule::readNextSensor( uint8_t index )
{
   if ( index < m_numSensors )
   {
      getPower( index );
      return( &m_sensors[ index ].m_sensor );
   }

   return( nullptr );
}

/*
  RegAddr Description                 Resolution
  0x0000  Voltage value               1LSB correspond to 0.1V
  0x0001  Current value low 16 bits   1LSB correspond to 0.001A
  0x0002  Current value high 16 bits
  0x0003  Power value low 16 bits     1LSB correspond to 0.1W
  0x0004  Power value high 16 bits
  0x0005  Energy value low 16 bits    1LSB correspond to 1Wh
  0x0006  Energy value high 16 bits
  0x0007  Frequency value             1LSB correspond to 0.1Hz
  0x0008  Power factor value          1LSB correspond to 0.01
  0x0009  Alarm status  0xFFFF is alarm，0x0000is not alarm
*/

bool PowerModule::getPower( uint8_t index )
{
   uint8_t  modbusResult;

   if ( index < m_numSensors && m_sensors[ index ].m_isValid && hwConfig->ModBusSerial != -1 )
   {
      if ( !m_masterStarted )
      {
         PW_MSG( "Starting MODBUS master" );
         m_master->begin( m_sensors[ index ].m_address, *m_serial );
         m_masterStarted = true;
      }
      // force a short delay
      delay( hwConfig->ModBusMsgDelay );

      m_master->setSlaveId( m_sensors[ index ].m_address );

      // Read the 9 registers of the PZEM-16
      modbusResult = m_master->readInputRegisters( 0x0,9 );

      if ( modbusResult != ModbusMaster::ku8MBSuccess )
      {
         PW_WARN( "Failed to obtain power info for %s",m_sensors[ index ].m_name );
      }
      else
      {
         uint32_t reg32;
         float_t  power, energy;

         float voltage = m_master->getResponseBuffer( 0 ) / 10.0;  //get the 16bit value for the voltage, divide it by 10 and cast in the float variable

         reg32 =  (m_master->getResponseBuffer( 2 ) << 16) + m_master->getResponseBuffer( 1 );  // Get the 2 16bits registers and combine them to an unsigned 32bit
         float current = reg32 / 1000.0;   // Divide the unsigned 32bit by 1000 and put in the current float variable

         reg32 =  (m_master->getResponseBuffer( 4 ) << 16) + m_master->getResponseBuffer( 3 );
         power = reg32 / 10.0;

         reg32 =  (m_master->getResponseBuffer( 6 ) << 16) + m_master->getResponseBuffer( 5 );
         energy = reg32;

         float hz = m_master->getResponseBuffer( 7 ) / 10.0;
         float pf = m_master->getResponseBuffer( 8 ) / 100.00;

         PW_MSG( "%s : %.0f W : %.0f Whr",m_sensors [ index ].m_name,power,energy );

         m_sensors[ index ].m_sensor.m_energy = energy;
         m_sensors[ index ].m_sensor.m_power = power;

         PW_DEBUG( "I [%.1f] : V [%.1f] : Freq [%.1f] : PowerFactor [%.1f]",current, voltage, hz, pf );
         return true;
      }
   }

   return false;
}
