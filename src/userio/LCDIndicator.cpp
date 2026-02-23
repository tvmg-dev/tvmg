#if defined(TVMG_WAVESHARE_LCDB)

#include "LCDIndicator.h"
#include "src/core/utils.h"

#include "LCDDisplay.h"

static LcdDisplay *s_display = nullptr;

LCDIndicator::LCDIndicator( IndicatorType type, uint32_t id )
   : Indicator( type, id ),
     m_led( -1 )
{
   if ( !s_display )
   {
      s_display = LcdDisplay::getInstance();
   }

   switch( type )
   {
      case SYSTEM:
         m_led = id;
         break;
      case NETWORK:
         m_led = 3;
         break;
      case THERM:
         m_led = 4;
         break;
      case POWER:
         m_led = 5;
         break;
      case HEATPUMP:
         m_led = 6;
         break;
      case HEATMETER:
         m_led = 7;
         break;
      default:
         m_led = 0;
         break;
   }
   PW_MSG( "LCDIndicator constructed type=%d id=%u", type, id );
}

LCDIndicator::~LCDIndicator()
{
   PW_MSG( "LCDIndicator destroyed" );
}

void LCDIndicator::doOn()
{
   s_display->setLed( m_led,true );
}

void LCDIndicator::doOff()
{
   s_display->setLed( m_led,false );
}

void LCDIndicator::doHeartbeat( uint32_t onMillis, uint32_t offMillis )
{
   s_display->startLedHeartbeat( m_led,onMillis,offMillis );
}

#endif // TVMG_WAVESHARE_LCDB