/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cJSON.h>
#include <esp_wifi.h>

#include "src/config/Config.h"
#include "src/core/utils.h"  // for scratchBuffer
#include "src/network/Networking.h"
#include "src/network/Update.h"


// Need to map the type to the cloud OTA manifest name, and we have k_versionStr from
// config.h that defines what we're currently running

#if defined( TVMG_WAVESHARE_LCDB )
const char* s_typeForCloudManifest = "WS-ESP32-S3-Touch-LCD-4.3B";
#elif defined( TVMG_WAVESHARE_RELAY )
const char* s_typeForCloudManifest = "WS-ESP32-S3-Relay-1CH";
#elif defined( TVMG_ESP32S3 ) && defined( TVMG_RS485 )
const char* s_typeForCloudManifest = "TVMG-ESP32S3-RS485";
#else
const char* s_typeForCloudManifest = "TVMG-ESP32";
#endif

UpdateManager::UpdateManager( Networking* networking, AsyncEventSource* otaEvents )
    : m_networking( networking ),
      m_updateEvents( otaEvents ),
      m_updateInProgress( false ),
      m_updatePos( 0 ),
      m_buffs( 0 ),
      m_totalSize( 0 ),
      m_filename(),
      m_otaManifestUrl(),
      m_group(),
      m_md5(),
      m_isOtaUpdateAutomatic( false ),
      m_otaUpdateRequested( false ),
      m_otaFailed( false ),
      m_otaAsset()
{
   String otaUpdateMode = String( GET_REGISTRY_STRING( UPDATE_MODE ) );
   m_isOtaUpdateAutomatic = ( otaUpdateMode == "auto" ? true : false );

   m_otaManifestUrl = String( GET_REGISTRY_STRING( OTA_MANIFEST_URL ) );
   if ( m_otaManifestUrl.length() == 0 )
   {
      m_otaManifestUrl = "https://github.com/tvmg-dev/tvmg/releases/download/binaries/otamanifest.json";
   }
   m_otaManifestUrl += "?t=";    // for forcing a non-cached GET in manifest retrieval


   m_group = String( GET_REGISTRY_STRING( UPDATE_GROUP ) );
   if ( m_group.length() == 0 )
   {
      m_group = "release";
   }

   m_otaAsset.url = "";
   m_otaAsset.md5 = "";
   m_otaAsset.version = "";

   TVMG_DEBUG( "group %s %d", m_group.c_str(), m_isOtaUpdateAutomatic );
}

UpdateManager::~UpdateManager() 
{
}

// Macro to clean up on OTA download failure

#define OTA_ERROR_EXIT( errorString,downloading ) \
do { \
   String msg = String( "failed: " ) + errorString; \
   TVMG_ERROR( "Cloud OTA Failed: %s", String( errorString ).c_str() ); \
   Update.abort(); \
   m_updateEvents->send( msg.c_str(),"ota_state",millis() ); \
   m_networking->setUpdateProgress( -1, m_otaAsset.version, downloading ); \
   m_updateInProgress = false; \
   m_otaFailed = true; \
   m_otaAsset.version = ""; \
   m_otaAsset.md5 = ""; \
   m_otaAsset.url = ""; \
   m_otaUpdateRequested = false; \
   return false; \
} while( 0 )

bool UpdateManager::performOtaUpdate()
{
   if ( m_updateInProgress || m_otaAsset.url.length() == 0 )
   {
      return false;
   }

   NetworkMutexGuard mutexGuard;
   if ( !mutexGuard.acquired() )
   {
      TVMG_ERROR( "Failed to acquire network mutex for cloud OTA" );
      return false;
   }

   HTTPClient http;

   // Set initial state
   m_updateInProgress = true;
   m_updatePos = 0;
   m_buffs = 0;
   m_filename = m_otaAsset.version;
   m_md5.begin();

   TVMG_DEBUG( "Starting cloud OTA from %s", m_otaAsset.url.c_str() );
   m_networking->setUpdateProgress( 0, m_filename, false );
   m_updateEvents->send( "0", "ota_progress", millis() );

   if ( !Update.begin( UPDATE_SIZE_UNKNOWN, U_FLASH ) )
   {
      OTA_ERROR_EXIT( "Could not begin flash update", false );
   }

   WiFiClientSecure client;
   client.setInsecure();

   http.begin( client, m_otaAsset.url );
   http.setUserAgent( "TVMG-ESP32-OTA" );
   http.setFollowRedirects( HTTPC_STRICT_FOLLOW_REDIRECTS );

   int httpCode = http.GET();
   if ( httpCode != HTTP_CODE_OK )
   {
      String error = "HTTP GET failed: " + String( httpCode );
      OTA_ERROR_EXIT( error, false );
   }

   size_t contentLength = http.getSize();
   m_totalSize = contentLength;
   WiFiClient* stream = http.getStreamPtr();
   size_t bytesRead = 0;

   while ( http.connected() && ( contentLength == 0 || bytesRead < contentLength ) )
   {
      yield();

      size_t available = stream->available();
      if ( available > 0 )
      {
         size_t toRead = min( available, (size_t)SCRATCH_BUFFER_SIZE - m_updatePos );
         size_t read = stream->readBytes( &scratchBuffer[m_updatePos], toRead );
         if ( read > 0 )
         {
            m_md5.add( &scratchBuffer[m_updatePos], read );
            m_updatePos += read;
            bytesRead += read;

            if ( m_updatePos == SCRATCH_BUFFER_SIZE )
            {
               if ( Update.write( scratchBuffer, SCRATCH_BUFFER_SIZE ) != SCRATCH_BUFFER_SIZE )
               {
                  OTA_ERROR_EXIT( "Flash write error", true );
               }
               m_buffs++;
               m_updatePos = 0;

               if ( contentLength > 0 )
               {
                  int progress = bytesRead * 100 / contentLength;
                  char progMsg[8];
                  snprintf( progMsg, sizeof( progMsg ), "%d", progress );
                  m_updateEvents->send( progMsg, "ota_progress", millis() );
                  m_networking->setUpdateProgress( progress, m_filename, false );
               }
            }
         }
      }
   }

   // Write out remaining bytes
   if ( m_updatePos > 0 )
   {
      if ( Update.write( scratchBuffer, m_updatePos ) != m_updatePos )
      {
         OTA_ERROR_EXIT( "Final flash write error", true );
      }
   }

   // MD5 check
   m_md5.calculate();
   if ( m_md5.toString() != m_otaAsset.md5 )
   {
      OTA_ERROR_EXIT( "MD5 verification failed", true );
   }

   // Finalise the update
   if ( !Update.end( true ) )
   {
      OTA_ERROR_EXIT( Update.errorString(), true );
   }

   // otherwise success, inform the any client and the loopTask will pick up the need to
   // reboot via the network update progress being successful.

   m_updateEvents->send( "100", "ota_progress", millis() );
   m_updateEvents->send( "reboot", "ota_state", millis() );
   m_networking->setUpdateProgress( 100, m_filename, true );

   TVMG_MSG( "Cloud OTA Success" );
   return true;
}

bool UpdateManager::startLocalUpdate( const String& filename, size_t totalSize )
{
   if ( m_updateInProgress )
   {
      return false;
   }

   // We take the network mutex, we can't use the RAII guard as the webserver
   // will be providing updates via feedLocalUpdateData() so the mutex must be held
   // across this startLocalUpdate() call.

   if ( Networking::takeNetworkMutex( 10000 ) != 1 )
   {
      TVMG_ERROR( "Failed to acquire network mutex for OTA" );
      m_updateInProgress = false;
      return false;
   }

   m_updateInProgress = true;
   m_updatePos = 0;
   m_buffs = 0;
   m_filename = filename;

   m_totalSize = totalSize;

   TVMG_DEBUG( "Starting OTA for %s", filename.c_str() );

   m_networking->setUpdateProgress( 0, filename, false );
   m_updateEvents->send( "0", "ota_progress", millis() );

   if ( !Update.begin( UPDATE_SIZE_UNKNOWN, U_FLASH ) )
   {
      TVMG_ERROR( "Failed to start Update" );
      m_networking->setUpdateProgress( -1, filename, false );
      Networking::releaseNetworkMutex();
      m_updateEvents->send( "failed:Could not begin update", "ota_state", millis() );
      m_updateInProgress = false;
      return false;
   }

   return true;
}

bool UpdateManager::feedLocalUpdateData( const uint8_t* data, size_t len )
{
   if ( !m_updateInProgress ) return false;

   size_t remainingInPacket = len;
   size_t packetOffset = 0;

   while ( remainingInPacket > 0 )
   {
      size_t spaceInBuffer = SCRATCH_BUFFER_SIZE - m_updatePos;
      size_t copyBytes = ( remainingInPacket < spaceInBuffer ) ? remainingInPacket : spaceInBuffer;

      memcpy( &scratchBuffer[m_updatePos], &data[packetOffset], copyBytes );

      m_updatePos += copyBytes;
      packetOffset += copyBytes;
      remainingInPacket -= copyBytes;

      if ( m_updatePos == SCRATCH_BUFFER_SIZE )
      {
         if ( Update.write( scratchBuffer, SCRATCH_BUFFER_SIZE ) != SCRATCH_BUFFER_SIZE )
         {
            TVMG_ERROR( "Update write failed" );
            Update.abort();
            Networking::releaseNetworkMutex();
            m_updateEvents->send( "failed:Flash write error", "ota_state", millis() );
            m_networking->setUpdateProgress( -1, m_filename, true );
            m_updateInProgress = false;
            return false;
         }
         m_buffs++;
         m_updatePos = 0;

         if ( m_totalSize > 0 )
         {
            size_t totalWritten = ( m_buffs * SCRATCH_BUFFER_SIZE ) + packetOffset;
            int progress = totalWritten * 100 / m_totalSize;
            char progMsg[8];
            sprintf( progMsg, "%d", progress );
            m_updateEvents->send( progMsg, "ota_progress", millis() );
            m_networking->setUpdateProgress( progress, m_filename, false );
         }
      }
   }

   return true;
}

bool UpdateManager::finishLocalUpdate()
{
   if ( !m_updateInProgress ) return false;

   // Write out any bytes remaining

   if ( m_updatePos > 0 )
   {
      if ( Update.write( scratchBuffer, m_updatePos ) != m_updatePos )
      {
         TVMG_ERROR( "Final Update write failed" );
         Update.abort();
         Networking::releaseNetworkMutex();
         m_updateEvents->send( "failed:Flash write error", "ota_state", millis() );
         m_networking->setUpdateProgress( -1, m_filename, true );
         m_updateInProgress = false;
         return false;
      }
   }

   // Now finalise the update
   if ( Update.end( true ) )
   {
      m_updateEvents->send( "100", "ota_progress", millis() );
      m_updateEvents->send( "reboot", "ota_state", millis() );
      size_t totalWritten = ( m_buffs * SCRATCH_BUFFER_SIZE ) + m_updatePos;
      m_networking->setUpdateProgress( totalWritten, m_filename, true );
      TVMG_MSG( "OTA Success. Total written: %d bytes", totalWritten );
      Networking::releaseNetworkMutex();
      m_updateInProgress = false;
      return true;
   }
   else
   {
      String errorStr = Update.errorString();
      String sseFailMsg = "failed:" + ( errorStr.length() ? errorStr : "Flash Error" );
      m_updateEvents->send( sseFailMsg.c_str(), "ota_state", millis() );
      m_networking->setUpdateProgress( -1, m_filename, true );
      TVMG_ERROR( "OTA Failed: %s", errorStr.c_str() );
      Networking::releaseNetworkMutex();
      m_updateInProgress = false;
      return false;
   }
}

bool UpdateManager::isOtaManifestUpdated( String* outPayload )
{
   NetworkMutexGuard mutexGuard;

   if ( !mutexGuard.acquired() )
   {
      TVMG_ERROR( "Failed to acquire network mutex for manifest check" );
      return false;
   }

   // free the web client, and then get a new WiFiSecureClient for a HTTP request

   Networking::releaseWebClient();

   WiFiClientSecure client;
   client.setInsecure();
   HTTPClient http;

   // Get URL for manifest from Registry or hardcoded, we'll add a timestamp
   // to force a query.

   String url = m_otaManifestUrl;
   String timeStamp( millis() % 1000000 );
   url += timeStamp;
   TVMG_DEBUG( "Manifest URL: [%s]", url.c_str() );

   http.begin( client, url );
   http.setUserAgent( "TVMG-OTA" );
   http.setFollowRedirects( HTTPC_STRICT_FOLLOW_REDIRECTS );

   // Add conditional header if an ETag exists in the registry

   const char* cachedEtag = GET_REGISTRY_STRING( MANIFEST_ETAG );
   if ( cachedEtag != nullptr && strlen( cachedEtag ) > 0 )
   {
      String header = "\"";
      header += cachedEtag;
      header += "\"";

      TVMG_DEBUG( "adding If-None-Match %s", header.c_str() );
      http.addHeader( "If-None-Match", header );
   }

   // Prepare to capture the ETag from the response headers

   const char* headerKeys[] = { "ETag" };
   http.collectHeaders( headerKeys, 1 );

   // Get may return 200 if manifest updated, 304 is content is the same, or possibly an error

   int httpCode = http.GET();
   bool isNewManifest = false;

   switch ( httpCode )
   {
      case HTTP_CODE_OK:
         isNewManifest = true;
         break;
      case HTTP_CODE_NOT_MODIFIED:
         TVMG_DEBUG( "No change in manifest detected" );
         break;
      default:
         TVMG_DEBUG( "HTTP GET failed: %s (code %d)", http.errorToString( httpCode ).c_str(), httpCode );
         break;
   }

   if ( isNewManifest )
   {
      TVMG_MSG( "New OTA manifest" );
      // New manifest found: Update (volatile) registry with the new ETag
      String newEtag = http.header( "ETag" );
      if ( newEtag.length() > 0 )
      {
         TVMG_DEBUG( "Assign manifest etag %s", newEtag.c_str() );
         SET_REGISTRY_VOLATILE( MANIFEST_ETAG, newEtag.c_str() );
      }

      *outPayload = http.getString();
   }

   http.end();

   return isNewManifest;
}

void UpdateManager::checkManifest()
{
   String manifest;

   START_TIMING( "OTA Manifest Check" );
   if ( isOtaManifestUpdated( &manifest ) )
   {
      cJSON* root = cJSON_Parse( manifest.c_str() );
      cJSON* deviceNode = nullptr;
      cJSON* groupNode = nullptr;

      if ( !root )
      {
         TVMG_ERROR( "Failed to parse the app manifest" );
      }
      else if ( !( deviceNode = cJSON_GetObjectItem( root, s_typeForCloudManifest ) ) )
      {
         TVMG_ERROR( "No %s device in manifest", s_typeForCloudManifest );
      }
      else if ( !( groupNode = cJSON_GetObjectItem( deviceNode, m_group.c_str() ) ) )
      {
         TVMG_ERROR( "No %s group in manifest", m_group.c_str() );
      }
      else
      {
         cJSON* version = cJSON_GetObjectItem( groupNode, "v" );
         cJSON* md5 = cJSON_GetObjectItem( groupNode, "md5" );
         cJSON* url = cJSON_GetObjectItem( groupNode, "url" );

         if ( cJSON_IsString( version ) && cJSON_IsString( md5 ) && cJSON_IsString( url ) )
         {
            if ( strlen( version->valuestring ) == 0 || strlen( md5->valuestring) == 0 ||
                                                         strlen( url->valuestring ) == 0 )
            {
               TVMG_WARN( "Invalid manifest data" );
               return;
            }

            bool isNew = false;
            if ( strcmp( version->valuestring, k_versionStr ) )
            {
               m_otaAsset.url = url->valuestring;
               m_otaAsset.md5 = md5->valuestring;
               m_otaAsset.version = version->valuestring;
               isNew = true;
            }
            else if ( m_otaAsset.url.length() > 0 )
            {
               // this shouldn't happen in practice, but for testing set the asset info to
               // empty strings (test/dev we may revert back without downloading).

               m_otaAsset.url = "";
               m_otaAsset.md5 = "";
               m_otaAsset.version = "";
            }

            TVMG_MSG( "Manifest %s: Our device %s, group %s, running %s, manifest %s",
                      ( isNew ? "updated" : "unchanged" ), s_typeForCloudManifest,
                      m_group.c_str(), k_versionStr, version->valuestring );
         }
         else
         {
            TVMG_ERROR( "Not all group data found in manifest" );
         }
      }

      cJSON_Delete( root );
   }

   END_TIMING;
}

UpdateManager::BinaryAsset UpdateManager::getOtaAsset()
{
   return m_otaAsset;
}

bool UpdateManager::isTimeToCheck()
{
   static int32_t offsetSeconds = -1;
   static bool alreadyPolledInPeriod = false;
   bool shouldCheck = false;

   // one off setting of random number of seconds, 0-50, for offsetting poll requests
   if ( offsetSeconds == -1 )
   {
      uint8_t mac[6];
      esp_wifi_get_mac( WIFI_IF_STA,mac );

      // Seed the generator using the last 4 bytes of the MAC
      uint32_t seed = ( mac[2] << 24 ) | ( mac[3] << 16 ) | ( mac[4] << 8 ) | mac[5];
      randomSeed( seed );

      offsetSeconds = random( 0, 50 );

      TVMG_DEBUG( "Manifest poll offset: %d seconds", offsetSeconds );
   }

   struct tm timeinfo;
   if ( !getLocalTime( &timeinfo ) ) 
   {
      return shouldCheck;
   }

   int currentMin = timeinfo.tm_min;
   int currentSec = timeinfo.tm_sec;

   // If we're an alpha group then check every 5 minutes for expediency, otherwise 15 & 45 minutes 
   // past the hour

   bool validTime;
   if ( m_group == "alpha" )
   {
      validTime = ( currentMin % 5 == 0 );
   }
   else
   {
      validTime = ( currentMin == 15 || currentMin == 45 );
   }

   if ( shouldCheck || validTime )
   {
      // Trigger only when we're beyond our random seconds 
      if ( currentSec > offsetSeconds && !alreadyPolledInPeriod )
      {
         shouldCheck = true;
         alreadyPolledInPeriod = true;
      }
   }
   else
   {
      alreadyPolledInPeriod = false;
   }

   if ( shouldCheck )
   {
      TVMG_MSG( "Should check manifest" );
   }

   return shouldCheck;
}

void UpdateManager::checkForOtaUpdate()
{
   static bool firstCheck = true;
   bool updateRequired = m_otaUpdateRequested;

   if ( firstCheck || isTimeToCheck() )
   {
      firstCheck = false;  // may as well always assign to avoid conditional

      // We don't check if we've failed OTA in this boot cycle, to avoid continuous
      // update attempts (e.g. if wrong URL or md5 in manifest).  We do this after time
      // check just to reduce debug warnings.

      if ( m_otaFailed )
      {
         TVMG_WARN( "Not checking for update, OTA previously failed in this cycle" );
         return;
      }

      checkManifest();     // Arguanly we may want to only check if no current asset ?

      if ( m_otaAsset.url.length() )
      {
         // Only actually update if we're in automatic mode
         if ( m_isOtaUpdateAutomatic )
         {
            updateRequired = true;
         }
      }
   }

   if ( updateRequired )
   {
      Networking::releaseWebClient();
      performOtaUpdate();
   }
}

void UpdateManager::triggerPendingOtaUpdate()
{
   if ( m_otaAsset.url.length() > 0 )
   {
      m_otaUpdateRequested = true;
   }
}
