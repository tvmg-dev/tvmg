#include <cJSON.h>

#include "HeatPumpModule.h"

#include "hwconfig.h"
#include "config.h"

HeatPumpModule::HeatPumpModule( PowerModule *powerModule )
           : m_sensor(),
             m_powerModule( powerModule )
{
   PW_DEBUG( "HeatPumpModule::HeatPump()" );

   m_sensor.m_isValid = false;
   m_sensor.m_hpSensor.m_name = nullptr;
   m_sensor.m_hpSensor.m_power = POWER_INVALID;
   m_sensor.m_hpSensor.m_energy = ENERGY_INVALID;

   // Parse the /sensors.dat file for thermometers

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
               PrivateSensor *hpSensor;

               hpSensor = &m_sensor;
               strncpy( hpSensor->m_name,cJSON_GetObjectItem( sensor,"name" )->valuestring,MAX_POWER_NAME );
               hpSensor->m_address = cJSON_GetObjectItem( sensor,"address" )->valueint;
               hpSensor->m_hpSensor.m_emonFeedId = cJSON_GetObjectItem( sensor,"emonFeedId" )->valueint;
               hpSensor->m_hpSensor.m_id = cJSON_GetObjectItem( sensor,"id" )->valueint;
               hpSensor->m_hpSensor.m_name = hpSensor->m_name;
               hpSensor->m_hpSensor.m_power = POWER_INVALID;
               hpSensor->m_hpSensor.m_energy = ENERGY_INVALID;
               hpSensor->m_isValid = true;

               PW_DEBUG( "Power: name %s address %u",hpSensor->m_name,hpSensor->m_address );
               PW_DEBUG( "Id %u,  feed %u",hpSensor->m_hpSensor.m_id,hpSensor->m_hpSensor.m_emonFeedId );
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
         PW_WARN( "No HP sensors registered" );
      }
   }
}

HeatPumpModule::~HeatPumpModule()
{
   PW_DEBUG( "HeatPumpModule::~HeatPumpModule()" );
}

void HeatPumpModule::initialise( void )
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

bool  HeatPumpModule::sampleHP()
{
   ModbusMaster *modbus = m_powerModule->getModbus();
   if ( modbus )
   {
      PW_DEBUG( "would sample HP" );
#if 1
      // force a short delay
      delay( hwConfig->ModBusMsgDelay );

      PW_DEBUG( "Set slave to 0x%02x",m_sensor.m_address );
      modbus->setSlaveId( m_sensor.m_address );

      uint8_t  numRegs = 5;
      // Read 12 input registers
      uint8_t modbusResult = modbus->readInputRegisters( 0x0,numRegs );

      if ( modbusResult != ModbusMaster::ku8MBSuccess )
      {
         PW_WARN( "Failed to read input resgisters for HP %u",modbusResult );
      }
      else
      {
         uint16_t reg16;
         float_t  value;
         String   opString = "HP Input Registers\n";
         char     line[ 20 ];

         for ( int i = 0; i < numRegs; i++ )
         {
            reg16 = modbus->getResponseBuffer( i );
            sprintf( line,"%02d: %d\n",i,reg16 );
            opString += line;
         }

         PW_DEBUG( "%s",opString.c_str() );
         return true;
      }
   }

#endif

   return false;
}

