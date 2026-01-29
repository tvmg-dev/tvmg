#ifndef TVMG_OLED

#include "src/core/utils.h"

#include "LCDDisplay.h"

LcdDisplay::LcdDisplay() : Display()
{
}

LcdDisplay::~LcdDisplay()
{
}

void LcdDisplay::initialise()
{
   PW_MSG( "initialise LCD display" );
}

void LcdDisplay::show( DisplayLine lines[] )
{
   for ( int row = 0; row < MAX_DISPLAY_ROWS; row++ )
   {
      if ( strlen( lines[ row ] ) )
      {
         PW_MSG( "%d : %s",row + 1,lines[ row ] );
      }
   }
}

void  LcdDisplay::updateScreensaver()
{
}

#endif

