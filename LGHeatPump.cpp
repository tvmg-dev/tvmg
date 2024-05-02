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

                  PW_MSG( "LG %u %u %s %u %.1f",
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

      PW_MSG( "LG parsed ok" );
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

void  LGHeatPump::getLGData()
{
   PW_MSG( "Fetch LG Data" );
}
