/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef UPDATE_H
#define UPDATE_H

#include <Arduino.h>
#include <AsyncUDP.h>
#include <ESPAsyncWebServer.h>
#include <MD5Builder.h>

class Networking;

class UpdateManager
{
public:
   typedef struct {
      String   url;
      String   md5;
      String   version;
   } BinaryAsset;

   UpdateManager( Networking* networking, AsyncEventSource* otaEvents );
   ~UpdateManager();

   // Local update, start, feed data, finish
   bool startLocalUpdate( const String& filename, size_t totalSize = 0 );
   bool feedLocalUpdateData( const uint8_t* data, size_t len );
   bool finishLocalUpdate();

   // have we got an update available from a previous check ?
   BinaryAsset getOtaAsset();
   void checkForOtaUpdate();
   void triggerPendingOtaUpdate();

  private:
   // Cloud OTA: Download and flash from URL with MD5 verification
   bool performOtaUpdate();
   void checkManifest();
   bool isTimeToCheck();
   bool isOtaManifestUpdated( String* outPayload );

   Networking* m_networking;
   AsyncEventSource* m_updateEvents;

   // Update state
   bool m_updateInProgress;
   size_t m_updatePos;
   size_t m_buffs;
   size_t m_totalSize;
   String m_filename;

   // Ota information 
   String m_otaManifestUrl;
   String m_group;
   MD5Builder m_md5;
   bool m_isOtaUpdateAutomatic;
   bool m_otaUpdateRequested;
   bool m_otaFailed;
   BinaryAsset m_otaAsset;
};

#endif  // UPDATE_H
