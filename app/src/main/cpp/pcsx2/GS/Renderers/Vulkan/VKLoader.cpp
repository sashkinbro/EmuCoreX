// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Vulkan/VKLoader.h"

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/DynamicLibrary.h"
#include "common/Error.h"
#include "pcsx2/Config.h"
#include "GS/GS.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef __ANDROID__
#ifdef USE_ADRENOTOOLS
#include <adrenotools/driver.h>
#endif
#include <dlfcn.h>
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

bool Vulkan::IsVulkanLibraryLoaded()
{
	return s_vulkan_library.IsOpen();
}

bool Vulkan::LoadVulkanLibrary(Error* error)
{
	pxAssertRel(!s_vulkan_library.IsOpen(), "Vulkan module is not loaded.");

	// Check for custom driver path from config
	std::string custom_driver_path;
	if (GSConfig.CustomDriverPath.empty())
	{
		char* libvulkan_env = getenv("LIBVULKAN_PATH");
		if (libvulkan_env)
			custom_driver_path = libvulkan_env;
#if defined(USE_ADRENOTOOLS) && defined(__ANDROID__)
		if (custom_driver_path.empty())
		{
			char* adreno_tools_path = getenv("ADRENOTOOLS_LIBVULKAN_PATH");
			if (adreno_tools_path)
				custom_driver_path = adreno_tools_path;
		}
#endif
	}
	else
	{
		custom_driver_path = GSConfig.CustomDriverPath;
	}

	// Try to load custom driver if specified
	if (!custom_driver_path.empty())
	{
#if defined(__ANDROID__) && defined(USE_ADRENOTOOLS)
		std::string custom_driver_dir;
		std::string custom_driver_name;
		
		size_t last_slash = custom_driver_path.find_last_of("/\\");
		if (last_slash != std::string::npos)
		{
			custom_driver_dir = custom_driver_path.substr(0, last_slash + 1);
			custom_driver_name = custom_driver_path.substr(last_slash + 1);
		}
		else
		{
			custom_driver_name = custom_driver_path;
		}

		const char* hook_lib_dir = getenv("ANDROID_NATIVE_LIB_DIR");
		if (!hook_lib_dir)
		{
			hook_lib_dir = getenv("ANDROID_DATA_DIR");
		}
		
		if (hook_lib_dir && !custom_driver_dir.empty() && !custom_driver_name.empty())
		{
			Console.WriteLn(Color_StrongGreen, "Vulkan: Using libadrenotools to load custom driver: %s from %s", 
				custom_driver_name.c_str(), custom_driver_dir.c_str());
			
			void* vulkan_handle = adrenotools_open_libvulkan(
				RTLD_NOW | RTLD_LOCAL,  // dlopenMode
				ADRENOTOOLS_DRIVER_CUSTOM,  // featureFlags
				nullptr,  // tmpLibDir (nullptr for API 29+)
				hook_lib_dir,  // hookLibDir
				custom_driver_dir.c_str(),  // customDriverDir
				custom_driver_name.c_str(),  // customDriverName
				nullptr,  // fileRedirectDir
				nullptr   // userMappingHandle
			);
			
			if (vulkan_handle)
			{
				// Grab the handle from libadrenotools
				s_vulkan_library.Adopt(vulkan_handle);
				Console.WriteLn(Color_StrongGreen, "Vulkan: Successfully loaded custom driver via libadrenotools");
			}
			else
			{
				Console.Warning("Vulkan: libadrenotools failed to load custom driver, falling back to direct loading");
				// Fall through to direct loading
				if (s_vulkan_library.Open(custom_driver_path.c_str(), error))
				{
					Console.WriteLn(Color_StrongGreen, "Vulkan: Successfully loaded custom driver directly");
				}
				else
				{
					Console.Warning("Vulkan: Failed to load custom driver from '%s', falling back to system driver", custom_driver_path.c_str());
				}
			}
		}
		else
		{
			Console.Warning("Vulkan: libadrenotools requires ANDROID_NATIVE_LIB_DIR and valid custom driver path, falling back to direct loading");
			if (s_vulkan_library.Open(custom_driver_path.c_str(), error))
			{
				Console.WriteLn(Color_StrongGreen, "Vulkan: Successfully loaded custom driver directly");
			}
			else
			{
				Console.Warning("Vulkan: Failed to load custom driver from '%s', falling back to system driver", custom_driver_path.c_str());
			}
		}
#else
		// Loading without libadrenotools
		Console.WriteLn(Color_StrongGreen, "Vulkan: Attempting to load custom driver from: %s", custom_driver_path.c_str());
		if (s_vulkan_library.Open(custom_driver_path.c_str(), error))
		{
			Console.WriteLn(Color_StrongGreen, "Vulkan: Successfully loaded custom driver");
		}
		else
		{
			Console.Warning("Vulkan: Failed to load custom driver from '%s', falling back to system driver", custom_driver_path.c_str());
		}
#endif
	}

#ifdef __APPLE__
	// On macOS, try MoltenVK if custom driver failed or wasn't specified
	if (!s_vulkan_library.IsOpen() &&
		!s_vulkan_library.Open(DynamicLibrary::GetVersionedFilename("MoltenVK").c_str(), error))
	{
		return false;
	}
#else
	// On other platforms, try versioned first, then unversioned system libraries
	if (!s_vulkan_library.IsOpen())
	{
#if defined(__ANDROID__)
		const char* android_native_lib_dir = getenv("ANDROID_NATIVE_LIB_DIR");
		if (android_native_lib_dir)
		{
			std::string custom_lib_path = std::string(android_native_lib_dir) + "/libvulkan.so";
			if (s_vulkan_library.Open(custom_lib_path.c_str(), error))
			{
				Console.WriteLn(Color_StrongGreen, "Vulkan: Loaded custom driver from app directory");
			}
		}
		
		if (!s_vulkan_library.IsOpen())
		{
			if (!s_vulkan_library.Open("libvulkan.so", error))
			{
				if (!s_vulkan_library.Open(DynamicLibrary::GetVersionedFilename("vulkan", 1).c_str(), error) &&
					!s_vulkan_library.Open(DynamicLibrary::GetVersionedFilename("vulkan").c_str(), error))
				{
					return false;
				}
			}
		}
#else
		if (!s_vulkan_library.Open(DynamicLibrary::GetVersionedFilename("vulkan", 1).c_str(), error) &&
			!s_vulkan_library.Open(DynamicLibrary::GetVersionedFilename("vulkan").c_str(), error))
		{
			return false;
		}
#endif
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
