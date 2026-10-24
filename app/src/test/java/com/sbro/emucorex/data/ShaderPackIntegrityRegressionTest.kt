package com.sbro.emucorex.data

import java.nio.file.Files
import org.junit.Assert.assertFalse
import org.junit.Test

class ShaderPackIntegrityRegressionTest {
    @Test fun aPresetWithoutItsShaderMustNotMarkAnInstallationUsable() {
        val directory = Files.createTempDirectory("shader-integrity-").toFile()
        try {
            directory.resolve("broken.slangp").writeText("shaders = 1\nshader0 = missing.slang\n")
            assertFalse(RetroArchShaderRepository.containsPresetFiles(directory))
        } finally {
            directory.deleteRecursively()
        }
    }
}
