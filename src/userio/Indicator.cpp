/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#include "src/config/Config.h"

#include "src/userio/Indicator.h"

#if defined(TVMG_OLED)
   #include "src/userio/OledIndicator.h"
   #define IndicatorClass   OledIndicator
#elif defined(TVMG_WAVESHARE_LCDB)   
   #include "src/userio/LCDIndicator.h"
   #define IndicatorClass   LCDIndicator
#elif defined(TVMG_WAVESHARE_RELAY)
   #define IndicatorClass   RelayIndicator
   #include "src/userio/RelayIndicator.h"
#endif


// map of keys to Indicator pointers
std::map<uint32_t, std::unique_ptr<Indicator>> Indicator::s_map;

Indicator::Indicator(IndicatorType type, uint32_t id)
    : m_type(type),
      m_id(id)
{
}

Indicator::~Indicator()
{
}

void Indicator::initialise()
{
   s_map.clear();
}

#if defined(TVMG_WAVESHARE_LCDB) || defined(TVMG_OLED) || defined(TVMG_WAVESHARE_RELAY)
Indicator* Indicator::getIndicator(IndicatorType type, uint32_t id)
{
   uint32_t key = generateKey(type, id);
   auto it = s_map.find(key);

   if (it == s_map.end())
   {
      TVMG_DEBUG( "Creating indicator with id %u",id );
      // try_emplace is more efficient than s_map[key]
      // because it doesn't require the value type to be default-constructible
      auto [new_it, success] = s_map.try_emplace(key, std::make_unique<IndicatorClass>(type, id));

      if (!success)
      {
         TVMG_ERROR( "Failed to create indicator with id %u",id );
         return nullptr;
      }
      it = new_it;
   }

   return it->second.get();
}
#else
Indicator* Indicator::getIndicator(IndicatorType type, uint32_t id)
{
   (void) type;
   (void) id;

   return nullptr;
}
#endif

void Indicator::on()
{
   doOn();
}

void Indicator::off()
{
   doOff();
}

void Indicator::heartbeat(uint32_t onMillis, uint32_t offMillis)
{
   doHeartbeat(onMillis, offMillis);
}

uint32_t Indicator::generateKey(IndicatorType type, uint32_t id)
{
   if (id > 0xFFFFFF)
   {
      TVMG_ERROR("Invalid Indicator id - truncating");
      id &= 0xFFFFFF;
   }

   return (static_cast<uint32_t>(type) << 24) | id;
}
