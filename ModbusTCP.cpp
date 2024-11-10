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

uint8_t     buff[ 256 ];

bool  ModbusTCP::getData( ModBusRequest *request, ModBusResponse *response )
{
   uint8_t  size = 0;

   WiFiClient host;
   if ( !host.connect( m_sensor.m_tcpServerAddress.toString().c_str(),m_sensor.m_tcpServerPort,TCP_SERVER_CONNECT_TIMEOUT_MS ) )
   {
      PW_ERROR( "Failed to connect to ModbusTCP server" );
      return false;
   }

   // flush the host from any previous data

   host.flush();

   // Create the request payload, we only support the 4 reads of
   // coils, discretes, holding and input

   buff[ size++ ] = highByte( request->transactionId );
   buff[ size++ ] = lowByte( request->transactionId );
   buff[ size++ ] = 0;     // protocol is always zero
   buff[ size++ ] = 0;
   buff[ size++ ] = 0;     // number following bytes always 6
   buff[ size++ ] = 6;
   buff[ size++ ] = request->slaveAddress;
   buff[ size++ ] = request->transactionType;

   buff[ size++ ] = highByte( request->startRegister );
   buff[ size++ ] = lowByte( request->startRegister );
   buff[ size++ ] = highByte( request->numRegisters );
   buff[ size++ ] = lowByte( request->numRegisters );

   PW_MSG( "ModBus request %u, %u registers, type %u",request->transactionId,request->numRegisters,request->transactionType );

   START_DEBUG;
   String dbg = "Tx ";
   for ( int i = 0; i < size; i++ )
   {
      char byteBuff[ 10 ];

      sprintf( byteBuff,"%02X ",buff[ i ] );
      dbg += byteBuff;
   }
   PW_DEBUG( dbg.c_str() );
   END_DEBUG;

   uint8_t written = host.write( buff,size );

   if ( written != size )
   {
      PW_ERROR( "Failed to transmit Modbus request" );
      return false;
   }

   uint8_t  bytesExpected = 9;

   switch( request->transactionType )
   {
      case READ_COILS:
      case READ_DISCRETES:
            bytesExpected += 1 + request->numRegisters / 8;
            PW_DEBUG( "Coils/Discretes : want %u regs, (%u data bytes)",request->numRegisters,bytesExpected - 9 );
         break;
      case READ_HOLDING:
      case READ_INPUTS:
            bytesExpected += request->numRegisters * 2;
            break;
      default:
            PW_ERROR( "Unsupported transaction type %u",request->transactionType );
            return false;
            break;
   }

   // Need to wait a bit for a response, have the TCP traffic then the 9600 baud
   // modbusRTU happening - so wait 25ms at least, up to a timeout limit

   uint32_t startMillis = millis();

   while ( millis() - startMillis < MODBUS_TCP_TIMEOUT_MS && host.available() < bytesExpected )
   {
      delay( 25 );
   }

   if ( host.available() != bytesExpected )
   {
      PW_ERROR( "Failed to acquire response data" );
      return false;
   }

   PW_DEBUG( "Took %u ms to acquire from ModBusTCP", millis() - startMillis );
   PW_DEBUG( "expecting %u bytes", bytesExpected );

   int numRead = host.read( buff,bytesExpected );

   START_DEBUG;
   String dbg = "Rx ";
   for ( int i = 0; i < numRead; i++ )
   {
      char byteBuff[ 10 ];

      sprintf( byteBuff,"%02X ",buff[ i ] );
      dbg += byteBuff;
   }
   PW_DEBUG( dbg.c_str() );
   END_DEBUG;

   if ( numRead != bytesExpected )
   {
      PW_ERROR( "Failed to read response data" );
      return false;
   }

   // 16 bit values are created from word( high,low )

   response->transactionId = word( buff[ 0 ],buff[ 1 ] );

   if ( response->transactionId != request->transactionId )
   {
      PW_ERROR( "Invalid transaction ID, rejecting" );
      return false;
   }

   response->numBytes = word( buff[ 4 ],buff[ 5 ] );
   response->slaveAddress = buff[ 6 ];
   response->transactionType = buff[ 7 ];

   response->dataBytes = buff[ 8 ];

   if ( response->transactionType == READ_HOLDING || response->transactionType == READ_INPUTS )
   {
      for ( uint8_t i = 0; i < request->numRegisters; i++ )
      {
         response->registers[ i ] = word( buff[ i * 2 + 9],buff[ i * 2 + 10 ] );
      }
   }
   else
   {
      for ( uint8_t i = 0; i < request->numRegisters; i++ )
      {
         uint8_t  byteNum = i / 8;
         uint8_t mask = 1 << ( i % 8);

//         PW_DEBUG( "test register %u, byte 0x%02x, mask 0x%02x", i,buff[ 9 + byteNum ], mask );
         response->registers[ i ] = 0;
         if ( buff[ 9 + byteNum ] & mask )
         {
            response->registers[ i ] = 1;
         }
      }
   }

   PW_DEBUG( "trans id %u, slave addr %u, type %u, bytes %u",response->transactionId,response->slaveAddress,response->transactionType,response->dataBytes );

   START_DEBUG;
   switch( response->transactionType )
   {
      case READ_COILS:
            PW_DEBUG( "COILS:" );
            break;
      case READ_DISCRETES:
            PW_DEBUG( "DISCRETES" );
            break;
      case READ_HOLDING:
            PW_DEBUG( "HOLDING" );
            break;
      case READ_INPUTS:
            PW_DEBUG( "INPUTS" );
            break;
      default:
            break;
   }

   for ( int i = 0; i < request->numRegisters; i++ )
   {
      char byteBuff[ 20 ];

      sprintf( byteBuff,"   %u: %u ",i,response->registers[ i ] );
      PW_DEBUG( byteBuff );
   }
   END_DEBUG;

   return true;
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
