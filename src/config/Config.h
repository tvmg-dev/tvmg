/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include "src/config/hwconfig.h"
#include "src/core/utils.h"
#include "src/core/TVMGFS.h"

extern const char *k_versionStr;

#define SAMPLING_PERIOD_MS 30000

#define MAX_REGISTRY_ENTRIES 50
#define MAX_KEY_LENGTH       32
#define MAX_VALUE_LENGTH     80

#define CONFIG_DEF_TO_STR( x ) #x

/* registry macros: The non-volatile setters update the config.json file */

#define GET_REGISTRY_INT( x )          Config::getRegistryInt( CONFIG_DEF_TO_STR( x ) )
#define GET_REGISTRY_STRING( x )       Config::getRegistryString( CONFIG_DEF_TO_STR( x ) )

#define SET_REGISTRY_INT_VOLATILE(x,y) Config::setRegistryInt(CONFIG_DEF_TO_STR(x), y, true)
#define SET_REGISTRY_INT(x,y)          Config::setRegistryInt(CONFIG_DEF_TO_STR(x), y, false)

#define SET_REGISTRY_VOLATILE(x,y)     Config::setRegistryEntry(CONFIG_DEF_TO_STR(x), y, true)
#define SET_REGISTRY(x,y)              Config::setRegistryEntry(CONFIG_DEF_TO_STR(x), y, false)

class Config
{
public:
   typedef struct {
      String key;
      String value;
      String volatileValue;
   } KeyValue;

   Config();
   ~Config();

   void  initialise();
   bool  isRegistryAvailable();

   static Config  * instance( bool create = false );

   static void setRegistryEntry( const char *key,const char *value,bool isVolatile );
   static void setRegistryInt( const char *key,int32_t value,bool isVolatile );

   static int32_t getRegistryInt( const char *key );
   static const char   *getRegistryString( const char *key );

private:
   bool readRegistryFromFile();
   bool readRegistryFromFlatFile();

   bool loadFromJSON();
   bool writeRegistryToJSON();

   int  findKey( const char *key );

   KeyValue m_entries[ MAX_REGISTRY_ENTRIES ];
   uint8_t  m_numRegistryEntries;
   bool     m_isRegistryOk;
};
#endif
