// SPDX-License-Identifier: GPL-3.0+

#include "Input/SDLInputSource.h"

#include "emucorex/android_runtime.h"

#include "common/SettingsInterface.h"
#include "common/StringUtil.h"

#include <android/log.h>

#include <algorithm>

namespace
{
constexpr const char* LOG_TAG = "EmuCoreX";
}

SDLInputSource::SDLInputSource() = default;
SDLInputSource::~SDLInputSource() = default;

bool SDLInputSource::Initialize(SettingsInterface& si, std::unique_lock<std::mutex>&)
{
	m_android_vibration_enabled = si.GetBoolValue("InputSources", "PadVibration", true);
	m_sdl_subsystem_initialized = true;
	__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "Android input source initialized without SDL joystick subsystem");
	return true;
}

void SDLInputSource::UpdateSettings(SettingsInterface& si, std::unique_lock<std::mutex>&)
{
	m_android_vibration_enabled = si.GetBoolValue("InputSources", "PadVibration", true);
	if (!m_android_vibration_enabled)
	{
		m_android_motor_state = {};
		emucorex::android::DispatchPadVibration(0, 0.0f, 0.0f);
		emucorex::android::DispatchPadVibration(1, 0.0f, 0.0f);
	}
}

bool SDLInputSource::ReloadDevices()
{
	return true;
}

void SDLInputSource::Shutdown()
{
	m_android_motor_state = {};
	emucorex::android::DispatchPadVibration(0, 0.0f, 0.0f);
	emucorex::android::DispatchPadVibration(1, 0.0f, 0.0f);
	m_sdl_subsystem_initialized = false;
}

bool SDLInputSource::IsInitialized()
{
	return m_sdl_subsystem_initialized;
}

void SDLInputSource::PollEvents()
{
}

std::vector<std::pair<std::string, std::string>> SDLInputSource::EnumerateDevices()
{
	return {{"SDL-0", "Android Gamepad 1"}, {"SDL-1", "Android Gamepad 2"}};
}

std::vector<InputBindingKey> SDLInputSource::EnumerateMotors()
{
	return {InputSource::MakeGenericControllerMotorKey(InputSourceType::SDL, 0, 0),
		InputSource::MakeGenericControllerMotorKey(InputSourceType::SDL, 0, 1),
		InputSource::MakeGenericControllerMotorKey(InputSourceType::SDL, 1, 0),
		InputSource::MakeGenericControllerMotorKey(InputSourceType::SDL, 1, 1)};
}

bool SDLInputSource::GetGenericBindingMapping(const std::string_view device, InputManager::GenericInputBindingMapping* mapping)
{
	if (!device.starts_with("SDL-") && !device.starts_with("SDL"))
		return false;

	std::string_view::size_type pos = device.find_first_of("0123456789");
	const u32 pad = (pos == std::string_view::npos) ? 0 : StringUtil::FromChars<u32>(device.substr(pos)).value_or(0);
	const std::string prefix = StringUtil::StdStringFromFormat("SDL-%u/", pad);

	*mapping = {
		{GenericInputBinding::DPadUp, prefix + "Button0"},
		{GenericInputBinding::DPadRight, prefix + "Button1"},
		{GenericInputBinding::DPadDown, prefix + "Button2"},
		{GenericInputBinding::DPadLeft, prefix + "Button3"},
		{GenericInputBinding::Triangle, prefix + "Button4"},
		{GenericInputBinding::Circle, prefix + "Button5"},
		{GenericInputBinding::Cross, prefix + "Button6"},
		{GenericInputBinding::Square, prefix + "Button7"},
		{GenericInputBinding::Select, prefix + "Button8"},
		{GenericInputBinding::Start, prefix + "Button9"},
		{GenericInputBinding::L1, prefix + "Button10"},
		{GenericInputBinding::L2, prefix + "FullAxis4"},
		{GenericInputBinding::R1, prefix + "Button12"},
		{GenericInputBinding::R2, prefix + "FullAxis5"},
		{GenericInputBinding::L3, prefix + "Button14"},
		{GenericInputBinding::R3, prefix + "Button15"},
		{GenericInputBinding::LeftStickUp, prefix + "-Axis1"},
		{GenericInputBinding::LeftStickRight, prefix + "+Axis0"},
		{GenericInputBinding::LeftStickDown, prefix + "+Axis1"},
		{GenericInputBinding::LeftStickLeft, prefix + "-Axis0"},
		{GenericInputBinding::RightStickUp, prefix + "-Axis3"},
		{GenericInputBinding::RightStickRight, prefix + "+Axis2"},
		{GenericInputBinding::RightStickDown, prefix + "+Axis3"},
		{GenericInputBinding::RightStickLeft, prefix + "-Axis2"},
		{GenericInputBinding::LargeMotor, prefix + "Motor0"},
		{GenericInputBinding::SmallMotor, prefix + "Motor1"},
	};
	return true;
}

InputLayout SDLInputSource::GetControllerLayout(u32)
{
	return InputLayout::Playstation;
}

void SDLInputSource::UpdateMotorState(InputBindingKey key, float intensity)
{
	if (!m_android_vibration_enabled || key.source_subtype != InputSubclass::ControllerMotor)
		return;

	const u32 pad = std::min<u32>(key.source_index, static_cast<u32>(m_android_motor_state.size() - 1));
	const u32 motor = std::min<u32>(key.data, 1u);
	m_android_motor_state[pad][motor] = std::clamp(intensity, 0.0f, 1.0f);
	emucorex::android::DispatchPadVibration(pad, m_android_motor_state[pad][0], m_android_motor_state[pad][1]);
}

void SDLInputSource::UpdateMotorState(InputBindingKey large_key, InputBindingKey small_key, float large_intensity, float small_intensity)
{
	if (!m_android_vibration_enabled)
		return;

	const u32 large_pad = std::min<u32>(large_key.source_index, static_cast<u32>(m_android_motor_state.size() - 1));
	if (large_key.source_subtype == InputSubclass::ControllerMotor)
		m_android_motor_state[large_pad][std::min<u32>(large_key.data, 1u)] = std::clamp(large_intensity, 0.0f, 1.0f);

	const u32 small_pad = std::min<u32>(small_key.source_index, static_cast<u32>(m_android_motor_state.size() - 1));
	if (small_key.source_subtype == InputSubclass::ControllerMotor)
		m_android_motor_state[small_pad][std::min<u32>(small_key.data, 1u)] = std::clamp(small_intensity, 0.0f, 1.0f);

	emucorex::android::DispatchPadVibration(large_pad, m_android_motor_state[large_pad][0], m_android_motor_state[large_pad][1]);
	if (small_pad != large_pad)
		emucorex::android::DispatchPadVibration(small_pad, m_android_motor_state[small_pad][0], m_android_motor_state[small_pad][1]);
}

std::optional<InputBindingKey> SDLInputSource::ParseKeyString(const std::string_view device, const std::string_view binding)
{
	return InputSource::ParseGenericControllerKey(InputSourceType::SDL, device, binding);
}

TinyString SDLInputSource::ConvertKeyToString(InputBindingKey key, bool display, bool)
{
	TinyString ret;
	if (display)
		ret.format("Android Pad {} {}", key.source_index + 1u, key.data);
	else
		ret.assign(InputSource::ConvertGenericControllerKeyToString(key));
	return ret;
}

TinyString SDLInputSource::ConvertKeyToIcon(InputBindingKey)
{
	return {};
}

bool SDLInputSource::ProcessSDLEvent(const SDL_Event*)
{
	return false;
}

SDL_Joystick* SDLInputSource::GetJoystickForDevice(const std::string_view)
{
	return nullptr;
}

u32 SDLInputSource::GetRGBForPlayerId(SettingsInterface& si, u32 player_id)
{
	const TinyString key = TinyString::from_format("SDLLEDColor%u", player_id + 1u);
	return si.GetUIntValue("InputSources", key.c_str(), static_cast<uint>(0));
}

u32 SDLInputSource::ParseRGBForPlayerId(const std::string_view str, u32)
{
	return StringUtil::FromChars<u32>(str, 16).value_or(0);
}

void SDLInputSource::ResetRGBForAllPlayers(SettingsInterface& si)
{
	for (u32 i = 0; i < MAX_LED_COLORS; i++)
	{
		const TinyString key = TinyString::from_format("SDLLEDColor%u", i + 1u);
		si.SetUIntValue("InputSources", key.c_str(), 0);
	}
}
