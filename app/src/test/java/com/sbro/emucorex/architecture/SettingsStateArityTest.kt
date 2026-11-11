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

    /**
     * dex invoke-direct/range and invoke-static/range address at most 255 register WORDS: a
     * Long/Double parameter takes two, and the synthetic copy$default / constructor variants add
     * the default-mask ints and a marker. A class that passes the parameter-count check can still
     * fail bytecode verification at app launch (VerifyError) when the word count crosses 255.
     */
    @Test
    fun settingsUiStateRegisterWordsStayWellUnderTheDexLimit() = assertRegisterWords(SettingsUiState::class.java)

    @Test
    fun settingsSnapshotRegisterWordsStayWellUnderTheDexLimit() = assertRegisterWords(SettingsSnapshot::class.java)

    @Test
    fun emulationUiStateRegisterWordsStayWellUnderTheDexLimit() = assertRegisterWords(EmulationUiState::class.java)

    @Test
    fun perGameSettingsRegisterWordsStayWellUnderTheDexLimit() = assertRegisterWords(PerGameSettings::class.java)

    private fun registerWords(parameterTypes: Array<Class<*>>, receiver: Boolean): Int =
        (if (receiver) 1 else 0) + parameterTypes.sumOf { if (it == Long::class.javaPrimitiveType || it == Double::class.javaPrimitiveType) 2 else 1 }

    private fun assertRegisterWords(type: Class<*>) {
        val counts = mutableListOf<Pair<String, Int>>()
        type.declaredConstructors.forEach {
            // Constructors are invoked with `this` as the first register word.
            counts += "<init>(${it.parameterCount})" to registerWords(it.parameterTypes, receiver = true)
        }
        type.declaredMethods.filter { it.name == "copy" || it.name == "copy\$default" }.forEach {
            // copy$default is static; its first parameter is already the receiver.
            counts += "${it.name}(${it.parameterCount})" to
                registerWords(it.parameterTypes, receiver = !java.lang.reflect.Modifier.isStatic(it.modifiers))
        }
        val worst = counts.maxByOrNull { it.second }!!
        assertTrue(
            "$type: ${worst.first} needs ${worst.second} register words; keep it at or below " +
                "$REGISTER_WORD_LIMIT (dex hard limit is 255). Group fields into a nested settings value.",
            worst.second <= REGISTER_WORD_LIMIT
        )
    }

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
        const val REGISTER_WORD_LIMIT = 240
    }
}
