#include <SD.h>

#include <WiFi.h>
#include <cJSON.h>

#include <ModbusMaster.h>

#include "LGHeatPump.h"

#include "src/config/hwconfig.h"
#include "src/config/Config.h"
#include "src/userio/UserIO.h"

// Some statics for quick bodge on register sampling

static ModbusMaster *s_master = nullptr;
static uint16_t     s_modbusAddress = 32;

#define LG_MIN_SAMPLING_PERIOD_MS   15000

// R32 refrigerant - pressure to temperature lookup, interpolate
// pressures read from LG to temperature equivalents

static std::map<float_t,float_t> r32Lookup = {
   {172,-30},
   {195,-28},
   {220,-26},
   {247,-24},
   {275,-22},
   {304,-20},
   {336,-18},
   {369,-16},
   {405,-14},
   {442,-12},
   {481,-10},
   {523,-8},
   {567,-6},
   {613,-4},
   {661,-2},
   {712,0},
   {765,2},
   {821,4},
   {880,6},
   {941,8},
   {1006,10},
   {1073,12},
   {1143,14},
   {1217,16},
   {1293,18},
   {1373,20},
   {1457,22},
   {1544,24},
   {1634,26},
   {1728,28},
   {1826,30},
   {1928,32},
   {2034,34},
   {2144,36},
   {2258,38},
   {2377,40},
   {2500,42},
   {2628,44},
   {2760,46},
   {2898,48},
   {3040,50},
   {3187,52},
   {3340,54},
   {3498,56},
   {3662,58},
   {3832,60},
   {4008,62},
   {4190,64},
   {4378,66},
   {4573,68},
   {4776,70},
   {4985,72},
};

LGStatus::LGStatus()
{
   m_time = 0;
   m_updates = 0;

   m_error = 0;
   m_inlet = 0;
   m_outlet = 0;
   m_oat = 0;
   m_room = 0;
   m_dhw = 0;
   m_heatingTarget = 0;
   m_wcOffset = 0;
   m_dhwTarget = 0;

   m_heatingMode = 0;
   m_extWaterPumpOn = false;

   m_isCompressorOn = false;
   m_isHeating = false;
   m_isDHW = false;
   m_isLegionella = false;
   m_isImmersion = false;
   m_isSilent = false;
   m_isDefrost = false;
}

LGHeatPump::LGHeatPump( ModbusMaster *master ) :
     m_registers( nullptr ),
     m_currentStatus(),
     m_numRegisters( 0 ),
     m_series( 0 ),
     m_modbus( master ),
     m_modbusAddress( 0 ),
     m_softwareVersion(),
     m_millisLastAquisition( -LG_MIN_SAMPLING_PERIOD_MS ),
     m_currentKW( 0 ),
     m_flowRateWhenNotHeating( 0 ),
     m_logRegisters( false )
{
   PW_DEBUG( "LGHeatPump::LGHeatPump()" );

   m_currentStatus.m_modbusError = false;

   cJSON *root = getAllSensorJSON();

   if ( root && isSensorRequired( LGHEATPUMP_SENSOR_NAME ) )
   {
      cJSON *sensor;
      cJSON_ArrayForEach( sensor,root )
      {
         if ( strcmpcJSON( sensor,"type",LGHEATPUMP_SENSOR_NAME ) == 0 )
         {
            uint8_t series,writeReg;

            strncpy( m_softwareVersion,getStringFromcJSON( sensor,"software" ).c_str(),MAX_LGSOFTWARE_LENGTH );

            m_logRegisters = getIntFromcJSON( sensor,"write",0 );
            series = getIntFromcJSON( sensor,"series",0 );
            m_modbusAddress = getIntFromcJSON( sensor,"address",0x11 );
            s_modbusAddress = m_modbusAddress;
            m_flowRateWhenNotHeating = getIntFromcJSON( sensor,"flowInNotHeating",0 );

            if ( series == 4 )
            {
               m_series = series;
            }
            else
            {
               PW_WARN( "Unsupported LG series (%d)", series );
            }

            PW_DEBUG( "address %u, write %d series %d flow in !heating %d",m_modbusAddress,m_logRegisters,series,m_flowRateWhenNotHeating );
            break;
         }
      }
   }


   if ( m_series )
   {
      // Parse the /lg.dat file for info

      fs::SPIFFSFS *spiffs = Config::instance()->getSPIFFS();
      File file = spiffs->open( "/lg.dat",FILE_READ );
      if ( !file )
      {
         PW_WARN( "/lg.dat is missing" );
      }
      else
      {
         m_registers = new LGRegister[ MAX_HP_REGISTERS ];

         for ( uint8_t i = 0; i < MAX_HP_REGISTERS; i++ )
         {
            m_registers[ i ].m_type = INVALID;
         }

         String data = file.readStringUntil( '@' );

         cJSON *root = cJSON_Parse( data.c_str() );

         if ( !root )
         {
            PW_ERROR( "Failed to parse lg.dat" );
            return;
         }

         if ( strcmpcJSON( root,"type","THERMAV" ) == 0 )
         {
            cJSON *registers = cJSON_GetObjectItem( root,"registers" );
            if ( registers && cJSON_IsArray( registers ) )
            {
               cJSON *reg;
               cJSON_ArrayForEach( reg,registers )
               {
                  if ( m_numRegisters < MAX_HP_REGISTERS  )
                  {
                     LGRegister *lgReg = &m_registers[ m_numRegisters ];
                     String name = getStringFromcJSON( reg,"name" );

                     lgReg->m_id = m_numRegisters + 1;
                     lgReg->m_address = getIntFromcJSON( reg,"addr",-1 );
                     lgReg->m_type = static_cast<ModbusType>( getIntFromcJSON( reg,"type",INPUTR ) );
                     lgReg->m_emonFeedId = getIntFromcJSON( reg,"emonFeedId",0 );

                     lgReg->m_scalingFactor = getFloatFromcJSON( reg,"scaling",1 );

                     // add to the lookup map, key = (type << 16 | modbus-addr + 1)

                     uint32_t parameter = (lgReg->m_type << 16) | (lgReg->m_address + 1);

                     m_registerMap[ parameter ] = m_numRegisters;

                     // add to sensor name map

                     setSensorName( HEATPUMP,lgReg->m_id,name );

                     PW_DEBUG( "LG %u %u %s %u %.1f %x %i",
                              lgReg->m_type,lgReg->m_address,name.c_str(),
                              lgReg->m_emonFeedId,lgReg->m_scalingFactor,parameter,m_numRegisters - 1 );

                     m_numRegisters++;
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
   return (m_numRegisters > 0);
}

bool LGHeatPump::isLogging()
{
   return m_logRegisters;
}

void  LGHeatPump::setCurrentKW( float_t kw )
{
   m_currentKW = kw;
}

void LGHeatPump::sample()
{
   if ( m_numRegisters && millis() - m_millisLastAquisition > LG_MIN_SAMPLING_PERIOD_MS )
   {
      START_TIMING( "LG Sample" );

      getLGData();
      m_millisLastAquisition = millis();

      END_TIMING;
   }
}

LGRegister *LGHeatPump::readNextSensor( uint8_t index )
{
   if ( index >= m_numRegisters )
   {
      return nullptr;
   }

   // If we've had a modbus error don't send any registers
   if ( m_currentStatus.m_modbusError )
   {
      PW_DEBUG( "LG: modbus error, not returning data this sample" );
      return nullptr;
   }

   return &m_registers[ index ];
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

   if ( !m_modbus )
   {
      PW_WARN( "No modbus available" );
      return false;
   }

   delay( 50 );
   m_modbus->clearResponseBuffer();

   switch( type )
   {
      case COIL: mbusRes = m_modbus->readCoils( m_registers[ start ].m_address,numRegs );
                 typeStr = "coils";
                 break;
      case DISCRETE: mbusRes = m_modbus->readDiscreteInputs( m_registers[ start ].m_address,numRegs );
                 typeStr = "discretes";
                 break;
      case HOLDING: mbusRes = m_modbus->readHoldingRegisters( m_registers[ start ].m_address,numRegs );
                 typeStr = "holding";
                 break;
      case INPUTR: mbusRes = m_modbus->readInputRegisters( m_registers[ start ].m_address,numRegs );
                 typeStr = "inputs";
                 break;
      default: PW_WARN( "Invalid modbus request type" );
               return false;
   }

   if ( mbusRes != ModbusMaster::ku8MBSuccess )
   {
      PW_ERROR( "Failed to get %u %s from %u [error %u]",numRegs,typeStr.c_str(),m_registers[ start ].m_address,numRegs,mbusRes );
      return false;
   }

   String dbg = typeStr;
   if ( type == COIL || type == DISCRETE )
   {
      for ( int i = 0; i < numRegs; i++ )
      {
         uint8_t  reg = i / 16;
         uint16_t word = m_modbus->getResponseBuffer( reg );
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
         m_registers[ start + i ].m_rawValue = static_cast<int16_t>(m_modbus->getResponseBuffer( i ));
         m_registers[ start + i ].m_value = m_registers[ start + i ].m_rawValue * m_registers[ start + i ].m_scalingFactor;
         dbg += " ";
         dbg += String( m_registers[ start + i ].m_value );
      }
   }

   PW_DEBUG( dbg.c_str() );

   return true;
}

void  LGHeatPump::getLGData()
{
   PW_MSG( "GetLGData" );

   if ( ! m_modbus )
   {
      return;
   }

   do
   {
      bool modbusFailed = false;
      m_currentStatus.m_modbusError = false;

      m_modbus->setSlaveId( m_modbusAddress );

      uint8_t  start,end;

      start = 0;
      while ( !modbusFailed && getContiguousRange( COIL, &start, &end ) )
      {
         PW_DEBUG( "LG Modbus coils from %u [%u] to %u [%u]",start,m_registers[ start ].m_address,
                                       end,m_registers[ end ].m_address );

         modbusFailed |= !getModbusData( COIL,start,end );

         start = end + 1;
      }

      start = 0;
      while ( !modbusFailed && getContiguousRange( DISCRETE, &start, &end ) )
      {
         PW_DEBUG( "LG Modbus discretes from %u [%u] to %u [%u]",start,m_registers[ start ].m_address,
                                       end,m_registers[ end ].m_address );

         modbusFailed |= !getModbusData( DISCRETE,start,end );

         start = end + 1;
      }

      start = 0;
      while ( !modbusFailed && getContiguousRange( HOLDING, &start, &end ) )
      {
         PW_DEBUG( "LG Modbus holding from %u [%u] to %u [%u]",start,m_registers[ start ].m_address,
                                       end,m_registers[ end ].m_address );

         modbusFailed |= !getModbusData( HOLDING,start,end );

         start = end + 1;
      }

      start = 0;
      while ( !modbusFailed && getContiguousRange( INPUTR, &start, &end ) )
      {
         PW_DEBUG( "LG Modbus inputs from %u [%u] to %u [%u]",start,m_registers[ start ].m_address,
                                       end,m_registers[ end ].m_address );

         !modbusFailed && getModbusData( INPUTR,start,end );
         start = end + 1;
      }

      if ( modbusFailed )
      {
         PW_ERROR( "LG: Failed to read modbus" );
         m_currentStatus.m_modbusError = true;
         break;
      }

      // log to file temporarily if enabled

      if ( m_logRegisters )
      {
         File file = SD.open( LGREGISTERS_LOG,FILE_APPEND );
         if ( file )
         {
            char buff[ 80 ];
            for ( int i = 0; i < m_numRegisters; i++ )
            {
               LGRegister *reg = &m_registers[ i ];

               if ( reg->m_type == CALCULATED )
               {
                  continue;
               }

               snprintf( buff,80,"%d,%d,%d",reg->m_type,reg->m_address,reg->m_rawValue );
               file.println( buff );
            }
            file.close();
         }
      }

      // lets zero the flow rate if returned 5 l/min from LG
      {
         float_t  flowRate = 0;
         float_t  tempFlowRate;

         if ( getValue( FLOW_RATE,&tempFlowRate ) )
         {
            if ( fabs(tempFlowRate - 5) > 0.1 )
            {
               flowRate = tempFlowRate;
            }
         }
         setValue( FLOW_RATE,flowRate );
      }

      // Now generate calculated data, need active compressor
      bool state;

      setValue( HEATING_POWER,0 );
      setValue( HIGH_PRESS_TEMP,0 );
      setValue( LOW_PRESS_TEMP,0 );
      setValue( COP,0 );
      setValue( COMPRESSION_RATIO,1 );

      if ( getStatus( COMPRESSOR_STATUS,&state ) )
      {
         if ( !state )
         {
            // We ordinarily report the actual flow rate when not in a
            // compressor cycle, if we override this then we report the settings
            // value as the flow rate if it's non-zero to make graph in openemoncms
            // easier to read if LG's pump setting in heating is not set continuous

            if ( m_flowRateWhenNotHeating )
            {
               float_t  tempFlowRate = 0;

               if ( getValue( FLOW_RATE,&tempFlowRate ) )
               {
                  if ( tempFlowRate > 1 )
                  {
                     tempFlowRate = m_flowRateWhenNotHeating;
                  }
               }
               setValue( FLOW_RATE,tempFlowRate );
            }
         }
         else
         {
            float_t  flowRate,flowTemp,returnTemp,currentPower = 0;

            if ( getValue( FLOW_RATE,&flowRate ) && getValue( INLET_TEMP,&returnTemp ) && getValue( OUTLET_TEMP,&flowTemp ) )
            {
               if ( flowTemp > returnTemp )
               {
                  currentPower = flowRate * 3.9 * (flowTemp - returnTemp) / 0.06;
                  setValue( HEATING_POWER,currentPower );
               }
            }

            float_t highPressure,lowPressure,temp;

            if ( getValue( HIGH_PRESSURE,&highPressure ) )
            {
               temp = convertR32PressureToTemp( highPressure );
               setValue( HIGH_PRESS_TEMP,temp );
            }

            if ( getValue( LOW_PRESSURE,&lowPressure ) )
            {
               temp = convertR32PressureToTemp( lowPressure );
               setValue( LOW_PRESS_TEMP,temp );
            }

            if ( m_currentKW > 0.0 )
            {
               setValue( COP,currentPower / m_currentKW );
            }

            setValue( COMPRESSION_RATIO,(highPressure + 100) / (lowPressure + 100) );
         }
      }

      for ( int i = 0; i < m_numRegisters; i++ )
      {
         PW_HP_MODBUS( "%s %.1f",getSensorName( HEATPUMP,m_registers[ i ].m_id ).c_str(),m_registers[ i ].m_value );
      }

      updateStatus();
   }
   while( 0 );
}

bool  LGHeatPump::valueChanged( uint32_t parameter )
{
   bool  hasChanged = false;

   int16_t newValue = getRawValue( parameter );

   if ( newValue == -9999 )
   {
      PW_ERROR( "Failed to get raw value for 0x%x",parameter );
   }
   else
   {
      switch ( parameter )
      {
         case ERROR_CODE: if ( m_currentStatus.m_error != newValue ) { hasChanged = true; }
            break;
         case COMPRESSOR_STATUS: if ( m_currentStatus.m_isCompressorOn != newValue ) { hasChanged = true; }
            break;
         case TARGET_TEMP: if ( m_currentStatus.m_heatingTarget != newValue ) { hasChanged = true; }
            break;
         case WC_OFFSET_TEMP: if ( m_currentStatus.m_wcOffset != newValue ) { hasChanged = true; }
            break;
         case DHW_TARGET_TEMP: if ( m_currentStatus.m_dhwTarget != newValue ) { hasChanged = true; }
            break;
         case DHW_HEATING: if ( m_currentStatus.m_isDHW != newValue ) { hasChanged = true; }
            break;
         case LEGIONELLA_STATUS: if ( m_currentStatus.m_isLegionella != newValue ) { hasChanged = true; }
            break;
         case BOOST_WATER: if ( m_currentStatus.m_isImmersion != newValue ) { hasChanged = true; }
            break;
         case SILENT_STATUS: if ( m_currentStatus.m_isSilent != newValue ) { hasChanged = true; }
            break;
         case DEFROST_STATUS: if ( m_currentStatus.m_isDefrost != newValue ) { hasChanged = true; }
            break;
         case HEATING_MODE: if ( m_currentStatus.m_heatingMode != newValue ) { hasChanged = true; }
            break;
         case HEATING_ENABLED: if ( m_currentStatus.m_isHeating != newValue ) { hasChanged = true; }
            break;
         default:
            hasChanged = false;
      }
   }

   if ( hasChanged )
   {
      PW_MSG( "Changed Parameter 0x%x to %d",parameter,newValue );
   }

   return hasChanged;
}

void  LGHeatPump::updateStatus()
{
   bool  updateState = false;
   bool  addHeader = false;

   // Check conditions for updating..

   if ( !m_currentStatus.m_time )
   {
      updateState = true;
      m_currentStatus.m_updates = 0;
   }
   else
   {
      updateState |= valueChanged( COMPRESSOR_STATUS );
      updateState |= valueChanged( ERROR_CODE );
      updateState |= valueChanged( TARGET_TEMP );
      updateState |= valueChanged( WC_OFFSET_TEMP );
      updateState |= valueChanged( DHW_TARGET_TEMP );
      updateState |= valueChanged( HEATING_MODE );
      updateState |= valueChanged( HEATING_ENABLED );
      updateState |= valueChanged( DHW_HEATING );
      updateState |= valueChanged( LEGIONELLA_STATUS );
      updateState |= valueChanged( BOOST_WATER );
      updateState |= valueChanged( SILENT_STATUS );
      updateState |= valueChanged( DEFROST_STATUS );
   }

   if ( !updateState )
   {
      return;
   }

   if ( m_currentStatus.m_updates == 1000 )
   {
      PW_WARN( "LG event log limit reached" );
   }
   else
   {
      struct tm timeInfo;
      char  line[ 80 ];
      char  timeStr[ 32 ];

      // limit to 1000 entries - so if error arises we can see it and
      // we won't write too much data to the log.  Its a daily file too.
      // This isn't perfect as the count resets to zero on power cycle
      // so could get more than 1000 but should be low chance of lots of
      // power cycles and some systemic faiure with the LG.

      m_currentStatus.m_updates++;

      time( &m_currentStatus.m_time );
      localtime_r( &m_currentStatus.m_time,&timeInfo );
      strftime( timeStr,32,"%Y%m%d,%H:%M:%S",&timeInfo );

      m_currentStatus.m_isCompressorOn = getRawValue( COMPRESSOR_STATUS );
      m_currentStatus.m_error = getRawValue( ERROR_CODE );
      m_currentStatus.m_inlet = getRawValue( INLET_TEMP );
      m_currentStatus.m_outlet = getRawValue( OUTLET_TEMP );
      m_currentStatus.m_oat = getRawValue( OUTSIDE_TEMP );
      m_currentStatus.m_room = getRawValue( ROOM_TEMP );
      m_currentStatus.m_dhw = getRawValue( DHW_TEMP );
      m_currentStatus.m_heatingTarget = getRawValue( TARGET_TEMP );
      m_currentStatus.m_wcOffset = getRawValue( WC_OFFSET_TEMP );

      m_currentStatus.m_dhwTarget = getRawValue( DHW_TARGET_TEMP );
      m_currentStatus.m_heatingMode = getRawValue( HEATING_MODE );
      m_currentStatus.m_extWaterPumpOn = getRawValue( EXT_WATER_PUMP_STATUS );
      m_currentStatus.m_isHeating = getRawValue( HEATING_ENABLED );
      m_currentStatus.m_isDHW = getRawValue( DHW_HEATING );
      m_currentStatus.m_isLegionella = getRawValue( LEGIONELLA_STATUS );
      m_currentStatus.m_isImmersion = getRawValue( BOOST_WATER );
      m_currentStatus.m_isSilent = getRawValue( SILENT_STATUS );
      m_currentStatus.m_isDefrost = getRawValue( DEFROST_STATUS );

      snprintf( line,80,"%s,%d,%d,%.1f,%.1f,%.1f,%.1f,%d,%d,%.1f,%.1f,%d,%.1f,%.1f,%d,%d,%d,%d",
               timeStr,
               m_currentStatus.m_error,
               m_currentStatus.m_isCompressorOn,
               m_currentStatus.m_inlet * 0.1,
               m_currentStatus.m_outlet * 0.1,
               m_currentStatus.m_room * 0.1,
               m_currentStatus.m_oat * 0.1,
               m_currentStatus.m_heatingMode,
               m_currentStatus.m_isHeating,
               m_currentStatus.m_heatingTarget * 0.1,
               m_currentStatus.m_wcOffset * 1.0,
               m_currentStatus.m_isDHW,
               m_currentStatus.m_dhw * 0.1,
               m_currentStatus.m_dhwTarget * 0.1,
               m_currentStatus.m_isSilent,
               m_currentStatus.m_isLegionella,
               m_currentStatus.m_isImmersion,
               m_currentStatus.m_isDefrost );

      PW_MSG( "LG events: %u",m_currentStatus.m_updates );
      PW_MSG( "LG state change: %s",line );

      // we may need to write header if the log file doesn't exist
      if ( ! Config::instance()->getSPIFFS()->exists( LGSTATUS_LOG ) )
      {
         addHeader = true;
      }

      File file = Config::instance()->getSPIFFS()->open( LGSTATUS_LOG,FILE_APPEND );
      if ( file )
      {
         if ( addHeader )
         {
            file.println( "date,time,error,compressor,"
                          "inlet,outlet,room,outside,"
                          "heating-mode,heating-active,heating-target,wc-offset,"
                          "dhw,dhw-temp,dhw-target,"
                          "silent,legionella,immersion,defrost" );
         }

         file.println( line );
         file.close();
      }
   }
}

void  LGHeatPump::resetEventLog()
{
   PW_MSG( "Reset LG event counter" );
   m_currentStatus.m_updates = 0;
}

void  LGHeatPump::dumpData()
{
   bool  state;

   (void) getStatus( HEATING_ENABLED,&state );
   (void) getStatus( DHW_ENABLED,&state );
   (void) getStatus( SILENT_ENABLED,&state );
   (void) getStatus( WATER_FLOW_STATUS,&state );
   (void) getStatus( WATER_PUMP_STATUS,&state );
   (void) getStatus( EXT_WATER_PUMP_STATUS,&state );
   (void) getStatus( COMPRESSOR_STATUS,&state );
   (void) getStatus( DHW_HEATING,&state );
   (void) getStatus( LEGIONELLA_STATUS,&state );
   (void) getStatus( SILENT_STATUS,&state );
   (void) getStatus( DEFROST_STATUS,&state );
   (void) getStatus( BOOST_WATER,&state );

   float_t value;

   (void) getValue( ERROR_CODE,&value );
   (void) getValue( HEATING_MODE,&value );
   (void) getValue( INLET_TEMP,&value );
   (void) getValue( OUTLET_TEMP,&value );
   (void) getValue( DHW_TEMP,&value );
   (void) getValue( ROOM_TEMP,&value );
   (void) getValue( FLOW_RATE,&value );
   (void) getValue( OUTSIDE_TEMP,&value );
   (void) getValue( PIPE_IN_TEMP,&value );
   (void) getValue( SUCTION_TEMP,&value );
   (void) getValue( DISCHARGE_TEMP,&value );
   (void) getValue( HEX_TEMP,&value );
   (void) getValue( HIGH_PRESSURE,&value );
   (void) getValue( LOW_PRESSURE,&value );
   (void) getValue( COMPRESSOR_HZ,&value );
}

bool  LGHeatPump::getStatus( uint32_t parameter,bool *state )
{
   bool  registerOk = false;

   std::map<uint32_t, uint8_t>::const_iterator it = m_registerMap.find( parameter );
   if ( it == m_registerMap.end() )
   {
      PW_ERROR( "No register found for %x",parameter );
   }
   else
   {
      uint8_t  index = it->second;
      LGRegister *lgReg = &m_registers[ index ];

      *state = lgReg->m_rawValue;
      registerOk = true;
      PW_DEBUG( "HP: %s:%u",getSensorName( HEATPUMP,lgReg->m_id ).c_str(),*state );
   }

   return registerOk;
}

bool  LGHeatPump::getValue( uint32_t parameter,float_t *value )
{
   bool  registerOk = false;

   std::map<uint32_t, uint8_t>::const_iterator it = m_registerMap.find( parameter );
   if ( it == m_registerMap.end() )
   {
      PW_ERROR( "No register found for %x",parameter );
   }
   else
   {
      uint8_t  index = it->second;
      LGRegister *lgReg = &m_registers[ index ];

      *value = lgReg->m_value;
      registerOk = true;
      PW_DEBUG( "HP: %s:%.1f",getSensorName( HEATPUMP,lgReg->m_id ).c_str(),*value );
   }

   return registerOk;
}

int16_t  LGHeatPump::getRawValue( uint32_t parameter )
{
   int16_t  value = -9999;

   std::map<uint32_t, uint8_t>::const_iterator it = m_registerMap.find( parameter );
   if ( it == m_registerMap.end() )
   {
      PW_ERROR( "No register found for %x",parameter );
   }
   else
   {
      uint8_t  index = it->second;
      LGRegister *lgReg = &m_registers[ index ];

      value = lgReg->m_rawValue;
      PW_DEBUG( "HP-Raw: 0x%x:%s:%u",parameter,getSensorName( HEATPUMP,lgReg->m_id ).c_str(),value );
   }

   return value;
}

bool  LGHeatPump::setValue( uint32_t parameter,float_t value )
{
   bool  registerOk = false;

   std::map<uint32_t, uint8_t>::const_iterator it = m_registerMap.find( parameter );
   if ( it == m_registerMap.end() )
   {
      PW_ERROR( "No register found for %x",parameter );
   }
   else
   {
      uint8_t  index = it->second;
      LGRegister *lgReg = &m_registers[ index ];

      lgReg->m_value = value;
      PW_DEBUG( "HP: set %s:%.1f",getSensorName( HEATPUMP,lgReg->m_id ).c_str(),value );
   }

   return registerOk;
}

void  LGHeatPump::updateUserIO( UserIO *userIO )
{
   char line[ MAX_DISPLAY_COLUMNS ];

   if ( m_currentStatus.m_modbusError )
   {
      snprintf( line,MAX_DISPLAY_COLUMNS,"Modbus Err" );
      userIO->storeLine( 0,line );
      return;
   }

   float_t  flowRate,targetTemp;
   (void) getValue( FLOW_RATE,&flowRate );
   (void) getValue( TARGET_TEMP,&targetTemp );
   snprintf( line,MAX_DISPLAY_COLUMNS,"%.1f l/m. t: %.1f",flowRate,targetTemp );
   userIO->storeLine( 0,line );

   float_t inlet,outlet;
   (void) getValue( INLET_TEMP,&inlet );
   (void) getValue( OUTLET_TEMP,&outlet );
   snprintf( line,MAX_DISPLAY_COLUMNS,"i: %.1f o: %.1f",inlet,outlet );
   userIO->storeLine( 1,line );

   if ( !getRawValue( COMPRESSOR_STATUS ) )
   {
      snprintf( line,MAX_DISPLAY_COLUMNS,"Compress: OFF" );
      userIO->storeLine( 3,line );
      return;
   }

   float pwr;
   (void) getValue( HEATING_POWER,&pwr );
   snprintf( line,MAX_DISPLAY_COLUMNS,"%.0f [%.0f]",pwr,m_currentKW );
   userIO->storeLine( 2,line );

   float_t cop,carnotCOP,copRatio;
   float_t highT,lowT;
   (void) getValue( COP,&cop );
   (void) getValue( LOW_PRESS_TEMP,&lowT );
   (void) getValue( HIGH_PRESS_TEMP,&highT );
   if ( highT - lowT > 1.0F )
   {
      carnotCOP = (273 + highT) / ( highT - lowT );
      copRatio = 100.0 * (cop / carnotCOP);
   }
   else
   {
      carnotCOP = 1;
      copRatio = 1;
   }
   PW_DEBUG( "HP COP %.1f %.1f %.0f%",cop,carnotCOP,copRatio );

   snprintf( line,MAX_DISPLAY_COLUMNS,"%.1f %.1f %.0f",cop,carnotCOP,copRatio );
   userIO->storeLine( 3,line );

   float_t cr;
   char powerChar = '+';
   if ( getRawValue( SILENT_STATUS ) )
   {
      powerChar = '-';
   }

   (void) getValue( COMPRESSION_RATIO,&cr );
   snprintf( line,MAX_DISPLAY_COLUMNS,"%d Hz %c %.1f",getRawValue( COMPRESSOR_HZ ),powerChar,cr );
   userIO->storeLine( 4,line );

   snprintf( line,MAX_DISPLAY_COLUMNS,"Evap %.1f cond %.1f",lowT,highT );
   userIO->storeLine( 5,line );


}

float_t  LGHeatPump::convertR32PressureToTemp( float_t pressure )
{
   std::map<float_t, float_t >::const_iterator it = r32Lookup.begin();
   float_t lowT = -1,highT = -1;
   float_t lowP = -1,highP = -1;
   float_t temp = -100;

   while ( it != r32Lookup.end() )
   {
      if ( it->first <= pressure )
      {
         lowP = it->first;
         lowT = it->second;
      }
      else
      {
         highP = it->first;
         highT = it->second;
         break;
      }
      ++it;
   }

   if ( it != r32Lookup.end() )
   {
      float_t gradient = (highT - lowT) / (highP - lowP);
      temp = lowT + gradient * (pressure - lowP);
      PW_DEBUG( "R32 pressure %f : %f [%f ] - %f [%f] = %f",pressure,lowP,lowT,highP,highT,temp );
   }

   return temp;
}

#include <SD.h>
#include <FS.h>

void  getHPData()
{
   uint8_t mbusRes = 1;

   if ( s_master )
   {
      s_master->setSlaveId( s_modbusAddress );

      if ( GET_REGISTRY_INT( LG_MODBUS_START_REG ) > 0 )
      {
         static uint16_t x = GET_REGISTRY_INT( LG_MODBUS_START_REG );
         for ( int i = x; i < x+8; i++ )
         {
            s_master->clearResponseBuffer();
            delay( 50 );
            mbusRes = s_master->readInputRegisters( i,1 );

            if ( !mbusRes || i % 128 == 0 )
            {
               PW_HP_MODBUS( "IR: %u %u [%u]",i,s_master->getResponseBuffer( 0 ),mbusRes );
               PW_HP_MODBUS( "IR: %u %u",i,s_master->getResponseBuffer( 0 ) );
               File file = SD.open( LGREGISTER_SCAN_LOG,FILE_APPEND );
               if ( file )
               {
                  char a[ 40 ];
                  sprintf( a,"IR: %u %u [%u]",i,s_master->getResponseBuffer( 0 ),mbusRes );
                  file.println( a );
                  file.close();
               }
            }
            else if ( i % 128 != 0 )
            {
               PW_ERROR( "!input: %d %u",i,mbusRes );
            }
         }
         x += 8;
      }
   }
}
