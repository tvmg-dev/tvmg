#ifndef POWER_MODULE_H
#define POWER_MODULE_H

#include <ModbusMaster.h>

#include "utils.h"

#define MAX_POWER_SENSORS  2
#define MAX_POWER_NAME     32

#define HEATPUMP_POWER        0
#define IMMERSION_POWER       1
#define POWER_INVALID         -1
#define ENERGY_INVALID        -1

class PowerModule
{
public:
   PowerModule();
   ~PowerModule();
   void  initialise( void );
   void  registerSensor( uint8_t index, uint8_t addr, char *name );
   bool  getPower( uint8_t index, float *power, float *energy );

private:
   typedef struct {
      uint8_t  m_address;                       // address on modbus
      char     m_name[ MAX_POWER_NAME + 1 ];    // Friendly name
      float_t  m_power;
      float_t  m_energy;
      bool     m_isValid;
   } Sensor;

   HardwareSerial *m_serial;
   ModbusMaster   *m_master;
   Sensor         m_sensors[ MAX_POWER_SENSORS ];
   bool           m_masterStarted;
};

#endif
