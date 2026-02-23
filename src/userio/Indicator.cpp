#include "Indicator.h"

#if defined(TVMG_OLED)
   #include "OledIndicator.h"
   #define IndicatorClass   OledIndicator
#elif defined(TVMG_WAVESHARE_LCDB)   
   #include "LCDIndicator.h"
   #define IndicatorClass   LCDIndicator
#endif

#include "src/core/utils.h"  // for PW_ERROR etc

// static member definition
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

#if defined(TVMG_WAVESHARE_LCDB) || defined(TVMG_OLED)
Indicator* Indicator::getIndicator(IndicatorType type, uint32_t id)
{
   uint32_t key = generateKey(type, id);
   auto it = s_map.find(key);

   if (it == s_map.end())
   {
      PW_DEBUG( "Creating indicator with id %u",id );
      // try_emplace is more efficient than s_map[key]
      // because it doesn't require the value type to be default-constructible
      auto [new_it, success] = s_map.try_emplace(key, std::make_unique<IndicatorClass>(type, id));

      if (!success)
      {
         PW_ERROR( "Failed to create indicator with id %u",id );
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
      PW_ERROR("Invalid Indicator id - truncating");
      id &= 0xFFFFFF;
   }

   return (static_cast<uint32_t>(type) << 24) | id;
}
