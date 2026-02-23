#if !defined(TVMG_WAVESHARE_LCDB) && !defined(TVMG_OLED)

#include "src/core/utils.h"

#include "DummyDisplay.h"

DummyDisplay::DummyDisplay() : Display()
{
}

DummyDisplay::~DummyDisplay()
{
}

void  DummyDisplay::updateLine( uint8_t lineNum,const char *line,bool isForLog )
{
   (void) lineNum;
   PW_DEBUG( line );
}

#endif

