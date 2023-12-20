#ifndef USERIO_H
#define USERIO_H

#include "utils.h"

#include "Measurement.h"

class U8G2_SSD1306_128X64_NONAME_F_HW_I2C;

#define  MAX_OLED_ROWS     6
#define  MAX_OLED_COLUMNS  24

class UserIO
{
public:
   typedef char  OLEDDisplayLine[ MAX_OLED_COLUMNS + 1 ];

   enum  ScreenType {
      TEMPERATURES,
      ENERGY,
      STATUS,
      NONE
   };

   UserIO();
   ~UserIO();
   void  initialise( void );
   void  setMeasurement( Measurement *measurement );
   void  updateLine( uint8_t lineNum,char *line,bool isForLog = true );
   void  clear( void );
   void  showNext( void );
   void  update( void );

private:
   void  show( OLEDDisplayLine lines[] );
   void  show( ScreenType type );

   U8G2_SSD1306_128X64_NONAME_F_HW_I2C *m_display;
   ScreenType           m_currentScreen;
   OLEDDisplayLine      m_currentLines[ MAX_OLED_ROWS ];
   Measurement          *m_measurement;
   Measurement::Sample  m_sample;
};

#endif
