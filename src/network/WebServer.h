/*
 * Copyright (c) 2026,  Peter Walton
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This source code is licensed under the BSD-3-Clause License.
 * See the LICENSE file in the project root for full license text.
 */

#ifndef WEBCONFIG_H
#define WEBCONFIG_H

#include <ESPAsyncWebServer.h>

class Networking;
class UpdateManager;

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
   void setupUpdateHandlers();
   void setupFilesHandlers();
   void setupControlHandlers();
   
   void setupMiscHandlers();
   void handleCheckbox( const String &item,const String &state );

   AsyncWebServer    *m_webServer;
   AsyncEventSource  *m_otaEvents;
   AsyncEventSource  *m_statusEvents;
   Networking        *m_networking;
   UpdateManager     *m_updateManager;
   String            m_hiddenPage;
   File              m_downloadFile;
};

#endif
