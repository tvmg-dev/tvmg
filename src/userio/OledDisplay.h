#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#ifdef TVMG_OLED

#include "Display.h"

#include "src/core/Measurement.h"

#define OLED_BOARD U8G2_SSD1306_128X64_NONAME_F_HW_I2C

class OLED_BOARD;

class OledDisplay : public Display
{
public:
   OledDisplay();
   ~OledDisplay();
   void  initialise() override;
   static OledDisplay *getInstance();

   void  setMeasurement( Measurement *measurement ) override;
   void  setNetworking( Networking *network ) override;
   void  setLGHeatPump( LGHeatPump *heatpump ) override;
   void  setHeatMeter( HeatMeterModule *heatMeter ) override;
   void  setModBus( ModbusMaster *modbus ) override;

   void  updateLine( uint8_t lineNum,const char *line,bool isForLog = true ) override;
   void  clear() override;;
   void  show( ScreenType type ) override;;

   void  setIndicator(uint8_t column, uint8_t row, bool state);
   void  showNext();
   void  refresh();
   void  updateScreensaver() ;

private:
   void  show( DisplayLine lines[] );
   void  storeLine( uint8_t lineNum,const char *line );
   bool  setNextScreen();
   void  showNetwork();
   void  showStorage();
   void  showEnergy();
   void  showTemps();
   void  showHeatMeter();
   void  showCommsStatus();
   void  showLGStatus();
   bool  getTemperature( uint8_t id,float *temp );
   void  getLGValue( uint32_t parameter,float_t *value );

   OLED_BOARD *         m_oled;
   bool                 m_isScreenSaving;
   ScreenType           m_currentScreen;
   DisplayLine          m_currentLines[ MAX_DISPLAY_ROWS ];
   Measurement *        m_measurement;
   Networking *         m_networking;
   LGHeatPump *         m_heatPump;
   HeatMeterModule *    m_heatMeter;
   ModbusMaster *       m_modbus;
   Measurement::Sample  m_sample;
   time_t               m_startTime;

};

#endif  // TVMG_OLED
#endif  // OLED_DISPLAY_H
