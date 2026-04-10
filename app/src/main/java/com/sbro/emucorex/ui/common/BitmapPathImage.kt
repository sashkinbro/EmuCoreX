package com.sbro.emucorex.ui.common

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import androidx.core.net.toUri
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.Collections

@Composable
fun BitmapPathImage(
    imagePath: String?,
    contentDescription: String?,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Crop,
    fallback: @Composable (() -> Unit)? = null
) {
    val context = LocalContext.current
    val bitmapState = produceState<Bitmap?>(initialValue = null, key1 = imagePath) {
        value = loadBitmapSafely(context, imagePath)
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

    imageCache[normalizedPath]?.let { return it }

    val bitmap = withContext(Dispatchers.IO) {
        runCatching {
            when {
                normalizedPath.startsWith("content://") -> {
                    context.contentResolver.openInputStream(normalizedPath.toUri())?.use { stream ->
                        BitmapFactory.decodeStream(stream, null, bitmapOptions())
                    }
                }
                normalizedPath.startsWith("http://") || normalizedPath.startsWith("https://") -> {
                    loadBitmapFromUrl(normalizedPath)
                }
                else -> {
                    val file = File(normalizedPath)
                    if (file.exists()) {
                        BitmapFactory.decodeFile(file.absolutePath, bitmapOptions())
                    } else {
                        null
                    }
                }
            }
        }.getOrNull()
    }

    if (bitmap != null) {
        imageCache[normalizedPath] = bitmap
    }

    return bitmap
}

private fun loadBitmapFromUrl(url: String): Bitmap? {
    var connection: HttpURLConnection? = null
    return runCatching {
        connection = (URL(url).openConnection() as HttpURLConnection).apply {
            connectTimeout = 8_000
            readTimeout = 12_000
            instanceFollowRedirects = true
            doInput = true
            setRequestProperty("User-Agent", "EmuCoreX/1.0")
        }

        val responseCode = connection.responseCode
        if (responseCode !in 200..299) {
            return null
        }

        connection.inputStream?.use { stream ->
            BitmapFactory.decodeStream(stream, null, bitmapOptions())
        }
    }.getOrNull().also {
        connection?.disconnect()
    }
}

private fun bitmapOptions(): BitmapFactory.Options {
    return BitmapFactory.Options().apply {
        inPreferredConfig = Bitmap.Config.RGB_565
    }
}

private val imageCache = Collections.synchronizedMap(mutableMapOf<String, Bitmap>())
