#ifndef TEMPERATURE_MODULE_H
#define TEMPERATURE_MODULE_H

#include <Arduino.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include "hwconfig.h"

#ifndef ONE_WIRE_GPIO
#error "Must define ONE_WIRE_GPIO"
#endif

//---------------------------------------------------------------------
// Temperature monitoring

#define MAX_TEMP_SENSORS                     5
#define MAX_TEMP_NAME                        32
#define TEMP_ADDR_STRLEN                     (1 + sizeof( DeviceAddress ) * 3 )
#define TEMPERATURE_PRECISION                11
#define TEMPERATURE_MIN_SAMPLING_PERIOD_MS   15000

class TemperatureModule
{
   typedef struct {
      DeviceAddress  m_address;                       // 64 bit address, array of 8 uint8_t
      char           m_name[ MAX_TEMP_NAME + 1 ];     // Friendly name
      uint8_t        m_busIndex;                      // index of the sensor on OneWire bus
      float_t        m_temp;                          // temperature (adjusted by calibration
      float_t        m_calibrationOffset;             // calibration offset
      bool           m_isValid;                       // true if registered ok
   } Sensor;

   public:
      TemperatureModule();
      ~TemperatureModule();
      void  initialise();
      void  registerSensor( uint8_t index, DeviceAddress deviceAddress,char *name, float calibrationOffset = 0.0f );
      bool  getTemperature( uint8_t index, float *temp );

   private:
      bool  getTemperatures( void );
      void  getAddressString( DeviceAddress addr,char *addrString );

      OneWire           *m_oneWireController;
      DallasTemperature *m_dallasController;

      bool              m_isOk;
      Sensor            m_sensors[ MAX_TEMP_SENSORS ];
      uint8_t           m_numSensors;

      int32_t           m_millisLastAquisition;       // milliseconds since last acquisition
};

#endif
