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
   void  initialise();
   void  storeSample( const Measurement::Sample &sample );
   char  *getCurrentFileName();
   void  setNetworking( Networking *network );
   void  getStatus( char *line );

private:
   void  saveSampleToBackingStore( const Measurement::Sample &sample );
   void  removeOldSamples();
   void  updateEmon( const Measurement::Sample &sample );

   char        m_currentFileName[ MAX_FILENAME +1 ];
   Networking  *m_networking;
   uint8_t     m_lastSentHour;
   uint8_t     m_dailyUpdateHour;
   bool        m_dailyUpdated;
   bool        m_storageOk;
};

#endif
