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
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.Shape
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
import com.sbro.emucorex.ui.theme.neon.neonShape

/** Portrait box-art canvas; images retain their original proportions within it. */
val LocalGameCoverAspectRatio = staticCompositionLocalOf { 2f / 3f }
val GameCoverAspectRatio: Float
    @Composable get() = LocalGameCoverAspectRatio.current

internal fun isGenerated3dCover(coverPath: String?): Boolean =
    coverPath?.replace('\\', '/')?.let { path ->
        val localCache3d = path.contains("/game-covers/") &&
            (path.endsWith("_3d.png") || path.endsWith("_3d.jpg"))
        val remote3d = path.contains("/covers/3d/") &&
            (path.endsWith(".png") || path.endsWith(".jpg"))
        localCache3d || remote3d
    } == true

/** 3D covers render with transparent padding, so rounding would cut their corners. */
fun gameCoverShape(coverPath: String?, roundedShape: Shape): Shape =
    if (isGenerated3dCover(coverPath)) RectangleShape else roundedShape

/** 3D covers must be fitted whole; regular covers fill and crop their frame. */
fun gameCoverContentScale(coverPath: String?, defaultScale: ContentScale): ContentScale =
    if (isGenerated3dCover(coverPath)) ContentScale.Fit else defaultScale

private val imageLoadingSemaphore = Semaphore(4)
private const val DEFAULT_COVER_DECODE_WIDTH = 400
private const val DEFAULT_COVER_DECODE_HEIGHT = 600

private val imagePrefetchSemaphore = Semaphore(1)

/** Warms only the exact size/key used by ordinary library cards, with one speculative decoder. */
internal suspend fun prefetchGameCoverArt(context: android.content.Context, coverPath: String) {
    if (coverPath.isBlank()) return
    val cacheKey = "$coverPath@${DEFAULT_COVER_DECODE_WIDTH}x$DEFAULT_COVER_DECODE_HEIGHT"
    if (getCachedBitmap(cacheKey) != null) return
    imagePrefetchSemaphore.withPermit {
        withContext(Dispatchers.IO) {
            imageLoadingSemaphore.withPermit {
                if (getCachedBitmap(cacheKey) == null) {
                    loadBitmap(context, coverPath, DEFAULT_COVER_DECODE_WIDTH, DEFAULT_COVER_DECODE_HEIGHT)
                        ?.let { putCachedBitmap(cacheKey, it) }
                }
            }
        }
    }
}

@Composable
fun GameCoverArt(
    coverPath: String?,
    fallbackTitle: String,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Fit,
    loadEnabled: Boolean = true,
    fallbackCoverPath: String? = null,
    showTitleWhileLoading: Boolean = true,
    shimmerWhileLoading: Boolean = true,
    decodeWidth: Int = DEFAULT_COVER_DECODE_WIDTH,
    decodeHeight: Int = DEFAULT_COVER_DECODE_HEIGHT,
    onAspectRatioResolved: ((Float) -> Unit)? = null
) {
    val context = LocalContext.current
    val candidates = remember(coverPath, fallbackCoverPath) {
        listOfNotNull(
            coverPath?.takeIf { it.isNotBlank() },
            fallbackCoverPath?.takeIf { it.isNotBlank() && it != coverPath }
        )
    }
    val cacheKey: (String) -> String = remember(decodeWidth, decodeHeight) {
        { path -> "$path@${decodeWidth}x$decodeHeight" }
    }
    var bitmap by remember(coverPath, fallbackCoverPath, decodeWidth, decodeHeight) {
        mutableStateOf(candidates.firstNotNullOfOrNull { getCachedBitmap(cacheKey(it)) })
    }
    var loadedPath by remember(coverPath, fallbackCoverPath, decodeWidth, decodeHeight) {
        mutableStateOf(candidates.firstOrNull { getCachedBitmap(cacheKey(it)) != null })
    }
    var isLoading by remember { mutableStateOf(false) }

    LaunchedEffect(coverPath, fallbackCoverPath, loadEnabled, decodeWidth, decodeHeight) {
        if (candidates.isEmpty()) {
            bitmap = null
            loadedPath = null
            isLoading = false
            return@LaunchedEffect
        }

        if (!loadEnabled) {
            bitmap = candidates.firstNotNullOfOrNull { getCachedBitmap(cacheKey(it)) }
            loadedPath = candidates.firstOrNull { getCachedBitmap(cacheKey(it)) != null }
            isLoading = false
            return@LaunchedEffect
        }

        if (loadedPath != null && bitmap != null) {
            isLoading = false
            return@LaunchedEffect
        }

        candidates.firstNotNullOfOrNull { getCachedBitmap(cacheKey(it)) }?.let { cached ->
            bitmap = cached
            loadedPath = candidates.first { getCachedBitmap(cacheKey(it)) != null }
            isLoading = false
            return@LaunchedEffect
        }

        isLoading = true
        var loadedBitmap: Bitmap? = null
        var loadedFrom: String? = null
        for (candidate in candidates) {
            val candidateBitmap = withContext(Dispatchers.IO) {
                imageLoadingSemaphore.withPermit {
                    getCachedBitmap(cacheKey(candidate)) ?: loadBitmap(context, candidate, decodeWidth, decodeHeight)
                }
            }
            if (candidateBitmap != null) {
                putCachedBitmap(cacheKey(candidate), candidateBitmap)
                loadedBitmap = candidateBitmap
                loadedFrom = candidate
                break
            }
        }
        if (loadedBitmap != null) {
            loadedPath = loadedFrom
        }
        if (loadedBitmap != null || bitmap == null) {
            bitmap = loadedBitmap
        }
        isLoading = false
    }

    val currentAspectRatioCallback by rememberUpdatedState(onAspectRatioResolved)
    LaunchedEffect(bitmap) {
        val loaded = bitmap ?: return@LaunchedEffect
        if (loaded.height > 0) {
            currentAspectRatioCallback?.invoke(loaded.width.toFloat() / loaded.height.toFloat())
        }
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
                .clip(neonShape(20.dp))
                .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f))
                .shimmer(showShimmer = isLoading && shimmerWhileLoading),
            contentAlignment = Alignment.Center
        ) {
            if (!isLoading || showTitleWhileLoading) {
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

private fun loadBitmap(
    context: android.content.Context,
    coverPath: String?,
    reqWidth: Int = DEFAULT_COVER_DECODE_WIDTH,
    reqHeight: Int = DEFAULT_COVER_DECODE_HEIGHT
): Bitmap? {
    if (coverPath.isNullOrBlank()) return null

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
            this.inPreferredConfig = Bitmap.Config.ARGB_8888
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
