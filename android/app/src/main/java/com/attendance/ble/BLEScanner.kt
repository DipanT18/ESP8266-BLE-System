package com.attendance.ble

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.*
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.util.Log
import androidx.core.content.ContextCompat
import kotlinx.coroutines.*
import java.util.concurrent.ConcurrentHashMap

private const val TAG = "BLEScanner"

/**
 * BLEScanner
 *
 * Continuously scans for nearby BLE devices and maintains a deduplicated
 * map of [deviceId -> ScannedDevice].  Weak signals (below [minRssi]) are
 * ignored to reduce false positives.
 *
 * Usage:
 *   val scanner = BLEScanner(context, minRssi = -80)
 *   scanner.start { devices -> /* called on each scan batch */ }
 *   scanner.stop()
 *
 * The callback fires every [scanWindowMs] milliseconds with a snapshot of
 * all currently visible devices.
 */
class BLEScanner(
    private val context: Context,
    private val minRssi: Int = -80,          // ignore devices weaker than this
    private val scanWindowMs: Long = 5_000L  // callback interval
) {
    /** Represents one discovered BLE device. */
    data class ScannedDevice(
        val deviceId: String,   // MAC address (or synthetic ID on API 31+)
        val rssi: Int,
        val timestamp: Long     // Unix epoch seconds (System.currentTimeMillis / 1000)
    )

    // Live deduplicated map: deviceId → latest scan result
    private val deviceMap = ConcurrentHashMap<String, ScannedDevice>()

    private var bluetoothLeScanner: BluetoothLeScanner? = null
    private var scanJob: Job? = null
    private var scanCallback: ScanCallback? = null
    private var onBatch: ((List<ScannedDevice>) -> Unit)? = null

    private val bleAvailable: Boolean
        get() {
            val bm = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
            return bm?.adapter?.isEnabled == true
        }

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    /**
     * Start continuous BLE scanning.
     * [callback] is invoked on the main thread every [scanWindowMs] ms.
     */
    fun start(callback: (List<ScannedDevice>) -> Unit) {
        if (!bleAvailable) {
            Log.w(TAG, "Bluetooth not available or disabled")
            return
        }
        if (!hasPermissions()) {
            Log.w(TAG, "Missing BLE permissions")
            return
        }

        onBatch = callback
        val bm = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothLeScanner = bm.adapter.bluetoothLeScanner

        // Low-latency scan settings (balanced mode to save battery slightly)
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_BALANCED)
            .build()

        scanCallback = buildScanCallback()
        bluetoothLeScanner?.startScan(null, settings, scanCallback)
        Log.i(TAG, "BLE scan started (minRssi=$minRssi)")

        // Periodic batch delivery
        scanJob = CoroutineScope(Dispatchers.Main).launch {
            while (isActive) {
                delay(scanWindowMs)
                deliverBatch()
            }
        }
    }

    /** Stop scanning and cancel the batch job. */
    fun stop() {
        scanJob?.cancel()
        scanJob = null
        try {
            scanCallback?.let { bluetoothLeScanner?.stopScan(it) }
        } catch (e: Exception) {
            Log.w(TAG, "stopScan exception: ${e.message}")
        }
        scanCallback = null
        deviceMap.clear()
        Log.i(TAG, "BLE scan stopped")
    }

    /** Returns a snapshot of currently visible devices. */
    fun getDevices(): List<ScannedDevice> = deviceMap.values.toList()

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

    private fun deliverBatch() {
        val snapshot = deviceMap.values.toList()
        if (snapshot.isNotEmpty()) {
            Log.d(TAG, "Delivering batch: ${snapshot.size} device(s)")
        }
        onBatch?.invoke(snapshot)
    }

    private fun buildScanCallback(): ScanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val rssi = result.rssi
            if (rssi < minRssi) return // filter weak signals

            val deviceId = result.device.address ?: return
            val ts = System.currentTimeMillis() / 1000

            val existing = deviceMap[deviceId]
            // Update only if RSSI improved or entry is new
            if (existing == null || rssi > existing.rssi) {
                deviceMap[deviceId] = ScannedDevice(deviceId, rssi, ts)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            Log.e(TAG, "Scan failed with error code: $errorCode")
        }
    }

    private fun hasPermissions(): Boolean {
        val perms = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT
            )
        } else {
            arrayOf(
                Manifest.permission.BLUETOOTH,
                Manifest.permission.BLUETOOTH_ADMIN,
                Manifest.permission.ACCESS_FINE_LOCATION
            )
        }
        return perms.all {
            ContextCompat.checkSelfPermission(context, it) == PackageManager.PERMISSION_GRANTED
        }
    }
}
