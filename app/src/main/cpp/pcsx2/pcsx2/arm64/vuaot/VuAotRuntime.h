// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "common/Pcsx2Defs.h"
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
#include "VuAotPersist.h"
#endif
struct microVU;
namespace vuaot
{
	struct MvuBlockTrace;
}
namespace mvu_aot
{
	bool Enabled();
	void Reset(microVU& mvu);
	void ObserveImage(microVU& mvu);
	void BeforeProgram(microVU& mvu);
	void Remember(microVU& mvu, const vuaot::MvuBlockTrace& trace);
	void* Dispatch(microVU& mvu, u32 pc, void* baselineEntry);
	void* AfterJump(microVU& mvu, u32 pc, const void* state, void* baselineEntry);
	void OnJumpMiss(microVU& mvu);
	struct Stats
	{
		u64 dispatches = 0, jumpEntries = 0, jumpTierUps = 0;
		u64 jumpResolverCalls = 0, bodyBlocks = 0;
		u64 jumpCellsInspected = 0, jumpCellsRelinked = 0;
		u64 stickyTrips = 0, stickyResets = 0, stickyDropped = 0, stickyRejected = 0;
		u64 targetsRecorded = 0, crossProgramTargets = 0, retierChecks = 0, retiers = 0;
		u64 savedRecords = 0, savedPlanJobs = 0;
		u64 singleDeclines = 0, timingDeclines = 0, unsupportedDeclines = 0, publishRejects = 0;
	};
	// Read on the VU owner, or with that owner quiescent.
	Stats GetStats();
	u32 CodegenModes();
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	bool CachedTargetsMatchForTest(u32 sourceA, u32 sourceB, u32 target);
	void TestRelinkJumpCaches(bool enabled);
	void SetPipelineForTest(const void* key, u32 viBackup);
	bool ReadSavedForTest(vuaot::persist::File& file);
	bool QueueSavedForTest(const vuaot::persist::File& file);
	void ForceSynchronous(bool enabled);
	void AllowStickyElisionForTest(bool enabled);
	u64 RegionCount();
	u64 EntryCount();
	u64 NativeEndCount();
	u64 BailCount();
    void CountEntries(bool on);
    void SetTierUpRuns(u32 runs);
	void SetJumpTierUpRuns(u32 runs);
    void HoldWorker(bool hold);
    void WaitWorker();
    u32 PendingCount();
#endif
} // namespace mvu_aot
