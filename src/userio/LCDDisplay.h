#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#if defined(TVMG_WAVESHARE_LCDB)

#include <cstdint>

#include "Display.h"

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
