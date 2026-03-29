package com.sbro.emucorex.ui.common

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
        value = runCatching {
            when {
                imagePath.isNullOrBlank() -> null
                imagePath.startsWith("content://") -> {
                    context.contentResolver.openInputStream(imagePath.toUri())?.use {
                        BitmapFactory.decodeStream(it)
                    }
                }
                imagePath.startsWith("http://") || imagePath.startsWith("https://") -> {
                    val connection = URL(imagePath).openConnection() as HttpURLConnection
                    connection.connectTimeout = 8000
                    connection.readTimeout = 12000
                    connection.instanceFollowRedirects = true
                    connection.inputStream.use { BitmapFactory.decodeStream(it) }
                }
                else -> {
                    val file = File(imagePath)
                    if (file.exists()) BitmapFactory.decodeFile(file.absolutePath) else null
                }
            }
        }.getOrNull()
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
