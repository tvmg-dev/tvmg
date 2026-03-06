/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#include <WiFi.h>

#include <cJSON.h>

#include "src/network/Networking.h"
#include "src/network/ModbusTCP.h"

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

ModbusTCP::ModbusTCP() : ModbusMaster(),
           m_sensor(),
           m_transactionId( 0 ),
           m_wifiClient( nullptr )
{
   TVMG_DEBUG( "ModbusTCP::ModbusTCP()" );

   m_sensor.m_isValid = false;

   cJSON *root = getAllSensorJSON();

   if ( root && isSensorRequired( MODBUSTCP_SENSOR_NAME ) )
   {
      cJSON *sensor;

      cJSON_ArrayForEach( sensor,root )
      {
         if ( strcmpcJSON( sensor,"type",MODBUSTCP_SENSOR_NAME ) == 0 )
         {
            String name;

            m_sensor.m_isValid = true;

            name = getStringFromcJSON( sensor,"name" );

            m_sensor.m_id = getIntFromcJSON( sensor,"id",1 );

            setSensorName( MODBUSTCP,m_sensor.m_id,name );

            if ( ! m_sensor.m_tcpServerAddress.fromString( getStringFromcJSON( sensor,"tcpServerAddress" ) ) )
            {
               TVMG_ERROR( "Failed to convert TCP server IP address" );
               m_sensor.m_isValid = false;
            }


            m_sensor.m_tcpServerPort = getIntFromcJSON( sensor,"tcpServerPort",MODBUS_TCP_DEFAULT_PORT );
            m_sensor.m_requestDelay = getIntFromcJSON( sensor,"tcpServerDelay",MODBUS_TC_DEFAULT_DELAY );

            TVMG_MSG( "ModbusTCP : name %s, Server : %s, port %u",name.c_str(),
                              m_sensor.m_tcpServerAddress.toString().c_str(),m_sensor.m_tcpServerPort );

            break;
         }
      }
   }

   if ( m_sensor.m_isValid )
   {
      TVMG_MSG( "Registered ModbusTCP" );
   }
   else
   {
      TVMG_WARN( "Failed to register ModbusTCP" );
   }
}

ModbusTCP::~ModbusTCP()
{
   TVMG_DEBUG( "ModbusTCP::~ModbusTCP()" );
}

void ModbusTCP::initialise()
{
   TVMG_DEBUG( "%s - nothing to do",__FUNCTION__ );
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
            TVMG_DEBUG( "Coils/Discretes : want %u regs, (%u data bytes)",request.numRegisters,bytesExpected - 9 );
         break;
      case READ_HOLDING:
      case READ_INPUTS:
            bytesExpected += request.numRegisters * 2;
            break;
      default:
            TVMG_ERROR( "Unsupported transaction type %u",request.transactionType );
            return false;
            break;
   }

   if ( bytesExpected > sizeof( buff ) )
   {
      TVMG_ERROR( "Invalid request, expected bytes %u",bytesExpected );
      return false;
   }

   uint8_t        attempts = 0;
   bool transactionOk = false;

   while ( attempts < MAX_RETRIES && !transactionOk )
   {
      attempts++;
      m_transactionId++;

      if ( millis() - lastConnectionMillis > KEEP_MODBUS_TCP_ALIVE_MS && m_wifiClient )
      {
         TVMG_DEBUG( "ModbusTCP: Closing connection (%u ms elapsed)",millis() - lastConnectionMillis );
         m_wifiClient->stop();
         delete m_wifiClient;
         m_wifiClient = nullptr;
      }

      if ( !m_wifiClient )
      {
         TVMG_DEBUG( "ModbusTCP: Create new WifiClient" );
         m_wifiClient = new WiFiClient();
         if ( !m_wifiClient->connect( m_sensor.m_tcpServerAddress.toString().c_str(),m_sensor.m_tcpServerPort,TCP_SERVER_CONNECT_TIMEOUT_MS ) )
         {
            TVMG_ERROR( "Failed to connect to ModbusTCP server" );
            delete m_wifiClient;
            m_wifiClient = nullptr;
            continue;
         }
         lastConnectionMillis = millis();
      }

      // flush (now clear in ESP 3.10) any previous data from the m_wifiClient, and a short delay

      m_wifiClient->flush();
      m_wifiClient->clear();

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

      TVMG_MSG( "ModBus request %u, %u registers, type %u",m_transactionId,request.numRegisters,request.transactionType );

      START_DEBUG;
      String dbg = "Tx ";
      for ( int i = 0; i < size; i++ )
      {
         char byteBuff[ 10 ];

         sprintf( byteBuff,"%02X ",buff[ i ] );
         dbg += byteBuff;
      }
      TVMG_DEBUG( dbg.c_str() );
      END_DEBUG;

      uint8_t written = m_wifiClient->write( buff,size );

      if ( written != size )
      {
         TVMG_ERROR( "Failed to transmit Modbus request" );
         continue;
      }

      // Need to wait a bit for a response, have the TCP traffic then the 9600 baud
      // modbusRTU happening - so wait 25ms at least, up to a timeout limit, need to
      // poll here for now

      uint32_t startMillis = millis();

      while ( millis() - startMillis < MODBUS_TCP_TIMEOUT_MS && m_wifiClient->available() < bytesExpected )
      {
         delay( 25 );
      }

      if ( m_wifiClient->available() != bytesExpected )
      {
         TVMG_ERROR( "Only received %d bytes,expected %d",m_wifiClient->available(),bytesExpected );
         continue;
      }

      TVMG_DEBUG( "Took %u ms to acquire %u bytes from ModBusTCP", millis() - startMillis,bytesExpected );

      int numRead = m_wifiClient->read( buff,bytesExpected );

      START_DEBUG;
      String dbg = "Rx ";
      for ( int i = 0; i < numRead; i++ )
      {
         char byteBuff[ 10 ];

         sprintf( byteBuff,"%02X ",buff[ i ] );
         dbg += byteBuff;
      }
      TVMG_DEBUG( dbg.c_str() );
      END_DEBUG;

      if ( numRead != bytesExpected )
      {
         TVMG_ERROR( "Failed to read response data" );
         continue;
      }

      // 16 bit values are created from word( high,low ) - check Transaction ID etc.

      response.transactionId = word( buff[ 0 ],buff[ 1 ] );
      response.numBytes = word( buff[ 4 ],buff[ 5 ] );
      response.slaveAddress = buff[ 6 ];
      response.transactionType = buff[ 7 ];

      if ( response.transactionId != m_transactionId )
      {
         TVMG_ERROR( "Invalid transaction ID, rejecting" );
         continue;
      }

      if ( response.transactionType != request.transactionType )
      {
         TVMG_ERROR( "Invalid transaction type, rejecting" );
         continue;
      }

      // Now need to populate the ModbusMaster::_u16ResponseBuffer
      // for ModbusMaster::getResponseBuffer() to return the data

      response.dataBytes = buff[ 8 ];

      TVMG_DEBUG( "trans id 0x%0x, slave addr %u, type %u, bytes %u",response.transactionId,response.slaveAddress,response.transactionType,response.dataBytes );

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
               TVMG_DEBUG( "COILS:" );
               break;
         case READ_DISCRETES:
               TVMG_DEBUG( "DISCRETES" );
               break;
         case READ_HOLDING:
               TVMG_DEBUG( "HOLDING" );
               break;
         case READ_INPUTS:
               TVMG_DEBUG( "INPUTS" );
               break;
         default:
               break;
      }

      for ( int i = 0; i < request.numRegisters; i++ )
      {
         char byteBuff[ 20 ];

         sprintf( byteBuff,"   %u: %u ",i,response.registers[ i ] );
         TVMG_DEBUG( byteBuff );
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
   ModBusRequest  request;

   TVMG_DEBUG( "MB: %u sent, %u failed",_u32TotalTransactions,_u32FailedTransactions  );

   TVMG_DEBUG( "MB: getData %d %d %d",transactionType,u16ReadAddress,u16ReadQty );

   request.transactionType = transactionType;
   request.slaveAddress = _u8MBSlave;        // from ModbusMaster
   request.startRegister = u16ReadAddress;
   request.numRegisters = u16ReadQty;

   _u32TotalTransactions++;

   if ( !getData( request ) )
   {
      TVMG_ERROR( "Modbus failed to read words for slave %d",_u8MBSlave );
      _u32FailedTransactions++;
      return ku8MBResponseTimedOut;
   }
   else
   {
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
