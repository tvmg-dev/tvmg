#ifndef STORAGE_H
#define STORAGE_H

#include "Measurement.h"

class Networking;

class Storage
{

public:
   Storage();
   ~Storage();
   void  initialise();
   void  storeSample( const Measurement::Sample &sample,bool isNewFile );
   char  *getCurrentFileName();
   void  setNetworking( Networking *network );
   void  getStatus( char *line,int lineSize );

private:
   void  removeOldSamples();

   char        m_currentFileName[ MAX_FILENAME +1 ];
   Networking  *m_networking;
   bool        m_storageOk;
};

#endif
