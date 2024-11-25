#include <WiFi.h>

#include <cJSON.h>
#include <mutex>

#include "hwconfig.h"
#include "config.h"

#include "ModbusTCP.h"
#include "Networking.h"

#define  MODBUS_TCP_DEFAULT_PORT 500
#define  MODBUS_TC_DEFAULT_DELAY 10

#define  TCP_SERVER_CONNECT_TIMEOUT_MS 2000
#define  MODBUS_TCP_TIMEOUT_MS         1500
#define  KEEP_MODBUS_TCP_ALIVE_MS      40000

#define  READ_COILS     1
#define  READ_DISCRETES 2
#define  READ_HOLDING   3
#define  READ_INPUTS    4

#define  MAX_RETRIES    1

static   WiFiClient     *s_wifiClient = nullptr;

ModbusTCP::ModbusTCP() : ModbusMaster(),
           m_sensor(),
           m_transactionId( 0 )
{
   PW_DEBUG( "ModbusTCP::ModbusTCP()" );

   m_sensor.m_isValid = false;

   // Parse the /sensors.dat file for modbus-tcp

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
            if ( strcmp( MODBUSTCP_SENSOR_NAME,cJSON_GetObjectItem( sensor,"type" )->valuestring ) == 0 )
            {
               char  tcpServerAddress[ 64 ];

               m_sensor.m_isValid = true;

               strncpy( m_sensor.m_name,cJSON_GetObjectItem( sensor,"name" )->valuestring,MAX_MODBUSTCP_NAME );

               m_sensor.m_id = getIntFromcJSON( sensor,"id",1 );

               if ( ! m_sensor.m_tcpServerAddress.fromString( cJSON_GetObjectItem( sensor,"tcpServerAddress" )->valuestring ) )
               {
                  PW_ERROR( "Failed to convert TCP server IP address" );
                  m_sensor.m_isValid = false;
               }

               m_sensor.m_tcpServerPort = getIntFromcJSON( sensor,"tcpServerPort",MODBUS_TCP_DEFAULT_PORT );
               m_sensor.m_requestDelay = getIntFromcJSON( sensor,"tcpServerDelay",MODBUS_TC_DEFAULT_DELAY );

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


bool  ModbusTCP::getData( const ModBusRequest &request )
{
   static uint32_t   lastConnectionMillis = 0;
   uint8_t           buff[ 128 ];
   ModBusResponse    response;

   uint8_t  size = 0;

   // Determine how many bytes we expect to receive back, and we're only
   // currently supporting read requests

   uint8_t  bytesExpected = 9;
   switch( request.transactionType )
   {
      case READ_COILS:
      case READ_DISCRETES:
            bytesExpected += 1 + (request.numRegisters -1) / 8;
            PW_DEBUG( "Coils/Discretes : want %u regs, (%u data bytes)",request.numRegisters,bytesExpected - 9 );
         break;
      case READ_HOLDING:
      case READ_INPUTS:
            bytesExpected += request.numRegisters * 2;
            break;
      default:
            PW_ERROR( "Unsupported transaction type %u",request.transactionType );
            return false;
            break;
   }

   if ( bytesExpected > sizeof( buff ) )
   {
      PW_ERROR( "Invalid request, expected bytes %u",bytesExpected );
      return false;
   }

   uint8_t        attempts = 0;
   bool transactionOk = false;

   while ( attempts < MAX_RETRIES && !transactionOk )
   {
      attempts++;
      m_transactionId++;

      std::lock_guard<std::recursive_mutex> lock(networkingMutex);

      if ( millis() - lastConnectionMillis > KEEP_MODBUS_TCP_ALIVE_MS && s_wifiClient )
      {
         PW_DEBUG( "ModbusTCP: Closing connection (%u ms elapsed)",millis() - lastConnectionMillis );
         s_wifiClient->stop();
         delete s_wifiClient;
         s_wifiClient = nullptr;
      }

      if ( !s_wifiClient )
      {
         PW_DEBUG( "ModbusTCP: Create new WifiClient" );
         s_wifiClient = new WiFiClient();
         if ( !s_wifiClient->connect( m_sensor.m_tcpServerAddress.toString().c_str(),m_sensor.m_tcpServerPort,TCP_SERVER_CONNECT_TIMEOUT_MS ) )
         {
            PW_ERROR( "Failed to connect to ModbusTCP server" );
            delete s_wifiClient;
            s_wifiClient = nullptr;
            continue;
         }
         lastConnectionMillis = millis();
      }

      // flush any previous data from the s_wifiClient, and a short delay

      s_wifiClient->flush();
      delay( m_sensor.m_requestDelay );

      // Create the request payload, we only support the 4 reads of
      // coils, discretes, holding and input

      buff[ size++ ] = highByte( m_transactionId );
      buff[ size++ ] = lowByte( m_transactionId );
      buff[ size++ ] = 0;     // protocol is always zero
      buff[ size++ ] = 0;
      buff[ size++ ] = 0;     // number following bytes always 6
      buff[ size++ ] = 6;
      buff[ size++ ] = request.slaveAddress;
      buff[ size++ ] = request.transactionType;

      buff[ size++ ] = highByte( request.startRegister );
      buff[ size++ ] = lowByte( request.startRegister );
      buff[ size++ ] = highByte( request.numRegisters );
      buff[ size++ ] = lowByte( request.numRegisters );

      PW_MSG( "ModBus request %u, %u registers, type %u",m_transactionId,request.numRegisters,request.transactionType );

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

      uint8_t written = s_wifiClient->write( buff,size );

      if ( written != size )
      {
         PW_ERROR( "Failed to transmit Modbus request" );
         continue;
      }

      // Need to wait a bit for a response, have the TCP traffic then the 9600 baud
      // modbusRTU happening - so wait 25ms at least, up to a timeout limit, need to
      // poll here for now

      uint32_t startMillis = millis();

      while ( millis() - startMillis < MODBUS_TCP_TIMEOUT_MS && s_wifiClient->available() < bytesExpected )
      {
         delay( 25 );
      }

      if ( s_wifiClient->available() != bytesExpected )
      {
         PW_ERROR( "Only received %d bytes,expected %d",s_wifiClient->available(),bytesExpected );
         continue;
      }

      PW_DEBUG( "Took %u ms to acquire %u bytes from ModBusTCP", millis() - startMillis,bytesExpected );

      int numRead = s_wifiClient->read( buff,bytesExpected );

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
         continue;
      }

      // 16 bit values are created from word( high,low ) - check Transaction ID etc.

      response.transactionId = word( buff[ 0 ],buff[ 1 ] );
      response.numBytes = word( buff[ 4 ],buff[ 5 ] );
      response.slaveAddress = buff[ 6 ];
      response.transactionType = buff[ 7 ];

      if ( response.transactionId != m_transactionId )
      {
         PW_ERROR( "Invalid transaction ID, rejecting" );
         continue;
      }

      if ( response.transactionType != request.transactionType )
      {
         PW_ERROR( "Invalid transaction type, rejecting" );
         continue;
      }

      // Now need to populate the ModbusMaster::_u16ResponseBuffer
      // for ModbusMaster::getResponseBuffer() to return the data

      response.dataBytes = buff[ 8 ];

      PW_DEBUG( "trans id 0x%0x, slave addr %u, type %u, bytes %u",response.transactionId,response.slaveAddress,response.transactionType,response.dataBytes );

      if ( response.transactionType == READ_HOLDING || response.transactionType == READ_INPUTS )
      {
         for ( uint8_t i = 0; i < response.dataBytes / 2; i++ )
         {
            _u16ResponseBuffer[ i ] = word( buff[ i * 2 + 9],buff[ i * 2 + 10 ] );
            response.registers[ i ] = _u16ResponseBuffer[ i ];
         }
      }
      else
      {
         for ( uint8_t i = 0; i < request.numRegisters; i++ )
         {
            uint8_t  byteNum = i / 8;
            uint8_t mask = 1 << ( i % 8);

   //         PW_DEBUG( "test register %u, byte 0x%02x, mask 0x%02x", i,buff[ 9 + byteNum ], mask );
            response.registers[ i ] = 0;
            if ( buff[ 9 + byteNum ] & mask )
            {
               response.registers[ i ] = 1;
            }
         }

         // single bits are ordered L,H,L,H in the response
         uint8_t i;
         for ( i = 0; i < response.dataBytes / 2; i++ )
         {
            _u16ResponseBuffer[ i ] = word( buff[ i * 2 + 10],buff[ i * 2 + 9 ] );
         }

         // Handle odd number of bytes
         if ( response.dataBytes % 2 )
         {
            _u16ResponseBuffer[ i ] = word( 0,buff[ i * 2 + 9 ] );
         }
      }

      // some conditional debug output

      START_DEBUG;
      switch( response.transactionType )
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

      for ( int i = 0; i < request.numRegisters; i++ )
      {
         char byteBuff[ 20 ];

         sprintf( byteBuff,"   %u: %u ",i,response.registers[ i ] );
         PW_DEBUG( byteBuff );
      }
      END_DEBUG;

      // can now break out of the loop as we have the data
      transactionOk = true;
      break;
   }

   return( transactionOk );
}

uint8_t  ModbusTCP::getData( uint8_t transactionType,uint16_t u16ReadAddress,uint16_t u16ReadQty )
{
   static uint32_t sent = 0;
   static uint32_t ok = 0;

   ModBusRequest  request;

   PW_MSG( "modbus so far %u sent, %u ok",sent,ok );

   PW_DEBUG( "getData %d %d %d",transactionType,u16ReadAddress,u16ReadQty );

   request.transactionType = transactionType;
   request.slaveAddress = _u8MBSlave;        // from ModbusMaster
   request.startRegister = u16ReadAddress;
   request.numRegisters = u16ReadQty;

   sent++;
   if ( !getData( request ) )
   {
      PW_ERROR( "Modbus failed to read words for slave %d",_u8MBSlave );
      return ku8MBResponseTimedOut;
   }
   else
   {
      ok++;
      return ku8MBSuccess;
   }
}

uint8_t  ModbusTCP::readInputRegisters( uint16_t u16ReadAddress,uint8_t u16ReadQty )
{
   return( getData( READ_INPUTS,u16ReadAddress,u16ReadQty ) );
}

uint8_t  ModbusTCP::readHoldingRegisters( uint16_t u16ReadAddress,uint16_t u16ReadQty )
{
   return( getData( READ_HOLDING,u16ReadAddress,u16ReadQty ) );
}

uint8_t  ModbusTCP::readCoils( uint16_t u16ReadAddress,uint16_t u16ReadQty )
{
   return( getData( READ_COILS,u16ReadAddress,u16ReadQty ) );
}

uint8_t  ModbusTCP::readDiscreteInputs( uint16_t u16ReadAddress,uint16_t u16ReadQty )
{
   return( getData( READ_DISCRETES,u16ReadAddress,u16ReadQty ) );
}
