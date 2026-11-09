// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/GSShaderCompileIndicator.h"
#include "GS/GS.h"
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "GS/Renderers/Vulkan/VKBakedShaderPack.h"
#include "GS/Renderers/Vulkan/VKBuilders.h"
#include "GS/Renderers/Vulkan/VKShaderCache.h"

#include "Config.h"
#include "BuildVersion.h"
#include "GS/GSCompileStats.h"
#include "ShaderCacheVersion.h"

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/DynamicLibrary.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/MD5Digest.h"
#include "common/Path.h"
#include "common/ScopedGuard.h"

#ifndef __ANDROID__
#include "SPIRV/GlslangToSpv.h"
#include "StandAlone/ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"
#endif

#include "fmt/format.h"
#include "shaderc/shaderc.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <mutex>
#ifdef _WIN32
#include "common/RedtapeWindows.h"
#include "common/StringUtil.h"
#elif !defined(__ANDROID__)
#include <dlfcn.h>
#endif

#ifdef __ANDROID__
#include <unistd.h>
#endif

// TODO: store the driver version and stuff in the shader header

std::unique_ptr<VKShaderCache> g_vulkan_shader_cache;

#ifndef EMUCOREX_DISABLE_GS_DIAGNOSTICS
static std::atomic<u32> s_next_bad_shader_id{0};
#endif
#ifndef __ANDROID__
static bool s_glslang_initialized = false;
static bool s_shaderc_fallback_warning_emitted = false;
#endif

namespace
{
#pragma pack(push, 4)
	struct VK_PIPELINE_CACHE_HEADER
	{
		u32 header_length;
		u32 header_version;
		u32 vendor_id;
		u32 device_id;
		u8 uuid[VK_UUID_SIZE];
	};

	struct CacheIndexEntry
	{
		u64 source_hash_low;
		u64 source_hash_high;
		u32 source_length;
		u32 shader_type;
		u32 file_offset;
		u32 blob_size;
	};
#pragma pack(pop)

	// Real pipeline caches are kilobytes to low megabytes. Anything beyond this
	// cannot be a valid blob, so reject it before the driver touches it.
	static constexpr size_t MAX_PIPELINE_CACHE_SIZE = 256 * 1024 * 1024;
	static constexpr std::string_view TFX_PREPARATION_RECEIPT_HEADER = "EMUCOREX-TFX-PREP/1\n";
	static constexpr size_t TFX_PREPARATION_IDENTITY_LENGTH = 64;
	static constexpr size_t TFX_PREPARATION_RECEIPT_SIZE = TFX_PREPARATION_RECEIPT_HEADER.size() +
		TFX_PREPARATION_IDENTITY_LENGTH + 1;

	static bool IsValidTFXPreparationIdentity(std::string_view identity)
	{
		if (identity.size() != TFX_PREPARATION_IDENTITY_LENGTH)
			return false;

		return std::all_of(identity.begin(), identity.end(), [](char ch) {
			return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
		});
	}
} // namespace

static bool ValidatePipelineCacheHeader(const VK_PIPELINE_CACHE_HEADER& header)
{
	if (header.header_length < sizeof(VK_PIPELINE_CACHE_HEADER))
	{
		Console.Error("Pipeline cache failed validation: Invalid header length");
		return false;
	}

	if (header.header_version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
	{
		Console.Error("Pipeline cache failed validation: Invalid header version");
		return false;
	}

	if (header.vendor_id != GSDeviceVK::GetInstance()->GetDeviceProperties().vendorID)
	{
		Console.Error("Pipeline cache failed validation: Incorrect vendor ID (file: 0x%X, device: 0x%X)",
			header.vendor_id, GSDeviceVK::GetInstance()->GetDeviceProperties().vendorID);
		return false;
	}

	if (header.device_id != GSDeviceVK::GetInstance()->GetDeviceProperties().deviceID)
	{
		Console.Error("Pipeline cache failed validation: Incorrect device ID (file: 0x%X, device: 0x%X)",
			header.device_id, GSDeviceVK::GetInstance()->GetDeviceProperties().deviceID);
		return false;
	}

	if (std::memcmp(header.uuid, GSDeviceVK::GetInstance()->GetDeviceProperties().pipelineCacheUUID, VK_UUID_SIZE) != 0)
	{
		Console.Error("Pipeline cache failed validation: Incorrect UUID");
		return false;
	}

	return true;
}

static void FillPipelineCacheHeader(VK_PIPELINE_CACHE_HEADER* header)
{
	header->header_length = sizeof(VK_PIPELINE_CACHE_HEADER);
	header->header_version = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
	header->vendor_id = GSDeviceVK::GetInstance()->GetDeviceProperties().vendorID;
	header->device_id = GSDeviceVK::GetInstance()->GetDeviceProperties().deviceID;
	std::memcpy(header->uuid, GSDeviceVK::GetInstance()->GetDeviceProperties().pipelineCacheUUID, VK_UUID_SIZE);
}

#define SHADERC_FUNCTIONS(X) \
	X(shaderc_compiler_initialize) \
	X(shaderc_compiler_release) \
	X(shaderc_compile_options_initialize) \
	X(shaderc_compile_options_release) \
	X(shaderc_compile_options_set_source_language) \
	X(shaderc_compile_options_set_generate_debug_info) \
	X(shaderc_compile_options_set_optimization_level) \
	X(shaderc_compile_options_set_target_env) \
	X(shaderc_compile_into_spv) \
	X(shaderc_result_release) \
	X(shaderc_result_get_length) \
	X(shaderc_result_get_num_warnings) \
	X(shaderc_result_get_bytes) \
	X(shaderc_result_get_error_message) \
	X(shaderc_result_get_compilation_status)

namespace dyn_shaderc
{
	static bool Open();
	static void Close();

	static DynamicLibrary s_library;
	static shaderc_compiler_t s_compiler = nullptr;
	static bool s_open_failed = false;
	static std::mutex s_mutex;

#define ADD_FUNC(F) static decltype(&::F) F;
	SHADERC_FUNCTIONS(ADD_FUNC)
#undef ADD_FUNC

} // namespace dyn_shaderc

bool dyn_shaderc::Open()
{
	std::lock_guard<std::mutex> lock(s_mutex);

	if (s_compiler)
		return true;

	if (s_open_failed)
		return false;

#ifdef __ANDROID__
#define LOAD_FUNC(F) F = &::F;
	SHADERC_FUNCTIONS(LOAD_FUNC)
#undef LOAD_FUNC
#else
	if (s_library.IsOpen())
		return true;

	Error error;

#ifdef _WIN32
	const std::string libname = DynamicLibrary::GetVersionedFilename("shaderc_shared");
#else
	// Use versioned, bundle post-processing adds it..
	const std::string libname = DynamicLibrary::GetVersionedFilename("shaderc_shared", 1);
#endif
	if (!s_library.Open(libname.c_str(), &error))
	{
		s_open_failed = true;
		ERROR_LOG("Failed to load shaderc: {}", error.GetDescription());
		return false;
	}

#define LOAD_FUNC(F) \
	if (!s_library.GetSymbol(#F, &F)) \
	{ \
		s_open_failed = true; \
		ERROR_LOG("Failed to find function {}", #F); \
		Close(); \
		return false; \
	}

	SHADERC_FUNCTIONS(LOAD_FUNC)
#undef LOAD_FUNC
#endif

	s_compiler = shaderc_compiler_initialize();
	if (!s_compiler)
	{
		s_open_failed = true;
		ERROR_LOG("shaderc_compiler_initialize() failed");
		Close();
		return false;
	}

	std::atexit(&dyn_shaderc::Close);
	return true;
}

void dyn_shaderc::Close()
{
	if (s_compiler)
	{
		shaderc_compiler_release(s_compiler);
		s_compiler = nullptr;
	}

#define UNLOAD_FUNC(F) F = nullptr;
	SHADERC_FUNCTIONS(UNLOAD_FUNC)
#undef UNLOAD_FUNC

	s_library.Close();
}

#undef SHADERC_FUNCTIONS
#undef SHADERC_INIT_FUNCTIONS

#ifndef EMUCOREX_DISABLE_GS_DIAGNOSTICS
static void DumpBadShader(std::string_view code, std::string_view errors)
{
	const std::string filename = Path::Combine(EmuFolders::Logs, fmt::format("pcsx2_bad_shader_{}.txt", ++s_next_bad_shader_id));
	auto fp = FileSystem::OpenManagedCFile(filename.c_str(), "wb");
	if (fp)
	{
		if (!code.empty())
			std::fwrite(code.data(), code.size(), 1, fp.get());
		std::fputs("\n\n**** ERRORS ****\n", fp.get());
		if (!errors.empty())
			std::fwrite(errors.data(), errors.size(), 1, fp.get());
	}
}
#endif

#ifndef __ANDROID__
static bool InitializeGlslang()
{
	if (s_glslang_initialized)
		return true;

	if (!glslang::InitializeProcess())
	{
		ERROR_LOG("Failed to initialize glslang shader compiler");
		return false;
	}

	std::atexit(&glslang::FinalizeProcess);
	s_glslang_initialized = true;
	return true;
}

static std::optional<EShLanguage> ShaderKindToGlslangStage(u32 stage)
{
	switch (static_cast<shaderc_shader_kind>(stage))
	{
		case shaderc_glsl_vertex_shader:
			return EShLangVertex;
		case shaderc_glsl_fragment_shader:
			return EShLangFragment;
		case shaderc_glsl_compute_shader:
			return EShLangCompute;
		default:
			return std::nullopt;
	}
}

static std::optional<std::vector<u32>> CompileShaderToSPVWithGlslang(u32 stage, std::string_view source, bool debug)
{
	if (!InitializeGlslang())
		return std::nullopt;

	const std::optional<EShLanguage> glslang_stage = ShaderKindToGlslangStage(stage);
	if (!glslang_stage.has_value())
	{
		ERROR_LOG("glslang fallback does not support shader stage {}", stage);
		return std::nullopt;
	}

	std::unique_ptr<glslang::TShader> shader = std::make_unique<glslang::TShader>(*glslang_stage);
	std::unique_ptr<glslang::TProgram> program;
	glslang::TShader::ForbidIncluder includer;
	const EProfile profile = ECoreProfile;
	const EShMessages messages =
		static_cast<EShMessages>(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules | (debug ? EShMsgDebugInfo : 0));
	const int default_version = 450;

	const char* pass_source_code = source.data();
	int pass_source_code_length = static_cast<int>(source.size());
	shader->setStringsWithLengths(&pass_source_code, &pass_source_code_length, 1);

	auto dump_with_logs = [&](const char* msg) {
		std::string errors(msg);
		errors.append("\nShader Info Log:\n");
		errors.append(shader->getInfoLog());
		errors.append("\nShader Debug Log:\n");
		errors.append(shader->getInfoDebugLog());
		if (program)
		{
			errors.append("\nProgram Info Log:\n");
			errors.append(program->getInfoLog());
			errors.append("\nProgram Debug Log:\n");
			errors.append(program->getInfoDebugLog());
		}
#ifndef EMUCOREX_DISABLE_GS_DIAGNOSTICS
		DumpBadShader(source, errors);
#endif
	};

	if (!shader->parse(&glslang::DefaultTBuiltInResource, default_version, profile, false, true, messages, includer))
	{
		dump_with_logs("Failed to parse shader");
		return std::nullopt;
	}

	program = std::make_unique<glslang::TProgram>();
	program->addShader(shader.get());
	if (!program->link(messages))
	{
		dump_with_logs("Failed to link program");
		return std::nullopt;
	}

	glslang::TIntermediate* intermediate = program->getIntermediate(*glslang_stage);
	if (!intermediate)
	{
		dump_with_logs("Failed to generate SPIR-V");
		return std::nullopt;
	}

	std::vector<u32> out_code;
	spv::SpvBuildLogger logger;
	glslang::SpvOptions options;
	options.generateDebugInfo = debug;
	glslang::GlslangToSpv(*intermediate, out_code, &logger, &options);

	if (std::strlen(shader->getInfoLog()) > 0)
		WARNING_LOG("Shader info log: {}", shader->getInfoLog());
	if (std::strlen(shader->getInfoDebugLog()) > 0)
		WARNING_LOG("Shader debug info log: {}", shader->getInfoDebugLog());
	if (std::strlen(program->getInfoLog()) > 0)
		WARNING_LOG("Program info log: {}", program->getInfoLog());
	if (std::strlen(program->getInfoDebugLog()) > 0)
		WARNING_LOG("Program debug info log: {}", program->getInfoDebugLog());

	const std::string spv_messages = logger.getAllMessages();
	if (!spv_messages.empty())
		WARNING_LOG("SPIR-V conversion messages: {}", spv_messages);

	return out_code;
}
#endif

static const char* compilation_status_to_string(shaderc_compilation_status status)
{
	switch (status)
	{
#define CASE(x) case shaderc_compilation_status_##x: return #x
		CASE(success);
		CASE(invalid_stage);
		CASE(compilation_error);
		CASE(internal_error);
		CASE(null_result_object);
		CASE(invalid_assembly);
		CASE(validation_error);
		CASE(transformation_error);
		CASE(configuration_error);
#undef CASE
	}
	return "unknown_error";
}

static std::mutex s_shaderc_open_mutex;
static bool OpenShaderCompiler()
{
    std::lock_guard lock(s_shaderc_open_mutex);
    return dyn_shaderc::Open();
}
// Linked compiler identity includes the content-derived GS build stamp. Dynamic
// fallback compiler identity is conservatively build-qualified as well.
static std::string GetShaderCompilerIdentity()
{
#if defined(__ANDROID__) || defined(ARMSX2_LINK_SHADERC)
	// The compiler is linked into this binary, so the binary stands for it.
	return fmt::format("linked {} {}", BuildVersion::GitHash, GSCacheFile::GetBuildId());
#else
	if (!OpenShaderCompiler())
		return fmt::format("linked glslang {} {}", BuildVersion::GitHash, GSCacheFile::GetBuildId());

	// The library that was loaded. Its path, size and modification time change with any update to it.
	std::string path;
	const void* const fn = reinterpret_cast<const void*>(dyn_shaderc::shaderc_compile_into_spv);
#ifdef _WIN32
	HMODULE module = nullptr;
	wchar_t wpath[MAX_PATH] = {};
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			static_cast<LPCWSTR>(fn), &module) &&
		GetModuleFileNameW(module, wpath, MAX_PATH) > 0)
	{
		path = StringUtil::WideStringToUTF8String(wpath);
	}
#else
	Dl_info info = {};
	if (dladdr(fn, &info) != 0 && info.dli_fname)
		path = info.dli_fname;
#endif
	FILESYSTEM_STAT_DATA sd = {};
	if (path.empty() || !FileSystem::StatFile(path.c_str(), &sd))
		return fmt::format("shared {} unknown", path);
	return fmt::format("shared {} {} {}", path, sd.Size, sd.ModificationTime);
#endif
}

static constexpr u16 SPIRV_TYPE = 1;

std::optional<VKShaderCache::SPIRVCodeVector> VKShaderCache::CompileShaderToSPV(u32 stage, std::string_view source, bool debug)
{
	std::optional<VKShaderCache::SPIRVCodeVector> ret;
	if (!OpenShaderCompiler())
	{
#ifdef __ANDROID__
		ERROR_LOG("Static shaderc compiler could not be initialized.");
		return std::nullopt;
#else
		if (!s_shaderc_fallback_warning_emitted)
		{
			WARNING_LOG("Falling back to glslang for SPIR-V compilation because shaderc could not be loaded.");
			s_shaderc_fallback_warning_emitted = true;
		}
		return CompileShaderToSPVWithGlslang(stage, source, debug);
#endif
	}

	const GSShaderCompileIndicator::CompileTimer compile_timer;

	shaderc_compile_options_t options = dyn_shaderc::shaderc_compile_options_initialize();
	pxAssertRel(options, "shaderc_compile_options_initialize() failed");

	dyn_shaderc::shaderc_compile_options_set_source_language(options, shaderc_source_language_glsl);
	dyn_shaderc::shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, 0);
#ifdef SHADERC_PCSX2_CUSTOM
	dyn_shaderc::shaderc_compile_options_set_generate_debug_info(options, debug,
		debug && GSDeviceVK::GetInstance()->GetOptionalExtensions().vk_khr_shader_non_semantic_info);
#else
	if (debug)
		dyn_shaderc::shaderc_compile_options_set_generate_debug_info(options);
#endif
	dyn_shaderc::shaderc_compile_options_set_optimization_level(
		options, debug ? shaderc_optimization_level_zero : shaderc_optimization_level_performance);

	const shaderc_compilation_result_t result = dyn_shaderc::shaderc_compile_into_spv(
		dyn_shaderc::s_compiler, source.data(), source.length(), static_cast<shaderc_shader_kind>(stage), "source",
		"main", options);

	shaderc_compilation_status status = shaderc_compilation_status_null_result_object;
	if (!result || (status = dyn_shaderc::shaderc_result_get_compilation_status(result)) != shaderc_compilation_status_success)
	{
		const std::string_view errors(result ? dyn_shaderc::shaderc_result_get_error_message(result)
		                                     : "null result object");
		ERROR_LOG("Failed to compile shader to SPIR-V: {}\n{}", compilation_status_to_string(status), errors);
#ifndef EMUCOREX_DISABLE_GS_DIAGNOSTICS
		DumpBadShader(source, errors);
#endif
	}
	else
	{
		const size_t num_warnings = dyn_shaderc::shaderc_result_get_num_warnings(result);
		if (num_warnings > 0)
			WARNING_LOG("Shader compiled with warnings:\n{}", dyn_shaderc::shaderc_result_get_error_message(result));

		const size_t spirv_size = dyn_shaderc::shaderc_result_get_length(result);
		const char* bytes = dyn_shaderc::shaderc_result_get_bytes(result);
		pxAssert(spirv_size > 0 && ((spirv_size % sizeof(u32)) == 0));
		ret = VKShaderCache::SPIRVCodeVector(reinterpret_cast<const u32*>(bytes),
			reinterpret_cast<const u32*>(bytes + spirv_size));
	}

	dyn_shaderc::shaderc_result_release(result);
	dyn_shaderc::shaderc_compile_options_release(options);
	return ret;
}

VKShaderCache::VKShaderCache() = default;

VKShaderCache::~VKShaderCache()
{
	// The last pass writes the packs and the pipeline cache within the close deadline. It needs the
	// device, which outlives this.
	m_writer.Stop(GSCacheFile::Clock::now() + GSCacheFile::PackWriter::CLOSE_DEADLINE);
	ClosePipelineCache();
}

void VKShaderCache::Create()
{
	pxAssert(!g_vulkan_shader_cache);
	g_vulkan_shader_cache.reset(new VKShaderCache());
	g_vulkan_shader_cache->Open();
}

void VKShaderCache::Destroy()
{
	g_vulkan_shader_cache.reset();
}

GSCacheFile::Stamp VKShaderCache::GetSPIRVStamp(bool debug)
{
	// No build identity and no driver: the key of every entry is the whole GLSL source text and
	// stage, and SPIR-V does not depend on the driver, so an entry is valid for as long as the
	// compiler and its options are the same. That is what keeps SPIR-V across a driver update.
	GSCacheFile::Stamp stamp;
	stamp.Add("shader_cache_version", SHADER_CACHE_VERSION);
	stamp.Add("compiler", GetShaderCompilerIdentity());
	stamp.Add("target", "vulkan1.0");
	stamp.Add("optimization", debug ? "zero" : "performance");
#ifdef SHADERC_PCSX2_CUSTOM
	// Debug info is non-semantic only when the device has the extension.
	stamp.Add("debug_info", debug ? (GSDeviceVK::GetInstance()->GetOptionalExtensions().vk_khr_shader_non_semantic_info ?
											 "non-semantic" :
											 "plain") :
									"none");
#else
	stamp.Add("debug_info", debug ? "plain" : "none");
#endif
	return stamp;
}

GSCacheFile::Stamp VKShaderCache::GetPipelineCacheStamp(bool debug)
{
	// Driver binaries: only valid for the driver that made them. pipelineCacheUUID alone is not
	// trusted, since drivers have shipped updates without changing it; the version and the driver's
	// own name and build string are in too. The build identity is in because what the blob holds
	// depends on this build's pipelines, and a new build starting from an empty blob is what stops
	// pipelines no build uses any more from piling up in it.
	const VkPhysicalDeviceProperties& props = GSDeviceVK::GetInstance()->GetDeviceProperties();
	const VkPhysicalDeviceDriverPropertiesKHR& drv = GSDeviceVK::GetInstance()->GetDeviceDriverProperties();
	GSCacheFile::Stamp stamp;
	stamp.Add("build", GSCacheFile::GetBuildId());
	stamp.Add("shader_cache_version", SHADER_CACHE_VERSION);
	stamp.Add("vendor", props.vendorID);
	stamp.Add("device", props.deviceID);
	stamp.Add("driver_version", props.driverVersion);
	stamp.Add("api_version", props.apiVersion);
	stamp.AddHex("pipeline_cache_uuid", props.pipelineCacheUUID, VK_UUID_SIZE);
	stamp.Add("driver_id", static_cast<u64>(drv.driverID));
	stamp.Add("driver_name", drv.driverName);
	stamp.Add("driver_info", drv.driverInfo);
	stamp.Add("debug", debug ? 1 : 0);
	return stamp;
}

void VKShaderCache::Open()
{
	m_device = GSDeviceVK::GetInstance()->GetDevice();
	m_baked_pack = std::make_unique<VKBakedShaderPack>();
	if (!m_baked_pack->Open()) m_baked_pack.reset();
	if (GSConfig.DisableShaderCache)
	{
		CreateNewPipelineCache();
		return;
	}

	const bool debug = GSConfig.UseDebugDevice;
	GSCacheFile::CleanStaleTempFiles(EmuFolders::Cache);

	// Each file's name carries its stamp's digest, so builds and drivers sharing a data root keep
	// separate files; the few most recently used others stay, older ones are removed.
	static constexpr u32 KEEP_IDENTITIES = 3;
	m_pipeline_cache_stamp = GetPipelineCacheStamp(debug);
	const std::string pipeline_stem = fmt::format("vulkan_pipelines{}_{}", debug ? "_debug" : "",
		GSCacheFile::ShortName(m_pipeline_cache_stamp.GetDigest()));
	GSCacheFile::PruneOtherIdentities(EmuFolders::Cache, "vulkan_pipelines", pipeline_stem, KEEP_IDENTITIES);
	m_pipeline_cache_filename = Path::Combine(EmuFolders::Cache, pipeline_stem + ".bin");
	if (!ReadExistingPipelineCache())
		CreateNewPipelineCache();

	m_writer.Start(EmuFolders::Cache, [this](GSCacheFile::Clock::time_point deadline, bool asked) {
		WritePipelineCache(deadline, asked);
	});

	// SPIR-V is kept per game, read whole when the game starts (SetGame); the shaders the device builds
	// before any game is known have a pack of their own.
	m_spirv_stamp = GetSPIRVStamp(debug);
	m_spirv_stem = fmt::format("vk_spirv{}_{}", debug ? "_debug" : "", GSCacheFile::ShortName(m_spirv_stamp.GetDigest()));
	m_device_spirv = OpenGamePack(std::string(), 0, "vk_spirv", m_spirv_stem, GSCacheFile::KIND_VK_SPIRV, m_spirv_stamp);
}

void VKShaderCache::SetGame(const std::string& serial, u32 crc)
{
	if (!m_writer.IsRunning())
		return;

	std::shared_ptr<GSCacheFile::Pack> pack;
	if (!serial.empty())
	{
		const std::string path =
			Path::Combine(GSCacheFile::GetPackDirectory(EmuFolders::Cache, serial, crc), m_spirv_stem + ".pack");
		{
			std::unique_lock lock(m_spirv_mutex);
			if (m_game_spirv && m_game_spirv->GetPath() == path)
				return;
		}
		pack = OpenGamePack(serial, crc, "vk_spirv", m_spirv_stem, GSCacheFile::KIND_VK_SPIRV, m_spirv_stamp);
	}

	std::shared_ptr<GSCacheFile::Pack> old;
	{
		std::unique_lock lock(m_spirv_mutex);
		old = std::exchange(m_game_spirv, std::move(pack));
	}
	ReleasePack(std::move(old));
}

std::shared_ptr<GSCacheFile::Pack> VKShaderCache::OpenGamePack(const std::string& serial, u32 crc,
	std::string_view prefix, const std::string& stem, GSCacheFile::Kind kind, const GSCacheFile::Stamp& stamp)
{
	if (!m_writer.IsRunning())
		return {};
	std::shared_ptr<GSCacheFile::Pack> pack =
		GSCacheFile::OpenPack(GSCacheFile::GetPackDirectory(EmuFolders::Cache, serial, crc), prefix, stem, kind, stamp);
	m_writer.Add(pack);
	return pack;
}

void VKShaderCache::ReleasePack(std::shared_ptr<GSCacheFile::Pack> pack)
{
	m_writer.Release(std::move(pack));
}

void VKShaderCache::RequestFlush()
{
	m_writer.Flush(false);
}

void VKShaderCache::ResetPipelineCache()
{
	InvalidateTFXPreparationReceipt();
	m_pipeline_cache_loaded_from_disk = false;
	// Nothing else may be using the cache but the writer, which the lock keeps out: the caller has
	// stopped the precompile workers and runs on the GS thread. Pipelines already built do not refer
	// to it.
	std::unique_lock lock(m_pipeline_cache_mutex);
	ClosePipelineCache();
	CreateNewPipelineCache();
}

VkPipelineCache VKShaderCache::GetPipelineCache(bool set_dirty /*= true*/)
{
	if (m_pipeline_cache == VK_NULL_HANDLE)
		return VK_NULL_HANDLE;

	if (set_dirty)
		m_pipeline_cache_dirty.store(true, std::memory_order_relaxed);
	return m_pipeline_cache;
}

bool VKShaderCache::CreateNewPipelineCache()
{
	const VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, 0, nullptr};
	VkResult res = vkCreatePipelineCache(m_device, &ci, nullptr, &m_pipeline_cache);
	if (res != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(res, "vkCreatePipelineCache() failed: ");
		return false;
	}

	m_pipeline_cache_file_hash = 0;
	m_pipeline_cache_dirty = true;
	return true;
}

bool VKShaderCache::ReadExistingPipelineCache()
{
	const s64 file_size = FileSystem::GetPathFileSize(m_pipeline_cache_filename.c_str());
	if (file_size > static_cast<s64>(MAX_PIPELINE_CACHE_SIZE + 56)) return false;
	std::vector<u8> data;
	const GSCacheFile::ReadResult res = GSCacheFile::ReadFramedFile(
		m_pipeline_cache_filename, GSCacheFile::KIND_VK_PIPELINES, m_pipeline_cache_stamp, &data);
	if (res != GSCacheFile::ReadResult::Ok)
	{
		if (res != GSCacheFile::ReadResult::Missing)
		{
			INFO_LOG("Vulkan: discarding pipeline cache '{}': {}", m_pipeline_cache_filename,
				GSCacheFile::ReadResultString(res));
		}
		return false;
	}

	// The stamp already covers the device, but the blob's own header is what the driver will judge,
	// so it has to agree too.
	VK_PIPELINE_CACHE_HEADER header;
	if (data.size() < sizeof(header))
		return false;
	std::memcpy(&header, data.data(), sizeof(header));
	if (!ValidatePipelineCacheHeader(header))
		return false;

	const VkPipelineCacheCreateInfo ci{
		VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, data.size(), data.data()};
	VkResult vres = vkCreatePipelineCache(m_device, &ci, nullptr, &m_pipeline_cache);
	if (vres != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(vres, "vkCreatePipelineCache() failed: ");
		return false;
	}

	m_pipeline_cache_loaded_from_disk = true;
	m_pipeline_cache_file_hash = GSCacheFile::Hash64(data.data(), data.size());
	INFO_LOG("Vulkan: read {} bytes of pipeline cache", data.size());
	return true;
}

void VKShaderCache::WritePipelineCache(GSCacheFile::Clock::time_point deadline, bool asked)
{
	if (!m_pipeline_cache_dirty.load(std::memory_order_relaxed) || m_pipeline_cache_filename.empty())
		return;

	// The whole cache is serialised and written each time (it is opaque, so there is nothing to
	// append), so it is done at most once a minute while it keeps changing. Losing the last minute
	// costs only compiling those pipelines again at the next start.
	static constexpr std::chrono::seconds MIN_FLUSH_INTERVAL{60};
	const auto now = std::chrono::steady_clock::now();
	if (!asked && m_last_pipeline_cache_flush.time_since_epoch().count() != 0 &&
		(now - m_last_pipeline_cache_flush) < MIN_FLUSH_INTERVAL)
	{
		return;
	}

	// vkGetPipelineCacheData puts no external-synchronisation requirement on the cache, so this runs
	// while the GS thread and the precompile workers go on creating pipelines with it. The lock keeps
	// out only ResetPipelineCache and the destructor.
	std::unique_lock lock(m_pipeline_cache_mutex);
	if (m_pipeline_cache == VK_NULL_HANDLE)
		return;
	m_last_pipeline_cache_flush = now;

	const GSCompileStats::ScopedTimer stats_timer(GSCompileStats::CacheFlushNs);

	// Cleared before the data is read, so a pipeline created while this runs marks it dirty again
	// instead of being lost.
	m_pipeline_cache_dirty.store(false, std::memory_order_relaxed);

	size_t data_size;
	VkResult res = vkGetPipelineCacheData(m_device, m_pipeline_cache, &data_size, nullptr);
	if (res != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(res, "vkGetPipelineCacheData() failed: ");
		m_pipeline_cache_dirty.store(true, std::memory_order_relaxed);
		return;
	}

	std::vector<u8> data(data_size);
	res = vkGetPipelineCacheData(m_device, m_pipeline_cache, &data_size, data.data());
	if (res != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(res, "vkGetPipelineCacheData() (2) failed: ");
		m_pipeline_cache_dirty.store(true, std::memory_order_relaxed);
		return;
	}

	data.resize(data_size);

	// Compared by content: a blob can change without changing size.
	const u64 hash = GSCacheFile::Hash64(data.data(), data.size());
	if (hash == m_pipeline_cache_file_hash)
		return;

	// Framed with a checksum and written to a temporary file that is renamed over the old one: a
	// half-written blob that still passes the driver's header check has rendered garbage on Adreno.
	if (!GSCacheFile::WriteFramedFile(m_pipeline_cache_filename, GSCacheFile::KIND_VK_PIPELINES,
			m_pipeline_cache_stamp, data.data(), data.size(), deadline))
	{
		Console.Warning("Vulkan: pipeline cache not written to '%s'%s", m_pipeline_cache_filename.c_str(),
			(GSCacheFile::Clock::now() >= deadline) ? " before the close deadline" : "");
		m_pipeline_cache_dirty.store(true, std::memory_order_relaxed);
		return;
	}
#if !defined(EMUCOREX_DISABLE_GS_DIAGNOSTICS) && !defined(NDEBUG)
	DevCon.WriteLn("Vulkan: wrote %zu bytes of pipeline cache", data_size);
#endif
	m_pipeline_cache_file_hash = hash;
}

void VKShaderCache::ClosePipelineCache()
{
	if (m_pipeline_cache == VK_NULL_HANDLE)
		return;

	vkDestroyPipelineCache(m_device, m_pipeline_cache, nullptr);
	m_pipeline_cache = VK_NULL_HANDLE;
}

std::optional<VKShaderCache::SPIRVCodeVector> VKShaderCache::GetShaderSPV(u32 type, std::string_view shader_code)
{
    // Android baked packs keep their established MD5 key format. Runtime packs
    // use upstream's full-source XXH128+length+stage key, without reinterpreting baked entries.
    if (m_baked_pack && !GSConfig.UseDebugDevice) {
        MD5Digest md5;md5.Update(shader_code.data(),static_cast<u32>(shader_code.size()));union {u8 bytes[16];struct {u64 hash_low,hash_high;};} hash{};md5.Final(hash.bytes);
        if (auto baked=m_baked_pack->Lookup(hash.hash_low,hash.hash_high,static_cast<u32>(shader_code.size()),type)) {
            GSShaderCompileIndicator::RecordBakedHit();return baked;
        }
    }

	// The key: a 128-bit hash of the whole source text, its length and the stage.
	u8 key[sizeof(GSCacheFile::Digest) + 2 * sizeof(u32)];
	const GSCacheFile::Digest digest = GSCacheFile::Hash128(shader_code.data(), shader_code.size());
	const u32 length = static_cast<u32>(shader_code.size());
	std::memcpy(&key[0], digest.bytes, sizeof(digest.bytes));
	std::memcpy(&key[16], &length, sizeof(length));
	std::memcpy(&key[20], &type, sizeof(type));

	std::shared_ptr<GSCacheFile::Pack> game;
	std::shared_ptr<GSCacheFile::Pack> device;
	{
		std::unique_lock lock(m_spirv_mutex);
		game = m_game_spirv;
		device = m_device_spirv;
	}

	std::vector<u8> data;
	for (const GSCacheFile::Pack* pack : {game.get(), device.get()})
	{
		if (!pack || !pack->Get(SPIRV_TYPE, key, sizeof(key), &data))
			continue;
		// The checksum already passed; a SPIR-V module is whole words starting with the magic number.
		static constexpr u32 SPIRV_MAGIC = 0x07230203;
		u32 magic = 0;
		if (data.size() >= sizeof(u32) && (data.size() % sizeof(u32)) == 0 &&
			(std::memcpy(&magic, data.data(), sizeof(magic)), magic == SPIRV_MAGIC))
		{
			GSCompileStats::Add(GSCompileStats::SpirvCacheHits, 1);
			SPIRVCodeVector spv(data.size() / sizeof(u32));
			std::memcpy(spv.data(), data.data(), data.size());
			return spv;
		}
		Console.Error("Vulkan: cached SPIR-V is not SPIR-V, recompiling");
	}

	std::optional<SPIRVCodeVector> spv = CompileShaderToSPV(type, shader_code, GSConfig.UseDebugDevice);
	if (GSCacheFile::Pack* pack = game ? game.get() : device.get(); spv.has_value() && pack)
		pack->Put(SPIRV_TYPE, key, sizeof(key), spv->data(), spv->size() * sizeof(SPIRVCodeType), 0, false);
	return spv;
}

VkShaderModule VKShaderCache::GetShaderModule(u32 type, std::string_view shader_code)
{
	std::optional<SPIRVCodeVector> spv = GetShaderSPV(type, shader_code);
	if (!spv.has_value())
		return VK_NULL_HANDLE;

	const VkShaderModuleCreateInfo ci{
		VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, spv->size() * sizeof(SPIRVCodeType), spv->data()};

	const GSCompileStats::ScopedTimer stats_timer(GSCompileStats::ModuleCreateNs);
	VkShaderModule mod;
	VkResult res = vkCreateShaderModule(m_device, &ci, nullptr, &mod);
	if (res != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(res, "vkCreateShaderModule() failed: ");
		return VK_NULL_HANDLE;
	}

	return mod;
}

VkShaderModule VKShaderCache::GetVertexShader(std::string_view shader_code)
{
	return GetShaderModule(shaderc_glsl_vertex_shader, std::move(shader_code));
}

VkShaderModule VKShaderCache::GetFragmentShader(std::string_view shader_code)
{
	return GetShaderModule(shaderc_glsl_fragment_shader, std::move(shader_code));
}

VkShaderModule VKShaderCache::GetComputeShader(std::string_view shader_code)
{
	return GetShaderModule(shaderc_glsl_compute_shader, std::move(shader_code));
}

std::string VKShaderCache::GetTFXPreparationReceiptPath() const
{
	return m_pipeline_cache_filename.empty() ? std::string() : (m_pipeline_cache_filename + ".tfxprep");
}

bool VKShaderCache::IsTFXPreparationReceiptValid(std::string_view identity) const
{
	if (!m_pipeline_cache_loaded_from_disk || m_pipeline_cache_filename.empty() ||
		!IsValidTFXPreparationIdentity(identity) || !FileSystem::FileExists(m_pipeline_cache_filename.c_str()))
	{
		return false;
	}

	const std::string path = GetTFXPreparationReceiptPath();
	if (FileSystem::GetPathFileSize(path.c_str()) != static_cast<s64>(TFX_PREPARATION_RECEIPT_SIZE))
		return false;

	const std::optional<std::vector<u8>> contents = FileSystem::ReadBinaryFile(path.c_str());
	if (!contents.has_value() || contents->size() != TFX_PREPARATION_RECEIPT_SIZE)
		return false;

	const std::string_view receipt(reinterpret_cast<const char*>(contents->data()), contents->size());
	return receipt.substr(0, TFX_PREPARATION_RECEIPT_HEADER.size()) == TFX_PREPARATION_RECEIPT_HEADER &&
		receipt.substr(TFX_PREPARATION_RECEIPT_HEADER.size(), TFX_PREPARATION_IDENTITY_LENGTH) == identity &&
		receipt.back() == '\n';
}

bool VKShaderCache::SaveTFXPreparationReceipt(const std::string& path, std::string_view identity)
{
	if (path.empty() || !IsValidTFXPreparationIdentity(identity))
		return false;

	std::vector<u8> contents;
	contents.reserve(TFX_PREPARATION_RECEIPT_SIZE);
	contents.insert(contents.end(), TFX_PREPARATION_RECEIPT_HEADER.begin(), TFX_PREPARATION_RECEIPT_HEADER.end());
	contents.insert(contents.end(), identity.begin(), identity.end());
	contents.push_back('\n');
	return SavePipelineCacheDataIfChanged(path, contents);
}

u32 VKShaderCache::GetTFXPreparationCacheVersion()
{
	return SHADER_CACHE_VERSION;
}

void VKShaderCache::InvalidateTFXPreparationReceipt() const
{
	const std::string path = GetTFXPreparationReceiptPath();
	if (path.empty())
		return;

	FileSystem::DeleteFilePath(path.c_str());
	const std::string tmp_path = path + ".tmp";
	FileSystem::DeleteFilePath(tmp_path.c_str());
}


bool VKShaderCache::SavePipelineCacheDataIfChanged(const std::string& filename, const std::vector<u8>& data,
	bool* written)
{
	if (written)
		*written = false;

	const std::optional<std::vector<u8>> existing_data = FileSystem::ReadBinaryFile(filename.c_str());
	if (existing_data.has_value() && *existing_data == data)
	{
		Console.WriteLn("Skipping updating pipeline cache '%s' due to no changes.", filename.c_str());
		return true;
	}

	Console.WriteLn("Writing %zu bytes to '%s'", data.size(), filename.c_str());
	// Write through a temp file and rename: a kill mid-flush must never leave a
	// torn blob behind, or the next launch feeds it to the driver and crashes.
	const std::string tmp_filename = filename + ".tmp";
	auto fp = FileSystem::OpenManagedCFile(tmp_filename.c_str(), "wb");
	if (!fp)
	{
		const int open_error = errno;
		Console.Error("Failed to write pipeline cache to '%s'", filename.c_str());
		FileSystem::DeleteFilePath(tmp_filename.c_str());
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
		std::fprintf(stderr, "Pipeline cache persistence temp open failed for '%s.tmp' (errno=%d)\n",
			filename.c_str(), open_error);
#endif
		return false;
	}

	const bool write_ok = data.empty() || std::fwrite(data.data(), 1, data.size(), fp.get()) == data.size();
	const bool flush_ok = std::fflush(fp.get()) == 0;
	const bool close_ok = std::fclose(fp.release()) == 0;
	if (!write_ok || !flush_ok || !close_ok ||
		!FileSystem::RenamePath(tmp_filename.c_str(), filename.c_str()))
	{
		Console.Error("Failed to write pipeline cache to '%s'", filename.c_str());
		FileSystem::DeleteFilePath(tmp_filename.c_str());
		return false;
	}

	if (written)
		*written = true;
	return true;
}

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
bool VKShaderCache::TestPipelineCachePersistence(const char* test_filename)
{
	if (!test_filename || !*test_filename)
	{
		std::fprintf(stderr, "Pipeline cache persistence test: empty test filename (errno=%d)\n", errno);
		return false;
	}

	const std::string filename(test_filename);
	const std::string tmp_filename = filename + ".tmp";
	const std::string failed_filename = filename + FS_OSPATH_SEPARATOR_STR "child";
	const std::string failed_tmp_filename = failed_filename + ".tmp";
	ScopedGuard cleanup([&]() {
		FileSystem::DeleteFilePath(filename.c_str());
		FileSystem::DeleteFilePath(tmp_filename.c_str());
		FileSystem::DeleteFilePath(failed_filename.c_str());
		FileSystem::DeleteFilePath(failed_tmp_filename.c_str());
	});
	bool written = false;
	const auto fail = [&written](const char* stage) {
		std::fprintf(stderr, "Pipeline cache persistence test failed at %s (written=%d, errno=%d)\n",
			stage, written, errno);
		return false;
	};

	FileSystem::DeleteFilePath(filename.c_str());
	FileSystem::DeleteFilePath(tmp_filename.c_str());
	FileSystem::DeleteFilePath(failed_filename.c_str());
	FileSystem::DeleteFilePath(failed_tmp_filename.c_str());

	const std::vector<u8> first{0x10, 0x20, 0x30};
	const std::vector<u8> changed{0x10, 0x20, 0x31};
	if (!SavePipelineCacheDataIfChanged(filename, first, &written) || !written)
		return fail("missing-file write");
	auto persisted = FileSystem::ReadBinaryFile(filename.c_str());
	if (!persisted.has_value() || *persisted != first)
	{
		std::fprintf(stderr, "Pipeline cache persistence test failed at initial readback (present=%d, size=%zu, expected=%zu, bytes_match=%d, errno=%d)\n",
			persisted.has_value(), persisted.has_value() ? persisted->size() : 0, first.size(),
			persisted.has_value() && *persisted == first, errno);
		return false;
	}

	if (!SavePipelineCacheDataIfChanged(filename, changed, &written) || !written)
		return fail("same-size changed write");
	persisted = FileSystem::ReadBinaryFile(filename.c_str());
	if (!persisted.has_value() || *persisted != changed)
	{
		std::fprintf(stderr, "Pipeline cache persistence test failed at changed readback (present=%d, size=%zu, expected=%zu, bytes_match=%d, errno=%d)\n",
			persisted.has_value(), persisted.has_value() ? persisted->size() : 0, changed.size(),
			persisted.has_value() && *persisted == changed, errno);
		return false;
	}

	if (!SavePipelineCacheDataIfChanged(filename, changed, &written) || written)
		return fail("equal blob no-write");

#ifdef __ANDROID__
	// /dev/full accepts buffered writes but fails when the stream flushes. The
	// temporary path is a test-owned symlink, so cleanup removes only the link.
	if (symlink("/dev/full", tmp_filename.c_str()) != 0)
		return fail("/dev/full symlink setup");
	if (SavePipelineCacheDataIfChanged(filename, first, &written) || written)
		return fail("/dev/full flush failure");
	persisted = FileSystem::ReadBinaryFile(filename.c_str());
	if (!persisted.has_value() || *persisted != changed || FileSystem::FileExists(tmp_filename.c_str()))
		return fail("/dev/full source/temp integrity");
#endif

	// Use the existing regular file as a parent path, which makes the child
	// destination unwritable and must leave the source and temporary path intact.
	if (SavePipelineCacheDataIfChanged(failed_filename, first, &written) || written)
		return fail("regular-file parent failure");
	persisted = FileSystem::ReadBinaryFile(filename.c_str());
	if (!persisted.has_value() || *persisted != changed || FileSystem::FileExists(failed_tmp_filename.c_str()) ||
		FileSystem::FileExists(failed_filename.c_str()))
		return fail("regular-file parent source/temp integrity");
	return true;
}

bool VKShaderCache::TestTFXPreparationReceipt(const char* test_filename)
{
	if (!test_filename || !*test_filename)
		return false;

	const std::string filename(test_filename);
	const std::string receipt_path = filename + ".tfxprep";
	ScopedGuard cleanup([&]() {
		FileSystem::DeleteFilePath(filename.c_str());
		FileSystem::DeleteFilePath((filename + ".tmp").c_str());
		FileSystem::DeleteFilePath(receipt_path.c_str());
		FileSystem::DeleteFilePath((receipt_path + ".tmp").c_str());
	});
	FileSystem::DeleteFilePath(filename.c_str());
	FileSystem::DeleteFilePath((filename + ".tmp").c_str());
	FileSystem::DeleteFilePath(receipt_path.c_str());
	FileSystem::DeleteFilePath((receipt_path + ".tmp").c_str());

	const std::string identity = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
	const std::string changed_identity = "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
	const std::vector<u8> cache_data{0x10, 0x20, 0x30, 0x40};
	const std::vector<u8> changed_cache_data{0x10, 0x20, 0x30, 0x41, 0x50};
	const auto fail = [](const char* stage) {
		std::fprintf(stderr, "TFX preparation receipt test failed at %s (errno=%d)\n", stage, errno);
		return false;
	};

	if (!FileSystem::WriteBinaryFile(filename.c_str(), cache_data.data(), cache_data.size()))
		return fail("pipeline cache fixture");
	VKShaderCache cache;
	cache.m_pipeline_cache_filename = filename;
	if (cache.GetTFXPreparationReceiptPath() != receipt_path)
		return fail("receipt path");
	if (cache.IsTFXPreparationReceiptValid(identity))
		return fail("missing receipt rejected");
	if (!SaveTFXPreparationReceipt(receipt_path, identity))
		return fail("write receipt");
	if (cache.IsTFXPreparationReceiptValid(identity))
		return fail("not-loaded cache rejected");
	cache.m_pipeline_cache_loaded_from_disk = true;
	if (!cache.IsTFXPreparationReceiptValid(identity))
		return fail("matching receipt accepted");
	if (!FileSystem::WriteBinaryFile(filename.c_str(), changed_cache_data.data(), changed_cache_data.size()) ||
		!cache.IsTFXPreparationReceiptValid(identity))
	{
		return fail("ordinary cache growth preserves receipt");
	}
	FileSystem::DeleteFilePath(filename.c_str());
	if (cache.IsTFXPreparationReceiptValid(identity))
		return fail("late cache deletion rejected");
	if (!FileSystem::WriteBinaryFile(filename.c_str(), changed_cache_data.data(), changed_cache_data.size()))
		return fail("restore cache fixture");
	if (cache.IsTFXPreparationReceiptValid(changed_identity))
		return fail("identity mismatch rejected");
	if (SaveTFXPreparationReceipt(receipt_path, "invalid"))
		return fail("malformed identity rejected");
	const char malformed_receipt[] = "TFXPREP/1\ninvalid\n";
	if (!FileSystem::WriteBinaryFile(receipt_path.c_str(), malformed_receipt, sizeof(malformed_receipt) - 1))
		return fail("malformed sidecar fixture");
	if (cache.IsTFXPreparationReceiptValid(identity))
		return fail("malformed sidecar rejected");
	if (!SaveTFXPreparationReceipt(receipt_path, identity))
		return fail("restore valid receipt");
	cache.InvalidateTFXPreparationReceipt();
	if (cache.IsTFXPreparationReceiptValid(identity) || FileSystem::FileExists(receipt_path.c_str()))
		return fail("new-cache invalidation");
	if (SaveTFXPreparationReceipt(receipt_path, ""))
		return fail("empty identity rejected");
	FileSystem::DeleteFilePath(filename.c_str());
	if (cache.IsTFXPreparationReceiptValid(identity))
		return fail("missing cache rejected");
	cache.m_pipeline_cache_filename.clear();
	if (!cache.GetTFXPreparationReceiptPath().empty() || cache.IsTFXPreparationReceiptValid(identity))
		return fail("disabled cache rejected");
	return true;
}
#endif


VkPipelineCache VKShaderCache::CreateTransientPipelineCache()
{
	VkDevice device = m_device;
	const VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, 0, nullptr};

	VkPipelineCache cache = VK_NULL_HANDLE;
	VkResult res = vkCreatePipelineCache(device, &ci, nullptr, &cache);
	if (res != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(res, "vkCreatePipelineCache() failed: ");
		return VK_NULL_HANDLE;
	}

	if (m_pipeline_cache != VK_NULL_HANDLE)
	{
		std::lock_guard lock(m_pipeline_cache_mutex);
		res = vkMergePipelineCaches(device, cache, 1, &m_pipeline_cache);
		if (res != VK_SUCCESS)
			LOG_VULKAN_ERROR(res, "vkMergePipelineCaches() (seed) failed: ");
	}

	return cache;
}

bool VKShaderCache::MergeTransientPipelineCache(VkPipelineCache cache)
{
	if (cache == VK_NULL_HANDLE)
		return false;

	VkDevice device = m_device;
	bool merged = false;
	if (m_pipeline_cache != VK_NULL_HANDLE)
	{
		std::lock_guard lock(m_pipeline_cache_mutex);
		const VkResult res = vkMergePipelineCaches(device, m_pipeline_cache, 1, &cache);
		if (res == VK_SUCCESS)
		{
			m_pipeline_cache_dirty.store(true, std::memory_order_relaxed);
			merged = true;
		}
		else
			LOG_VULKAN_ERROR(res, "vkMergePipelineCaches() (merge) failed: ");
	}

	vkDestroyPipelineCache(device, cache, nullptr);
	return merged;
}

std::mutex& VKShaderCache::GetPipelineCacheMutex()
{
	return m_pipeline_cache_mutex;
}


bool VKShaderCache::FlushPipelineCache(bool force)
{
    if (m_pipeline_cache_filename.empty() || !m_writer.IsRunning()) return false;
    m_writer.Flush(force);
    return !force || (!m_pipeline_cache_dirty.load(std::memory_order_relaxed) &&
        FileSystem::FileExists(m_pipeline_cache_filename.c_str()));
}
#ifdef EMUCOREX_ENABLE_NATIVE_SELF_TESTS
#include "GSCachePackTests.inl"
#endif
