#ifndef GRUNDFOS_UPS3_H
#define GRUNDFOS_UPS3_H

#include "utils.h"

class GrundfosUPS3
{
public:
   GrundfosUPS3( uint8_t pwmGPIO );
   ~GrundfosUPS3();
   void  initialise();
   void  test();
   void  test2();

private:
   uint8_t        m_pwmGPIO;
};

#endif
