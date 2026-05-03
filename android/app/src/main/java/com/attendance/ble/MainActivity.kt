package com.attendance.ble

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.View
import android.widget.*
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.*

/**
 * MainActivity
 *
 * Single-screen UI:
 *  ┌─────────────────────────────────────────────┐
 *  │  ESP8266 IP: [_______________] [Connect]     │
 *  │  Status: ●  Connected / Disconnected         │
 *  │  ─────────────────────────────────────────   │
 *  │  [▶ Start Scanning]  [■ Stop Scanning]       │
 *  │  Devices found: 3                            │
 *  │  ─────────────────────────────────────────   │
 *  │  AA:BB:CC:DD    -62 dBm                      │
 *  │  11:22:33:44    -71 dBm                      │
 *  └─────────────────────────────────────────────┘
 *
 * The BLE scanner sends detected devices to the ESP8266 every [SYNC_INTERVAL_MS].
 */
class MainActivity : AppCompatActivity() {

    companion object {
        private const val SYNC_INTERVAL_MS = 5_000L
        private const val RECONNECT_INTERVAL_MS = 10_000L
    }

    // -----------------------------------------------------------------------
    // Views (found via findViewById; no ViewBinding dependency on layout)
    // -----------------------------------------------------------------------
    private lateinit var etIpAddress: EditText
    private lateinit var btnConnect: Button
    private lateinit var tvStatus: TextView
    private lateinit var btnStartScan: Button
    private lateinit var btnStopScan: Button
    private lateinit var tvDeviceCount: TextView
    private lateinit var listDevices: ListView
    private lateinit var progressBar: ProgressBar

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    private val bleScanner by lazy { BLEScanner(this) }
    private val deviceAdapter by lazy { ArrayAdapter<String>(this, android.R.layout.simple_list_item_1) }

    private var isScanning = false
    private var isConnected = false
    private var syncJob: Job? = null
    private var reconnectJob: Job? = null

    // -----------------------------------------------------------------------
    // Permission launcher
    // -----------------------------------------------------------------------
    private val permLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { results ->
        if (results.values.all { it }) {
            startScanning()
        } else {
            toast("BLE permissions are required")
        }
    }

    private val enableBtLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == RESULT_OK) requestPermissionsAndScan()
        else toast("Bluetooth must be enabled")
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(buildLayout())

        listDevices.adapter = deviceAdapter

        btnConnect.setOnClickListener {
            val ip = etIpAddress.text.toString().trim()
            if (ip.isEmpty()) { toast("Enter ESP8266 IP"); return@setOnClickListener }
            ApiClient.baseUrl = "http://$ip"
            checkConnection()
            startReconnectLoop()
        }

        btnStartScan.setOnClickListener { requestPermissionsAndScan() }
        btnStopScan.setOnClickListener  { stopScanning() }

        updateScanButtons()
    }

    override fun onDestroy() {
        super.onDestroy()
        stopScanning()
        reconnectJob?.cancel()
    }

    // -----------------------------------------------------------------------
    // BLE scanning
    // -----------------------------------------------------------------------

    private fun requestPermissionsAndScan() {
        val bm = getSystemService(BLUETOOTH_SERVICE) as? BluetoothManager
        val adapter = bm?.adapter
        if (adapter == null || !adapter.isEnabled) {
            enableBtLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            return
        }
        val needed = requiredPermissions().filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (needed.isEmpty()) startScanning()
        else permLauncher.launch(needed.toTypedArray())
    }

    private fun startScanning() {
        if (isScanning) return
        isScanning = true
        updateScanButtons()

        bleScanner.start { devices ->
            // Update UI
            deviceAdapter.clear()
            devices.forEach { deviceAdapter.add("${it.deviceId}   ${it.rssi} dBm") }
            tvDeviceCount.text = "Devices found: ${devices.size}"
        }

        // Sync loop – send current device list to ESP8266 every SYNC_INTERVAL_MS
        syncJob = lifecycleScope.launch {
            while (isActive) {
                delay(SYNC_INTERVAL_MS)
                val devices = bleScanner.getDevices()
                if (devices.isNotEmpty()) {
                    val marked = ApiClient.sendBatch(devices)
                    if (marked > 0) toast("Marked $marked new student(s) present")
                }
            }
        }
    }

    private fun stopScanning() {
        if (!isScanning) return
        isScanning = false
        syncJob?.cancel()
        syncJob = null
        bleScanner.stop()
        updateScanButtons()
        deviceAdapter.clear()
        tvDeviceCount.text = "Devices found: 0"
    }

    // -----------------------------------------------------------------------
    // Connectivity & reconnect loop
    // -----------------------------------------------------------------------

    private fun checkConnection() {
        lifecycleScope.launch {
            val reachable = ApiClient.isReachable()
            isConnected = reachable
            updateStatus()
        }
    }

    private fun startReconnectLoop() {
        reconnectJob?.cancel()
        reconnectJob = lifecycleScope.launch {
            while (isActive) {
                delay(RECONNECT_INTERVAL_MS)
                val reachable = ApiClient.isReachable()
                if (reachable != isConnected) {
                    isConnected = reachable
                    updateStatus()
                    if (reachable) toast("Reconnected to ESP8266")
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // UI helpers
    // -----------------------------------------------------------------------

    private fun updateStatus() {
        val dot = if (isConnected) "🟢" else "🔴"
        val text = if (isConnected) "Connected to ${ApiClient.baseUrl}" else "Disconnected"
        tvStatus.text = "$dot $text"
    }

    private fun updateScanButtons() {
        btnStartScan.isEnabled = !isScanning
        btnStopScan.isEnabled  = isScanning
    }

    private fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()

    private fun requiredPermissions(): List<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            listOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT
            )
        } else {
            listOf(
                Manifest.permission.BLUETOOTH,
                Manifest.permission.BLUETOOTH_ADMIN,
                Manifest.permission.ACCESS_FINE_LOCATION
            )
        }

    // -----------------------------------------------------------------------
    // Layout built programmatically to avoid xml file dependency
    // -----------------------------------------------------------------------

    private fun buildLayout(): android.widget.LinearLayout {
        val root = android.widget.LinearLayout(this).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            setPadding(32, 48, 32, 16)
            setBackgroundColor(android.graphics.Color.WHITE)
        }

        // IP row
        val ipRow = android.widget.LinearLayout(this).apply {
            orientation = android.widget.LinearLayout.HORIZONTAL
        }
        etIpAddress = EditText(this).apply {
            hint = "ESP8266 IP (e.g. 192.168.4.1)"
            setText("192.168.4.1")
            layoutParams = android.widget.LinearLayout.LayoutParams(
                0, android.widget.LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        }
        btnConnect = Button(this).apply { text = "Connect" }
        ipRow.addView(etIpAddress)
        ipRow.addView(btnConnect)
        root.addView(ipRow)

        // Status
        tvStatus = TextView(this).apply {
            text = "🔴 Disconnected"
            setPadding(0, 12, 0, 12)
        }
        root.addView(tvStatus)

        // Divider
        root.addView(divider())

        // Scan buttons
        val scanRow = android.widget.LinearLayout(this).apply {
            orientation = android.widget.LinearLayout.HORIZONTAL
            setPadding(0, 12, 0, 0)
        }
        btnStartScan = Button(this).apply {
            text = "▶ Start Scan"
            layoutParams = android.widget.LinearLayout.LayoutParams(
                0, android.widget.LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply {
                    setMargins(0, 0, 8, 0)
                }
        }
        btnStopScan = Button(this).apply {
            text = "■ Stop Scan"
            layoutParams = android.widget.LinearLayout.LayoutParams(
                0, android.widget.LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        }
        scanRow.addView(btnStartScan)
        scanRow.addView(btnStopScan)
        root.addView(scanRow)

        // Device count
        tvDeviceCount = TextView(this).apply {
            text = "Devices found: 0"
            setPadding(0, 16, 0, 8)
        }
        root.addView(tvDeviceCount)

        // Progress bar (visible during scan)
        progressBar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            isIndeterminate = true
            visibility = View.GONE
        }
        root.addView(progressBar)

        root.addView(divider())

        // Device list
        listDevices = ListView(this).apply {
            layoutParams = android.widget.LinearLayout.LayoutParams(
                android.widget.LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
        }
        root.addView(listDevices)

        return root
    }

    private fun divider() = View(this).apply {
        layoutParams = android.widget.LinearLayout.LayoutParams(
            android.widget.LinearLayout.LayoutParams.MATCH_PARENT, 2).apply {
                setMargins(0, 8, 0, 8)
            }
        setBackgroundColor(android.graphics.Color.LTGRAY)
    }
}
