package com.sbro.emucorex.ui.common

import android.graphics.BitmapFactory
import androidx.annotation.DrawableRes
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.EmulationSideArtwork
import com.sbro.emucorex.data.EmulationSideArtworkRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.math.roundToInt

data class SideArtworkGutters(
    val leftPx: Int,
    val rightPx: Int
) {
    val isVisible: Boolean get() = leftPx > 0 || rightPx > 0
}

data class SideArtworkPreviewLayout(
    val heightDp: Int,
    val contentFraction: Float
)

fun calculateSideArtworkPreviewLayout(widthDp: Int, heightDp: Int): SideArtworkPreviewLayout {
    val landscape = widthDp > heightDp
    return if (landscape) {
        SideArtworkPreviewLayout(
            heightDp = (heightDp * 0.42f).roundToInt().coerceIn(132, 180),
            contentFraction = 0.58f
        )
    } else {
        SideArtworkPreviewLayout(
            heightDp = (heightDp * 0.24f).roundToInt().coerceIn(148, 190),
            contentFraction = 0.60f
        )
    }
}

fun calculateSideArtworkPreviewGutters(
    widthPx: Int,
    contentFraction: Float
): SideArtworkGutters {
    if (widthPx <= 0) return SideArtworkGutters(0, 0)
    val unusedWidth = (widthPx * (1f - contentFraction.coerceIn(0.1f, 1f)))
        .roundToInt()
        .coerceAtLeast(0)
    val left = unusedWidth / 2
    return SideArtworkGutters(left, unusedWidth - left)
}

/**
 * Calculates conservative side gutters for the renderer's aspect-ratio modes.
 * Auto starts at the PS2-standard 4:3 until the renderer reports its exact draw rectangle.
 */
fun calculateSideArtworkGutters(
    widthPx: Int,
    heightPx: Int,
    aspectRatioMode: Int,
    nativeDrawRect: FloatArray? = null
): SideArtworkGutters {
    if (widthPx <= 0 || heightPx <= 0 || aspectRatioMode == 0) {
        return SideArtworkGutters(0, 0)
    }
    if (nativeDrawRect != null && nativeDrawRect.size >= 4) {
        val left = nativeDrawRect[0].roundToInt().coerceIn(0, widthPx)
        val right = (widthPx - nativeDrawRect[2].roundToInt()).coerceIn(0, widthPx)
        return if (left + right >= 4) SideArtworkGutters(left, right) else SideArtworkGutters(0, 0)
    }
    val contentAspect = when (aspectRatioMode) {
        // Prevent the previous SurfaceView frame from showing edge-to-edge while the new
        // game starts. The renderer-provided rectangle replaces this as soon as it exists.
        1 -> 4f / 3f
        2 -> 4f / 3f
        3 -> 16f / 9f
        4 -> 10f / 7f
        else -> return SideArtworkGutters(0, 0)
    }
    val contentWidth = (heightPx * contentAspect).roundToInt().coerceAtMost(widthPx)
    val unusedWidth = (widthPx - contentWidth).coerceAtLeast(0)
    if (unusedWidth < 4) return SideArtworkGutters(0, 0)
    val left = unusedWidth / 2
    return SideArtworkGutters(left, unusedWidth - left)
}

@Composable
fun EmulationSideArtworkOverlay(
    artwork: EmulationSideArtwork,
    revision: Int,
    aspectRatioMode: Int,
    modifier: Modifier = Modifier,
    nativeDrawRect: FloatArray? = null,
    preview: Boolean = false,
    previewContentFraction: Float = 0.60f,
    dimPercent: Int = 0
) {
    if (artwork == EmulationSideArtwork.NONE) return
    val painter = rememberSideArtworkPainter(artwork, revision) ?: return

    BoxWithConstraints(modifier = modifier) {
        val density = androidx.compose.ui.platform.LocalDensity.current
        val widthPx = with(density) { maxWidth.roundToPx() }
        val heightPx = with(density) { maxHeight.roundToPx() }
        val gutters = if (preview) {
            calculateSideArtworkPreviewGutters(widthPx, previewContentFraction)
        } else {
            calculateSideArtworkGutters(widthPx, heightPx, aspectRatioMode, nativeDrawRect)
        }
        if (!gutters.isVisible) return@BoxWithConstraints
        Image(
            painter = painter,
            contentDescription = null,
            modifier = Modifier
                .fillMaxSize()
                .graphicsLayer { alpha = 1f - (dimPercent.coerceIn(0, 85) / 100f) }
                .drawWithContent {
                    if (gutters.leftPx > 0) {
                        clipRect(right = gutters.leftPx.toFloat()) {
                            this@drawWithContent.drawContent()
                        }
                    }
                    if (gutters.rightPx > 0) {
                        clipRect(left = size.width - gutters.rightPx) {
                            this@drawWithContent.drawContent()
                        }
                    }
                },
            contentScale = ContentScale.Crop
        )

        if (preview) {
            val leftWidth = with(density) { gutters.leftPx.toDp() }
            val rightWidth = with(density) { gutters.rightPx.toDp() }
            Box(
                modifier = Modifier
                    .width(maxWidth - leftWidth - rightWidth)
                    .fillMaxHeight()
                    .align(Alignment.Center)
            )
        }
    }
}

@Composable
fun EmulationSideArtworkThumbnail(
    artwork: EmulationSideArtwork,
    revision: Int,
    modifier: Modifier = Modifier
) {
    val painter = rememberSideArtworkPainter(artwork, revision) ?: return
    Image(
        painter = painter,
        contentDescription = null,
        modifier = modifier,
        contentScale = ContentScale.Crop
    )
}

@Composable
private fun rememberSideArtworkPainter(
    artwork: EmulationSideArtwork,
    revision: Int
): Painter? {
    val builtIn = artwork.drawableResource()
    if (builtIn != null) return painterResource(builtIn)
    if (artwork != EmulationSideArtwork.CUSTOM) return null

    val context = LocalContext.current
    val repository = androidx.compose.runtime.remember(context) { EmulationSideArtworkRepository(context) }
    val file = repository.existingCustomFile()
    val bitmap by produceState<androidx.compose.ui.graphics.ImageBitmap?>(
        initialValue = null,
        key1 = file?.absolutePath,
        key2 = revision,
        key3 = file?.lastModified()
    ) {
        value = withContext(Dispatchers.IO) {
            file?.takeIf { it.isFile }?.let(::decodeSideArtwork)?.asImageBitmap()
        }
    }
    return bitmap?.let(::BitmapPainter)
}

private fun decodeSideArtwork(file: java.io.File): android.graphics.Bitmap? {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeFile(file.absolutePath, bounds)
    if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null

    var sampleSize = 1
    while (bounds.outWidth / sampleSize > 4096 || bounds.outHeight / sampleSize > 4096) {
        sampleSize *= 2
    }
    return BitmapFactory.decodeFile(
        file.absolutePath,
        BitmapFactory.Options().apply { inSampleSize = sampleSize }
    )
}

@DrawableRes
private fun EmulationSideArtwork.drawableResource(): Int? = when (this) {
    EmulationSideArtwork.OLYMPUS -> R.drawable.emulation_side_art_olympus
    EmulationSideArtwork.NIGHT_RACING -> R.drawable.emulation_side_art_night_racing
    EmulationSideArtwork.JUNGLE -> R.drawable.emulation_side_art_jungle
    EmulationSideArtwork.COLOSSUS -> R.drawable.emulation_side_art_colossus
    EmulationSideArtwork.GOTHIC -> R.drawable.emulation_side_art_gothic
    EmulationSideArtwork.STEALTH -> R.drawable.emulation_side_art_stealth
    EmulationSideArtwork.SAMURAI -> R.drawable.emulation_side_art_samurai
    EmulationSideArtwork.WEST_COAST -> R.drawable.emulation_side_art_west_coast
    EmulationSideArtwork.CRYSTAL -> R.drawable.emulation_side_art_crystal
    EmulationSideArtwork.NONE,
    EmulationSideArtwork.CUSTOM -> null
}
