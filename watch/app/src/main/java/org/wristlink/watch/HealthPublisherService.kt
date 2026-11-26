package org.wristlink.watch

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattServer
import android.bluetooth.BluetoothGattServerCallback
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertiseSettings
import android.bluetooth.le.BluetoothLeAdvertiser
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.Ringtone
import android.media.RingtoneManager
import android.os.BatteryManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.ParcelUuid
import android.os.SystemClock
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.health.services.client.HealthServices
import androidx.health.services.client.PassiveListenerCallback
import androidx.health.services.client.PassiveMonitoringClient
import androidx.health.services.client.data.DataPointContainer
import androidx.health.services.client.data.DataType
import androidx.health.services.client.data.PassiveListenerConfig
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID

class HealthPublisherService : Service(), SensorEventListener {
    private val handler = Handler(Looper.getMainLooper())
    private var sensorManager: SensorManager? = null
    private var passiveClient: PassiveMonitoringClient? = null
    private var server: BluetoothGattServer? = null
    private var advertiser: BluetoothLeAdvertiser? = null
    private var advertising = false
    private var central: BluetoothDevice? = null
    private var preparedAlert: ByteArray? = null
    private var closed = false
    private var serviceAddFailures = 0
    private var sensorsStarted = false
    private var idleStatus = "Waiting for the phone"
    private var phoneBattery = -1
    private var phoneCharging = false

    private var steps = 0
    private var healthSteps = 0
    private var sensorSteps = 0
    private var hasHealthSteps = false
    private var calories = 0
    private var distanceMeters = 0
    private var heartRate = 0
    private var floors = 0
    private var flags = FLAG_BATTERY
    private var stepBaseline = -1
    private var dayKey = ""

    private val alertsByApp = linkedMapOf<String, MutableSet<Int>>()
    private val colorByApp = hashMapOf<String, Int>()
    private val summaryIds = hashMapOf<String, Int>()
    private var nextSummaryId = SUMMARY_NOTIFY_BASE

    private var ringing = false
    private var lastRingMs = 0L
    private var ringtone: Ringtone? = null
    private var vibrator: Vibrator? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_ALERT_DISMISSED -> forgetAlert(intent.getIntExtra(EXTRA_ALERT_ID, 0))
            ACTION_RESTART_LINK -> handler.post { restartServer() }
        }
        return START_STICKY
    }

    override fun onCreate() {
        super.onCreate()
        createChannels()
        val types = if (Build.VERSION.SDK_INT >= 34) {
            ServiceInfo.FOREGROUND_SERVICE_TYPE_HEALTH or
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        } else {
            ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        }
        ServiceCompat.startForeground(this, 1, notification("Waiting for the phone"), types)
        val filter = IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(bluetoothReceiver, filter, RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(bluetoothReceiver, filter)
        }
        startSensors()
        startHealthServices()
        openServer()
        handler.post(flushRunnable)
    }

    override fun onDestroy() {
        closed = true
        handler.removeCallbacksAndMessages(null)
        stopCallRing()
        stopAdvertising()
        try {
            server?.close()
        } catch (_: Exception) {
        }
        server = null
        try {
            unregisterReceiver(bluetoothReceiver)
        } catch (_: Exception) {
        }
        sensorManager?.unregisterListener(this)
        try {
            HealthServices.getClient(this).passiveMonitoringClient.clearPassiveListenerCallbackAsync()
        } catch (_: Exception) {
        }
        super.onDestroy()
    }

    private val flushRunnable = object : Runnable {
        override fun run() {
            try {
                passiveClient?.flushAsync()
            } catch (_: Exception) {
            }
            handler.postDelayed(this, FLUSH_INTERVAL_MS)
        }
    }

    private val advertiseRetry = Runnable {
        if (!closed && central == null)
            startAdvertising()
    }

    private val ringTimeout = object : Runnable {
        override fun run() {
            if (!ringing) return
            if (SystemClock.elapsedRealtime() - lastRingMs > 8000) {
                stopCallRing()
                return
            }
            handler.postDelayed(this, 2000)
        }
    }

    private val bluetoothReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != BluetoothAdapter.ACTION_STATE_CHANGED) return
            when (intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR)) {
                BluetoothAdapter.STATE_ON -> if (!closed) openServer()
                BluetoothAdapter.STATE_OFF -> {
                    central = null
                    stopAdvertising()
                    try {
                        server?.close()
                    } catch (_: Exception) {
                    }
                    server = null
                    updateNotification("Bluetooth is off")
                }
            }
        }
    }

    private val serverCallback = object : BluetoothGattServerCallback() {
        override fun onConnectionStateChange(device: BluetoothDevice, status: Int, newState: Int) {
            handler.post {
                if (closed) return@post
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    central = device
                    stopAdvertising()
                    updateNotification("Phone connected")
                } else if (device.address == central?.address || central == null) {
                    central = null
                    preparedAlert = null
                    if (!ringing)
                        updateNotification("Waiting for the phone")
                    startAdvertising()
                }
            }
        }

        override fun onServiceAdded(status: Int, service: BluetoothGattService) {
            handler.post {
                if (closed) return@post
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    Log.w(TAG, "service add $status")
                    try {
                        server?.close()
                    } catch (_: Exception) {
                    }
                    server = null
                    if (++serviceAddFailures <= 3)
                        handler.postDelayed({ openServer() }, 5000)
                    else
                        updateNotification("Could not publish WristLink over Bluetooth")
                    return@post
                }
                serviceAddFailures = 0
                val already = connectedCentrals()
                if (already.isNotEmpty()) {
                    central = already[0]
                    updateNotification("Phone connected")
                } else {
                    startAdvertising()
                }
            }
        }

        override fun onCharacteristicReadRequest(
            device: BluetoothDevice,
            requestId: Int,
            offset: Int,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (characteristic.uuid != METRICS_UUID) {
                respond(device, requestId, BluetoothGatt.GATT_FAILURE, offset, null)
                return
            }
            handler.post {
                if (closed) return@post
                val bytes = payload()
                val slice = when {
                    offset <= 0 -> bytes
                    offset >= bytes.size -> ByteArray(0)
                    else -> bytes.copyOfRange(offset, bytes.size)
                }
                respond(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, slice)
            }
        }

        override fun onCharacteristicWriteRequest(
            device: BluetoothDevice,
            requestId: Int,
            characteristic: BluetoothGattCharacteristic,
            preparedWrite: Boolean,
            responseNeeded: Boolean,
            offset: Int,
            value: ByteArray
        ) {
            if (characteristic.uuid != ALERT_UUID) {
                if (responseNeeded)
                    respond(device, requestId, BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED, offset, null)
                return
            }
            if (preparedWrite || offset > 0) {
                preparedAlert = merge(preparedAlert, offset, value)
                if (responseNeeded)
                    respond(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, null)
                return
            }
            val bytes = value.copyOf()
            handler.post {
                if (responseNeeded)
                    respond(device, requestId, BluetoothGatt.GATT_SUCCESS, 0, null)
                if (!closed)
                    handleAlert(bytes)
            }
        }

        override fun onExecuteWrite(device: BluetoothDevice, requestId: Int, execute: Boolean) {
            val bytes = if (execute) preparedAlert?.copyOf() else null
            preparedAlert = null
            handler.post {
                if (bytes != null && !closed)
                    handleAlert(bytes)
                respond(device, requestId, BluetoothGatt.GATT_SUCCESS, 0, null)
            }
        }
    }

    private val advertiseCallback = object : AdvertiseCallback() {
        override fun onStartSuccess(settingsInEffect: AdvertiseSettings) {
            handler.post {
                if (closed || central != null) {
                    stopAdvertising()
                    return@post
                }
                advertising = true
                handler.removeCallbacks(advertiseRetry)
            }
        }

        override fun onStartFailure(errorCode: Int) {
            handler.post {
                advertising = false
                Log.w(TAG, "advertise failed $errorCode")
                if (!closed && central == null) {
                    updateNotification("Waiting to advertise…")
                    handler.removeCallbacks(advertiseRetry)
                    handler.postDelayed(advertiseRetry, 15_000)
                }
            }
        }
    }

    private fun bluetoothAdapter(): BluetoothAdapter? {
        val manager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        return manager.adapter
    }

    private fun connectedCentrals(): List<BluetoothDevice> {
        val manager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        return try {
            manager.getConnectedDevices(BluetoothProfile.GATT_SERVER)
        } catch (_: SecurityException) {
            emptyList()
        }
    }

    private fun openServer() {
        if (closed || server != null) return
        val manager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        val adapter = manager.adapter
        if (adapter == null || !adapter.isEnabled) {
            updateNotification("Turn Bluetooth on")
            return
        }
        try {
            server = manager.openGattServer(this, serverCallback)
        } catch (error: SecurityException) {
            Log.w(TAG, "gatt server blocked", error)
            updateNotification("Allow Bluetooth, then reopen WristLink")
            return
        }
        if (server == null) {
            updateNotification("Could not open Bluetooth")
            return
        }
        val service = BluetoothGattService(SERVICE_UUID, BluetoothGattService.SERVICE_TYPE_PRIMARY)
        service.addCharacteristic(
            BluetoothGattCharacteristic(
                METRICS_UUID,
                BluetoothGattCharacteristic.PROPERTY_READ,
                BluetoothGattCharacteristic.PERMISSION_READ
            )
        )
        service.addCharacteristic(
            BluetoothGattCharacteristic(
                ALERT_UUID,
                BluetoothGattCharacteristic.PROPERTY_WRITE or
                    BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE,
                BluetoothGattCharacteristic.PERMISSION_WRITE
            )
        )
        try {
            server?.addService(service)
        } catch (error: SecurityException) {
            Log.w(TAG, "add service blocked", error)
            updateNotification("Allow Bluetooth, then reopen WristLink")
        }
    }

    private fun restartServer() {
        if (closed) return
        serviceAddFailures = 0
        central = null
        preparedAlert = null
        stopAdvertising()
        try {
            server?.close()
        } catch (_: Exception) {
        }
        server = null
        updateNotification("Restarting WristLink…")
        openServer()
    }

    private fun startAdvertising() {
        if (closed || central != null || advertising || server == null) return
        val adapter = bluetoothAdapter()
        if (adapter == null || !adapter.isEnabled) {
            updateNotification("Turn Bluetooth on")
            return
        }
        val ble = adapter.bluetoothLeAdvertiser
        if (ble == null) {
            updateNotification("This watch cannot accept a Bluetooth connection")
            return
        }
        advertiser = ble
        // Balanced + higher TX so Sailfish/BlueZ can see the LE advert after
        // dropping a classic ACL (Pixel Watch stays on BR/EDR otherwise).
        val settings = AdvertiseSettings.Builder()
            .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_BALANCED)
            .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_HIGH)
            .setConnectable(true)
            .setTimeout(0)
            .build()
        val data = AdvertiseData.Builder()
            .addServiceUuid(ParcelUuid(SERVICE_UUID))
            .setIncludeDeviceName(false)
            .build()
        val scanResponse = AdvertiseData.Builder()
            .setIncludeDeviceName(true)
            .build()
        try {
            ble.startAdvertising(settings, data, scanResponse, advertiseCallback)
            advertising = true
            updateNotification("Waiting for the phone")
        } catch (error: SecurityException) {
            advertising = false
            Log.w(TAG, "advertise blocked", error)
            updateNotification("Allow Bluetooth, then reopen WristLink")
        } catch (error: IllegalStateException) {
            advertising = false
            Log.w(TAG, "advertise failed", error)
            updateNotification("Turn Bluetooth on")
        } catch (error: IllegalArgumentException) {
            // Some Wear builds reject the scan response; advertise without it.
            Log.w(TAG, "advertise with name failed, retrying", error)
            try {
                ble.startAdvertising(settings, data, advertiseCallback)
                advertising = true
                updateNotification("Waiting for the phone")
            } catch (retry: Exception) {
                advertising = false
                Log.w(TAG, "advertise failed", retry)
                updateNotification("Waiting to advertise…")
                handler.removeCallbacks(advertiseRetry)
                handler.postDelayed(advertiseRetry, 15_000)
            }
        }
    }

    private fun stopAdvertising() {
        advertising = false
        handler.removeCallbacks(advertiseRetry)
        try {
            advertiser?.stopAdvertising(advertiseCallback)
        } catch (_: Exception) {
        }
    }

    private fun respond(
        device: BluetoothDevice,
        requestId: Int,
        status: Int,
        offset: Int,
        value: ByteArray?
    ) {
        try {
            server?.sendResponse(device, requestId, status, offset, value)
        } catch (error: SecurityException) {
            Log.w(TAG, "response blocked", error)
        }
    }

    private fun merge(existing: ByteArray?, offset: Int, value: ByteArray): ByteArray {
        val base = existing ?: ByteArray(0)
        val size = maxOf(base.size, offset + value.size)
        val merged = base.copyOf(size)
        value.copyInto(merged, destinationOffset = offset)
        return merged
    }

    private fun todayKey(): String = SimpleDateFormat("yyyyMMdd", Locale.US).format(Date())

    private fun rolloverIfNeeded() {
        val today = todayKey()
        if (dayKey == today) return
        dayKey = today
        healthSteps = 0
        sensorSteps = 0
        hasHealthSteps = false
        steps = 0
        calories = 0
        distanceMeters = 0
        floors = 0
        stepBaseline = -1
        flags = FLAG_BATTERY
        getSharedPreferences("health", MODE_PRIVATE).edit().remove("baseline").putString("day", today).apply()
    }

    private fun refreshStepTotal() {
        steps = if (hasHealthSteps) healthSteps else sensorSteps
        if (hasHealthSteps || steps > 0) flags = flags or FLAG_STEPS
    }

    private fun startSensors() {
        if (sensorsStarted) return
        sensorsStarted = true
        sensorManager = getSystemService(Context.SENSOR_SERVICE) as SensorManager
        // Step counter is batched and cheap. Do NOT register TYPE_HEART_RATE —
        // that keeps the optical sensor lit and drains a watch overnight.
        val step = sensorManager?.getDefaultSensor(Sensor.TYPE_STEP_COUNTER)
        if (step != null)
            sensorManager?.registerListener(this, step, SensorManager.SENSOR_DELAY_NORMAL)
    }

    private fun startHealthServices() {
        try {
            val config = PassiveListenerConfig.builder()
                .setDataTypes(
                    setOf(
                        DataType.STEPS_DAILY,
                        DataType.CALORIES_DAILY,
                        DataType.DISTANCE_DAILY,
                        DataType.FLOORS_DAILY,
                        DataType.HEART_RATE_BPM
                    )
                )
                .build()
            val client = HealthServices.getClient(this).passiveMonitoringClient
            passiveClient = client
            client.setPassiveListenerCallback(config, passiveCallback)
            client.flushAsync()
        } catch (error: Exception) {
            Log.w(TAG, "Health Services unavailable", error)
        }
    }

    private fun highest(values: List<Int>, current: Int): Int {
        var best = current
        for (value in values) {
            if (value > best) best = value
        }
        return best
    }

    private val passiveCallback = object : PassiveListenerCallback {
        override fun onNewDataPointsReceived(dataPoints: DataPointContainer) {
            val dailySteps = dataPoints.getData(DataType.STEPS_DAILY).map { it.value.toInt() }
            val dailyCalories = dataPoints.getData(DataType.CALORIES_DAILY).map { it.value.toInt() }
            val dailyDistance = dataPoints.getData(DataType.DISTANCE_DAILY).map { it.value.toInt() }
            val dailyFloors = dataPoints.getData(DataType.FLOORS_DAILY).map { it.value.toInt() }
            val heart = dataPoints.getData(DataType.HEART_RATE_BPM).lastOrNull()?.value?.toInt()
            handler.post {
                rolloverIfNeeded()
                if (dailySteps.isNotEmpty()) {
                    healthSteps = highest(dailySteps, healthSteps)
                    hasHealthSteps = true
                }
                if (dailyCalories.isNotEmpty()) {
                    calories = highest(dailyCalories, calories)
                    flags = flags or FLAG_CALORIES
                }
                if (dailyDistance.isNotEmpty()) {
                    distanceMeters = highest(dailyDistance, distanceMeters)
                    flags = flags or FLAG_DISTANCE
                }
                if (dailyFloors.isNotEmpty()) {
                    floors = highest(dailyFloors, floors)
                    flags = flags or FLAG_FLOORS
                }
                if (heart != null && heart > 0) {
                    heartRate = heart
                    flags = flags or FLAG_HEART
                }
                refreshStepTotal()
            }
        }
    }

    override fun onSensorChanged(event: SensorEvent?) {
        val sensorEvent = event ?: return
        if (sensorEvent.sensor.type != Sensor.TYPE_STEP_COUNTER) return
        val reading = sensorEvent.values[0]
        handler.post { applySensor(reading) }
    }

    private fun applySensor(reading: Float) {
        rolloverIfNeeded()
        val total = reading.toInt()
        val prefs = getSharedPreferences("health", MODE_PRIVATE)
        val today = todayKey()
        if (prefs.getString("day", "") != today) {
            prefs.edit().putString("day", today).putInt("baseline", total).apply()
            stepBaseline = total
        } else if (stepBaseline < 0) {
            stepBaseline = prefs.getInt("baseline", total)
            if (prefs.getInt("baseline", -1) < 0) {
                prefs.edit().putInt("baseline", total).apply()
                stepBaseline = total
            }
        }
        sensorSteps = (total - stepBaseline).coerceAtLeast(0)
        refreshStepTotal()
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit

    private fun batteryPercent(): Int {
        val filter = IntentFilter(Intent.ACTION_BATTERY_CHANGED)
        val intent = registerReceiver(null, filter) ?: return 0
        val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, 0)
        val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, 100).coerceAtLeast(1)
        return (level * 100) / scale
    }

    private fun payload(): ByteArray {
        rolloverIfNeeded()
        refreshStepTotal()
        val battery = batteryPercent()
        flags = flags or FLAG_BATTERY
        val buffer = ByteBuffer.allocate(18).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(steps)
        buffer.putInt(calories)
        buffer.putInt(distanceMeters)
        buffer.putShort(heartRate.toShort())
        buffer.putShort(floors.toShort())
        buffer.put(battery.toByte())
        buffer.put(flags.toByte())
        return buffer.array()
    }

    private fun handleAlert(value: ByteArray) {
        if (value.size < 2) return
        val version = value[0].toInt() and 0xff
        if (version != 1) return
        val alertFlags = value[1].toInt() and 0xff
        if (alertFlags and 0x04 != 0) {
            if (value.size < 4) return
            phoneBattery = (value[2].toInt() and 0xff).coerceIn(0, 100)
            phoneCharging = value[3].toInt() and 0xff != 0
            if (!ringing)
                updateNotification(idleStatus)
            return
        }
        if (value.size < 8) return
        val buffer = ByteBuffer.wrap(value).order(ByteOrder.LITTLE_ENDIAN)
        buffer.get()
        buffer.get()
        val id = buffer.int
        val manager = getSystemService(NotificationManager::class.java)
        if (alertFlags and 0x02 != 0) {
            val app = readField(buffer)
            val title = readField(buffer).ifEmpty { app.ifEmpty { "Incoming call" } }
            val body = readField(buffer)
            startCallRing(title, body)
            return
        }
        if (alertFlags and 0x08 != 0) {
            val notifyId = alertNotifyId(id)
            manager.cancel(notifyId)
            forgetAlert(notifyId)
            return
        }
        if (alertFlags and 0x01 != 0) {
            stopCallRing()
            val notifyId = alertNotifyId(id)
            manager.cancel(notifyId)
            forgetAlert(notifyId)
            return
        }
        val app = readField(buffer).ifEmpty { "Phone" }
        val title = readField(buffer)
        val body = readField(buffer)
        val notifyId = alertNotifyId(id)
        val message = alertMessage(app, title, body)
        val provided = if (buffer.remaining() >= 4) buffer.int else 0
        val accent = AppAppearance.color(app, provided)
        colorByApp[app] = accent
        val badge = AppAppearance.badge(this, accent)
        val dismiss = PendingIntent.getService(
            this,
            notifyId,
            Intent(this, HealthPublisherService::class.java).apply {
                action = ACTION_ALERT_DISMISSED
                putExtra(EXTRA_ALERT_ID, notifyId)
            },
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val posted = NotificationCompat.Builder(this, ALERT_CHANNEL)
            .setContentTitle(app)
            .setContentText(message)
            .setStyle(
                NotificationCompat.BigTextStyle()
                    .setBigContentTitle(app)
                    .bigText(message)
            )
            .setSmallIcon(R.drawable.ic_notification)
            .setLargeIcon(badge)
            .setColor(accent)
            .setGroup(AppAppearance.groupKey(app))
            .setSortKey(app.lowercase())
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_MESSAGE)
            .setAutoCancel(true)
            .setDeleteIntent(dismiss)
            .build()
        manager.notify(notifyId, posted)
        rememberAlert(app, notifyId)
        postAppSummary(manager, app)
    }

    private fun alertMessage(app: String, title: String, body: String): String {
        val lines = listOf(title, body)
            .map { it.trim() }
            .filter { it.isNotEmpty() && !it.equals(app, ignoreCase = true) }
            .distinct()
        return if (lines.isEmpty()) app else lines.joinToString("\n")
    }

    private fun rememberAlert(app: String, notifyId: Int) {
        alertsByApp.getOrPut(app) { linkedSetOf() }.add(notifyId)
    }

    private fun forgetAlert(notifyId: Int) {
        val app = alertsByApp.entries.firstOrNull { notifyId in it.value }?.key ?: return
        val ids = alertsByApp.getValue(app)
        ids.remove(notifyId)
        val manager = getSystemService(NotificationManager::class.java)
        if (ids.isEmpty()) {
            alertsByApp.remove(app)
            summaryIds[app]?.let { manager.cancel(it) }
        } else {
            postAppSummary(manager, app)
        }
    }

    private fun postAppSummary(manager: NotificationManager, app: String) {
        val count = alertsByApp[app]?.size ?: 0
        if (count <= 1) {
            summaryIds[app]?.let { manager.cancel(it) }
            return
        }
        val summaryId = summaryIds.getOrPut(app) { nextSummaryId++ }
        val text = "$count notifications"
        val accent = colorByApp[app] ?: AppAppearance.color(app)
        val summary = NotificationCompat.Builder(this, ALERT_CHANNEL)
            .setContentTitle(app)
            .setContentText(text)
            .setSmallIcon(R.drawable.ic_notification)
            .setLargeIcon(AppAppearance.badge(this, accent))
            .setColor(accent)
            .setGroup(AppAppearance.groupKey(app))
            .setGroupSummary(true)
            .setGroupAlertBehavior(NotificationCompat.GROUP_ALERT_CHILDREN)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_MESSAGE)
            .build()
        manager.notify(summaryId, summary)
    }

    private fun alertNotifyId(id: Int): Int = ALERT_NOTIFY_BASE + (id and 0x7fffffff)

    private fun readField(buffer: ByteBuffer): String {
        if (buffer.remaining() < 2) return ""
        val length = buffer.short.toInt() and 0xffff
        val take = length.coerceAtMost(buffer.remaining())
        val bytes = ByteArray(take)
        buffer.get(bytes)
        return String(bytes, Charsets.UTF_8)
    }

    private fun startCallRing(title: String, body: String) {
        lastRingMs = SystemClock.elapsedRealtime()
        showCallNotification(title, body)
        if (ringing) return
        ringing = true
        val audio = getSystemService(AudioManager::class.java)
        if (audio.ringerMode == AudioManager.RINGER_MODE_NORMAL
            && audio.getStreamVolume(AudioManager.STREAM_RING) > 0
        ) {
            val uri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)
            if (uri != null) {
                ringtone = RingtoneManager.getRingtone(applicationContext, uri)?.apply {
                    audioAttributes = ringtoneAttributes()
                    isLooping = true
                    play()
                }
            }
        }
        if (shouldVibrateForCall(audio)) {
            vibrator = deviceVibrator()
            vibrator?.vibrate(
                VibrationEffect.createWaveform(longArrayOf(0, 800, 400, 800, 1200), 0),
                ringtoneAttributes()
            )
        }
        openCallScreen(title, body, false)
        handler.removeCallbacks(ringTimeout)
        handler.postDelayed(ringTimeout, 2000)
        updateNotification("Phone is ringing")
    }

    private fun stopCallRing() {
        handler.removeCallbacks(ringTimeout)
        if (!ringing && ringtone == null) {
            getSystemService(NotificationManager::class.java).cancel(CALL_NOTIFICATION_ID)
            return
        }
        ringing = false
        try {
            ringtone?.stop()
        } catch (_: Exception) {
        }
        ringtone = null
        vibrator?.cancel()
        getSystemService(NotificationManager::class.java).cancel(CALL_NOTIFICATION_ID)
        openCallScreen("", "", true)
        updateNotification(if (central != null) "Phone connected" else "Waiting for the phone")
    }

    private fun deviceVibrator(): Vibrator {
        return if (Build.VERSION.SDK_INT >= 31) {
            (getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as VibratorManager).defaultVibrator
        } else {
            @Suppress("DEPRECATION")
            getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
        }
    }

    private fun ringtoneAttributes(): AudioAttributes {
        return AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_NOTIFICATION_RINGTONE)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
    }

    private fun shouldVibrateForCall(audio: AudioManager): Boolean {
        return when (audio.ringerMode) {
            AudioManager.RINGER_MODE_SILENT -> false
            AudioManager.RINGER_MODE_VIBRATE -> true
            else -> audio.shouldVibrate(AudioManager.VIBRATE_TYPE_RINGER)
        }
    }

    private fun openCallScreen(title: String, body: String, stop: Boolean) {
        val intent = Intent(this, IncomingCallActivity::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP)
            putExtra(IncomingCallActivity.EXTRA_TITLE, title)
            putExtra(IncomingCallActivity.EXTRA_BODY, body)
            putExtra(IncomingCallActivity.EXTRA_STOP, stop)
        }
        try {
            startActivity(intent)
        } catch (error: Exception) {
            Log.w(TAG, "call screen", error)
        }
    }

    private fun showCallNotification(title: String, body: String) {
        val open = PendingIntent.getActivity(
            this,
            0,
            Intent(this, IncomingCallActivity::class.java).apply {
                flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP
                putExtra(IncomingCallActivity.EXTRA_TITLE, title)
                putExtra(IncomingCallActivity.EXTRA_BODY, body)
            },
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val posted = NotificationCompat.Builder(this, CALL_CHANNEL)
            .setContentTitle(title.ifEmpty { "Incoming call" })
            .setContentText(if (body.isEmpty()) "Your phone is ringing" else body)
            .setSmallIcon(android.R.drawable.ic_menu_call)
            .setCategory(NotificationCompat.CATEGORY_CALL)
            .setPriority(NotificationCompat.PRIORITY_MAX)
            .setOngoing(true)
            .setFullScreenIntent(open, true)
            .setContentIntent(open)
            .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
            .build()
        getSystemService(NotificationManager::class.java).notify(CALL_NOTIFICATION_ID, posted)
    }

    private fun createChannels() {
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL, "WristLink", NotificationManager.IMPORTANCE_LOW)
        )
        manager.createNotificationChannel(
            NotificationChannel(ALERT_CHANNEL, "Phone notifications", NotificationManager.IMPORTANCE_HIGH).apply {
                description = "Notifications forwarded from the Jolla"
                enableVibration(true)
            }
        )
        manager.deleteNotificationChannel("wristlink-calls")
        manager.createNotificationChannel(
            NotificationChannel(CALL_CHANNEL, "Phone calls", NotificationManager.IMPORTANCE_HIGH).apply {
                description = "Shows when the Jolla receives a call"
                setSound(null, null)
                enableVibration(false)
                lockscreenVisibility = Notification.VISIBILITY_PUBLIC
            }
        )
    }

    private fun notification(text: String): Notification {
        return NotificationCompat.Builder(this, CHANNEL)
            .setContentTitle("WristLink")
            .setContentText(text)
            .setSmallIcon(R.drawable.ic_notification)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification(text: String) {
        if (text != "Phone is ringing")
            idleStatus = text
        val shown = if (phoneBattery < 0 || text == "Phone is ringing") {
            text
        } else {
            val charging = if (phoneCharging) ", charging" else ""
            "$text · Phone $phoneBattery%$charging"
        }
        val manager = getSystemService(NotificationManager::class.java)
        manager.notify(1, notification(shown))
    }

    companion object {
        private const val TAG = "WristLinkWatch"
        private const val CHANNEL = "wristlink"
        private const val ALERT_CHANNEL = "wristlink-alerts"
        private const val CALL_CHANNEL = "wristlink-calls-2"
        private const val ALERT_NOTIFY_BASE = 2000
        private const val SUMMARY_NOTIFY_BASE = 8000
        private const val CALL_NOTIFICATION_ID = 7
        private const val FLUSH_INTERVAL_MS = 30 * 60_000L
        private const val ACTION_ALERT_DISMISSED = "org.wristlink.watch.ALERT_DISMISSED"
        const val ACTION_RESTART_LINK = "org.wristlink.watch.RESTART_LINK"
        private const val EXTRA_ALERT_ID = "alertId"
        val SERVICE_UUID: UUID = UUID.fromString("f1a5e001-7c4b-4d2a-9e11-57a1f15c0001")
        val METRICS_UUID: UUID = UUID.fromString("f1a5e001-7c4b-4d2a-9e11-57a1f15c0002")
        val ALERT_UUID: UUID = UUID.fromString("f1a5e001-7c4b-4d2a-9e11-57a1f15c0003")
        const val FLAG_STEPS = 0x01
        const val FLAG_CALORIES = 0x02
        const val FLAG_DISTANCE = 0x04
        const val FLAG_HEART = 0x08
        const val FLAG_FLOORS = 0x10
        const val FLAG_BATTERY = 0x20
    }
}
