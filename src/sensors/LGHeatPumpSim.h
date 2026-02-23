#ifndef LG_HEATPUMP_SIMULATOR_H
#define LG_HEATPUMP_SIMULATOR_H

#include "src/sensors/LGHeatPump.h"

#include "src/userio/Indicator.h"

#define  LGHEATPUMPSIM_SENSOR_NAME  "LGHEATPUMPSIM"

class ModbusRTU;

class LGHeatPumpSimulator
{
public:
   LGHeatPumpSimulator( HardwareSerial *serial );
   ~LGHeatPumpSimulator();
   void  initialise();
   void  heartbeat();

   static Indicator  *s_indicator;

   private:
   struct MaxRegisterAddresses
   {
      int coils = 0;
      int discretes = 0;
      int holding = 0;
      int inputs = 0;
   };

   MaxRegisterAddresses scanForMaxAddresses( const char *path );
   void updateModbusFromFile( const char* path );

   HardwareSerial *m_serial;
   ModbusRTU      *m_slave;
   bool           m_available;
   uint8_t        m_series;
   uint16_t       m_modbusAddress;
   int16_t        m_recordIndex;
   uint16_t       m_recordSize;
   char           m_softwareVersion[ MAX_LGSOFTWARE_LENGTH ];
};

#endif
