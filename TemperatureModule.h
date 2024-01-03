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

// The ID's should be matched in the sensors.dat file

#define  HEAT_PUMP_FLOW    1
#define  HEAT_PUMP_RETURN  2
#define  UFH_FLOW          5
#define  UFH_RETURN        6
#define  OUTSIDE           10
#define  GND_FLOW          20
#define  GND_RETURN        21
#define  FIRST_FLOW        30
#define  FIRST_RETURN      31
#define  LOFT_FLOW         40
#define  LOFT_RETURN       41

typedef struct {
   uint8_t  m_id;          // should be unique ID
   uint32_t m_emonFeedId;  // Feed ID for emonCMS
   float_t  m_temp;        // temperature
   char    *m_name;        // name (don't store the name here to keep the structure size to minimum)
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
      bool           m_isRemote;                      // true if remote
      char           m_addressStr[ 17 ];              // string for the address - 8 hex chars + null
   } PrivateSensor;

   bool  getTemperatures();
   void  getAddressString( DeviceAddress addr,char *addrString );
   void  localBroadcastData();

   OneWire           *m_oneWireController;
   DallasTemperature *m_dallasController;

   bool              m_isOk;
   PrivateSensor     m_sensors[ MAX_TEMP_SENSORS ];
   uint8_t           m_numSensors;

   int32_t           m_millisLastAquisition;       // milliseconds since last acquisition
};

#endif
