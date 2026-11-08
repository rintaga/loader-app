package com.loader.app

import android.content.Context
import android.content.Intent
import android.content.res.AssetManager
import android.content.res.Resources
import android.os.Bundle
import android.widget.Toast
import com.mojang.minecraftpe.MainActivity   // stub saat compile, asli saat runtime

/** Lapisan 1: Activity Minecraft dijalankan di proses kita, aset/resource dari paket Mojang. */
class HostActivity : MainActivity() {
    companion object {
        @Volatile var instance: HostActivity? = null
        private const val REQ_IMPORT = 0x4C4F

        @JvmStatic fun requestImport() {
            val a = instance ?: return
            a.runOnUiThread {
                a.startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                    addCategory(Intent.CATEGORY_OPENABLE); type = "*/*"
                    putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream"))
                }, REQ_IMPORT)
            }
        }
    }

    private val mc: Context by lazy { createPackageContext(McInject.PKG, Context.CONTEXT_IGNORE_SECURITY) }
    override fun getAssets(): AssetManager = mc.assets
    override fun getResources(): Resources = mc.resources

    override fun onCreate(s: Bundle?) { instance = this; super.onCreate(s) }
    override fun onDestroy() { instance = null; super.onDestroy() }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(req: Int, res: Int, data: Intent?) {
        if (req != REQ_IMPORT) { super.onActivityResult(req, res, data); return }
        val uri = data?.data ?: return
        Thread {
            val n = try { ShaderImporter.import(applicationContext, uri) } catch (t: Throwable) { null }
            runOnUiThread { Toast.makeText(this, if (n != null) "Diimpor: $n" else "ZIP tidak berisi folder shaders/", Toast.LENGTH_SHORT).show() }
        }.start()
    }
}
