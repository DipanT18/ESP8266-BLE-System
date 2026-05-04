/**
 * api_handlers.cpp
 * REST API route implementations.
 */

#include "api_handlers.h"
#include "attendance.h"
#include "config.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

namespace ApiHandlers {

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------

static void replyJson(AsyncWebServerRequest* req, int code, const String& body) {
    req->send(code, "application/json", body);
}

static void replyError(AsyncWebServerRequest* req, int code, const char* msg) {
    String body = "{\"error\":\"";
    body += msg;
    body += "\"}";
    replyJson(req, code, body);
}

static void replyOk(AsyncWebServerRequest* req, const char* msg = "ok") {
    String body = "{\"ok\":\"";
    body += msg;
    body += "\"}";
    replyJson(req, 200, body);
}

static bool checkAuth(AsyncWebServerRequest* req) {
    if (strlen(AUTH_TOKEN) == 0) return true;
    if (!req->hasHeader("Authorization")) return false;
    String auth = req->header("Authorization");
    return auth == (String("Bearer ") + AUTH_TOKEN);
}

// ---------------------------------------------------------------------------
// Body accumulation (ESPAsyncWebServer calls onBody before the handler)
// ---------------------------------------------------------------------------

static void onBody(AsyncWebServerRequest* req, uint8_t* data, size_t len,
                   size_t index, size_t total) {
    if (index == 0) {
        String* body = new String();
        body->reserve(total);
        req->_tempObject = body;
    }
    if (req->_tempObject) {
        static_cast<String*>(req->_tempObject)->concat(
            reinterpret_cast<char*>(data), len);
    }
}

static String getBody(AsyncWebServerRequest* req) {
    if (!req->_tempObject) return "";
    String* p = static_cast<String*>(req->_tempObject);
    String  s = *p;
    delete p;
    req->_tempObject = nullptr;
    return s;
}

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------

/** GET /api/session */
static void handleGetSession(AsyncWebServerRequest* req) {
    String out;
    Attendance::sessionToJson(out);
    replyJson(req, 200, out);
}

/** POST /api/session/start  body: { "class_name": "CS101" } (optional) */
static void handleStartSession(AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { replyError(req, 401, "Unauthorized"); return; }

    String body = getBody(req);
    String className = "";
    if (body.length() > 0) {
        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            if (doc.containsKey("class_name")) {
                className = doc["class_name"].as<String>();
            }
        }
    }

    if (Attendance::startSession(className)) {
        String out;
        Attendance::sessionToJson(out);
        replyJson(req, 200, out);
    } else {
        replyError(req, 409, "Session already active");
    }
}

/** POST /api/session/stop */
static void handleStopSession(AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { replyError(req, 401, "Unauthorized"); return; }
    getBody(req); // consume body if any
    if (Attendance::stopSession()) {
        String out;
        Attendance::sessionToJson(out);
        replyJson(req, 200, out);
    } else {
        replyError(req, 409, "No active session");
    }
}

/** GET /api/students */
static void handleGetStudents(AsyncWebServerRequest* req) {
    String out;
    Attendance::studentsToJson(out);
    replyJson(req, 200, out);
}

/**
 * POST /api/students
 * Body: { "name": "...", "device_id": "AA:BB:...", "beacon_id": "0042",
 *         "roll_number": "CS2021001" }
 * beacon_id and roll_number are optional.
 */
static void handleRegisterStudent(AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { replyError(req, 401, "Unauthorized"); return; }

    String body = getBody(req);
    StaticJsonDocument<384> doc;
    DeserializationError jsonErr = deserializeJson(doc, body);
    if (jsonErr) {
        String errMsg = "Invalid JSON: ";
        errMsg += jsonErr.c_str();
        replyError(req, 400, errMsg.c_str());
        return;
    }

    const char* name       = doc["name"];
    const char* deviceId   = doc["device_id"];
    const char* beaconId   = doc["beacon_id"]   | "";
    const char* rollNumber = doc["roll_number"]  | "";

    if (!name || strlen(name) == 0) {
        replyError(req, 400, "Missing name"); return;
    }
    if ((!deviceId || strlen(deviceId) == 0) &&
        (strlen(beaconId) == 0)) {
        replyError(req, 400, "Provide device_id or beacon_id"); return;
    }

    // Allow beacon-only registration (deviceId can be empty placeholder)
    String mac = deviceId ? String(deviceId) : String("");

    if (Attendance::registerStudent(mac, String(name),
                                    String(beaconId), String(rollNumber))) {
        replyOk(req, "registered");
    } else {
        replyError(req, 409, "Already registered or limit reached");
    }
}

/** DELETE /api/students/:id */
static void handleDeleteStudent(AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { replyError(req, 401, "Unauthorized"); return; }

    if (!req->hasParam("id")) {
        replyError(req, 400, "Missing id"); return;
    }
    int idx = req->getParam("id")->value().toInt();
    if (Attendance::removeStudent(idx)) {
        replyOk(req, "removed");
    } else {
        replyError(req, 404, "Student not found");
    }
}

/** GET /api/attendance */
static void handleGetAttendance(AsyncWebServerRequest* req) {
    String out;
    Attendance::attendanceToJson(out);
    replyJson(req, 200, out);
}

/** GET /api/attendance/session – only current session */
static void handleGetSessionAttendance(AsyncWebServerRequest* req) {
    const String& sid = Attendance::sessionId;
    DynamicJsonDocument doc(Attendance::attendanceCount * 120 + 64);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < Attendance::attendanceCount; i++) {
        if (Attendance::attendance[i].sessionId != sid) continue;
        JsonObject obj = arr.createNestedObject();
        obj["device_id"]  = Attendance::attendance[i].deviceId;
        obj["session_id"] = Attendance::attendance[i].sessionId;
        obj["timestamp"]  = (long)Attendance::attendance[i].timestamp;
        obj["name"]       = Attendance::attendance[i].name;
    }
    String out;
    serializeJson(doc, out);
    replyJson(req, 200, out);
}

/** GET /api/stats */
static void handleGetStats(AsyncWebServerRequest* req) {
    String out;
    Attendance::statsToJson(out);
    replyJson(req, 200, out);
}

/** GET /api/scan-logs */
static void handleGetScanLogs(AsyncWebServerRequest* req) {
    File f = LittleFS.open("/scanlogs.json", "r");
    if (!f || f.isDirectory()) {
        req->send(200, "application/json", "[]");
        return;
    }
    req->send(f, "/scanlogs.json", "application/json");
}

/**
 * POST /api/students/import
 * Body: JSON array of student objects (same format as POST /api/students).
 * Skips duplicates; returns count of newly added students.
 */
static void handleImportStudents(AsyncWebServerRequest* req) {
    if (!checkAuth(req)) { replyError(req, 401, "Unauthorized"); return; }

    String body = getBody(req);
    DynamicJsonDocument doc(16384);
    DeserializationError jsonErr = deserializeJson(doc, body);
    if (jsonErr) {
        String errMsg = "Invalid JSON: ";
        errMsg += jsonErr.c_str();
        replyError(req, 400, errMsg.c_str());
        return;
    }

    if (!doc.is<JsonArray>()) {
        replyError(req, 400, "Expected JSON array"); return;
    }

    int added = 0;
    for (JsonObject obj : doc.as<JsonArray>()) {
        const char* name       = obj["name"]        | "";
        const char* deviceId   = obj["device_id"]   | "";
        const char* beaconId   = obj["beacon_id"]   | "";
        const char* rollNumber = obj["roll_number"]  | "";
        if (strlen(name) == 0) continue;
        if (Attendance::registerStudent(String(deviceId), String(name),
                                        String(beaconId), String(rollNumber))) {
            added++;
        }
    }

    String out = "{\"added\":" + String(added) + "}";
    replyJson(req, 200, out);
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

static void registerBodyRoute(AsyncWebServer& server, const char* uri,
                              WebRequestMethodComposite method,
                              ArRequestHandlerFunction handler) {
    AsyncCallbackWebHandler* h = new AsyncCallbackWebHandler();
    h->setUri(uri);
    h->setMethod(method);
    h->onRequest(handler);
    h->onBody(onBody);
    server.addHandler(h);
}

void registerRoutes(AsyncWebServer& server) {
    // ESPAsyncWebServer 1.2.x lacks a server.on overload with a body callback,
    // so we register POST routes with AsyncCallbackWebHandler to attach onBody.

    // CORS
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");

    server.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        req->send(204);
    });

    // Session

    server.on("/api/session",       HTTP_GET,  handleGetSession);
    registerBodyRoute(server, "/api/session/start", HTTP_POST, handleStartSession);
    registerBodyRoute(server, "/api/session/stop",  HTTP_POST, handleStopSession);

    // Students – register more-specific paths before their prefix so that
    // ESPAsyncWebServer's prefix-match logic doesn't route /import to the
    // wrong handler.
    registerBodyRoute(server, "/api/students/import", HTTP_POST, handleImportStudents);
    registerBodyRoute(server, "/api/students",        HTTP_POST, handleRegisterStudent);
    server.on("/api/students",        HTTP_GET,  handleGetStudents);
    server.on("^/api/students/([0-9]+)$", HTTP_DELETE,
              [](AsyncWebServerRequest* req) {
                  if (!checkAuth(req)) { replyError(req, 401, "Unauthorized"); return; }
                  String idStr = req->pathArg(0);
                  int idx = idStr.toInt();
                  if (Attendance::removeStudent(idx)) {
                      replyOk(req, "removed");
                  } else {
                      replyError(req, 404, "Student not found");
                  }
              });

    // Attendance – session must come before the parent path for the same reason.
    server.on("/api/attendance/session", HTTP_GET, handleGetSessionAttendance);
    server.on("/api/attendance",         HTTP_GET, handleGetAttendance);

    // Misc
    server.on("/api/stats",     HTTP_GET, handleGetStats);
    server.on("/api/scan-logs", HTTP_GET, handleGetScanLogs);

    // Serve dashboard SPA from LittleFS
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // 404
    server.onNotFound([](AsyncWebServerRequest* req) {
        // For SPA deep-links, return index.html
        if (req->method() == HTTP_GET && !req->url().startsWith("/api/")) {
            req->send(LittleFS, "/index.html", "text/html");
        } else {
            req->send(404, "application/json", "{\"error\":\"Not found\"}");
        }
    });

    Serial.println("[API] Routes registered");
}

} // namespace ApiHandlers
