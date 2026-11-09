package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Test

class GSAspectRatioSettingsTest {
    @Test fun persistedIdsAndFallbackMatchNativeNames() {
        // Reflection lets the tests-only baseline compile before the production consumer exists.
        val clazz = Class.forName("com.sbro.emucorex.core.GSAspectRatioSettings")
        val instance = clazz.getField("INSTANCE").get(null)
        val normalize = clazz.getMethod("normalize", Integer::class.java)
        @Suppress("UNCHECKED_CAST")
        val names = clazz.getMethod("getSettingValues").invoke(instance) as Map<Int, String>
        val expected = listOf("Stretch", "Auto 4:3/3:2", "4:3", "16:9", "10:7", "21:9", "20:9", "19.5:9", "Custom")
        expected.forEachIndexed { id, value ->
            assertEquals(id, normalize.invoke(instance, id))
            assertEquals(value, names[id])
        }
        listOf(-1, 9, Int.MAX_VALUE).forEach { assertEquals(1, normalize.invoke(instance, it)) }
        assertEquals(1, normalize.invoke(instance, null))
    }
}
