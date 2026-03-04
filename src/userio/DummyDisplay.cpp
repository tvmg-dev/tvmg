#if !defined(TVMG_WAVESHARE_LCDB) && !defined(TVMG_OLED)

#include "src/config/Config.h"

#include "src/userio/DummyDisplay.h"

DummyDisplay::DummyDisplay() : Display()
{
}

DummyDisplay::~DummyDisplay()
{
}

void  DummyDisplay::updateLine( uint8_t lineNum,const char *line,bool isForLog )
{
   (void) lineNum;
   TVMG_DEBUG( line );
}

#endif

