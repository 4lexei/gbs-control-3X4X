#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Web server initialization
void startWebserver();
void initUpdateOTA();

// [patch web-update] true while a firmware upload is in progress: the main loop then only services WiFi/web
// (frame-sync measurements block the CPU for long stretches and starve the TCP stack -> upload drops)
bool webUpdateBusy();

#endif // WEB_SERVER_H

