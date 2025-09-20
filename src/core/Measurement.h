#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <time.h>
#include <vector>

#include "utils.h"
#include "src/sensors/TemperatureModule.h"
#include "src/sensors/PowerModule.h"
#include "src/sensors/LGHeatPump.h"
#include "src/sensors/HeatMeter.h"

class Storage;
class Networking;

class Measurement
{
public:
   struct Sample {
      time_t       m_sampleTime;
      std::vector<TempSensor>       m_tempSensors;
      std::vector<PowerSensor>      m_powerSensors;
      std::vector<LGRegister>       m_lgRegisters;
      std::vector<HeatMeterSensor>  m_heatMeterSensors;

      Sample();
      Sample( const Sample &other );
      Sample & operator=(const Sample &other );
   };

   Measurement( TemperatureModule *tempModule, PowerModule *powerModule,LGHeatPump *heatPump,
                                    HeatMeterModule *hmModule,Storage *storage,Networking *networking );
   ~Measurement();
   void     initialise();
   void     takeSample();
   const Sample   &getLastSample();
   bool  didDailyUpdate();
   bool  isTemperatureDataAvailable();
   bool  isPowerDataAvailable();
   bool  isHeatMeterDataAvailable();

private:
   void  saveLastSample();
   bool  shouldSendDailyUpdate();
   void  updateEmon();
   void  sendUpdate();

   TemperatureModule *m_tempModule;
   PowerModule       *m_powerModule;
   LGHeatPump        *m_heatPump;
   HeatMeterModule   *m_heatMeterModule;
   Storage           *m_storageModule;
   Networking        *m_networking;
   Sample            m_lastSample;
   Sample            m_newSample;
   uint32_t          m_millisLastAquisition;

   uint8_t     m_dailyUpdateHour;
   bool        m_dailyUpdated;
   uint32_t    m_dailyModbusSent;
   uint32_t    m_dailyModbusFailed;
   uint32_t    m_dailyEmonSent;
   uint32_t    m_dailyEmonFailed;
};

#endif
