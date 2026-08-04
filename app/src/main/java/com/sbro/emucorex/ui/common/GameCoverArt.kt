package com.sbro.emucorex.ui.common

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Log
import android.util.LruCache
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.security.MessageDigest
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit

private val imageLoadingSemaphore = Semaphore(4)
@Composable
fun GameCoverArt(
    coverPath: String?,
    fallbackTitle: String,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Fit,
    loadEnabled: Boolean = true
) {
    val context = LocalContext.current
    var bitmap by remember(coverPath) { mutableStateOf(coverPath?.let(::getCachedBitmap)) }
    var loadedPath by remember(coverPath) { mutableStateOf(coverPath?.takeIf { getCachedBitmap(it) != null }) }
    var isLoading by remember { mutableStateOf(false) }

    LaunchedEffect(coverPath, loadEnabled) {
        if (coverPath.isNullOrBlank()) {
            bitmap = null
            loadedPath = null
            isLoading = false
            return@LaunchedEffect
        }

        if (!loadEnabled) {
            bitmap = getCachedBitmap(coverPath)
            loadedPath = coverPath.takeIf { bitmap != null }
            isLoading = false
            return@LaunchedEffect
        }

        if (loadedPath == coverPath && bitmap != null) {
            isLoading = false
            return@LaunchedEffect
        }

        getCachedBitmap(coverPath)?.let { cached ->
            bitmap = cached
            loadedPath = coverPath
            isLoading = false
            return@LaunchedEffect
        }

        isLoading = true
        val loadedBitmap = withContext(Dispatchers.IO) {
            imageLoadingSemaphore.withPermit {
                loadBitmap(context, coverPath)
            }
        }
        if (loadedBitmap != null) {
            putCachedBitmap(coverPath, loadedBitmap)
            loadedPath = coverPath
        }
        if (loadedBitmap != null || bitmap == null) {
            bitmap = loadedBitmap
        }
        isLoading = false
    }

    val currentBitmap = bitmap
    if (currentBitmap != null) {
        Image(
            bitmap = currentBitmap.asImageBitmap(),
            contentDescription = fallbackTitle,
            contentScale = contentScale,
            modifier = modifier
        )
    } else {
        Box(
            modifier = modifier
                .clip(RoundedCornerShape(20.dp))
                .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f))
                .shimmer(showShimmer = isLoading),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = fallbackTitle,
                style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.Medium),
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.78f),
                textAlign = TextAlign.Center,
                maxLines = 4,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier
                    .fillMaxSize()
                    .padding(12.dp)
                    .wrapContentSize(Alignment.Center)
            )
        }
    }
}

private val coverBitmapCache = object : LruCache<String, Bitmap>(100 * 1024 * 1024) { // 100 MB cache
    override fun sizeOf(key: String, value: Bitmap): Int = value.allocationByteCount
}

fun clearCoverImageMemoryCache() {
    coverBitmapCache.evictAll()
    // Let the next remote request prune/reconcile its freshly cleared disk directory again.
    remoteCachePruned.set(false)
}

private fun getCachedBitmap(path: String): Bitmap? = coverBitmapCache.get(path)

private fun putCachedBitmap(path: String, bitmap: Bitmap) {
    if (getCachedBitmap(path) == null) {
        coverBitmapCache.put(path, bitmap)
    }
}

private fun loadBitmap(context: android.content.Context, coverPath: String?): Bitmap? {
    if (coverPath.isNullOrBlank()) return null

    val reqWidth = 400
    val reqHeight = 600

    return runCatching {
        fun openStream() = when {
            coverPath.startsWith("content://") -> {
                context.contentResolver.openInputStream(coverPath.toUri())
            }
            coverPath.startsWith("http://") || coverPath.startsWith("https://") -> {
                val cachedFile = getOrCreateRemoteImageCacheFile(context, coverPath)
                if (cachedFile != null && cachedFile.exists()) {
                    cachedFile.inputStream()
                } else {
                    val connection = URL(coverPath).openConnection() as HttpURLConnection
                    connection.connectTimeout = 8_000
                    connection.readTimeout = 12_000
                    connection.instanceFollowRedirects = true
                    connection.inputStream
                }
            }
            else -> {
                val file = File(coverPath)
                if (file.exists()) file.inputStream() else null
            }
        }

        val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        openStream()?.use { stream ->
            BitmapFactory.decodeStream(stream, null, options)
        }
        
        if (options.outWidth <= 0 || options.outHeight <= 0) return null
        var inSampleSize = 1
        val height = options.outHeight
        val width = options.outWidth
        
        if (height > reqHeight || width > reqWidth) {
            val halfHeight = height / 2
            val halfWidth = width / 2
            while (halfHeight / inSampleSize >= reqHeight && halfWidth / inSampleSize >= reqWidth) {
                inSampleSize *= 2
            }
        }
        
        val decodeOptions = BitmapFactory.Options().apply {
            this.inSampleSize = inSampleSize
            this.inJustDecodeBounds = false
            this.inPreferredConfig = Bitmap.Config.RGB_565
        }
        
        openStream()?.use { stream ->
            BitmapFactory.decodeStream(stream, null, decodeOptions)
        }
    }.getOrElse { e ->
        Log.w("GameCoverArt", "Failed to load cover from $coverPath: ${e.message}")
        null
    }
}

private fun getOrCreateRemoteImageCacheFile(context: android.content.Context, url: String): File? {
    val cacheDir = File(context.cacheDir, "remote-image-cache").apply { mkdirs() }
    pruneRemoteImageCacheOnce(cacheDir)
    val missFile = File(cacheDir, "${url.sha1()}.miss")
    if (missFile.exists()) {
        if (System.currentTimeMillis() - missFile.lastModified() < REMOTE_MISS_TTL_MS) return null
        missFile.delete()
    }
    var receivedNotFound = false
    for (candidateUrl in remoteImageCandidates(url)) {
        val extension = candidateUrl.substringAfterLast('.', "").substringBefore('?').lowercase()
            .takeIf { it in setOf("jpg", "jpeg", "png", "webp") }
            ?: "img"
        val targetFile = File(cacheDir, "${candidateUrl.sha1()}.$extension")
        if (isDecodableCachedImage(targetFile)) {
            return targetFile
        }
        if (targetFile.exists()) targetFile.delete()

        val tempFile = File.createTempFile("${targetFile.name}.", ".tmp", cacheDir)
        val downloaded = runCatching {
            val connection = URL(candidateUrl).openConnection() as HttpURLConnection
            try {
                connection.connectTimeout = 8_000
                connection.readTimeout = 12_000
                connection.instanceFollowRedirects = true
                if (connection.responseCode !in 200..299) {
                    if (connection.responseCode == HttpURLConnection.HTTP_NOT_FOUND) receivedNotFound = true
                    return@runCatching null
                }
                connection.inputStream.use { input ->
                    tempFile.outputStream().use { output ->
                        input.copyTo(output)
                    }
                }
            } finally {
                connection.disconnect()
            }

            if (!isDecodableCachedImage(tempFile)) return@runCatching null
            synchronized(remoteCacheWriteLock) {
                if (isDecodableCachedImage(targetFile)) {
                    return@synchronized targetFile
                }
                if (targetFile.exists()) targetFile.delete()
                if (!tempFile.renameTo(targetFile)) tempFile.copyTo(targetFile, overwrite = true)
                targetFile.takeIf(::isDecodableCachedImage)
            }
        }.getOrNull()

        if (downloaded != null) {
            missFile.delete()
            return downloaded
        }
        tempFile.delete()
    }
    if (receivedNotFound) {
        runCatching { missFile.writeText(System.currentTimeMillis().toString()) }
    }
    return null
}

private const val REMOTE_MISS_TTL_MS = 7L * 24L * 60L * 60L * 1000L
private const val REMOTE_CACHE_MAX_BYTES = 256L * 1024L * 1024L
private val remoteCachePruned = AtomicBoolean(false)
private val remoteCacheWriteLock = Any()

private fun isDecodableCachedImage(file: File): Boolean {
    if (!file.isFile || file.length() <= 0L) return false
    return runCatching {
        val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(file.absolutePath, options)
        options.outWidth > 0 && options.outHeight > 0
    }.getOrDefault(false)
}

private fun pruneRemoteImageCacheOnce(cacheDir: File) {
    if (!remoteCachePruned.compareAndSet(false, true)) return
    val cachedImages = cacheDir.listFiles()
        .orEmpty()
        .filter { it.isFile && !it.name.endsWith(".tmp") && !it.name.endsWith(".miss") }
        .sortedBy { it.lastModified() }
    var totalBytes = cachedImages.sumOf { it.length() }
    for (file in cachedImages) {
        if (totalBytes <= REMOTE_CACHE_MAX_BYTES) break
        val size = file.length()
        if (file.delete()) totalBytes -= size
    }
    cacheDir.listFiles()
        .orEmpty()
        .filter { it.name.endsWith(".tmp") && System.currentTimeMillis() - it.lastModified() > 60L * 60L * 1000L }
        .forEach { it.delete() }
}

private fun remoteImageCandidates(url: String): List<String> {
    val withoutQuery = url.substringBefore('?')
    return when {
        withoutQuery.endsWith(".jpg", ignoreCase = true) -> listOf(url, url.replaceSuffixPreservingQuery(".jpg", ".png"))
        withoutQuery.endsWith(".jpeg", ignoreCase = true) -> listOf(url, url.replaceSuffixPreservingQuery(".jpeg", ".png"))
        withoutQuery.endsWith(".png", ignoreCase = true) -> listOf(url, url.replaceSuffixPreservingQuery(".png", ".jpg"))
        else -> listOf(url)
    }.distinct()
}

private fun String.replaceSuffixPreservingQuery(oldSuffix: String, newSuffix: String): String {
    val query = substringAfter('?', missingDelimiterValue = "")
    val base = substringBefore('?')
    val replacedBase = base.removeSuffix(oldSuffix).removeSuffix(oldSuffix.uppercase()) + newSuffix
    return if (query.isBlank()) replacedBase else "$replacedBase?$query"
}

private fun String.sha1(): String {
    val digest = MessageDigest.getInstance("SHA-1")
    return digest.digest(toByteArray()).joinToString("") { "%02x".format(it) }
}
