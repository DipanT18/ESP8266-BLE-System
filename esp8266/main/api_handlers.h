#pragma once
/**
 * api_handlers.h
 * ESPAsyncWebServer route registration and handler declarations.
 *
 * All handlers are non-blocking:
 *  - JSON is parsed/generated with ArduinoJson StaticJsonDocument
 *  - No delay() calls
 *  - Responses sent via AsyncWebServerResponse
 *
 * Authentication:
 *  - Optional Bearer token check.  Set AUTH_TOKEN to "" to disable.
 */

#include <ESPAsyncWebServer.h>

// Set to a non-empty string to require  Authorization: Bearer <token>  header.
// Leave empty ("") to disable authentication.
// SECURITY: Change this to a strong random token before deployment.
// Do not use the default value in production.
#define AUTH_TOKEN "esp8266-attend-secret"

namespace ApiHandlers {

/** Register all REST routes on the given server instance. */
void registerRoutes(AsyncWebServer& server);

} // namespace ApiHandlers
