package com.loader.app

import android.app.Application
import android.content.Context
import android.util.Log

class LoaderApp : Application() {
    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(base)
        try { McInject.run(this) } catch (t: Throwable) { Log.e("LOADER", "inject gagal (Minecraft terpasang?)", t) }
        // dimuat SEBELUM libminecraftpe.so; JNI_OnLoad memasang hook
        System.loadLibrary("customloader")
        Native.init(ShaderImporter.root(this).apply { mkdirs() }.path)
    }
}
