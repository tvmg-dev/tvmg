/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef SHELLY_POWER_MODULE_H
#define SHELLY_POWER_MODULE_H

#include "src/config/Config.h"

#include "src/userio/Indicator.h"

#define SHELLY_SENSOR_NAME "SHELLYPM"

#define MAX_SHELLY_SENSORS 3
#define INVALID_EM_METER   10

typedef struct {
   uint8_t  m_id;          // should be unique ID
   uint32_t m_emonFeedId;  // Feed ID for emonCMS
   float_t  m_power;       // power
   float_t  m_energy;      // energy
} ShellyPowerSensor;

class ShellyPowerModule
{
public:
   ShellyPowerModule();
   ~ShellyPowerModule();
   void initialise();
   void sample();
   ShellyPowerSensor *readNextSensor( uint8_t index );
   bool getPower( uint8_t index );

private:
   enum ShellyModel { UNKNOWN,PMG3,EM,EMG3 };

   typedef struct {
      ShellyPowerSensor m_data;        // sensor essentials
      ShellyModel       m_model;       // model type
      IPAddress         m_ipAddress;   // IP address of the sensor
      uint8_t           m_meter;       // may be 0 or 1 for EM meter
      bool              m_isValid;
   } PrivateSensor;

   cJSON *getData( const String &query );
   bool getEM( PrivateSensor *sensor );
   bool getEMG3( PrivateSensor *sensor );
   bool getPMG3( PrivateSensor *sensor );

   PrivateSensor  m_sensors[ MAX_SHELLY_SENSORS ];
   uint8_t        m_numSensors;
   int32_t        m_millisLastAquisition;       // milliseconds since last acquisition
   Indicator      *m_indicator;
};

#endif
