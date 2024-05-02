#include <WiFi.h>
#include <cJSON.h>

#include <ModbusMaster.h>

#include "LGHeatPump.h"

#include "hwconfig.h"
#include "config.h"

#define LG_MIN_SAMPLING_PERIOD_MS   15000

#if 0
   modbus type, name, address, value, feed-id, valid
   modbus-type = calc ==> not read

// handle breaks ?
   if type != last type, request data
   || address != last address + 1, request data

   calculated fields
      high press. temp
      low press. temp
      power
      compression ratio

   coil name, register, value
   coil, Heating, 0, 0,


#endif


LGHeatPump::LGHeatPump( ModbusMaster *master )
   : m_isValid( false ),
     m_registers( nullptr ),
     m_numRegisters( 0 ),
     m_modbusRTU( master ),
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

                  PW_DEBUG( "LG %u %u %s %u %.1f",
                           lgReg->m_type,lgReg->m_address,lgReg->m_name,
                           lgReg->m_emonFeedId,lgReg->m_scalingFactor );

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

      PW_MSG( "LG parsed lg.dat ok" );
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

void  LGHeatPump::getLGData()
{
   PW_MSG( "GetLGData" );
   if ( m_modbusRTU )
   {
      m_modbusRTU->setSlaveId( 32 );

      uint8_t  start,end;

      start = 0;
      while ( getContiguousRange( COIL, &start, &end ) )
      {
         PW_DEBUG( "LG Modbus coils from %u [%u] to %u [%u]",start,m_registers[ start ].m_address,
                                       end,m_registers[ end ].m_address );
         start = end + 1;
      }

      start = 0;
      while ( getContiguousRange( DISCRETE, &start, &end ) )
      {
         PW_DEBUG( "LG Modbus discretes from %u [%u] to %u [%u]",start,m_registers[ start ].m_address,
                                       end,m_registers[ end ].m_address );
         start = end + 1;
      }

      start = 0;
      while ( getContiguousRange( HOLDING, &start, &end ) )
      {
         PW_DEBUG( "LG Modbus holding from %u [%u] to %u [%u]",start,m_registers[ start ].m_address,
                                       end,m_registers[ end ].m_address );
         start = end + 1;
      }

      start = 0;
      while ( getContiguousRange( INPUTR, &start, &end ) )
      {
         PW_DEBUG( "LG Modbus inputs from %u [%u] to %u [%u]",start,m_registers[ start ].m_address,
                                       end,m_registers[ end ].m_address );
         start = end + 1;
      }

   }
}
