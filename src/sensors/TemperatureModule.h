#ifndef TEMPERATURE_MODULE_H
#define TEMPERATURE_MODULE_H

#include <Arduino.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include "src/userio/Indicator.h"

class AsyncUDP;

//---------------------------------------------------------------------
// Temperature monitoring

#define TEMPERATURE_SENSOR_NAME  "THERM"        // for JSON name

#define MAX_TEMP_SENSORS      13

#define TEMPERATURE_INVALID   -100

// The ID's should be matched in the sensors.dat file

#define  HEAT_PUMP_FLOW    1
#define  HEAT_PUMP_RETURN  2
#define  HEATING_FLOW      5
#define  HEATING_RETURN    6
#define  OUTSIDE           10
#define  GND_FLOW          20
#define  GND_RETURN        21
#define  FIRST_FLOW        30
#define  FIRST_RETURN      31
#define  LOFT_FLOW         40
#define  LOFT_RETURN       41
#define  LG_OUTLET         50
#define  LG_INLET          51

typedef struct {
   uint8_t  m_id;          // should be unique ID
   uint32_t m_emonFeedId;  // Feed ID for emonCMS
   float_t  m_temp;        // temperature
} TempSensor;

class TemperatureModule
{
public:
   TemperatureModule();
   ~TemperatureModule();
   void  initialise();
   void  sample();
   TempSensor  *readNextSensor( uint8_t index );
   float_t     getTemperature( uint8_t tempId );

private:

   typedef struct {
      DeviceAddress  m_address;                       // 64 bit address, array of 8 uint8_t
      uint8_t        m_busIndex;                      // index of the sensor on OneWire bus
      float_t        m_calibrationOffset;             // calibration offset
      char           m_addressStr[ 17 ];              // string for the address - 8 hex chars + null
   } DS1820BSensor;

   typedef struct {
      char  *m_url;
   } OpenWeatherSensor;

   typedef struct {
      TempSensor        m_data;        // sensor essentials
      bool              m_isValid;     // true if registered ok
      bool              m_isDs18b20;   // true if local/remote DS18B20
      bool              m_isRemote;    // true if remote
      union {
         DS1820BSensor     m_ds18b20;     // for local/remote ds18b20
         OpenWeatherSensor m_openWeather; // for open weather API
      };
   } PrivateSensor;

   bool  getTemperatures();
   void  getAddressString( DeviceAddress addr,char *addrString );
   void  addUDPListener();
   void  localBroadcastData();
   float fetchOpenWeather( const String &url );

   OneWire           *m_oneWireController;
   DallasTemperature *m_dallasController;
   AsyncUDP          *m_udp;

   PrivateSensor     m_sensors[ MAX_TEMP_SENSORS ];
   uint8_t           m_numSensors;
   bool              m_haveRemoteSensors;
   uint16_t          m_sendPort;

   int32_t           m_millisLastAquisition;       // milliseconds since last acquisition
   Indicator         *m_indicator;   
};

#endif
