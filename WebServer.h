#ifndef WEBCONFIG_H
#define WEBCONFIG_H

#include <ESPAsyncWebServer.h>

class WebServer
{
public:
   WebServer();
   ~WebServer();
   void initialise();

private:
   void setupAsyncServer();

   AsyncWebServer *m_webServer;
};

#endif
