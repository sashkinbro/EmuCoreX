package com.sbro.emucorex.core

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeNotNull
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class EmulatorDataStorageInstrumentedTest {
    private val context = ApplicationProvider.getApplicationContext<android.content.Context>()

    @Test
    fun internalSelectionUsesPrimaryAppSpecificDirectory() {
        val prepared = requireNotNull(
            EmulatorStorage.prepareStandardDataRoot(context, EmulatorDataLocation.INTERNAL)
        )

        assertNull(prepared.preferencePath)
        assertEquals(
            (context.getExternalFilesDir(null) ?: context.filesDir).absolutePath,
            prepared.directory.absolutePath
        )
        assertRuntimeDirectoriesExist(prepared.directory)
    }

    @Test
    fun sdSelectionUsesAndroidProvidedSecondaryAppDirectoryWhenPresent() {
        val sdCardRoot = EmulatorStorage.sdCardRoot(context)
        assumeNotNull(sdCardRoot)

        val prepared = requireNotNull(
            EmulatorStorage.prepareStandardDataRoot(context, EmulatorDataLocation.SD_CARD)
        )

        assertEquals(sdCardRoot?.absolutePath, prepared.preferencePath)
        assertEquals(sdCardRoot?.absolutePath, prepared.directory.absolutePath)
        assertEquals(
            EmulatorDataLocation.SD_CARD,
            EmulatorStorage.selectedStandardLocation(prepared.preferencePath, sdCardRoot?.absolutePath)
        )
        assertRuntimeDirectoriesExist(prepared.directory)
    }

    private fun assertRuntimeDirectoriesExist(root: java.io.File) {
        listOf("sstates", "memcards", "textures", "cheats", "patches", "logs").forEach { name ->
            val directory = root.resolve(name)
            assertTrue("$name must exist", directory.isDirectory)
            assertTrue("$name must be writable", directory.canWrite())
        }
    }
}
