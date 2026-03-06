/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef DUMMY_DISPLAY_H
#define DUMMY_DISPLAY_H

#if !defined(TVMG_OLED) && !defined(TVMG_WAVESHARE_LCDB)

#include "src/userio/Display.h"

class DummyDisplay : public Display
{
public:
   DummyDisplay();
   ~DummyDisplay();
   void  initialise() {};
   
   void  setMeasurement( Measurement *measurement ) override { (void) measurement; }
   void  setNetworking( Networking *network ) override { (void) network; }
   void  setLGHeatPump( LGHeatPump *heatpump ) override { (void) heatpump; }
   void  setHeatMeter( HeatMeterModule *heatMeter ) override { (void) heatMeter; }
   void  setModBus( ModbusMaster *modbus ) override { (void) modbus; }

   void  updateLine( uint8_t lineNum,const char *line,bool isForLog = true ) override;
   void  clear() override {}
   void  show( ScreenType type ) override { (void) type; }
};

#endif // !defined(TVMG_OLED) && !defined(TVMG_WAVESHARE_LCDB)
#endif // DUMMY_DISPLAY_H
