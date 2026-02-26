#if defined(TVMG_WAVESHARE_RELAY)

#include "RelayIndicator.h"

#include "src/core/utils.h"

// Only have 2 possible available LED's
//
// The Relay LED can be enabled but this also triggers the relay which I believe is a mechanical 
// device given the noise on active, so can't be used excessively.  AP mode.
// The modbus TxD GPIO can be used in early boot phase only, after that then modbus
// must have control

#define RELAY_GPIO      47
#define MODBUS_TX_GPIO  17

RelayIndicator::RelayIndicator( IndicatorType type, uint32_t id )
   : Indicator( type, id ),
     m_led( -1 ),
     m_gpio( -1 )
{
   switch( type )
   {
      case SYSTEM:
         m_led = id;
         if ( id == SYSTEM_AP_ID )
         {
            m_gpio = RELAY_GPIO;
         }
         break;
      default:
         break;
   }

   PW_MSG( "RelayIndicator constructed type=%d id=%u", type, id );
}

RelayIndicator::~RelayIndicator()
{
   PW_MSG( "RelayIndicator destroyed" );
   doOff();
}

void RelayIndicator::doOn()
{
   if ( m_gpio !=-1 )
   {
      PW_DEBUG( "Relay board GPIO %d on",m_gpio );
      pinMode( m_gpio,OUTPUT );
      digitalWrite( m_gpio,HIGH );
      delay( 10 );
   }
}

void RelayIndicator::doOff()
{
   if ( m_gpio !=-1 )
   {
      PW_DEBUG( "Relay board GPIO %d off",m_gpio );
      pinMode( m_gpio,OUTPUT );
      digitalWrite( m_gpio,LOW );
      delay( 10 );
   }
}

void RelayIndicator::doHeartbeat( uint32_t onMillis, uint32_t offMillis )
{
   (void) onMillis;
   (void) offMillis;
}

#endif // TVMG_WAVESHARE_RELAY