package com.sbro.emucorex.data

import java.io.File
import kotlin.io.path.createTempDirectory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RetroArchShaderRepositoryTest {
    @Test
    fun shaderPackProgressReportsKnownFraction() {
        val progress = ShaderPackInstallProgress(
            stage = ShaderPackInstallStage.DOWNLOADING,
            downloadedBytes = 25L,
            totalBytes = 100L
        )

        assertEquals(0.25f, progress.fraction!!, 0.001f)
    }

    @Test
    fun shaderPackProgressIsIndeterminateWhenSizeIsUnknown() {
        val progress = ShaderPackInstallProgress(
            stage = ShaderPackInstallStage.DOWNLOADING,
            downloadedBytes = 25L
        )

        assertNull(progress.fraction)
    }

    @Test
    fun shaderPackProgressFractionIsClamped() {
        val progress = ShaderPackInstallProgress(
            stage = ShaderPackInstallStage.DOWNLOADING,
            downloadedBytes = 125L,
            totalBytes = 100L
        )

        assertEquals(1f, progress.fraction!!, 0.001f)
    }

    @Test
    fun commonArchiveRootStripsGitHubWrapperOnlyWhenShared() {
        assertEquals(
            "slang-shaders-master/",
            RetroArchShaderRepository.commonArchiveRoot(
                listOf("slang-shaders-master/crt/a.slangp", "slang-shaders-master/crt/a.slang")
            )
        )
        assertEquals(
            "",
            RetroArchShaderRepository.commonArchiveRoot(listOf("crt/a.slangp", "handheld/b.slangp"))
        )
    }

    @Test
    fun archiveEntryCannotEscapeDestination() {
        val root = createTempDirectory("shader-root").toFile()
        try {
            assertTrue(RetroArchShaderRepository.isSafeArchiveEntry(root, "crt/preset.slangp"))
            assertFalse(RetroArchShaderRepository.isSafeArchiveEntry(root, "../outside.slangp"))
            assertFalse(RetroArchShaderRepository.isSafeArchiveEntry(root, "crt/../../outside.slang"))
        } finally {
            root.deleteRecursively()
            File(root.parentFile, "outside.slangp").delete()
            File(root.parentFile, "outside.slang").delete()
        }
    }

    @Test
    fun installedPackRequiresAtLeastOnePresetFile() {
        val root = createTempDirectory("shader-pack-state").toFile()
        try {
            assertFalse(RetroArchShaderRepository.containsPresetFiles(root))
            File(root, "crt").mkdirs()
            File(root, "crt/pass.slang").writeText("shader")
            assertFalse(RetroArchShaderRepository.containsPresetFiles(root))
            File(root, "crt/preset.SLANGP").writeText("preset")
            assertTrue(RetroArchShaderRepository.containsPresetFiles(root))
        } finally {
            root.deleteRecursively()
        }
    }
}
