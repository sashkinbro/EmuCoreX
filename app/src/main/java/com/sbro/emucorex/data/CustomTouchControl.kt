package com.sbro.emucorex.data

import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.util.UUID

@Serializable
enum class CustomTouchControlShape {
    CIRCLE,
    ROUNDED,
    SQUARE,
    PILL
}

@Serializable
enum class CustomTouchControlContent {
    SYMBOL,
    TEXT,
    NONE
}

@Serializable
enum class CustomTouchControlPressMode {
    HOLD,
    TOGGLE
}

@Serializable
data class CustomTouchControl(
    val id: String = UUID.randomUUID().toString(),
    val name: String = "Custom button",
    val actionId: String = "cross",
    val secondaryActionId: String? = null,
    val pressMode: CustomTouchControlPressMode = CustomTouchControlPressMode.HOLD,
    val content: CustomTouchControlContent = CustomTouchControlContent.SYMBOL,
    val label: String = "X",
    val shape: CustomTouchControlShape = CustomTouchControlShape.CIRCLE,
    val positionX: Float = 0.72f,
    val positionY: Float = 0.72f,
    val widthDp: Int = 64,
    val heightDp: Int = 64,
    val cornerDp: Int = 18,
    val opacity: Int = 90,
    val fillColor: Int = 0xCC121824.toInt(),
    val contentColor: Int = 0xFFFFFFFF.toInt(),
    val borderColor: Int = 0xFF6688FF.toInt(),
    val borderWidthDp: Float = 1.5f,
    val rotationDegrees: Int = 0,
    val contentScalePercent: Int = 100,
    val shadowElevationDp: Float = 0f,
    val pressedScalePercent: Int = 112,
    val haptics: Boolean = true,
    val enabled: Boolean = true,
    val createdAtMillis: Long = 0L,
    val updatedAtMillis: Long = createdAtMillis
) {
    fun sanitized(): CustomTouchControl? {
        val safeId = id.trim().take(MAX_ID_LENGTH)
        if (safeId.isEmpty()) return null
        val safeCreatedAt = createdAtMillis.coerceAtLeast(0L)
        val safeActionId = actionId.takeIf(ALLOWED_ACTION_IDS::contains) ?: DEFAULT_ACTION_ID
        return copy(
            id = safeId,
            name = name.trim().take(MAX_NAME_LENGTH).ifEmpty { DEFAULT_NAME },
            actionId = safeActionId,
            secondaryActionId = secondaryActionId
                ?.takeIf(ALLOWED_ACTION_IDS::contains)
                ?.takeUnless { it == safeActionId },
            label = label.trim().take(MAX_LABEL_LENGTH).ifEmpty { defaultLabelFor(safeActionId) },
            positionX = positionX.takeIf(Float::isFinite)?.coerceIn(0f, 1f) ?: 0.5f,
            positionY = positionY.takeIf(Float::isFinite)?.coerceIn(0f, 1f) ?: 0.5f,
            widthDp = widthDp.coerceIn(MIN_SIZE_DP, MAX_SIZE_DP),
            heightDp = heightDp.coerceIn(MIN_SIZE_DP, MAX_SIZE_DP),
            cornerDp = cornerDp.coerceIn(0, MAX_CORNER_DP),
            opacity = opacity.coerceIn(MIN_OPACITY, MAX_OPACITY),
            borderWidthDp = borderWidthDp.takeIf(Float::isFinite)
                ?.coerceIn(0f, MAX_BORDER_DP)
                ?: 0f,
            rotationDegrees = rotationDegrees.coerceIn(
                MIN_ROTATION_DEGREES,
                MAX_ROTATION_DEGREES
            ),
            contentScalePercent = contentScalePercent.coerceIn(
                MIN_CONTENT_SCALE_PERCENT,
                MAX_CONTENT_SCALE_PERCENT
            ),
            shadowElevationDp = shadowElevationDp.takeIf(Float::isFinite)
                ?.coerceIn(0f, MAX_SHADOW_DP)
                ?: 0f,
            pressedScalePercent = pressedScalePercent.coerceIn(
                MIN_PRESSED_SCALE_PERCENT,
                MAX_PRESSED_SCALE_PERCENT
            ),
            createdAtMillis = safeCreatedAt,
            updatedAtMillis = updatedAtMillis.coerceAtLeast(safeCreatedAt)
        )
    }

    companion object {
        const val DEFAULT_NAME = "Custom button"
        const val DEFAULT_ACTION_ID = "cross"
        const val MAX_ID_LENGTH = 80
        const val MAX_NAME_LENGTH = 40
        const val MAX_LABEL_LENGTH = 8
        const val MIN_SIZE_DP = 32
        const val MAX_SIZE_DP = 180
        const val MAX_CORNER_DP = 72
        const val MIN_OPACITY = 20
        const val MAX_OPACITY = 100
        const val MAX_BORDER_DP = 6f
        const val MIN_ROTATION_DEGREES = -180
        const val MAX_ROTATION_DEGREES = 180
        const val MIN_CONTENT_SCALE_PERCENT = 60
        const val MAX_CONTENT_SCALE_PERCENT = 180
        const val MAX_SHADOW_DP = 16f
        const val MIN_PRESSED_SCALE_PERCENT = 85
        const val MAX_PRESSED_SCALE_PERCENT = 140

        val ALLOWED_ACTION_IDS = setOf(
            "up", "down", "left", "right",
            "triangle", "cross", "square", "circle",
            "l1", "l2", "r1", "r2", "l3", "r3",
            "select", "start", "pressure"
        )

        fun defaultLabelFor(actionId: String): String = when (actionId) {
            "up" -> "↑"
            "down" -> "↓"
            "left" -> "←"
            "right" -> "→"
            "triangle" -> "△"
            "cross" -> "×"
            "square" -> "□"
            "circle" -> "○"
            "l1", "l2", "r1", "r2", "l3", "r3" -> actionId.uppercase()
            "select" -> "SEL"
            "start" -> "START"
            "pressure" -> "P"
            else -> "X"
        }
    }
}

@Serializable
data class CustomTouchControlLibrary(
    val schemaVersion: Int = CURRENT_SCHEMA_VERSION,
    val controls: List<CustomTouchControl> = emptyList()
) {
    fun sanitized(): CustomTouchControlLibrary {
        val unique = LinkedHashMap<String, CustomTouchControl>()
        controls.forEach { item ->
            item.sanitized()?.let { safe ->
                if (safe.id !in unique && unique.size < MAX_CONTROLS) {
                    unique[safe.id] = safe
                }
            }
        }
        return copy(
            schemaVersion = CURRENT_SCHEMA_VERSION,
            controls = unique.values.toList()
        )
    }

    fun encode(): String = JSON.encodeToString(serializer(), sanitized())

    companion object {
        const val CURRENT_SCHEMA_VERSION = 2
        const val MAX_CONTROLS = 32
        val Empty = CustomTouchControlLibrary()

        fun decode(raw: String?): CustomTouchControlLibrary {
            if (raw.isNullOrBlank()) return Empty
            return runCatching {
                JSON.decodeFromString(serializer(), raw).sanitized()
            }.getOrDefault(Empty)
        }

        private val JSON = Json {
            ignoreUnknownKeys = true
            encodeDefaults = true
        }
    }
}
