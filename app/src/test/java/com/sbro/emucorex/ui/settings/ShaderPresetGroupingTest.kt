package com.sbro.emucorex.ui.settings

import com.sbro.emucorex.data.RetroArchShaderPreset
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ShaderPresetGroupingTest {
    private val presets = listOf(
        RetroArchShaderPreset("crt/crt-royale", "/shaders/crt-royale.slangp"),
        RetroArchShaderPreset("crt/crt-easymode", "/shaders/crt-easymode.slangp"),
        RetroArchShaderPreset("anti-aliasing/fxaa", "/shaders/fxaa.slangp"),
        RetroArchShaderPreset("stock", "/shaders/stock.slangp")
    )

    @Test
    fun groupsSpecialAndRootPresetsBeforeFolderCategories() {
        val groups = groups()

        assertEquals("General", groups.first().title)
        assertEquals(listOf("global", "", "/shaders/stock.slangp"), groups.first().options.map { it.first })
        assertEquals(listOf("Anti Aliasing", "CRT"), groups.drop(1).map { it.title })
        assertEquals(2, groups.last().options.size)
    }

    @Test
    fun categorySearchKeepsEveryPresetInMatchingCategory() {
        val groups = groups(query = "crt")

        assertEquals(listOf("CRT"), groups.map { it.title })
        assertEquals(2, groups.single().options.size)
    }

    @Test
    fun presetSearchKeepsOnlyMatchingEntries() {
        val groups = groups(query = "easymode")

        assertEquals(listOf("CRT"), groups.map { it.title })
        assertEquals(listOf("crt/crt-easymode"), groups.single().options.map { it.second })
    }

    @Test
    fun technicalCategoryNamesAreReadable() {
        assertEquals("Anti Aliasing", shaderPresetCategoryTitle("anti-aliasing"))
        assertEquals("NTSC Filters", shaderPresetCategoryTitle("ntsc_filters"))
        assertTrue(shaderPresetCategoryTitle("handheld").startsWith("Handheld"))
    }

    private fun groups(query: String = "") = buildShaderPresetDialogGroups(
        presets = presets,
        leadingOptions = listOf("global" to "Default for all games"),
        noneLabel = "None",
        generalLabel = "General",
        query = query
    )
}
