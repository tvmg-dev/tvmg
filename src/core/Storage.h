/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

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
