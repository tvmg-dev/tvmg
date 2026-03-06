/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#define  MAX_DISPLAY_ROWS     6
#define  MAX_DISPLAY_COLUMNS  24

class Measurement;
class Networking;
class LGHeatPump;
class HeatMeterModule;
class ModbusMaster;

typedef char  DisplayLine[ MAX_DISPLAY_COLUMNS + 1 ];

class Display
{
public:
   enum  ScreenType {
      NETWORK_STATUS,
      STORAGE_STATUS,
      COMMS_STATUS,
      ENERGY,
      TEMPERATURES,
      HEAT_METERS,
      LG_STATUS,
      OTA_UPDATE,
      NONE
   };

   Display() {};
   virtual ~Display() {};
   virtual void  initialise();

   virtual void  setMeasurement( Measurement *measurement ) = 0;
   virtual void  setNetworking( Networking *network ) = 0;
   virtual void  setLGHeatPump( LGHeatPump *heatpump ) = 0;
   virtual void  setHeatMeter( HeatMeterModule *heatMeter ) = 0;
   virtual void  setModBus( ModbusMaster *modbus ) = 0;

   virtual void  updateLine( uint8_t lineNum,const char *line,bool isForLog = true ) = 0;
   virtual void  clear() = 0;
   virtual void  show( ScreenType type ) = 0;
};

#endif
