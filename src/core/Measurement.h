/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <time.h>
#include <vector>

#include "src/sensors/TemperatureModule.h"
#include "src/sensors/PowerModule.h"
#include "src/sensors/ShellyPM.h"
#include "src/sensors/LGHeatPump.h"
#include "src/sensors/HeatMeter.h"

class Storage;
class Networking;

class Measurement
{
public:
   struct Sample {
      time_t                              m_sampleTime;
      std::vector<TempSensor>             m_tempSensors;
      std::vector<PowerSensor>            m_powerSensors;
      std::vector<ShellyPowerSensor>      m_shellyPowerSensors;
      std::vector<LGHeatPump::LGRegister> m_lgRegisters;
      std::vector<HeatMeterSensor>        m_heatMeterSensors;

      Sample();
      Sample( const Sample &other );
      Sample & operator=(const Sample &other );
   };

   Measurement( TemperatureModule *tempModule, PowerModule *powerModule,ShellyPowerModule *shellyModule, LGHeatPump *heatPump,
                                    HeatMeterModule *hmModule,Storage *storage,Networking *networking );
   ~Measurement();
   static   Measurement *instance();
   void     initialise();
   void     takeSample();
   const Sample   &getLastSample();
   char *getSampleJSON();
   bool  didDailyUpdate();

private:
   void  saveLastSample();
   void  updateEmon();
   bool  shouldSendDailyUpdate();
   void  sendDailyUpdate();

   TemperatureModule *m_tempModule;
   PowerModule       *m_powerModule;
   ShellyPowerModule *m_shellyPowerModule;
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
