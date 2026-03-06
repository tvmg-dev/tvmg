/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#if defined(TVMG_WAVESHARE_LCDB)

#include <cstdint>

#include "src/userio/Display.h"

class LcdDisplay : public Display
{
public:
   LcdDisplay();
   ~LcdDisplay();
   void  initialise() override;
   static LcdDisplay *getInstance();

   void  setMeasurement( Measurement *measurement ) override { (void) measurement; }
   void  setNetworking( Networking *network ) override { (void) network; }
   void  setLGHeatPump( LGHeatPump *heatpump ) override { (void) heatpump; }
   void  setHeatMeter( HeatMeterModule *heatMeter ) override { (void) heatMeter; }
   void  setModBus( ModbusMaster *modbus ) override { (void) modbus; }

   void  updateLine( uint8_t lineNum,const char *line,bool isForLog ) override;
   void  clear() override {}
   void  show( ScreenType type ) override { (void) type; }

   void setLed(int led, bool on);
   void startLedHeartbeat(int led, uint32_t onMillis, uint32_t offMillis);
   void setLedColour(int led, uint32_t rgb888);
};

#endif // TVMG_WAVESHARE_LCDB
#endif // LCD_DISPLAY_H
