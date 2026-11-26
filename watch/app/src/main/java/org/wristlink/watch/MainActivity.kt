package org.wristlink.watch

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat

class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val label = TextView(this)
        label.text = "WristLink shows notifications and rings when the phone rings. Steps sync when that happens, or about every half hour. Grant sensors and Bluetooth, then leave this app running so it can advertise. The phone switches to Bluetooth LE to reach this app — classic pairing alone is not enough."
        label.textSize = 16f
        val restart = Button(this)
        restart.text = "Restart WristLink"
        restart.setOnClickListener {
            startPublisher(HealthPublisherService.ACTION_RESTART_LINK)
        }
        val layout = LinearLayout(this)
        layout.orientation = LinearLayout.VERTICAL
        layout.setPadding(24, 48, 24, 24)
        layout.addView(label)
        layout.addView(restart)
        setContentView(layout)
        if (hasAllPermissions()) {
            startPublisher(null)
        } else {
            requestPermissionsIfNeeded()
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        startPublisher(null)
    }

    private fun startPublisher(action: String?) {
        val intent = Intent(this, HealthPublisherService::class.java)
        if (action != null)
            intent.action = action
        ContextCompat.startForegroundService(this, intent)
    }

    private fun permissionList(): List<String> {
        val needed = mutableListOf(
            Manifest.permission.BODY_SENSORS,
            Manifest.permission.ACTIVITY_RECOGNITION,
            Manifest.permission.BLUETOOTH_CONNECT,
            Manifest.permission.BLUETOOTH_ADVERTISE
        )
        if (Build.VERSION.SDK_INT >= 33) {
            needed.add(Manifest.permission.POST_NOTIFICATIONS)
        }
        return needed
    }

    private fun hasAllPermissions(): Boolean {
        return permissionList().all {
            ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
        }
    }

    private fun requestPermissionsIfNeeded() {
        val missing = permissionList().filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, missing.toTypedArray(), 1)
        }
    }
}
