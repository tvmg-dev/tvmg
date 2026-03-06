/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#include <ModbusRTU.h>
#include <cJSON.h>

#include "src/config/Config.h"

#include "src/sensors/LGHeatPumpSim.h"

Indicator *LGHeatPumpSimulator::s_indicator = nullptr;

// Need a task for the modbus library to check for read requests, bit
// wasteful in terms of spinning up every 10ms but may be necessary due
// to the frame timing.  Maybe the h/w serial does the heavy lifting ?

void taskModbus( void *parameter )
{
   ModbusRTU *mb = (ModbusRTU *) parameter;
   while (1)
   {
      mb->task();
      vTaskDelay( pdMS_TO_TICKS( 10 ) );
   }
   vTaskDelete(NULL);
}

// bool to indicate that a master has read the last input register so
// we need to read the next sample line

bool needNextSample = false;

// Callback set for the first coil and the last input register read

uint16_t registerReadCallback( TRegister* reg, uint16_t val )
{
   Indicator *s_indicator = LGHeatPumpSimulator::s_indicator;

   if ( reg->address == COIL(0) && s_indicator )
   {
      s_indicator->on();
   }
   else if ( reg->address == IREG(24) )
   {
      needNextSample = true;
      if ( s_indicator )
      {
         s_indicator->off();
      }
   }

   return val;
}

LGHeatPumpSimulator::LGHeatPumpSimulator( HardwareSerial *serial ) :
                     m_serial( serial ),
                     m_slave(),
                     m_available( false ),
                     m_series( 0 ),
                     m_modbusAddress( 0 ),
                     m_recordIndex( 0 ),
                     m_recordSize( 0 ),
                     m_softwareVersion()
{
   TVMG_DEBUG( "LGHeatPumpSimulator::LGHeatPumpSimulator()" );

   cJSON *root = getAllSensorJSON();

   if ( m_serial && root && isSensorRequired( LGHEATPUMPSIM_SENSOR_NAME ) )
   {
      cJSON *sensor;
      cJSON_ArrayForEach( sensor,root )
      {
         if ( strcmpcJSON( sensor,"type",LGHEATPUMPSIM_SENSOR_NAME ) == 0 )
         {
            uint8_t series,writeReg;

            strncpy( m_softwareVersion,getStringFromcJSON( sensor,"software" ).c_str(),MAX_LGSOFTWARE_LENGTH );

            series = getIntFromcJSON( sensor,"series",0 );
            m_modbusAddress = getIntFromcJSON( sensor,"address",0x11 );
            m_recordIndex = getIntFromcJSON( sensor,"start",0 );

            if ( series == 4 )
            {
               m_series = series;
               s_indicator = Indicator::getIndicator( Indicator::HEATPUMP,m_modbusAddress );
            }
            else
            {
               TVMG_WARN( "Unsupported LG series (%d)", series );
            }

            TVMG_DEBUG( "address %u, series %d version %s - start at %d",m_modbusAddress,m_series,m_softwareVersion,m_recordIndex );
            break;
         }
      }
   }
}

LGHeatPumpSimulator::~LGHeatPumpSimulator()
{
   if ( m_slave )
   {
      delete m_slave;
   }
}

// Find the maximum address for each modbus input type

LGHeatPumpSimulator::MaxRegisterAddresses LGHeatPumpSimulator::scanForMaxAddresses( const char *path )
{
   MaxRegisterAddresses mapping;

   File file;

   if ( !tvmgFileSys || !(file = tvmgFileSys.open( path,FILE_READ)) )
   {
      TVMG_ERROR( "Failed to open %s",path );
      m_recordIndex = -1;
      return mapping;
   }

   // First detect the length of the first line - all lines should be padded to
   // the same length.

   m_recordSize = 0;
   while ( file.available() )
   {
      char c = file.read();
      if ( c == '\n' )
      {
         m_recordSize++; // Include the newline character in the count
         break;
      }
      m_recordSize++;
   }

   TVMG_DEBUG( "Modbus file has padded sample length %d",m_recordSize );

   char *buffer = (char*) malloc( m_recordSize + 1 );
   if ( !buffer )
   {
      TVMG_ERROR( "Insufficient memory for max address scan");
      file.close();
      m_recordIndex = -1;
      return mapping;
   }

   file.seek(0);
   int bytesRead = file.readBytes( buffer,m_recordSize );
   buffer[ bytesRead ] = '\0';

   // Terminate at the added padding - we pad each line with ,5,xxxxx

   char* paddingPoint = strstr( buffer,",5,x" );
   if ( paddingPoint )
   {
      *paddingPoint = '\0';
   }

   // each line has N entries of type,address,value - get the largest address
   // located for each type

   char* token = strtok( buffer,"," );
   int col = 0;
   int curType = 0;

   while ( token != NULL )
   {
      int val = atoi( token );
      if (col % 3 == 0)
      {
         curType = val;
         if ( curType == 5 )     // quit, we hit the padding
         {
            break;
         }
      }
      else if (col % 3 == 1)
      {
         switch ( curType )
         {
            case 1:  if ( val >= mapping.coils )
                     {
                        mapping.coils = val + 1;
                     }
                     break;
            case 2:  if ( val >= mapping.discretes )
                     {
                        mapping.discretes = val + 1;
                     }
                     break;
            case 3:  if ( val >= mapping.holding )
                     {
                        mapping.holding = val + 1;
                     }
                     break;
            case 4:  if ( val >= mapping.inputs )
                     {
                        mapping.inputs = val + 1;
                     }
                     break;
            default:
               break;
         }
      }
      token = strtok( NULL,"," );
      col++;
   }

   free( buffer );
   file.close();

   return mapping;
}

void LGHeatPumpSimulator::updateModbusFromFile( const char* path )
{
   File file;

   if ( !tvmgFileSys || !(file = tvmgFileSys.open( path,FILE_READ)) )
   {
      TVMG_ERROR( "Failed to open %s",path );
      m_recordIndex = -1;
      return;
   }

   char* buffer = (char*)malloc( m_recordSize + 1 );
   if ( !buffer )
   {
      TVMG_ERROR( "Insufficient memory for max address scan" );
      m_recordIndex = -1;
      file.close();
      return;
   }

   // Do we need to reset the record index ?
   size_t offset = m_recordIndex * m_recordSize;
   if ( offset >= file.size() )
   {
      TVMG_DEBUG( "Record %d exceeds file size. Resetting to 0", m_recordIndex );
      offset  = 0;
      m_recordIndex = 0;
   }

   TVMG_MSG( "Reading sample %d from %s",m_recordIndex,path );

   // seek to the record & then read the comma separated triplets - type,address,value
   // until we hit the end marker ",5,x"

   if ( file.seek(offset) && file.readBytes( buffer,m_recordSize ) == m_recordSize )
   {
      m_recordIndex++;
      buffer[ m_recordSize ] = '\0';

      char* paddingPoint = strstr( buffer,",5,x" );
      if ( paddingPoint )
      {
         *paddingPoint = '\0';
      }

      char* token = strtok( buffer,"," );
      int col = 0;
      int t = 0, a = 0, v = 0;

      while ( token != NULL )
      {
         int val = atoi(token);
         int mod = col % 3;

         switch( mod )
         {
            case 0: t = val;  break;
            case 1: a = val;  break;
            case 2: v = val;
//                    TVMG_DEBUG( "modbus %d %d %d",t,a,val );
                    switch( t )
                    {
                       case 1: m_slave->Coil(a, v); break;
                       case 2: m_slave->Ists(a, v); break;
                       case 3: m_slave->Hreg(a, v); break;
                       case 4: m_slave->Ireg(a, v); break;
                    }
                    break;
            default: break;
         }

         if ( t == 5 )
         {
            break;
         }
         token = strtok(NULL, ",");
         col++;
      }
   }

   free( buffer );
   file.close();
}

void LGHeatPumpSimulator::initialise()
{
   if ( !m_series )
   {
      TVMG_WARN( "Not initialising, invalid LG series" );
      return;
   }

   if ( !tvmgFileSys )
   {
      TVMG_WARN( "No FS, can't simulate LG" );
      return;
   }

   if ( tvmgFileSys.exists( LGREGISTERS_LOG ) )
   {
      TVMG_MSG( "LG register log opened" );

      MaxRegisterAddresses reg = scanForMaxAddresses( LGREGISTERS_LOG );

      if ( m_recordIndex == -1 )
      {
         return;
      }

      // Now instantiate the modbus slave, adding register maps as identified
      // from the address scan of the 1st line of the register file

      m_slave = new ModbusRTU();
      if ( m_slave )
      {
         TVMG_DEBUG( "Mapping : coils %d : discretes %d : holding %d : input %d",
                                          reg.coils,reg.discretes,reg.holding,reg.inputs );

         m_slave->begin( m_serial,hwConfig->ModBus485EnGPIO );
         m_slave->slave( m_modbusAddress );

         m_slave->addCoil( 0,0,reg.coils );
         m_slave->addIsts( 0,0,reg.discretes );
         m_slave->addHreg( 0,0,reg.holding );
         m_slave->addIreg( 0,0,reg.inputs );

         updateModbusFromFile( LGREGISTERS_LOG );

         if ( m_recordIndex == -1 )
         {
            TVMG_ERROR( "Failed to read modbus register file" );
            delete m_slave;
            m_slave = nullptr;
         }
         else
         {
            // Get the callback when input register 24 (compressor) is read

            m_slave->onGet( IREG(24),registerReadCallback );
            m_slave->onGet( COIL(0),registerReadCallback );

            // Now setup the task - run on core 0, not the main loop core, 3K stack

            xTaskCreatePinnedToCore( taskModbus,"taskModbus",3072,m_slave,0,NULL,0 );

            m_available = true;
         }
      }
   }
}

void LGHeatPumpSimulator::heartbeat()
{
   if ( m_recordIndex != -1 && needNextSample )
   {
      needNextSample = false;
      updateModbusFromFile( LGREGISTERS_LOG );
   }
}

