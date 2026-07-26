package com.sbro.emucorex.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

class CustomThemeConfigTest {
    @Test
    fun jsonRoundTripPreservesTheme() {
        val theme = CustomThemeConfig(
            name = "Ocean Blue",
            dark = false,
            primary = 0xFF123456.toInt(),
            largeCornerDp = 34f
        )

        assertEquals(theme.sanitized(), CustomThemeConfig.decode(theme.encode()))
    }

    @Test
    fun sanitizeClampsFieldsAndForcesOpaqueColors() {
        val sanitized = CustomThemeConfig(
            name = "  ",
            primary = 0x00123456,
            smallCornerDp = -10f,
            mediumCornerDp = 100f,
            largeCornerDp = 100f
        ).sanitized()

        assertEquals("My Theme", sanitized.name)
        assertEquals(0xFF123456.toInt(), sanitized.primary)
        assertEquals(CustomThemeConfig.MIN_CORNER_DP, sanitized.smallCornerDp)
        assertEquals(CustomThemeConfig.MAX_MEDIUM_CORNER_DP, sanitized.mediumCornerDp)
        assertEquals(CustomThemeConfig.MAX_LARGE_CORNER_DP, sanitized.largeCornerDp)
    }

    @Test
    fun sanitizeRepairsUnreadableTextColors() {
        val sanitized = CustomThemeConfig(
            primary = 0xFFFFFF00.toInt(),
            onPrimary = 0xFFFFFFFF.toInt(),
            surface = 0xFF101010.toInt(),
            onSurface = 0xFF181818.toInt()
        ).sanitized()

        assertEquals(0xFF101216.toInt(), sanitized.onPrimary)
        assertEquals(0xFFF8F9FF.toInt(), sanitized.onSurface)
    }

    @Test
    fun malformedJsonFallsBackToSafeDefault() {
        assertEquals(CustomThemeConfig.Default, CustomThemeConfig.decode("{broken"))
        assertNotEquals(CustomThemeConfig.Default, CustomThemeConfig(name = "Different"))
    }

    @Test
    fun unknownJsonFieldsRemainForwardCompatible() {
        val decoded = CustomThemeConfig.decode(
            """{"schemaVersion":99,"name":"Future","unknownRole":"value"}"""
        )

        assertEquals("Future", decoded.name)
        assertEquals(CustomThemeConfig.CURRENT_SCHEMA_VERSION, decoded.schemaVersion)
    }

    @Test
    fun themeLibraryRoundTripPreservesMultipleThemesAndActiveSelection() {
        val library = CustomThemeLibrary(
            activeThemeId = "ocean",
            themes = listOf(
                SavedCustomTheme("ocean", CustomThemeConfig(name = "Ocean")),
                SavedCustomTheme("forest", CustomThemeConfig(name = "Forest"))
            )
        )

        val decoded = CustomThemeLibrary.decode(library.encode())
        assertEquals(2, decoded.themes.size)
        assertEquals("Ocean", decoded.activeTheme()?.config?.name)
    }

    @Test
    fun themeLibraryRejectsDuplicateAndInvalidIds() {
        val library = CustomThemeLibrary(
            activeThemeId = "same",
            themes = listOf(
                SavedCustomTheme("same", CustomThemeConfig(name = "First")),
                SavedCustomTheme("same", CustomThemeConfig(name = "Duplicate")),
                SavedCustomTheme(" ", CustomThemeConfig(name = "Invalid"))
            )
        ).sanitized()

        assertEquals(1, library.themes.size)
        assertEquals("First", library.activeTheme()?.config?.name)
    }

    @Test
    fun oldSingleThemeMigratesIntoLibrary() {
        val legacy = CustomThemeConfig(name = "Legacy")
        val migrated = CustomThemeLibrary.decode(raw = null, legacyThemeRaw = legacy.encode())

        assertEquals(1, migrated.themes.size)
        assertEquals("Legacy", migrated.activeTheme()?.config?.name)
    }
}
