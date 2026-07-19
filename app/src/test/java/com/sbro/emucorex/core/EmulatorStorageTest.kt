package com.sbro.emucorex.core

import org.junit.Assert.assertFalse
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
}
