/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#include <esp_timer.h>

#include "src/config/Config.h"

#include "src/userio/Display.h"

static Display  *s_display = nullptr;

static void  heartbeatCallback( void *params )
{
   static uint32_t pendingPeriodEnd = millis() + (5 * 60 * 1000);

   Display *display = static_cast<Display *>(params);

   if ( !display )
   {
      return;
   }

//   TVMG_DEBUG( "Display::heartbeatCallback" );

   Display::ScreenSaverMode mode = display->getScreenSaverMode();
   
   if ( mode == Display::Pending && millis() > pendingPeriodEnd )
   {
      TVMG_DEBUG( "Enabling screen saver mode" );
      mode = Display::Enabled;
      display->setScreenSaverMode( mode );
   }

   display->doUpdate();
}

Display::Display( uint32_t heartbeatMS )
       : m_ssMode( ScreenSaverMode::Disabled ),
         m_heartbeatMS( heartbeatMS )

{
   s_display = this;
}

const char *Display::screenToString( ScreenType type )
{
   switch (type)
   {
      case NETWORK_STATUS:
         return "Network";
      case STORAGE_STATUS:
         return "Storage";
      case COMMS_STATUS:
         return "Comms";
      case ENERGY:
         return "Energy";
      case TEMPERATURES:
         return "Temperatures";
      case HEAT_METERS:
         return "Heat Meters";
      case LG_STATUS:
         return "LG Status";
      case OTA_UPDATE:
         return "OTA Update";
      case NONE:
         return "None";
      default:
         return "Unknown";
   }
}

void  Display::startHeartbeat()
{
   int regSS = GET_REGISTRY_INT( USERIO_SCREENSAVER );
   
   switch( regSS )
   {
      case 0 : m_ssMode = Disabled;
               break;
      case 1 : m_ssMode = Enabled;
               break;
      default : m_ssMode = Pending;
               break;
   }

   if ( m_heartbeatMS )
   {
      esp_timer_handle_t displayTimer;

      const esp_timer_create_args_t args = {
         .callback = &heartbeatCallback,
         .arg = this,
         .name = "DisplayUpdate" };

      if ( esp_timer_create( &args,&displayTimer ) != ESP_OK )
      {
         TVMG_ERROR( "Failed to start Display callback timer" );
      }
      else
      {
         esp_timer_start_periodic( displayTimer,m_heartbeatMS * 1000 );
      }
   }
}

void Display::doUpdate()
{
   TVMG_DEBUG( "Display::doUpdate" );
}

void Display::setScreenSaverMode( ScreenSaverMode mode )
{
   // We're not persisting the screensaver mode, we don't therefore actually
   // need to use the registry at all (here or startHeartbeat) but we include it in
   // case we want to persist state at some later checkin.

   if ( s_display && mode != s_display->m_ssMode )
   {
      SET_REGISTRY_INT_VOLATILE( USERIO_SCREENSAVER,mode );
      s_display->m_ssMode = mode;
   }
}

Display::ScreenSaverMode Display::getScreenSaverMode()
{
   if ( s_display )
   {
      return s_display->m_ssMode;
   }
   
   return Disabled;
}
