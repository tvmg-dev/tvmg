#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <time.h>

#include "utils.h"

class TemperatureModule;
class PowerModule;
class Storage;
class Networking;

class Measurement
{
public:
   typedef struct {
      time_t   m_sampleTime;
      float_t  m_flowHP,m_returnHP;
      float_t  m_flowHeating,m_returnHeating;
      float_t  m_outside;
      float_t  m_powerHP,m_powerImmersion;
      float_t  m_energyHP,m_energyImmersion;
   } Sample;

   Measurement( TemperatureModule *tempModule, PowerModule *powerModule,Storage *storage );
   ~Measurement();
   void     initialise( void );
   void     takeSample( void );
   Sample   getLastSample( void );
   void     dumpMeasurements( void );

private:
   void  displayMeasurement( uint16_t index );
   void  saveLastSample( void );

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
