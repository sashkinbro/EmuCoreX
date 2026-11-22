package com.sbro.emucorex.ps2ui

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Safe RGBA8888 image decoder for PS2 game covers and quick-save PNG screenshots.
 */
object Ps2ImageDecoder {
    private val decodeLock = Any()
    private var pixelScratch = IntArray(0)

    /**
     * Decodes [path] bounded to [maxDimension] px on the longest side.
     * Returns `[width, height]` (8 bytes LE) followed by `width * height * 4` RGBA bytes.
     */
    @JvmStatic
    fun decodeRgba(path: String, maxDimension: Int): ByteArray? {
        if (path.isBlank()) return null
        val file = File(path)
        if (!file.isFile || file.length() <= 0L) return null
        val maxDim = maxDimension.coerceIn(32, 1024)

        return runCatching {
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(path, bounds)
            if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return@runCatching null

            var sampleSize = 1
            val longest = max(bounds.outWidth, bounds.outHeight)
            while (longest / (sampleSize * 2) >= maxDim) {
                sampleSize *= 2
            }

            val decodeOpts = BitmapFactory.Options().apply {
                inSampleSize = sampleSize
                inPreferredConfig = Bitmap.Config.ARGB_8888
            }
            val decoded = BitmapFactory.decodeFile(path, decodeOpts) ?: return@runCatching null
            var bitmap = decoded
            try {
                val w = bitmap.width
                val h = bitmap.height
                val curLongest = max(w, h)
                if (curLongest > maxDim) {
                    val scale = maxDim.toFloat() / curLongest.toFloat()
                    val dstW = (w * scale).roundToInt().coerceAtLeast(1)
                    val dstH = (h * scale).roundToInt().coerceAtLeast(1)
                    val scaled = Bitmap.createScaledBitmap(bitmap, dstW, dstH, true)
                    if (scaled !== bitmap) {
                        bitmap.recycle()
                        bitmap = scaled
                    }
                }
                if (bitmap.config != Bitmap.Config.ARGB_8888) {
                    val converted = bitmap.copy(Bitmap.Config.ARGB_8888, false)
                    if (converted != null) {
                        if (converted !== bitmap) {
                            bitmap.recycle()
                        }
                        bitmap = converted
                    }
                }

                val outW = bitmap.width
                val outH = bitmap.height
                val numPixels = outW * outH
                synchronized(decodeLock) {
                    if (pixelScratch.size < numPixels) {
                        pixelScratch = IntArray(numPixels)
                    }
                    val pixels = pixelScratch
                    bitmap.getPixels(pixels, 0, outW, 0, 0, outW, outH)

                    val out = ByteBuffer.allocate(8 + numPixels * 4).order(ByteOrder.LITTLE_ENDIAN)
                    out.putInt(outW)
                    out.putInt(outH)
                    val raw = out.array()
                    var offset = 8
                    for (i in 0 until numPixels) {
                        val argb = pixels[i]
                        raw[offset] = ((argb ushr 16) and 0xFF).toByte()
                        raw[offset + 1] = ((argb ushr 8) and 0xFF).toByte()
                        raw[offset + 2] = (argb and 0xFF).toByte()
                        raw[offset + 3] = ((argb ushr 24) and 0xFF).toByte()
                        offset += 4
                    }
                    raw
                }
            } finally {
                bitmap.recycle()
            }
        }.getOrNull()
    }
}
