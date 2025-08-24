#ifndef GRUNDFOS_UPS3_H
#define GRUNDFOS_UPS3_H

#include "src/core/utils.h"

class GrundfosUPS3
{
public:
   enum  Mode {
      CONSTANT_SPEED1,
      CONSTANT_SPEED2,
      CONSTANT_SPEED3,
      CONSTANT_PRESSURE1,
      CONSTANT_PRESSURE2,
      PROPORTIONAL_PRESSURE1,
      PROPORTIONAL_PRESSURE2,
   };

   GrundfosUPS3( uint8_t pwmGPIO,const char *mode );
   ~GrundfosUPS3();
   void  initialise();
   void  sample();
   float_t  getFlowRate();
   String getMode();
   float_t  getPowerConsumed();

private:
   uint8_t  m_pwmGPIO;
   float_t  m_power;
   uint8_t  m_quality;
   Mode     m_mode;
   String   m_modeString;
};

#endif
