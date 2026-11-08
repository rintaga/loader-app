package com.loader.app

import android.app.Application
import android.content.Context
import android.util.Log

class LoaderApp : Application() {
    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(base)
        val prev = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            CrashLog.write(this, "CRASH thread " + t.name + ":\n" + Log.getStackTraceString(e))
            prev?.uncaughtException(t, e)
        }
        try { McInject.run(this) } catch (t: Throwable) {
            val pk = packageManager.getInstalledApplications(0).map { it.packageName }
                .filter { it.contains("mojang") || it.contains("minecraft") }
            CrashLog.write(this, "INJECT GAGAL. Paket mojang/minecraft terpasang: $pk\n" + Log.getStackTraceString(t))
        }
        try {
            System.loadLibrary("customloader")
            Native.init(ShaderImporter.root(this).apply { mkdirs() }.path)
        } catch (t: Throwable) { CrashLog.write(this, "LOAD customloader GAGAL:\n" + Log.getStackTraceString(t)) }
    }
}
