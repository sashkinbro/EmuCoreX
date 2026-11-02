// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "GS/Renderers/Vulkan/VKLoader.h"

#include "common/HashCombine.h"

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

class VKBakedShaderPack;

class VKShaderCache
{
public:
	~VKShaderCache();

	static void Create();
	static void Destroy();

	/// Returns a handle to the pipeline cache. Set set_dirty to true if you are planning on writing to it externally.
	VkPipelineCache GetPipelineCache(bool set_dirty = true);

	/// Writes pipeline cache to file, saving all newly compiled pipelines. The write re-serialises
	/// the whole cache, so non-forced calls are rate-limited; force=true is for teardown.
	bool FlushPipelineCache(bool force = false);
	bool HasPipelineCacheFile() const { return !m_pipeline_cache_filename.empty(); }
	/// Returns the sidecar path associated with this pipeline cache, or empty when caching is disabled.
	std::string GetTFXPreparationReceiptPath() const;
	/// True only when this process loaded the current pipeline cache through the driver and the
	/// sidecar matches the caller's deterministic 64-character lowercase-hex identity.
	bool IsTFXPreparationReceiptValid(std::string_view identity) const;
	/// Atomically persists an opaque TFX preparation identity at the supplied sidecar path.
	static bool SaveTFXPreparationReceipt(const std::string& path, std::string_view identity);
	static u32 GetTFXPreparationCacheVersion();
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	static bool TestPipelineCachePersistence(const char* test_filename);
	static bool TestTFXPreparationReceipt(const char* test_filename);
#endif

	/// Creates a pipeline cache seeded with the contents of the shared cache, for use on a
	/// compilation thread. Returns VK_NULL_HANDLE on failure. Merge it back with
	/// MergeTransientPipelineCache() when the thread is done.
	VkPipelineCache CreateTransientPipelineCache();

	/// Merges a transient cache into the shared cache and destroys it. Returns true only
	/// when the merge succeeded; a null cache or absent shared cache returns false.
	bool MergeTransientPipelineCache(VkPipelineCache cache);

	/// Serializes creation against the shared pipeline cache and FlushPipelineCache().
	std::mutex& GetPipelineCacheMutex();

	VkShaderModule GetVertexShader(std::string_view shader_code);
	VkShaderModule GetFragmentShader(std::string_view shader_code);
	VkShaderModule GetComputeShader(std::string_view shader_code);

private:
	// SPIR-V compiled code type
	using SPIRVCodeType = u32;
	using SPIRVCodeVector = std::vector<SPIRVCodeType>;

	struct CacheIndexKey
	{
		u64 source_hash_low;
		u64 source_hash_high;
		u32 source_length;
		u32 shader_type;

		bool operator==(const CacheIndexKey& key) const;
		bool operator!=(const CacheIndexKey& key) const;
	};

	struct CacheIndexEntryHasher
	{
		std::size_t operator()(const CacheIndexKey& e) const noexcept
		{
			std::size_t h = 0;
			HashCombine(h, e.source_hash_low, e.source_hash_high, e.source_length, e.shader_type);
			return h;
		}
	};

	struct CacheIndexData
	{
		u32 file_offset;
		u32 blob_size;
	};

	using CacheIndex = std::unordered_map<CacheIndexKey, CacheIndexData, CacheIndexEntryHasher>;

	VKShaderCache();

	static std::string GetShaderCacheBaseFileName(bool debug);
	static std::string GetPipelineCacheBaseFileName(bool debug);
	static CacheIndexKey GetCacheKey(u32 type, const std::string_view shader_code);
	static std::optional<VKShaderCache::SPIRVCodeVector> CompileShaderToSPV(
		u32 stage, std::string_view source, bool debug);

	void Open();

	bool CreateNewShaderCache(const std::string& index_filename, const std::string& blob_filename);
	bool ReadExistingShaderCache(const std::string& index_filename, const std::string& blob_filename);
	void CloseShaderCache();

	bool CreateNewPipelineCache();
	bool ReadExistingPipelineCache();
	void InvalidateTFXPreparationReceipt() const;
	void ClosePipelineCache();
	static bool SavePipelineCacheDataIfChanged(const std::string& filename, const std::vector<u8>& data,
		bool* written = nullptr);

	std::optional<SPIRVCodeVector> GetShaderSPV(u32 type, std::string_view shader_code);
	std::optional<SPIRVCodeVector> CompileAndAddShaderSPV(const CacheIndexKey& key, std::string_view shader_code);
	VkShaderModule GetShaderModule(u32 type, std::string_view shader_code);

	std::FILE* m_index_file = nullptr;
	std::FILE* m_blob_file = nullptr;
	std::string m_pipeline_cache_filename;

	CacheIndex m_index;

	std::mutex m_shader_mutex;
	std::mutex m_pipeline_cache_mutex;

	VkPipelineCache m_pipeline_cache = VK_NULL_HANDLE;
	bool m_pipeline_cache_loaded_from_disk = false;
	std::atomic<bool> m_pipeline_cache_dirty{false};
	std::chrono::steady_clock::time_point m_last_pipeline_cache_flush{};

	std::unique_ptr<VKBakedShaderPack> m_baked_pack;
};

extern std::unique_ptr<VKShaderCache> g_vulkan_shader_cache;
