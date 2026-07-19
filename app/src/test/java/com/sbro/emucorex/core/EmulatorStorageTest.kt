package com.sbro.emucorex.core

import org.junit.Assert.assertFalse
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class EmulatorStorageTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test
    fun customRootIsPreparedOnceAndRemainsReusable() {
        val root = temporaryFolder.newFolder("emulator-data")

        assertTrue(EmulatorStorage.prepareCustomDataRoot(root.absolutePath))
        assertTrue(EmulatorStorage.prepareCustomDataRoot(root.absolutePath))
        listOf("sstates", "memcards", "textures", "cheats", "patches", "logs").forEach { name ->
            assertTrue(root.resolve(name).isDirectory)
        }
        assertFalse(root.resolve(".emucorex-write-test").exists())
    }

    @Test
    fun regularFileCannotBeUsedAsCustomRoot() {
        val file = temporaryFolder.newFile("not-a-directory")

        assertFalse(EmulatorStorage.prepareCustomDataRoot(file.absolutePath))
    }

    @Test
    fun secondaryStorageSkipsPrimaryAndMissingVolumes() {
        val primary = temporaryFolder.newFolder("primary")
        val sdCard = temporaryFolder.newFolder("sd-card")

        assertEquals(
            sdCard,
            EmulatorStorage.findSecondaryExternalFilesDir(arrayOf(primary, null, sdCard))
        )
    }

    @Test
    fun secondaryStorageIsAbsentWhenAndroidOnlyReturnsPrimary() {
        val primary = temporaryFolder.newFolder("primary-only")

        assertNull(EmulatorStorage.findSecondaryExternalFilesDir(arrayOf(primary)))
        assertNull(EmulatorStorage.findSecondaryExternalFilesDir(arrayOf(primary, null)))
    }

    @Test
    fun standardLocationRecognizesInternalSdAndLegacyPaths() {
        val sdCardPath = temporaryFolder.newFolder("selected-sd").absolutePath
        val legacyPath = temporaryFolder.newFolder("legacy-custom").absolutePath

        assertEquals(
            EmulatorDataLocation.INTERNAL,
            EmulatorStorage.selectedStandardLocation(null, sdCardPath)
        )
        assertEquals(
            EmulatorDataLocation.SD_CARD,
            EmulatorStorage.selectedStandardLocation(sdCardPath, sdCardPath)
        )
        assertNull(EmulatorStorage.selectedStandardLocation(legacyPath, sdCardPath))
        assertNull(EmulatorStorage.selectedStandardLocation(sdCardPath, null))
    }
}
