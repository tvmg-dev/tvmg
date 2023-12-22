#ifndef TEMPERATURE_MODULE_H
#define TEMPERATURE_MODULE_H

#include <Arduino.h>

#include <OneWire.h>
#include <DallasTemperature.h>

//---------------------------------------------------------------------
// Temperature monitoring

#define MAX_TEMP_SENSORS                     10
#define MAX_TEMP_NAME                        32
#define TEMP_ADDR_STRLEN                     (1 + sizeof( DeviceAddress ) * 3 )
#define TEMPERATURE_PRECISION                11
#define TEMPERATURE_MIN_SAMPLING_PERIOD_MS   15000

#define HEATPUMP_FLOW_THERM   "HP-FLOW"
#define HEATPUMP_RETURN_THERM "HP-RETURN"
#define HEATING_FLOW_THERM    "UFH-FLOW"
#define HEATING_RETURN_THERM  "UFH-RETURN"
#define OUTSIDE_THERM         "OUTSIDE"
#define TEMPERATURE_INVALID   -100

class TemperatureModule
{
   typedef struct {
      DeviceAddress  m_address;                       // 64 bit address, array of 8 uint8_t
      char           m_name[ MAX_TEMP_NAME + 1 ];     // Friendly name
      uint8_t        m_busIndex;                      // index of the sensor on OneWire bus
      uint32_t       m_emonFeedId;                    // Feed ID for emonCMS
      float_t        m_temp;                          // temperature (adjusted by calibration
      float_t        m_calibrationOffset;             // calibration offset
      bool           m_isValid;                       // true if registered ok
      char           m_addressStr[ 17 ];              // string for the address - 8 hex chars + null
   } Sensor;

   public:
      TemperatureModule();
      ~TemperatureModule();
      void  initialise();
      bool  getTemperature( char *name, float *temp );

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
