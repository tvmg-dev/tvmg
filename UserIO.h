#ifndef USERIO_H
#define USERIO_H

#include "utils.h"

#include "Display.h"

#include "Measurement.h"
#include "Networking.h"
#include "LGHeatPump.h"
#include "HeatMeter.h"

class ModbusMaster;

class UserIO
{
public:
   enum  ScreenType {
      NETWORK_STATUS,
      STORAGE_STATUS,
      ENERGY,
      TEMPERATURES,
      HEAT_METERS,
      COMMS_STATUS,
      LG_STATUS,
      NONE
   };

   UserIO();
   ~UserIO();
   void  initialise();
   void  setMeasurement( Measurement *measurement );
   void  setNetworking( Networking *network );
   void  setLGHeatPump( LGHeatPump *heatpump );
   void  setHeatMeter( HeatMeterModule *heatMeter );
   void  setModBus( ModbusMaster *modbus );

   void  updateLine( uint8_t lineNum,char *line,bool isForLog = true );
   void  clear();
   void  showNext();
   void  show( ScreenType type );
   void  refresh();
   void  update();
   void  storeLine( uint8_t lineNum,char *line );

private:
   void  show( DisplayLine lines[] );
   bool  setNextScreen();
   void  showNetwork();
   void  showStorage();
   void  showEnergy();
   void  showTemps();
   void  showHeatMeter();
   void  showCommsStatus();
   void  showLGStatus();

   TempSensor *findTempSensor( uint8_t id );
   bool  isTemperatureDataAvailable();
   bool  isPowerDataAvailable();
   bool  isHeatMeterDataAvailable();

   Display              *m_display;
   ScreenType           m_currentScreen;
   DisplayLine          m_currentLines[ MAX_DISPLAY_ROWS ];
   Measurement          *m_measurement;
   Networking           *m_networking;
   LGHeatPump           *m_heatPump;
   HeatMeterModule      *m_heatMeter;
   ModbusMaster         *m_modbus;
   Measurement::Sample  m_sample;
   time_t               m_startTime;
};

#endif
