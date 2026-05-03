package com.attendance.ble

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONObject
import java.io.IOException
import java.util.concurrent.TimeUnit

private const val TAG = "ApiClient"

/**
 * ApiClient
 *
 * Lightweight OkHttp-based client for ESP8266 REST API.
 * All network calls are suspend functions – run them from a coroutine.
 *
 * Configuration:
 *   ApiClient.baseUrl   = "http://192.168.4.1"   // ESP8266 IP or AP address
 *   ApiClient.authToken = "esp8266-attend-secret" // must match firmware
 *
 * Example:
 *   val result = ApiClient.sendScan(deviceId = "AA:BB:CC:DD", rssi = -65, timestamp = 1710000000)
 */
object ApiClient {

    /** ESP8266 base URL – set before first use. */
    var baseUrl: String = "http://192.168.4.1"

    /** Bearer token – must match firmware's AUTH_TOKEN.
     *  NOTE: In production, load this from encrypted SharedPreferences
     *  or prompt the user to enter it on first launch. */
    var authToken: String = "esp8266-attend-secret"

    // Sealed result type avoids throwing exceptions across coroutine boundaries
    sealed class Result<out T> {
        data class Success<T>(val data: T) : Result<T>()
        data class Error(val message: String) : Result<Nothing>()
    }

    // -----------------------------------------------------------------------
    // OkHttp client (shared, thread-safe)
    // -----------------------------------------------------------------------

    private val JSON_MEDIA_TYPE = "application/json; charset=utf-8".toMediaType()

    private val client: OkHttpClient by lazy {
        OkHttpClient.Builder()
            .connectTimeout(5, TimeUnit.SECONDS)
            .readTimeout(10, TimeUnit.SECONDS)
            .writeTimeout(10, TimeUnit.SECONDS)
            .retryOnConnectionFailure(true)
            .build()
    }

    // -----------------------------------------------------------------------
    // Public API methods
    // -----------------------------------------------------------------------

    /**
     * POST /scan
     * Send a single BLE device hit to ESP8266.
     */
    suspend fun sendScan(
        deviceId: String,
        rssi: Int,
        timestamp: Long
    ): Result<Boolean> = withContext(Dispatchers.IO) {
        val body = JSONObject().apply {
            put("device_id", deviceId)
            put("rssi",      rssi)
            put("timestamp", timestamp)
        }
        val res = post("/scan", body) ?: return@withContext Result.Error("Network error")
        return@withContext Result.Success(res.optBoolean("marked", false))
    }

    /**
     * POST /scan – send a batch of devices in parallel (one request each).
     * Returns a count of how many were newly marked present.
     */
    suspend fun sendBatch(devices: List<BLEScanner.ScannedDevice>): Int {
        var markedCount = 0
        for (device in devices) {
            val r = sendScan(device.deviceId, device.rssi, device.timestamp)
            if (r is Result.Success && r.data) markedCount++
        }
        return markedCount
    }

    /**
     * POST /register
     * Register a new student on the ESP8266.
     */
    suspend fun registerStudent(name: String, deviceId: String): Result<String> =
        withContext(Dispatchers.IO) {
            val body = JSONObject().apply {
                put("name",      name)
                put("device_id", deviceId)
            }
            val res = post("/register", body)
                ?: return@withContext Result.Error("Network error")
            return@withContext Result.Success(res.optString("ok", "registered"))
        }

    /**
     * GET /session
     * Returns the current session status as a [JSONObject].
     */
    suspend fun getSession(): Result<JSONObject> = withContext(Dispatchers.IO) {
        val res = get("/session") ?: return@withContext Result.Error("Network error")
        return@withContext Result.Success(res)
    }

    /**
     * POST /start-session
     */
    suspend fun startSession(): Result<JSONObject> = withContext(Dispatchers.IO) {
        val res = post("/start-session", JSONObject())
            ?: return@withContext Result.Error("Network error")
        return@withContext Result.Success(res)
    }

    /**
     * POST /stop-session
     */
    suspend fun stopSession(): Result<JSONObject> = withContext(Dispatchers.IO) {
        val res = post("/stop-session", JSONObject())
            ?: return@withContext Result.Error("Network error")
        return@withContext Result.Success(res)
    }

    /**
     * Quick connectivity check: GET /session and return true if it succeeds.
     */
    suspend fun isReachable(): Boolean = withContext(Dispatchers.IO) {
        try {
            val req = Request.Builder()
                .url("$baseUrl/session")
                .addHeader("Authorization", "Bearer $authToken")
                .get()
                .build()
            client.newCall(req).execute().use { it.isSuccessful }
        } catch (e: IOException) {
            false
        }
    }

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    private fun post(path: String, json: JSONObject): JSONObject? {
        return try {
            val body = json.toString().toRequestBody(JSON_MEDIA_TYPE)
            val req = Request.Builder()
                .url("$baseUrl$path")
                .addHeader("Authorization", "Bearer $authToken")
                .post(body)
                .build()
            client.newCall(req).execute().use { res ->
                if (!res.isSuccessful) {
                    Log.w(TAG, "POST $path → HTTP ${res.code}")
                    return null
                }
                JSONObject(res.body?.string() ?: "{}")
            }
        } catch (e: Exception) {
            Log.e(TAG, "POST $path failed: ${e.message}")
            null
        }
    }

    private fun get(path: String): JSONObject? {
        return try {
            val req = Request.Builder()
                .url("$baseUrl$path")
                .addHeader("Authorization", "Bearer $authToken")
                .get()
                .build()
            client.newCall(req).execute().use { res ->
                if (!res.isSuccessful) {
                    Log.w(TAG, "GET $path → HTTP ${res.code}")
                    return null
                }
                JSONObject(res.body?.string() ?: "{}")
            }
        } catch (e: Exception) {
            Log.e(TAG, "GET $path failed: ${e.message}")
            null
        }
    }
}
