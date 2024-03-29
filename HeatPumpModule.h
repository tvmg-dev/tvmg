#ifndef HEAT_PUMP_MODULE_H
#define HEAT_PUMP_MODULE_H

#include <ModbusMaster.h>

#include "PowerModule.h"

#include "utils.h"

#define MAX_HEATPUMP_NAME  32

// The ID should be matched in the sensors.dat file

#define HEAT_PUMP_ID    200

typedef struct {
   uint8_t  m_id;          // should be unique ID
   uint32_t m_emonFeedId;  // Feed ID for emonCMS
   float_t  m_flowRate;    // FlowRate
   float_t  m_power;       // power
   float_t  m_energy;      // energy
   char    *m_name;        // name (don't store the name here to keep the structure size to minimum
} HeatPumpSensor;

class HeatPumpModule
{
public:
   HeatPumpModule( PowerModule *powerModule );
   ~HeatPumpModule();
   void  initialise( void );
   HeatPumpSensor *readNextSensor( uint8_t index );
   bool  sampleHP();

private:
   typedef struct {
      HeatPumpSensor m_hpSensor;                // sensor essentials
      char     m_name[ MAX_HEATPUMP_NAME + 1 ];    // Friendly name
      uint8_t  m_address;                       // address on modbus
      bool     m_isValid;
   } PrivateSensor;

   PrivateSensor  m_sensor;
   PowerModule    *m_powerModule;
};

#endif
