// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "common/Pcsx2Defs.h"
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
#include "VuAotPersist.h"
#include "VuAotCompileObserve.h"
#endif
struct microVU;
struct microProgram;
struct microBlock;
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
	// Baseline control links keep immutable private targets while recording.
	void* CanonicalStaticTarget(const void* target);
	// Internal tails preserve active branch-delay VI backup labels.
	void* PrivateStaticTarget(microVU& mvu, u32 pc, const void* state);
	void RememberStaticBranch(microVU& mvu, microProgram* owner, void* site, const void* privateTarget);
	void FinishBaselineCompile(microVU& mvu);
	void RememberHydratedBlock(microVU& mvu, microProgram* owner, microBlock* block, u32 pc, const void* state);
	// Owner-only framed body hook; never publishes into an active compiler.
	bool CompileBody(microVU& mvu, const vuaot::MvuBlockTrace& trace);
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
		u64 readyDeferred = 0, readyPublished = 0, readyStale = 0, readyEvicted = 0, readyDuplicates = 0;
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
		u64 automaticJobs = 0;
		u64 dispatchSlowKeys = 0, dispatchFastEntries = 0;
		u64 quietSaveAttempts = 0, quietSaveQueues = 0;
#endif
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
	u64 InlineBodyEntryCount(u32 pc);
	u64 NativeRegionEntryCount(u32 pc);
	u64 CurrentOwnerForTest();
	u32 BoundStaticLinksForTest();
	bool TestStaticLinkWords(bool restore);
	bool TestCompileWarmRoot(u32 pc, const void* key);
	bool ReadDispatchCadenceForTest(u32& runs, u32& jumpRuns, u32& bucket);
	void SetCompileObservationForTest(bool enabled);
	vuaot::observe::Snapshot GetCompileObservationForTest();
	u64 RetainedCompileObservationBytesForTest();
	void TestDirtyQuietSave();
	bool QuietSaveDirtyForTest();
	u32 TestFillQuietWorkerQueue();
	void TestClearQuietWorkerQueue();
	u32 SavedPlanCountForTest();
	u64 BailCount();
    void CountEntries(bool on);
    void SetTierUpRuns(u32 runs);
	void SetJumpTierUpRuns(u32 runs);
    void HoldWorker(bool hold);
    void WaitWorker();
    u32 PendingCount();
	u32 ReadyCountForTest();
	u64 ReadyBytesForTest();
	void TestReadyLimits(u64 bytes, u32 jobs, u32 perProgram);
	void TestHoldReadyPublication(bool hold);
	bool TestDuplicateReady();
	bool TestStaleReady(u32 kind); // 0: epoch; 1: captured CPU configuration
	bool TestExpandReadyOwner(bool observed);
	bool TestRetryReadySnapshot();
	bool TestReadyBodyExit(u32 mode, u32& targetPC); // conflict live / change unread snapshot / unrelated live word
#endif
} // namespace mvu_aot
