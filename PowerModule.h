#ifndef POWER_MODULE_H
#define POWER_MODULE_H

#include <ModbusMaster.h>

#include "utils.h"

#define MAX_POWER_SENSORS  2
#define MAX_POWER_NAME     32

#define POWER_INVALID         -1
#define ENERGY_INVALID        -1

typedef struct {
   uint8_t  m_id;          // should be unique ID
   uint32_t m_emonFeedId;  // Feed ID for emonCMS
   float_t  m_power;       // power
   float_t  m_energy;      // energy
   char    *m_name;        // name (don't store the name here to keep the structure size to minimum
} PowerSensor;

class PowerModule
{
public:
   PowerModule();
   ~PowerModule();
   void  initialise( void );
   PowerSensor *readNextSensor( uint8_t index );
   bool  getPower( uint8_t index );

private:
   typedef struct {
      PowerSensor m_sensor;                     // sensor essentials
      char     m_name[ MAX_POWER_NAME + 1 ];    // Friendly name
      uint8_t  m_address;                       // address on modbus
      bool     m_isValid;
   } PrivateSensor;

   HardwareSerial *m_serial;
   ModbusMaster   *m_master;
   PrivateSensor  m_sensors[ MAX_POWER_SENSORS ];
   uint8_t        m_numSensors;
   bool           m_masterStarted;
};

#endif
