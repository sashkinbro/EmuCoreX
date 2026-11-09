// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "GS/GSCacheFile.h"
#include "GS/Renderers/Vulkan/VKLoader.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class VKShaderCache
{
public:
	~VKShaderCache();

	static void Create();
	static void Destroy();

	/// Returns a handle to the pipeline cache. Set set_dirty to true if you are planning on writing to it externally.
	/// Callable from pipeline compile workers: the VkPipelineCache is created internally synchronized.
	VkPipelineCache GetPipelineCache(bool set_dirty = true);

	/// Asks the background writer to save the pipeline cache now instead of at its next pass (an app
	/// about to be killed in the background). Returns at once.
	void RequestFlush();
    // Compatibility with Android offline TFX preparation. A forced flush waits
    // for the writer ticket before a completion receipt may be published.
    bool FlushPipelineCache(bool force=false);
    bool HasPipelineCacheFile() const { return !m_pipeline_cache_filename.empty(); }
    std::string GetTFXPreparationReceiptPath() const;
    bool IsTFXPreparationReceiptValid(std::string_view identity) const;
    static bool SaveTFXPreparationReceipt(const std::string& path,std::string_view identity);
    static u32 GetTFXPreparationCacheVersion();
    VkPipelineCache CreateTransientPipelineCache();
    bool MergeTransientPipelineCache(VkPipelineCache cache);
    std::mutex& GetPipelineCacheMutex();
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
    static bool TestPipelineCachePersistence(const char* test_filename);
    static bool TestTFXPreparationReceipt(const char* test_filename);
    static void TestGamePackStorage(void(*check)(bool,const char*),const char* directory);
#endif


	/// Replaces the pipeline cache with an empty one, so a cleared cache is not written back. GS thread
	/// only, with no pipeline compile running on another thread.
	void ResetPipelineCache();

	/// Opens the SPIR-V pack of the game the GS runs now (an empty serial: none), reading it whole.
	/// GS thread, with no pipeline compile running on another thread.
	void SetGame(const std::string& serial, u32 crc);

	/// Opens `<stem>.pack` in the game's cache directory, written by this cache's writer until
	/// ReleasePack. Null when the cache is off.
	std::shared_ptr<GSCacheFile::Pack> OpenGamePack(const std::string& serial, u32 crc, std::string_view prefix,
		const std::string& stem, GSCacheFile::Kind kind, const GSCacheFile::Stamp& stamp);
	void ReleasePack(std::shared_ptr<GSCacheFile::Pack> pack);

	/// The shader getters may run on several threads at once: the packs lock themselves, and GLSL
	/// compilation runs outside any lock.
	VkShaderModule GetVertexShader(std::string_view shader_code);
	VkShaderModule GetFragmentShader(std::string_view shader_code);
	VkShaderModule GetComputeShader(std::string_view shader_code);

private:
	// SPIR-V compiled code type
	using SPIRVCodeType = u32;
	using SPIRVCodeVector = std::vector<SPIRVCodeType>;

	VKShaderCache();
	void InvalidateTFXPreparationReceipt() const;
	static bool SavePipelineCacheDataIfChanged(const std::string&, const std::vector<u8>&, bool* written=nullptr);

	static std::optional<VKShaderCache::SPIRVCodeVector> CompileShaderToSPV(
		u32 stage, std::string_view source, bool debug);
	static GSCacheFile::Stamp GetSPIRVStamp(bool debug);
	static GSCacheFile::Stamp GetPipelineCacheStamp(bool debug);

	void Open();

	bool CreateNewPipelineCache();
	bool ReadExistingPipelineCache();
	void ClosePipelineCache();
	/// The writer's part: serialises the pipeline cache and writes it, when it changed, at most once a
	/// minute unless `asked`. Writer thread.
	void WritePipelineCache(GSCacheFile::Clock::time_point deadline, bool asked);

	std::optional<SPIRVCodeVector> GetShaderSPV(u32 type, std::string_view shader_code);
	VkShaderModule GetShaderModule(u32 type, std::string_view shader_code);

	/// Writes the packs and the pipeline cache in the background. Not running when the cache is off.
	GSCacheFile::PackWriter m_writer;

	/// SPIR-V by GLSL source: the device's own shaders, and the running game's.
	GSCacheFile::Stamp m_spirv_stamp;
	std::string m_spirv_stem;
	std::mutex m_spirv_mutex;
	std::shared_ptr<GSCacheFile::Pack> m_device_spirv;
	std::shared_ptr<GSCacheFile::Pack> m_game_spirv;

	std::string m_pipeline_cache_filename;
	GSCacheFile::Stamp m_pipeline_cache_stamp;
	/// Hash of the pipeline cache data last read or written, so an unchanged cache is not rewritten.
	u64 m_pipeline_cache_file_hash = 0;

	/// The device, for the writer thread: GSDeviceVK::GetInstance() is not safe to call from it.
	VkDevice m_device = VK_NULL_HANDLE;
	bool m_pipeline_cache_loaded_from_disk = false;
	std::unique_ptr<class VKBakedShaderPack> m_baked_pack;
	/// Held by the writer while it serialises the cache, and by whatever replaces or destroys it.
	std::mutex m_pipeline_cache_mutex;
	VkPipelineCache m_pipeline_cache = VK_NULL_HANDLE;
	std::atomic<bool> m_pipeline_cache_dirty{false};
	/// When the cache was last serialised. Writer thread.
	std::chrono::steady_clock::time_point m_last_pipeline_cache_flush{};
};

extern std::unique_ptr<VKShaderCache> g_vulkan_shader_cache;
