#ifndef HEAT_METER_H
#define HEAT_METER_H

#include "utils.h"
#include "TemperatureModule.h"

#include "GrundfosUPS3.h"

#define MAX_HEAT_METERS 5
#define MAX_HM_NAME     32

#define  FLOW_RATE_ERROR   100
#define  HM_POWER_ERROR    -1

#define  GROUND_HM         60
#define  FIRST_HM          70
#define  SECOND_HM         80

typedef struct {
   uint8_t  m_id;          // should be unique ID
   char    *m_name;        // name (don't store the name here to keep the structure size to minimum)
   uint32_t m_emonPowerId; // Feed ID for emonCMS power
   uint32_t m_emonFlowId;  // Feed ID for emonCMS flow rate
   float_t  m_power;       // power
   float_t  m_flowRate;    // flow rate
   float_t  m_flowTemp;
   float_t  m_returnTemp;
} HeatMeterSensor;

class HeatMeter
{
public:
   HeatMeter( GrundfosUPS3 *pump,TemperatureModule *tempModule,const String &name,uint8_t id,
                           uint32_t emonFlowId, uint32_t emonPowerId, uint8_t flowTempId,
                           uint8_t returnTempId, float_t shc );
   ~HeatMeter();
   void  initialise();
   void  takeMeasurement();
   HeatMeterSensor   *getHeatMeterSensor();

private:
   GrundfosUPS3      *m_flowMeter;
   TemperatureModule *m_tempModule;
   HeatMeterSensor    m_sensor;
   char        m_name[ MAX_HM_NAME + 1 ];
   uint8_t     m_flowTempId;
   uint8_t     m_returnTempId;
   float_t     m_shc;
};

class HeatMeterModule
{
public:
   HeatMeterModule( TemperatureModule *tempModule );
   ~HeatMeterModule();
   void  initialise();
   HeatMeterSensor  *readNextSensor( uint8_t index );

private:
   TemperatureModule *m_tempModule;
   bool         m_isOk;
   HeatMeter   *m_sensors[ MAX_HEAT_METERS ];
   uint8_t      m_numLocalSensors;

   int32_t      m_millisLastAquisition;       // milliseconds since last acquisition
};


#endif
