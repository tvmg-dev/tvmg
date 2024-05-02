#include <WiFi.h>
#include <cJSON.h>

#include <ModbusMaster.h>

#include "LGHeatPump.h"

#include "hwconfig.h"
#include "config.h"

#define LG_MIN_SAMPLING_PERIOD_MS   15000

#define  MB_COIL     0x10000
#define  MB_DISCRETE 0x20000
#define  MB_HOLDING  0x30000
#define  MB_INPUTR   0x40000

#define  HEATING_ENABLED   (MB_COIL | 0x0001)
#define  DHW_ENABLED       (MB_COIL | 0x0002)
#define  SILENT_ENABLED    (MB_COIL | 0x0003)

#define  COMPRESSOR_STATUS (MB_DISCRETE | 0x0004)

#define  TARGET_TEMP       (MB_HOLDING | 0x0003 )

#define  DHW_TEMP          (MB_INPUTR | 0x0006 )

//std::map<int,int reg> m_registerMap{{1,2},{3,5}};

LGHeatPump::LGHeatPump( ModbusMaster *master )
   : m_isValid( false ),
     m_registers( nullptr ),
     m_numRegisters( 0 ),
     m_modbusRTU( master ),
     m_modbusRequests( 0 ),
     m_modbusFailures( 0 ),
     m_millisLastAquisition( -LG_MIN_SAMPLING_PERIOD_MS )
{
   PW_DEBUG( "LGHeatPump::LGHeatPump()" );

   // Parse the /lg.dat file for info

   fs::SPIFFSFS *spiffs = Config::instance()->getSPIFFS();
   File file = spiffs->open( "/lg.dat",FILE_READ );
   if ( !file )
   {
      PW_WARN( "/lg.dat is missing" );
   }
   else
   {
      m_registers = new LGRegister[ MAX_REGISTERS ];

      for ( uint8_t i = 0; i < MAX_REGISTERS; i++ )
      {
         m_registers[ i ].m_type = INVALID;
      }

      PW_MSG( "size of reg %u",MAX_REGISTERS * sizeof( LGRegister ) );

      String data = file.readStringUntil( '@' );

      cJSON *root = cJSON_Parse( data.c_str() );

      if ( !root )
      {
         PW_ERROR( "Failed to parse lg.dat" );
         return;
      }

      if ( strcmp( "THERMAV",cJSON_GetObjectItem( root,"type" )->valuestring ) == 0 )
      {
         cJSON *registers = cJSON_GetObjectItem( root,"registers" );
         if ( registers && cJSON_IsArray( registers ) )
         {
            cJSON *reg;
            cJSON_ArrayForEach( reg,registers )
            {
               if ( m_numRegisters < MAX_REGISTERS  )
               {
                  LGRegister *lgReg = &m_registers[ m_numRegisters++ ];

                  strncpy( lgReg->m_name,cJSON_GetObjectItem( reg,"name" )->valuestring,MAX_HPREG_NAME );
                  lgReg->m_address = cJSON_GetObjectItem( reg,"addr" )->valueint;
                  lgReg->m_type = static_cast<ModbusType> (cJSON_GetObjectItem( reg,"type" )->valueint);
                  lgReg->m_emonFeedId = cJSON_GetObjectItem( reg,"emonFeedId" )->valueint;
                  if ( cJSON_HasObjectItem( reg,"scaling" ) )
                  {
                     lgReg->m_scalingFactor = static_cast<float> (cJSON_GetObjectItem( reg,"scaling" )->valuedouble);
                  }
                  else
                  {
                     lgReg->m_scalingFactor = 1;
                  }

                  // add to the lookup map, key = (type << 16 | modbus-addr + 1)

                  uint32_t parameter = cJSON_GetObjectItem( reg,"type" )->valueint << 16 | lgReg->m_address + 1;

                  m_registerMap[ parameter ] = m_numRegisters - 1;

                  PW_DEBUG( "LG %u %u %s %u %.1f %x %i",
                           lgReg->m_type,lgReg->m_address,lgReg->m_name,
                           lgReg->m_emonFeedId,lgReg->m_scalingFactor,parameter,m_numRegisters - 1 );

               }
               else
               {
                  PW_WARN( "Exceeded max LG registers limit" );
               }
            }
         }
         else
         {
            PW_ERROR( "Registers not located in lg.dat" );
         }
      }

      cJSON_Delete( root );
      close( file );
   }
}

LGHeatPump::~LGHeatPump()
{
   delete [] m_registers;
}

void LGHeatPump::initialise()
{
}

bool  LGHeatPump::isAvailable()
{
}

bool LGHeatPump::readNextSensor( uint8_t index )
{
   if ( index >= m_numRegisters )
   {
      return false;
   }

   if ( millis() - m_millisLastAquisition > LG_MIN_SAMPLING_PERIOD_MS && !index )
   {
      getLGData();
      m_millisLastAquisition = millis();
   }
   return true;
}

bool  LGHeatPump::getContiguousRange( ModbusType type,uint8_t *start,uint8_t *end )
{
   uint16_t i,startAddress,endAddress;

   if ( *start >= m_numRegisters )
   {
      return false;
   }

   // locate first register of given type from the start index
   for ( i = *start; i < m_numRegisters; i++ )
   {
      if  ( m_registers[ i ].m_type == type )
      {
         break;
      }
   }

   // if we located a register, then set the start & end index & modbus address,
   // early return if we didn't locate a register of specified from the type

   if ( i == m_numRegisters )
   {
      return false;
   }
   else
   {
      *start = i;
      startAddress = m_registers[ i ].m_address;
      *end = i;
      endAddress = startAddress;
   }

   i++;
   while ( i < m_numRegisters && m_registers[ i ].m_type == type && m_registers[ i ].m_address == endAddress + 1 )
   {
      *end = i;
      endAddress++;
      i++;
   }

   return true;
}

bool  LGHeatPump::getModbusData( ModbusType type,uint8_t start,uint8_t end )
{
   uint8_t mbusRes = 1;
   uint8_t numRegs = 1 + end - start;
   String  typeStr;

   m_modbusRequests++;

   delay( 50 );
   m_modbusRTU->clearResponseBuffer();

   switch( type )
   {
      case COIL: mbusRes = m_modbusRTU->readCoils( m_registers[ start ].m_address,numRegs );
                 typeStr = "coils";
                 break;
      case DISCRETE: mbusRes = m_modbusRTU->readDiscreteInputs( m_registers[ start ].m_address,numRegs );
                 typeStr = "discretes";
                 break;
      case HOLDING: mbusRes = m_modbusRTU->readHoldingRegisters( m_registers[ start ].m_address,numRegs );
                 typeStr = "holding";
                 break;
      case INPUTR: mbusRes = m_modbusRTU->readInputRegisters( m_registers[ start ].m_address,numRegs );
                 typeStr = "inputs";
                 break;
      default: PW_WARN( "Invalid modbus request type" );
               return false;
   }

   if ( mbusRes != ModbusMaster::ku8MBSuccess )
   {
      PW_ERROR( "Failed to get %u %s from %u [error %u]",numRegs,typeStr.c_str(),m_registers[ start ].m_address,numRegs,mbusRes );
      m_modbusFailures++;
      return false;
   }

   String dbg = typeStr;
   if ( type == COIL || type == DISCRETE )
   {
      for ( int i = 0; i < numRegs; i++ )
      {
         uint8_t  reg = i / 16;
         uint16_t word = m_modbusRTU->getResponseBuffer( reg );
         uint8_t  bit = i % 16;
         bool     state = word & (1 << bit);

         m_registers[ start + i ].m_rawValue = state;
         m_registers[ start + i ].m_value = state;

         if ( state )
         {
            dbg += " ON";
         }
         else
         {
            dbg += " OFF";
         }
      }
   }
   else
   {
      for ( int i = 0; i < numRegs; i++ )
      {
         m_registers[ start + i ].m_rawValue = static_cast<int16_t>(m_modbusRTU->getResponseBuffer( i ));
         m_registers[ start + i ].m_value = m_registers[ start + i ].m_rawValue * m_registers[ start + i ].m_scalingFactor;
         dbg += " ";
         dbg += String( m_registers[ start + i ].m_value );
      }
   }

   PW_HP_MODBUS( dbg.c_str() );

   return true;
}

void  LGHeatPump::getLGData()
{
   PW_MSG( "GetLGData" );

   START_TIMING( "LG Data Aquisition" );
   if ( m_modbusRTU )
   {
      m_modbusRTU->setSlaveId( 32 );

      uint8_t  start,end;

      start = 0;
      while ( getContiguousRange( COIL, &start, &end ) )
      {
         PW_DEBUG( "LG Modbus coils from %u [%u] to %u [%u]",start,m_registers[ start ].m_address,
                                       end,m_registers[ end ].m_address );

         (void) getModbusData( COIL,start,end );

         start = end + 1;
      }

      start = 0;
      while ( getContiguousRange( DISCRETE, &start, &end ) )
      {
         PW_DEBUG( "LG Modbus discretes from %u [%u] to %u [%u]",start,m_registers[ start ].m_address,
                                       end,m_registers[ end ].m_address );

         (void) getModbusData( DISCRETE,start,end );

         start = end + 1;
      }

      start = 0;
      while ( getContiguousRange( HOLDING, &start, &end ) )
      {
         PW_DEBUG( "LG Modbus holding from %u [%u] to %u [%u]",start,m_registers[ start ].m_address,
                                       end,m_registers[ end ].m_address );

         (void) getModbusData( HOLDING,start,end );

         start = end + 1;
      }

      start = 0;
      while ( getContiguousRange( INPUTR, &start, &end ) )
      {
         PW_DEBUG( "LG Modbus inputs from %u [%u] to %u [%u]",start,m_registers[ start ].m_address,
                                       end,m_registers[ end ].m_address );

         (void) getModbusData( INPUTR,start,end );
         start = end + 1;
      }

      for ( int i = 0; i < m_numRegisters; i++ )
      {
         PW_HP_MODBUS( "HP: %s %.1f",m_registers[ i ].m_name,m_registers[ i ].m_value );
      }
   }
   END_TIMING;

   bool  state;
   float_t  value;

   (void) getStatus( COMPRESSOR_STATUS,&state );
   (void) getStatus( SILENT_ENABLED,&state );

   (void) getValue( TARGET_TEMP,&value );
   (void) getValue( DHW_TEMP,&value );
}

bool  LGHeatPump::getStatus( uint32_t parameter,bool *state )
{
   bool  registerOk = false;

   std::map<uint32_t, uint8_t >::const_iterator it = m_registerMap.find( parameter );
   if ( it == m_registerMap.end() )
   {
      PW_ERROR( "No register found for %x",parameter );
   }
   else
   {
      uint8_t  index = it->second;
      *state = m_registers[ index ].m_rawValue;
      registerOk = true;
      PW_HP_MODBUS( "%s:%u",m_registers[ index ].m_name,*state );
   }

   return registerOk;
}

bool  LGHeatPump::getValue( uint32_t parameter,float_t *value )
{
   bool  registerOk = false;

   std::map<uint32_t, uint8_t >::const_iterator it = m_registerMap.find( parameter );
   if ( it == m_registerMap.end() )
   {
      PW_ERROR( "No register found for %x",parameter );
   }
   else
   {
      uint8_t  index = it->second;
      *value = m_registers[ index ].m_value;
      registerOk = true;
      PW_HP_MODBUS( "%s:%.1f",m_registers[ index ].m_name,*value );
   }

   return registerOk;
}

void  LGHeatPump::getModbusStats( uint32_t *requests,uint32_t *failures )
{
   *requests = m_modbusRequests;
   *failures = m_modbusFailures;
}
