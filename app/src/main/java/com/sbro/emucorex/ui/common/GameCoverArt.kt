package com.sbro.emucorex.ui.common

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Log
import android.util.LruCache
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit

private val imageLoadingSemaphore = Semaphore(4)
@Composable
fun GameCoverArt(
    coverPath: String?,
    fallbackTitle: String,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Fit
) {
    val context = LocalContext.current
    var bitmap by remember { mutableStateOf(coverPath?.let(::getCachedBitmap)) }
    var loadedPath by remember { mutableStateOf(coverPath?.takeIf { getCachedBitmap(it) != null }) }
    var isLoading by remember { mutableStateOf(false) }

    LaunchedEffect(coverPath) {
        if (coverPath.isNullOrBlank()) {
            bitmap = null
            loadedPath = null
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
            if (!isLoading) {
                Icon(
                    imageVector = Icons.Rounded.SportsEsports,
                    contentDescription = null,
                    modifier = Modifier.size(40.dp),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.35f)
                )
            }
        }
    }
}

private val coverBitmapCache = object : LruCache<String, Bitmap>(100 * 1024 * 1024) { // 100 MB cache
    override fun sizeOf(key: String, value: Bitmap): Int = value.allocationByteCount
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
                val connection = URL(coverPath).openConnection() as HttpURLConnection
                connection.connectTimeout = 8_000
                connection.readTimeout = 12_000
                connection.instanceFollowRedirects = true
                connection.inputStream
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
