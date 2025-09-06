#if !PW_LCD

#include <U8g2lib.h>

#include "src/config/hwconfig.h"
#include "src/core/utils.h"

#include "OledDisplay.h"

OledDisplay::OledDisplay() : Display(),
             m_oled( nullptr ),
             m_saverX( 0 ),
             m_saverY( 0 )
{
   m_oled = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C( U8G2_R0,U8X8_PIN_NONE,hwConfig->OLEDClkGPIO,hwConfig->OLEDDataGPIO );
}

OledDisplay::~OledDisplay()
{
   delete m_oled;
}

void OledDisplay::initialise()
{
   if ( m_oled )
   {
      m_oled->begin();

      m_oled->setFont(u8g2_font_6x10_tf);
      m_oled->setFontRefHeightExtendedText();
      m_oled->setDrawColor(1);
      m_oled->setFontPosTop();
      m_oled->setFontDirection(0);
   }
}

void OledDisplay::show( DisplayLine lines[] )
{
   m_oled->clearBuffer();

   for ( int row = 0; row < MAX_DISPLAY_ROWS; row++ )
   {
      m_oled->drawStr( 0,row * 10, lines[ row ] );
   }

   m_oled->sendBuffer();
}

void OledDisplay::updateScreensaver()
{
   m_saverX = (m_saverX + 1) % MAX_DISPLAY_COLUMNS;
   m_saverY = (m_saverY + 1) % MAX_DISPLAY_ROWS;
   m_oled->clearBuffer();
   m_oled->drawStr( m_saverX * 5,m_saverY * 10,"O" );
   m_oled->sendBuffer();
}

#endif

