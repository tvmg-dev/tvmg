/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include <SD.h>
#include <FS.h>

// For ReadyMail configuration
//#define ENABLE_DEBUG
#define ENABLE_SMTP
#define ENABLE_FS
#define READYMAIL_TIME_SOURCE time(nullptr)
#include <ReadyMail.h>

#include "src/config/Config.h"
#include "src/core/Measurement.h"

#include "src/network/Networking.h"
#include "src/network/WebServer.h"

// ISRG Root X1 cert used by emoncms

const char iSRGRootX1Cert[] = R"rawliteral(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE----- )rawliteral";


const char* ntpServer = "pool.ntp.org";

static WiFiClientSecure *s_emoncmsClient = nullptr;
static HTTPClient       *s_webClient = nullptr;
static String           s_emoncmsApiKey;

#define  KEEP_ALIVE_MS                 6000
#define  DEFAULT_WIFI_CONNECT_TIMEOUT  60000
#define  DEFAULT_NTP_UPDATE_TIMEOUT    60000


#define  EMON_ACQUIRE_MUTEX_MS         20000    // time allowed for emon task to get the nw mutex
#define  EMAIL_ACQUIRE_MUTEX_MS        10000    // time allowed for email to get the nw mutex

//----------------------------------------------------------------------

static uint32_t emonSendRequests = 0,emonQFailures = 0, emonSendFailures = 0;

// Example feed data insertion -
// https://emoncms.org/feed/insert.json?id=0&time=0&value=100&apikey=abcdef

struct   EmonData {
   uint32_t emonFeedId;
   float_t  value;
};

static TaskHandle_t  backgroundHandle = NULL;
static QueueHandle_t dataQueue = NULL;
static char          threadBuff[ 128 ];     // to reduce stack use

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
            snprintf( threadBuff,sizeof(threadBuff),"EMONCMS: Processed Q for [%u], %.2f",data->emonFeedId,data->value );
            START_TIMING( threadBuff );

            TVMG_DEBUG( "EMONCMS: Received from Q (cpu%u) - [%u], %.2f",xPortGetCoreID(),data->emonFeedId,data->value );

            if ( sendData )
            {
               // We only send if we can take the network mutex as we may be
               // releasing the web-client in sendToEmonCMS().  If we can't get
               // the mutex (e.g. OTA download occuring) then we simply don't send.

               if ( Networking::takeNetworkMutex( EMON_ACQUIRE_MUTEX_MS ) == 1 )
               {
                  sendToEmonCMS( data->emonFeedId,data->value );
                  Networking::releaseNetworkMutex();
               }
            }
            else
            {
               TVMG_MSG( "EMONCMS: Would send to emon [%u], %.2f",data->emonFeedId,data->value );
               delay( random( 100,250 ) );
            }

            delete data;
            END_TIMING;
         }
         else
         {
            TVMG_DEBUG( "EMONCMS: Nothing received from Q (cpu%u)",xPortGetCoreID() );
         }
      }
      else
      {
         TVMG_WARN( "EMONCMS: Waiting for Q creation" );
         delay( 2000 );
      }
   }
}

void  sendToEmonCMS( uint32_t emonFeedId,float_t value )
{
   static uint32_t   lastSentMillis = 0;
   time_t      utc;

   // If we've not processed a send request for KEEP_ALIVE_MS then force
   // the connection to drop. Maybe unecessary but don't want to try and
   // keep a permanent connection.  Really would like to start a connection
   // when we do the batch update and then close it, but we don't have an
   // API to handle that yet (and it would need to be thread safe).

   if ( millis() - lastSentMillis > KEEP_ALIVE_MS && s_webClient )
   {
      TVMG_DEBUG( "EMONCMS: Closing connection (%u ms elapsed)",millis() - lastSentMillis );
      Networking::releaseWebClient();

      // take this opportunity to show some stats
      TVMG_MSG( "EMONCMS: Sent %u, failed [Q,E] [%u,%u]",emonSendRequests,emonQFailures,emonSendFailures );
   }

   // Create a new HTTPClient if we need to, and we try to connect to the
   // emon host, but no GET request yet

   if ( !s_webClient )
   {
      TVMG_MSG( "EMONCMS: Create new HTTPClient" );
      s_webClient = new HTTPClient();
      s_webClient->setReuse( true );
      TVMG_DEBUG( "EMONCMS: begin HTTPClient" );

      if ( ! s_webClient->begin( *s_emoncmsClient,"https://emoncms.org" ) )
      {
         TVMG_ERROR( "EMONCMS: Can't start HTTPClient" );
         Networking::releaseWebClient();
         emonSendFailures++;
         return;
      }
   }

   // Form our path for the GET request based on the feed Id
   time( &utc );

   snprintf( threadBuff,sizeof(threadBuff),"/feed/insert.json?id=%u&time=%d&value=%.2f&apikey=%s",emonFeedId,utc,value,s_emoncmsApiKey.c_str() );

   TVMG_MSG( "EMONCMS: Will %s",threadBuff );

   // Send the GET request - which will force a connect if necessary

   s_webClient->setURL( threadBuff );
   int httpCode = s_webClient->GET();

   TVMG_DEBUG( "EMONCMS: GET response %d",httpCode );

   // Check success from HTTP perspective, then check success from emon REST perspective

   if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
   {
      String payload = s_webClient->getString();
      if ( strstr( payload.c_str(),"false" ) )
      {
         TVMG_ERROR( "EMONCMS: emon failure [%s]",payload.c_str() );
         emonSendFailures++;
      }
   }
   else
   {
      emonSendFailures++;
      TVMG_ERROR( "EMONCMS: GET failed [%s]",s_webClient->errorToString(httpCode).c_str() );
   }

   lastSentMillis = millis();
}

//----------------------------------------------------------------------
// Emailer class

// The ReadyMail buffer needs some padding so can't be the full size of the
// scratch buffer

static_assert(READYMAIL_EXTERNAL_BUFF_SIZE <= SCRATCH_BUFFER_SIZE - 32, "ReadyMail buffer too large");

// Need a global file as used across callbacks as reference

File readyMailFile;

// RAII for the network mutex where we take the network mutex and release
// the web client

struct NetworkGuard
{
   NetworkGuard( int ms )
   {
      isTaken = Networking::takeNetworkMutex( ms );
      if ( isTaken )
      {
         Networking::releaseWebClient();
      }
   }

   ~NetworkGuard() { if ( isTaken ) Networking::releaseNetworkMutex(); readyMailFile.close(); }

   bool isTaken;
};

using namespace ReadyMailCallbackNS;

class Emailer
{
public:
   Emailer();
   ~Emailer();

   void initialise( const String &mdnsName );
   bool sendEmail( const char *recipient,const char *subject,const String &msg );
   bool sendEmailWithAttachment( const char *recipient,const char *subject,const String &msg,const char *fileName,bool fromSD );
   bool sendEmailWithFileAsBody( const char *recipient,const char *subject,const char *fileName,bool fromSD );

private:
   static void fileCallbackForFS(File &file, const char *path, readymail_file_operating_mode mode);
   static void fileCallbackForSD(File &file, const char *path, readymail_file_operating_mode mode);

   String   setupConnection( const NetworkGuard &guard,const char *recipient,const char *subject,SMTPMessage *smtpMsg );
   void     closeConnection();

   String   getNameFromEMailAddress( const String &recipient );

   WiFiClientSecure  *sslClient;
   SMTPClient        *smtp;
   String            m_host;
   String            m_account;
   String            m_password;
   String            m_sender;
   int               m_port;
};

Emailer::Emailer()
       : sslClient( nullptr ),
         smtp( nullptr ),
         m_host(),
         m_account(),
         m_password(),
         m_sender(),
         m_port( 465 )
{
   TVMG_DEBUG( "Emailer::Emailer()" );
}

Emailer::~Emailer()
{
   TVMG_DEBUG( "Emailer::~Emailer()" );
}

void  Emailer::initialise( const String &mdnsName )
{
   TVMG_MSG( "Emailer initialise" );

   m_host = String( GET_REGISTRY_STRING( SMTP_HOST ) );
   m_port = GET_REGISTRY_INT( SMTP_PORT );
   m_account = String( GET_REGISTRY_STRING( ACCOUNT_EMAIL ) );
   m_password = String( GET_REGISTRY_STRING( ACCOUNT_PASSWORD ) );
   m_sender = mdnsName;

   TVMG_DEBUG( "Email : Host %s [%d] - %s %s",m_host,m_port,m_account.c_str(),m_password.c_str() );
}

void Emailer::fileCallbackForFS(File &file, const char *path, readymail_file_operating_mode mode)
{
   bool isValid = false;

   TVMG_DEBUG( "FS File callback %s %d",path,mode );

   switch (mode)
   {
      case readymail_file_mode_open_read:
         file.close();
         readyMailFile.close();
         if ( tvmgFileSys.exists( path ) )
         {
            readyMailFile = tvmgFileSys.open( path,FILE_OPEN_MODE_READ );

            if ( readyMailFile && readyMailFile.size() )
            {
                  isValid = true;
                  file = readyMailFile;

            }
         }

         if ( !isValid )
         {
            TVMG_ERROR( "FS File callback failed %s %d",path,mode );
         }
         else
         {
            TVMG_DEBUG( "FS File callback ok %s %d",path,mode );
         }
         break;
      default:
         TVMG_WARN( "Igoring non-read calls from ReadyMail on %s",path );
         break;
   }
}

void Emailer::fileCallbackForSD(File &file, const char *path, readymail_file_operating_mode mode)
{
   bool isValid = false;

   TVMG_DEBUG( "SD File callback %s %d",path,mode );

   switch (mode)
   {
      case readymail_file_mode_open_read:
         file.close();
         readyMailFile.close();
         if ( SD.exists( path ) )
         {
            readyMailFile = SD.open( path,FILE_OPEN_MODE_READ );

            if ( readyMailFile && readyMailFile.size() )
            {
                  isValid = true;
                  file = readyMailFile;

            }
         }

         if ( !isValid )
         {
            TVMG_ERROR( "SD File callback failed %s %d",path,mode );
         }
         else
         {
            TVMG_DEBUG( "SD File callback ok %s %d",path,mode );
         }
         break;
      default:
         TVMG_WARN( "Igoring non-read calls from ReadyMail on %s",path );
         break;
   }
}

String   Emailer::getNameFromEMailAddress( const String &recipient )
{
   int atIndex = recipient.indexOf('@');

   if ( atIndex != -1 )
   {
      return( recipient.substring( 0,atIndex ) );
   }
   return String();
}

void smtpCb(SMTPStatus status)
{
    if (status.progress.available)
        TVMG_DEBUG("ReadyMail[smtp][%d] Uploading file %s, %d %% completed\n", status.state,
                         status.progress.filename.c_str(), status.progress.value);
    else
        TVMG_DEBUG("ReadyMail[smtp][%d]%s\n", status.state, status.text.c_str());
}

String   Emailer::setupConnection( const NetworkGuard &guard,const char *recipient,const char *subject,SMTPMessage *smtpMsg )
{
   String sendString;

   if ( guard.isTaken )
   {
      START_TIMING( (String( "Connecting to " ) + m_host) );
      sslClient = new WiFiClientSecure;
      if ( !sslClient )
      {
         TVMG_ERROR( "Failed to create WiFiClientSecure" );
      }
      else
      {
         sslClient->setInsecure();
         smtp = new SMTPClient(*sslClient);

         if ( !smtp )
         {
            TVMG_ERROR( "Failed to create SMTPClient" );
            delete sslClient;
            sslClient = nullptr;
         }
         else if ( !smtp->connect( m_host,m_port,smtpCb ) )
         {
            TVMG_ERROR( "Failed to connect to %s (port %d)",m_host.c_str(),m_port );
         }
         else if ( !smtp->authenticate( m_account,m_password, readymail_auth_password) )
         {
            TVMG_ERROR( "Failed to authenticate with %s (password 5s)",m_account.c_str(),m_password.c_str() );
         }
         else
         {
            smtpMsg->headers.add( rfc822_from, m_sender + " <"+ m_account + ">" );
            smtpMsg->headers.add( rfc822_to, getNameFromEMailAddress( recipient ) + " <" + recipient + ">" );
            smtpMsg->headers.add( rfc822_subject,subject );

            sendString.reserve( 128 );
            sendString = "Send email 'SUBJECT' to RECIPIENT";
            sendString.replace( "SUBJECT",subject );
            sendString.replace( "RECIPIENT",getNameFromEMailAddress( recipient ) );

            TVMG_DEBUG( sendString.c_str() );

         }
      }
      END_TIMING;
   }

   if ( !sendString.length() )
   {
      closeConnection();
   }

   return sendString;
}

void Emailer::closeConnection()
{
   if ( smtp )
   {
      smtp->stop();
   }

   delete smtp;
   delete sslClient;

   smtp = nullptr;
   sslClient = nullptr;
}

bool Emailer::sendEmail( const char *recipient,const char *subject,const String &msg )
{
   NetworkGuard guard( EMAIL_ACQUIRE_MUTEX_MS );
   SMTPMessage smtpMsg;
   bool sentOk = false;

   String logMsg = setupConnection( guard,recipient,subject,&smtpMsg );
   if ( logMsg.length() )
   {
      START_TIMING( logMsg );

      smtpMsg.text.body( msg );
      if ( smtp->send( smtpMsg ) )
      {
         TVMG_MSG( "%s - success",logMsg.c_str() );
         sentOk = true;
      }
      else
      {
         TVMG_ERROR("%s - failed",logMsg.c_str() );
      }

      END_TIMING;

      closeConnection();
   }

   return sentOk;
}

bool Emailer::sendEmailWithAttachment( const char *recipient,const char *subject,const String &msg,const char *fileName,bool fromSD )
{
   if ( !recipient || !subject || !fileName )
   {
      return false;
   }

   if ( !(fromSD ? SD.exists( fileName ) : tvmgFileSys.exists( fileName )) )
   {
      TVMG_WARN( "%s doesn't exist",fileName );
      return false;
   }

   NetworkGuard guard( EMAIL_ACQUIRE_MUTEX_MS );
   SMTPMessage smtpMsg;
   bool sentOk = false;

   String connectMsg = setupConnection( guard,recipient,subject,&smtpMsg );
   if ( connectMsg.length() )
   {
      String logMsg = connectMsg + ", sending " + fileName;
      START_TIMING( logMsg );

      smtpMsg.text.body( msg );

      Attachment attachment;
      attachment.filename = fileName;           // name in the email header, strip leading '/'
      attachment.filename.remove( 0,fileName[ 0 ] == '/' ? 1 : 0 );
      attachment.mime = "text/plain";
      attachment.name = attachment.filename;    // what the file will be shown as

      if ( fromSD )
      {
         attachment.attach_file.callback = fileCallbackForSD;
      }
      else
      {
         attachment.attach_file.callback = fileCallbackForFS;
      }

      attachment.attach_file.path = fileName;
      smtpMsg.attachments.add(attachment, attach_type_attachment);

      if ( smtp->send( smtpMsg ) )
      {
         TVMG_MSG( "%s - success",logMsg.c_str() );
         sentOk = true;
      }
      else
      {
         TVMG_ERROR("%s - failed",logMsg.c_str() );
      }

      END_TIMING;

      closeConnection();
   }

   return sentOk;
}

bool Emailer::sendEmailWithFileAsBody( const char *recipient,const char *subject,const char *fileName,bool fromSD )
{
   if ( !recipient || !subject || !fileName )
   {
      return false;
   }

   if ( !(fromSD ? SD.exists( fileName ) : tvmgFileSys.exists( fileName )) )
   {
      TVMG_WARN( "%s doesn't exist",fileName );
      return false;
   }

   NetworkGuard guard( EMAIL_ACQUIRE_MUTEX_MS );
   SMTPMessage smtpMsg;
   bool sentOk = false;

   String connectMsg = setupConnection( guard,recipient,subject,&smtpMsg );
   if ( connectMsg.length() )
   {
      String logMsg = connectMsg + ", sending " + fileName;
      START_TIMING( logMsg );

      if ( fromSD )
      {
         smtpMsg.html.body( fileName,fileCallbackForSD );
      }
      else
      {
         smtpMsg.html.body( fileName,fileCallbackForFS );
      }

      if ( smtp->send( smtpMsg ) )
      {
         TVMG_MSG( "%s - success",logMsg.c_str() );
         sentOk = true;
      }
      else
      {
         TVMG_ERROR("%s - failed",logMsg.c_str() );
      }

      END_TIMING;

      closeConnection();
   }

   return sentOk;
}

//----------------------------------------------------------------------

AsyncUDP *Networking::s_udp = nullptr;
AsyncUDP *Networking::s_listenUdp = nullptr;

uint32_t Networking::s_mutexAcquiredMillis;
SemaphoreHandle_t Networking::s_networkMutex = NULL;
Indicator *Networking::s_indicator = nullptr;

Networking::Networking( NetworkingInfoCallback infoCallback )
          : m_emailer( nullptr ),
            m_webServer( nullptr ),
            m_status(),
            m_willSendEmails( false ),
            m_hasUpdated( false ),
            m_infoCallback( infoCallback )
{
   TVMG_DEBUG( "Networking::Networking()" );
   TVMG_MSG( "Networking Startup" );

   if ( !m_infoCallback )
   {
      TVMG_ERROR( "No info callback set" );
      delay( 1000 );
      abort();
   }

   if ( !s_networkMutex )
   {
      s_networkMutex = xSemaphoreCreateRecursiveMutex();
      s_indicator = Indicator::getIndicator( Indicator::NETWORK,1 );

   }

   // set status to defaults, not connected etc.

   m_status.ipAddr = "";
   m_status.mdnsName = "";
   m_status.timeToAcquireNTP = -1;
   m_status.timeToConnect = 0;
   m_status.emonFails = 0;
   m_status.emonSent = 0;
   m_status.emonQFails = 0;
   m_status.state = NOTSTARTED;

   if ( GET_REGISTRY_INT( SEND_EMAILS ) == 1 )
   {
      m_willSendEmails = true;
   }

   // read the emocms API key from config

   const char *emonKey = GET_REGISTRY_STRING( EMONCMS_APIKEY );
   if ( emonKey )
   {
      s_emoncmsApiKey = String( emonKey );
   }
}

Networking::~Networking()
{
   TVMG_DEBUG( "Networking::~Networking()" );

   delete s_emoncmsClient;
   delete m_webServer;
   delete m_emailer;
}

bool Networking::startAccessPoint()
{
   String SSID( "ThermaV-Monitor" );

   m_status.mdnsName = String( "tvmg-init" );

   m_infoCallback( STARTING_AP,String( "Starting AP " ) + m_status.mdnsName );

   WiFi.disconnect();

   WiFi.mode( WIFI_AP );
   WiFi.softAP( SSID.c_str() );

   m_status.SSID = SSID;
   m_status.ipAddr = WiFi.softAPIP().toString();

   TVMG_DEBUG( "AP:" );
   TVMG_DEBUG( "  SSID : %s",m_status.SSID.c_str() );
   TVMG_DEBUG( "  IP   : %s",m_status.ipAddr.c_str() );

   // Start the configuration/download server

   m_webServer = new WebServer( this );
   m_webServer->initialise();

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
      TVMG_WARN( "Failed to setup MDNS responder" );
      mdnsOk = false;
   }
   else
   {
      m_status.mdnsName += String( ".local" );
      TVMG_MSG( "MDNS :  at %s/manager",m_status.mdnsName.c_str() );

      MDNS.addService( "http","tcp",80 );
   }

   return( mdnsOk );
}

void Networking::initialise()
{
   TVMG_DEBUG( "Networking::initialise" );

   WiFi.mode( WIFI_STA );

   uint32_t start = millis();

   // Connect to the WiFi network

   int wifiTimeout = GET_REGISTRY_INT( WIFI_CONNECT_TIMEOUT );
   if ( wifiTimeout == -1 )
   {
      wifiTimeout = DEFAULT_WIFI_CONNECT_TIMEOUT;
   }

   // set hostname as mdns name + last 2 hex digits of MAC address

   uint8_t  mac[ 6 ];
   String   hostName( String( GET_REGISTRY_STRING( MDNS_NAME ) ) );

   char line [ 32 ];

   // get mac for STA mode
   esp_wifi_get_mac( WIFI_IF_STA,mac );

   hostName += "-";
   snprintf( line,sizeof(line),"%02x%02x",mac[ 4 ],mac[ 5 ] );
   hostName += line;

   TVMG_MSG( "Set hostname %s",hostName.c_str() );
   if ( !WiFi.hostname( hostName ) )
   {
      TVMG_ERROR( "Failed to set hostname" );
   }

   m_infoCallback( STARTING_STA, String( "Starting STA " ) + String( GET_REGISTRY_STRING( WIFI_SSID ) ) );

   WiFi.begin( GET_REGISTRY_STRING( WIFI_SSID ), GET_REGISTRY_STRING( WIFI_PASSWORD ) );
   while (WiFi.status() != WL_CONNECTED && (millis() - start < wifiTimeout) )
   {
      delay(200);
   }

   if ( WiFi.status() != WL_CONNECTED )
   {
      TVMG_WARN( "Network not connected" );
      m_status.state = DISCONNECTED;
      m_infoCallback( m_status.state,"Failed to connect to WiFi" );
      return;
   }


   m_status.timeToConnect = ( millis() - start ) / 1000;
   m_status.ipAddr = WiFi.localIP().toString();
   m_status.SSID = String( GET_REGISTRY_STRING( WIFI_SSID ) );

   TVMG_MSG( "Connected to %s",m_status.SSID.c_str() );
   TVMG_DEBUG( "  IP : %s",m_status.ipAddr.c_str() );
   TVMG_DEBUG( "  Autoreconnect : %u", WiFi.getAutoReconnect() );

   m_status.state = CONNECTED_STA;
   m_infoCallback( m_status.state,m_status.SSID + " : connected" );

   acquireNTP();

   // We have connected network, so we can have the emailer

   m_emailer = new Emailer();
   m_emailer->initialise( GET_REGISTRY_STRING( MDNS_NAME ) );

   // And now for the emoncms client...

   s_emoncmsClient = new WiFiClientSecure;
   if ( GET_REGISTRY_INT( EMON_INSECURE ) == 1 )
   {
      TVMG_WARN( "Setting WiFi client to insecure mode" );
      s_emoncmsClient->setInsecure();
   }
   else
   {
      s_emoncmsClient->setCACert( iSRGRootX1Cert );
   }

   // Start our configuration/download server

   m_webServer = new WebServer( this );
   m_webServer->initialise();

   // start MDNS

   m_status.mdnsName = String( GET_REGISTRY_STRING( MDNS_NAME ) );
   startMDNS();

   // Create new UDP
   s_udp = new AsyncUDP;
   s_listenUdp = new AsyncUDP;

   // Now create out background task helper, up to 50 emon messages
   // may be queued.

   dataQueue = xQueueCreate( 50,sizeof( struct EmonData *) );
   if ( ! dataQueue )
   {
      TVMG_ERROR( "Failed to create XQueue" );
   }
   else
   {
      xTaskCreatePinnedToCore(
         backgroundThread,    // thread fn
         "EmonCMS-Task",      // Name of the task
         (6 * 1024),          // Stack size in bytes
         NULL,                // no input params
         0,                   // Priority
         &backgroundHandle,   // handle
         1 );                 // Assign to core 1, core 1 used for main loop
   }
}

bool  Networking::acquireNTP()
{
   struct tm   timeInfo;
   uint32_t    start;

   TVMG_DEBUG( "Acquiring NTP..." );

   m_infoCallback( ACQUIRING_NTP,"Acquiring NTP" );

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
      TVMG_WARN( "NTP not available" );
      m_status.timeToAcquireNTP = -1;
   }
   else
   {
      m_status.timeToAcquireNTP = ( millis() - start ) / 1000;
   }

   time( &m_status.startTime );

   return( m_status.timeToAcquireNTP > -1  );
}

const Networking::Status   &Networking::getStatus()
{
   // Update WiFi info

   isConnected();

   m_status.RSSI = WiFi.RSSI();

   // now Emon stats, the emonSendFailures could be updated in the emontask
   // but its not critical data so not protecting

   m_status.emonSent = emonSendRequests;
   m_status.emonFails = emonSendFailures;
   m_status.emonQFails = emonQFailures;

   return m_status;
}

bool  Networking::isConnected()
{
   bool connected = (WiFi.status() == WL_CONNECTED);
   if ( connected )
   {
      m_status.state = ( inAPMode() ? CONNECTED_AP : CONNECTED_STA );
   }
   else
   {
      m_status.state = DISCONNECTED;
   }

   return connected;
}

bool Networking::inAPMode()
{
   return ( WiFi.getMode() == WIFI_MODE_AP );
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
      TVMG_DEBUG( "Not sending email %s",subject );
      return true;
   }

   if ( m_emailer )
   {
      return m_emailer->sendEmail( recipient,subject,msg );
   }
   return false;
}

bool Networking::sendEmailWithAttachment( const char *recipient,const char *subject,const String &msg,const char *fileName,bool fromSD )
{
   if ( !m_willSendEmails )
   {
      TVMG_DEBUG( "Not sending email %s",subject );
      return true;
   }

   if ( m_emailer )
   {
      return m_emailer->sendEmailWithAttachment( recipient,subject,msg,fileName,fromSD );
   }
   return false;
}

bool Networking::sendEmailWithFileAsBody( const char *recipient,const char *subject,const char *fileName,bool fromSD )
{
   if ( !m_willSendEmails )
   {
      TVMG_DEBUG( "Not sending email %s",subject );
      return true;
   }

   if ( m_emailer )
   {
      return m_emailer->sendEmailWithFileAsBody( recipient,subject,fileName,fromSD );
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
         TVMG_ERROR( "Failed to allocate EmonData" );
         emonQFailures++;
         return;
      }

      data->emonFeedId = emonFeedId;
      data->value = value;

      TVMG_DEBUG( "EMONCMS:Sending to Q (cpu%u) - %u %.1f %d",xPortGetCoreID(),data->emonFeedId,data->value,ESP.getFreeHeap() / 1024 );

      if ( xQueueSend( dataQueue,(void *) &data,0 ) != pdTRUE )
      {
         TVMG_WARN( "EMONCMS:Q full - failed to send" );
         emonQFailures++;
         delete data;
      }
   }
}

WebServer   *Networking::getWebServer()
{
   return( m_webServer );
}

AsyncUDP    *Networking::getUDP()
{
   return( s_udp );
}

AsyncUDP    *Networking::getListenUDP()
{
   return( s_listenUdp );
}

void  Networking::releaseWebClient()
{
   if ( s_webClient )
   {
      TVMG_DEBUG( "Release WebClient" );
      s_webClient->end();
      delete s_webClient;
      s_webClient = nullptr;
   }
}

void  Networking::setUpdateProgress( int percentComplete,const String &filename,bool finished )
{
   static bool notifyUpdate = false;
   static int lastPercentage = -1;

   if ( percentComplete != 0 )
   {
      notifyUpdate = false;
   }

   // If percentComplete is -1 then an error has occured, if finished is false then 
   // failed to start, otherwise incomplete..
   if ( percentComplete == -1 )
   {
      if ( !finished )
      {
         m_infoCallback( OTA_FAILED,"Failed to start update" );
      }
      else
      {
         m_infoCallback( OTA_FAILED,"Failed to complete update" );
      }
   }
   else if ( finished )
   {
      m_hasUpdated = true;
      m_infoCallback( OTA_COMPLETE,"Update Completed" );
   }
   else if ( !percentComplete )
   {
      if ( !notifyUpdate )
      {
         TVMG_MSG( "Update with %s",filename.c_str() );
         
         m_hasUpdated = false;
         m_infoCallback( OTA_STARTED,filename );
         notifyUpdate = true;
      }
   }
   else if ( percentComplete != lastPercentage )
   {
      TVMG_DEBUG( "OTA %d complete",percentComplete );
      m_infoCallback( OTA_PROGRESS, String( percentComplete ) );
   }
}

bool  Networking::hasUpdated()
{
   return m_hasUpdated;
}

int Networking::takeNetworkMutex( int ms )
{
   if ( ! s_networkMutex )
   {
      TVMG_WARN( "No nw mutex" );
      return -1;
   }

   uint32_t startMillis;

   TVMG_DEBUG( "Take n/w mutex" );

   startMillis = millis();
   int ok = xSemaphoreTakeRecursive( s_networkMutex,ms * portTICK_PERIOD_MS);

   if ( ok != pdTRUE )
   {
      TVMG_WARN( "Failed to take nw mutex" );
   }
   else
   {
      s_mutexAcquiredMillis = millis();
      TVMG_DEBUG( "n/w mutex took %d ms",s_mutexAcquiredMillis - startMillis );
      if ( s_indicator )
      {
         s_indicator->on();
      }
   }

   return( ok == pdTRUE );
}

void  Networking::releaseNetworkMutex()
{
   if ( s_networkMutex )
   {
      TVMG_DEBUG( "n/w mutex held for %d",millis() - s_mutexAcquiredMillis );
      xSemaphoreGiveRecursive( s_networkMutex );

      if ( s_indicator )
      {
         s_indicator->off();
      }
   }
}

NetworkMutexGuard::NetworkMutexGuard( uint32_t timeoutMS )
{
   m_acquired = ( Networking::takeNetworkMutex( timeoutMS ) == 1 ? true : false );
}

NetworkMutexGuard::~NetworkMutexGuard()
{
   if ( m_acquired )
   {
      Networking::releaseNetworkMutex();
   }
}

bool NetworkMutexGuard::acquired()
{
   return m_acquired;
}
