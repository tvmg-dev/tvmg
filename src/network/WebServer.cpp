//
// Initially from https://www.hackster.io/myhomethings/esp32-web-updater-and-spiffs-file-manager-cf8dc5
// 20th Dec. 2023
// No license attribution provided in the source code
//
// Subsequently reworked to fix edit (now using POST, not GET with long
// url's) and added download capablity.
//

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <time.h>

#include "WebServer.h"
#include "html/edit_html.h"
#include "html/manager_html.h"
#include "html/ok_html.h"
#include "html/failed_html.h"

#include "src/config/Config.h"
#include "src/core/utils.h"

#include "src/userio/UserIO.h"
#include "Networking.h"

// this will be executing on the second CPU core, so probably hazards with
// SPIFFS here - should probably mutex it

static fs::SPIFFSFS *s_spiffs = nullptr;

// Need static here for web page template processing accee

static Networking *s_networking = nullptr;

// should have password in a file somewhere for user modification

const char* http_username = "admin";
const char* http_password = "admin";

#define  DEFAULT_EXTENSION ".def"

bool   showAllFiles = false;
static char hiddenExtensions[][ 5 ] = { ".pub",".hid",DEFAULT_EXTENSION };

String allowedExtensionsForEdit = "txt, dat, def, pub";

String filesDropdownOptions = "";
String textareaContent = "";
String savePath = "";
String savePathInput = "";

const char* param_delete_path = "delete_path";
const char* param_edit_path = "edit_path";
const char* param_download_path = "download_path";
const char* param_edit_textarea = "edit_textarea";
const char* param_save_path = "save_path";

//----------------------------------------------------------------------
// Additional section for debug purposes, usually not defined

// #define  MANAGER_DEBUG_SECTION

#ifdef MANAGER_DEBUG_SECTION
const char debugSection[] = R"raw(
<div id="spacer_5"></div>
<fieldset><legend>Debug Section</legend>
 <div id="spacer_5"></div>
 <form method="POST" action="/debug" target="self_page">
   <table><tr><td>
   <p>Debug</p>
   <p>Debug2</p>
   <p>Debug3</p>
   </td><td>
   <input type="submit" id="submit" value="Debug">
   </td></tr></table>
 </form>
 <div id="spacer_5"></div>
</fieldset>
)raw";
#else
const char debugSection[] = "";
#endif

//----------------------------------------------------------------------

void resetFS()
{
   // Find default files and copy to their 'dat' equivalent - crude as
   // can't map to any other extension, would need a map somewhere but
   // good enough for now.

   File root = s_spiffs->open( "/" );

   while (true)
   {
      File entry = root.openNextFile();
      if (!entry)
      {
         break;
      }

      if ( !entry.isDirectory() )
      {
         String fileName = String( "/" ) + entry.name();
         if ( fileName.lastIndexOf( DEFAULT_EXTENSION ) != -1 )
         {
            String newName = fileName;
            newName.replace( DEFAULT_EXTENSION,".dat" );

            replaceSpiffsFile( newName,fileName );
         }

         entry.close();
      }
   }

   root.close();

   // write out we've performed a reset via the server, then set factory reset

   Config::instance()->setPersistentInt( k_rebootType,SERVER_RESET );
   Config::instance()->setFactoryReset();
}

bool  isHiddenExtension( const String &filename )
{
   bool isHidden = false;

   for ( int i = 0; i < sizeof(hiddenExtensions) /  sizeof(hiddenExtensions[ 0 ]); i++ )
   {
      if ( filename.indexOf( hiddenExtensions[ i ] ) != -1 )
      {
         isHidden = true;
         break;
      }
   }

   String msg = filename;
   String add;
   msg += String( " is " );
   if ( isHidden )
   {
      add = "hidden";
   }
   else
   {
      add = "visible";
   }
   msg += add;
   PW_DEBUG( msg.c_str() );

   return isHidden;
}

String convertFileSize(const size_t bytes)
{
   if(bytes < 10240)
   {
      return String(bytes) + " B";
   }
   else if (bytes < 1048576)
   {
      return String(bytes / 1024.0) + " kB";
   }
   else if (bytes < 1073741824)
   {
      return String(bytes / 1048576.0) + " MB";
   }
}

String listDir(fs::FS *fs, const char * dirname, uint8_t levels)
{
  filesDropdownOptions = "";
  String listenFiles = "<table><tr><th id=\"first_td_th\">Folder: </th><th>";
  listenFiles += dirname;
  listenFiles += "</th></tr>";

  File root = fs->open(dirname);
  String fail = "";
  if(!root)
  {
    fail = " the folder cannot be opened";
    return fail;
  }
  if(!root.isDirectory())
  {
    fail = " this is not a folder";
    return fail;
  }

  File file = root.openNextFile();
  while(file)
  {
    if(file.isDirectory())
    {
      listenFiles += "<tr><td>Library: ";
      listenFiles += file.name();

      filesDropdownOptions += "<option value=\"";
      filesDropdownOptions += file.name();
      filesDropdownOptions += "\">";
      filesDropdownOptions += file.name();
      filesDropdownOptions += "</option>";

      listenFiles += "</td><td> - </td></tr>";

      if(levels)
      {
        listDir(s_spiffs, file.name(), levels -1);
      }
    }
    else
    {
      if ( showAllFiles || !isHiddenExtension( file.name() ) )
      {
         listenFiles += "<tr><td>";
         listenFiles += file.name();

         filesDropdownOptions += "<option value=\"";
         filesDropdownOptions += file.name();
         filesDropdownOptions += "\">";
         filesDropdownOptions += file.name();
         filesDropdownOptions += "</option>";

         listenFiles += " </td><td>\tSize: ";
         listenFiles += convertFileSize(file.size());
         listenFiles += "</td></tr>";
      }

      file.close();
    }
    file = root.openNextFile();
  }
  listenFiles += "</table>";
  return listenFiles;
}

String readFile(fs::FS *fs, const char * path)
{
   String fileContent = "";
   File file = fs->open(path, "r");

   if(!file || file.isDirectory())
   {
      return fileContent;
   }

   int count;
   while( ( count = file.read( scratchBuffer,scratchBufferSize ) ) > 0 )
   {
      for ( int i = 0; i < count; i++ )
      {
         fileContent += static_cast<char>( scratchBuffer[ i ] );
      }
   }
   file.close();

   return fileContent;
}

void writeFile(fs::FS *fs, const char * path, const char * message)
{
   File file = fs->open(path, "w");

   if(!file)
   {
      return;
   }

   file.print(message);
   file.close();
}

void uploadFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if(!index)
  {
    request->_tempFile = s_spiffs->open("/" + filename, "w");
  }
  if(len)
  {
    request->_tempFile.write(data, len);
  }
  if(final)
  {
    request->_tempFile.close();
    request->redirect("/manager");
  }
}

String processor(const String& var)
{
   if(var == "VERSION")
   {
      return( String( VERSION_STR ) );
   }

   if(var == "UPTIME")
   {
      String uptime( "Uptime " );

      if ( s_networking )
      {
         Networking::Status state = s_networking->getStatus();

         time_t currentTime;

         time( &currentTime );
         uint32_t  secondsDiff = difftime( currentTime,state.startTime );

         uptime += String( secondsDiff / ( 24 * 3600 ),DEC );

         char timeStr[ 24 ];
         snprintf( timeStr,sizeof(timeStr)," days, %02u:%02u (hh:mm)",
                        (secondsDiff / 3600) % 24,(secondsDiff / 60) % 60 );

         uptime += String( timeStr );
      }
      return uptime;
   }

  if(var == "IPADDR" )
  {
     String info( "IP: ");

     if ( s_networking )
     {
        Networking::Status state = s_networking->getStatus();
        info += state.ipAddr;
     }

     return info;
  }

  if(var == "WIFI" )
  {
    String wifiStr;

    if ( s_networking )
    {
      char line[ 32 ];

      Networking::Status state = s_networking->getStatus();

      snprintf( line,sizeof(line),"RSSI: %d dBm",state.RSSI );

      wifiStr = String( line );
   }

    return wifiStr;
  }

  if(var == "MODBUS")
  {
    uint32_t   sends,fails;

    getModbusStats( &sends,&fails );
    char line[ 36 ];

    if ( sends )
    {
       snprintf( line,sizeof(line),"MB: %u / %u",sends,fails );
    }
    else
    {
       snprintf( line,sizeof(line),"MB: N/A" );
    }

    return String( line );
  }

  if(var == "EMON")
  {
    char line[ 36 ];
    String emonStr( "EM: N/A" );

    if ( s_networking && GET_REGISTRY_INT( UPDATE_EMONCMS ) == 1 )
    {
      Networking::Status state = s_networking->getStatus();

      if ( state.emonSent )
      {
        snprintf( line,sizeof(line),"EM: %u / %u",state.emonSent,state.emonFails );
        emonStr = String( line );
      }
    }

    return emonStr;
  }

  if(var == "ALLOWED_EXTENSIONS_EDIT")
  {
    return allowedExtensionsForEdit;
  }
  if(var == "SPIFFS_FREE_BYTES")
  {
    return convertFileSize((s_spiffs->totalBytes() - s_spiffs->usedBytes()));
  }

  if(var == "SPIFFS_USED_BYTES")
  {
    return convertFileSize(s_spiffs->usedBytes());
  }

  if(var == "SPIFFS_TOTAL_BYTES")
  {
    return convertFileSize(s_spiffs->totalBytes());
  }

  if(var == "LISTEN_FILES")
  {
    return listDir(s_spiffs, "/", 0);
  }

  if(var == "EDIT_FILES")
  {
    String editDropdown = "<select name=\"edit_path\" id=\"edit_path\">";
    editDropdown += "<option value=\"choose\">Select file to edit</option>";
    editDropdown += filesDropdownOptions;
    editDropdown += "</select>";
    return editDropdown;
  }

  if(var == "DELETE_FILES")
  {
    String deleteDropdown = "<select name=\"delete_path\" id=\"delete_path\">";
    deleteDropdown += "<option value=\"choose\">Select file to delete</option>";
    deleteDropdown += filesDropdownOptions;
    deleteDropdown += "</select>";
    return deleteDropdown;
  }

  if(var == "DOWNLOAD_FILES")
  {
    String downloadDropdown = "<select name=\"download_path\" id=\"download_path\">";
    downloadDropdown += "<option value=\"choose\">Select file to download</option>";
    downloadDropdown += filesDropdownOptions;
    downloadDropdown += "</select>";
    return downloadDropdown;
  }

  if(var == "TEXTAREA_CONTENT")
  {
    return textareaContent;
  }

  if(var == "SAVE_PATH_INPUT")
  {
    return "";
  }

  if(var == "DEBUG_SECTION")
  {
     return String( debugSection );
  }

  return String( "N/A" );
}

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Page not found");
}

WebServer::WebServer( Networking *networking )
        : m_webServer( nullptr ),
          m_networking( networking ),
          m_hiddenPage(),
          m_downloadFile()

{
   PW_DEBUG( "WebServer()" );

   Config   *config = Config::instance();
   s_spiffs = config->getSPIFFS();

   assert( s_spiffs != 0 );

   if ( GET_REGISTRY_INT( SHOW_ALL_FILES ) > 0 )
   {
      showAllFiles = true;
   }

   s_networking = m_networking;

   // Assign a random page for debugging if not set in config

   m_hiddenPage = GET_REGISTRY_STRING( HIDDEN_WEB_PAGE );
   if ( m_hiddenPage.indexOf( "debug" ) == -1 )
   {
      randomSeed( analogRead( 0 ) );
      int   ra = random( 1000000 );

      m_hiddenPage = "/dbg-";
      m_hiddenPage += String( ra,DEC );
   }

   PW_DEBUG( "Debug Page at %s",m_hiddenPage.c_str() );
}

WebServer::~WebServer()
{
   PW_DEBUG( "~WebServer()" );
}

void WebServer::initialise()
{
   PW_DEBUG( "WebServer::initialise" );

   setupAsyncServer();
}

//#define  DEBUG_OTA_BUFFER

int      updatePos;
int      buffs;

void WebServer::setupAsyncServer()
{
   m_webServer = new AsyncWebServer( 80 );

   m_webServer->on("/manager", HTTP_GET, [this](AsyncWebServerRequest *request)
   {
      PW_DEBUG( "/manager request" );

      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }
      request->send_P(200, "text/html", manager_html, processor);
   });

   m_webServer->on("/update", HTTP_POST, [&](AsyncWebServerRequest *request)
   {
      bool ok = !Update.hasError();

      AsyncWebServerResponse *response = request->beginResponse(200, "text/html", ok ? ok_html : failed_html);

      response->addHeader("Connection", "close");
      request->send(response);

      Networking::releaseNetworkMutex();
   },
   [&](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
   {
      if(!index)
      {
         bool  startedOk = false;
         updatePos = 0;
         buffs = 0;

         if ( Networking::takeNetworkMutex( 10000 ) == 1 )
         {
            PW_DEBUG( "server - update with %s",filename.c_str() );

            m_networking->setUpdateProgress( 0,filename,false );

            startedOk = Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH);
         }

         if ( !startedOk )
         {
            m_networking->setUpdateProgress( -1,filename,false );

            Networking::releaseNetworkMutex();

            return request->send(400, "text/plain", "OTA could not begin");
         }
      }

      if(!Update.hasError())
      {
         int   copyLen;

         // We copy as many bytes into our scratchBuffer as we can

         if ( updatePos + len <= scratchBufferSize )
         {
            copyLen = len;
         }
         else
         {
            copyLen = scratchBufferSize - updatePos;
         }

#ifdef DEBUG_OTA_BUFFER
         PW_DEBUG( "curr %d, add %d",updatePos,copyLen );
#endif

         memcpy( &scratchBuffer[ updatePos ],data,copyLen );

         // Now set out next update position in our buffer (therefore modulo buff size)

         updatePos += copyLen;
         updatePos %= scratchBufferSize;

         // If our update position is zero then we need to write the buffer to file

         if ( ! updatePos )
         {
            m_networking->setUpdateProgress( index,filename,false );

            buffs++;

#ifdef DEBUG_OTA_BUFFER
            PW_DEBUG( "Writing buffer... %d",buffs );
#endif

            Update.write( scratchBuffer,scratchBufferSize );

            // Now need to set a new update position based on the bytes we didn't copy over
            // and of course copy these bytes into the start of the buffer

#ifdef DEBUG_OTA_BUFFER
            PW_DEBUG( "new tmpBuff from %d - %d bytes",copyLen,len-copyLen );
#endif
            memcpy( scratchBuffer,&data[ copyLen ],len - copyLen );
            updatePos = len - copyLen;
         }

         if ( final )
         {
            PW_MSG( "Final size %d, final buffer %d",index + len,buffs * scratchBufferSize + updatePos );
            Update.write( scratchBuffer,updatePos );
         }
      }

      if( final )
      {
         if ( !Update.end( true ) )
         {
            Update.printError(Serial);
            m_networking->setUpdateProgress( -1,filename,true );
         }
         else
         {
            m_networking->setUpdateProgress( index,filename,true );
         }
      }
   });

   m_webServer->on("/upload", HTTP_POST, [](AsyncWebServerRequest *request)
   {
      // don't send a response as we will send a redirect in the uploadFile method
   }, uploadFile);

   m_webServer->on("/edit", HTTP_GET, [this](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }
      uint32_t largestFreeBlock = largestFreeInternalBlock();

      PW_DEBUG( "Largest free heap %d",largestFreeBlock );

      String fileName = "/" + request->getParam(param_edit_path)->value();

      PW_DEBUG( "Editing %s",fileName.c_str() );
      savePath = fileName;
      textareaContent = readFile(s_spiffs, fileName.c_str());
      request->send_P(200, "text/html", edit_html, processor);
   });

   m_webServer->on("/save", HTTP_POST, [](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }

      if ( request->params() == 1 )
      {
         const AsyncWebParameter* param = request->getParam( static_cast<size_t> (0) );
         if ( param && ( param->name() == String( param_edit_textarea ) ) )
         {
            PW_DEBUG( "Saving %d bytes to %s",param->value().length(),savePath.c_str() );

#if 0
            // code to replace CR+LF with just LF
            char newLineCR[] = { '\n','\r','\0' };
            char newLine[] = { '\r','\0' };

            String nlCR( newLineCR );
            String nl( newLine );

            String str = param->value();
            str.replace( newLineCR,newLine );
#endif

            writeFile( s_spiffs, savePath.c_str(), param->value().c_str() );
         }
      }

      request->redirect("/manager");
   });

   m_webServer->on("/delete", HTTP_GET, [](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }

      String fileName = "/" + request->getParam(param_delete_path)->value();
      PW_DEBUG( "Deleting %s",fileName.c_str() );

      if ( ! s_spiffs->remove(fileName.c_str()) )
      {
         PW_WARN( "Failed to delete %s",fileName.c_str() );
      }

      request->redirect("/manager");
   });

   m_webServer->on("/download", HTTP_GET, [this](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }

      String fileName = "/" + request->getParam(param_download_path)->value();
      PW_DEBUG( "Downloading %s",fileName.c_str() );

      m_downloadFile = s_spiffs->open( fileName,"r" );

      AsyncWebServerResponse *response = request->beginChunkedResponse("text/plain", [ & ](uint8_t *buffer, size_t maxLen, size_t index) mutable -> size_t {

         // Needed to limit to 4 KiB otherwise larger files (> ~4 KiB) failed to
         // download, a fault it the webserver library perhaps.  The first maxlen passed
         // in is typically ~ 5600 bytes
         if ( maxLen > 4096 )
         {
            maxLen = 4096;
         }

         int len = m_downloadFile.read( buffer,maxLen );
         if ( !len )
         {
            m_downloadFile.close();
         }
         return len;
      });

      // Need to add the content-disposition header with filename attachment, and header
      // for chunked transfer.

      String headerValue( "attachment; filename=\"" );
      headerValue += request->getParam(param_download_path)->value();
      headerValue += "\"";

      response->addHeader("Content-Disposition",headerValue.c_str() );
      response->addHeader("Transfer-Encoding","chunked");

      request->send(response);
   });

   m_webServer->on("/reset", HTTP_POST, [](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }

      PW_WARN( "Resetting..." );

      resetFS();

      request->send(200);

      delay( 500 );

      ESP.restart();
   });

   m_webServer->on("/checkbox", HTTP_GET, [](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }

      if (request->hasParam("item") && request->hasParam("state"))
      {
         String msg = request->getParam("item")->value();
         msg = request->getParam("state")->value();

         getRunTimeInfo();
         if ( msg == "1" )
         {
            SET_REGISTRY( USERIO_SCREENSAVER,1 );
         }
         else
         {
            SET_REGISTRY( USERIO_SCREENSAVER,0 );
         }
      }

      request->send( 200,"text/plain","OK" );
   });

   m_webServer->on(m_hiddenPage.c_str(), HTTP_GET, [](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }

      if ( Networking::takeNetworkMutex( 100 ) == 1 )
      {
         getRunTimeInfo();
         Networking::releaseNetworkMutex();
#if 1
         // cause task watchog
         uint32_t start = millis();
         while( millis() - start < 10000 )
         {
            buffs++;
         }
#endif
      }
      request->send(200);
   });

   m_webServer->on("/debug", HTTP_POST, [](AsyncWebServerRequest *request)
   {
      if ( Networking::takeNetworkMutex( 100 ) == 1 )
      {
         getRunTimeInfo();
         Networking::releaseNetworkMutex();
      }

      request->redirect("/manager");
   });

   // if we reboot then we also clear down the factor reset marker if it exists

   m_webServer->on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request)
   {
      request->send(200);

      Config::instance()->clearFactoryReset();
      Config::instance()->setPersistentInt( k_rebootType,SERVER_REBOOT );

      delay( 1500 );
      ESP.restart();
   });

   m_webServer->onNotFound(notFound);

   m_webServer->begin();
}


