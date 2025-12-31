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
#include "html/reboot_html.h"
#include "html/history_html.h"
#include "html/common_css.h"

#include "src/config/Config.h"
#include "src/core/utils.h"
#include "src/core/Measurement.h"

#include "src/userio/UserIO.h"
#include "Networking.h"

const char status_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>HP Monitor - Live</title>
    <style>
        :root {
            --bg-color: #1a1a1a;
            --card-bg: #2d2d2d;
            --text-main: #e0e0e0;
            --accent: #00adb5;
        }
        body { font-family: sans-serif; background: var(--bg-color); color: var(--text-main); margin: 20px; }
        .container { max-width: 500px; margin: auto; }
        .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid var(--accent); padding-bottom: 10px; margin-bottom: 20px; }
        .card { background: var(--card-bg); padding: 15px; border-radius: 8px; margin-bottom: 15px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
        .row { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid #3d3d3d; }
        .row:last-child { border-bottom: none; }
        .label { color: #aaa; font-size: 0.9em; }
        .value { font-family: 'Courier New', monospace; font-weight: bold; font-size: 1.1em; color: var(--accent); }
        #status { font-size: 0.7em; padding: 3px 8px; border-radius: 4px; text-transform: uppercase; }
        .online { background: #1b5e20; }
        .offline { background: #b71c1c; }
        .status-dot { height: 10px; width: 10px; border-radius: 50%; display: inline-block; margin-right: 8px; vertical-align: middle; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h3>THERMAL LIVE</h3>
            <span id="status" class="offline">Offline</span>
        </div>

        <div class="card">
            <div class="row">
                <span class="label">Updated</span>
                <span class="value" id="time">--:--:--</span>
            </div>
        </div>

        <div class="card">
            <div class="row"><span class="label">HP Flow</span><span class="value" id="hp-flow">--.-</span></div>
            <div class="row"><span class="label">HP Return</span><span class="value" id="hp-return">--.-</span></div>
        </div>

        <div class="card">
            <div class="row">
                <span class="label">Silent Mode</span>
                <span><span id="silent-dot" class="status-dot" style="background:gray"></span><span class="value" id="silent-on">---</span></span>
            </div>
            <div class="row"><span class="label">Inlet</span><span class="value" id="inlet">--.-</span></div>
            <div class="row"><span class="label">Outlet</span><span class="value" id="outlet">--.-</span></div>
        </div>
    </div>

    <script>
        const source = new EventSource('/telemetry');
        const status = document.getElementById('status');

        source.onopen = () => { status.innerText = "Online"; status.className = "online"; };
        source.onerror = () => { status.innerText = "Offline"; status.className = "offline"; };

        source.onmessage = (e) => {
            try {
                const d = JSON.parse(e.data);

                document.getElementById('time').innerText = d.time;
                document.getElementById('hp-flow').innerText = d.temperatures["HP Flow"] ?? "--";
                document.getElementById('hp-return').innerText = d.temperatures["HP Return"] ?? "--";
                document.getElementById('inlet').innerText = d.LG.inlet ?? "--";
                document.getElementById('outlet').innerText = d.LG.outlet ?? "--";

                const silentOn = d.LG["silent-on"];
                document.getElementById('silent-on').innerText = silentOn ? "ON" : "OFF";
                document.getElementById('silent-dot').style.backgroundColor = silentOn ? "#4caf50" : "#f44336";

            } catch (err) { console.error("Parse error", err); }
        };
    </script>
</body>
</html>
)rawliteral";

// this will be executing on the second CPU core, so probably hazards with
// SPIFFS here - should probably mutex it

static fs::SPIFFSFS *s_spiffs = nullptr;

// Need static here for web page template processing access

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
// Additional section for options purposes

String optionsSection;

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
   while( ( count = file.read( scratchBuffer,SCRATCH_BUFFER_SIZE ) ) > 0 )
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
   if(var == "STYLE")
   {
      return( String( common_css ) );
   }

   if(var == "VERSION")
   {
      return( String( k_versionStr ) );
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

  if(var == "EDIT_FILENAME")
  {
     return savePath;
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

   if(var == "OPTIONS_SECTION")
   {
      return optionsSection;
   }

   return String( "N/A" );
}

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Page not found");
}

WebServer::WebServer( Networking *networking )
        : m_webServer( nullptr ),
          m_otaEvents( nullptr ),
          m_statusEvents( nullptr ),
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

   delete m_otaEvents;
   delete m_statusEvents;
   delete m_webServer;
}

// HTML for the check boxes and runtime info

const char *initialSSaverCheckbox = R"raw(
<div class="form-row">
  <span class="form-label">Display screen saver (activates after 5m)</span>
  <input id="ssaver" onchange="checkbox(this)" type="checkbox" checked>
</div>)raw";

const char *initalUdpCheckbox = R"raw(
<div class="form-row">
  <span class="form-label">UDP Debug Active</span>
  <input id="udpdebug" onchange="checkbox(this)" type="checkbox" checked>
</div>)raw";

const char *runtimeInfoButton = R"raw(
<div class="form-row">
  <span class="form-label">Generate Runtime Info (see logs)</span>
  <form method="POST" action="/runtimeinfo" target="_self">
    <input type="submit" value="Run">
  </form>
</div>)raw";

// Generate the options settings.  We need to generate this as any manager
// page refresh/reload will perform a GET for the page so we need to ensure
// the page reflects setting when reloaded.

void WebServer::generateOptionsSection()
{
   optionsSection = initialSSaverCheckbox;

   // Has the screen saver been disabled ?

   int32_t ssaverState = GET_REGISTRY_INT( USERIO_SCREENSAVER );
   if ( ssaverState == 0 )
   {
      optionsSection.replace( "checked","" );
   }

   // Show the debug section, we can add lines here as required

   if ( GET_REGISTRY_INT( WEBPAGE_DEBUG_SECTION ) == 1 )
   {
      String udpCheckbox = initalUdpCheckbox;
      if ( getUdpDebugState() == DEBUG_OFF )
      {
         udpCheckbox.replace( "checked","" );
      }
      optionsSection += udpCheckbox;

      optionsSection += String( runtimeInfoButton );
   }
}

void WebServer::initialise()
{
   PW_DEBUG( "WebServer::initialise" );

   generateOptionsSection();

   setupAsyncServer();
}

int updatePos;
int buffs;

void WebServer::updateClients( const char *data )
{
   if ( m_statusEvents )
   {
      PW_MSG( "SSE update to web clients" );
      m_statusEvents->send( data,NULL,millis() );
   }
}

// --- The Server Handlers ---

void WebServer::setupAsyncServer()
{
   m_webServer = new AsyncWebServer( 80 );

   m_otaEvents = new AsyncEventSource( "/events" );
   m_webServer->addHandler( m_otaEvents );

   m_statusEvents = new AsyncEventSource( "/telemetry" );
   m_webServer->addHandler( m_statusEvents );

   m_webServer->serveStatic( LGSTATUS_LOG_HTML,SPIFFS,LGSTATUS_LOG_HTML );
   m_webServer->serveStatic( LGSTATUS_YESTERDAY,SPIFFS,LGSTATUS_YESTERDAY );

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
      // --- 1. THE RESPONSE HANDLER (Called after upload completes) ---
      // We just send a simple HTTP 200 to acknowledge the AJAX request.
      // The manager UI is being updated separately in Javascript via the
      // Send Server Events (SSE) '/events' stream.

      bool ok = !Update.hasError();
      request->send(200, "text/plain", ok ? "OK" : "FAIL");

      // Release the network mutex now that the transfer is done
      Networking::releaseNetworkMutex();
   },
   [&](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
   {
      // --- 2. THE UPLOAD HANDLER (Called for every chunk of the .bin file) ---

      size_t totalSize = request->contentLength(); // Total file size for percentage

      if (!index)
      {
         bool startedOk = false;
         updatePos = 0;
         buffs = 0;

         if (Networking::takeNetworkMutex(10000) == 1)
         {
            PW_DEBUG( "OTA: Starting update for %s", filename.c_str() );
            m_networking->setUpdateProgress(0, filename, false);

            // Reset UI via SSE
            m_otaEvents->send( "0", "ota_progress", millis() );

            // Start the internal Flash update process
            startedOk = Update.begin( UPDATE_SIZE_UNKNOWN,U_FLASH );
         }

         if (!startedOk)
         {
            PW_ERROR( "Failed to start update" );
            m_networking->setUpdateProgress( -1, filename, false );
            Networking::releaseNetworkMutex();
            // Signal failure to the UI immediately
            m_otaEvents->send( "failed:Could not begin update", "ota_state", millis() );
            return;
         }
      }

      // Copy incoming data into the scratchBuffer
      if ( !Update.hasError() )
      {
         size_t remainingInPacket = len;
         size_t packetOffset = 0;

         while (remainingInPacket > 0)
         {
            size_t spaceInBuffer = SCRATCH_BUFFER_SIZE - updatePos;
            size_t canCopy = (remainingInPacket < spaceInBuffer) ? remainingInPacket : spaceInBuffer;

            memcpy( &scratchBuffer[updatePos], &data[packetOffset], canCopy );

            updatePos += canCopy;
            packetOffset += canCopy;
            remainingInPacket -= canCopy;

            // When scratchBuffer is full, write it to Flash
            if (updatePos == SCRATCH_BUFFER_SIZE)
            {
               Update.write(scratchBuffer, SCRATCH_BUFFER_SIZE);
               buffs++;
               updatePos = 0;

               // Send progress percentage via SSE (e.g., "45")
               if (totalSize > 0)
               {
                  int progress = (index + packetOffset) * 100 / totalSize;
                  char progMsg[8];
                  sprintf( progMsg, "%d", progress );
                  m_otaEvents->send( progMsg, "ota_progress", millis() );
               }
               m_networking->setUpdateProgress( index + packetOffset, filename, false );
            }
         }

         // FINALIZATION: Runs on the last packet
         if (final)
         {
            // Write any remaining bytes left in the buffer
            if (updatePos > 0)
            {
               Update.write(scratchBuffer, updatePos);
            }

            if (Update.end(true))
            {
               // SUCCESS: Tell the browser to start its reboot countdown
               m_otaEvents->send( "100", "ota_progress", millis() );
               m_otaEvents->send( "reboot", "ota_state", millis() );
               m_networking->setUpdateProgress(index + len, filename, true);
               PW_MSG( "OTA Success. Total written: %d bytes", (buffs * SCRATCH_BUFFER_SIZE) + updatePos );
            }
            else
            {
               // FAILURE: Send the specific error message to the browser
               String errorStr = Update.errorString();
               String sseFailMsg = "failed:" + (errorStr.length() ? errorStr : "Flash Error");
               m_otaEvents->send( sseFailMsg.c_str(), "ota_state", millis() );

               m_networking->setUpdateProgress(-1, filename, true);
               PW_ERROR( "OTA Failed %s",errorStr.c_str() );
            }
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

      // reset files (set to defaults) and set factor reset marker, then reboot
      resetFS();
      Config::instance()->setFactoryReset();

      request->send(200);

      delay( 500 );

      ESP.restart();
   });

   m_webServer->on("/checkbox", HTTP_GET, [this](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }

      if (request->hasParam("item") && request->hasParam("state"))
      {
         handleCheckbox( request->getParam("item")->value(), request->getParam("state")->value() );
      }

      request->send( 200,"text/plain","OK" );
   });

   m_webServer->on(m_hiddenPage.c_str(), HTTP_GET, [](AsyncWebServerRequest *request)
   {
      if ( GET_REGISTRY_INT( DEBUGPAGE_HWRESET ) == 1 )
      {
         hwReset();
      }

      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }

      if ( Networking::takeNetworkMutex( 100 ) == 1 )
      {
         getRunTimeInfo();
         Networking::releaseNetworkMutex();

         if ( GET_REGISTRY_INT( DEBUGPAGE_CPU0_TASKWDT ) == 1 )
         {
            // cause task watchog
            uint32_t start = millis();
            while( millis() - start < 180000 )
            {
               buffs++;
            }
         }
      }
      request->send(200);
   });

   m_webServer->on("/runtimeinfo", HTTP_POST, [](AsyncWebServerRequest *request)
   {
      getRunTimeInfo();
      debugSensorNameMap();

      extern bool loopTestRequired;

      loopTestRequired = true;

      request->send(204);
   });

   m_webServer->on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }

      request->send_P(200, "text/html", reboot_html,processor);

      // if this we're in Access Point mode then allow immediate reboot

      if ( s_networking->inAPMode() )
      {
         reboot();
      }

      setRebootRequired();

   });

   m_webServer->on("/json", HTTP_GET, [this](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }

     Measurement *measurement = Measurement::instance();
      bool sent = false;

      if ( measurement )
      {
         char *json = measurement->getSampleJSON();

         if ( json )
         {
            request->send( 200,"application/json",json );
            free( json );
            sent = true;
         }
      }

      if ( !sent  )
      {
         request->send( 500,"text/plain","JSON Generation Failed" );
      }
   });

   m_webServer->on("/status", HTTP_GET, [this](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }
      request->send( 200,"text/html",status_html );
   });

   m_webServer->on("/history", HTTP_GET, [this](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }
      request->send_P(200, "text/html", history_html,processor);
   });

   m_webServer->onNotFound(notFound);

   m_webServer->begin();
}

void  WebServer::handleCheckbox( const String &item,const String &state )
{
   bool active = (state == "1" );

   PW_MSG( "Checkbox item %s state (%s)",item.c_str(),state.c_str() );

   if ( item == "ssaver" )
   {
      if ( active )
      {
         SET_REGISTRY( USERIO_SCREENSAVER,1 );
      }
      else
      {
         SET_REGISTRY( USERIO_SCREENSAVER,0 );
      }
   }
   else if ( item == "udpdebug" )
   {
      setUdpDebugState( (active ? DEBUG_ON : DEBUG_OFF ) );

   }

   generateOptionsSection();
}

