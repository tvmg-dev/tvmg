#include <WiFi.h>

#include <cJSON.h>

#include "hwconfig.h"
#include "config.h"

#include "ModbusTCP.h"

#define  TCP_SERVER_CONNECT_TIMEOUT_MS 1000
#define  MODBUS_TCP_TIMEOUT_MS         2000

#define  READ_COILS     1
#define  READ_DISCRETES 2
#define  READ_HOLDING   3
#define  READ_INPUTS    4

ModbusTCP::ModbusTCP() : ModbusMaster(),
           m_sensor()
{
   PW_DEBUG( "HeatPumpModule::HeatPump()" );

   m_sensor.m_isValid = false;
   m_sensor.m_hpSensor.m_name = nullptr;

   // Parse the /sensors.dat file for heat pump

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
            if ( strcmp( "HEATPUMP",cJSON_GetObjectItem( sensor,"type" )->valuestring ) == 0 )
            {
               char           tcpServerAddress[ 17 ];

               m_sensor.m_isValid = true;

               strncpy( m_sensor.m_name,cJSON_GetObjectItem( sensor,"name" )->valuestring,MAX_MODBUSTCP_NAME );
               m_sensor.m_hpSensor.m_id = cJSON_GetObjectItem( sensor,"id" )->valueint;
               m_sensor.m_slaveAddress = cJSON_GetObjectItem( sensor,"slaveAddress" )->valueint;
               if ( ! m_sensor.m_tcpServerAddress.fromString( cJSON_GetObjectItem( sensor,"tcpServerAddress" )->valuestring ) )
               {
                  PW_ERROR( "Failed to convert TCP server IP address" );
                  m_sensor.m_isValid = false;
               }

               m_sensor.m_tcpServerPort = cJSON_GetObjectItem( sensor,"tcpServerPort" )->valueint;

               m_sensor.m_requestDelay = cJSON_GetObjectItem( sensor,"tcpServerDelay" )->valueint;

               m_sensor.m_simulateNonInputs = cJSON_GetObjectItem( sensor,"simulateNonInputs" )->valueint;
               m_sensor.m_numCoils = cJSON_GetObjectItem( sensor,"coils" )->valueint;
               m_sensor.m_numDiscretes = cJSON_GetObjectItem( sensor,"discretes" )->valueint;
               m_sensor.m_numHolding = cJSON_GetObjectItem( sensor,"holding" )->valueint;
               m_sensor.m_numInput = cJSON_GetObjectItem( sensor,"input" )->valueint;

               if ( m_sensor.m_numCoils > MAX_REGISTERS )
               {
                  PW_ERROR( "Too many coils" );
                  m_sensor.m_isValid = false;
               }
               if ( m_sensor.m_numDiscretes > MAX_REGISTERS )
               {
                  PW_ERROR( "Too many discretes" );
                  m_sensor.m_isValid = false;
               }
               if ( m_sensor.m_numHolding > MAX_REGISTERS )
               {
                  PW_ERROR( "Too many holding" );
                  m_sensor.m_isValid = false;
               }
               if ( m_sensor.m_numInput > MAX_REGISTERS )
               {
                  PW_ERROR( "Too many inputs" );
                  m_sensor.m_isValid = false;
               }

               m_sensor.m_hpSensor.m_emonFeedId = cJSON_GetObjectItem( sensor,"emonFeedId" )->valueint;

               m_sensor.m_hpSensor.m_name = m_sensor.m_name;

               PW_MSG( "Heat Pump : name %s, modbus slave address 0x%02x",m_sensor.m_name,m_sensor.m_slaveAddress );
               PW_DEBUG( "Coils %u, Discretes %u, Holding %u, Input %u",m_sensor.m_numCoils,m_sensor.m_numDiscretes,
                                                         m_sensor.m_numHolding,m_sensor.m_numInput );
               PW_DEBUG( "TCP Server : %s, port %u",m_sensor.m_tcpServerAddress.toString().c_str(),m_sensor.m_tcpServerPort );
               PW_DEBUG( "Id %u,  feed %u",m_sensor.m_hpSensor.m_id,m_sensor.m_hpSensor.m_emonFeedId );

               break;
            }
         }
      }

      cJSON_Delete( root );
      close( file );

      if ( m_sensor.m_isValid )
      {
         PW_MSG( "Registered HP sensor" );
      }
      else
      {
         PW_WARN( "Failed to register HP sensor" );
      }
   }
}

ModbusTCP::~ModbusTCP()
{
   PW_DEBUG( "HeatPumpModule::~HeatPumpModule()" );
}

void ModbusTCP::initialise()
{
   PW_DEBUG( "%s - nothing to do",__FUNCTION__ );
}
