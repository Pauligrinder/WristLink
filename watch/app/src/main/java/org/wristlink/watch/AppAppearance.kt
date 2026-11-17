package org.wristlink.watch

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import androidx.core.content.ContextCompat

object AppAppearance {
    fun color(app: String, provided: Int = 0): Int {
        if (provided != 0) return provided
        val key = app.trim().lowercase()
        named[key]?.let { return it }
        if (key.isEmpty()) return named.getValue("phone")
        val index = key.fold(0) { acc, c -> acc * 31 + c.code } and Int.MAX_VALUE
        return palette[index % palette.size]
    }

    fun badge(context: Context, argb: Int): Bitmap {
        badges[argb]?.let { return it }
        val size = (context.resources.displayMetrics.density * 48f).toInt().coerceAtLeast(64)
        val bitmap = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        paint.color = argb
        canvas.drawCircle(size / 2f, size / 2f, size / 2f, paint)
        val fish = ContextCompat.getDrawable(context, R.drawable.ic_notification)?.mutate()
        if (fish != null) {
            fish.setTint(Color.WHITE)
            val pad = (size * 0.06f).toInt()
            fish.setBounds(pad, pad, size - pad, size - pad)
            fish.draw(canvas)
        }
        if (badges.size > 24)
            badges.clear()
        badges[argb] = bitmap
        return bitmap
    }

    fun groupKey(app: String): String {
        val key = app.trim().lowercase().ifEmpty { "phone" }
        return "wristlink:$key"
    }

    private val badges = HashMap<Int, Bitmap>()

    private val palette = intArrayOf(
        0xFF1A73E8.toInt(),
        0xFF34A853.toInt(),
        0xFFEA4335.toInt(),
        0xFFFBBC04.toInt(),
        0xFF7B1FA2.toInt(),
        0xFF00897B.toInt(),
        0xFFEF6C00.toInt(),
        0xFF5C6BC0.toInt(),
        0xFFC2185B.toInt(),
        0xFF00838F.toInt()
    )

    private val named = mapOf(
        "email" to 0xFF1A73E8.toInt(),
        "messages" to 0xFF34A853.toInt(),
        "phone" to 0xFF1E88E5.toInt(),
        "whatsapp" to 0xFF25D366.toInt(),
        "telegram" to 0xFF2AABEE.toInt(),
        "signal" to 0xFF3A76F0.toInt(),
        "messenger" to 0xFF0084FF.toInt(),
        "helmsman" to 0xFF64B5F6.toInt(),
        "instagram" to 0xFFE1306C.toInt(),
        "linkedin" to 0xFF0A66C2.toInt(),
        "slack" to 0xFF4A154B.toInt(),
        "discord" to 0xFF5865F2.toInt(),
        "firefox" to 0xFFFF7139.toInt(),
        "chrome" to 0xFF4285F4.toInt()
    )
}
