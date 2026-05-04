/**
 * ble_scanner.cpp
 * ESP32 BLE GAP scanner implementation using NimBLE-Arduino.
 */

#include "ble_scanner.h"
#include <NimBLEDevice.h>
#include <time.h>

namespace BLEScanner {

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------

static EventCallback  s_callback;
// Static allocation avoids dynamic memory; dedup window set in begin()
static Dedup::Deduplicator s_dedupInstance(DEDUP_WINDOW_S);
static Dedup::Deduplicator* s_dedup = &s_dedupInstance;
static bool           s_scanning = false;
static bool           s_stopRequested = false;

// ---------------------------------------------------------------------------
// NimBLE advertised-device callback
// ---------------------------------------------------------------------------

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
public:
    void onResult(NimBLEAdvertisedDevice* adv) override {
        int rssi = adv->getRSSI();
        if (rssi < RSSI_THRESHOLD) return;   // too weak

        String mac = String(adv->getAddress().toString().c_str());
        mac.toUpperCase();

        DeviceEvent ev;
        ev.rssi       = rssi;
        ev.mac        = mac;
        ev.name       = adv->haveName() ? String(adv->getName().c_str()) : "";
        ev.hasBeacon  = false;
        ev.major      = 0;
        ev.minor      = 0;

        // Attempt iBeacon parse from manufacturer data
        if (adv->haveManufacturerData()) {
            std::string mfr = adv->getManufacturerData();
            IBeacon::Payload beacon = IBeacon::parse(
                reinterpret_cast<const uint8_t*>(mfr.data()), mfr.size());

            if (beacon.valid) {
                // Optional UUID filter
                const String uuidFilter = IBEACON_UUID_FILTER;
                if (!IBeacon::uuidMatches(beacon, uuidFilter)) return;

                ev.hasBeacon = true;
                ev.beaconId  = beacon.beaconId();
                ev.beaconUuid = beacon.uuid;
                ev.major     = beacon.major;
                ev.minor     = beacon.minor;
            }
        }

        // Use beacon ID for dedup key if available, otherwise MAC
        String dedupKey = ev.hasBeacon ? ("b:" + ev.beaconId) : ("m:" + mac);
        if (!s_dedup->shouldProcess(dedupKey)) return;

        // Timestamp: prefer real NTP time, fall back to uptime seconds
        time_t now = time(nullptr);
        ev.timestamp = (now > 1000000000L) ? now : (time_t)(millis() / 1000UL);

        if (s_callback) s_callback(ev);
    }
};

static ScanCallbacks s_scanCallbacks;

static void configureScan(NimBLEScan* scan) {
    scan->setAdvertisedDeviceCallbacks(&s_scanCallbacks, /*duplicates=*/true);
    scan->setActiveScan(true);

    uint16_t interval = BLE_SCAN_INTERVAL;
    uint16_t window   = BLE_SCAN_WINDOW;
    if (window > interval) window = interval;

    scan->setInterval(interval);
    scan->setWindow(window);
    scan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
    scan->setDuplicateFilter(false);
    scan->setMaxResults(0);
}

static void onScanComplete(const NimBLEScanResults& results) {
    (void)results;
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->clearResults();

    if (!s_scanning || s_stopRequested) return;

    bool ok = scan->start(BLE_SCAN_DURATION_S, onScanComplete, /*restart=*/false);
    if (!ok) {
        s_scanning = false;
        Serial.println("[BLE] Scan auto-restart FAILED");
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void begin(EventCallback callback) {
    s_callback = callback;

    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // max TX power for scanning

    Serial.println("[BLE] NimBLE initialised");
}

void startScan() {
    NimBLEScan* scan = NimBLEDevice::getScan();
    configureScan(scan);
    scan->clearResults();

    bool ok = false;
    s_stopRequested = false;
    if (BLE_SCAN_DURATION_S == 0) {
        ok = scan->start(0, nullptr, /*restart=*/false);
    } else {
        ok = scan->start(BLE_SCAN_DURATION_S, onScanComplete, /*restart=*/false);
    }

    s_scanning = ok;
    Serial.printf("[BLE] Scan %s (duration %d s)\n",
                  ok ? "started" : "FAILED", BLE_SCAN_DURATION_S);
}

void stopScan() {
    s_stopRequested = true;
    NimBLEDevice::getScan()->stop();
    NimBLEDevice::getScan()->clearResults();
    s_scanning = false;
    Serial.println("[BLE] Scan stopped");
}

bool isScanning() {
    return s_scanning && !s_stopRequested;
}

} // namespace BLEScanner
