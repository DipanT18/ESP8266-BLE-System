/**
 * attendance.cpp
 * In-memory attendance engine implementation.
 */

#include "attendance.h"
#include <ArduinoJson.h>

namespace Attendance {

// ---------------------------------------------------------------------------
// Public state definitions
// ---------------------------------------------------------------------------

bool         sessionActive  = false;
String       sessionId      = "";
unsigned long sessionStart  = 0;
unsigned long sessionEnd    = 0;

Storage::Student          students[MAX_STUDENTS];
int                       studentCount = 0;
Storage::AttendanceRecord attendance[MAX_ATTENDANCE];
int                       attendanceCount = 0;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void init() {
    studentCount   = Storage::loadStudents(students, MAX_STUDENTS);
    attendanceCount = Storage::loadAttendance(attendance, MAX_ATTENDANCE);
    Serial.printf("[Attendance] Loaded %d students, %d attendance records\n",
                  studentCount, attendanceCount);
}

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------

bool startSession() {
    if (sessionActive) return false;

    // NOTE: millis()/1000 gives seconds since last reboot, not a real Unix
    // epoch. For real timestamps integrate NTP (e.g. WiFiUDP + NTPClient).
    // The session ID is unique per boot cycle and is used only for
    // deduplication within a run; reboots reset the counter.
    sessionStart  = millis() / 1000;
    sessionId     = "sess_" + String(sessionStart);
    sessionEnd    = 0;
    sessionActive = true;

    Serial.printf("[Attendance] Session started: %s\n", sessionId.c_str());
    return true;
}

bool stopSession() {
    if (!sessionActive) return false;

    sessionEnd    = millis() / 1000;
    sessionActive = false;

    Serial.printf("[Attendance] Session stopped: %s  (duration %lu s)\n",
                  sessionId.c_str(), sessionEnd - sessionStart);
    return true;
}

void checkTimeout(unsigned long timeoutSeconds) {
    if (!sessionActive || timeoutSeconds == 0) return;
    unsigned long elapsed = millis() / 1000 - sessionStart;
    if (elapsed >= timeoutSeconds) {
        Serial.println("[Attendance] Session timed out");
        stopSession();
    }
}

// ---------------------------------------------------------------------------
// Student management
// ---------------------------------------------------------------------------

int findStudent(const String& deviceId) {
    for (int i = 0; i < studentCount; i++) {
        if (students[i].deviceId == deviceId) return i;
    }
    return -1;
}

bool registerStudent(const String& deviceId, const String& name) {
    if (findStudent(deviceId) >= 0) return false; // duplicate
    if (studentCount >= MAX_STUDENTS) return false;

    students[studentCount].deviceId = deviceId;
    students[studentCount].name     = name;
    studentCount++;

    Storage::saveStudents(students, studentCount);
    Serial.printf("[Attendance] Registered student: %s (%s)\n",
                  name.c_str(), deviceId.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Scan processing
// ---------------------------------------------------------------------------

bool scanDevice(const String& deviceId, int rssi, unsigned long timestamp,
                int minRssi) {
    if (!sessionActive) return false;
    if (rssi < minRssi) return false;           // signal too weak
    if (findStudent(deviceId) < 0) return false; // unknown device

    // Deduplicate: check if already marked in this session
    for (int i = 0; i < attendanceCount; i++) {
        if (attendance[i].sessionId == sessionId &&
            attendance[i].deviceId  == deviceId) {
            return false; // already present
        }
    }

    if (attendanceCount >= MAX_ATTENDANCE) {
        Serial.println("[Attendance] Attendance buffer full");
        return false;
    }

    attendance[attendanceCount].deviceId  = deviceId;
    attendance[attendanceCount].sessionId = sessionId;
    attendance[attendanceCount].timestamp = timestamp;
    attendanceCount++;

    Storage::saveAttendance(attendance, attendanceCount);
    Serial.printf("[Attendance] Marked present: %s in %s\n",
                  deviceId.c_str(), sessionId.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// JSON serialisation
// ---------------------------------------------------------------------------

void studentsToJson(String& out) {
    // ~50 bytes * 50 students = 2500 bytes; fit in 4 KB doc
    StaticJsonDocument<4096> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < studentCount; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["device_id"] = students[i].deviceId;
        obj["name"]      = students[i].name;
    }
    serializeJson(doc, out);
}

void attendanceToJson(String& out) {
    // ~70 bytes * 200 records = 14 KB
    StaticJsonDocument<16384> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < attendanceCount; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["device_id"]  = attendance[i].deviceId;
        obj["session_id"] = attendance[i].sessionId;
        obj["timestamp"]  = attendance[i].timestamp;
        // Resolve name for convenience
        int idx = findStudent(attendance[i].deviceId);
        obj["name"] = (idx >= 0) ? students[idx].name : "Unknown";
    }
    serializeJson(doc, out);
}

void sessionToJson(String& out) {
    StaticJsonDocument<256> doc;
    doc["active"]     = sessionActive;
    doc["session_id"] = sessionId;
    doc["start"]      = sessionStart;
    doc["end"]        = sessionEnd;
    serializeJson(doc, out);
}

} // namespace Attendance
