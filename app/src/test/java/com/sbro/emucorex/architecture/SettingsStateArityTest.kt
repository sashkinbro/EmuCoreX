package com.sbro.emucorex.architecture

import com.sbro.emucorex.data.PerGameSettings
import com.sbro.emucorex.data.SettingsSnapshot
import com.sbro.emucorex.ui.emulation.EmulationUiState
import com.sbro.emucorex.ui.settings.SettingsUiState
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The big settings states sit close to the JVM's 255-argument method limit: a data class
 * copy() with 255 or more parameters cannot be loaded at all (ClassFormatError).
 *
 * New settings must not simply grow these classes: put them in a nested settings group or
 * expose them as their own StateFlow, as the gyro/light-gun additions already do.
 */
class SettingsStateArityTest {
    @Test
    fun settingsUiStateCopyStaysUnderTheJvmLimit() = assertCopyArity(SettingsUiState::class.java)

    @Test
    fun settingsSnapshotCopyStaysUnderTheJvmLimit() = assertCopyArity(SettingsSnapshot::class.java)

    @Test
    fun emulationUiStateCopyStaysUnderTheJvmLimit() = assertCopyArity(EmulationUiState::class.java)

    @Test
    fun perGameSettingsCopyStaysUnderTheJvmLimit() = assertCopyArity(PerGameSettings::class.java)

    private fun assertCopyArity(type: Class<*>) {
        val copy = type.declaredMethods.firstOrNull { it.name == "copy" && it.parameterCount > 1 }
        assertNotNull("$type has no data-class copy(); did it stop being a data class?", copy)
        assertTrue(
            "$type.copy() takes ${copy!!.parameterCount} parameters; keep it below " +
                "$JVM_METHOD_ARGUMENT_LIMIT or the class stops loading. Move new settings " +
                "into a nested group or a dedicated StateFlow.",
            copy.parameterCount < JVM_METHOD_ARGUMENT_LIMIT
        )
    }

    private companion object {
        const val JVM_METHOD_ARGUMENT_LIMIT = 255
    }
}
