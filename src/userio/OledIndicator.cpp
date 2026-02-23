#ifdef TVMG_OLED

#include "OledIndicator.h"
#include "src/core/utils.h"

#include "OledDisplay.h"

static OledDisplay *s_display = nullptr;

OledIndicator::OledIndicator( IndicatorType type, uint32_t id )
   : Indicator( type, id ),
     m_led( -1 )
{
   s_display = OledDisplay::getInstance();

   switch( type )
   {
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
   PW_MSG( "OledIndicator constructed type=%d id=%u", type, id );
}

OledIndicator::~OledIndicator()
{
   PW_MSG( "OledIndicator destroyed" );
}

void OledIndicator::setIndicator( bool on )
{
   if ( !s_display )
   {
      return;
   }

   uint8_t row = 2;
   uint8_t col = m_led;

   if ( m_led > 2 )
   {
      row = 4;
      col -= 3;
   }
   
   col = (col * 2) + 1;

   s_display->setIndicator( col,row,on );
}

void OledIndicator::doOn()
{
   setIndicator( true );
}

void OledIndicator::doOff()
{
   setIndicator( false );
}

void OledIndicator::doHeartbeat( uint32_t onMillis, uint32_t offMillis )
{
   (void) onMillis;
   (void) offMillis;
}

#endif // TVMG_OLED
