#ifndef POWER_MODULE_H
#define POWER_MODULE_H

#include <ModbusMaster.h>

#include "utils.h"

#define MAX_POWER_SENSORS  2
#define MAX_POWER_NAME     32

#define HEATPUMP_POWER        "HP"
#define IMMERSION_POWER       "IMMERSION"
#define POWER_INVALID         -1
#define ENERGY_INVALID        -1

class PowerModule
{
public:
   PowerModule();
   ~PowerModule();
   void  initialise( void );
   bool  getPower( char *name, float *power, float *energy );

private:
   typedef struct {
      uint8_t  m_address;                       // address on modbus
      char     m_name[ MAX_POWER_NAME + 1 ];    // Friendly name
      uint32_t m_emonFeedId;                    // Feed ID for emonCMS
      float_t  m_power;
      float_t  m_energy;
      bool     m_isValid;
   } Sensor;

   HardwareSerial *m_serial;
   ModbusMaster   *m_master;
   Sensor         m_sensors[ MAX_POWER_SENSORS ];
   uint8_t        m_numSensors;
   bool           m_masterStarted;
};

#endif
