#pragma once

#include "emucorex/android_runtime.h"
#include "emucorex/tfx_preparation_job.h"

#include <string>

namespace emucorex::android
{
using VmStartupCallback = void (*)(void* userdata, bool succeeded);

bool IsUpstreamVmBridgeAvailable();
bool RunUpstreamVm(const VmLaunchConfig& config, VmStartupCallback startup_callback = nullptr, void* startup_userdata = nullptr);
struct TFXPreparationBridgeResult
{
	TFXPreparationState state = TFXPreparationState::Failed;
	TFXPreparationError error = TFXPreparationError::InternalError;
	std::string receipt_path;
	std::string receipt_identity;
};

TFXPreparationBridgeResult RunTFXPreparation(const VmLaunchConfig& config, TFXPreparationJob& job, bool validation_only = false);
void ApplyRuntimeSettingsToUpstream(const VmLaunchConfig& config);
int QueryMaxUpscaleMultiplier(int renderer);
void InitializeSettingsLayer();

// Human-readable reason for the most recent failed VM start, exposed to the UI so
// arcade/BIOS launch failures are not reduced to a generic "Unable to open this game".
void SetLastBootError(const std::string& message);
std::string GetLastBootError();
}
