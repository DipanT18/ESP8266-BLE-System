#pragma once
/**
 * attendance.h
 * In-memory attendance engine.
 *
 * Keeps RAM-resident arrays of students and attendance records.
 * Persists to LittleFS via Storage module on every mutation.
 *
 * Session lifecycle:
 *   startSession() → scanDevice() (many times) → stopSession()
 * A student is marked PRESENT when their BLE device is seen at least once
 * during an active session. Duplicates within the same session are ignored.
 */

#include <Arduino.h>
#include "storage.h"

namespace Attendance {

// Expose constants so API handlers can reference them
static const int MAX_STUDENTS   = Storage::MAX_STUDENTS;
static const int MAX_ATTENDANCE = Storage::MAX_ATTENDANCE;

// ---- Public state ---------------------------------------------------------

extern bool sessionActive;
extern String sessionId;      // e.g. "sess_1710000000"
extern unsigned long sessionStart;
extern unsigned long sessionEnd; // 0 while active

// In-memory mirrors (populated at boot from flash, kept in sync)
extern Storage::Student   students[];
extern int                studentCount;
extern Storage::AttendanceRecord attendance[];
extern int                attendanceCount;

// ---- Lifecycle ------------------------------------------------------------

/** Load persisted data from flash into RAM. Call once from setup(). */
void init();

// ---- Session management ---------------------------------------------------

/** Start a new session. Returns false if already active. */
bool startSession();

/**
 * Stop the current session.
 * Also enforces a maximum session length if timeoutSeconds > 0.
 * Returns false if no session was active.
 */
bool stopSession();

/** Called from loop(); stops session automatically after timeoutSeconds. */
void checkTimeout(unsigned long timeoutSeconds);

// ---- Student management ---------------------------------------------------

/**
 * Register a new student.
 * Returns false if deviceId already exists or limit reached.
 */
bool registerStudent(const String& deviceId, const String& name);

/** Find student index by deviceId. Returns -1 if not found. */
int findStudent(const String& deviceId);

// ---- Scan processing ------------------------------------------------------

/**
 * Process an incoming BLE scan hit.
 * Marks the student present if:
 *  - Session is active
 *  - RSSI >= minRssi (weak-signal filter)
 *  - Not already marked in current session
 * Returns true when a new attendance record is created.
 */
bool scanDevice(const String& deviceId, int rssi, unsigned long timestamp,
                int minRssi = -80);

// ---- Serialisation helpers (for API responses) ----------------------------

/** Write students JSON array into provided buffer. */
void studentsToJson(String& out);

/** Write attendance JSON array into provided buffer. */
void attendanceToJson(String& out);

/** Write session status JSON object into provided buffer. */
void sessionToJson(String& out);

} // namespace Attendance
