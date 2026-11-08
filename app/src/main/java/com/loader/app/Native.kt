package com.loader.app

object Native {
    @JvmStatic external fun init(dir: String)
    @JvmStatic external fun toggleMenu()
}

object Bridge {
    @JvmStatic fun requestImport() {
        try { Class.forName("com.loader.app.HostActivity").getMethod("requestImport").invoke(null) } catch (_: Throwable) {}
    }
}
