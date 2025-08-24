#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "Display.h"

class LcdDisplay : public Display
{
public:
   LcdDisplay();
   ~LcdDisplay();
   void  initialise() override;
   void  show( DisplayLine lines[] ) override;
};

#endif
