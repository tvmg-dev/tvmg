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

#include "WebServer.h"
#include "html/edit_html.h"
#include "html/manager_html.h"
#include "html/ok_html.h"
#include "html/failed_html.h"

#include "Config.h"
#include "utils.h"

#include "UserIO.h"

// this will be executing on the second CPU core, so probably hazards with
// SPIFFS here - should probably mutex it

fs::SPIFFSFS *s_spiffs = nullptr;

const char* http_username = "admin";
const char* http_password = "admin";

String allowedExtensionsForEdit = "txt, dat, def";

#define  DEFAULT_EXTENSION ".def"
bool   showDefaultFiles = false;

String filesDropdownOptions = "";
String textareaContent = "";
String savePath = "";
String savePathInput = "";

const char* param_delete_path = "delete_path";
const char* param_edit_path = "edit_path";
const char* param_download_path = "download_path";
const char* param_edit_textarea = "edit_textarea";
const char* param_save_path = "save_path";

String convertFileSize(const size_t bytes)
{
   if(bytes < 1024)
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

uint8_t  fileBuff[ 4096 ];

void  replaceFile( const char *origFile,const char *newFile )
{
   if ( !s_spiffs )
      return;

   // First remove the original file, then we'll copy from the new file
   // back to the original

   s_spiffs->remove( origFile );

   File ipFile = s_spiffs->open( newFile,"r" );
   if ( ipFile )
   {
      File opFile = s_spiffs->open( origFile,"w" );
      if ( opFile )
      {
         PW_MSG( "Replacing %s with %s",origFile,newFile );

         int count;
         while( ( count = ipFile.read( fileBuff,sizeof( fileBuff ) ) ) > 0 )
         {
            PW_DEBUG( "from %s read %d",newFile,count );
            opFile.write( fileBuff,count );
         }
         opFile.close();
      }

      ipFile.close();
   }
}

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

            replaceFile( newName.c_str(),fileName.c_str() );
         }

         entry.close();
      }
   }

   root.close();
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
      listenFiles += "<tr><td id=\"first_td_th\">Library: ";
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
      if ( showDefaultFiles || !strstr( file.name(),DEFAULT_EXTENSION ) )
      {
         listenFiles += "<tr><td id=\"first_td_th\">";
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
   while( ( count = file.read( fileBuff,sizeof( fileBuff ) ) ) > 0 )
   {
      for ( int i = 0; i < count; i++ )
      {
         fileContent += static_cast<char>( fileBuff[ i ] );
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
     return String( VERSION_STR );
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
  return String();
}

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Page not found");
}

WebServer::WebServer()
        : m_webServer( nullptr ),
          m_userIO( nullptr ),
          m_downloadFile()

{
   PW_DEBUG( "WebServer()" );

   Config   *config = Config::instance();
   s_spiffs = config->getSPIFFS();

   assert( s_spiffs != 0 );

   if ( GET_REGISTRY_INT( SHOW_DEFAULT_FILES ) > 0 )
   {
      showDefaultFiles = true;
   }
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

void WebServer::setupAsyncServer()
{
   m_webServer = new AsyncWebServer( 80 );

   m_webServer->on("/manager", HTTP_GET, [](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }
      request->send_P(200, "text/html", manager_html, processor);
   });

   m_webServer->on("/update", HTTP_POST, [&](AsyncWebServerRequest *request)
   {
      bool rebooting = !Update.hasError();

      AsyncWebServerResponse *response = request->beginResponse(200, "text/html", rebooting ? ok_html : failed_html);

      response->addHeader("Connection", "close");
      request->send(response);

      if ( rebooting )
      {
         PW_DEBUG( "PW 2s to restart" );
         delay( 2 * 1000 );
         ESP.restart();
      }
   },
   [&](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
   {
      if(!index)
      {
         if ( m_userIO )
         {
            m_userIO->setFirmwareUpdateInProgress( true );
            m_userIO->clear();
            m_userIO->updateLine( 5,"Updating..." );

            char line[ 128 ];
            snprintf( line,MAX_OLED_COLUMNS," %s",filename.c_str() );
            m_userIO->updateLine( 1,line );
         }

         PW_MSG( "Updating with %s",filename.c_str() );

         if ( !Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH) )
         {
            PW_ERROR( "Failed to start update" );

            if ( m_userIO )
            {
               m_userIO->updateLine( 5,"FAILED !!" );
               delay( 2000 );
               m_userIO->setFirmwareUpdateInProgress( false );
            }

            return request->send(400, "text/plain", "OTA could not begin");
         }
      }

      if(!Update.hasError())
      {
         if ( m_userIO )
         {
            static int i = 0;
            char  progress[] = ".oOo";
            char  line[ 2 ];

            line[ 0 ] = progress[ i++ % 4 ];
            line[ 1 ] = 0;

            m_userIO->updateLine( 5,line,false );
         }

         if(Update.write(data, len) != len)
         {
            Update.printError(Serial);
         }
      }

      if(final)
      {
         if(Update.end(true))
         {
            if ( m_userIO )
            {
               m_userIO->updateLine( 5,"Completed Ok" );
            }

            PW_MSG( "Finished update");
         }
      else
      {
         Update.printError(Serial);
      }
   }
   });

   m_webServer->on("/upload", HTTP_POST, [](AsyncWebServerRequest *request)
   {
      request->send(200);
   }, uploadFile);

   m_webServer->on("/edit", HTTP_GET, [](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }
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
         AsyncWebParameter* param = request->getParam( 0 );
         if ( param && ( param->name() == String( param_edit_textarea ) ) )
         {
            PW_DEBUG( "Saving %d bytes to %s : contents :",param->value().length(),savePath.c_str() );

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

            START_DEBUG;
               // Limit to 2K for debug
               String dbg = param->value().substring( 0,2047 );
               PW_DEBUG( "%s",dbg.c_str() );
            END_DEBUG
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

   m_webServer->on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request)
   {
      request->send(200);
      delay( 2 * 1000 );
      ESP.restart();
   });

   m_webServer->onNotFound(notFound);

   m_webServer->begin();
}

void WebServer::setUserIO( UserIO *userIO )
{
   m_userIO = userIO;
}


