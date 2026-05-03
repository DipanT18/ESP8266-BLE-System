/**
 * api_handlers.cpp
 * REST API route implementations.
 *
 * Endpoints:
 *   POST /scan           – receive BLE scan hit from Android
 *   POST /register       – register student (name + device_id)
 *   GET  /students       – list registered students
 *   GET  /attendance     – list attendance records
 *   POST /start-session  – start an attendance session
 *   POST /stop-session   – stop the current session
 *   GET  /session        – session status
 *
 * All mutation endpoints require the Authorization header when AUTH_TOKEN != "".
 */

#include "api_handlers.h"
#include "attendance.h"
#include <ArduinoJson.h>

namespace ApiHandlers {

// ---------------------------------------------------------------------------
// Helper utilities
// ---------------------------------------------------------------------------

/** Respond with a JSON object containing a single "error" field. */
static void replyError(AsyncWebServerRequest* req, int code, const char* msg) {
    String body = "{\"error\":\"";
    body += msg;
    body += "\"}";
    req->send(code, "application/json", body);
}

/** Respond with a JSON object containing a single "ok" field. */
static void replyOk(AsyncWebServerRequest* req, const char* msg = "ok") {
    String body = "{\"ok\":\"";
    body += msg;
    body += "\"}";
    req->send(200, "application/json", body);
}

/** Check Bearer token if AUTH_TOKEN is non-empty.  Returns true if allowed. */
static bool checkAuth(AsyncWebServerRequest* req) {
    if (strlen(AUTH_TOKEN) == 0) return true;
    if (!req->hasHeader("Authorization")) return false;
    String auth = req->header("Authorization");
    String expected = String("Bearer ") + AUTH_TOKEN;
    return auth == expected;
}

// ---------------------------------------------------------------------------
// Body-parsing helper
// ---------------------------------------------------------------------------

/**
 * ESPAsyncWebServer calls the body handler before the request handler.
 * We attach the raw body as a request attribute via a small heap String.
 * The caller is responsible for freeing it (done in each handler below).
 */
static void onBody(AsyncWebServerRequest* req, uint8_t* data, size_t len,
                   size_t index, size_t total) {
    // Accumulate body into a String stored as a void* attribute
    if (index == 0) {
        // First chunk – allocate a new String
        String* body = new String();
        body->reserve(total);
        req->_tempObject = body;
    }
    if (req->_tempObject) {
        String* body = (String*)req->_tempObject;
        body->concat((char*)data, len);
    }
}

/** Retrieve and consume the body String; caller owns the pointer. */
static String getBody(AsyncWebServerRequest* req) {
    if (!req->_tempObject) return "";
    String* p = (String*)req->_tempObject;
    String s  = *p;
    delete p;
    req->_tempObject = nullptr;
    return s;
}

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------

/** POST /scan
 *  Body: {"device_id":"AA:BB:CC:DD","rssi":-65,"timestamp":1710000000}
 */
static void handleScan(AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { replyError(req, 401, "Unauthorized"); return; }

    String body = getBody(req);
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        replyError(req, 400, "Invalid JSON"); return;
    }

    const char* deviceId = doc["device_id"];
    int         rssi     = doc["rssi"]      | -100;
    unsigned long ts     = doc["timestamp"] | 0UL;

    if (!deviceId || strlen(deviceId) == 0) {
        replyError(req, 400, "Missing device_id"); return;
    }

    bool marked = Attendance::scanDevice(deviceId, rssi, ts);
    String resp = "{\"marked\":";
    resp += marked ? "true" : "false";
    resp += "}";
    req->send(200, "application/json", resp);
}

/** POST /register
 *  Body: {"name":"John Doe","device_id":"AA:BB:CC:DD"}
 */
static void handleRegister(AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { replyError(req, 401, "Unauthorized"); return; }

    String body = getBody(req);
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        replyError(req, 400, "Invalid JSON"); return;
    }

    const char* name     = doc["name"];
    const char* deviceId = doc["device_id"];

    if (!name || strlen(name) == 0 || !deviceId || strlen(deviceId) == 0) {
        replyError(req, 400, "Missing name or device_id"); return;
    }

    if (Attendance::registerStudent(deviceId, name)) {
        replyOk(req, "registered");
    } else {
        replyError(req, 409, "Already registered or limit reached");
    }
}

/** GET /students */
static void handleGetStudents(AsyncWebServerRequest* req) {
    String out;
    Attendance::studentsToJson(out);
    req->send(200, "application/json", out);
}

/** GET /attendance */
static void handleGetAttendance(AsyncWebServerRequest* req) {
    String out;
    Attendance::attendanceToJson(out);
    req->send(200, "application/json", out);
}

/** POST /start-session */
static void handleStartSession(AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { replyError(req, 401, "Unauthorized"); return; }

    if (Attendance::startSession()) {
        String out;
        Attendance::sessionToJson(out);
        req->send(200, "application/json", out);
    } else {
        replyError(req, 409, "Session already active");
    }
}

/** POST /stop-session */
static void handleStopSession(AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { replyError(req, 401, "Unauthorized"); return; }

    if (Attendance::stopSession()) {
        String out;
        Attendance::sessionToJson(out);
        req->send(200, "application/json", out);
    } else {
        replyError(req, 409, "No active session");
    }
}

/** GET /session */
static void handleGetSession(AsyncWebServerRequest* req) {
    String out;
    Attendance::sessionToJson(out);
    req->send(200, "application/json", out);
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

void registerRoutes(AsyncWebServer& server) {
    // Attach body accumulator to all POST routes
    server.onRequestBody(onBody);

    // Add CORS headers for browser fetch() calls from the dashboard
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");

    // Preflight handler
    server.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        req->send(204);
    });

    server.on("/scan",          HTTP_POST, handleScan);
    server.on("/register",      HTTP_POST, handleRegister);
    server.on("/students",      HTTP_GET,  handleGetStudents);
    server.on("/attendance",    HTTP_GET,  handleGetAttendance);
    server.on("/start-session", HTTP_POST, handleStartSession);
    server.on("/stop-session",  HTTP_POST, handleStopSession);
    server.on("/session",       HTTP_GET,  handleGetSession);

    // Serve frontend files from LittleFS (/, /index.html, static assets)
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // 404 fallback
    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "application/json", "{\"error\":\"Not found\"}");
    });

    Serial.println("[API] Routes registered");
}

} // namespace ApiHandlers
