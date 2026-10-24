package com.sbro.emucorex.data

import java.io.File
import java.io.FileOutputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.util.UUID
import java.util.zip.CRC32
import java.util.zip.ZipFile

/** Filesystem transactions shared by downloads, imports and preset management. */
internal class ShaderPackStore(private val root: File, private val cache: File) {
    companion object {
        private val lock = Any()
        private const val OFFICIAL = ".official-pack"
        private const val REMOVED = ".removed-presets"
        private const val MAX_BYTES = 768L * 1024 * 1024
        private val assignment = Regex("^([\\w]+)\\s*=\\s*(.*)$")
        private val reference = Regex("^#reference\\s+[\"'](.+)[\"']\\s*(?://.*)?$")
        private val include = Regex("^\\s*#include\\s+\"([^\"]+)\"")
        private data class FileStamp(val file: File, val size: Long, val modified: Long) {
            fun unchanged() = file.exists() && file.length() == size && file.lastModified() == modified
        }
        private data class Catalog(val presets: List<File>, val stamps: List<FileStamp>)
        private val catalogs = object : LinkedHashMap<String, Catalog>(8, 0.75f, true) {
            override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, Catalog>?) = size > 8
        }

        internal fun usablePresetFiles(directory: File): List<File> = synchronized(lock) {
            if (!directory.isDirectory) return emptyList()
            val base = directory.canonicalFile
            catalogs[base.path]?.takeIf { it.stamps.all(FileStamp::unchanged) }?.let { return it.presets }
            val checked = mutableMapOf<String, Map<String, Pair<String, File>>?>()
            val checkedSources = mutableMapOf<String, Boolean>()
            fun safe(file: File) = file.canonicalPath.startsWith(base.path + File.separator) && file.isFile
            fun sourceValid(file: File, visiting: Set<String> = emptySet()): Boolean {
                if (!safe(file) || file.length() > 8L * 1024 * 1024) return false
                val path = file.canonicalPath
                if (path in visiting) return true // Include guards may intentionally form a cycle.
                if (visiting.size > 64) return false
                checkedSources[path]?.let { return it }
                val result = file.useLines { lines -> lines.all { line ->
                    val dependency = include.find(line)?.groupValues?.get(1)
                    dependency == null || sourceValid(File(file.parentFile, dependency), visiting + path)
                } }
                checkedSources[path] = result
                return result
            }
            fun resolve(file: File, visiting: Set<String>): Map<String, Pair<String, File>>? {
                if (!safe(file) || file.length() > 1024L * 1024) return null
                val path = file.canonicalPath
                if (checked.containsKey(path)) return checked[path]
                if (path in visiting || visiting.size > 64) return null
                val text = file.readLines()
                val references = text.mapNotNull { reference.matchEntire(it.trim())?.groupValues?.get(1) }
                val fields = mutableMapOf<String, Pair<String, File>>()
                for (dependency in references) {
                    val inherited = resolve(File(file.parentFile, dependency), visiting + path) ?: return null
                    fields.putAll(inherited)
                }
                text.mapNotNull { assignment.matchEntire(it.trim()) }.forEach {
                    fields[it.groupValues[1]] = it.groupValues[2].substringBefore(" //").trim().trim('"', '\'') to requireNotNull(file.parentFile)
                }
                checked[path] = fields
                return fields
            }
            fun valid(file: File): Boolean {
                val fields = resolve(file, emptySet()) ?: return false
                val passes = fields["shaders"]?.first?.toIntOrNull()
                val hasChain = passes != null && passes in 1..64 && (0 until passes).all { index ->
                    fields["shader$index"]?.let { (value, origin) -> sourceValid(File(origin, value)) } == true
                }
                val textures = fields["textures"]?.first?.split(';').orEmpty().filter(String::isNotBlank)
                return hasChain && textures.all { key ->
                    fields[key]?.let { (value, origin) -> safe(File(origin, value)) } == true
                }
            }
            val files = base.walkTopDown().onEnter {
                it == base || it.canonicalPath.startsWith(base.path + File.separator)
            }.toList()
            val presets = files.filter { it.isFile && it.extension.equals("slangp", true) }
                .filter { runCatching { valid(it) }.getOrDefault(false) }
            catalogs[base.path] = Catalog(presets, files.map { FileStamp(it, it.length(), it.lastModified()) })
            presets
        }
    }

    fun listPresets(): List<RetroArchShaderPreset> = synchronized(lock) {
        recoverInterruptedInstall()
        val removed = root.resolve(REMOVED).takeIf(File::isFile)?.readLines()?.toSet().orEmpty()
        usablePresetFiles(root).mapNotNull { file ->
            val relative = file.relativeTo(root.canonicalFile).invariantSeparatorsPath
            if (relative in removed) null else RetroArchShaderPreset(
                relative.substringBeforeLast('.'), file.absolutePath
            )
        }.sortedBy { it.label.lowercase() }
    }

    fun hasInstalledPack(): Boolean = synchronized(lock) {
        recoverInterruptedInstall()
        root.resolve(OFFICIAL).isFile && listPresets().isNotEmpty()
    }

    private fun recoverInterruptedInstall() {
        val backup = File(root.parentFile, "${root.name}.backup")
        if (!root.exists() && backup.isDirectory) {
            check(backup.renameTo(root)) { "Unable to restore shader backup" }
        }
    }

    fun removePresets(paths: Set<String>): Result<Int> = runCatching { synchronized(lock) {
        val available = listPresets().map { it.absolutePath }.toSet()
        require(paths.isNotEmpty() && paths.all { it in available }) { "Unknown shader preset" }
        val removedFile = root.resolve(REMOVED)
        val removed = removedFile.takeIf(File::isFile)?.readLines()?.toMutableSet() ?: mutableSetOf()
        paths.forEach { removed += File(it).relativeTo(root.canonicalFile).invariantSeparatorsPath }
        val temp = root.resolve(".removed-${UUID.randomUUID()}")
        try {
            FileOutputStream(temp).use { out ->
                out.write(removed.sorted().joinToString("\n", postfix = "\n").toByteArray())
                out.fd.sync()
            }
            Files.move(temp.toPath(), removedFile.toPath(), StandardCopyOption.ATOMIC_MOVE,
                StandardCopyOption.REPLACE_EXISTING)
        } finally { temp.delete() }
        paths.size
    } }

    fun installArchive(archive: File, official: Boolean): Int = synchronized(lock) {
        cache.mkdirs()
        val staging = File(cache, "shader-stage-${UUID.randomUUID()}")
        val backup = File(root.parentFile, "${root.name}.backup")
        require(staging.mkdirs()) { "Unable to stage shader pack" }
        try {
            extract(archive, staging)
            val presets = usablePresetFiles(staging)
            require(presets.isNotEmpty()) { "No usable .slangp presets in archive" }
            if (official) staging.resolve(OFFICIAL).writeText("1\n")
            // A killed process may have moved the old root before committing the new one.
            if (!root.exists() && backup.exists()) require(backup.renameTo(root))
            if (backup.exists()) require(backup.deleteRecursively()) { "Unable to remove old shader backup" }
            if (root.exists()) require(root.renameTo(backup)) { "Unable to stage old shader pack" }
            if (!staging.renameTo(root)) {
                if (backup.exists()) check(backup.renameTo(root)) { "Unable to restore shader backup" }
                error("Unable to install shader pack")
            }
            backup.deleteRecursively()
            presets.size
        } finally { staging.deleteRecursively() }
    }

    private fun extract(archive: File, destination: File) {
        ZipFile(archive).use { zip ->
            val entries = zip.entries().toList().filterNot {
                it.name.startsWith("__MACOSX/") || it.name.substringAfterLast('/').startsWith('.')
            }
            require(entries.size <= 20_000) { "Too many files in shader archive" }
            // Validate the original path before stripping a common root.
            entries.forEach { entry ->
                require(!entry.name.startsWith('/') && !entry.name.contains('\\') &&
                    entry.name.split('/').none { it == ".." || ':' in it }) { "Unsafe archive path" }
            }
            val common = RetroArchShaderRepository.commonArchiveRoot(entries.map { it.name })
            var total = 0L
            entries.forEach { entry ->
                val relative = entry.name.removePrefix(common)
                if (relative.isBlank()) return@forEach
                require(RetroArchShaderRepository.isSafeArchiveEntry(destination, relative)) { "Unsafe archive path" }
                val output = File(destination, relative)
                if (entry.isDirectory) { output.mkdirs(); return@forEach }
                output.parentFile?.mkdirs()
                val crc = CRC32()
                var size = 0L
                zip.getInputStream(entry).use { input -> output.outputStream().use { sink ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        val count = input.read(buffer)
                        if (count < 0) break
                        total += count; size += count
                        require(total <= MAX_BYTES) { "Expanded shader pack is too large" }
                        crc.update(buffer, 0, count); sink.write(buffer, 0, count)
                    }
                } }
                require(size == entry.size && crc.value == entry.crc) { "Shader archive is incomplete or corrupt" }
            }
        }
    }
}
