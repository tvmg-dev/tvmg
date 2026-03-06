/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef TVMG_FS_H
#define TVMG_FS_H

#include <FS.h>

// Couldn't derive from SPIFFS of LITTLEFS due to global instance of those
// classes that caused guru-meditations with a second global instance

class TVMGFileSystem
{
public:
   TVMGFileSystem();

   File open( const String& path,const char* mode = "r" );
   bool exists( const String& path );
   bool remove( const String& path );
   bool rename( const char* pathFrom,const char* pathTo );
   bool mkdir( const String& path );
   bool rmdir( const String& path );
   bool format();

   bool begin( bool formatOnFail = false );

   fs::FS & getFS();

   size_t totalBytes();
   size_t usedBytes();

   const char* typeName() const;

   explicit operator bool();

private:
   bool  m_isMounted;
};

// Declare the global instance
extern TVMGFileSystem tvmgFileSys;
#endif
