package com.sbro.emucorex.data

private const val GAMEPAD_BINDINGS_KEY = "gamepadBindingsByPad"
private const val KEYBOARD_MOUSE_KEY = "keyboardMouse"

/** Updates only controller bindings, retaining every other per-game override. */
fun PerGameSettings.withGamepadBindingsByPad(
    bindingsByPad: Map<Int, Map<String, Int>>
): PerGameSettings {
    val updatedKeys = providedKeys?.let { keys ->
        if (bindingsByPad.isEmpty()) keys - GAMEPAD_BINDINGS_KEY else keys + GAMEPAD_BINDINGS_KEY
    }
    return copy(gamepadBindingsByPad = bindingsByPad, providedKeys = updatedKeys)
}

/** Updates only keyboard/mouse settings, retaining every other per-game override. */
fun PerGameSettings.withKeyboardMouseSettings(
    settings: KeyboardMouseSettings?
): PerGameSettings {
    val updatedKeys = providedKeys?.let { keys ->
        if (settings == null) keys - KEYBOARD_MOUSE_KEY else keys + KEYBOARD_MOUSE_KEY
    }
    return copy(keyboardMouse = settings, providedKeys = updatedKeys)
}
