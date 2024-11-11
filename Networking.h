#ifndef NETWORKING_H
#define NETWORKING_H

#include <String.h>
#include <AsyncUDP.h>

#include "utils.h"

class WebServer;
class Emailer;

extern std::recursive_mutex  networkingMutex;

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
   WebServer   *getWebServer();

   static   AsyncUDP    *getUDP();

private:
   Emailer           *m_emailer;
   WebServer         *m_webServer;
   static  AsyncUDP  *s_udp;
   Status            m_status;
   String            m_emonCert;
};

#endif
