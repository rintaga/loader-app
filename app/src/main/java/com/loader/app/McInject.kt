package com.loader.app

import android.app.Application
import android.content.Context
import dalvik.system.BaseDexClassLoader
import dalvik.system.DexClassLoader
import org.lsposed.hiddenapibypass.HiddenApiBypass
import java.io.File
import java.util.zip.ZipFile

object McInject {
    @Volatile var PKG = "com.mojang.minecraftpe"
    @Volatile var ready = false

    fun detect(c: Context): Boolean {
        val all = c.packageManager.getInstalledApplications(0).map { it.packageName }
        val pick = all.firstOrNull { it == "com.mojang.minecraftpe" }
            ?: all.firstOrNull { it.startsWith("com.mojang.minecraftpe") }
            ?: all.firstOrNull { it.contains("minecraftpe") }
        if (pick != null) PKG = pick
        return pick != null
    }

    private fun pathList(cl: ClassLoader): Any {
        val f = BaseDexClassLoader::class.java.getDeclaredField("pathList"); f.isAccessible = true
        return f.get(cl)!!
    }
    private fun elements(cl: ClassLoader): Array<Any> {
        val pl = pathList(cl); val f = pl.javaClass.getDeclaredField("dexElements"); f.isAccessible = true
        @Suppress("UNCHECKED_CAST") return f.get(pl) as Array<Any>
    }

    fun run(app: Application) {
        if (!detect(app)) throw IllegalStateException("Minecraft Bedrock tidak terpasang")
        val ai = app.packageManager.getApplicationInfo(PKG, 0)
        val apks = listOf(ai.sourceDir) + (ai.splitSourceDirs?.toList() ?: emptyList())
        val libDir = File(app.filesDir, "mclibs").apply { mkdirs() }
        var n = 0
        for (a in apks) ZipFile(a).use { z ->
            z.entries().asSequence()
                .filter { it.name.startsWith("lib/armeabi-v7a/") && it.name.endsWith(".so") }
                .forEach { e ->
                    val out = File(libDir, File(e.name).name)
                    if (!out.exists() || out.length() != e.size)
                        z.getInputStream(e).use { i -> out.outputStream().use { i.copyTo(it) } }
                    n++
                }
        }
        if (n == 0) throw IllegalStateException("Minecraft ini tidak punya lib armeabi-v7a (hanya 64-bit?)")
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
        HiddenApiBypass.invoke(pl.javaClass, pl, "addNativePath", listOf(libDir))
        ready = true
    }
}
