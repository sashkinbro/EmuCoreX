package com.sbro.emucorex.ui.common

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.LruCache
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import android.net.ConnectivityManager
import android.net.Network
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.core.net.toUri
import java.io.ByteArrayInputStream
import java.io.File
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext

private const val TARGET_MAX_DIMENSION_PX = 384
private const val IMAGE_CACHE_MAX_BYTES = 32 * 1024 * 1024

private val imageLoadingSemaphore = Semaphore(4)

private val imageCache = object : LruCache<String, Bitmap>(IMAGE_CACHE_MAX_BYTES) {
    override fun sizeOf(key: String, value: Bitmap): Int = value.allocationByteCount
}

/** Bumps whenever a network becomes available so failed remote images (badges) can retry. */
private object ImageNetworkRetry {
    val tick = MutableStateFlow(0)
    private var registered = false

    @Synchronized
    fun ensureRegistered(context: Context) {
        if (registered) return
        registered = true
        runCatching {
            val manager = context.applicationContext.getSystemService(ConnectivityManager::class.java)
            manager?.registerDefaultNetworkCallback(object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(network: Network) {
                    tick.update { it + 1 }
                }
            })
        }
    }
}

private val remoteRetryDelaysMs = longArrayOf(4_000L, 12_000L)

@Composable
fun BitmapPathImage(
    imagePath: String?,
    contentDescription: String?,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Crop,
    fallback: @Composable (() -> Unit)? = null
) {
    val context = LocalContext.current
    val isRemote = imagePath?.trim()?.let { it.startsWith("http://") || it.startsWith("https://") } == true
    if (isRemote) ImageNetworkRetry.ensureRegistered(context)
    val networkTick by ImageNetworkRetry.tick.collectAsState()
    val bitmapState = produceState<Bitmap?>(initialValue = null, key1 = imagePath, key2 = if (isRemote) networkTick else 0) {
        value = loadBitmapSafely(context, imagePath)
        if (value == null && isRemote) {
            // A failed download is retried with a short backoff and again when the network returns.
            for (wait in remoteRetryDelaysMs) {
                delay(wait)
                value = loadBitmapSafely(context, imagePath)
                if (value != null) break
            }
        }
    }

    val bitmap = bitmapState.value
    if (bitmap != null) {
        Image(
            bitmap = bitmap.asImageBitmap(),
            contentDescription = contentDescription,
            modifier = modifier,
            contentScale = contentScale
        )
    } else if (fallback != null) {
        Box(
            modifier = modifier,
            contentAlignment = Alignment.Center
        ) {
            fallback()
        }
    }
}

private suspend fun loadBitmapSafely(context: Context, imagePath: String?): Bitmap? {
    val normalizedPath = imagePath?.trim().orEmpty()
    if (normalizedPath.isBlank()) return null

    imageCache.get(normalizedPath)?.let { return it }

    val bitmap = withContext(Dispatchers.IO) {
        imageLoadingSemaphore.withPermit {
            runCatching {
                when {
                    normalizedPath.startsWith("content://") -> decodeSampled {
                        context.contentResolver.openInputStream(normalizedPath.toUri())
                    }
                    normalizedPath.startsWith("http://") || normalizedPath.startsWith("https://") -> {
                        loadBitmapFromUrl(normalizedPath)
                    }
                    else -> decodeSampled {
                        val file = File(normalizedPath)
                        if (file.exists()) file.inputStream() else null
                    }
                }
            }.getOrNull()
        }
    }

    if (bitmap != null) {
        imageCache.put(normalizedPath, bitmap)
    }

    return bitmap
}

private fun loadBitmapFromUrl(url: String): Bitmap? {
    val connection = runCatching { URL(url).openConnection() as HttpURLConnection }.getOrNull()
        ?: return null
    return try {
        runCatching {
            connection.connectTimeout = 8_000
            connection.readTimeout = 12_000
            connection.instanceFollowRedirects = true
            connection.doInput = true
            connection.setRequestProperty("User-Agent", "EmuCoreX/1.0")

            if (connection.responseCode !in 200..299) return null
            val bytes = connection.inputStream?.use { it.readBytes() } ?: return null
            decodeSampled { ByteArrayInputStream(bytes) }
        }.getOrNull()
    } finally {
        connection.disconnect()
    }
}

private fun decodeSampled(openStream: () -> InputStream?): Bitmap? {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    openStream()?.use { BitmapFactory.decodeStream(it, null, bounds) }
    if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null

    var sampleSize = 1
    var largestDimension = maxOf(bounds.outWidth, bounds.outHeight)
    while (largestDimension / 2 >= TARGET_MAX_DIMENSION_PX) {
        sampleSize *= 2
        largestDimension /= 2
    }

    val options = BitmapFactory.Options().apply {
        inSampleSize = sampleSize
        inPreferredConfig = Bitmap.Config.ARGB_8888
    }
    return openStream()?.use { BitmapFactory.decodeStream(it, null, options) }
}
