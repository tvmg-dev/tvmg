#ifndef NETWORKING_H
#define NETWORKING_H

#include "utils.h"

class AsyncWebServer;
class Emailer;
class WiFiClientSecure;

class Networking
{
public:

   typedef struct
   {
      bool     isConnected;
      char     ipAddr[ 16 ];
      int32_t  timeToConnect;
      int32_t  timeToAcquireNTP;
   } Status;

   Networking();
   ~Networking();

   void initialise( void );
   bool sendEmail( const char *recipient,const char *subject,const char *msg );
   bool sendEmailWithAttachment( const char *recipient,const char *subject,const char *msg,const char *fileName );
   bool sendToEmonCMS( uint32_t emonFeedId,float_t value );
   bool isConnected( void );
   bool didAcquireNTP( void );
   void getIPAddress( char *addrStr );

private:
   Emailer           *m_emailer;
   AsyncWebServer    *m_webServer;
   WiFiClientSecure  *m_emoncmsClient;

   Status  m_status;
};

#endif
