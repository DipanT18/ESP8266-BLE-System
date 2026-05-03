#pragma once
/**
 * storage.h
 * Handles LittleFS JSON persistence for students and attendance records.
 * Keeps each JSON file compact to stay within ~40 KB RAM budget.
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// File paths on LittleFS
#define STUDENTS_FILE   "/students.json"
#define ATTENDANCE_FILE "/attendance.json"

// Maximum counts to protect against RAM exhaustion
#define MAX_STUDENTS   50
#define MAX_ATTENDANCE 200

namespace Storage {

/** Mount LittleFS. Call once from setup(). Returns false on failure. */
bool begin();

/** Format the filesystem (wipes all data). */
void format();

// ---- Student record -------------------------------------------------------

struct Student {
    String deviceId; // BLE MAC or UUID, used as primary key
    String name;
};

/** Load all students from flash into dest. Returns number of students loaded. */
int loadStudents(Student dest[], int maxCount);

/** Persist the full students array to flash. Returns true on success. */
bool saveStudents(const Student src[], int count);

// ---- Attendance record ----------------------------------------------------

struct AttendanceRecord {
    String deviceId;
    String sessionId;
    unsigned long timestamp; // Unix epoch seconds
};

/** Load all attendance records from flash into dest. */
int loadAttendance(AttendanceRecord dest[], int maxCount);

/** Persist the full attendance array to flash. Returns true on success. */
bool saveAttendance(const AttendanceRecord src[], int count);

} // namespace Storage
