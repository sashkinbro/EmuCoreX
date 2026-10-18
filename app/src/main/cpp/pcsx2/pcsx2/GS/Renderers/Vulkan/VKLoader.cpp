// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Vulkan/VKLoader.h"

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/DynamicLibrary.h"
#include "common/Error.h"
#include "pcsx2/Config.h"
#include "Host.h"
#include "IconsFontAwesome.h"
#include "GS/GS.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef __ANDROID__
#include <filesystem>
#include <sys/stat.h>
#endif

#ifdef __ANDROID__
#include "common/AndroidLogging.h"
#include <adrenotools/driver.h>
#include <dlfcn.h>
#include "emucorex/android_runtime.h"
#endif

extern "C" {

#define VULKAN_MODULE_ENTRY_POINT(name, required) PFN_##name name;
#define VULKAN_INSTANCE_ENTRY_POINT(name, required) PFN_##name name;
#define VULKAN_DEVICE_ENTRY_POINT(name, required) PFN_##name name;
#include "VKEntryPoints.inl"
#undef VULKAN_DEVICE_ENTRY_POINT
#undef VULKAN_INSTANCE_ENTRY_POINT
#undef VULKAN_MODULE_ENTRY_POINT
}

void Vulkan::ResetVulkanLibraryFunctionPointers()
{
#define VULKAN_MODULE_ENTRY_POINT(name, required) name = nullptr;
#define VULKAN_INSTANCE_ENTRY_POINT(name, required) name = nullptr;
#define VULKAN_DEVICE_ENTRY_POINT(name, required) name = nullptr;
#include "VKEntryPoints.inl"
#undef VULKAN_DEVICE_ENTRY_POINT
#undef VULKAN_INSTANCE_ENTRY_POINT
#undef VULKAN_MODULE_ENTRY_POINT
}

static DynamicLibrary s_vulkan_library;

#ifdef __ANDROID__
// A custom driver was requested for this process. The ICD itself is loaded
// lazily by the Android Vulkan loader on first instance creation, so the load
// outcome can only be checked after vkCreateInstance (CheckCustomDriverLoadStatus).
static bool s_custom_driver_requested = false;
static std::string s_custom_driver_name;

static const char* BasenameForLog(const std::string& path)
{
	const size_t last_separator = path.find_last_of("/\\");
	return path.c_str() + ((last_separator == std::string::npos) ? 0 : last_separator + 1);
}

static bool LoadVulkanLibraryWithAdrenoTools(const std::string& custom_driver_path, Error* error)
{
	const char* hook_lib_dir = getenv("ANDROID_NATIVE_LIB_DIR");
	if (!hook_lib_dir || !*hook_lib_dir)
	{
		__android_log_write(ANDROID_LOG_ERROR, "EmuCoreX",
			"Vulkan custom driver: native library directory env missing");
		Console.Warning("Vulkan: Custom driver requested, but native library directory is not set");
		return false;
	}

	const size_t last_separator = custom_driver_path.find_last_of("/\\");
	if (last_separator == std::string::npos || last_separator + 1 >= custom_driver_path.size())
	{
		Console.Warning("Vulkan: Custom driver path is invalid: %s", custom_driver_path.c_str());
		return false;
	}

	std::string custom_driver_dir = custom_driver_path.substr(0, last_separator + 1);
	std::string custom_driver_name = custom_driver_path.substr(last_separator + 1);

	// Pre-validate before adrenotools/dlopen: a corrupt or truncated .so gets
	// past stat() inside adrenotools but then crashes natively on first use
	// (observed as SIGSEGV in strncmp inside Turnip). Fall back to the system
	// driver here while we still can.
	{
		struct stat st = {};
		// Real Turnip/Adreno ICDs are several MB; anything smaller cannot be
		// a working Vulkan driver.
		static constexpr off_t MIN_DRIVER_SIZE = 256 * 1024;
		if (stat(custom_driver_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < MIN_DRIVER_SIZE)
		{
			__android_log_print(ANDROID_LOG_ERROR, "EmuCoreX",
				"Vulkan custom driver failed validation (missing/too small): %s", custom_driver_path.c_str());
			Console.Warning("Vulkan: Custom driver failed validation, falling back to system driver: %s",
				custom_driver_path.c_str());
			return false;
		}
	}

	// Vortek-style Xclipse packs ship a Vulkan layer JSON next to the ICD. The
	// loader only discovers it if the driver directory is in VK_LAYER_PATH.
	bool has_layer_json = false;
	{
		std::error_code dir_error;
		for (const auto& entry : std::filesystem::directory_iterator(custom_driver_dir, dir_error))
		{
			const std::string filename = entry.path().filename().string();
			if (filename.rfind("VkLayer_", 0) == 0 && entry.path().extension() == ".json")
			{
				has_layer_json = true;
				break;
			}
		}
	}
	if (has_layer_json)
	{
		const char* existing_layer_path = getenv("VK_LAYER_PATH");
		std::string layer_path = custom_driver_dir;
		if (existing_layer_path && *existing_layer_path)
			layer_path = std::string(existing_layer_path) + ":" + layer_path;
		setenv("VK_LAYER_PATH", layer_path.c_str(), 1);
	}

	EMUCOREX_DIAGNOSTIC_LOG_PRINT(ANDROID_LOG_INFO, "EmuCoreX",
		"Vulkan custom driver: adrenotools open driver=%s hookDir=%s layer=%s",
		custom_driver_name.c_str(), hook_lib_dir, has_layer_json ? "yes" : "no");
	void* handle = adrenotools_open_libvulkan(
		RTLD_NOW,
		ADRENOTOOLS_DRIVER_CUSTOM,
		nullptr,
		hook_lib_dir,
		custom_driver_dir.c_str(),
		custom_driver_name.c_str(),
		nullptr,
		nullptr);
	if (!handle)
	{
		unsetenv("VK_LAYER_PATH");
		__android_log_print(ANDROID_LOG_ERROR, "EmuCoreX",
			"Vulkan custom driver: adrenotools failed driver=%s", custom_driver_name.c_str());
		Error::SetStringFmt(error, "adrenotools failed to open custom Vulkan driver {}", custom_driver_path);
		return false;
	}

	s_vulkan_library.Adopt(handle);
	// The hook chain is in place, but the Android Vulkan loader loads the ICD
	// lazily on first instance creation. The real outcome is reported by
	// CheckCustomDriverLoadStatus() right after vkCreateInstance.
	EMUCOREX_DIAGNOSTIC_LOG_PRINT(ANDROID_LOG_INFO, "EmuCoreX",
		"Vulkan custom driver: adrenotools handshake driver=%s", custom_driver_name.c_str());
	return true;
}

void Vulkan::CheckCustomDriverLoadStatus()
{
#ifdef __ANDROID__
	if (!s_custom_driver_requested)
		return;

	if (adrenotools_custom_driver_library_loaded())
	{
		EMUCOREX_DIAGNOSTIC_LOG_PRINT(ANDROID_LOG_INFO, "EmuCoreX",
			"Vulkan custom driver: loaded driver=%s", s_custom_driver_name.c_str());
		Console.WriteLn(Color_StrongGreen, "Vulkan: Loaded custom GPU driver: %s", s_custom_driver_name.c_str());
		return;
	}

	// The hook falls back to the system loader when the custom driver dlopen
	// fails (e.g. a missing dependency). Rendering still works on the system
	// driver, but the player must be told the selection had no effect.
	unsetenv("VK_LAYER_PATH");
	__android_log_print(ANDROID_LOG_ERROR, "EmuCoreX",
		"Vulkan custom driver: fell back to system driver, driver=%s", s_custom_driver_name.c_str());
	Console.Error("Vulkan: Custom driver '%s' could not be loaded; using the system driver",
		s_custom_driver_name.c_str());
	Host::AddIconOSDMessage("CustomGpuDriverFallback", ICON_FA_TRIANGLE_EXCLAMATION,
		"Custom GPU driver could not be loaded. Using the system driver.", Host::OSD_ERROR_DURATION);
#endif
}
#endif

bool Vulkan::IsVulkanLibraryLoaded()
{
	return s_vulkan_library.IsOpen();
}

bool Vulkan::LoadVulkanLibrary(Error* error)
{
	pxAssertRel(!s_vulkan_library.IsOpen(), "Vulkan module is not loaded.");

	std::string custom_driver_path;
#if defined(__ANDROID__)
	char* libvulkan_env = getenv("LIBVULKAN_PATH");
	if (libvulkan_env)
		custom_driver_path = libvulkan_env;
	if (custom_driver_path.empty())
		custom_driver_path = emucorex::android::AndroidRuntime::Instance().GetSetting("EmuCoreX", "CustomDriverPath");
	EMUCOREX_DIAGNOSTIC_LOG_PRINT(ANDROID_LOG_INFO, "EmuCoreX", "Vulkan loader entry customDriver=%s",
		custom_driver_path.empty() ? "<none>" : BasenameForLog(custom_driver_path));

	s_custom_driver_requested = !custom_driver_path.empty();
	s_custom_driver_name = custom_driver_path.empty() ? std::string() : BasenameForLog(custom_driver_path);

	// A stale layer path from a previous pack must not leak into the system
	// driver or into a different driver. LoadVulkanLibraryWithAdrenoTools
	// re-adds it when the selected pack actually ships a layer.
	unsetenv("VK_LAYER_PATH");

	if (!custom_driver_path.empty())
	{
		Console.WriteLn(Color_StrongGreen, "Vulkan: Attempting to load custom driver with adrenotools: %s",
			custom_driver_path.c_str());
		if (!LoadVulkanLibraryWithAdrenoTools(custom_driver_path, error))
			Console.Warning("Vulkan: Failed to load custom driver, falling back to system driver");
	}

	if (!s_vulkan_library.IsOpen())
	{
		const char* android_native_lib_dir = getenv("ANDROID_NATIVE_LIB_DIR");
		if (android_native_lib_dir)
		{
			const std::string custom_lib_path = std::string(android_native_lib_dir) + "/libvulkan.so";
			if (s_vulkan_library.Open(custom_lib_path.c_str(), error))
				Console.WriteLn(Color_StrongGreen, "Vulkan: Loaded bundled libvulkan from app directory");
		}
	}
#endif

#ifdef __APPLE__
	// Check if a path to a specific Vulkan library has been specified.
	char* libvulkan_env = getenv("LIBVULKAN_PATH");
	if (libvulkan_env)
		s_vulkan_library.Open(libvulkan_env, error);
	if (!s_vulkan_library.IsOpen() &&
		!s_vulkan_library.Open(DynamicLibrary::GetVersionedFilename("MoltenVK").c_str(), error))
	{
		return false;
	}
#else
	// try versioned first, then unversioned.
	if (!s_vulkan_library.IsOpen() &&
		!s_vulkan_library.Open(DynamicLibrary::GetVersionedFilename("vulkan", 1).c_str(), error) &&
		!s_vulkan_library.Open(DynamicLibrary::GetVersionedFilename("vulkan").c_str(), error))
	{
		return false;
	}
#endif

	bool required_functions_missing = false;
#define VULKAN_MODULE_ENTRY_POINT(name, required) \
	if (!s_vulkan_library.GetSymbol(#name, &name)) \
	{ \
		ERROR_LOG("Vulkan: Failed to load required module function {}", #name); \
		required_functions_missing = true; \
	}

#include "VKEntryPoints.inl"
#undef VULKAN_MODULE_ENTRY_POINT

	if (required_functions_missing)
	{
		ResetVulkanLibraryFunctionPointers();
		s_vulkan_library.Close();
		return false;
	}

	return true;
}

void Vulkan::UnloadVulkanLibrary()
{
	ResetVulkanLibraryFunctionPointers();
	s_vulkan_library.Close();
}

bool Vulkan::LoadVulkanInstanceFunctions(VkInstance instance)
{
	bool required_functions_missing = false;
	auto LoadFunction = [&required_functions_missing, instance](PFN_vkVoidFunction* func_ptr, const char* name, bool is_required) {
		*func_ptr = vkGetInstanceProcAddr(instance, name);
		if (!(*func_ptr) && is_required)
		{
			std::fprintf(stderr, "Vulkan: Failed to load required instance function %s\n", name);
			required_functions_missing = true;
		}
	};

#define VULKAN_INSTANCE_ENTRY_POINT(name, required) \
	LoadFunction(reinterpret_cast<PFN_vkVoidFunction*>(&name), #name, required);
#include "VKEntryPoints.inl"
#undef VULKAN_INSTANCE_ENTRY_POINT

	return !required_functions_missing;
}

bool Vulkan::LoadVulkanDeviceFunctions(VkDevice device)
{
	bool required_functions_missing = false;
	auto LoadFunction = [&required_functions_missing, device](PFN_vkVoidFunction* func_ptr, const char* name, bool is_required) {
		*func_ptr = vkGetDeviceProcAddr(device, name);
		if (!(*func_ptr) && is_required)
		{
			std::fprintf(stderr, "Vulkan: Failed to load required device function %s\n", name);
			required_functions_missing = true;
		}
	};

#define VULKAN_DEVICE_ENTRY_POINT(name, required) \
	LoadFunction(reinterpret_cast<PFN_vkVoidFunction*>(&name), #name, required);
#include "VKEntryPoints.inl"
#undef VULKAN_DEVICE_ENTRY_POINT

	return !required_functions_missing;
}
