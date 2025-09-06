#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "Display.h"

class U8G2_SSD1306_128X64_NONAME_F_HW_I2C;

class OledDisplay : public Display
{
public:
   OledDisplay();
   ~OledDisplay();
   void  initialise() override;
   void  show( DisplayLine lines[] ) override;
   void  updateScreensaver() override;


private:
   U8G2_SSD1306_128X64_NONAME_F_HW_I2C *m_oled;
   int m_saverX;
   int m_saverY;
};

#endif
