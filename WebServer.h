#ifndef WEBCONFIG_H
#define WEBCONFIG_H

#include <ESPAsyncWebServer.h>

class Networking;

class WebServer
{
public:
   WebServer( Networking *networking );
   ~WebServer();
   void initialise();

private:
   void setupAsyncServer();

   AsyncWebServer *m_webServer;
   Networking     *m_networking;
   File           m_downloadFile;
};

#endif
