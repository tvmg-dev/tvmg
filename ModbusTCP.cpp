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

#define  MAX_RETRIES    5

ModbusTCP::ModbusTCP() : ModbusMaster(),
           m_sensor(),
           m_transactionId( 0 )
{
   PW_DEBUG( "ModbusTCP::ModbusTCP()" );

   m_sensor.m_isValid = false;

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
            if ( strcmp( "MODBUSTCP",cJSON_GetObjectItem( sensor,"type" )->valuestring ) == 0 )
            {
               char  tcpServerAddress[ 64 ];

               m_sensor.m_isValid = true;

               strncpy( m_sensor.m_name,cJSON_GetObjectItem( sensor,"name" )->valuestring,MAX_MODBUSTCP_NAME );
               m_sensor.m_id = cJSON_GetObjectItem( sensor,"id" )->valueint;

               if ( ! m_sensor.m_tcpServerAddress.fromString( cJSON_GetObjectItem( sensor,"tcpServerAddress" )->valuestring ) )
               {
                  PW_ERROR( "Failed to convert TCP server IP address" );
                  m_sensor.m_isValid = false;
               }

               m_sensor.m_tcpServerPort = cJSON_GetObjectItem( sensor,"tcpServerPort" )->valueint;
               m_sensor.m_requestDelay = cJSON_GetObjectItem( sensor,"tcpServerDelay" )->valueint;

               PW_MSG( "ModbusTCP : name %s, Server : %s, port %u",m_sensor.m_name,
                                 m_sensor.m_tcpServerAddress.toString().c_str(),m_sensor.m_tcpServerPort );

               break;
            }
         }
      }

      cJSON_Delete( root );
      close( file );

      if ( m_sensor.m_isValid )
      {
         PW_MSG( "Registered ModbusTCP" );
      }
      else
      {
         PW_WARN( "Failed to register ModbusTCP" );
      }
   }
}

ModbusTCP::~ModbusTCP()
{
   PW_DEBUG( "ModbusTCP::~ModbusTCP()" );
}

void ModbusTCP::initialise()
{
   PW_DEBUG( "%s - nothing to do",__FUNCTION__ );
}

bool ModbusTCP::isOk()
{
   return( m_sensor.m_isValid );
}

bool  ModbusTCP::getData( ModBusRequest *request, ModBusResponse *response )
{
   return false;
}


uint8_t  ModbusTCP::readInputRegisters( uint16_t u16ReadAddress,uint8_t u16ReadQty )
{
   ModBusRequest  request;
   ModBusResponse response;
   uint8_t        attempts = 0;

   PW_DEBUG( "readInputRegisters %d %d",u16ReadAddress,u16ReadQty );
   request.transactionType = READ_INPUTS;
   request.slaveAddress = _u8MBSlave;
   request.startRegister = u16ReadAddress;
   request.numRegisters = u16ReadQty;

   request.transactionId = m_transactionId++;

   while ( attempts < MAX_RETRIES && !getData( &request,&response ) )
   {
      delay( m_sensor.m_requestDelay );
      attempts++;
      request.transactionId = m_transactionId++;
   }

   if ( attempts == MAX_RETRIES )
   {
      PW_ERROR( "Modbus failed to read inputs for slave %d",_u8MBSlave );
      return ku8MBResponseTimedOut;
   }
   else
   {
      return ku8MBSuccess;
   }
}
