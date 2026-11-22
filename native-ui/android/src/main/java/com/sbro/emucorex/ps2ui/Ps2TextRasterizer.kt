package com.sbro.emucorex.ps2ui

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Shader
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
 * High-DPI shaped text rasterizer for the native GLES 3.0 PlayStation 2 BIOS UI.
 * Uses Exo 2 geometric typography (when provided by host) with automatic system
 * fallback via Android StaticLayout for all 18 scripts (Arabic, Persian RTL,
 * Hindi Devanagari, CJK, Cyrillic, Latin).
 */
object Ps2TextRasterizer {
    const val STYLE_HEADER = 0
    const val STYLE_MENU = 1
    const val STYLE_BODY = 2
    const val STYLE_BODY_BOLD = 3
    const val STYLE_MONO = 4
    const val STYLE_PS2_LOGO = 5

    const val ALIGN_START = 0
    const val ALIGN_CENTER = 1
    const val ALIGN_END = 2

    private val lock = Any()

    @Volatile
    private var customLight: Typeface? = null

    @Volatile
    private var customRegular: Typeface? = null

    @Volatile
    private var customBold: Typeface? = null

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

    fun setCustomTypefaces(light: Typeface?, regular: Typeface?, bold: Typeface?) {
        synchronized(lock) {
            customLight = light
            customRegular = regular
            customBold = bold
        }
    }

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
        val pxFontSize = (virtualFontSize * scale).coerceIn(10f, 260f)
        val clampedMaxLines = maxLines.coerceIn(1, 16)
        val hasMaxWidth = maxVirtualWidth > 1f
        val pxMaxWidth = if (hasMaxWidth) {
            (maxVirtualWidth * scale).roundToInt().coerceIn(8, 3072)
        } else {
            3072
        }

        synchronized(lock) {
            val typeface = when (style) {
                STYLE_PS2_LOGO -> customBold ?: mediumTypeface
                STYLE_HEADER, STYLE_MENU -> customLight ?: lightTypeface
                STYLE_BODY_BOLD -> customBold ?: mediumTypeface
                STYLE_MONO -> monoRegular
                else -> customRegular ?: sansRegular
            }
            mainPaint.shader = null
            mainPaint.clearShadowLayer()
            mainPaint.style = Paint.Style.FILL
            mainPaint.color = Color.WHITE
            mainPaint.typeface = typeface
            mainPaint.textSize = pxFontSize
            mainPaint.textScaleX = when (style) {
                STYLE_PS2_LOGO -> 1.08f
                STYLE_MENU, STYLE_HEADER -> 1.10f
                STYLE_BODY, STYLE_BODY_BOLD -> 1.04f
                else -> 1.00f
            }
            mainPaint.letterSpacing = when (style) {
                STYLE_PS2_LOGO -> 0.08f
                STYLE_MENU, STYLE_HEADER -> 0.045f
                STYLE_MONO -> 0.02f
                else -> 0.01f
            }

            val isLogo = (style == STYLE_PS2_LOGO)
            val shadowOffset = when (style) {
                STYLE_PS2_LOGO -> 0f
                STYLE_MENU, STYLE_HEADER -> max(1.4f, pxFontSize * 0.055f)
                else -> max(1.0f, pxFontSize * 0.04f)
            }
            val glowMargin = if (isLogo) ceil(pxFontSize * 0.24f).toInt() else 0
            val padLeft = if (isLogo) glowMargin else ceil(scale * 1.2f).toInt()
            val padRight = if (isLogo) glowMargin else ceil(shadowOffset + scale * 1.8f).toInt()
            val padTop = if (isLogo) glowMargin else ceil(scale * 1.2f).toInt()
            val padBottom = if (isLogo) glowMargin else ceil(shadowOffset + scale * 1.6f).toInt()

            val isSingleLine = clampedMaxLines == 1 && text.indexOf('\n') < 0
            val rawW = max(mainPaint.measureText(text), StaticLayout.getDesiredWidth(text, mainPaint))
            val desiredPx = ceil(rawW + 4f).toInt().coerceIn(1, 3000)

            val effectiveAvailPx = if (hasMaxWidth) {
                (pxMaxWidth - padLeft - padRight).coerceAtLeast(8)
            } else {
                desiredPx.coerceAtLeast(4)
            }
            val allowEllipsize = hasMaxWidth && desiredPx > effectiveAvailPx

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

            val mainLayout = buildLayout(
                text, mainPaint, layoutWidth, layoutAlign, clampedMaxLines, effectiveAvailPx, allowEllipsize
            )
            var measuredContentW = 0f
            for (i in 0 until mainLayout.lineCount) {
                measuredContentW = max(measuredContentW, mainLayout.getLineWidth(i))
            }
            val finalLayoutWidth = if (!isSingleLine && hasMaxWidth && align != ALIGN_START) {
                effectiveAvailPx
            } else if (allowEllipsize) {
                ceil(measuredContentW).toInt().coerceIn(1, effectiveAvailPx)
            } else {
                ceil(measuredContentW + 2f).toInt().coerceIn(1, effectiveAvailPx)
            }
            val finalMainLayout = if (finalLayoutWidth != layoutWidth) {
                buildLayout(
                    text, mainPaint, finalLayoutWidth, layoutAlign, clampedMaxLines, finalLayoutWidth, allowEllipsize
                )
            } else {
                mainLayout
            }

            val texW = (finalMainLayout.width + padLeft + padRight).coerceIn(2, 3200)
            val texH = (finalMainLayout.height + padTop + padBottom).coerceIn(2, 2048)

            val bitmap = Bitmap.createBitmap(texW, texH, Bitmap.Config.ARGB_8888)
            try {
                val canvas = Canvas(bitmap)

                if (isLogo) {
                    // Tight pure-black backing contrast so hero EmuCoreX pops cleanly over crystals & mist
                    val logoShadow = max(1.2f, pxFontSize * 0.04f)
                    mainPaint.style = Paint.Style.FILL
                    mainPaint.shader = null
                    mainPaint.clearShadowLayer()
                    mainPaint.color = Color.argb(215, 0, 0, 0)
                    canvas.save()
                    canvas.translate(padLeft + logoShadow, padTop + logoShadow)
                    finalMainLayout.draw(canvas)
                    canvas.restore()

                    // Iconic PS2 futuristic metallic silver-white vertical sheen + tight electric-blue edge glow
                    mainPaint.color = Color.WHITE
                    mainPaint.setShadowLayer(
                        max(1.4f, pxFontSize * 0.08f),
                        0f,
                        0f,
                        Color.argb(155, 52, 165, 255)
                    )
                    mainPaint.shader = LinearGradient(
                        0f,
                        0f,
                        0f,
                        finalMainLayout.height.toFloat().coerceAtLeast(16f),
                        intArrayOf(
                            Color.WHITE,
                            Color.rgb(210, 232, 255),
                            Color.rgb(248, 252, 255)
                        ),
                        floatArrayOf(0f, 0.55f, 1f),
                        Shader.TileMode.CLAMP
                    )
                    canvas.save()
                    canvas.translate(padLeft.toFloat(), padTop.toFloat())
                    finalMainLayout.draw(canvas)
                    canvas.restore()
                } else {
                    // Tight pure-black drop shadow for maximum clarity on pitch-black #000000 and mist
                    mainPaint.style = Paint.Style.FILL
                    mainPaint.color = when (style) {
                        STYLE_MENU, STYLE_HEADER -> Color.argb(235, 0, 0, 0)
                        else -> Color.argb(200, 0, 0, 0)
                    }
                    canvas.save()
                    canvas.translate(padLeft + shadowOffset, padTop + shadowOffset)
                    finalMainLayout.draw(canvas)
                    canvas.restore()

                    // Crisp pure-white foreground glyphs (no fuzzy stroke outline)
                    mainPaint.style = Paint.Style.FILL
                    mainPaint.color = Color.WHITE
                    canvas.save()
                    canvas.translate(padLeft.toFloat(), padTop.toFloat())
                    finalMainLayout.draw(canvas)
                    canvas.restore()
                }

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
                mainPaint.shader = null
                mainPaint.clearShadowLayer()
                mainPaint.textScaleX = 1.0f
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
        allowEllipsize: Boolean,
    ): StaticLayout {
        val builder = StaticLayout.Builder
            .obtain(text, 0, text.length, paint, widthPx.coerceAtLeast(1))
            .setAlignment(align)
            .setTextDirection(TextDirectionHeuristics.FIRSTSTRONG_LTR)
            .setLineSpacing(0f, 1.08f)
            .setIncludePad(false)
            .setMaxLines(maxLines)
        if (allowEllipsize) {
            builder
                .setEllipsize(TextUtils.TruncateAt.END)
                .setEllipsizedWidth(ellipsizeWidthPx.coerceAtLeast(1))
        }
        return builder.build()
    }
}
