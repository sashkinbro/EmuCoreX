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

// Status codes returned by ReloadGpuDriver (mirrored in EmulatorBridge.kt).
enum GpuDriverReloadStatus : int
{
	GpuDriverReloadApplied = 0,         // GS device recreated with the requested driver
	GpuDriverReloadStoredNoVm = 1,      // no VM running; setting stored for the next boot
	GpuDriverReloadStoredNotVulkan = 2, // active renderer is not Vulkan; applies once Vulkan is used
	GpuDriverReloadFellBack = 3,        // device recreated, but on the system driver
	GpuDriverReloadFailed = 4,          // GS reopen failed
	GpuDriverReloadDeferred = 5,        // still queued/running when the wait timed out
};

// Stores the custom driver path (empty = system driver) and, when a Vulkan VM is running,
// recreates the GS device so the change takes effect. Blocks the caller (never call from the UI thread).
int ReloadGpuDriver(const std::string& path);
int QueryMaxUpscaleMultiplier(int renderer);
void InitializeSettingsLayer();

// Human-readable reason for the most recent failed VM start, exposed to the UI so
// arcade/BIOS launch failures are not reduced to a generic "Unable to open this game".
void SetLastBootError(const std::string& message);
std::string GetLastBootError();
}
