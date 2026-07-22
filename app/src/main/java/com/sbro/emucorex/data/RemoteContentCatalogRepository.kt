package com.sbro.emucorex.data

import android.content.Context
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.Locale
import java.util.UUID

data class RemoteTexturePack(
    val id: String,
    val name: String,
    val gameTitle: String,
    val serials: List<String>,
    val version: String,
    val authors: List<String>,
    val credits: String,
    val description: String,
    val downloadUrl: String,
    val sourceUrl: String,
    val license: String,
    val sizeBytes: Long,
    val sha256: String,
    val fileCount: Int,
    val previewUrls: List<String>
)

data class RemoteCheatPack(
    val id: String,
    val title: String,
    val serials: List<String>,
    val crc: String,
    val authors: List<String>,
    val description: String,
    val downloadUrl: String,
    val sourceUrl: String,
    val sourceName: String,
    val license: String,
    val blockCount: Int
)

data class RemoteCatalogResult<T>(
    val entries: List<T>,
    val fromCache: Boolean,
    val error: Throwable? = null
)

class RemoteContentCatalogRepository(context: Context) {
    private val appContext = context.applicationContext
    private val json = Json { ignoreUnknownKeys = true }
    private val cacheDir = File(appContext.filesDir, "remote-content").apply { mkdirs() }
    private val downloadDir = File(appContext.cacheDir, "remote-content-downloads").apply { mkdirs() }

    fun loadTextureCatalog(): RemoteCatalogResult<RemoteTexturePack> = loadCatalog(
        urls = TEXTURE_CATALOG_URLS,
        cacheFile = File(cacheDir, "textures-v1.json"),
        parser = ::parseTextureCatalog
    )

    fun loadCheatCatalog(): RemoteCatalogResult<RemoteCheatPack> = loadCatalog(
        urls = CHEAT_CATALOG_URLS,
        cacheFile = File(cacheDir, "cheats-v1.json"),
        parser = ::parseCheatCatalog
    )

    fun downloadTexturePack(
        pack: RemoteTexturePack,
        onProgress: (Float) -> Unit = {}
    ): File {
        require(pack.downloadUrl.isHttpsUrl()) { "Texture download must use HTTPS" }
        require(pack.sizeBytes in 1..MAX_TEXTURE_ARCHIVE_BYTES) { "Texture archive size is invalid" }
        val target = File(downloadDir, "${sanitizeFileName(pack.id)}-${UUID.randomUUID()}.zip")
        return downloadFile(
            url = pack.downloadUrl,
            target = target,
            expectedSize = pack.sizeBytes,
            maxBytes = MAX_TEXTURE_ARCHIVE_BYTES,
            expectedSha256 = pack.sha256,
            onProgress = onProgress
        )
    }

    fun downloadCheatText(pack: RemoteCheatPack): String {
        require(pack.downloadUrl.isHttpsUrl()) { "Cheat download must use HTTPS" }
        val bytes = fetchBytes(pack.downloadUrl, MAX_CHEAT_BYTES)
        val text = bytes.toString(Charsets.UTF_8).removePrefix("\uFEFF")
        require(
            text.lineSequence().any { line ->
                val trimmed = line.trimStart()
                trimmed.startsWith("patch=", ignoreCase = true) ||
                    trimmed.startsWith("dpatch=", ignoreCase = true)
            }
        ) { "Downloaded file does not contain supported PNACH patches" }
        return text
    }

    fun discardDownload(file: File?) {
        if (file != null && file.parentFile?.canonicalFile == downloadDir.canonicalFile) {
            file.delete()
        }
    }

    private fun <T> loadCatalog(
        urls: List<String>,
        cacheFile: File,
        parser: (String) -> List<T>
    ): RemoteCatalogResult<T> {
        if (cacheFile.isFile) {
            val cacheAge = (System.currentTimeMillis() - cacheFile.lastModified()).coerceAtLeast(0L)
            if (cacheAge <= CATALOG_CACHE_TTL_MS) {
                runCatching { parser(cacheFile.readText()) }
                    .getOrNull()
                    ?.takeIf { it.isNotEmpty() }
                    ?.let { entries ->
                        // A fresh cache is the normal fast path, not an offline fallback warning.
                        return RemoteCatalogResult(entries, fromCache = false)
                    }
            }
        }
        var lastError: Throwable? = null
        urls.forEach { url ->
            val result = runCatching {
                val raw = fetchBytes(url, MAX_CATALOG_BYTES).toString(Charsets.UTF_8)
                val parsed = parser(raw)
                require(parsed.isNotEmpty()) { "Remote catalog is empty" }
                writeAtomically(cacheFile, raw)
                parsed
            }
            result.getOrNull()?.let { return RemoteCatalogResult(it, fromCache = false) }
            lastError = result.exceptionOrNull()
        }
        if (cacheFile.exists()) {
            runCatching { parser(cacheFile.readText()) }
                .getOrNull()
                ?.takeIf { it.isNotEmpty() }
                ?.let { return RemoteCatalogResult(it, fromCache = true, error = lastError) }
        }
        return RemoteCatalogResult(emptyList(), fromCache = false, error = lastError)
    }

    private fun parseTextureCatalog(raw: String): List<RemoteTexturePack> {
        val root = json.parseToJsonElement(raw).jsonObject
        require(root.int("schemaVersion") == 1) { "Unsupported texture catalog version" }
        return root.array("entries").mapNotNull { element ->
            val item = element.jsonObject
            runCatching {
                RemoteTexturePack(
                    id = item.requiredString("id"),
                    name = item.requiredString("name"),
                    gameTitle = item.requiredString("gameTitle"),
                    serials = item.stringList("serials").mapNotNull(::normalizeSerial).distinct(),
                    version = item.requiredString("version"),
                    authors = item.stringList("authors").filter(String::isNotBlank),
                    credits = item.string("credits"),
                    description = item.string("description"),
                    downloadUrl = item.requiredString("downloadUrl").requireHttps(),
                    sourceUrl = item.requiredString("sourceUrl").requireHttps(),
                    license = item.string("license"),
                    sizeBytes = item.long("sizeBytes"),
                    sha256 = item.requiredString("sha256").uppercase(Locale.US),
                    fileCount = item.int("fileCount"),
                    previewUrls = item.stringList("previewUrls").filter(String::isHttpsUrl)
                ).also { pack ->
                    require(pack.serials.isNotEmpty())
                    require(pack.authors.isNotEmpty())
                    require(pack.sizeBytes in 1..MAX_TEXTURE_ARCHIVE_BYTES)
                    require(pack.sha256.matches(Regex("[0-9A-F]{64}")))
                    require(pack.fileCount > 0)
                }
            }.getOrNull()
        }.distinctBy(RemoteTexturePack::id)
    }

    private fun parseCheatCatalog(raw: String): List<RemoteCheatPack> {
        val root = json.parseToJsonElement(raw).jsonObject
        require(root.int("schemaVersion") == 1) { "Unsupported cheat catalog version" }
        return root.array("entries").mapNotNull { element ->
            val item = element.jsonObject
            runCatching {
                RemoteCheatPack(
                    id = item.requiredString("id"),
                    title = item.requiredString("title"),
                    serials = item.stringList("serials").mapNotNull(::normalizeSerial).distinct(),
                    crc = item.requiredString("crc").uppercase(Locale.US),
                    authors = item.stringList("authors").filter(String::isNotBlank),
                    description = item.string("description"),
                    downloadUrl = item.requiredString("downloadUrl").requireHttps(),
                    sourceUrl = item.requiredString("sourceUrl").requireHttps(),
                    sourceName = item.requiredString("sourceName"),
                    license = item.string("license"),
                    blockCount = item.int("blockCount")
                ).also { pack ->
                    require(pack.crc.matches(Regex("[0-9A-F]{8}")))
                    require(pack.authors.isNotEmpty())
                    require(pack.blockCount > 0)
                }
            }.getOrNull()
        }.distinctBy(RemoteCheatPack::id)
    }

    private fun downloadFile(
        url: String,
        target: File,
        expectedSize: Long,
        maxBytes: Long,
        expectedSha256: String,
        onProgress: (Float) -> Unit
    ): File {
        val part = File(target.parentFile, "${target.name}.part")
        part.delete()
        try {
            val connection = openConnection(url)
            try {
                val responseLength = connection.contentLengthLong
                if (responseLength > maxBytes) throw IOException("Download is too large")
                val total = if (responseLength > 0) responseLength else expectedSize
                val digest = MessageDigest.getInstance("SHA-256")
                var copied = 0L
                connection.inputStream.buffered().use { input ->
                    part.outputStream().buffered().use { output ->
                        val buffer = ByteArray(DEFAULT_BUFFER_SIZE * 4)
                        while (true) {
                            val read = input.read(buffer)
                            if (read < 0) break
                            copied += read
                            if (copied > maxBytes) throw IOException("Download is too large")
                            digest.update(buffer, 0, read)
                            output.write(buffer, 0, read)
                            if (total > 0) onProgress((copied.toFloat() / total).coerceIn(0f, 1f))
                        }
                    }
                }
                if (expectedSize > 0 && copied != expectedSize) {
                    throw IOException("Downloaded size does not match the catalog")
                }
                val actualHash = digest.digest().joinToString("") { byte -> "%02X".format(byte) }
                if (!actualHash.equals(expectedSha256, ignoreCase = true)) {
                    throw IOException("Downloaded SHA-256 does not match the catalog")
                }
                if (!part.renameTo(target)) {
                    part.copyTo(target, overwrite = true)
                    part.delete()
                }
                onProgress(1f)
                return target
            } finally {
                connection.disconnect()
            }
        } catch (error: Throwable) {
            part.delete()
            target.delete()
            throw error
        }
    }

    private fun fetchBytes(url: String, maxBytes: Long): ByteArray {
        require(url.isHttpsUrl()) { "Only HTTPS downloads are allowed" }
        val connection = openConnection(url)
        return try {
            val contentLength = connection.contentLengthLong
            if (contentLength > maxBytes) throw IOException("Response is too large")
            connection.inputStream.buffered().use { input ->
                val output = java.io.ByteArrayOutputStream()
                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                var total = 0L
                while (true) {
                    val read = input.read(buffer)
                    if (read < 0) break
                    total += read
                    if (total > maxBytes) throw IOException("Response is too large")
                    output.write(buffer, 0, read)
                }
                output.toByteArray()
            }
        } finally {
            connection.disconnect()
        }
    }

    private fun openConnection(url: String): HttpURLConnection {
        val connection = URL(url).openConnection() as HttpURLConnection
        connection.connectTimeout = 15_000
        connection.readTimeout = 45_000
        connection.instanceFollowRedirects = true
        connection.setRequestProperty("Accept", "application/json, application/octet-stream, text/plain, */*")
        connection.setRequestProperty("User-Agent", "EmuCoreX-Android")
        connection.connect()
        if (connection.responseCode !in 200..299) {
            val code = connection.responseCode
            connection.disconnect()
            throw IOException("HTTP $code")
        }
        return connection
    }

    private fun writeAtomically(target: File, contents: String) {
        target.parentFile?.mkdirs()
        val temp = File(target.parentFile, "${target.name}.tmp")
        temp.writeText(contents)
        if (!temp.renameTo(target)) {
            temp.copyTo(target, overwrite = true)
            temp.delete()
        }
    }

    private fun sanitizeFileName(value: String): String =
        value.replace(Regex("[^A-Za-z0-9._-]"), "_").take(120).ifBlank { "content" }

    private companion object {
        const val MAX_CATALOG_BYTES = 8L * 1024L * 1024L
        const val MAX_CHEAT_BYTES = 2L * 1024L * 1024L
        const val MAX_TEXTURE_ARCHIVE_BYTES = 4L * 1024L * 1024L * 1024L
        const val CATALOG_CACHE_TTL_MS = 6L * 60L * 60L * 1000L

        val TEXTURE_CATALOG_URLS = listOf(
            "https://raw.githubusercontent.com/sashkinbro/EmuCoreX-Textures/main/textures.json",
            "https://github.com/sashkinbro/EmuCoreX-Textures/raw/main/textures.json",
            "https://cdn.jsdelivr.net/gh/sashkinbro/EmuCoreX-Textures@main/textures.json"
        )
        val CHEAT_CATALOG_URLS = listOf(
            "https://raw.githubusercontent.com/sashkinbro/EmuCoreX-Cheat/main/cheats.json",
            "https://github.com/sashkinbro/EmuCoreX-Cheat/raw/main/cheats.json",
            "https://cdn.jsdelivr.net/gh/sashkinbro/EmuCoreX-Cheat@main/cheats.json"
        )
    }
}

private fun JsonObject.array(name: String): JsonArray = get(name)?.jsonArray ?: JsonArray(emptyList())
private fun JsonObject.string(name: String): String = get(name)?.jsonPrimitive?.content.orEmpty()
private fun JsonObject.requiredString(name: String): String = string(name).trim().also { require(it.isNotEmpty()) }
private fun JsonObject.int(name: String): Int = get(name)?.jsonPrimitive?.content?.toIntOrNull() ?: 0
private fun JsonObject.long(name: String): Long = get(name)?.jsonPrimitive?.content?.toLongOrNull() ?: 0L
private fun JsonObject.stringList(name: String): List<String> =
    get(name)?.jsonArray?.mapNotNull { it.jsonPrimitive.content.trim().takeIf(String::isNotEmpty) }.orEmpty()

private fun String.requireHttps(): String = trim().also { require(it.isHttpsUrl()) }
private fun String.isHttpsUrl(): Boolean = startsWith("https://", ignoreCase = true)

private fun normalizeSerial(value: String): String? {
    val compact = value.trim().uppercase(Locale.US).replace(Regex("[-_ ]"), "")
    if (!compact.matches(Regex("[A-Z]{4}[0-9]{5}"))) return null
    return "${compact.take(4)}-${compact.drop(4)}"
}
