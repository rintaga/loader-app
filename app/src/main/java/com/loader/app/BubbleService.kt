package com.loader.app

import android.app.*
import android.content.Intent
import android.graphics.Color
import android.graphics.PixelFormat
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.IBinder
import android.view.Gravity
import android.view.MotionEvent
import android.view.WindowManager
import android.widget.TextView
import kotlin.math.abs

class BubbleService : Service() {
    private var wm: WindowManager? = null
    private var bubble: TextView? = null

    override fun onBind(i: Intent?): IBinder? = null
    override fun onStartCommand(i: Intent?, f: Int, id: Int) = START_STICKY

    override fun onCreate() {
        super.onCreate()
        if (Build.VERSION.SDK_INT >= 26)
            getSystemService(NotificationManager::class.java)
                .createNotificationChannel(NotificationChannel("loader", "Loader", NotificationManager.IMPORTANCE_MIN))
        val nb = if (Build.VERSION.SDK_INT >= 26) Notification.Builder(this, "loader") else Notification.Builder(this)
        startForeground(1, nb.setSmallIcon(android.R.drawable.ic_menu_manage).setContentTitle("Loader aktif").build())
        addBubble()
    }

    private fun addBubble() {
        val d = resources.displayMetrics.density
        val size = (64 * d).toInt()
        val tv = TextView(this).apply {
            text = "LOADER"; textSize = 9f; setTextColor(Color.WHITE); gravity = Gravity.CENTER
            setTypeface(typeface, android.graphics.Typeface.BOLD)
            background = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(0xE61E88E5.toInt()) }
        }
        val type = if (Build.VERSION.SDK_INT >= 26) WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                   else @Suppress("DEPRECATION") WindowManager.LayoutParams.TYPE_PHONE
        val lp = WindowManager.LayoutParams(size, size, type,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT).apply { gravity = Gravity.TOP or Gravity.START; x = 40; y = 300 }
        var sx = 0f; var sy = 0f; var ox = 0; var oy = 0; var moved = false
        tv.setOnTouchListener { _, e ->
            when (e.action) {
                MotionEvent.ACTION_DOWN -> { sx = e.rawX; sy = e.rawY; ox = lp.x; oy = lp.y; moved = false }
                MotionEvent.ACTION_MOVE -> {
                    if (abs(e.rawX - sx) > 12 || abs(e.rawY - sy) > 12) moved = true
                    if (moved) { lp.x = ox + (e.rawX - sx).toInt(); lp.y = oy + (e.rawY - sy).toInt(); wm?.updateViewLayout(tv, lp) }
                }
                MotionEvent.ACTION_UP -> if (!moved) onClick()
            }
            true
        }
        wm = getSystemService(WINDOW_SERVICE) as WindowManager
        wm!!.addView(tv, lp); bubble = tv
    }

    private fun onClick() {
        if (!McInject.ready) { android.widget.Toast.makeText(this, "Minecraft Bedrock tidak terdeteksi. Pasang dulu, lalu buka Loader lagi.", android.widget.Toast.LENGTH_LONG).show(); return }
        if (HostActivity.instance != null) Native.toggleMenu()          // game jalan -> buka/tutup ImGui
        else startActivity(Intent(this, HostActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) // belum -> jalankan Minecraft
    }

    override fun onDestroy() { bubble?.let { wm?.removeView(it) }; super.onDestroy() }
}
