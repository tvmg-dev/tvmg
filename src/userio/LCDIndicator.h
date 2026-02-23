#ifndef LCD_INDICATOR_H
#define LCD_INDICATOR_H

#include "Indicator.h"

class LCDIndicator : public Indicator
{
public:
   LCDIndicator( IndicatorType type, uint32_t id );
   ~LCDIndicator() override;

protected:
   void doOn() override;
   void doOff() override;
   void doHeartbeat( uint32_t onMillis, uint32_t offMillis ) override;

private:
   int8_t m_led;
};

#endif // LCD_INDICATOR_H
