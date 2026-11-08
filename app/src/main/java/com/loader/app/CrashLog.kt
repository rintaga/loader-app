package com.loader.app

import android.content.Context
import android.util.Log
import java.io.File
import java.util.Date

object CrashLog {
    fun file(c: Context) = File(c.filesDir, "crash.txt")
    fun write(c: Context, s: String) {
        try { file(c).appendText("[" + Date() + "]\n" + s + "\n\n") } catch (_: Throwable) {}
        Log.e("LOADER", s)
    }
}
