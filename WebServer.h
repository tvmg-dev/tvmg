#ifndef WEBCONFIG_H
#define WEBCONFIG_H

#include <ESPAsyncWebServer.h>

class WebStuff
{
public:
   WebStuff();
   ~WebStuff();
   void initialise();

private:
   void setupAsyncServer();

   AsyncWebServer *m_webServer;
};

#endif
