#ifndef NETWORKING_H
#define NETWORKING_H

#include <String.h>
#include <AsyncUDP.h>
#include <mutex>

#include "utils.h"
#include "UserIO.h"

#define  SCOPE_LOCK_NW_MUTEX \
do { \
   std::lock_guard<std::recursive_mutex> lock( Networking::getNetworkingMutex() ); \
} while( 0 );

#define  SCOPE_RELEASE_WEB_CLIENT \
do { \
   std::lock_guard<std::recursive_mutex> lock( Networking::getNetworkingMutex() ); \
   Networking::releaseWebClient(); \
} while( 0 );

class WebServer;
class Emailer;

class Networking
{
public:

   typedef struct
   {
      bool     isConnected;
      String   ipAddr;
      String   mdnsName;
      String   SSID;
      int32_t  timeToConnect;
      int32_t  timeToAcquireNTP;
   } Status;

   Networking();
   ~Networking();

   void initialise();
   bool sendEmail( const char *recipient,const char *subject,const String &msg );
   bool sendEmailWithAttachment( const char *recipient,const char *subject,const char *msg,const char *fileName,bool fromSPIFFS = false );
   void sendToEmonCMS( uint32_t emonFeedId,float_t value );
   void getEMONStats( uint32_t *sends,uint32_t *qFails, uint32_t *fails );
   bool isConnected();
   bool didAcquireNTP();
   String getIPAddress();
   String getMDNSName();
   String getLocalMDNSName();
   String getSSID();
   bool  startAccessPoint();
   bool  startMDNS();
   bool  acquireNTP();
   void  setUpdateProgress( int index, const String &filename,bool finished );
   void  setUserIO( UserIO *userIO );
   void  serverHome();
   void  startFileEdit( const String &filename );


   static void  releaseWebClient();
   static std::recursive_mutex   &getNetworkingMutex();
   WebServer   *getWebServer();
   bool  isBusy();


   static   AsyncUDP    *getUDP();

private:
   Emailer           *m_emailer;
   WebServer         *m_webServer;
   UserIO            *m_userIO;
   static  AsyncUDP  *s_udp;
   Status            m_status;
   String            m_emonCert;
   bool              m_willSendEmails;
   bool              m_isUpdating;
   bool              m_isEditing;
};

#endif
