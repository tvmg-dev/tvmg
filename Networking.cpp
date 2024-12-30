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

std::recursive_mutex  networkingMutex;

static WiFiClientSecure *s_emoncmsClient = nullptr;
static HTTPClient       *s_webClient = nullptr;
static String           s_emoncmsApiKey;

#define  KEEP_ALIVE_MS                 6000
#define  DEFAULT_WIFI_CONNECT_TIMEOUT  60000
#define  DEFAULT_NTP_UPDATE_TIMEOUT    60000

static uint32_t emonSendRequests = 0,emonQFailures = 0, emonSendFailures = 0;

// Example feed data insertion -
// https://emoncms.org/feed/insert.json?id=0&time=0&value=100&apikey=***REMOVED***

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
               delay( random( 100,250 ) );
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

   // possible fix for lack of emails, take mutex before doing anything
   // still have UDP traffic ??

   SCOPE_LOCK_NW_MUTEX;

   // If we've not processed a send request for KEEP_ALIVE_MS then force
   // the connection to drop. Maybe unecessary but don't want to try and
   // keep a permanent connection.  Really would like to start a connection
   // when we do the batch update and then close it, but we don't have an
   // API to handle that yet (and it would need to be thread safe).

   if ( millis() - lastSentMillis > KEEP_ALIVE_MS && s_webClient )
   {
      PW_DEBUG( "EMONCMS: Closing connection (%u ms elapsed)",millis() - lastSentMillis );
      Networking::releaseWebClient();

      // take this opportunity to show some stats
      PW_MSG( "EMONCMS: Sent %u, failed [Q,E] [%u,%u]",emonSendRequests,emonQFailures,emonSendFailures );
   }

   // Create a new HTTPClient if we need to, and we try to connect to the
   // emon host, but no GET request yet

   if ( !s_webClient )
   {
      PW_MSG( "EMONCMS: Create new HTTPClient" );
      s_webClient = new HTTPClient();
      s_webClient->setReuse( true );
      PW_DEBUG( "EMONCMS: begin HTTPClient" );

      if ( ! s_webClient->begin( *s_emoncmsClient,"https://emoncms.org" ) )
      {
         PW_ERROR( "EMONCMS: Can't start HTTPClient" );
         Networking::releaseWebClient();
         emonSendFailures++;
         return;
      }
   }

   // Form our path for the GET request based on the feed Id
   time( &utc );

   snprintf( path,128,"/feed/insert.json?id=%u&time=%d&value=%.2f&apikey=%s",emonFeedId,utc,value,s_emoncmsApiKey.c_str() );

   PW_MSG( "EMONCMS: Will %s",path );

   // Send the GET request - which will force a connect if necessary

   s_webClient->setURL( path );
   int httpCode = s_webClient->GET();

   PW_DEBUG( "EMONCMS: GET response %d",httpCode );

   // Check success from HTTP perspective, then check success from emon REST perspective

#if 0
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
#endif

   lastSentMillis = millis();
}

class Emailer
{
public:
   Emailer( Networking *networking );
   ~Emailer();

   void initialise();
   bool sendEmail( const char *recipient,const char *subject,const String &msg );
   bool sendEmailWithAttachment( const char *recipient,const char *subject,const char *msg,const char *fileName,bool fromSPIFFS );

private:
   String      m_ipDesc;
   EMailSender *m_sender;
   Networking  *m_networking;
};

Emailer::Emailer( Networking *networking )
       : m_ipDesc(),
         m_sender( nullptr ),
         m_networking( networking )
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

   // Setup IP descriptor

   m_ipDesc = String( account );

   int   lastAmper = m_ipDesc.lastIndexOf( '@' ) + 1;

   if ( lastAmper < m_ipDesc.length() )
   {
      m_ipDesc = m_ipDesc.substring( lastAmper );
      PW_DEBUG( "Email public IP Desc. %s",m_ipDesc.c_str() );

      m_sender->setPublicIpDescriptor( m_ipDesc.c_str() );
   }
}

bool Emailer::sendEmail( const char *recipient,const char *subject,const String &msg )
{
   if ( m_sender )
   {
      EMailSender::EMailMessage message;
      String newSubject = m_networking->getLocalMDNSName() + " : " + String( subject );

      message.subject = newSubject;
      message.message = msg.c_str();
      message.mime = "text/plain";

      PW_MSG( "Sending to %s [%s]",recipient,subject );

      EMailSender::Response resp;

      {
         // remove the web client before we send, cautionary to avoid
         // networking conflicts

         SCOPE_RELEASE_WEB_CLIENT;
         resp = m_sender->send( recipient,message );
      }

      if ( !resp.status )
      {
         PW_WARN( "Failed to send email %s, %s", resp.code.c_str(),resp.desc.c_str() );
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

      String newSubject = m_networking->getLocalMDNSName() + " : " + String( subject );

      message.subject = newSubject;
      message.message = msg;
      message.mime = "text/plain";

      EMailSender::Response resp;

      {
         // remove the web client before we send, cautionary to avoid
         // networking conflicts

         SCOPE_RELEASE_WEB_CLIENT;
         resp = m_sender->send( recipient,message,attachments );
      }

      if ( !resp.status )
      {
         PW_WARN( "Failed to send email %s, %s", resp.code.c_str(),resp.desc.c_str() );
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
            m_userIO( nullptr ),
            m_status(),
            m_emonCert(),
            m_willSendEmails( false ),
            m_isUpdating( false ),
            m_isEditing( false )
{
   PW_DEBUG( "Networking::Networking()" );
   PW_MSG( "Networking Startup" );

   // set status to defaults, not connected etc.

   m_status.ipAddr = "";
   m_status.mdnsName = "";
   m_status.isConnected = false;
   m_status.timeToAcquireNTP = -1;
   m_status.timeToConnect = 0;

   if ( GET_REGISTRY_INT( SEND_EMAILS ) == 1 )
   {
      m_willSendEmails = true;
   }

   // read the emocms API key from config, and the public certificate
   // for the emon webserver from /emoncms.pub

   char *emonKey = GET_REGISTRY_STRING( EMONCMS_APIKEY );
   if ( emonKey )
   {
      s_emoncmsApiKey = String( emonKey );
   }

   fs::SPIFFSFS   *spiffs = Config::instance()->getSPIFFS();

   File file = spiffs->open( "/emoncms.pub",FILE_READ );
   if ( !file )
   {
      PW_WARN( "/emoncms.pub is missing" );
   }
   else
   {
      while( file.available() )
      {
         m_emonCert += static_cast<char>( file.read() );
      }
      file.close();
   }
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
   String SSID( "ThermaV-Monitor" );

   WiFi.disconnect();

   WiFi.mode( WIFI_AP );
   WiFi.softAP( SSID.c_str() );

   m_status.SSID = SSID;
   m_status.ipAddr = WiFi.softAPIP().toString();

   PW_DEBUG( "AP:" );
   PW_DEBUG( "  SSID : %s",m_status.SSID.c_str() );
   PW_DEBUG( "  IP   : %s",m_status.ipAddr.c_str() );

   // Start the configuration/download server

   m_webServer = new WebServer( this );
   m_webServer->initialise();

   m_status.mdnsName = String( "tvm-init" );

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

#define  OLED_DEBUG( x ) \
do {\
  userIO->updateLine( 5,x ); \
  delay( 1000 ); \
} while( 0 )

void Networking::initialise()
{
   PW_DEBUG( "Networking::initialise" );

   WiFi.mode( WIFI_STA );

   uint32_t start = millis();

   // Connect to the WiFi network

   int wifiTimeout = GET_REGISTRY_INT( WIFI_CONNECT_TIMEOUT );
   if ( wifiTimeout == -1 )
   {
      wifiTimeout = DEFAULT_WIFI_CONNECT_TIMEOUT;
   }

   WiFi.begin( GET_REGISTRY_STRING( WIFI_SSID ), GET_REGISTRY_STRING( WIFI_PASSWORD ) );
   while (WiFi.status() != WL_CONNECTED && (millis() - start < wifiTimeout) )
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

   m_emailer = new Emailer( this );
   m_emailer->initialise();

   // And now for the emoncms client...

   s_emoncmsClient = new WiFiClientSecure;
   s_emoncmsClient->setCACert( m_emonCert.c_str() );

   // Start our configuration/download server

   m_webServer = new WebServer( this );
   m_webServer->initialise();

   // start MDNS

   m_status.mdnsName = String( GET_REGISTRY_STRING( MDNS_NAME ) );
   startMDNS();

   // Create new UDP
   s_udp = new AsyncUDP;

   // Now create out background task helper, up to 50 emon messages
   // may be queued.

   dataQueue = xQueueCreate( 50,sizeof( struct EmonData *) );
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

   int ntpTimeout = GET_REGISTRY_INT( NTP_UPDATE_TIMEOUT );
   if ( ntpTimeout == -1 )
   {
      ntpTimeout = DEFAULT_NTP_UPDATE_TIMEOUT;
   }

   while ( !getLocalTime( &timeInfo ) && (millis() - start < ntpTimeout ) )
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
   if ( !m_willSendEmails )
   {
      PW_DEBUG( "Not sending email %s",subject );
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
   if ( !m_willSendEmails )
   {
      PW_DEBUG( "Not sending email %s",subject );
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
   if ( dataQueue )
   {
      // Create a new data item and add to the queue - the background task will delete
      // the data.

      emonSendRequests++;

      EmonData *data = static_cast<EmonData *>( malloc( sizeof( EmonData ) ) );
      if ( !data )
      {
         PW_ERROR( "Failed to allocate EmonData" );
         emonQFailures++;
         return;
      }

      data->emonFeedId = emonFeedId;
      data->value = value;

      PW_DEBUG( "EMONCMS:Sending to Q (cpu%u) - %u %.1f %d",xPortGetCoreID(),data->emonFeedId,data->value,ESP.getFreeHeap() / 1024 );

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

void  Networking::releaseWebClient()
{
   if ( s_webClient )
   {
      PW_DEBUG( "Release WebClient" );
      s_webClient->end();
      delete s_webClient;
      s_webClient = nullptr;
   }
}

std::recursive_mutex   &Networking::getNetworkingMutex()
{
   return networkingMutex;
}

bool  Networking::isBusy()
{
   return ( m_isUpdating | m_isEditing );
}

void  Networking::setUpdateProgress( int index,const String &filename,bool finished )
{
   if ( finished )
   {
      m_isUpdating = false;
      if ( m_userIO )
      {
         m_userIO->updateLine( 5,"Completed" );
      }
      return;
   }

   m_isUpdating = true;

   // Update UserIO if available

   if ( ! m_userIO )
   {
      return;
   }

   if ( !index )
   {
      m_userIO->clear();
      m_userIO->updateLine( 1,"Updating :" );

      char line[ 128 ];
      snprintf( line,MAX_OLED_COLUMNS," %s",filename.c_str() );
      m_userIO->updateLine( 2,line );
   }
   else if ( index == -1 )
   {
      m_userIO->updateLine( 5,"FAILED !!" );
      delay( 2000 );
   }
   else
   {
      static int i = 0;
      char  progress[] = ".oOo";
      char  line[ 2 ];

      line[ 0 ] = progress[ i++ % 4 ];
      line[ 1 ] = 0;

      m_userIO->updateLine( 5,line,false );
   }
}

void  Networking::setUserIO( UserIO *userIO )
{
   m_userIO = userIO;
}

void  Networking::serverHome()
{
   m_isEditing = false;
}

void  Networking::startFileEdit( const String &filename )
{
   char line[ MAX_OLED_COLUMNS + 1 ];

   m_isEditing = true;

   m_userIO->clear();
   m_userIO->updateLine( 1,"Editing :" );

   snprintf( line,MAX_OLED_COLUMNS,"%s",filename.c_str() );
   m_userIO->updateLine( 2,line );

   // To release memory we release the http client which returns ~ 50KB
   // to the heap.

   SCOPE_RELEASE_WEB_CLIENT;
}

