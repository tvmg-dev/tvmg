#ifndef NETWORKING_H
#define NETWORKING_H

#include <Arduino.h>
#include <AsyncUDP.h>

#include "src/userio/Indicator.h"

class WebServer;
class Emailer;
class UserIO;

class Networking
{
public:

   enum NetworkingInfo
   {
      NOTSTARTED,
      CONNECTING,
      STARTING_STA,
      CONNECTED_STA,
      STARTING_AP,
      CONNECTED_AP,
      DISCONNECTED,
      ACQUIRING_NTP,
      OTA_STARTED,
      OTA_PROGRESS,
      OTA_COMPLETE,
      OTA_FAILED,
      INFORMATION_UPDATE
   };

   typedef void (*NetworkingInfoCallback)( NetworkingInfo info,const String &str );

   typedef struct
   {
      NetworkingInfo    state;
      String            ipAddr;
      String            mdnsName;
      String            SSID;
      time_t            startTime;
      int32_t           timeToConnect;
      int32_t           timeToAcquireNTP;
      int32_t           RSSI;
      uint32_t          emonSent;
      uint32_t          emonFails;
      uint32_t          emonQFails;
   } Status;

   Networking(NetworkingInfoCallback infoCallback = nullptr);
   ~Networking();

   void initialise();
   const Status   &getStatus();
   bool sendEmail( const char *recipient,const char *subject,const String &msg );
   bool sendEmailWithAttachment( const char *recipient,const char *subject,const String &msg,const char *fileName,bool fromSD = false );
   bool sendEmailWithFileAsBody( const char *recipient,const char *subject,const char *fileName,bool fromSD = false );
   void sendToEmonCMS( uint32_t emonFeedId,float_t value );
   bool isConnected();
   bool inAPMode();
   bool didAcquireNTP();
   String getIPAddress();
   String getMDNSName();
   String getLocalMDNSName();
   String getSSID();
   bool  startAccessPoint();
   bool  startMDNS();
   bool  acquireNTP();
   void  setUpdateProgress( int percentComplete, const String &filename,bool finished );
   bool  hasUpdated();

   static void  releaseWebClient();
   WebServer   *getWebServer();

   static int   takeNetworkMutex( int ms );
   static void  releaseNetworkMutex();

   static AsyncUDP    *getUDP();
   static AsyncUDP    *getListenUDP();

private:
   Emailer                    *m_emailer;
   WebServer                  *m_webServer;
   Status                      m_status;
   bool                        m_willSendEmails;
   bool                        m_hasUpdated;
   NetworkingInfoCallback      m_infoCallback;

   static AsyncUDP            *s_udp;
   static AsyncUDP            *s_listenUdp;

   static SemaphoreHandle_t   s_networkMutex;
   static uint32_t            s_mutexAcquiredMillis;
   static Indicator           *s_indicator;
};

#endif
