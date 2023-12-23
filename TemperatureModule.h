#ifndef TEMPERATURE_MODULE_H
#define TEMPERATURE_MODULE_H

#include <Arduino.h>

#include <OneWire.h>
#include <DallasTemperature.h>

//---------------------------------------------------------------------
// Temperature monitoring

#define MAX_TEMP_SENSORS      11
#define MAX_TEMP_NAME         32

#define TEMPERATURE_INVALID   -100

typedef struct {
   uint8_t  m_id;          // should be unique ID
   uint32_t m_emonFeedId;  // Feed ID for emonCMS
   float_t  m_temp;        // temperature
   char    *m_name;        // name (don't store the name here to keep the structure size to minimum
} TempSensor;

class TemperatureModule
{
public:
   TemperatureModule();
   ~TemperatureModule();
   void  initialise();
   TempSensor  *readNextSensor( uint8_t index );

private:
   typedef struct {
      TempSensor     m_sensor;                        // sensor essentials
      char           m_name[ MAX_TEMP_NAME + 1 ];     // Friendly name
      DeviceAddress  m_address;                       // 64 bit address, array of 8 uint8_t
      uint8_t        m_busIndex;                      // index of the sensor on OneWire bus
      float_t        m_calibrationOffset;             // calibration offset
      bool           m_isValid;                       // true if registered ok
      char           m_addressStr[ 17 ];              // string for the address - 8 hex chars + null
   } PrivateSensor;

   bool  getTemperatures( void );
   void  getAddressString( DeviceAddress addr,char *addrString );

   OneWire           *m_oneWireController;
   DallasTemperature *m_dallasController;

   bool              m_isOk;
   PrivateSensor     m_sensors[ MAX_TEMP_SENSORS ];
   uint8_t           m_numSensors;

   int32_t           m_millisLastAquisition;       // milliseconds since last acquisition
};

#endif
