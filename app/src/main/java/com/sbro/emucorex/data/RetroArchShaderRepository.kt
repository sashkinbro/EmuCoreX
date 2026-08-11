package com.sbro.emucorex.data

import android.content.Context
import android.net.Uri
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.UUID
import java.util.zip.ZipFile

data class RetroArchShaderPreset(val label: String, val absolutePath: String)

/** Installs complete RetroArch shader packs while preserving their relative-path structure. */
class RetroArchShaderRepository(private val context: Context) {
    companion object {
        const val OFFICIAL_PACK_URL =
            "https://buildbot.libretro.com/assets/frontend/shaders_slang.zip"
        private const val MAX_ARCHIVE_BYTES = 256L * 1024L * 1024L
        private const val MAX_EXTRACTED_BYTES = 768L * 1024L * 1024L
        private const val MAX_ENTRIES = 20_000

        internal fun commonArchiveRoot(paths: List<String>): String {
            if (paths.isEmpty()) return ""
            val first = paths.first().substringBefore('/', missingDelimiterValue = "")
            if (first.isBlank()) return ""
            return if (paths.all { it == first || it.startsWith("$first/") }) "$first/" else ""
        }

        internal fun isSafeArchiveEntry(destination: File, relative: String): Boolean {
            val canonicalRoot = destination.canonicalPath + File.separator
            return File(destination, relative).canonicalPath.startsWith(canonicalRoot)
        }

        internal fun containsPresetFiles(directory: File): Boolean =
            directory.exists() && directory.walkTopDown().any {
                it.isFile && it.extension.equals("slangp", ignoreCase = true)
            }
    }

    private val root: File
        get() = File(context.filesDir, "retroarch-shaders").apply { mkdirs() }

    fun listPresets(): List<RetroArchShaderPreset> {
        val base = root.canonicalFile
        return base.walkTopDown()
            .filter { it.isFile && it.extension.equals("slangp", ignoreCase = true) }
            .map { file ->
                RetroArchShaderPreset(
                    label = file.relativeTo(base).invariantSeparatorsPath.removeSuffix(".slangp"),
                    absolutePath = file.absolutePath
                )
            }
            .sortedBy { it.label.lowercase() }
            .toList()
    }

    fun hasInstalledPack(): Boolean = containsPresetFiles(root)

    fun downloadOfficialPack(): Result<Int> = runCatching {
        val archive = File(context.cacheDir, "retroarch-shaders-${UUID.randomUUID()}.zip")
        try {
            val connection = (URL(OFFICIAL_PACK_URL).openConnection() as HttpURLConnection).apply {
                connectTimeout = 20_000
                readTimeout = 30_000
                instanceFollowRedirects = true
                setRequestProperty("User-Agent", "EmuCoreX-Android")
            }
            try {
                require(connection.responseCode in 200..299) { "HTTP ${connection.responseCode}" }
                connection.inputStream.use { input ->
                    FileOutputStream(archive).use { output ->
                        val buffer = ByteArray(64 * 1024)
                        var total = 0L
                        while (true) {
                            val read = input.read(buffer)
                            if (read < 0) break
                            total += read
                            require(total <= MAX_ARCHIVE_BYTES) { "Shader archive is too large" }
                            output.write(buffer, 0, read)
                        }
                        output.fd.sync()
                    }
                }
            } finally {
                connection.disconnect()
            }
            installArchive(archive)
        } finally {
            archive.delete()
        }
    }

    fun importArchive(uri: Uri): Result<Int> = runCatching {
        val archive = File(context.cacheDir, "shader-import-${UUID.randomUUID()}.zip")
        try {
            context.contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(archive).use { output ->
                    val buffer = ByteArray(64 * 1024)
                    var total = 0L
                    while (true) {
                        val read = input.read(buffer)
                        if (read < 0) break
                        total += read
                        require(total <= MAX_ARCHIVE_BYTES) { "Shader archive is too large" }
                        output.write(buffer, 0, read)
                    }
                    output.fd.sync()
                }
            } ?: error("Unable to open shader archive")
            installArchive(archive)
        } finally {
            archive.delete()
        }
    }

    private fun installArchive(archive: File): Int {
        val staging = File(context.cacheDir, "shader-stage-${UUID.randomUUID()}")
        val target = root
        val backup = File(context.filesDir, "retroarch-shaders.backup")
        staging.mkdirs()
        try {
            extractSafely(archive, staging)
            val presets = staging.walkTopDown()
                .count { it.isFile && it.extension.equals("slangp", ignoreCase = true) }
            require(presets > 0) { "No .slangp presets in archive" }
            backup.deleteRecursively()
            if (target.exists()) require(target.renameTo(backup)) { "Unable to stage old shader pack" }
            if (!staging.renameTo(target)) {
                backup.renameTo(target)
                error("Unable to install shader pack")
            }
            backup.deleteRecursively()
            return presets
        } finally {
            staging.deleteRecursively()
        }
    }

    private fun extractSafely(archive: File, destination: File) {
        val rootPath = destination.canonicalPath + File.separator
        ZipFile(archive).use { zip ->
            val entries = zip.entries().toList()
                .filterNot { it.name.startsWith("__MACOSX/") || it.name.substringAfterLast('/').startsWith(".") }
            require(entries.size <= MAX_ENTRIES) { "Too many files in shader archive" }
            val commonRoot = commonArchiveRoot(entries.map { it.name })
            var extracted = 0L
            entries.forEach { entry ->
                val relative = entry.name.removePrefix(commonRoot)
                if (relative.isBlank()) return@forEach
                val output = File(destination, relative)
                require(output.canonicalPath.startsWith(rootPath) && isSafeArchiveEntry(destination, relative)) {
                    "Unsafe archive path"
                }
                if (entry.isDirectory) {
                    output.mkdirs()
                } else {
                    output.parentFile?.mkdirs()
                    zip.getInputStream(entry).use { input ->
                        output.outputStream().use { sink ->
                            val buffer = ByteArray(64 * 1024)
                            while (true) {
                                val read = input.read(buffer)
                                if (read < 0) break
                                extracted += read
                                require(extracted <= MAX_EXTRACTED_BYTES) {
                                    "Expanded shader pack is too large"
                                }
                                sink.write(buffer, 0, read)
                            }
                        }
                    }
                }
            }
        }
    }

}
