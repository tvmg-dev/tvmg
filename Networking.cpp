#include <WiFi.h>
#include <ESPmDNS.h>
#include <EMailSender.h>

#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "utils.h"
#include "config.h"

#include "Networking.h"
#include "Measurement.h"

#include "WebServer.h"

const char* ntpServer = "pool.ntp.org";

// emoncms.org certificate is signed by 'ZeroSSL RSA Domain Secure Site CA'
// (in turn signed by USERTrust RSA Certification Authority).  The ZeroSSL
// certificate is below.

const char* emoncmsCertificate = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIG1TCCBL2gAwIBAgIQbFWr29AHksedBwzYEZ7WvzANBgkqhkiG9w0BAQwFADCB\n" \
"iDELMAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0pl\n" \
"cnNleSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNV\n" \
"BAMTJVVTRVJUcnVzdCBSU0EgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMjAw\n" \
"MTMwMDAwMDAwWhcNMzAwMTI5MjM1OTU5WjBLMQswCQYDVQQGEwJBVDEQMA4GA1UE\n" \
"ChMHWmVyb1NTTDEqMCgGA1UEAxMhWmVyb1NTTCBSU0EgRG9tYWluIFNlY3VyZSBT\n" \
"aXRlIENBMIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAhmlzfqO1Mdgj\n" \
"4W3dpBPTVBX1AuvcAyG1fl0dUnw/MeueCWzRWTheZ35LVo91kLI3DDVaZKW+TBAs\n" \
"JBjEbYmMwcWSTWYCg5334SF0+ctDAsFxsX+rTDh9kSrG/4mp6OShubLaEIUJiZo4\n" \
"t873TuSd0Wj5DWt3DtpAG8T35l/v+xrN8ub8PSSoX5Vkgw+jWf4KQtNvUFLDq8mF\n" \
"WhUnPL6jHAADXpvs4lTNYwOtx9yQtbpxwSt7QJY1+ICrmRJB6BuKRt/jfDJF9Jsc\n" \
"RQVlHIxQdKAJl7oaVnXgDkqtk2qddd3kCDXd74gv813G91z7CjsGyJ93oJIlNS3U\n" \
"gFbD6V54JMgZ3rSmotYbz98oZxX7MKbtCm1aJ/q+hTv2YK1yMxrnfcieKmOYBbFD\n" \
"hnW5O6RMA703dBK92j6XRN2EttLkQuujZgy+jXRKtaWMIlkNkWJmOiHmErQngHvt\n" \
"iNkIcjJumq1ddFX4iaTI40a6zgvIBtxFeDs2RfcaH73er7ctNUUqgQT5rFgJhMmF\n" \
"x76rQgB5OZUkodb5k2ex7P+Gu4J86bS15094UuYcV09hVeknmTh5Ex9CBKipLS2W\n" \
"2wKBakf+aVYnNCU6S0nASqt2xrZpGC1v7v6DhuepyyJtn3qSV2PoBiU5Sql+aARp\n" \
"wUibQMGm44gjyNDqDlVp+ShLQlUH9x8CAwEAAaOCAXUwggFxMB8GA1UdIwQYMBaA\n" \
"FFN5v1qqK0rPVIDh2JvAnfKyA2bLMB0GA1UdDgQWBBTI2XhootkZaNU9ct5fCj7c\n" \
"tYaGpjAOBgNVHQ8BAf8EBAMCAYYwEgYDVR0TAQH/BAgwBgEB/wIBADAdBgNVHSUE\n" \
"FjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwIgYDVR0gBBswGTANBgsrBgEEAbIxAQIC\n" \
"TjAIBgZngQwBAgEwUAYDVR0fBEkwRzBFoEOgQYY/aHR0cDovL2NybC51c2VydHJ1\n" \
"c3QuY29tL1VTRVJUcnVzdFJTQUNlcnRpZmljYXRpb25BdXRob3JpdHkuY3JsMHYG\n" \
"CCsGAQUFBwEBBGowaDA/BggrBgEFBQcwAoYzaHR0cDovL2NydC51c2VydHJ1c3Qu\n" \
"Y29tL1VTRVJUcnVzdFJTQUFkZFRydXN0Q0EuY3J0MCUGCCsGAQUFBzABhhlodHRw\n" \
"Oi8vb2NzcC51c2VydHJ1c3QuY29tMA0GCSqGSIb3DQEBDAUAA4ICAQAVDwoIzQDV\n" \
"ercT0eYqZjBNJ8VNWwVFlQOtZERqn5iWnEVaLZZdzxlbvz2Fx0ExUNuUEgYkIVM4\n" \
"YocKkCQ7hO5noicoq/DrEYH5IuNcuW1I8JJZ9DLuB1fYvIHlZ2JG46iNbVKA3ygA\n" \
"Ez86RvDQlt2C494qqPVItRjrz9YlJEGT0DrttyApq0YLFDzf+Z1pkMhh7c+7fXeJ\n" \
"qmIhfJpduKc8HEQkYQQShen426S3H0JrIAbKcBCiyYFuOhfyvuwVCFDfFvrjADjd\n" \
"4jX1uQXd161IyFRbm89s2Oj5oU1wDYz5sx+hoCuh6lSs+/uPuWomIq3y1GDFNafW\n" \
"+LsHBU16lQo5Q2yh25laQsKRgyPmMpHJ98edm6y2sHUabASmRHxvGiuwwE25aDU0\n" \
"2SAeepyImJ2CzB80YG7WxlynHqNhpE7xfC7PzQlLgmfEHdU+tHFeQazRQnrFkW2W\n" \
"kqRGIq7cKRnyypvjPMkjeiV9lRdAM9fSJvsB3svUuu1coIG1xxI1yegoGM4r5QP4\n" \
"RGIVvYaiI76C0djoSbQ/dkIUUXQuB8AL5jyH34g3BZaaXyvpmnV4ilppMXVAnAYG\n" \
"ON51WhJ6W0xNdNJwzYASZYH+tmCWI+N60Gv2NNMGHwMZ7e9bXgzUCZH5FaBFDGR5\n" \
"S9VWqHB73Q+OyIVvIbKYcSc2w/aSuFKGSA==\n" \
"-----END CERTIFICATE-----\n";

const char *emoncmsApiKey = "***REMOVED***";

class Emailer
{
public:
   Emailer();
   ~Emailer();

   void initialise();
   bool sendEmail( const char *recipient,const char *subject,const char *msg );
   bool sendEmailWithAttachment( const char *recipient,const char *subject,const char *msg,const char *fileName );

private:
   EMailSender *m_sender;
};

Emailer::Emailer()
       : m_sender( nullptr )
{
   PW_DEBUG( "Emailer::Emailer()" );
}

Emailer::~Emailer()
{
   PW_DEBUG( "Emailer::~Emailer()" );

   delete m_sender;
}

void  Emailer::initialise()
{
   PW_MSG( "Emailer initialise" );

   char host[ MAX_VALUE_LENGTH ];
   char account[ MAX_VALUE_LENGTH ],password[ MAX_VALUE_LENGTH ];
   int  port;

   strcpy( host,GET_REGISTRY_STRING( SMTP_HOST ) );
   port = GET_REGISTRY_INT( SMTP_PORT );
   strcpy( account,GET_REGISTRY_STRING( ACCOUNT_EMAIL ) );
   strcpy( password,GET_REGISTRY_STRING( ACCOUNT_PASSWORD ) );

   PW_DEBUG( "Email : Host %s [%d] - %s %s",host,port,account,password );

   m_sender = new EMailSender( account,password,account,"HeatPump",host,port );

   m_sender->setPublicIpDescriptor( "dyllysplace.com" );
}

bool Emailer::sendEmail( const char *recipient,const char *subject,const char *msg )
{
   if ( m_sender )
   {
      EMailSender::EMailMessage message;

      message.subject = subject;
      message.message = msg;
      message.mime = "text/plain";

      PW_DEBUG( "Sending to %s [%s]",recipient,subject );

      EMailSender::Response resp = m_sender->send( recipient,message );

      if ( !resp.status )
      {
         PW_WARN( "Failed to send email");
      }

      return resp.status;
   }
   return false;
}

bool Emailer::sendEmailWithAttachment( const char *recipient,const char *subject,const char *msg,const char *fileName )
{
   if ( m_sender )
   {
      EMailSender::EMailMessage message;
      EMailSender::FileDescriptior fileDescriptor[ 1 ];

      fileDescriptor[ 0 ].filename = "testfile.dat";
      fileDescriptor[ 0 ].url = fileName;
      fileDescriptor[ 0 ].mime = "text/plain";
      fileDescriptor[0].encode64 = false;
      fileDescriptor[ 0 ].storageType = EMailSender::EMAIL_STORAGE_TYPE_SD;

      EMailSender::Attachments attachments = { 1, fileDescriptor };

      message.subject = subject;
      message.message = msg;
      message.mime = "text/plain";

      PW_DEBUG( "Sending to %s [%s]",recipient,subject );

      EMailSender::Response resp = m_sender->send( recipient,message,attachments );

      if ( !resp.status )
      {
         PW_WARN( "Failed to send email");
      }

      return resp.status;
   }

   return false;
}

Networking::Networking()
          : m_emailer( nullptr ),
            m_webServer( nullptr ),
            m_emoncmsClient( nullptr )
{
   PW_DEBUG( "Networking::Networking()" );
   PW_MSG( "Networking Startup" );

   // set status to defaults, not connected etc.

   m_status.ipAddr = "";
   m_status.mdnsName = "";
   m_status.isConnected = false;
   m_status.timeToAcquireNTP = -1;
   m_status.timeToConnect = 0;
}

Networking::~Networking()
{
   PW_DEBUG( "Networking::~Networking()" );

   delete m_emoncmsClient;
   delete m_webServer;
   delete m_emailer;
}

void Networking::initialise( bool isNewSetup )
{
   char  accessPointName[ 24 ];

   PW_DEBUG( "Networking::initialise" );

   if ( isNewSetup )
   {
      strncpy( accessPointName,"HeatPump-Monitor",24 );
   }
   else
   {
      strncpy( accessPointName,GET_REGISTRY_STRING( ACCESS_POINT_NAME ),24 );
   }

   // now networking...

   uint32_t start = millis();

   // We want to advertise as an AccessPoint and also act as a station

   WiFi.mode( WIFI_AP_STA );
   WiFi.softAP( accessPointName );

   PW_MSG( "AP at : %s",WiFi.softAPIP().toString().c_str() );

   // if this has been configured then connect to WiFi & then acquire NTP

   if ( !isNewSetup )
   {
      WiFi.begin( GET_REGISTRY_STRING( WIFI_SSID ), GET_REGISTRY_STRING( WIFI_PASSWORD ) );
      while (WiFi.status() != WL_CONNECTED && (millis() - start < GET_REGISTRY_INT( WIFI_CONNECT_TIMEOUT )) )
      {
         delay(200);
      }

      // Now onto NTP

      if ( WiFi.status() != WL_CONNECTED )
      {
         PW_WARN( "Network not connected" );
         m_status.isConnected = false;
      }
      else
      {
         struct tm   timeInfo;

         m_status.isConnected = true;
         m_status.timeToConnect = ( millis() - start ) / 1000;
         m_status.ipAddr = WiFi.localIP().toString();

         PW_DEBUG( "Acquiring NTP..." );

         configTzTime( "GMT0BST,M3.5.0/1,M10.5.0",ntpServer );
         start = millis();
         while ( !getLocalTime( &timeInfo ) && (millis() - start < GET_REGISTRY_INT( NTP_UPDATE_TIMEOUT) ) )
         {
            delay( 200 );
         }

         if ( !getLocalTime( &timeInfo ) )
         {
            PW_WARN( "NTP not available" );
            m_status.timeToAcquireNTP = -1;
         }
         else
         {
            m_status.timeToAcquireNTP = ( millis() - start ) / 1000;
         }

         // We have connected network, so we can have the emailer

         m_emailer = new Emailer;
         m_emailer->initialise();

         // And now for the emoncms client...

         m_emoncmsClient = new WiFiClientSecure;
         m_emoncmsClient->setCACert( emoncmsCertificate );
      }
   }

   // Start the MDNS service so we can be discovered and configured

   while( !MDNS.begin( accessPointName ) )
   {
      PW_WARN( "Failed to setup MDNS responder" );
      delay( 5000 );
   }

   m_status.mdnsName = String( accessPointName ) + String( ".local" );
   PW_MSG( "Available at %s/manager",m_status.mdnsName.c_str() );

   // Start our configuration/download server

   m_webServer = new WebServer();
   m_webServer->initialise();

   MDNS.addService( "http","tcp",80 );
}

bool  Networking::isConnected()
{
   return m_status.isConnected;
}

String Networking::getIPAddress()
{
   return( m_status.ipAddr );
}

String Networking::getMDNSName()
{
   return( m_status.mdnsName );
}

bool Networking::didAcquireNTP()
{
   return (m_status.timeToAcquireNTP > -1 );
}

bool Networking::sendEmail( const char *recipient,const char *subject,const char *msg )
{
#if NO_EMAIL != 1
   if ( m_emailer )
   {
      return m_emailer->sendEmail( recipient,subject,msg );
   }
   return false;
#else
   PW_WARN( "Would send email %s",subject );
   return true;
#endif
}

bool Networking::sendEmailWithAttachment( const char *recipient,const char *subject,const char *msg,const char *fileName )
{
#if NO_EMAIL != 1
   if ( m_emailer )
   {
      return m_emailer->sendEmailWithAttachment( recipient,subject,msg,fileName );
   }
   return false;
#else
   PW_WARN( "Would send email %s",subject );
   return true;
#endif
}

// https://emoncms.org/feed/insert.json?id=0&time=0&value=100&apikey=***REMOVED***

bool Networking::sendToEmonCMS( uint32_t emonFeedId,float_t value )
{
#if NO_EMONCMS_UPDATE == 1
   (void) emonFeedId;
   (void) value;

   PW_WARN( "Would send to emon {%u : %.2f]",emonFeedId,value );
   return true;
#else
   bool        retOk = false;
   char        url[ 256 ];
   time_t      utc;
   HTTPClient  https;

   time( &utc );

   snprintf( url,256,"https://emoncms.org/feed/insert.json?id=%u&time=%d&value=%.2f&apikey=%s",emonFeedId,utc,value,emoncmsApiKey );

   PW_DEBUG( "EMONCMS: Send %s",url );

   if ( ! https.begin( *m_emoncmsClient, url ) )
   {
      PW_ERROR( "EMONCMS: failed to connect" );
   }
   else
   {
      // start connection and send HTTP header
      int httpCode = https.GET();

      // httpCode will be negative on error
      if (httpCode < 0)
      {
         PW_ERROR( "EMONCMS: GET failed [%s]",https.errorToString(httpCode).c_str() );
      }
      else if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
      {
         // HTTP header has been sent and Server response header has been handled
         retOk = true;

         String payload = https.getString();
         PW_DEBUG( "EMONCMS: Response %s",payload.c_str() );
      }

      https.end();
   }

   return retOk;
#endif
}
