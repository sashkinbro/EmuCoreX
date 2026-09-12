#pragma once

#include "emucorex/android_runtime.h"

#include <string>

namespace emucorex::android
{
using VmStartupCallback = void (*)(void* userdata, bool succeeded);

bool IsUpstreamVmBridgeAvailable();
bool RunUpstreamVm(const VmLaunchConfig& config, VmStartupCallback startup_callback = nullptr, void* startup_userdata = nullptr);
void ApplyRuntimeSettingsToUpstream(const VmLaunchConfig& config);
void InitializeSettingsLayer();

// Human-readable reason for the most recent failed VM start, exposed to the UI so
// arcade/BIOS launch failures are not reduced to a generic "Unable to open this game".
void SetLastBootError(const std::string& message);
std::string GetLastBootError();
}
