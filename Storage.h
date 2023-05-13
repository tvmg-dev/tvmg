#ifndef STORAGE_H
#define STORAGE_H

#include "utils.h"

#include "Measurement.h"

class Networking;

class Storage
{
public:
   Storage();
   ~Storage();
   void  initialise( void );
   void  storeSample( const Measurement::Sample &sample );
   char  *getCurrentFileName();
   void  setNetworking( Networking *network );
   static bool  isSDCardOk( void );

private:
   char        m_currentFileName[ MAX_FILENAME +1 ];
   Networking  *m_networking;
   uint8_t     m_lastSentHour;
   bool        m_dailyUpdate;

   static bool m_sdCardOk;
};

#endif
