package com.loader.app

import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.io.File

class LauncherActivity : Activity() {
    override fun onCreate(s: Bundle?) {
        super.onCreate(s)
        val cf = CrashLog.file(this)
        if (cf.exists() && cf.length() > 0) showLog(cf) else start()
    }

    private fun start() {
        if (!Settings.canDrawOverlays(this)) {
            Toast.makeText(this, "Izinkan 'tampil di atas aplikasi lain', lalu buka lagi", Toast.LENGTH_LONG).show()
            startActivity(Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName")))
        } else {
            val i = Intent(this, BubbleService::class.java)
            if (Build.VERSION.SDK_INT >= 26) startForegroundService(i) else startService(i)
        }
        finish()
    }

    private fun diag(): String {
        val sb = StringBuilder()
        sb.append("ABI: ").append(Build.SUPPORTED_ABIS.joinToString()).append("  SDK ").append(Build.VERSION.SDK_INT)
        sb.append("  proses64=").append(android.os.Process.is64Bit()).append('\n')
        try {
            McInject.detect(this); val pi = packageManager.getPackageInfo(McInject.PKG, 0)
            sb.append("Minecraft: ").append(pi.versionName).append(" (").append(pi.versionCode).append(")\n")
            sb.append("splits: ").append(pi.applicationInfo.splitSourceDirs?.size ?: 0).append('\n')
        } catch (t: Throwable) { sb.append("Minecraft TIDAK terdeteksi\n") }
        sb.append("mclibs: ").append(File(filesDir, "mclibs").list()?.joinToString() ?: "kosong").append("\n\n")
        return sb.toString()
    }

    private fun showLog(cf: File) {
        val tv = TextView(this).apply {
            text = diag() + cf.readText().takeLast(6000)
            textSize = 11f; setTextColor(Color.BLACK); setTextIsSelectable(true); setPadding(24, 24, 24, 24)
        }
        val copy = Button(this).apply {
            text = "Salin log"
            setOnClickListener {
                (getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager)
                    .setPrimaryClip(ClipData.newPlainText("log", tv.text))
                Toast.makeText(this@LauncherActivity, "Tersalin", Toast.LENGTH_SHORT).show()
            }
        }
        val go = Button(this).apply { text = "Hapus log & lanjut"; setOnClickListener { cf.delete(); start() } }
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setBackgroundColor(Color.WHITE)
            addView(copy); addView(go)
            addView(ScrollView(this@LauncherActivity).apply { addView(tv) }, LinearLayout.LayoutParams(-1, 0, 1f))
        }
        setContentView(root)
    }
}
