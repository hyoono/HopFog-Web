#ifndef API_HANDLERS_H
#define API_HANDLERS_H

#include <ESPAsyncWebServer.h>

// Register all REST API routes on the server
void registerApiRoutes(AsyncWebServer &server);

#endif // API_HANDLERS_H
