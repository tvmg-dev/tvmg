#include <WiFi.h>

#include <cJSON.h>

#include "HeatPumpModule.h"

#include "hwconfig.h"
#include "config.h"

#define  TCP_SERVER_CONNECT_TIMEOUT_MS 1000
#define  MODBUS_TCP_TIMEOUT_MS         2000

#define  READ_COILS     1
#define  READ_DISCRETES 2
#define  READ_HOLDING   3
#define  READ_INPUTS    4

HeatPumpModule::HeatPumpModule()
           : m_sensor()
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

               strncpy( m_sensor.m_name,cJSON_GetObjectItem( sensor,"name" )->valuestring,MAX_POWER_NAME );
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

HeatPumpModule::~HeatPumpModule()
{
   PW_DEBUG( "HeatPumpModule::~HeatPumpModule()" );
}

void HeatPumpModule::initialise()
{
   PW_DEBUG( "%s - nothing to do",__FUNCTION__ );
}

HeatPumpSensor  *HeatPumpModule::readNextSensor( uint8_t index )
{
   if ( !index && m_sensor.m_isValid )
   {
      sampleHP();
      return( &m_sensor.m_hpSensor);
   }

   return( nullptr );
}

/* From the LG installation manual
 *
 * Coil Registers (0x01) - binary may be read or write (0x5)
 *    01 - Enable/Disable (Heating/Cooling)
 *    02 - Enable/Disabled DHW
 *    03 - Silent mode set
 *    04 - Trigger Disinfectant
 *    05 - Emergency Stop
 *    06 - Triger Emergency Operation
 *
 * Discrete Registers (0x02) - binary read only
 *    01 - Water flow status        (1 = flow too low)
 *    02 - Water pump status        (1 = ON)
 *    03 - Ext. water pump status   (1 = ON)
 *    04 - Compressor status        (1 = ON)
 *    05 - Defrost status           (1 = defrosting)
 *    06 - DHW heating              (1 = ON)
 *    07 - Tank Disinfectant        (1 = disinfecting)
 *    08 - Silent mode statsu       (1 = SILENT)
 *    09 - Cooling status           (1 = cooling)
 *    10 - Solar pump status        (1 = ON)
 *    11 - backup header (step 1)   (1 = ON)
 *    12 - backup header (step 2)   (1 = ON)
 *    13 - DHW Boost status         (1 = ON)
 *    14 - Error status             (1 = IN ERROR)
 *    15 - Emerg. op avail - heating(1 = Available)
 *    16 - Emerg. op avail - DHW    (1 = Available)
 *    17 - Mix pump status          (1 = ON)
 *
 * Holding Registers (0x03) - 16 bit read (write is 0x6)
 *    01 - Mode                           (0: Cool, 4: Heat, 3: Auto)
 *    02 - Control method                 (0: Outlet, 1: inlet, 2: air)
 *    03 - Target temp circuit 1          0.1 deg C x 10
 *    04 - Room Air Temp Circuit 1        0.1 deg C x 10
 *    05 - Shift val(target) in auto C1   1K
 *    06 - Target temp circuit 2          0.1 deg C x 10
 *    07 - Room Air Temp Circuit 2        0.1 deg C x 10
 *    08 - Shift val(target) in auto C2   1K
 *    09 - Energy state input             0 - 8, see document
 *
 * Input Registers (0x04) - 16 bit read only
 *    01 - Error Code                  Error value
 *    02 - Outside operation cycle     0: Off, 1: Cooling, 2:Heating
 *    03 - Water Inlet Temp            0.1 deg C x 10
 *    04 - Water outlet Temp           0.1 deg C x 10
 *    05 - Backup heater outlet Temp   0.1 deg C x 10
 *    06 - DHW Temp                    0.1 deg C x 10
 *    07 - Solar Collector Temp        0.1 deg C x 10
 *    08 - Room Temp                   0.1 deg C x 10
 *    09 - Current Flow Rate           0.1 LPM x 10
 *    10 - Flow Temp (C2)              0.1 deg C x 10
 *    11 - Air Temp (C2)               0.1 deg C x 10
 *    12 - Energy Input State          ...
 *    13 - Outdoor air temp            0.1 deg C x 10
 *  9998 - Product Group               see document
 *  9999 - Product Info                see document (split/mono etc)
 *
 */

bool  HeatPumpModule::getData( WiFiClient *host,ModBusRequest *request, ModBusResponse *response )
{
   uint8_t  buff[ 256 ];
   uint8_t  size = 0;

   // flush the host from any previous data

   host->flush();

   // Create the request payload, we only support the 4 reads of
   // coils, discretes, holding and input

   buff[ size++ ] = highByte( request->transactionId );
   buff[ size++ ] = lowByte( request->transactionId );
   buff[ size++ ] = 0;
   buff[ size++ ] = 0;
   buff[ size++ ] = highByte( request->numBytes );
   buff[ size++ ] = lowByte( request->numBytes );
   buff[ size++ ] = request->slaveAddress;
   if ( !m_sensor.m_simulateNonInputs )
   {
      buff[ size++ ] = request->transactionType;
   }
   else
   {
      buff[ size++ ] = READ_INPUTS;
   }

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

   uint8_t written = host->write( buff,size );

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

   while ( millis() - startMillis < MODBUS_TCP_TIMEOUT_MS && host->available() < bytesExpected )
   {
      delay( 25 );
   }

   if ( !m_sensor.m_simulateNonInputs && host->available() != bytesExpected )
   {
      PW_ERROR( "Failed to acquire response data" );
      return false;
   }

   PW_DEBUG( "Took %u ms to acquire from ModBusTCP", millis() - startMillis );
   PW_DEBUG( "expecting %u bytes", bytesExpected );

   int numRead = host->read( buff,bytesExpected );

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

   if ( !m_sensor.m_simulateNonInputs && numRead != bytesExpected )
   {
      PW_ERROR( "Failed to read response data" );
      return false;
   }

   // 16 bit values are created from word( high,low )

   response->transactionId = word( buff[ 0 ],buff[ 1 ] );

   if ( !m_sensor.m_simulateNonInputs && response->transactionId != request->transactionId )
   {
      PW_ERROR( "Invalid transaction ID, rejecting" );
      return false;
   }

   response->numBytes = word( buff[ 4 ],buff[ 5 ] );
   response->slaveAddress = buff[ 6 ];
   if ( !m_sensor.m_simulateNonInputs )
   {
      response->transactionType = buff[ 7 ];
   }
   else
   {
      response->transactionType = request->transactionType;
   }

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

bool  HeatPumpModule::isAvailable()
{
   return m_sensor.m_isValid;
}

bool  HeatPumpModule::sampleHP()
{
   static uint16_t   transactionId = 1;

   if ( !m_sensor.m_isValid )
   {
      PW_WARN( "No valid HP" );
      return false;
   }

   ModBusRequest  request;
   ModBusResponse response;
   WiFiClient host;

   if ( !host.connect( m_sensor.m_tcpServerAddress.toString().c_str(),m_sensor.m_tcpServerPort,TCP_SERVER_CONNECT_TIMEOUT_MS ) )
   {
      PW_ERROR( "Failed to connect to ModbusTCP server" );
      return false;
   }

   uint8_t  numSuccessRequests = 0,numRequests = 0;

   // common request fields

   request.protocol = 0;
   request.numBytes = 6;
   request.slaveAddress = m_sensor.m_slaveAddress;

   uint16_t target,flow,ret,flowRate,outside,dhwTemp,externalPump,compressor;


   if ( m_sensor.m_numCoils )
   {
      numRequests++;
      request.transactionId = transactionId++;
      request.transactionType = READ_COILS;
      request.startRegister = 0;
      request.numRegisters = m_sensor.m_numCoils;

      if ( getData( &host,&request,&response ) )
      {
         numSuccessRequests++;
      }
      else if ( getData( &host,&request,&response ) )
      {
         PW_WARN( "Second attempt to get coil" );
         numSuccessRequests++;
      }
      else
      {
         PW_ERROR( "Failed to get coil info" );
      }
   }

   if ( m_sensor.m_numDiscretes )
   {
      numRequests++;

      delay( m_sensor.m_requestDelay );

      request.transactionId = transactionId++;
      request.transactionType = READ_DISCRETES;
      request.startRegister = 0;
      request.numRegisters = m_sensor.m_numDiscretes;

      if ( getData( &host,&request,&response ) )
      {
         compressor = response.registers[ 3 ];
         externalPump = response.registers[ 2 ];
         numSuccessRequests++;
      }
   }

   if ( m_sensor.m_numHolding )
   {
      numRequests++;

      delay( m_sensor.m_requestDelay );

      request.transactionId = transactionId++;
      request.transactionType = READ_HOLDING;
      request.startRegister = 0;
      request.numRegisters = m_sensor.m_numHolding;

      if ( getData( &host,&request,&response ) )
      {
         numSuccessRequests++;
         target = response.registers[ 2 ];
      }

   }

   if ( m_sensor.m_numInput )
   {
      numRequests++;

      delay( m_sensor.m_requestDelay );

      request.transactionId = transactionId++;
      request.transactionType = READ_INPUTS;
      request.startRegister = 0;
      request.numRegisters = m_sensor.m_numInput;

      if ( getData( &host,&request,&response ) )
      {
         ret = response.registers[ 2 ];
         flow = response.registers[ 3 ];
         dhwTemp = response.registers[ 5 ];
         flowRate = response.registers[ 8 ];
         outside = response.registers[ 12 ];
         numSuccessRequests++;
      }
   }

   PW_DEBUG( "MEASURE: %u %u %u %u %u %u %u %u",compressor,externalPump,flowRate,ret,flow,outside,target,dhwTemp );
   return ( numSuccessRequests == numRequests );
}

