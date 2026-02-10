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
   void updateClients( const char *data );

private:
   void generateOptionsSection();
   void setupAsyncServer();
   void setupEventSources();
   void setupOTAHandler();
   void setupFilesHandlers();
   void setupControlHandlers();
   
   void setupMiscHandlers();
   void handleCheckbox( const String &item,const String &state );

   AsyncWebServer    *m_webServer;
   AsyncEventSource  *m_otaEvents;
   AsyncEventSource  *m_statusEvents;
   Networking        *m_networking;
   String            m_hiddenPage;
   File              m_downloadFile;
};

#endif
