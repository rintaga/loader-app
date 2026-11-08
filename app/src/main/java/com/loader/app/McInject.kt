package com.loader.app

import android.app.Application
import dalvik.system.BaseDexClassLoader
import dalvik.system.DexClassLoader
import org.lsposed.hiddenapibypass.HiddenApiBypass
import java.io.File
import java.util.zip.ZipFile

/** Lapisan 1: menarik kelas + lib Minecraft ke proses ini tanpa repackaging APK-nya. */
object McInject {
    const val PKG = "com.mojang.minecraftpe"

    private fun pathList(cl: ClassLoader): Any {
        val f = BaseDexClassLoader::class.java.getDeclaredField("pathList"); f.isAccessible = true
        return f.get(cl)!!
    }
    private fun elements(cl: ClassLoader): Array<Any> {
        val pl = pathList(cl); val f = pl.javaClass.getDeclaredField("dexElements"); f.isAccessible = true
        @Suppress("UNCHECKED_CAST") return f.get(pl) as Array<Any>
    }

    fun run(app: Application) {
        val ai = app.packageManager.getApplicationInfo(PKG, 0)
        val apks = listOf(ai.sourceDir) + (ai.splitSourceDirs?.toList() ?: emptyList())

        // 1) salin lib 32-bit Minecraft ke data dir kita (namespace linker mengizinkan dlopen dari sini)
        val libDir = File(app.filesDir, "mclibs").apply { mkdirs() }
        for (a in apks) ZipFile(a).use { z ->
            z.entries().asSequence()
                .filter { it.name.startsWith("lib/armeabi-v7a/") && it.name.endsWith(".so") }
                .forEach { e ->
                    val out = File(libDir, File(e.name).name)
                    if (!out.exists() || out.length() != e.size)
                        z.getInputStream(e).use { i -> out.outputStream().use { i.copyTo(it) } }
                }
        }

        // 2) gabungkan dex Minecraft ke classloader aplikasi (teknik multidex)
        HiddenApiBypass.addHiddenApiExemptions("L")
        val cl = app.classLoader
        val extra = DexClassLoader(apks.joinToString(File.pathSeparator), app.codeCacheDir.path, null, cl.parent)
        val mine = elements(cl); val theirs = elements(extra)
        @Suppress("UNCHECKED_CAST")
        val merged = java.lang.reflect.Array.newInstance(mine.javaClass.componentType!!, mine.size + theirs.size) as Array<Any>
        System.arraycopy(mine, 0, merged, 0, mine.size)
        System.arraycopy(theirs, 0, merged, mine.size, theirs.size)
        val pl = pathList(cl)
        pl.javaClass.getDeclaredField("dexElements").apply { isAccessible = true }.set(pl, merged)

        // 3) System.loadLibrary("minecraftpe") nanti menemukan lib asli di mclibs
        HiddenApiBypass.invoke(pl.javaClass, pl, "addNativePath", listOf(libDir))
    }
}
