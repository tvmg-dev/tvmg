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
#include "UserIO.h"

extern UserIO  *userIO;

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

static WiFiClientSecure *s_emoncmsClient = nullptr;
static HTTPClient       *s_webClient = nullptr;

#define  KEEP_ALIVE_MS     6000

static uint32_t emonSendRequests = 0,emonQFailures = 0, emonSendFailures = 0;

// Example feed data insertion -
// s_webClient://emoncms.org/feed/insert.json?id=0&time=0&value=100&apikey=***REMOVED***

struct   EmonData {
   uint32_t emonFeedId;
   float_t  value;
};

static TaskHandle_t  backgroundHandle = NULL;
static QueueHandle_t dataQueue = NULL;

void  sendToEmonCMS( uint32_t emonFeedId,float_t value );

void  backgroundThread( void *params )
{
   EmonData *data;
   bool     sendData = true;

   if ( GET_REGISTRY_INT( UPDATE_EMONCMS ) != 1 )
   {
      sendData = false;
   }

   while( true )
   {
      if ( dataQueue )
      {
         if ( xQueueReceive( dataQueue,&data,portTICK_PERIOD_MS * 60000 ) )
         {
            char  buff[ 128 ];
            snprintf( buff,128,"EMONCMS: Processed Q for [%u], %.2f",data->emonFeedId,data->value );
            START_TIMING( buff );

            PW_DEBUG( "EMONCMS: Received from Q (cpu%u) - [%u], %.2f",xPortGetCoreID(),data->emonFeedId,data->value );

            if ( sendData )
            {
               sendToEmonCMS( data->emonFeedId,data->value );
            }
            else
            {
               PW_MSG( "EMONCMS: Would send to emon [%u], %.2f",data->emonFeedId,data->value );
               delay( random( 1000,2500 ) );
            }

            delete data;
            END_TIMING;
         }
         else
         {
            PW_DEBUG( "EMONCMS: Nothing received from Q (cpu%u)",xPortGetCoreID() );
         }
      }
      else
      {
         PW_WARN( "EMONCMS: Waiting for Q creation" );
         delay( 2000 );
      }
   }
}

void  sendToEmonCMS( uint32_t emonFeedId,float_t value )
{
   static uint32_t   lastSentMillis = 0;
   char        path[ 128 ];
   time_t      utc;

   // If we've not processed a send request for KEEP_ALIVE_MS then force
   // the connection to drop. Maybe unecessary but don't want to try and
   // keep a permanent connection.  Really would like to start a connection
   // when we do the batch update and then close it, but we don't have an
   // API to handle that yet (and it would need to be thread safe).

   if ( millis() - lastSentMillis > KEEP_ALIVE_MS && s_webClient )
   {
      PW_DEBUG( "EMONCMS: Closing connection (%u ms elapsed)",millis() - lastSentMillis );
      s_webClient->end();
      delete s_webClient;
      s_webClient = nullptr;

      // take this opportunity to show some stats
      PW_MSG( "EMONCMS: Sent %u, failed [Q,E] [%u,%u]",emonSendRequests,emonQFailures,emonSendFailures );
   }

   // Create a new HTTPClient if we need to, and we try to connect to the
   // emon host, but no GET request yet

   if ( !s_webClient )
   {
      s_webClient = new HTTPClient();
      s_webClient->setReuse( true );

      if ( ! s_webClient->begin( *s_emoncmsClient,"https://emoncms.org" ) )
      {
         PW_ERROR( "EMONCMS: Can't start HTTPClient" );
         delete s_webClient;
         s_webClient = nullptr;
         emonSendFailures++;
         return;
      }
   }

   // Form our path for the GET request based on the feed Id
   time( &utc );

   snprintf( path,128,"/feed/insert.json?id=%u&time=%d&value=%.2f&apikey=%s",emonFeedId,utc,value,emoncmsApiKey );

   PW_MSG( "EMONCMS: Send %s",path );

   // Send the GET request - which will force a connect if necessary

   s_webClient->setURL( path );
   int httpCode = s_webClient->GET();

   // Check success from HTTP perspective, then check success from emon REST perspective

   if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
   {
      String payload = s_webClient->getString();
      if ( strstr( payload.c_str(),"false" ) )
      {
         PW_ERROR( "EMONCMS: emon failure [%s]",payload.c_str() );
         emonSendFailures++;
      }
   }
   else
   {
      emonSendFailures++;
      PW_ERROR( "EMONCMS: GET failed [%s]",s_webClient->errorToString(httpCode).c_str() );
   }

   lastSentMillis = millis();
}

class Emailer
{
public:
   Emailer();
   ~Emailer();

   void initialise();
   bool sendEmail( const char *recipient,const char *subject,const String &msg );
   bool sendEmailWithAttachment( const char *recipient,const char *subject,const char *msg,const char *fileName,bool fromSPIFFS );

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

bool Emailer::sendEmail( const char *recipient,const char *subject,const String &msg )
{
   if ( m_sender )
   {
      EMailSender::EMailMessage message;

      message.subject = subject;
      message.message = msg.c_str();
      message.mime = "text/plain";

      PW_MSG( "Sending to %s [%s]",recipient,subject );

      EMailSender::Response resp = m_sender->send( recipient,message );

      if ( !resp.status )
      {
         PW_WARN( "Failed to send email");
      }

      return resp.status;
   }
   return false;
}

bool Emailer::sendEmailWithAttachment( const char *recipient,const char *subject,const char *msg,const char *fileName,bool fromSPIFFS )
{
   char buff[ 256 ];
   snprintf( buff,256,"Sending to %s [%s]",recipient,subject );

   START_TIMING( buff );

   if ( fileName )
   {
      PW_MSG( "  attachment %s",fileName );
   }

   if ( m_sender )
   {
      EMailSender::EMailMessage message;
      EMailSender::FileDescriptior fileDescriptor[ 1 ];

      if ( fileName[ 0 ] == '/' )
      {
         fileDescriptor[ 0 ].filename = &fileName[ 1 ];
      }
      else
      {
         fileDescriptor[ 0 ].filename = fileName;
      }

      fileDescriptor[ 0 ].url = fileName;
      fileDescriptor[ 0 ].mime = "text/plain";
      fileDescriptor[ 0 ].encode64 = false;
      if ( fromSPIFFS )
      {
         fileDescriptor[ 0 ].storageType = EMailSender::EMAIL_STORAGE_TYPE_SPIFFS;
      }
      else
      {
         fileDescriptor[ 0 ].storageType = EMailSender::EMAIL_STORAGE_TYPE_SD;
         if ( ! SD.exists( fileName ) )
         {
            PW_WARN( "%s doesn't exist, not sending email",fileName );
            return false;
         }

      }

      EMailSender::Attachments attachments = { 1, fileDescriptor };

      message.subject = subject;
      message.message = msg;
      message.mime = "text/plain";

      PW_MSG( "Suspend emon task" );
      if ( backgroundHandle != NULL )
      {
         vTaskSuspend( backgroundHandle );
      }

      EMailSender::Response resp = m_sender->send( recipient,message,attachments );

      PW_MSG( "Resume emon task" );
      if ( backgroundHandle != NULL )
      {
         vTaskResume( backgroundHandle );
      }

      if ( !resp.status )
      {
         PW_WARN( "Failed to send email");
      }

      return resp.status;
   }

   END_TIMING;

   return false;
}

AsyncUDP *Networking::s_udp = nullptr;

Networking::Networking()
          : m_emailer( nullptr ),
            m_webServer( nullptr ),
            m_status()
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

   delete s_emoncmsClient;
   delete m_webServer;
   delete m_emailer;
}

bool Networking::startAccessPoint()
{
   String SSID( "HeatPump-Monitor" );

   WiFi.disconnect();

   WiFi.mode( WIFI_AP );
   WiFi.softAP( SSID.c_str() );

   m_status.SSID = SSID;
   m_status.ipAddr = WiFi.softAPIP().toString();

   PW_DEBUG( "AP:" );
   PW_DEBUG( "  SSID : %s",m_status.SSID.c_str() );
   PW_DEBUG( "  IP   : %s",m_status.ipAddr.c_str() );

   // Start the configuration/download server

   m_webServer = new WebServer();
   m_webServer->initialise();

   m_status.mdnsName = String( "heatpump-monitor" );

   if ( !startMDNS() )
   {
      m_status.mdnsName = String();
   }

   return( true );
}

bool Networking::startMDNS()
{
   bool  mdnsOk = true;

   // Start the MDNS service so we can be discovered and configured

   if ( ! MDNS.begin( m_status.mdnsName.c_str() ) )
   {
      PW_WARN( "Failed to setup MDNS responder" );
      mdnsOk = false;
   }
   else
   {
      m_status.mdnsName += String( ".local" );
      PW_MSG( "MDNS :  at %s/manager",m_status.mdnsName.c_str() );

      MDNS.addService( "http","tcp",80 );
   }

   return( mdnsOk );
}


void Networking::initialise()
{
   PW_DEBUG( "Networking::initialise" );

   WiFi.mode( WIFI_STA );

   uint32_t start = millis();

   // Connect to the WiFi network

   WiFi.begin( GET_REGISTRY_STRING( WIFI_SSID ), GET_REGISTRY_STRING( WIFI_PASSWORD ) );
   while (WiFi.status() != WL_CONNECTED && (millis() - start < GET_REGISTRY_INT( WIFI_CONNECT_TIMEOUT )) )
   {
      delay(200);
   }

   if ( WiFi.status() != WL_CONNECTED )
   {
      PW_WARN( "Network not connected" );
      m_status.isConnected = false;
      return;
   }

   m_status.isConnected = true;

   m_status.timeToConnect = ( millis() - start ) / 1000;
   m_status.ipAddr = WiFi.localIP().toString();
   m_status.SSID = String( GET_REGISTRY_STRING( WIFI_SSID ) );

   PW_MSG( "Connected to %s",m_status.SSID.c_str() );
   PW_DEBUG( "  IP : %s",m_status.ipAddr.c_str() );
   PW_DEBUG( "  Autoreconnect : %u", WiFi.getAutoReconnect() );

   if ( userIO )
   {
      userIO->updateLine( 3,"Acquire NTP" );
   }

   acquireNTP();

   // We have connected network, so we can have the emailer

   m_emailer = new Emailer;
   m_emailer->initialise();

   // And now for the emoncms client...

   s_emoncmsClient = new WiFiClientSecure;
   s_emoncmsClient->setCACert( emoncmsCertificate );

   // Start our configuration/download server

   m_webServer = new WebServer();
   m_webServer->initialise();

   // start MDNS

   m_status.mdnsName = String( GET_REGISTRY_STRING( ACCESS_POINT_NAME ) );
   startMDNS();

   // Create new UDP
   s_udp = new AsyncUDP;

   // Now create out background task helper, up to 20 emon messages
   // may be queued.

   dataQueue = xQueueCreate( 20,sizeof( struct EmonData *) );
   if ( ! dataQueue )
   {
      PW_ERROR( "Failed to create XQueue" );
   }
   else
   {
      xTaskCreatePinnedToCore(
         backgroundThread,    // thread fn
         "EmonCMS-Task",      // Name of the task
         10000,               // Stack size in words
         NULL,                // no input params
         0,                   // Priority
         &backgroundHandle,   // handle
         0 );                 // Assign to core 0, core 1 used for main loop
   }
}

bool  Networking::acquireNTP()
{
   struct tm   timeInfo;
   uint32_t    start;

   PW_DEBUG( "Acquiring NTP..." );

   configTzTime( "GMT0BST,M3.5.0/1,M10.5.0",ntpServer );
   start = millis();
   while ( !getLocalTime( &timeInfo ) && (millis() - start < GET_REGISTRY_INT( NTP_UPDATE_TIMEOUT) ) )
   {
      delay( 2000 );
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

   return( m_status.timeToAcquireNTP > -1  );
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

String Networking::getLocalMDNSName()
{
   String ret = m_status.mdnsName;
   int    dotPos;

   dotPos = ret.lastIndexOf( '.' );
   ret.remove( dotPos );

   return( ret );
}


String Networking::getSSID()
{
   return( m_status.SSID );
}

bool Networking::didAcquireNTP()
{
   return ( m_status.timeToAcquireNTP > -1 );
}

bool Networking::sendEmail( const char *recipient,const char *subject,const String &msg )
{
   if ( GET_REGISTRY_INT( SEND_EMAILS ) != 1 )
   {
      PW_DEBUG( "Would send email %s",subject );
      return true;
   }

   if ( m_emailer )
   {
      return m_emailer->sendEmail( recipient,subject,msg );
   }
   return false;
}

bool Networking::sendEmailWithAttachment( const char *recipient,const char *subject,const char *msg,const char *fileName,bool fromSPIFFS )
{
   if ( GET_REGISTRY_INT( SEND_EMAILS ) != 1 )
   {
      PW_DEBUG( "Would send email %s",subject );
      return true;
   }

   if ( m_emailer )
   {
      return m_emailer->sendEmailWithAttachment( recipient,subject,msg,fileName,fromSPIFFS );
   }
   return false;
}

void Networking::sendToEmonCMS( uint32_t emonFeedId,float_t value )
{
   // Create a new data item and add to the queue - the background task will delete
   // the data.

   emonSendRequests++;

   EmonData *data = new EmonData;
   data->emonFeedId = emonFeedId;
   data->value = value;

   if ( dataQueue )
   {
      PW_DEBUG( "EMONCMS:Sending to Q (cpu%u) - %u %.1f",xPortGetCoreID(),data->emonFeedId,data->value );

      if ( xQueueSend( dataQueue,(void *) &data,0 ) != pdTRUE )
      {
         PW_WARN( "EMONCMS:Q full - failed to send" );
         emonQFailures++;
         delete data;
      }
   }
}
void Networking::getEMONStats( uint32_t *sends,uint32_t *qFails, uint32_t *fails )
{
   *sends = emonSendRequests;
   *qFails = emonQFailures;
   *fails = emonSendFailures;
}


WebServer   *Networking::getWebServer()
{
   return( m_webServer );
}

AsyncUDP    *Networking::getUDP()
{
   return( s_udp );
}
