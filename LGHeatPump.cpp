#include <SD.h>

#include <WiFi.h>
#include <cJSON.h>

#include <ModbusMaster.h>

#include "LGHeatPump.h"

#include "hwconfig.h"
#include "config.h"
#include "UserIO.h"

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

LGHeatPump::LGHeatPump( ModbusMaster *master ) :
     m_registers( nullptr ),
     m_currentStatus(),
     m_numRegisters( 0 ),
     m_modbusRTU( master ),
     m_modbusRequests( 0 ),
     m_modbusFailures( 0 ),
     m_millisLastAquisition( -LG_MIN_SAMPLING_PERIOD_MS ),
     m_currentKW(0),
     m_useFlowRateWhenNotHeating( true )
{
   PW_DEBUG( "LGHeatPump::LGHeatPump()" );

   m_currentStatus.m_time = 0;
   m_currentStatus.m_updates = 0;

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

      if ( strcmp( "THERMAV",cJSON_GetObjectItem( root,"type" )->valuestring ) == 0 )
      {
         cJSON *registers = cJSON_GetObjectItem( root,"registers" );
         if ( registers && cJSON_IsArray( registers ) )
         {
            cJSON *reg;
            cJSON_ArrayForEach( reg,registers )
            {
               if ( m_numRegisters < MAX_HP_REGISTERS  )
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

   if ( GET_REGISTRY_INT( LG_SET_ACTIVEFLOW_NOTHEATING ) > 0 )
   {
      m_useFlowRateWhenNotHeating = false;
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

void  LGHeatPump::setCurrentKW( float_t kw )
{
   m_currentKW = kw;
}

LGRegister *LGHeatPump::readNextSensor( uint8_t index )
{
   if ( index >= m_numRegisters )
   {
      return nullptr;
   }

   if ( !index && millis() - m_millisLastAquisition > LG_MIN_SAMPLING_PERIOD_MS )
   {
      getLGData();
      m_millisLastAquisition = millis();
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

   PW_DEBUG( dbg.c_str() );

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
            // compressor cycle, if we override this then we report 2.0 as the
            // flow rate if non-zero to make graph in openemoncms easier to read if LG's
            // pump setting in heating is not set continuous
            if ( !m_useFlowRateWhenNotHeating )
            {
               float_t  tempFlowRate = 0;

               if ( getValue( FLOW_RATE,&tempFlowRate ) )
               {
                  if ( tempFlowRate > 1 )
                  {
                     tempFlowRate = 2;
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
         PW_HP_MODBUS( "%s %.1f",m_registers[ i ].m_name,m_registers[ i ].m_value );
      }

      updateStatus();
   }

   END_TIMING;
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
         case UNIT_CYCLE: if ( m_currentStatus.m_isActive != newValue ) { hasChanged = true; }
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
      updateState |= valueChanged( DHW_TARGET_TEMP );
      updateState |= valueChanged( UNIT_CYCLE );
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
      m_currentStatus.m_dhw = getRawValue( DHW_TEMP );
      m_currentStatus.m_heatingTarget = getRawValue( TARGET_TEMP );
      m_currentStatus.m_dhwTarget = getRawValue( DHW_TARGET_TEMP );
      m_currentStatus.m_isActive = getRawValue( UNIT_CYCLE );
      m_currentStatus.m_isHeating = getRawValue( HEATING_ENABLED );
      m_currentStatus.m_isDHW = getRawValue( DHW_HEATING );
      m_currentStatus.m_isLegionella = getRawValue( LEGIONELLA_STATUS );
      m_currentStatus.m_isImmersion = getRawValue( BOOST_WATER );
      m_currentStatus.m_isSilent = getRawValue( SILENT_STATUS );
      m_currentStatus.m_isDefrost = getRawValue( DEFROST_STATUS );

      snprintf( line,80,"%s,%d,%d,%d,%.1f,%.1f,%d,%d,%.1f,%d,%.1f,%.1f,%d,%d,%d",
               timeStr,
               m_currentStatus.m_error,
               m_currentStatus.m_isCompressorOn,
               m_currentStatus.m_isSilent,
               m_currentStatus.m_inlet * 0.1,
               m_currentStatus.m_outlet * 0.1,
               m_currentStatus.m_isActive,
               m_currentStatus.m_isHeating,
               m_currentStatus.m_heatingTarget * 0.1,
               m_currentStatus.m_isDHW,
               m_currentStatus.m_dhw * 0.1,
               m_currentStatus.m_dhwTarget * 0.1,
               m_currentStatus.m_isLegionella,
               m_currentStatus.m_isImmersion,
               m_currentStatus.m_isDefrost );

      PW_MSG( line );

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
            file.println( "date,time,error,compressor,silent,inlet,outlet,active,heating,heating-target,"
                          "dhw,dhw-temp,dhw-target,legionella,immersion,defrost" );
         }

         file.println( line );
         file.close();
      }
   }
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
   (void) getStatus( DEFROST_STATUS,&state );
   (void) getStatus( DHW_HEATING,&state );
   (void) getStatus( LEGIONELLA_STATUS,&state );
   (void) getStatus( SILENT_STATUS,&state );
   (void) getStatus( DEFROST_STATUS,&state );
   (void) getStatus( BOOST_WATER,&state );

   float_t value;

   (void) getValue( ERROR_CODE,&value );
   (void) getValue( UNIT_CYCLE,&value );
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
      *state = m_registers[ index ].m_rawValue;
      registerOk = true;
      PW_DEBUG( "HP: %s:%u",m_registers[ index ].m_name,*state );
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
      *value = m_registers[ index ].m_value;
      registerOk = true;
      PW_DEBUG( "HP: %s:%.1f",m_registers[ index ].m_name,*value );
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
      value = m_registers[ index ].m_rawValue;
      PW_DEBUG( "HP-Raw: %s:%u",m_registers[ index ].m_name,value );
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
      m_registers[ index ].m_value = value;
      PW_DEBUG( "HP: set %s:%.1f",m_registers[ index ].m_name,value );
   }

   return registerOk;
}

void  LGHeatPump::getModbusStats( uint32_t *requests,uint32_t *failures )
{
   *requests = m_modbusRequests;
   *failures = m_modbusFailures;
}

void  LGHeatPump::updateUserIO( UserIO *userIO )
{
   char line[ MAX_OLED_COLUMNS ];

   float_t  flowRate,targetTemp;
   (void) getValue( FLOW_RATE,&flowRate );
   (void) getValue( TARGET_TEMP,&targetTemp );
   snprintf( line,MAX_OLED_COLUMNS,"%.1f l/m. t: %.1f",flowRate,targetTemp );
   userIO->storeLine( 0,line );

   float_t inlet,outlet;
   (void) getValue( INLET_TEMP,&inlet );
   (void) getValue( OUTLET_TEMP,&outlet );
   snprintf( line,MAX_OLED_COLUMNS,"i: %.1f o: %.1f",inlet,outlet );
   userIO->storeLine( 1,line );

   if ( !getRawValue( COMPRESSOR_STATUS ) )
   {
      snprintf( line,MAX_OLED_COLUMNS,"Compress: OFF" );
      userIO->storeLine( 3,line );
      return;
   }

   float pwr;
   (void) getValue( HEATING_POWER,&pwr );
   snprintf( line,MAX_OLED_COLUMNS,"%.0f [%.0f]",pwr,m_currentKW );
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

   snprintf( line,MAX_OLED_COLUMNS,"%.1f %.1f %.0f",cop,carnotCOP,copRatio );
   userIO->storeLine( 3,line );

   float_t cr;
   char powerChar = '+';
   if ( getRawValue( SILENT_STATUS ) )
   {
      powerChar = '-';
   }

   (void) getValue( COMPRESSION_RATIO,&cr );
   snprintf( line,MAX_OLED_COLUMNS,"%d Hz %c %.1f",getRawValue( COMPRESSOR_HZ ),powerChar,cr );
   userIO->storeLine( 4,line );

   snprintf( line,MAX_OLED_COLUMNS,"Evap %.1f cond %.1f",lowT,highT );
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
ModbusMaster *s_master = nullptr;

void  getHPData()
{
   uint8_t mbusRes = 1;

   if ( s_master )
   {
      s_master->setSlaveId( 32 );

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
               File file = SD.open( LGREGISTERS_LOG,FILE_APPEND );
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
