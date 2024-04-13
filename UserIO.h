#ifndef USERIO_H
#define USERIO_H

#include "utils.h"

#include "Measurement.h"
#include "Networking.h"

class U8G2_SSD1306_128X64_NONAME_F_HW_I2C;

#define  MAX_OLED_ROWS     6
#define  MAX_OLED_COLUMNS  24

class UserIO
{
public:
   typedef char  OLEDDisplayLine[ MAX_OLED_COLUMNS + 1 ];

   enum  ScreenType {
      NETWORK_STATUS,
      STORAGE_STATUS,
      ENERGY,
      TEMPERATURES,
      NONE
   };

   UserIO();
   ~UserIO();
   void  initialise();
   void  setMeasurement( Measurement *measurement );
   void  setNetworking( Networking *network );

   void  updateLine( uint8_t lineNum,char *line,bool isForLog = true );
   void  clear();
   void  showNext();
   void  show( ScreenType type );
   void  update();
   void  setFirmwareUpdateInProgress( bool inProgress );
   bool  isFirmwareUpdateInProgress();

private:
   void  show( OLEDDisplayLine lines[] );
   void  storeLine( uint8_t lineNum,char *line );
   void  showNetwork();
   void  showStorage();
   void  showEnergy();
   void  showTemps();
   TempSensor *findTempSensor( uint8_t id );
   bool  isPowerDataAvailable();


   U8G2_SSD1306_128X64_NONAME_F_HW_I2C *m_display;
   ScreenType           m_currentScreen;
   OLEDDisplayLine      m_currentLines[ MAX_OLED_ROWS ];
   Measurement          *m_measurement;
   Networking           *m_networking;
   Measurement::Sample  m_sample;
   bool                 m_firmwareUpdateInProgress;
   time_t               m_startTime;
};

#endif
