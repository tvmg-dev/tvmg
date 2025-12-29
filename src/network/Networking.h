#ifndef NETWORKING_H
#define NETWORKING_H

#include <String.h>
#include <AsyncUDP.h>

class WebServer;
class Emailer;
class UserIO;

class Networking
{
public:

   typedef struct
   {
      bool     isConnected;
      String   ipAddr;
      String   mdnsName;
      String   SSID;
      time_t   startTime;
      int32_t  timeToConnect;
      int32_t  timeToAcquireNTP;
      int32_t  RSSI;
      uint32_t emonSent;
      uint32_t emonFails;
      uint32_t emonQFails;
   } Status;

   Networking();
   ~Networking();

   void initialise();
   const Status   &getStatus();
   bool sendEmail( const char *recipient,const char *subject,const String &msg );
   bool sendEmailWithAttachment( const char *recipient,const char *subject,const String &msg,const char *fileName,bool fromSD = false );
   bool sendEmailWithFileAsBody( const char *recipient,const char *subject,const String &msg,const char *fileName,bool fromSD = false );
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
   void  setUpdateProgress( int size, const String &filename,bool finished );
   void  setUserIO( UserIO *userIO );
   bool  hasUpdated();

   static void  releaseWebClient();
   WebServer   *getWebServer();

   static int   takeNetworkMutex( int ms );
   static void  releaseNetworkMutex();

   static AsyncUDP    *getUDP();
   static AsyncUDP    *getListenUDP();

private:
   Emailer           *m_emailer;
   WebServer         *m_webServer;
   UserIO            *m_userIO;
   Status            m_status;
   bool              m_willSendEmails;
   bool              m_hasUpdated;

   static AsyncUDP            *s_udp;
   static AsyncUDP            *s_listenUdp;

   static SemaphoreHandle_t   s_networkMutex;
   static uint32_t            s_mutexAcquiredMillis;
};

#endif
