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
   void generateOptionsSection();
   void setupAsyncServer();
   void handleCheckbox( const String &item,const String &state );

   AsyncWebServer *m_webServer;
   Networking     *m_networking;
   String         m_hiddenPage;
   File           m_downloadFile;
};

#endif
