// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+
#pragma once

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)

#include <cstddef>
#include <cstdint>
#include <string>

namespace vuaot::spandump
{
	constexpr size_t kInternalKeyBytes = 96;
	constexpr size_t kDefaultBinaryLimit = 32u << 20;
	constexpr size_t kDefaultManifestLimit = 2u << 20;
	constexpr uint32_t kDefaultSpanLimit = 4096;

	enum class KindOrigin : uint8_t
	{
		LiveRegion,
		LiveBody,
		ImportedUnknown,
	};

	enum class Result : uint8_t
	{
		Disabled,
		Written,
		SkippedLimit,
		Rejected,
		Failed,
	};

	struct Node
	{
		uint32_t pc = 0;
		uint32_t codeOffset = 0;
		uint32_t entryOffset = 0;
		const uint8_t* key = nullptr; // exactly kInternalKeyBytes, retained only during the call.
	};

	struct PublishedSpan
	{
		// Final owner allocation after all Publish fixups and end wrappers; never pass worker job.code.
		const uint8_t* base = nullptr;
		size_t totalBytes = 0;
		size_t registeredCodeBytes = 0;
		uint32_t rootPC = 0;
		uint32_t ownerOrdinal = 0;
		uint64_t generation = 0;
		const uint8_t* rootKey = nullptr; // exactly kInternalKeyBytes.
		uint64_t microSnapshotHash = 0;
		bool retier = false;
		KindOrigin kindOrigin = KindOrigin::ImportedUnknown;
		const Node* nodes = nullptr;
		size_t nodeCount = 0;
	};

	struct Stats
	{
		uint64_t processSequence = 0;
		uint64_t clockReads = 0;
		uint64_t spansWritten = 0;
		uint64_t spansSkipped = 0;
		uint64_t codeBytesWritten = 0;
		uint64_t manifestBytesWritten = 0;
		uint64_t resetEventsWritten = 0;
		uint64_t rejectedRecords = 0;
		uint64_t fileFailures = 0;
		uint64_t lastBinaryOffset = 0;
		uint64_t lastSpanBytes = 0;
		bool enabled = false;
		bool failedThisEpoch = false;
	};

	// Called at owner initialization/reset. It reads the activation and destination properties
	// once per epoch; the disabled path in RecordPublishedSpan performs no I/O or clock reads.
	void BeginEpochFromProperties();
	void BeginEpoch(bool enabled, const char* directory);
	Result RecordPublishedSpan(const PublishedSpan& span);
	void DisableForCurrentEpoch(const char* reason) noexcept;
	// Call on the owner before Runtime::Reset frees any jobs in this generation.
	void EndGeneration(uint64_t generation);
	Stats GetStats();

	// Narrow test controls. They are absent from Release because this entire API is Debug/self-test only.
	void ResetForTests();
	void SetLimitsForTests(size_t binaryBytes, size_t manifestBytes, uint32_t spanCount);
	void SetWriteFailureAfterBytesForTests(int64_t bytes);
	std::string BinaryPathForTests();
	std::string ManifestPathForTests();
} // namespace vuaot::spandump

#endif // EMUCOREX_ENABLE_NATIVE_SELF_TESTS && !NDEBUG
