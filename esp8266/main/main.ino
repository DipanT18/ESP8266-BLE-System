/**
 * main.ino
 * ESP8266 Bluetooth Attendance System – entry point
 *
 * Hardware: NodeMCU / ESP8266 (4 MB flash, ~40 KB usable heap)
 *
 * Libraries required (install via Arduino Library Manager or platformio.ini):
 *   - ESPAsyncWebServer  (me-no-dev/ESPAsyncWebServer)
 *   - ESPAsyncTCP        (me-no-dev/ESPAsyncTCP)  [dependency of above]
 *   - ArduinoJson        (bblanchon/ArduinoJson)   >= 6.x
 *   - LittleFS_esp8266   (built-in with ESP8266 Arduino core >= 2.7)
 *
 * WiFi modes:
 *   1. Station (STA) – connects to your home/school router.
 *      Set WIFI_SSID and WIFI_PASSWORD below.
 *   2. Access Point (AP) – fallback if STA fails; SSID = "AttendAP".
 *      Useful for standalone classroom use without a router.
 *
 * Session timeout:
 *   Sessions auto-stop after SESSION_TIMEOUT_S seconds (0 = never).
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "storage.h"
#include "attendance.h"
#include "api_handlers.h"

// ---------------------------------------------------------------------------
// Configuration – edit these before flashing
// SECURITY: Do not commit real credentials to version control.
// Consider a WiFi provisioning captive portal for production deployments.
// ---------------------------------------------------------------------------

#define WIFI_SSID       "BLE_Attendance"
#define WIFI_PASSWORD   "attend123"

#define AP_SSID         "AttendAP"
#define AP_PASSWORD     "attend123"   // min 8 chars; leave "" for open network

#define HTTP_PORT       80

// Auto-stop session after this many seconds (0 = disabled)
#define SESSION_TIMEOUT_S  3600UL  // 1 hour

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

AsyncWebServer server(HTTP_PORT);

// ---------------------------------------------------------------------------
// WiFi helpers
// ---------------------------------------------------------------------------

static bool connectSTA() {
    Serial.printf("[WiFi] Connecting to STA: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const int maxAttempts = 20;
    for (int i = 0; i < maxAttempts; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WiFi] Connected, IP: %s\n",
                          WiFi.localIP().toString().c_str());
            return true;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WiFi] STA failed");
    return false;
}

static void startAP() {
    WiFi.mode(WIFI_AP);
    bool ok = (strlen(AP_PASSWORD) >= 8)
              ? WiFi.softAP(AP_SSID, AP_PASSWORD)
              : WiFi.softAP(AP_SSID);
    if (ok) {
        Serial.printf("[WiFi] AP started: SSID=%s  IP=%s\n",
                      AP_SSID, WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("[WiFi] AP start failed");
    }
}

// ---------------------------------------------------------------------------
// Arduino lifecycle
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n[Boot] ESP8266 Attendance System starting...");

    // 1. Mount flash filesystem
    if (!Storage::begin()) {
        Serial.println("[Boot] FATAL: filesystem unavailable");
        // Continue anyway; data won't persist but server will work
    }

    // 2. Load persisted attendance data into RAM
    Attendance::init();

    // 3. Connect WiFi (STA preferred, AP fallback)
    if (!connectSTA()) {
        startAP();
    }

    // 4. Register HTTP routes
    ApiHandlers::registerRoutes(server);

    // 5. Start HTTP server
    server.begin();
    Serial.printf("[Boot] HTTP server listening on port %d\n", HTTP_PORT);
    Serial.println("[Boot] Setup complete");
}

void loop() {
    // ESPAsyncWebServer is fully asynchronous; no need to poll it here.

    // Check session timeout every second (millis() is non-blocking)
    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    if (now - lastCheck >= 1000UL) {
        lastCheck = now;
        Attendance::checkTimeout(SESSION_TIMEOUT_S);
    }

    // Yield to allow background WiFi tasks and async server processing
    yield();
}
