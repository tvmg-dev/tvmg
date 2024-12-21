#ifndef WEBCONFIG_H
#define WEBCONFIG_H

#include <ESPAsyncWebServer.h>

class UserIO;

class WebServer
{
public:
   WebServer();
   ~WebServer();
   void initialise();
   void setUserIO( UserIO *userIO );

private:
   void setupAsyncServer();

   AsyncWebServer *m_webServer;
   UserIO         *m_userIO;
   File           m_downloadFile;
};

#endif
