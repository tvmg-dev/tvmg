#ifndef NETWORKING_H
#define NETWORKING_H

#include <String.h>

#include "utils.h"

class WebServer;
class Emailer;
class WiFiClientSecure;

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
   bool sendEmail( const char *recipient,const char *subject,const char *msg );
   bool sendEmailWithAttachment( const char *recipient,const char *subject,const char *msg,const char *fileName );
   bool sendToEmonCMS( uint32_t emonFeedId,float_t value );
   bool isConnected();
   bool didAcquireNTP();
   String getIPAddress();
   String getMDNSName();
   String getLocalMDNSName();
   String getSSID();
   bool  startAccessPoint();
   bool  startMDNS();
   bool  acquireNTP();

private:
   Emailer           *m_emailer;
   WebServer         *m_webServer;
   WiFiClientSecure  *m_emoncmsClient;

   Status  m_status;
};

#endif
