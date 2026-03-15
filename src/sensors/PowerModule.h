/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef POWER_MODULE_H
#define POWER_MODULE_H

#include <ModbusMaster.h>

#include "src/config/Config.h"

#include "src/userio/Indicator.h"

#define POWER_SENSOR_NAME "POWER"

#define MAX_POWER_SENSORS  3

#define POWER_INVALID         -1
#define ENERGY_INVALID        -1

// The ID's should be matched in the sensors file

#define  HEAT_PUMP_ID         100
#define  HEATING_CIRCUITS_ID  101

typedef struct {
   uint8_t  m_id;          // should be unique ID
   uint32_t m_emonFeedId;  // Feed ID for emonCMS
   float_t  m_power;       // power
   float_t  m_energy;      // energy
} PowerSensor;

class PowerModule
{
public:
   PowerModule( ModbusMaster *modbus );
   ~PowerModule();
   void initialise();
   ModbusMaster *getModbus();
   void sample();
   PowerSensor *readNextSensor( uint8_t index );
   bool getPower( uint8_t index );

private:
   typedef struct {
      PowerSensor m_data;  // sensor essentials
      uint8_t  m_address;  // address on modbus
      bool     m_isValid;
   } PrivateSensor;

   ModbusMaster   *m_modbus;
   PrivateSensor  m_sensors[ MAX_POWER_SENSORS ];
   uint8_t        m_numSensors;
   int32_t        m_millisLastAquisition;       // milliseconds since last acquisition
   Indicator      *m_indicator;
};

#endif
