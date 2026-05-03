/**
 * storage.cpp
 * LittleFS JSON persistence implementation.
 *
 * Design notes:
 *  - Each file is a JSON array to allow incremental updates.
 *  - StaticJsonDocument sizes are tuned to the MAX_* constants above.
 *  - All flash I/O is synchronous but brief; no blocking loops.
 */

#include "storage.h"

namespace Storage {

bool begin() {
    if (!LittleFS.begin()) {
        Serial.println("[Storage] LittleFS mount failed – trying format");
        LittleFS.format();
        if (!LittleFS.begin()) {
            Serial.println("[Storage] LittleFS unavailable");
            return false;
        }
    }
    Serial.println("[Storage] LittleFS mounted OK");
    return true;
}

void format() {
    LittleFS.format();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static String readFile(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) return "[]";
    String s = f.readString();
    f.close();
    return s;
}

static bool writeFile(const char* path, const String& data) {
    File f = LittleFS.open(path, "w");
    if (!f) {
        Serial.printf("[Storage] Cannot write %s\n", path);
        return false;
    }
    f.print(data);
    f.close();
    return true;
}

// ---------------------------------------------------------------------------
// Students
// ---------------------------------------------------------------------------

int loadStudents(Student dest[], int maxCount) {
    // Each student: {"d":"AA:BB","n":"John"} ~30 bytes + overhead
    // 50 students * ~50 bytes = 2500 bytes + JSON array overhead ≈ 3 KB
    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, readFile(STUDENTS_FILE));
    if (err) {
        Serial.printf("[Storage] loadStudents parse error: %s\n", err.c_str());
        return 0;
    }
    JsonArray arr = doc.as<JsonArray>();
    int count = 0;
    for (JsonObject obj : arr) {
        if (count >= maxCount) break;
        dest[count].deviceId = obj["d"].as<String>();
        dest[count].name     = obj["n"].as<String>();
        count++;
    }
    return count;
}

bool saveStudents(const Student src[], int count) {
    StaticJsonDocument<4096> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < count; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["d"] = src[i].deviceId;
        obj["n"] = src[i].name;
    }
    String output;
    serializeJson(doc, output);
    return writeFile(STUDENTS_FILE, output);
}

// ---------------------------------------------------------------------------
// Attendance
// ---------------------------------------------------------------------------

int loadAttendance(AttendanceRecord dest[], int maxCount) {
    // Each record: {"d":"AA:BB","s":"sess1","t":1710000000} ~50 bytes
    // 200 records * ~60 bytes = 12 KB + overhead ≈ 14 KB
    StaticJsonDocument<16384> doc;
    DeserializationError err = deserializeJson(doc, readFile(ATTENDANCE_FILE));
    if (err) {
        Serial.printf("[Storage] loadAttendance parse error: %s\n", err.c_str());
        return 0;
    }
    JsonArray arr = doc.as<JsonArray>();
    int count = 0;
    for (JsonObject obj : arr) {
        if (count >= maxCount) break;
        dest[count].deviceId  = obj["d"].as<String>();
        dest[count].sessionId = obj["s"].as<String>();
        dest[count].timestamp = obj["t"].as<unsigned long>();
        count++;
    }
    return count;
}

bool saveAttendance(const AttendanceRecord src[], int count) {
    StaticJsonDocument<16384> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < count; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["d"] = src[i].deviceId;
        obj["s"] = src[i].sessionId;
        obj["t"] = src[i].timestamp;
    }
    String output;
    serializeJson(doc, output);
    return writeFile(ATTENDANCE_FILE, output);
}

} // namespace Storage
