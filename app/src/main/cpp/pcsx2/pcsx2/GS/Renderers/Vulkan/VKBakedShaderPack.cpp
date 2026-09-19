// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Vulkan/VKBakedShaderPack.h"

#include "Config.h"
#include "ShaderCacheVersion.h"

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#include <utility>

namespace
{
#pragma pack(push, 4)
	struct BakedShaderPackHeader
	{
		u32 magic;
		u32 pack_version;
		u32 shader_cache_version;
		u32 entry_count;
		u64 blob_size_bytes;
		u32 reserved[2];
	};

	struct BakedShaderPackEntry
	{
		u64 source_hash_low;
		u64 source_hash_high;
		u32 source_length;
		u32 shader_type;
		u32 blob_offset;
		u32 blob_size;
	};
#pragma pack(pop)

	static constexpr u32 BAKED_SHADER_PACK_MAGIC = 0x4B425845; // 'EXBK'
	static constexpr u32 BAKED_SHADER_PACK_VERSION = 1;
	static constexpr u32 SPIRV_MAGIC = 0x07230203;
} // namespace

VKBakedShaderPack::VKBakedShaderPack() = default;

VKBakedShaderPack::~VKBakedShaderPack()
{
	Close();
}

std::string VKBakedShaderPack::GetIndexFileName()
{
	return Path::Combine(EmuFolders::Resources, "shaders/vulkan/baked_shaders.idx");
}

std::string VKBakedShaderPack::GetBlobFileName()
{
	return Path::Combine(EmuFolders::Resources, "shaders/vulkan/baked_shaders.bin");
}

std::string VKBakedShaderPack::GetFallbackIndexFileName()
{
	return Path::Combine(EmuFolders::Cache, "baked_shaders.idx");
}

std::string VKBakedShaderPack::GetFallbackBlobFileName()
{
	return Path::Combine(EmuFolders::Cache, "baked_shaders.bin");
}

bool VKBakedShaderPack::Open()
{
	Close();

	if (OpenFromFiles(GetIndexFileName(), GetBlobFileName()))
		return true;

	return OpenFromFiles(GetFallbackIndexFileName(), GetFallbackBlobFileName());
}

bool VKBakedShaderPack::OpenFromFiles(const std::string& index_filename, const std::string& blob_filename)
{
	if (!FileSystem::FileExists(index_filename.c_str()) || !FileSystem::FileExists(blob_filename.c_str()))
		return false;

	std::FILE* index_file = FileSystem::OpenCFile(index_filename.c_str(), "rb");
	if (!index_file)
	{
		Console.Error("Failed to open baked shader pack index '%s'", index_filename.c_str());
		return false;
	}

	BakedShaderPackHeader header = {};
	if (std::fread(&header, sizeof(header), 1, index_file) != 1 || header.magic != BAKED_SHADER_PACK_MAGIC)
	{
		Console.Error("Baked shader pack '%s' has an invalid header", index_filename.c_str());
		std::fclose(index_file);
		return false;
	}

	if (header.pack_version != BAKED_SHADER_PACK_VERSION)
	{
		Console.Warning("Ignoring baked shader pack '%s' with unsupported version %u", index_filename.c_str(),
			header.pack_version);
		std::fclose(index_file);
		return false;
	}

	if (header.shader_cache_version != SHADER_CACHE_VERSION)
	{
		Console.Warning("Ignoring stale baked shader pack '%s' (version %u, expected %u)", index_filename.c_str(),
			header.shader_cache_version, SHADER_CACHE_VERSION);
		std::fclose(index_file);
		return false;
	}

	FILESYSTEM_STAT_DATA index_stat = {};
	if (!FileSystem::StatFile(index_filename.c_str(), &index_stat) ||
		index_stat.Size < static_cast<s64>(sizeof(BakedShaderPackHeader)) ||
		header.entry_count >
			(static_cast<u64>(index_stat.Size) - sizeof(BakedShaderPackHeader)) / sizeof(BakedShaderPackEntry))
	{
		Console.Error("Baked shader pack index '%s' has an invalid entry count", index_filename.c_str());
		std::fclose(index_file);
		return false;
	}

	FILESYSTEM_STAT_DATA blob_stat = {};
	if (!FileSystem::StatFile(blob_filename.c_str(), &blob_stat) || blob_stat.Size < 0 ||
		static_cast<u64>(blob_stat.Size) < header.blob_size_bytes)
	{
		Console.Error("Baked shader pack blob '%s' is missing or truncated", blob_filename.c_str());
		std::fclose(index_file);
		return false;
	}

	std::FILE* blob_file = FileSystem::OpenCFile(blob_filename.c_str(), "rb");
	if (!blob_file)
	{
		Console.Error("Failed to open baked shader pack blob '%s'", blob_filename.c_str());
		std::fclose(index_file);
		return false;
	}

	std::unordered_map<LookupKey, BlobReference, LookupKeyHasher> index;
	index.reserve(header.entry_count);

	const u64 max_blob_words = header.blob_size_bytes / sizeof(u32);
	u32 duplicate_count = 0;
	for (u32 i = 0; i < header.entry_count; i++)
	{
		BakedShaderPackEntry entry = {};
		if (std::fread(&entry, sizeof(entry), 1, index_file) != 1)
		{
			Console.Error("Baked shader pack '%s' is truncated at entry %u", index_filename.c_str(), i);
			std::fclose(blob_file);
			std::fclose(index_file);
			return false;
		}

		if (entry.blob_size == 0 || static_cast<u64>(entry.blob_offset) + entry.blob_size > max_blob_words)
		{
			Console.Error("Baked shader pack '%s' entry %u has an invalid blob range", index_filename.c_str(), i);
			std::fclose(blob_file);
			std::fclose(index_file);
			return false;
		}

		const LookupKey key{
			entry.source_hash_low, entry.source_hash_high, entry.source_length, entry.shader_type};
		if (!index.emplace(key, BlobReference{entry.blob_offset, entry.blob_size}).second)
			duplicate_count++;
	}

	std::fclose(index_file);

	if (duplicate_count > 0)
		Console.Warning("Baked shader pack '%s' contains %u duplicate entries", index_filename.c_str(), duplicate_count);

	m_blob_file = blob_file;
	m_index = std::move(index);

	Console.WriteLn("Baked shader pack: loaded %u shaders (%llu bytes) from '%s'", static_cast<u32>(m_index.size()),
		static_cast<unsigned long long>(header.blob_size_bytes), blob_filename.c_str());
	return true;
}

void VKBakedShaderPack::Close()
{
	if (m_blob_file)
	{
		std::fclose(m_blob_file);
		m_blob_file = nullptr;
	}

	m_index.clear();
}

std::optional<std::vector<u32>> VKBakedShaderPack::Lookup(
	u64 source_hash_low, u64 source_hash_high, u32 source_length, u32 shader_type)
{
	if (!m_blob_file)
		return std::nullopt;

	const auto it = m_index.find(LookupKey{source_hash_low, source_hash_high, source_length, shader_type});
	if (it == m_index.end())
		return std::nullopt;

	const BlobReference& ref = it->second;
	std::vector<u32> words(ref.blob_size);
	if (FileSystem::FSeek64(m_blob_file, static_cast<s64>(ref.blob_offset) * sizeof(u32), SEEK_SET) != 0 ||
		std::fread(words.data(), sizeof(u32), ref.blob_size, m_blob_file) != ref.blob_size)
	{
		Console.Error("Failed to read baked shader blob (offset %u, size %u)", ref.blob_offset, ref.blob_size);
		return std::nullopt;
	}

	if (words.front() != SPIRV_MAGIC)
	{
		Console.Error("Baked shader blob is not valid SPIR-V (magic %08X)", words.front());
		return std::nullopt;
	}

	return words;
}

bool VKBakedShaderPack::LookupKey::operator==(const LookupKey& other) const
{
	return (source_hash_low == other.source_hash_low && source_hash_high == other.source_hash_high &&
			source_length == other.source_length && shader_type == other.shader_type);
}

std::size_t VKBakedShaderPack::LookupKeyHasher::operator()(const LookupKey& key) const noexcept
{
	std::size_t h = 0;
	HashCombine(h, key.source_hash_low, key.source_hash_high, key.source_length, key.shader_type);
	return h;
}
