// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/HashCombine.h"
#include "common/Pcsx2Types.h"

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/// Read-only pack of pre-baked SPIR-V shipped with the application. Entries use
/// the same source-hash key as VKShaderCache, so a stale or mismatched pack can
/// only miss, never return the wrong binary.
///
/// Only touched from the GS thread, matching VKShaderCache's file access.
class VKBakedShaderPack
{
public:
	VKBakedShaderPack();
	~VKBakedShaderPack();

	VKBakedShaderPack(const VKBakedShaderPack&) = delete;
	VKBakedShaderPack& operator=(const VKBakedShaderPack&) = delete;

	/// Opens the pack from the resources directory, falling back to the writable
	/// cache directory so a development build can adb-push one. Missing packs are
	/// not an error; corrupt ones are logged and ignored.
	bool Open();
	void Close();

	bool IsOpen() const { return (m_blob_file != nullptr); }
	u32 GetEntryCount() const { return static_cast<u32>(m_index.size()); }

	std::optional<std::vector<u32>> Lookup(
		u64 source_hash_low, u64 source_hash_high, u32 source_length, u32 shader_type);

	static std::string GetIndexFileName();
	static std::string GetBlobFileName();

private:
	struct BlobReference
	{
		u32 blob_offset;
		u32 blob_size;
	};

	struct LookupKey
	{
		u64 source_hash_low;
		u64 source_hash_high;
		u32 source_length;
		u32 shader_type;

		bool operator==(const LookupKey& other) const;
	};

	struct LookupKeyHasher
	{
		std::size_t operator()(const LookupKey& key) const noexcept;
	};

	static std::string GetFallbackIndexFileName();
	static std::string GetFallbackBlobFileName();

	bool OpenFromFiles(const std::string& index_filename, const std::string& blob_filename);

	std::FILE* m_blob_file = nullptr;
	std::unordered_map<LookupKey, BlobReference, LookupKeyHasher> m_index;
};
