package com.loader.app

object Native {
    @JvmStatic external fun init(dir: String)
    @JvmStatic external fun toggleMenu()
}

/** Dipanggil dari C++ (kelas ini sengaja tidak turunan Mojang agar FindClass aman di JNI_OnLoad). */
object Bridge {
    @JvmStatic fun requestImport() = HostActivity.requestImport()
}
