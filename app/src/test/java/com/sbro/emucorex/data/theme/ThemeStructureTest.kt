package com.sbro.emucorex.data.theme

import androidx.compose.ui.graphics.Color
import com.sbro.emucorex.ui.theme.toThemeExtras
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** Parses the sample packages in src/test/resources/themes and checks the structural fields. */
class ThemeStructureTest {
    private fun load(name: String): ThemePackageSpec {
        val raw = checkNotNull(javaClass.classLoader?.getResourceAsStream("themes/$name.ecxtheme.json")) { name }
            .bufferedReader().use { it.readText() }
        return ThemePackageSpec.decode(raw)
    }

    @Test
    fun legacyPackageKeepsEveryStructuralDefault() {
        val style = load("legacy-plain").toStyleSpec()
        assertEquals(ThemeStyleSpec(), style)
        assertEquals(CardVariant.DEFAULT, style.card)
        assertEquals(ButtonVariant.DEFAULT, style.button)
        assertEquals(IconContainer.DEFAULT, style.iconContainer)
        assertNull(style.borderWidthDp)
    }

    @Test
    fun terminalBrutalMapsEveryField() {
        val style = load("terminal-brutal").toStyleSpec()
        assertEquals(UiStyle.TERMINAL, style.uiStyle)
        assertEquals(ThemeFont.SPACE_MONO, style.font)
        assertEquals(TitleCase.UPPER, style.titleCase)
        assertEquals(1.5f, style.titleLetterSpacingSp)
        assertEquals(CardVariant.DOUBLE, style.card)
        assertEquals(ButtonVariant.BRACKET, style.button)
        assertEquals(ChipVariant.SQUARE, style.chip)
        assertEquals(SwitchVariant.CHECKBOX, style.switch)
        assertEquals(HeaderVariant.UNDERLINE, style.header)
        assertEquals(1f, style.borderWidthDp)
        assertEquals(AccentEdge.LEFT, style.accentEdge)
        assertEquals(BackgroundPattern.GRID, style.pattern)
        assertEquals(IconContainer.SQUARE, style.iconContainer)
        val extras = style.toThemeExtras(Color.Red, Color.Green, Color.Blue, null)
        assertEquals(CardVariant.DOUBLE, extras.card)
        assertEquals(BackgroundPattern.GRID, extras.pattern)
        assertEquals(0.5f, extras.backgroundDim)
    }

    @Test
    fun candySoftMapsLargeTitleAndLed() {
        val style = load("candy-soft").toStyleSpec()
        assertEquals(HeaderVariant.LARGE_TITLE, style.header)
        assertEquals(SwitchVariant.LED, style.switch)
        assertEquals(ButtonVariant.GEL, style.button)
        assertEquals(ThemeFont.LORA, style.font)
        assertEquals(AccentEdge.BOTTOM, style.accentEdge)
        assertEquals(IconContainer.CIRCLE, style.iconContainer)
    }

    @Test
    fun unknownValuesFallBackToDefaultsAndOutOfRangeIsClamped() {
        val style = load("unknown-values").toStyleSpec()
        assertNull(style.font)
        assertEquals(TitleCase.NONE, style.titleCase)
        assertEquals(6f, style.titleLetterSpacingSp)
        assertEquals(CardVariant.DEFAULT, style.card)
        assertEquals(ButtonVariant.DEFAULT, style.button)
        assertEquals(3f, style.borderWidthDp)
        assertEquals(AccentEdge.NONE, style.accentEdge)
        assertEquals(BackgroundPattern.NONE, style.pattern)
        assertEquals(IconContainer.DEFAULT, style.iconContainer)
    }
}
