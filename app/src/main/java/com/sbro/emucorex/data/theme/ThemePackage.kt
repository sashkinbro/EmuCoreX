package com.sbro.emucorex.data.theme

import com.sbro.emucorex.data.CustomThemeConfig
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

/** Parsed `theme.json` of an `.ecxtheme` package. See docs/themes/ecxtheme-format.md. */
@Serializable
data class ThemePackageSpec(
    val formatVersion: Int = 0,
    val id: String = "",
    val version: Int = 1,
    val minAppVersionCode: Int = 0,
    val requiresPro: Boolean = false,
    val author: String = "",
    val name: Map<String, String> = emptyMap(),
    val colors: Map<String, String> = emptyMap(),
    val dark: Boolean? = null,
    val accents: List<String> = emptyList(),
    val semantic: Map<String, String> = emptyMap(),
    val shape: ShapeSpec = ShapeSpec(),
    val typography: TypographySpec = TypographySpec(),
    val effects: EffectsSpec = EffectsSpec(),
    val background: BackgroundSpec = BackgroundSpec()
) {
    @Serializable
    data class ShapeSpec(val style: String = "rounded", val scale: Float = 1f)

    @Serializable
    data class TypographySpec(val font: String? = null)

    @Serializable
    data class EffectsSpec(
        val decor: String = "none",
        val style: String = "standard",
        val scanlines: Boolean = false
    )

    @Serializable
    data class BackgroundSpec(val dim: Float = 0.35f)

    fun displayName(locale: String): String {
        val language = locale.substringBefore('-').substringBefore('_').lowercase()
        return (name[language] ?: name["en"] ?: name.values.firstOrNull() ?: id)
            .trim()
            .take(CustomThemeConfig.MAX_NAME_LENGTH)
    }

    companion object {
        const val SUPPORTED_FORMAT_VERSION = 1
        private val ID_PATTERN = Regex("[a-z0-9-]{3,48}")
        private val REQUIRED_COLORS = listOf(
            "primary", "onPrimary", "secondary", "tertiary", "background", "surface",
            "surfaceVariant", "onBackground", "onSurface", "onSurfaceVariant", "outline", "error"
        )

        private val JSON = Json {
            ignoreUnknownKeys = true
            isLenient = false
        }

        fun decode(raw: String): ThemePackageSpec {
            val spec = JSON.decodeFromString(serializer(), raw)
            require(spec.formatVersion >= 1) { "Missing theme formatVersion" }
            require(ID_PATTERN.matches(spec.id)) { "Invalid theme id" }
            require(spec.version >= 1) { "Invalid theme version" }
            require(!spec.name["en"].isNullOrBlank()) { "Theme name.en is required" }
            REQUIRED_COLORS.forEach { key ->
                require(parseHexColor(spec.colors[key]) != null) { "Theme color $key is missing or invalid" }
            }
            return spec
        }

        fun parseHexColor(value: String?): Int? {
            val hex = value?.trim()?.removePrefix("#") ?: return null
            if (hex.length != 6) return null
            return hex.toLongOrNull(16)?.let { (it or 0xFF000000L).toInt() }
        }
    }
}

enum class ThemeShapeStyle { ROUNDED, CUT, CUT_DIAGONAL, SQUARE }

enum class ThemeDecor { NONE, NEON }

/** Whole-UI drawing style (panels, buttons, headers). Each one is implemented in ui/theme/style. */
enum class UiStyle(val wire: String) {
    STANDARD("standard"), NEON("neon"), CLASSIC("classic"), LUNA("luna"), S60("s60"), AERO("aero"),
    METRO("metro"), OSD("osd"), PIXEL("pixel"), TERMINAL("terminal"), PAPER("paper");

    companion object {
        fun fromWire(value: String?): UiStyle = entries.firstOrNull { it.wire.equals(value, ignoreCase = true) } ?: STANDARD
    }
}

enum class ThemeFont { SYSTEM, RUBIK, EXO2, MONOSPACE, SERIF, PACKAGE }

/** Theme properties beyond the Material color scheme, as plain values (no Compose types). */
data class ThemeStyleSpec(
    val shapeStyle: ThemeShapeStyle = ThemeShapeStyle.ROUNDED,
    val shapeScale: Float = 1f,
    val decor: ThemeDecor = ThemeDecor.NONE,
    val uiStyle: UiStyle = UiStyle.STANDARD,
    val accents: List<Int> = emptyList(),
    val success: Int? = null,
    val warning: Int? = null,
    val info: Int? = null,
    val gold: Int? = null,
    val font: ThemeFont? = null,
    val scanlines: Boolean = false,
    val backgroundDim: Float = 0.35f
)

fun ThemePackageSpec.toCustomThemeConfig(locale: String): CustomThemeConfig {
    fun color(key: String): Int? = ThemePackageSpec.parseHexColor(colors[key])
    val base = CustomThemeConfig(
        name = displayName(locale),
        dark = dark ?: isDarkColor(color("background")!!),
        primary = color("primary")!!,
        onPrimary = color("onPrimary")!!,
        secondary = color("secondary")!!,
        tertiary = color("tertiary")!!,
        background = color("background")!!,
        surface = color("surface")!!,
        surfaceVariant = color("surfaceVariant")!!,
        onBackground = color("onBackground")!!,
        onSurface = color("onSurface")!!,
        onSurfaceVariant = color("onSurfaceVariant")!!,
        outline = color("outline")!!,
        error = color("error")!!
    )
    return base.copy(
        primaryContainer = color("primaryContainer") ?: base.primaryContainer,
        onPrimaryContainer = color("onPrimaryContainer") ?: base.onPrimaryContainer,
        onSecondary = color("onSecondary") ?: base.onSecondary,
        secondaryContainer = color("secondaryContainer") ?: base.secondaryContainer,
        onSecondaryContainer = color("onSecondaryContainer") ?: base.onSecondaryContainer,
        onTertiary = color("onTertiary") ?: base.onTertiary,
        tertiaryContainer = color("tertiaryContainer") ?: base.tertiaryContainer,
        onTertiaryContainer = color("onTertiaryContainer") ?: base.onTertiaryContainer,
        outlineVariant = color("outlineVariant") ?: base.outlineVariant,
        onError = color("onError") ?: base.onError,
        errorContainer = color("errorContainer") ?: base.errorContainer,
        onErrorContainer = color("onErrorContainer") ?: base.onErrorContainer
    ).sanitized()
}

private fun isDarkColor(argb: Int): Boolean {
    val r = (argb ushr 16) and 0xFF
    val g = (argb ushr 8) and 0xFF
    val b = argb and 0xFF
    return (0.299 * r + 0.587 * g + 0.114 * b) < 128
}

fun ThemePackageSpec.toStyleSpec(): ThemeStyleSpec = ThemeStyleSpec(
    shapeStyle = when (shape.style.lowercase()) {
        "cut" -> ThemeShapeStyle.CUT
        "cutdiagonal" -> ThemeShapeStyle.CUT_DIAGONAL
        "square" -> ThemeShapeStyle.SQUARE
        else -> ThemeShapeStyle.ROUNDED
    },
    shapeScale = shape.scale.takeIf { it.isFinite() }?.coerceIn(0f, 2f) ?: 1f,
    decor = if (effects.decor.equals("neon", ignoreCase = true)) ThemeDecor.NEON else ThemeDecor.NONE,
    uiStyle = UiStyle.fromWire(effects.style),
    accents = accents.mapNotNull(ThemePackageSpec::parseHexColor).take(3),
    success = ThemePackageSpec.parseHexColor(semantic["success"]),
    warning = ThemePackageSpec.parseHexColor(semantic["warning"]),
    info = ThemePackageSpec.parseHexColor(semantic["info"]),
    gold = ThemePackageSpec.parseHexColor(semantic["gold"]),
    font = when (typography.font?.lowercase()) {
        "system" -> ThemeFont.SYSTEM
        "rubik" -> ThemeFont.RUBIK
        "exo2" -> ThemeFont.EXO2
        "monospace" -> ThemeFont.MONOSPACE
        "serif" -> ThemeFont.SERIF
        "package" -> ThemeFont.PACKAGE
        else -> null
    },
    scanlines = effects.scanlines,
    backgroundDim = background.dim.takeIf { it.isFinite() }?.coerceIn(0f, 0.9f) ?: 0.35f
)
