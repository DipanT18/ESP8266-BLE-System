# ESP32 BLE Attendance System

A **standalone, classroom-ready** attendance system powered by an **ESP32** microcontroller.  
The ESP32 simultaneously scans Bluetooth Low Energy (BLE) advertisements and runs a full web server — no external scanner app, no PC required.  
Students carry their phones or any BLE beacon device; the teacher opens the admin dashboard in any browser.

```
┌──────────────────────────────────────────────────────────────────────────┐
│          Student Phones / BLE Beacons                                    │
│  • iBeacon app (UUID / Major / Minor)  ·or·  any BLE-advertising device  │
└────────────────────────────┬─────────────────────────────────────────────┘
                             │ BLE radio (passive + active scan)
                     ┌───────▼────────┐
                     │   ESP32 Board  │
                     │  ┌──────────┐  │
                     │  │BLE Stack │  │  NimBLE – scans every 5 s
                     │  │(NimBLE)  │  │  iBeacon parser + MAC fallback
                     │  └────┬─────┘  │  Time-window deduplicator
                     │       │event   │
                     │  ┌────▼─────┐  │
                     │  │Attendance│  │  Session management
                     │  │ Engine   │  │  Student ↔ device matching
                     │  └────┬─────┘  │  LittleFS JSON persistence
                     │       │WS push │
                     │  ┌────▼─────┐  │
                     │  │ Web      │  │  ESPAsyncWebServer
                     │  │ Server   │  │  REST API  +  WebSocket /ws
                     │  └──────────┘  │
                     └───────┬────────┘
                             │ WiFi (STA or AP)
                     ┌───────▼────────┐
                     │ Browser (PC /  │
                     │ Phone)         │  http://<ESP32-IP>/
                     │  Dashboard SPA │  Live scan feed, attendance table,
                     │  (LittleFS)    │  student mgmt, session control
                     └────────────────┘
```

---

## Features

| Feature | Detail |
|---------|--------|
| **Standalone** | No laptop, Raspberry Pi, or Android scanner needed |
| **iBeacon support** | Parses Apple iBeacon UUID/Major/Minor; Minor = student ID |
| **MAC fallback** | Works with any BLE device even without a beacon app |
| **Real-time dashboard** | WebSocket push updates (scan feed, attendance) |
| **Session management** | Start/stop sessions; auto-timeout; per-session attendance |
| **RSSI filtering** | Configurable signal-strength threshold |
| **Deduplication** | Sliding-window prevents re-processing the same device |
| **LittleFS persistence** | Students & attendance survive reboots |
| **WiFi AP fallback** | Creates its own hotspot if router is unavailable |
| **NTP time sync** | Real timestamps on all records |
| **REST API** | Full JSON API for external integrations |
| **Auth token** | Bearer token on all mutation endpoints |

---

## Project Structure

```
ESP32-BLE-Attendance/
├── firmware/
│   ├── platformio.ini          # PlatformIO build config (ESP32)
│   ├── src/
│   │   ├── main.cpp            # Entry point – WiFi, NTP, BLE, server boot
│   │   ├── config.h            # All compile-time settings (edit before flash)
│   │   ├── ble_scanner.h/.cpp  # NimBLE GAP scanner → DeviceEvent callback
│   │   ├── ibeacon.h/.cpp      # Apple iBeacon advertisement parser
│   │   ├── dedup.h             # Time-window deduplicator (header-only)
│   │   ├── storage.h/.cpp      # LittleFS JSON persistence
│   │   ├── attendance.h/.cpp   # Session + attendance business logic
│   │   ├── ws_manager.h/.cpp   # AsyncWebSocket broadcast manager
│   │   └── api_handlers.h/.cpp # REST API route handlers
│   └── data/
│       └── index.html          # Dashboard SPA (served from LittleFS)
└── README.md
```

---

## How It Works

### 1 · BLE Detection

The ESP32 runs a continuous BLE GAP scan using **NimBLE-Arduino** — a lightweight BLE stack that leaves ~50 KB more heap than the default Bluedroid stack.

For each advertisement received the scanner:
1. Checks RSSI ≥ `RSSI_THRESHOLD` (default −80 dBm)
2. Tries to parse **Apple iBeacon** manufacturer data  
   → If valid: extracts UUID, Major, Minor (student beacon ID)  
   → If not: uses the raw MAC address
3. Applies a **sliding-window deduplicator** (`DEDUP_WINDOW_S` = 30 s)  
   → Same device is processed at most once per 30 seconds
4. Fires the `DeviceEvent` callback with all parsed fields

### 2 · Attendance Engine

On each `DeviceEvent`:
1. **Broadcast** raw scan event to all WebSocket clients (shows in live feed)
2. **Append** to scan log (LittleFS ring buffer, max 300 entries)
3. If no session is active → stop here
4. **Match** device to a registered student  
   → Check beacon ID first, then MAC address
5. If student not found → stop (unknown device)
6. If student already marked in this session → stop (duplicate)
7. **Mark present**: save to in-memory array + LittleFS + broadcast WebSocket event

### 3 · Web Server

An `AsyncWebServer` serves:
- **Static files** from LittleFS (`/` → `index.html`)
- **REST API** under `/api/*`
- **WebSocket** at `/ws`

The dashboard is a vanilla-JS SPA that connects to the WebSocket on load and updates in real-time.

### 4 · Student Registration

Two identification modes:

| Mode | When to use |
|------|-------------|
| **iBeacon (recommended)** | Student installs a beacon app on their phone that advertises a known Minor value. Register with `beacon_id = "0042"`. |
| **MAC address** | Register the student's phone/laptop MAC. Works with any Bluetooth device. Note: iOS randomises MACs; use iBeacon mode for iPhones. |

You can register both `device_id` (MAC) and `beacon_id` for the same student; the system tries beacon first.

---

## Hardware Requirements

| Component | Requirement |
|-----------|-------------|
| **Microcontroller** | Any ESP32 board (ESP32-WROOM-32, DevKitC, etc.) |
| **Flash** | ≥ 4 MB (LittleFS partition for filesystem) |
| **RAM** | ~300 KB usable (ESP32 standard) |
| **USB** | USB-to-UART for flashing and serial monitor |
| **Power** | USB 5V or 3.3V regulated |

No external hardware required — BLE and WiFi are built into the ESP32.

---

## Software Requirements

| Tool | Version |
|------|---------|
| [PlatformIO Core](https://platformio.org/install) | ≥ 6.x |
| ESP32 Arduino Core | ≥ 2.0 (installed automatically by PlatformIO) |
| Python | ≥ 3.8 (used by PlatformIO) |

---

## Quick Start

### Step 1 – Configure

Open `firmware/src/config.h` and edit:

```cpp
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// Optional: change the admin password
#define AUTH_TOKEN      "esp32-attend-secret"

// Optional: set your timezone UTC offset in seconds (e.g. 19800 = IST +5:30)
#define NTP_TIMEZONE_OFFSET_S  0
```

Also update `AUTH_TOKEN` in `firmware/data/index.html` (the line `const AUTH_TOKEN = "esp32-attend-secret";`).

### Step 2 – Build & Flash Firmware

```bash
cd firmware
pio run --target upload
```

### Step 3 – Upload the Dashboard (LittleFS)

```bash
pio run --target uploadfs
```

> **Important:** Upload the filesystem *after* the firmware, or the LittleFS partition will be overwritten.

### Step 4 – Find the IP Address

Open the serial monitor:

```bash
pio device monitor
```

Look for output like:
```
[WiFi] Connected  IP: 192.168.1.42
[Boot] Open http://192.168.1.42/ in your browser
```

If your router is not available, the ESP32 creates an access point:
- SSID: `ESP32-BLE-Attend`
- Password: `attend123`
- Dashboard: `http://192.168.4.1/`

### Step 5 – Use the Dashboard

1. Open `http://<ESP32-IP>/` in any browser
2. Go to **Students** → register each student (name + MAC or beacon ID)
3. Go to **Session** → click **▶ Start Session** (optionally enter a class name)
4. The **Dashboard** live feed shows detected BLE devices in real-time
5. Matched students appear in **Current Session Attendance** immediately
6. When class ends, go to **Session** → click **■ Stop Session**
7. Full records are on the **Attendance** tab

---

## REST API Reference

All mutation endpoints require:  
`Authorization: Bearer <AUTH_TOKEN>`

### Session

| Method | Endpoint | Body | Description |
|--------|----------|------|-------------|
| `GET`  | `/api/session` | – | Current session status |
| `POST` | `/api/session/start` | `{"class_name":"CS101"}` | Start a session |
| `POST` | `/api/session/stop` | – | Stop the active session |

### Students

| Method | Endpoint | Body | Description |
|--------|----------|------|-------------|
| `GET`  | `/api/students` | – | List all students |
| `POST` | `/api/students` | see below | Register a student |
| `DELETE` | `/api/students/:id` | – | Remove by index |
| `POST` | `/api/students/import` | JSON array | Bulk import |

**POST /api/students body:**
```json
{
  "name":        "Alice Johnson",
  "device_id":   "AA:BB:CC:11:22:33",
  "beacon_id":   "0001",
  "roll_number": "CS2024001"
}
```

### Attendance

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET`  | `/api/attendance` | All attendance records |
| `GET`  | `/api/attendance/session` | Current session records only |
| `GET`  | `/api/stats` | Summary statistics |
| `GET`  | `/api/scan-logs` | Recent raw BLE scan events |

### WebSocket `/ws`

Messages are JSON envelopes: `{"event":"<type>","data":{...}}`

| Event | Data fields | Description |
|-------|-------------|-------------|
| `scan` | `mac`, `rssi`, `name`, `beacon_id`, `timestamp` | BLE device detected |
| `attendance` | `name`, `mac`, `beacon_id`, `session_id`, `rssi`, `timestamp` | Student marked present |
| `session` | `active`, `session_id`, `class_name`, `start`, `end`, `count` | Session state changed |

---

## Configuration Reference (`config.h`)

| Constant | Default | Description |
|----------|---------|-------------|
| `WIFI_SSID` | – | Station mode SSID |
| `WIFI_PASSWORD` | – | Station mode password |
| `AP_SSID` | `"ESP32-BLE-Attend"` | AP fallback SSID |
| `AP_PASSWORD` | `"attend123"` | AP password |
| `AUTH_TOKEN` | `"esp32-attend-secret"` | API Bearer token |
| `BLE_SCAN_DURATION_S` | `5` | Scan cycle seconds |
| `RSSI_THRESHOLD` | `-80` | Min signal dBm |
| `DEDUP_WINDOW_S` | `30` | Dedup window seconds |
| `IBEACON_UUID_FILTER` | `""` | Filter by UUID (`""` = all) |
| `MAX_STUDENTS` | `150` | Max students in memory |
| `MAX_ATTENDANCE` | `1000` | Max attendance records |
| `SESSION_TIMEOUT_S` | `3600` | Auto-stop timeout (0 = off) |
| `NTP_SERVER` | `"pool.ntp.org"` | NTP server |
| `NTP_TIMEZONE_OFFSET_S` | `0` | UTC offset seconds |

---

## iBeacon Student App Setup

The recommended method for phone-based detection:

1. Install a BLE beacon app:
   - **Android**: [BLE Peripheral Simulator](https://play.google.com/store/apps/details?id=de.sauernetworks.btle_peripheral_sim)
   - **iOS**: [Beacon Scope](https://apps.apple.com/app/beaconscope/id1033915911) or similar

2. Configure the beacon:
   - Type: **iBeacon**
   - UUID: shared UUID (e.g. `A8B3F9E2-C4D5-4F6A-7B8C-9D0E1F2A3B4C`)
   - Major: `1`
   - Minor: student's unique number (e.g. `42`)

3. Register on dashboard with Beacon ID `"0042"`.

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| No scan events on dashboard | Check WebSocket connection dot; ensure port 80 reachable |
| iPhone not detected | Use iBeacon mode (iOS randomises MAC addresses) |
| Student not marked | Verify registration; move closer or lower `RSSI_THRESHOLD` |
| Wrong timestamps | Check WiFi + NTP; verify `NTP_TIMEZONE_OFFSET_S` |
| LittleFS mount failed | Re-run `pio run --target uploadfs` after full flash erase |
| BLE scan FAILED | Restart ESP32; ensure no conflicting BLE task |
| `Permission denied: /dev/ttyUSB0` (Linux) | Add your user to the `dialout` group, then log out and back in: `sudo usermod -aG dialout $USER` |
| Still permission denied after group change | Install PlatformIO udev rules: `curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/master/scripts/99-platformio-udev.rules &#124; sudo tee /etc/udev/rules.d/99-platformio-udev.rules && sudo udevadm control --reload-rules && sudo udevadm trigger`, then unplug/replug the board |
| Dashboard shows old UI after firmware update | Always upload the filesystem **after** the firmware: `pio run -t upload && pio run -t uploadfs`. Uploading firmware alone does not update LittleFS |
| `POST /api/students` returns `400 Invalid JSON` | Ensure you send `Content-Type: application/json` and a valid JSON body with `name` plus at least one of `device_id` (MAC `AA:BB:CC:DD:EE:FF`) or `beacon_id`. The dashboard and firmware auth token must also match (`AUTH_TOKEN` in `config.h` and `index.html`) |

---

## License

MIT License — free for personal and educational use.
