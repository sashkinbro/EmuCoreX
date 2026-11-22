package com.sbro.emucorex.ps2ui

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.text.Layout
import android.text.StaticLayout
import android.text.TextDirectionHeuristics
import android.text.TextPaint
import android.text.TextUtils
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * High-DPI shaped text rasterizer for the native GLES 3.0 PS2 BIOS UI.
 * Employs the sleek sans-serif-light / geometric aesthetic of the PlayStation 2
 * OSD while using Android StaticLayout so all 18 languages (Arabic/Persian RTL,
 * Hindi Devanagari, CJK, Cyrillic, Latin) render crisply at native density.
 */
object Ps2TextRasterizer {
    const val STYLE_HEADER = 0
    const val STYLE_MENU = 1
    const val STYLE_BODY = 2
    const val STYLE_BODY_BOLD = 3
    const val STYLE_MONO = 4

    const val ALIGN_START = 0
    const val ALIGN_CENTER = 1
    const val ALIGN_END = 2

    private val lock = Any()

    private val lightTypeface: Typeface =
        Typeface.create("sans-serif-light", Typeface.NORMAL)
    private val mediumTypeface: Typeface =
        Typeface.create("sans-serif-medium", Typeface.NORMAL)
    private val sansRegular: Typeface =
        Typeface.create("sans-serif", Typeface.NORMAL)
    private val monoRegular: Typeface =
        Typeface.create(Typeface.MONOSPACE, Typeface.NORMAL)

    private val mainPaint = TextPaint(Paint.ANTI_ALIAS_FLAG or Paint.SUBPIXEL_TEXT_FLAG).apply {
        color = Color.WHITE
    }
    private var pixelScratch = IntArray(0)

    /**
     * Rasterizes [text] into a byte buffer:
     * `[pixelWidth, pixelHeight, logicalWidthX100, logicalHeightX100]` (16 bytes LE)
     * followed by `pixelWidth * pixelHeight * 4` straight RGBA8888 bytes.
     */
    @JvmStatic
    fun rasterizeText(
        text: String,
        virtualFontSize: Float,
        style: Int,
        maxVirtualWidth: Float,
        maxLines: Int,
        align: Int,
        rasterScale: Float,
    ): ByteArray? {
        if (text.isEmpty()) return null
        val scale = rasterScale.coerceIn(1.25f, 2.5f)
        val pxFontSize = (virtualFontSize * scale).coerceIn(10f, 220f)
        val clampedMaxLines = maxLines.coerceIn(1, 16)
        val hasMaxWidth = maxVirtualWidth > 1f
        val pxMaxWidth = if (hasMaxWidth) {
            (maxVirtualWidth * scale).roundToInt().coerceIn(8, 3072)
        } else {
            3072
        }

        synchronized(lock) {
            val typeface = when (style) {
                STYLE_HEADER, STYLE_MENU -> lightTypeface
                STYLE_BODY_BOLD -> mediumTypeface
                STYLE_MONO -> monoRegular
                else -> sansRegular
            }
            mainPaint.style = Paint.Style.FILL
            mainPaint.color = Color.WHITE
            mainPaint.typeface = typeface
            mainPaint.textSize = pxFontSize
            mainPaint.letterSpacing = when (style) {
                STYLE_MENU -> 0.06f
                STYLE_HEADER -> 0.08f
                STYLE_MONO -> 0.02f
                else -> 0.0f
            }

            val shadowOffset = when (style) {
                STYLE_MENU -> max(1.6f, pxFontSize * 0.06f)
                STYLE_HEADER -> max(1.4f, pxFontSize * 0.055f)
                else -> max(0.9f, pxFontSize * 0.04f)
            }
            val padRight = ceil(shadowOffset + if (style == STYLE_MENU) scale * 2.5f else scale * 1.5f).toInt()
            val padLeft = ceil(if (style == STYLE_MENU) scale * 2.0f else scale * 1.0f).toInt()
            val padTop = ceil(scale * 1.2f).toInt()
            val padBottom = ceil(shadowOffset + scale * 1.5f).toInt()

            val isSingleLine = clampedMaxLines == 1 && text.indexOf('\n') < 0
            val desiredPx = if (isSingleLine) {
                ceil(mainPaint.measureText(text)).toInt().coerceIn(1, 3000)
            } else {
                ceil(StaticLayout.getDesiredWidth(text, mainPaint)).toInt().coerceIn(1, 3000)
            }

            val effectiveAvailPx = if (hasMaxWidth) {
                (pxMaxWidth - padLeft - padRight).coerceAtLeast(8)
            } else {
                desiredPx.coerceAtLeast(4)
            }

            val layoutAlign = when (align) {
                ALIGN_CENTER -> Layout.Alignment.ALIGN_CENTER
                ALIGN_END -> Layout.Alignment.ALIGN_OPPOSITE
                else -> Layout.Alignment.ALIGN_NORMAL
            }

            val layoutWidth = if (!isSingleLine && hasMaxWidth && align != ALIGN_START) {
                effectiveAvailPx
            } else if (hasMaxWidth) {
                desiredPx.coerceAtMost(effectiveAvailPx)
            } else {
                desiredPx
            }

            val mainLayout = buildLayout(text, mainPaint, layoutWidth, layoutAlign, clampedMaxLines, effectiveAvailPx)
            var measuredContentW = 0f
            for (i in 0 until mainLayout.lineCount) {
                measuredContentW = max(measuredContentW, mainLayout.getLineWidth(i))
            }
            val finalLayoutWidth = if (!isSingleLine && hasMaxWidth && align != ALIGN_START) {
                effectiveAvailPx
            } else {
                ceil(measuredContentW).toInt().coerceIn(1, effectiveAvailPx)
            }
            val finalMainLayout = if (finalLayoutWidth != layoutWidth) {
                buildLayout(text, mainPaint, finalLayoutWidth, layoutAlign, clampedMaxLines, finalLayoutWidth)
            } else {
                mainLayout
            }

            val texW = (finalMainLayout.width + padLeft + padRight).coerceIn(2, 3200)
            val texH = (finalMainLayout.height + padTop + padBottom).coerceIn(2, 2048)

            val bitmap = Bitmap.createBitmap(texW, texH, Bitmap.Config.ARGB_8888)
            try {
                val canvas = Canvas(bitmap)

                // Soft PS2 translucent deep-navy/black drop shadow
                mainPaint.style = Paint.Style.FILL
                mainPaint.color = when (style) {
                    STYLE_MENU -> Color.argb(215, 0, 8, 28)
                    STYLE_HEADER -> Color.argb(200, 0, 10, 34)
                    else -> Color.argb(175, 0, 4, 16)
                }
                canvas.save()
                canvas.translate(padLeft + shadowOffset, padTop + shadowOffset)
                finalMainLayout.draw(canvas)
                canvas.restore()

                // Delicate luminous edge stroke on large PS2 menu / header typography
                if (style == STYLE_MENU || style == STYLE_HEADER) {
                    mainPaint.style = Paint.Style.STROKE
                    mainPaint.strokeWidth = max(1.1f, pxFontSize * 0.035f)
                    mainPaint.strokeJoin = Paint.Join.ROUND
                    mainPaint.color = Color.argb(115, 95, 195, 255)
                    canvas.save()
                    canvas.translate(padLeft.toFloat(), padTop.toFloat())
                    finalMainLayout.draw(canvas)
                    canvas.restore()
                }

                // Crisp luminous foreground glyphs
                mainPaint.style = Paint.Style.FILL
                mainPaint.color = Color.WHITE
                canvas.save()
                canvas.translate(padLeft.toFloat(), padTop.toFloat())
                finalMainLayout.draw(canvas)
                canvas.restore()

                val numPixels = texW * texH
                if (pixelScratch.size < numPixels) {
                    pixelScratch = IntArray(numPixels)
                }
                val pixels = pixelScratch
                bitmap.getPixels(pixels, 0, texW, 0, 0, texW, texH)

                val out = ByteBuffer.allocate(16 + numPixels * 4).order(ByteOrder.LITTLE_ENDIAN)
                out.putInt(texW)
                out.putInt(texH)
                out.putInt(((texW / scale) * 100f).roundToInt())
                out.putInt(((texH / scale) * 100f).roundToInt())

                val rawBytes = out.array()
                var offset = 16
                for (i in 0 until numPixels) {
                    val argb = pixels[i]
                    val a = (argb ushr 24) and 0xFF
                    if (a == 0) {
                        rawBytes[offset] = 0
                        rawBytes[offset + 1] = 0
                        rawBytes[offset + 2] = 0
                        rawBytes[offset + 3] = 0
                    } else {
                        rawBytes[offset] = ((argb ushr 16) and 0xFF).toByte()
                        rawBytes[offset + 1] = ((argb ushr 8) and 0xFF).toByte()
                        rawBytes[offset + 2] = (argb and 0xFF).toByte()
                        rawBytes[offset + 3] = a.toByte()
                    }
                    offset += 4
                }
                return rawBytes
            } finally {
                bitmap.recycle()
            }
        }
    }

    private fun buildLayout(
        text: String,
        paint: TextPaint,
        widthPx: Int,
        align: Layout.Alignment,
        maxLines: Int,
        ellipsizeWidthPx: Int,
    ): StaticLayout {
        return StaticLayout.Builder
            .obtain(text, 0, text.length, paint, widthPx.coerceAtLeast(1))
            .setAlignment(align)
            .setTextDirection(TextDirectionHeuristics.FIRSTSTRONG_LTR)
            .setLineSpacing(0f, 1.08f)
            .setIncludePad(false)
            .setMaxLines(maxLines)
            .setEllipsize(TextUtils.TruncateAt.END)
            .setEllipsizedWidth(ellipsizeWidthPx.coerceAtLeast(1))
            .build()
    }
}
