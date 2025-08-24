#ifndef DISPLAY_H
#define DISPLAY_H

#define  MAX_DISPLAY_ROWS     6
#define  MAX_DISPLAY_COLUMNS  24

typedef char  DisplayLine[ MAX_DISPLAY_COLUMNS + 1 ];

class Display
{
public:
   Display() {};
   virtual ~Display() {};
   virtual void  initialise() = 0;
   virtual void  show( DisplayLine lines[] ) = 0;
};

#endif
