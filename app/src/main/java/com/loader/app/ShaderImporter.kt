package com.loader.app

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.util.zip.ZipInputStream

object ShaderImporter {
    fun root(c: Context) = File(c.filesDir, "shader_packs")
    private val ok = setOf("fsh", "vsh", "glsl", "inc", "h", "properties", "txt", "gsh")

    fun import(c: Context, uri: Uri): String? {
        var name = "pack"
        c.contentResolver.query(uri, null, null, null, null)?.use { q ->
            if (q.moveToFirst()) { val i = q.getColumnIndex(OpenableColumns.DISPLAY_NAME); if (i >= 0) name = q.getString(i) }
        }
        name = name.removeSuffix(".zip").replace(Regex("[^A-Za-z0-9._ -]"), "_")
        val out = File(root(c), name).apply { deleteRecursively(); mkdirs() }
        var n = 0
        c.contentResolver.openInputStream(uri)?.use { ins ->
            ZipInputStream(ins).use { z ->
                var e = z.nextEntry
                while (e != null) {
                    val p = e.name.replace('\\', '/'); val i = p.indexOf("shaders/")
                    if (!e.isDirectory && i >= 0 && p.substringAfterLast('.', "").lowercase() in ok) {
                        val f = File(out, p.substring(i + 8))
                        if (f.canonicalPath.startsWith(out.canonicalPath + "/")) {     // anti zip-slip
                            f.parentFile?.mkdirs(); f.outputStream().use { z.copyTo(it) }; n++
                        }
                    }
                    e = z.nextEntry
                }
            }
        }
        if (n == 0) { out.deleteRecursively(); return null }
        return name
    }
}
