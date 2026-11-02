// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+
#include "VuAotSpanDump.h"

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)

#define XXH_STATIC_LINKING_ONLY 1
#define XXH_INLINE_ALL 1
#include "xxhash.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <time.h>
#include <string>

#if defined(__ANDROID__)
#include <android/log.h>
#include <sys/system_properties.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <process.h>
#include <windows.h>
#endif

namespace vuaot::spandump
{
	namespace
	{
		constexpr size_t kMaxNodesPerSpan = 512;
	constexpr size_t kPathCapacity = 4096;

	struct Recorder
	{
		FILE* binary = nullptr;
		FILE* manifest = nullptr;
		std::string directory;
		std::string binaryPath;
		std::string manifestPath;
		uint64_t processToken = 0;
		size_t binaryLimit = kDefaultBinaryLimit;
		size_t manifestLimit = kDefaultManifestLimit;
		uint32_t spanLimit = kDefaultSpanLimit;
		size_t binaryBytes = 0;
		size_t manifestBytes = 0;
		size_t binaryFileOffset = 0;
		size_t manifestFileOffset = 0;
		bool initialFileSizesLoaded = false;
		int64_t writeFailureAfter = -1;
		Stats stats;
	};

	Recorder& State()
	{
		static Recorder recorder;
		return recorder;
	}

	void CloseFiles(Recorder& state)
	{
		if (state.binary) { std::fclose(state.binary); state.binary = nullptr; }
		if (state.manifest) { std::fclose(state.manifest); state.manifest = nullptr; }
	}

	void ReportFailure(const char* reason)
	{
	#if defined(__ANDROID__)
		__android_log_print(ANDROID_LOG_ERROR, "EmuCoreX", "VU AOT span dump disabled for epoch: %s", reason);
	#else
		std::fprintf(stderr, "VU AOT span dump disabled for epoch: %s\n", reason);
	#endif
	}

	void FailEpoch(Recorder& state, const char* reason)
	{
		if (!state.stats.failedThisEpoch)
		{
			state.stats.failedThisEpoch = true;
			++state.stats.fileFailures;
			ReportFailure(reason);
		}
		state.stats.enabled = false;
		CloseFiles(state);
	}

	uint64_t MonotonicNs(Recorder& state)
	{
		++state.stats.clockReads;
	#if defined(_WIN32)
		return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	#else
		timespec value{};
		if (clock_gettime(CLOCK_MONOTONIC, &value) != 0 || value.tv_sec < 0 || value.tv_nsec < 0)
			return 0;
		return static_cast<uint64_t>(value.tv_sec) * 1000000000ULL + static_cast<uint64_t>(value.tv_nsec);
	#endif
	}

	uint32_t ProcessId()
	{
	#if defined(_WIN32)
		return static_cast<uint32_t>(_getpid());
	#else
		return static_cast<uint32_t>(getpid());
	#endif
	}

	uint32_t OwnerThreadId()
	{
	#if defined(__linux__)
		return static_cast<uint32_t>(syscall(SYS_gettid));
	#elif defined(_WIN32)
		return static_cast<uint32_t>(GetCurrentThreadId());
	#else
		return 0;
	#endif
	}

	std::string Hex64(uint64_t value)
	{
		char buffer[17]{};
		std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
		return buffer;
	}

	const char* KindName(KindOrigin origin)
	{
		switch (origin)
		{
			case KindOrigin::LiveRegion: return "live_region";
			case KindOrigin::LiveBody: return "live_body";
			case KindOrigin::ImportedUnknown: return "imported_unknown";
		}
		return "imported_unknown";
	}

	bool OpenFiles(Recorder& state, uint64_t timestamp)
	{
		if (state.binary && state.manifest)
			return true;
		if (state.directory.empty() || state.directory[0] != '/')
			return false;
		if (!state.processToken)
			state.processToken = timestamp;
		char binaryPath[kPathCapacity]{};
		char manifestPath[kPathCapacity]{};
		const int binaryPathLength = std::snprintf(binaryPath, sizeof(binaryPath), "%s/vu-aot-spans-%u-%llu.bin",
			state.directory.c_str(), ProcessId(), static_cast<unsigned long long>(state.processToken));
		const int manifestPathLength = std::snprintf(manifestPath, sizeof(manifestPath), "%s/vu-aot-spans-%u-%llu.jsonl",
			state.directory.c_str(), ProcessId(), static_cast<unsigned long long>(state.processToken));
		if (binaryPathLength <= 0 || manifestPathLength <= 0 || binaryPathLength >= static_cast<int>(sizeof(binaryPath)) ||
			manifestPathLength >= static_cast<int>(sizeof(manifestPath)))
			return false;
		state.binaryPath = binaryPath;
		state.manifestPath = manifestPath;
		state.binary = std::fopen(state.binaryPath.c_str(), "ab");
		state.manifest = std::fopen(state.manifestPath.c_str(), "ab");
		if (!state.binary || !state.manifest)
		{
			CloseFiles(state);
			return false;
		}
		if (std::fseek(state.binary, 0, SEEK_END) != 0)
		{
			CloseFiles(state);
			return false;
		}
		const long binaryLength = std::ftell(state.binary);
		if (binaryLength < 0 || std::fseek(state.manifest, 0, SEEK_END) != 0)
		{
			CloseFiles(state);
			return false;
		}
		const long manifestLength = std::ftell(state.manifest);
		if (manifestLength < 0)
		{
			CloseFiles(state);
			return false;
		}
		state.binaryFileOffset = static_cast<size_t>(binaryLength);
		state.manifestFileOffset = static_cast<size_t>(manifestLength);
		if (!state.initialFileSizesLoaded)
		{
			state.binaryBytes = state.binaryFileOffset;
			state.manifestBytes = state.manifestFileOffset;
			state.initialFileSizesLoaded = true;
		}
		else
		{
			state.binaryBytes = std::max(state.binaryBytes, state.binaryFileOffset);
			state.manifestBytes = std::max(state.manifestBytes, state.manifestFileOffset);
		}
		return true;
	}

	size_t WriteChunk(Recorder& state, FILE* file, const void* data, size_t bytes)
	{
		if (!bytes)
			return 0;
		if (state.writeFailureAfter >= 0)
		{
			const size_t allowance = static_cast<size_t>(state.writeFailureAfter);
			const size_t attempt = std::min(bytes, allowance);
			const size_t written = attempt ? std::fwrite(data, 1, attempt, file) : 0;
			state.writeFailureAfter -= static_cast<int64_t>(written);
			return written;
		}
		return std::fwrite(data, 1, bytes, file);
	}

	bool AppendManifest(Recorder& state, const std::string& row)
	{
		if (!state.manifest || row.empty() || state.manifestBytes > state.manifestLimit ||
			row.size() > state.manifestLimit - state.manifestBytes || state.manifestFileOffset > state.manifestLimit ||
			row.size() > state.manifestLimit - state.manifestFileOffset)
			return false;
		if (WriteChunk(state, state.manifest, row.data(), row.size()) != row.size() || std::fflush(state.manifest) != 0)
			return false;
		state.manifestBytes += row.size();
		state.manifestFileOffset += row.size();
		state.stats.manifestBytesWritten += row.size();
		return true;
	}

	void AppendOverflow(Recorder& state, uint64_t timestamp)
	{
		++state.stats.spansSkipped;
		if (!OpenFiles(state, timestamp))
		{
			FailEpoch(state, "cannot open output files");
			return;
		}
		const uint64_t sequence = ++state.stats.processSequence;
		std::string row = "{\"event\":\"overflow\",\"version\":1,\"pid\":" + std::to_string(ProcessId()) +
			",\"owner_tid\":" + std::to_string(OwnerThreadId()) + ",\"monotonic_ns\":" +
			std::to_string(timestamp) + ",\"sequence\":" + std::to_string(sequence) +
			",\"skipped_total\":" + std::to_string(state.stats.spansSkipped) + "}\n";
		if (!AppendManifest(state, row))
			FailEpoch(state, "manifest cap or write failure while recording overflow");
	}

	bool BuildSpanRow(const Recorder& state, const PublishedSpan& span, uint64_t timestamp,
		uint64_t sequence, uint64_t binaryOffset, std::string& row)
	{
		const auto* bytes = span.base;
		const uint64_t codeHash = XXH3_64bits(bytes, span.totalBytes);
		const uint64_t rootHash = XXH3_64bits(span.rootKey, kInternalKeyBytes);
		row = "{\"event\":\"span\",\"version\":1,\"pid\":" + std::to_string(ProcessId()) +
			",\"owner_tid\":" + std::to_string(OwnerThreadId()) + ",\"monotonic_ns\":" +
			std::to_string(timestamp) + ",\"sequence\":" + std::to_string(sequence) +
			",\"generation\":" + std::to_string(span.generation) + ",\"base\":\"0x" +
			Hex64(reinterpret_cast<uintptr_t>(span.base)) + "\",\"total_bytes\":" + std::to_string(span.totalBytes) +
			",\"binary_file\":\"" + state.binaryPath.substr(state.binaryPath.find_last_of('/') + 1) +
			"\",\"binary_offset\":" + std::to_string(binaryOffset) +
			",\"registered_code_bytes\":" + std::to_string(span.registeredCodeBytes) +
			",\"bridge_offset\":" + std::to_string(span.registeredCodeBytes) +
			",\"bridge_bytes\":" + std::to_string(span.totalBytes - span.registeredCodeBytes) +
			",\"span_hash\":\"xxh3_64:" + Hex64(codeHash) + "\",\"root_pc\":" +
			std::to_string(span.rootPC) + ",\"owner_ordinal\":" + std::to_string(span.ownerOrdinal) +
			",\"root_key_hash\":\"xxh3_64:" + Hex64(rootHash) + "\",\"micro_snapshot_hash\":\"xxh3_64:" +
			Hex64(span.microSnapshotHash) + "\",\"retier\":" + (span.retier ? "true" : "false") +
			",\"kind_origin\":\"" + KindName(span.kindOrigin) + "\",\"nodes\":[";
		for (size_t index = 0; index < span.nodeCount; index++)
		{
			const Node& node = span.nodes[index];
			if (index)
				row += ',';
			row += "{\"pc\":" + std::to_string(node.pc) + ",\"code_offset\":" +
				std::to_string(node.codeOffset) + ",\"entry_offset\":" + std::to_string(node.entryOffset) +
				",\"internal_key_hash\":\"xxh3_64:" + Hex64(XXH3_64bits(node.key, kInternalKeyBytes)) + "\"}";
			if (row.size() > kDefaultManifestLimit)
				return false;
		}
		row += "]}\n";
		return row.size() <= kDefaultManifestLimit;
	}

	} // namespace

	void BeginEpoch(bool enabled, const char* directory)
	{
		Recorder& state = State();
		state.stats.failedThisEpoch = false;
		state.stats.enabled = enabled && directory && directory[0] == '/';
		if (state.stats.enabled && state.directory != directory)
		{
			CloseFiles(state);
			state.directory = directory;
			state.binaryPath.clear();
			state.manifestPath.clear();
		}
	}

	void BeginEpochFromProperties()
	{
#if defined(__ANDROID__)
		char enabledValue[PROP_VALUE_MAX]{};
		char directory[PROP_VALUE_MAX]{};
		const bool enabled = __system_property_get("debug.emucorex.vu_aot_dump", enabledValue) > 0 && enabledValue[0] == '1';
		const bool hasDirectory = __system_property_get("debug.emucorex.perf_map_dir", directory) > 0 && directory[0] == '/';
		BeginEpoch(enabled && hasDirectory, hasDirectory ? directory : nullptr);
#else
		BeginEpoch(false, nullptr);
#endif
	}

	Result RecordPublishedSpan(const PublishedSpan& span)
	{
		Recorder& state = State();
		if (!state.stats.enabled || state.stats.failedThisEpoch)
			return Result::Disabled;
		if (!span.base || !span.totalBytes || !span.registeredCodeBytes || span.registeredCodeBytes > span.totalBytes ||
			!span.rootKey || span.nodeCount > kMaxNodesPerSpan || (span.nodeCount && !span.nodes))
		{
			++state.stats.rejectedRecords;
			return Result::Rejected;
		}
		for (size_t index = 0; index < span.nodeCount; index++)
		{
			const Node& node = span.nodes[index];
			if (!node.key || node.codeOffset >= span.registeredCodeBytes || node.entryOffset >= span.totalBytes)
			{
				++state.stats.rejectedRecords;
				return Result::Rejected;
			}
		}
		const uint64_t timestamp = MonotonicNs(state);
		if (!timestamp)
		{
			FailEpoch(state, "CLOCK_MONOTONIC failed");
			return Result::Failed;
		}
		if (state.stats.spansWritten >= state.spanLimit || state.binaryBytes > state.binaryLimit ||
			span.totalBytes > state.binaryLimit - state.binaryBytes)
		{
			AppendOverflow(state, timestamp);
			return state.stats.enabled ? Result::SkippedLimit : Result::Failed;
		}
		const bool fileCapsWereInitialized = state.initialFileSizesLoaded;
		if (!OpenFiles(state, timestamp))
		{
			FailEpoch(state, "cannot open output files");
			return Result::Failed;
		}
		if ((!fileCapsWereInitialized && (state.binaryBytes > state.binaryLimit ||
			span.totalBytes > state.binaryLimit - std::min(state.binaryBytes, state.binaryLimit))) ||
			state.manifestBytes >= state.manifestLimit || state.binaryFileOffset > state.binaryLimit ||
			span.totalBytes > state.binaryLimit - std::min(state.binaryFileOffset, state.binaryLimit))
		{
			AppendOverflow(state, timestamp);
			return state.stats.enabled ? Result::SkippedLimit : Result::Failed;
		}
		if (std::fseek(state.binary, 0, SEEK_END) != 0)
		{
			FailEpoch(state, "cannot seek binary output");
			return Result::Failed;
		}
		const long offset = std::ftell(state.binary);
		if (offset < 0)
		{
			FailEpoch(state, "cannot read binary output offset");
			return Result::Failed;
		}
		const uint64_t sequence = ++state.stats.processSequence;
		std::string row;
		if (!BuildSpanRow(state, span, timestamp, sequence, static_cast<uint64_t>(offset), row))
		{
			AppendOverflow(state, timestamp);
			return state.stats.enabled ? Result::SkippedLimit : Result::Failed;
		}
		if (state.manifestBytes > state.manifestLimit || row.size() > state.manifestLimit - state.manifestBytes)
		{
			AppendOverflow(state, timestamp);
			return state.stats.enabled ? Result::SkippedLimit : Result::Failed;
		}
		if (WriteChunk(state, state.binary, span.base, span.totalBytes) != span.totalBytes || std::fflush(state.binary) != 0)
		{
			FailEpoch(state, "short or failed executable-span write");
			return Result::Failed;
		}
		if (!AppendManifest(state, row))
		{
			FailEpoch(state, "manifest write failed after binary span write");
			return Result::Failed;
		}
		state.binaryBytes += span.totalBytes;
		state.binaryFileOffset += span.totalBytes;
		state.stats.codeBytesWritten += span.totalBytes;
		state.stats.lastBinaryOffset = static_cast<uint64_t>(offset);
		state.stats.lastSpanBytes = span.totalBytes;
		++state.stats.spansWritten;
		return Result::Written;
	}

	void DisableForCurrentEpoch(const char* reason) noexcept
	{
		FailEpoch(State(), reason ? reason : "diagnostic capture failed");
	}

	void EndGeneration(uint64_t generation)
	{
		Recorder& state = State();
		if (!state.stats.enabled || state.stats.failedThisEpoch)
			return;
		const uint64_t timestamp = MonotonicNs(state);
		if (!timestamp)
		{
			FailEpoch(state, "CLOCK_MONOTONIC failed during generation end");
			return;
		}
		if (!OpenFiles(state, timestamp))
		{
			FailEpoch(state, "cannot open output files for generation end");
			return;
		}
		const uint64_t sequence = ++state.stats.processSequence;
		std::string row = "{\"event\":\"end_generation\",\"version\":1,\"pid\":" + std::to_string(ProcessId()) +
			",\"owner_tid\":" + std::to_string(OwnerThreadId()) + ",\"monotonic_ns\":" +
			std::to_string(timestamp) + ",\"sequence\":" + std::to_string(sequence) +
			",\"generation\":" + std::to_string(generation) + "}\n";
		if (!AppendManifest(state, row))
		{
			FailEpoch(state, "manifest cap or write failure during generation end");
			return;
		}
		++state.stats.resetEventsWritten;
	}

	Stats GetStats()
	{
		return State().stats;
	}

	void ResetForTests()
	{
		Recorder& state = State();
		CloseFiles(state);
		state = Recorder{};
	}

	void SetLimitsForTests(size_t binaryBytes, size_t manifestBytes, uint32_t spanCount)
	{
		Recorder& state = State();
		state.binaryLimit = binaryBytes;
		state.manifestLimit = manifestBytes;
		state.spanLimit = spanCount;
	}

	void SetWriteFailureAfterBytesForTests(int64_t bytes)
	{
		State().writeFailureAfter = bytes;
	}

	std::string BinaryPathForTests()
	{
		return State().binaryPath;
	}

	std::string ManifestPathForTests()
	{
		return State().manifestPath;
	}
} // namespace vuaot::spandump

#endif // EMUCOREX_ENABLE_NATIVE_SELF_TESTS && !NDEBUG
