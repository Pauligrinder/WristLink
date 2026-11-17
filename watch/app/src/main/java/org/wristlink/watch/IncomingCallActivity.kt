package org.wristlink.watch

import android.app.Activity
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.view.Gravity
import android.view.WindowManager
import android.widget.LinearLayout
import android.widget.TextView

class IncomingCallActivity : Activity() {
    private lateinit var callerView: TextView
    private lateinit var detailView: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (Build.VERSION.SDK_INT >= 27) {
            setShowWhenLocked(true)
            setTurnScreenOn(true)
        }
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setBackgroundColor(Color.BLACK)
            setPadding(32, 48, 32, 48)
        }
        root.addView(TextView(this).apply {
            text = "Incoming call"
            textSize = 14f
            setTextColor(Color.LTGRAY)
            gravity = Gravity.CENTER
        })
        callerView = TextView(this).apply {
            textSize = 22f
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
            setPadding(0, 16, 0, 8)
        }
        detailView = TextView(this).apply {
            textSize = 14f
            setTextColor(Color.LTGRAY)
            gravity = Gravity.CENTER
        }
        root.addView(callerView)
        root.addView(detailView)
        setContentView(root)
        applyIntent(intent)
    }

    override fun onNewIntent(intent: android.content.Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        applyIntent(intent)
    }

    private fun applyIntent(intent: android.content.Intent) {
        if (intent.getBooleanExtra(EXTRA_STOP, false)) {
            finish()
            return
        }
        val title = intent.getStringExtra(EXTRA_TITLE)?.ifEmpty { null } ?: "Incoming call"
        val body = intent.getStringExtra(EXTRA_BODY).orEmpty()
        callerView.text = title
        detailView.text = if (body.isEmpty() || body == title) "Your phone is ringing" else body
    }

    companion object {
        const val EXTRA_TITLE = "title"
        const val EXTRA_BODY = "body"
        const val EXTRA_STOP = "stop"
    }
}
