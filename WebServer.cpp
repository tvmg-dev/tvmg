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

fs::SPIFFSFS *s_spiffs;

const char* http_username = "admin";
const char* http_password = "admin";

const char* host = "esp32-filemanager";

String allowedExtensionsForEdit = "txt, dat";

String filesDropdownOptions = "";
String textareaContent = "";
String savePath = "";
String savePathInput = "";

const char* param_delete_path = "delete_path";
const char* param_edit_path = "edit_path";
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

String listDir(fs::FS *fs, const char * dirname, uint8_t levels)
{
  filesDropdownOptions = "";
  String listenFiles = "<table><tr><th id=\"first_td_th\">List the library: </th><th>";
  listenFiles += dirname;
  listenFiles += "</th></tr>";

  File root = fs->open(dirname);
  String fail = "";
  if(!root)
  {
    fail = " the library cannot be opened";
    return fail;
  }
  if(!root.isDirectory())
  {
    fail = " this is not a library";
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
      listenFiles += "<tr><td id=\"first_td_th\">File: ";
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
    file = root.openNextFile();
  }
  listenFiles += "</table>";
  return listenFiles;
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
    editDropdown += "<option value=\"new\">New text file</option>";
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

  if(var == "TEXTAREA_CONTENT")
  {
    return textareaContent;
  }

  if(var == "SAVE_PATH_INPUT")
  {
    if(savePath == "/new.txt")
    {
      savePathInput = "<input type=\"text\" id=\"save_path\" name=\"save_path\" value=\"" + savePath + "\" >";
    }
    else
    {
      savePathInput = "";
    }
    return savePathInput;
  }
  return String();
}

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Page not found");
}

String readFile(fs::FS *fs, const char * path)
{
   String fileContent = "";
   File file = fs->open(path, "r");

   if(!file || file.isDirectory())
   {
      return fileContent;
   }

   while(file.available())
   {
      fileContent+=String((char)file.read());
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

WebServer::WebServer()
        : m_webServer( nullptr ),
          m_userIO( nullptr )

{
   PW_DEBUG( "WebServer()" );

   Config   *config = Config::instance();
   s_spiffs = config->getSPIFFS();

   assert( s_spiffs != 0 );
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
//      PW_DEBUG( "on update: %d %d %u",index,len,final );
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
            Serial.println(convertFileSize(index + len));
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
      String inputMessage = "/" + request->getParam(param_edit_path)->value();

      PW_DEBUG( "Editing %s",inputMessage.c_str() );
      if(inputMessage == "/new")
      {
         textareaContent = "";
         savePath = "/new.txt";
      }
      else
      {
         savePath = inputMessage;
         textareaContent = readFile(s_spiffs, inputMessage.c_str());
      }
      request->send_P(200, "text/html", edit_html, processor);
   });

   m_webServer->on("/save", HTTP_GET, [](AsyncWebServerRequest *request)
   {
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }
      String inputMessage = "";
      if (request->hasParam(param_edit_textarea))
      {
         inputMessage = request->getParam(param_edit_textarea)->value();
      }
      if (request->hasParam(param_save_path))
      {
         savePath = request->getParam(param_save_path)->value();
      }
      PW_DEBUG( "Saving to %s : contents :",savePath.c_str() );
      PW_DEBUG( "%s",inputMessage.c_str() );
      writeFile(s_spiffs, savePath.c_str(), inputMessage.c_str());

      request->redirect("/manager");
   });


   m_webServer->on("/delete", HTTP_GET, [](AsyncWebServerRequest *request)
   {
      PW_DEBUG( "Deleting" );
      if(!request->authenticate(http_username, http_password))
      {
         return request->requestAuthentication();
      }
      String inputMessage = "/" + request->getParam(param_delete_path)->value();
      if(inputMessage !="choose")
      {
         if ( ! s_spiffs->remove(inputMessage.c_str()) )
         {
            PW_WARN( "Failed to delete %s",inputMessage.c_str() );
         }
      }

      request->redirect("/manager");
   });

   m_webServer->on("/format", HTTP_POST, [](AsyncWebServerRequest *request)
   {
      s_spiffs->format();
      request->send(200);
      delay( 2 * 1000 );
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


