package com.loader.app

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Toast

class LauncherActivity : Activity() {
    override fun onCreate(s: Bundle?) {
        super.onCreate(s)
        if (!Settings.canDrawOverlays(this)) {
            Toast.makeText(this, "Izinkan 'tampil di atas aplikasi lain', lalu buka lagi", Toast.LENGTH_LONG).show()
            startActivity(Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName")))
        } else {
            val i = Intent(this, BubbleService::class.java)
            if (Build.VERSION.SDK_INT >= 26) startForegroundService(i) else startService(i)
        }
        finish()
    }
}
