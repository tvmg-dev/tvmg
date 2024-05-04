#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <time.h>

#include "utils.h"
#include "TemperatureModule.h"
#include "PowerModule.h"
#include "LGHeatPump.h"

class Storage;
class Networking;

class Measurement
{
public:
   struct Sample {
      time_t       m_sampleTime;
      TempSensor  *m_tempSensors[ MAX_TEMP_SENSORS + 1 ];         // The 1 after is null pointer to terminate the list
      TempSensor   m_actualTemps[ MAX_TEMP_SENSORS ];             // local copy as remotes can update via UDP in another thread
      PowerSensor *m_powerSensors[ MAX_POWER_SENSORS + 1 ];       // The 1 after is null pointer to terminate the list

      Sample();
      Sample( const Sample &other );
      Sample & operator=(const Sample &other );
   };

   Measurement( TemperatureModule *tempModule, PowerModule *powerModule,LGHeatPump *heatPump,Storage *storage );
   ~Measurement();
   void     initialise();
   void     takeSample();
   Sample   getLastSample();

private:
   void  saveLastSample();

   TemperatureModule *m_tempModule;
   PowerModule       *m_powerModule;
   LGHeatPump        *m_heatPump;
   Storage           *m_storageModule;
   Networking        *m_networking;
   Sample            m_lastSample;
   uint32_t          m_millisLastAquisition;
};

#endif
