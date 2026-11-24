package com.sbro.emucorex.data.store

import com.sbro.emucorex.ui.store.StoreAccessFilter
import com.sbro.emucorex.ui.store.StoreSort
import com.sbro.emucorex.ui.store.filterStoreItems
import kotlinx.serialization.json.Json
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StoreCatalogDecodingTest {
    private val json = Json {
        ignoreUnknownKeys = true
        explicitNulls = false
    }

    private fun item(id: String, kind: String, tags: String = "[]") = """
        {"id":"$id","kind":"$kind","version":1,"tags":$tags,
         "cover":{"path":"c/$id.webp","sha256":"00","bytes":1},
         "packageFile":{"path":"p/$id.pkg","sha256":"00","bytes":1}}
    """.trimIndent()

    @Test
    fun unknownKindsAreSkippedInsteadOfBreakingTheCatalog() {
        val catalog = json.decodeFromString<StoreCatalog>(
            """
            {"formatVersion":1,"releaseId":"r1",
             "categories":[{"id":"a","kind":"theme"},{"id":"b","kind":"hologram"},{"id":"c","kind":"font"}],
             "items":[${item("one-theme", "theme")},${item("future-thing", "hologram")},${item("nice-font", "font")}]}
            """.trimIndent()
        )

        assertEquals(listOf("a", "c"), catalog.categories.map { it.id })
        assertEquals(listOf("one-theme", "nice-font"), catalog.items.map { it.id })
        assertEquals(StoreKind.FONT, catalog.items.last().kind)
    }

    @Test
    fun catalogWithoutUnknownKindsRoundTrips() {
        val source = StoreCatalog(1, "r", emptyList(), emptyList())
        val encoded = json.encodeToString(StoreCatalog.serializer(), source)
        assertEquals(source, json.decodeFromString<StoreCatalog>(encoded))
    }

    @Test
    fun scriptAndContentTagsAreParsed() {
        val catalog = json.decodeFromString<StoreCatalog>(
            """{"formatVersion":1,"releaseId":"r","items":[
                ${item("f", "font", """["script:latin","script:Cyrillic","other"]""")},
                ${item("s", "sounds", """["contains:music","contains:achievement"]""")}]}"""
        )
        assertEquals(listOf("latin", "cyrillic"), catalog.items[0].scripts())
        assertEquals(listOf("music", "achievement"), catalog.items[1].contents())
    }

    @Test
    fun languageFilterMatchesTheScriptOfTheAppLanguage() {
        val catalog = json.decodeFromString<StoreCatalog>(
            """{"formatVersion":1,"releaseId":"r","items":[
                ${item("latin-only", "font", """["script:latin"]""")},
                ${item("cyr-font", "font", """["script:latin","script:cyrillic"]""")}]}"""
        )
        assertEquals("cyrillic", storeScriptForLanguage("uk"))
        assertEquals("cjk", storeScriptForLanguage("zh-CN"))
        assertEquals("latin", storeScriptForLanguage("de"))

        val result = filterStoreItems(
            catalog.items, StoreKind.FONT, StoreAccessFilter.LANGUAGE, null, "", StoreSort.NAME,
            emptySet(), "uk", storeScriptForLanguage("uk")
        )
        assertEquals(listOf("cyr-font"), result.map { it.id })
        assertTrue(
            filterStoreItems(
                catalog.items, StoreKind.FONT, StoreAccessFilter.LANGUAGE, null, "", StoreSort.NAME,
                emptySet(), "en", null
            ).isEmpty()
        )
    }
}
