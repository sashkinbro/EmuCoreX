package com.sbro.emucorex.data.hub

import android.content.Context
import com.sbro.emucorex.BuildConfig
import com.sbro.emucorex.R
import kotlinx.serialization.json.Json
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.KeyFactory
import java.security.MessageDigest
import java.security.Signature
import java.security.spec.X509EncodedKeySpec
import java.util.Base64

data class HubRemoteSnapshot(
    val channel: HubChannel,
    val manifest: HubManifest,
    val locale: String,
    val requestedLocale: String,
    val baseUrl: String,
    val items: List<HubIndexItem>,
    val assets: List<HubAsset>
)

class HubRemoteDataSource(context: Context) {
    private val appContext = context.applicationContext
    private val json = Json {
        ignoreUnknownKeys = true
        explicitNulls = false
    }

    fun load(requestedLocale: String): HubRemoteSnapshot {
        val channelBytes = fetchBytes(CHANNEL_URL, MAX_CHANNEL_BYTES)
        val signatureBytes = fetchBytes(CHANNEL_SIGNATURE_URL, MAX_SIGNATURE_BYTES)
            .toString(Charsets.UTF_8)
            .trim()
            .let { Base64.getDecoder().decode(it) }
        require(verifyChannel(channelBytes, signatureBytes)) { "Hub channel signature is invalid" }

        val channel = json.decodeFromString<HubChannel>(channelBytes.toString(Charsets.UTF_8))
        require(channel.formatVersion == SUPPORTED_CHANNEL_FORMAT) { "Unsupported Hub channel format" }
        require(channel.channel == "stable") { "Unexpected Hub channel" }
        require(channel.commit.matches(Regex("[0-9a-f]{40}"))) { "Invalid Hub commit" }
        require(channel.minimumClientVersionCode <= BuildConfig.VERSION_CODE) { "A newer app version is required" }
        val manifestUrl = channel.manifest.url ?: error("Hub manifest URL is missing")
        requirePinnedManifestUrl(manifestUrl, channel.commit)
        val manifestBytes = fetchVerified(manifestUrl, channel.manifest, MAX_MANIFEST_BYTES)
        val manifest = json.decodeFromString<HubManifest>(manifestBytes.toString(Charsets.UTF_8))
        require(manifest.formatVersion == SUPPORTED_CATALOG_FORMAT) { "Unsupported Hub catalog format" }
        require(manifest.releaseId == channel.releaseId) { "Hub release identifiers do not match" }
        require(manifest.catalogRevision == channel.catalogRevision) { "Hub catalog revisions do not match" }
        require(manifest.minimumClientVersionCode <= BuildConfig.VERSION_CODE) { "A newer app version is required" }

        val resolvedLocale = HubLocaleResolver.resolve(requestedLocale, manifest.supportedLocales)
        val manifestSuffix = "catalog/v1/manifest.json"
        require(manifestUrl.endsWith(manifestSuffix)) { "Unexpected Hub manifest path" }
        val baseUrl = manifestUrl.removeSuffix(manifestSuffix)
        val items = HubKind.entries.flatMap { kind ->
            val section = manifest.indexes[kind.wireValue]
                ?: if (kind == HubKind.MANUALS) return@flatMap emptyList() else error("Hub ${kind.wireValue} index is missing")
            val references = section[resolvedLocale] ?: section[manifest.defaultLocale]
                ?: error("Hub locale index is missing")
            references.flatMap { reference ->
                val url = resolvePinnedUrl(baseUrl, reference.path)
                val bytes = fetchVerified(url, reference, MAX_INDEX_BYTES)
                val page = json.decodeFromString<HubIndexPage>(bytes.toString(Charsets.UTF_8))
                require(page.formatVersion == SUPPORTED_CATALOG_FORMAT)
                require(page.releaseId == manifest.releaseId)
                require(page.catalogRevision == manifest.catalogRevision)
                require(page.kind == kind.wireValue)
                require(page.locale == resolvedLocale || page.locale == manifest.defaultLocale)
                page.items
            }
        }.distinctBy(HubIndexItem::id)

        val assetsUrl = resolvePinnedUrl(baseUrl, manifest.assets.path)
        val assetBytes = fetchVerified(assetsUrl, manifest.assets, MAX_ASSET_CATALOG_BYTES)
        val assets = json.decodeFromString<HubAssetCatalog>(assetBytes.toString(Charsets.UTF_8))
        require(assets.releaseId == manifest.releaseId)

        return HubRemoteSnapshot(
            channel = channel,
            manifest = manifest,
            locale = resolvedLocale,
            requestedLocale = HubLocaleResolver.resolve(requestedLocale),
            baseUrl = baseUrl,
            items = items,
            assets = assets.assets
        )
    }

    fun fetchDocument(reference: HubFileReference): ByteArray {
        val url = reference.url ?: error("Hub document URL is missing")
        return fetchVerified(url, reference, MAX_DOCUMENT_BYTES)
    }

    fun fetchAsset(url: String, sha256: String, bytes: Long): ByteArray = fetchVerified(
        url = url,
        reference = HubFileReference(url = url, sha256 = sha256, bytes = bytes, contentType = "application/octet-stream"),
        hardLimit = MAX_IMAGE_BYTES
    )

    private fun fetchVerified(url: String, reference: HubFileReference, hardLimit: Long): ByteArray {
        require(reference.bytes in 1..hardLimit) { "Hub file size is invalid" }
        require(reference.sha256.matches(Regex("[0-9a-f]{64}"))) { "Hub checksum is invalid" }
        val bytes = fetchBytes(url, hardLimit)
        require(bytes.size.toLong() == reference.bytes) { "Hub file size does not match" }
        val digest = MessageDigest.getInstance("SHA-256").digest(bytes).toHex()
        require(digest == reference.sha256) { "Hub file checksum does not match" }
        return bytes
    }

    private fun fetchBytes(rawUrl: String, maxBytes: Long): ByteArray {
        var next = URL(rawUrl)
        repeat(MAX_REDIRECTS + 1) {
            require(next.protocol == "https") { "Hub downloads require HTTPS" }
            val connection = (next.openConnection() as HttpURLConnection).apply {
                connectTimeout = CONNECT_TIMEOUT_MS
                readTimeout = READ_TIMEOUT_MS
                instanceFollowRedirects = false
                setRequestProperty("Accept", "application/json, text/plain, image/*")
                setRequestProperty("User-Agent", "EmuCoreX-Hub/${BuildConfig.VERSION_NAME}")
            }
            try {
                val response = connection.responseCode
                if (response in 300..399) {
                    val location = connection.getHeaderField("Location") ?: throw IOException("Hub redirect is missing a location")
                    next = URL(next, location)
                    return@repeat
                }
                if (response !in 200..299) throw IOException("Hub request failed with HTTP $response")
                val declaredLength = connection.contentLengthLong
                if (declaredLength > maxBytes) throw IOException("Hub response is too large")
                connection.inputStream.buffered().use { input ->
                    val output = ByteArrayOutputStream()
                    val buffer = ByteArray(DEFAULT_BUFFER_SIZE * 2)
                    var total = 0L
                    while (true) {
                        val read = input.read(buffer)
                        if (read < 0) break
                        total += read
                        if (total > maxBytes) throw IOException("Hub response is too large")
                        output.write(buffer, 0, read)
                    }
                    return output.toByteArray()
                }
            } finally {
                connection.disconnect()
            }
        }
        throw IOException("Too many Hub redirects")
    }

    private fun verifyChannel(bytes: ByteArray, signatureBytes: ByteArray): Boolean = runCatching {
        val pem = appContext.resources.openRawResource(R.raw.hub_channel_public_key)
            .bufferedReader()
            .use { it.readText() }
        val der = pem.lineSequence()
            .filterNot { it.startsWith("-----") }
            .joinToString("")
            .let { Base64.getDecoder().decode(it) }
        val publicKey = KeyFactory.getInstance("EC").generatePublic(X509EncodedKeySpec(der))
        Signature.getInstance("SHA256withECDSA").run {
            initVerify(publicKey)
            update(bytes)
            verify(signatureBytes)
        }
    }.getOrDefault(false)

    private fun requirePinnedManifestUrl(url: String, commit: String) {
        val parsed = URL(url)
        require(parsed.protocol == "https")
        require(parsed.host == "raw.githubusercontent.com")
        require(parsed.path == "/sashkinbro/EmuCoreX-News-Media-Hub/$commit/catalog/v1/manifest.json")
    }

    private fun resolvePinnedUrl(baseUrl: String, relativePath: String?): String {
        require(!relativePath.isNullOrBlank()) { "Hub path is missing" }
        require(relativePath.matches(Regex("[A-Za-z0-9._/-]+"))) { "Hub path contains invalid characters" }
        require(!relativePath.startsWith('/') && !relativePath.contains("..") && !relativePath.contains('\\')) { "Unsafe Hub path" }
        val resolved = URL(URL(baseUrl), relativePath)
        require(resolved.protocol == "https" && resolved.host == "raw.githubusercontent.com") { "Unexpected Hub host" }
        return resolved.toString()
    }

    companion object {
        const val CHANNEL_URL = "https://raw.githubusercontent.com/sashkinbro/EmuCoreX-News-Media-Hub/refs/heads/main/channels/stable-v1.json"
        const val CHANNEL_SIGNATURE_URL = "https://raw.githubusercontent.com/sashkinbro/EmuCoreX-News-Media-Hub/refs/heads/main/channels/stable-v1.sig"
        private const val SUPPORTED_CHANNEL_FORMAT = 1
        private const val SUPPORTED_CATALOG_FORMAT = 1
        private const val MAX_REDIRECTS = 4
        private const val CONNECT_TIMEOUT_MS = 8_000
        private const val READ_TIMEOUT_MS = 15_000
        private const val MAX_CHANNEL_BYTES = 64L * 1024L
        private const val MAX_SIGNATURE_BYTES = 4L * 1024L
        private const val MAX_MANIFEST_BYTES = 2L * 1024L * 1024L
        private const val MAX_INDEX_BYTES = 2L * 1024L * 1024L
        private const val MAX_ASSET_CATALOG_BYTES = 2L * 1024L * 1024L
        private const val MAX_DOCUMENT_BYTES = 512L * 1024L
        private const val MAX_IMAGE_BYTES = 8L * 1024L * 1024L
    }
}

private fun ByteArray.toHex(): String = joinToString("") { byte -> "%02x".format(byte.toInt() and 0xff) }
