package com.sbro.emucorex.core

import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class StringsLocalizationTest {
    private val requiredKeys = listOf(
        "hardware_warning_title",
        "hardware_warning_body",
        "hardware_warning_gpu",
        "hardware_warning_recommended",
        "hardware_warning_continue",
        "settings_keyboard_mouse_section",
        "settings_keyboard_input",
        "settings_keyboard_input_desc",
        "settings_keyboard_target_player",
        "settings_keyboard_mapping_title",
        "settings_keyboard_mapping_desc",
        "settings_mouse_input",
        "settings_mouse_input_desc",
        "settings_mouse_target_player",
        "settings_mouse_mapping_title",
        "settings_mouse_mapping_desc",
        "settings_keyboard_mapping_listening_title",
        "settings_keyboard_mapping_listening_desc",
        "settings_mouse_mapping_listening_title",
        "settings_mouse_mapping_listening_desc",
        "settings_keyboard_connected",
        "settings_mouse_connected",
        "settings_mouse_aim_mode",
        "settings_mouse_aim_off",
        "settings_mouse_aim_right_stick",
        "settings_mouse_aim_left_stick",
        "settings_mouse_aim_mode_desc",
        "settings_mouse_sensitivity",
        "settings_mouse_sensitivity_desc",
        "settings_mouse_vertical_sensitivity",
        "settings_mouse_smoothing",
        "settings_mouse_smoothing_desc",
        "settings_mouse_deadzone",
        "settings_mouse_wheel_pulse",
        "settings_mouse_invert_x",
        "settings_mouse_invert_x_desc",
        "settings_mouse_invert_y",
        "settings_mouse_invert_y_desc",
        "settings_mouse_capture",
        "settings_mouse_capture_desc",
        "settings_mouse_left_handed",
        "settings_mouse_left_handed_desc",
        "settings_mouse_wheel",
        "settings_mouse_wheel_off",
        "settings_mouse_wheel_dpad",
        "settings_mouse_wheel_triggers",
        "settings_mouse_wheel_left_stick",
        "settings_mouse_wheel_right_stick",
        "settings_mouse_wheel_shoulders",
        "settings_keyboard_mouse_mapping_reset_title",
        "settings_keyboard_mouse_mapping_reset_desc",
        "settings_keyboard_mouse_global_note",
        "settings_keyboard_mouse_game_desc",
        "settings_keyboard_mouse_use_global",
        "settings_keyboard_mouse_use_global_desc",
        "settings_keyboard_mouse_source_keyboard",
        "settings_keyboard_mouse_source_mouse"
    )

    @Test
    fun `hardware warning strings exist in every locale`() {
        val resDir = resolveResDir()
        val localeDirs = resDir
            .listFiles { file -> file.isDirectory && file.name.startsWith("values") }
            .orEmpty()
            .filter { dir -> File(dir, "strings.xml").isFile }
        assertTrue("Expected at least 18 locale string files", localeDirs.size >= 18)
        localeDirs.forEach { dir ->
            val content = File(dir, "strings.xml").readText()
            requiredKeys.forEach { key ->
                assertTrue("Missing $key in ${dir.name}/strings.xml", content.contains("name=\"$key\""))
            }
        }
    }

    private fun resolveResDir(): File {
        val candidates = listOf(
            File("src/main/res"),
            File("app/src/main/res"),
            File(System.getProperty("user.dir").orEmpty(), "src/main/res"),
            File(System.getProperty("user.dir").orEmpty(), "app/src/main/res")
        )
        return candidates.firstOrNull { it.isDirectory }
            ?: error("Could not locate the Android res directory")
    }
}
