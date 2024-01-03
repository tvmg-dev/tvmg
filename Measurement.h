#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <time.h>

#include "utils.h"
#include "TemperatureModule.h"
#include "PowerModule.h"

class PowerModule;
class Storage;
class Networking;

#define  MAX_SENSOR_NAME

class MeasurementSensor;

//Measurement::Sample & operator = (const Measurement::Sample &other )

class Measurement
{
public:
   struct Sample {
      time_t       m_sampleTime;
      TempSensor  *m_tempSensors[ MAX_TEMP_SENSORS + 1 ];         // The 1 after is null pointer to terminate the list
      PowerSensor *m_powerSensors[ MAX_POWER_SENSORS + 1 ];       // The 1 after is null pointer to terminate the list

      Sample();
      Sample( const Sample &other );
      Sample & operator=(const Sample &other );
   };

   Measurement( TemperatureModule *tempModule, PowerModule *powerModule,Storage *storage );
   ~Measurement();
   void     initialise();
   void     takeSample();
   Sample   getLastSample();

private:
   void  saveLastSample();

   TemperatureModule *m_tempModule;
   PowerModule       *m_powerModule;
   Storage           *m_storageModule;
   Networking        *m_networking;
   Sample            *m_samples;
   uint16_t          m_numSamples;
   uint16_t          m_read,m_write;
   Sample            m_lastSample;
   uint32_t          m_millisLastAquisition;
};

#endif
