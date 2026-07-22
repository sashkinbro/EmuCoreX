// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "microVU-arm64.h"
#include "JitProfiler.h"

#include <unordered_set>

#include "common/AlignedMalloc.h"
#include "common/Perf.h"
#include "common/StringUtil.h"
#include "arm64/OaknutHelpers-arm64.h"
#include "GS/GSXXH.h"

alignas(64) vuRegistersPack g_vuRegistersPack;
VU_Thread& vu1Thread = g_vuRegistersPack.vu1Thread;

//------------------------------------------------------------------
// Micro VU - Main Functions
//------------------------------------------------------------------

// Only run this once per VU! ;)
void mVUinit(microVU& mVU, uint vuIndex)
{
	std::memset(&mVU.prog, 0, sizeof(mVU.prog));

	mVU.index        =  vuIndex;
	mVU.cop2         =  0;
	mVU.vuMemSize    = (mVU.index ? 0x4000 : 0x1000);
	mVU.microMemSize = (mVU.index ? 0x4000 : 0x1000);
	mVU.progSize     = (mVU.index ? 0x4000 : 0x1000) / 4;
	mVU.progMemMask  =  mVU.progSize-1;
	mVU.cache        = vuIndex ? SysMemory::GetVU1Rec() : SysMemory::GetVU0Rec();
	mVU.prog.x86end  = (vuIndex ? SysMemory::GetVU1RecEnd() : SysMemory::GetVU0RecEnd()) - (mVUcacheSafeZone * _1mb);

	mVU.regAlloc.reset(new microRegAlloc(mVU.index));
}

// Resets Rec Data
void mVUreset(microVU& mVU, bool resetReserve)
{

	if (THREAD_VU1)
	{
		DevCon.Warning("mVU Reset");
		// If MTVU is toggled on during gameplay we need to flush the running VU1 program, else it gets in a mess
		if (VU0.VI[REG_VPU_STAT].UL & 0x100)
		{
			CpuVU1->Execute(vu1RunCycles);
		}
		VU0.VI[REG_VPU_STAT].UL &= ~0x100;
	}
	u8* const old_start = mVU.prog.x86start;
	u8* const old_high_water = mVU.prog.x86ptr;
	const u64 discarded_host_bytes = (old_start && old_high_water > old_start) ?
		static_cast<u64>(old_high_water - old_start) : 0;
	JitProfiler::RecordCodeCacheReset(mVU.index ? 3 : 2, discarded_host_bytes);

    oakSetAsmPtr(mVU.cache, mVU.index ? HostMemoryMap::mVU1recSize : HostMemoryMap::mVU0recSize);
    oakStartBlock();
    ////
	mVUdispatcherAB(mVU);
	mVUdispatcherCD(mVU);
	mVUGenerateWaitMTVU(mVU);
	mVUGenerateCopyPipelineState(mVU);
	mVUGenerateCompareState(mVU);
    ////
    mVU.prog.x86start = oakEndBlock();

	mVU.regs().nextBlockCycles = 0;
	memset(&mVU.prog.lpState, 0, sizeof(mVU.prog.lpState));

	// Program Variables
	mVU.prog.cleared  =  1;
	mVU.prog.isSame   = -1;
	mVU.prog.cur      = NULL;
	mVU.prog.total    =  0;
	mVU.prog.curFrame =  0;

	// Setup Dynarec Cache Limits for Each Program
//	mVU.prog.x86start = xGetAlignedCallTarget();
	mVU.prog.x86ptr   = mVU.prog.x86start;
	SysMemory::DiscardCodeCachePages(mVU.prog.x86ptr, old_high_water);

	// Program objects have one owner regardless of how many start-PC lists
	// reference them. Drifted programs are no longer hash-addressable, but stay
	// alive until reset because quick/list entries may still use their ranges.
	for (auto& entry : mVU.contentPrograms)
		mVUdeleteProg(mVU, entry.second);
	mVU.contentPrograms.clear();
	for (microProgram*& prog : mVU.orphanedPrograms)
		mVUdeleteProg(mVU, prog);
	mVU.orphanedPrograms.clear();

    u32 i, e = (mVU.progSize >> 1); // mVU.progSize / 2
	for ( i = 0; i < e; ++i)
	{
		if (!mVU.prog.prog[i])
		{
			mVU.prog.prog[i] = new std::deque<microProgram*>();
			continue;
		}
		mVU.prog.prog[i]->clear();
		mVU.prog.quick[i].block = nullptr;
		mVU.prog.quick[i].prog = nullptr;
	}
}

// Free Allocated Resources
void mVUclose(microVU& mVU)
{
	for (auto& entry : mVU.contentPrograms)
		mVUdeleteProg(mVU, entry.second);
	mVU.contentPrograms.clear();
	for (microProgram*& prog : mVU.orphanedPrograms)
		mVUdeleteProg(mVU, prog);
	mVU.orphanedPrograms.clear();

	// Per-PC lists are non-owning shells.
    u32 i, e = (mVU.progSize >> 1); // mVU.progSize / 2
	for (i = 0; i < e; ++i)
	{
		if (!mVU.prog.prog[i])
			continue;
		safe_delete(mVU.prog.prog[i]);
	}
}

static __fi bool mVUProgRangesOverlap(const microProgram* prog, u32 addr, u32 size)
{
	// Unknown coverage must be invalidated conservatively. A quick-cached
	// program should always have at least one range, but stale code is much
	// worse than an unnecessary cache miss if that invariant is ever broken.
	if (!prog || !prog->ranges || prog->ranges->empty())
		return true;

	const u64 write_start = addr;
	const u64 write_end = write_start + size;
	for (const microRange& range : *prog->ranges)
	{
		if (range.start < 0 || range.end <= range.start)
			return true;
		const u64 range_start = static_cast<u32>(range.start);
		const u64 range_end = static_cast<u32>(range.end);
		if (range_start < write_end && write_start < range_end)
			return true;
	}
	return false;
}

// Clears Block Data in specified range
__fi void mVUclear(mV, u32 addr, u32 size)
{
	++mVU.microMemWriteGeneration;

	if (doWholeProgCompare)
	{
		if (!mVU.prog.cleared)
		{
			mVU.prog.cleared = 1;
			std::memset(&mVU.prog.lpState, 0, sizeof(mVU.prog.lpState));
			std::memset(mVU.prog.quick, 0, (mVU.progSize >> 1) * sizeof(microProgramQuick));
		}
		return;
	}

	// Partial-program mode only invalidates entries whose compiled code can
	// observe the write. Unrelated boot/menu/gameplay programs remain on the
	// quick path instead of forcing another walk through an ever-growing list.
	for (u32 i = 0; i < (mVU.progSize >> 1); i++)
	{
		microProgramQuick& quick = mVU.prog.quick[i];
		if (!quick.prog)
			continue;
		if (mVUProgRangesOverlap(quick.prog, addr, size))
		{
			quick.block = nullptr;
			quick.prog = nullptr;
		}
	}

	// lpState is a carried entry-search key and becomes invalid after every
	// micro-memory write, including writes disjoint from cached code. Do not set
	// cleared here: surviving quick entries do not pass through mVUsearchProg(),
	// so the full-invalidation latch would remain stale on that path.
	std::memset(&mVU.prog.lpState, 0, sizeof(mVU.prog.lpState));
}

//------------------------------------------------------------------
// Micro VU - Private Functions
//------------------------------------------------------------------

// Deletes a program
__ri void mVUdeleteProg(microVU& mVU, microProgram*& prog)
{
    u32 i, e = (mVU.progSize >> 1); // mVU.progSize / 2
	for (i = 0; i < e; ++i)
	{
		safe_delete(prog->block[i]);
	}
	safe_delete(prog->ranges);
	safe_aligned_free(prog);
}

static __fi MvuContentKey mVUcomputeContentKey(const microVU& mVU, u32 startPC)
{
	const XXH128_hash_t hash = XXH3_128bits(mVU.regs().Micro, mVU.microMemSize);
	return {hash.low64, hash.high64, startPC};
}

static __fi void mVUcontentMapInsert(microVU& mVU, microProgram* prog)
{
	[[maybe_unused]] const bool inserted = mVU.contentPrograms.emplace(prog->contentKey, prog).second;
	pxAssert(inserted);
}

static __fi void mVUlistPushUnique(microProgramList* list, microProgram* prog)
{
	for (microProgram* existing : *list)
	{
		if (existing == prog)
			return;
	}
	list->push_front(prog);
}

// Creates a new Micro Program
__ri microProgram* mVUcreateProg(microVU& mVU, int startPC, const MvuContentKey& contentKey)
{
	auto* prog = (microProgram*)_aligned_malloc(sizeof(microProgram), 64);
	memset(prog, 0, sizeof(microProgram));
	prog->idx = mVU.prog.total++;
	prog->ranges = new std::deque<microRange>();
	prog->startPC = startPC;
	if(doWholeProgCompare)
		mVUcacheProg(mVU, *prog); // Cache Micro Program
	prog->contentKey = contentKey;
	prog->contentWriteGeneration = mVU.microMemWriteGeneration;
	prog->contentKeyValid = true;
	mVUcontentMapInsert(mVU, prog);
	return prog;
}

static __fi void mVUcontentMapRefreshOrEvict(microVU& mVU, microProgram& prog, const MvuContentKey& liveKey)
{
	auto it = mVU.contentPrograms.find(prog.contentKey);
	if (it != mVU.contentPrograms.end() && it->second == &prog)
	{
		mVU.contentPrograms.erase(it);

		const bool inserted = mVU.contentPrograms.emplace(liveKey, &prog).second;
		if (inserted)
		{
			prog.contentKey = liveKey;
			prog.contentWriteGeneration = mVU.microMemWriteGeneration;
			return;
		}

		// Another program already owns this exact full-memory image. Existing
		// per-PC lists may still reference this range-specialized variant.
		mVU.orphanedPrograms.push_back(&prog);
	}
	else
	{
		pxAssert(false);
	}
	prog.contentKeyValid = false;
}

// Caches Micro Program
__ri void mVUcacheProg(microVU& mVU, microProgram& prog)
{
	if (!doWholeProgCompare)
	{
		auto cmpOffset = [&](void* x) { return (u8*)x + mVUrange.start; };
		memcpy(cmpOffset(prog.data), cmpOffset(mVU.regs().Micro), (mVUrange.end - mVUrange.start));
	}
	else
	{
		if (!mVU.index)
			memcpy(prog.data, mVU.regs().Micro, 0x1000);
		else
			memcpy(prog.data, mVU.regs().Micro, 0x4000);
	}

	// A program can survive a write outside its compiled ranges. If it later
	// expands into the modified area, its cached ranges no longer represent the
	// full-memory image under which it was indexed. Remove that mixed instance
	// from hash lookup while keeping existing range/list references alive.
	if (prog.contentKeyValid && prog.contentWriteGeneration != mVU.microMemWriteGeneration)
	{
		const MvuContentKey liveKey = mVUcomputeContentKey(mVU, prog.startPC);
		if (!(liveKey == prog.contentKey))
			mVUcontentMapRefreshOrEvict(mVU, prog, liveKey);
		else
			prog.contentWriteGeneration = mVU.microMemWriteGeneration;
	}
}

// Generate Hash for partial program based on compiled ranges...
u64 mVUrangesHash(microVU& mVU, microProgram& prog)
{
	union
	{
		u64 v64;
		u32 v32[2];
	} hash = {0};

	std::deque<microRange>::const_iterator it(prog.ranges->begin());
    int i, s, e;
	for (; it != prog.ranges->end(); ++it)
	{
		if ((it[0].start < 0) || (it[0].end < 0))
		{
			DevCon.Error("microVU%d: Negative Range![%d][%d]", mVU.index, it[0].start, it[0].end);
		}
        s = it[0].start >> 2; // it[0].start / 4
        e = it[0].end >> 2;   // it[0].end / 4
		for (i = s; i < e; ++i)
		{
			hash.v32[0] -= prog.data[i];
			hash.v32[1] ^= prog.data[i];
		}
	}
	return hash.v64;
}

// Prints the ratio of unique programs to total programs
void mVUprintUniqueRatio(microVU& mVU)
{
	std::vector<u64> v;
    u32 pc, e = mProgSize >> 1; // mProgSize / 2
	for (pc = 0; pc < e; ++pc)
	{
		microProgramList* list = mVU.prog.prog[pc];
		if (!list)
			continue;
		auto it(list->begin());
		for (; it != list->end(); ++it)
		{
			v.push_back(mVUrangesHash(mVU, *it[0]));
		}
	}
	u32 total = v.size();
	sortVector(v);
	makeUnique(v);
	if (!total)
		return;
}

// Compare Cached microProgram to mVU.regs().Micro
__fi bool mVUcmpProg(microVU& mVU, microProgram& prog)
{
	if (doWholeProgCompare)
	{
		if (memcmp((u8*)prog.data, mVU.regs().Micro, mVU.microMemSize))
			return false;
	}
	else
	{
		for (const auto& range : *prog.ranges)
		{
#if defined(PCSX2_DEVBUILD) || defined(_DEBUG)
			if ((range.start < 0) || (range.end < 0))
				DevCon.Error("microVU%d: Negative Range![%d][%d]", mVU.index, range.start, range.end);
#endif
			auto cmpOffset = [&](void* x) { return (u8*)x + range.start; };

			if (memcmp(cmpOffset(prog.data), cmpOffset(mVU.regs().Micro), (range.end - range.start)))
				return false;
		}
	}
	mVU.prog.cleared = 0;
	mVU.prog.cur = &prog;
	mVU.prog.isSame = doWholeProgCompare ? 1 : -1;
	return true;
}

// Searches for Cached Micro Program and sets prog.cur to it (returns entry-point to program)
_mVUt __fi void* mVUsearchProg(u32 startPC, uptr pState)
{
	microVU& mVU = mVUx;

    u32 start_pc_8 = startPC >> 3; // startPC / 8
    u32 regs_start_pc_8 = mVU.regs().start_pc >> 3; // mVU.regs().start_pc / 8

	microProgramQuick& quick = mVU.prog.quick[regs_start_pc_8];
	microProgramList*  list  = mVU.prog.prog [regs_start_pc_8];

	if (!quick.prog) // If null, we need to search for new program
	{
		// An exact full-memory and entry-PC hit is O(1) and always safe. Keeping
		// entry PCs separate preserves microVU's indirect-jump partitioning and
		// prevents one giant program from being invalidated by every upload.
		const MvuContentKey liveKey = mVUcomputeContentKey(mVU, regs_start_pc_8);
		auto contentIt = mVU.contentPrograms.find(liveKey);
		if (contentIt != mVU.contentPrograms.end())
		{
			microProgram* shared = contentIt->second;
			mVUlistPushUnique(list, shared);
			mVU.prog.cleared = 0;
			mVU.prog.isSame = 1;
			mVU.prog.cur = shared;
			quick.prog = shared;
			quick.block = shared->block[start_pc_8];
			if (quick.block == nullptr)
				return mVUblockFetch(mVU, startPC, pState);
			return mVUentryGet(mVU, quick.block, startPC, pState);
		}

		auto it(list->begin());
		for (; it != list->end(); ++it)
		{
			bool b = mVUcmpProg(mVU, *it[0]);

			if (b)
			{
				quick.block = it[0]->block[start_pc_8];
				quick.prog  = it[0];
				list->erase(it);
				list->push_front(quick.prog);

				// Sanity check, in case for some reason the program compilation aborted half way through (JALR for example)
				if (quick.block == nullptr)
				{
					void* entryPoint = mVUblockFetch(mVU, startPC, pState);
					return entryPoint;
				}
				return mVUentryGet(mVU, quick.block, startPC, pState);
			}
		}

		// Hash only after the per-PC MRU list misses completely. Identical full
		// micro-memory images reached through another start PC can share the same
		// program and compile only the missing entry block.
		// If cleared and program not found, make a new program instance
		mVU.prog.cleared = 0;
		mVU.prog.isSame  = 1;
		mVU.prog.cur     = mVUcreateProg(mVU, regs_start_pc_8, liveKey);
		void* entryPoint = mVUblockFetch(mVU,  startPC, pState);
		quick.block      = mVU.prog.cur->block[start_pc_8];
		quick.prog       = mVU.prog.cur;
		list->push_front(mVU.prog.cur);
		//mVUprintUniqueRatio(mVU);
		return entryPoint;
	}

	// If list.quick, then we've already found and recompiled the program ;)
	mVU.prog.isSame = -1;
	mVU.prog.cur = quick.prog;
	// Because the VU's can now run in sections and not whole programs at once
	// we need to set the current block so it gets the right program back
	quick.block = mVU.prog.cur->block[start_pc_8];

	// Sanity check, in case for some reason the program compilation aborted half way through
	if (quick.block == nullptr)
	{
		void* entryPoint = mVUblockFetch(mVU, startPC, pState);
		return entryPoint;
	}
	return mVUentryGet(mVU, quick.block, startPC, pState);
}

//------------------------------------------------------------------
// recMicroVU0 / recMicroVU1
//------------------------------------------------------------------

recMicroVU0 CpuMicroVU0;
recMicroVU1 CpuMicroVU1;

recMicroVU0::recMicroVU0() { m_Idx = 0; IsInterpreter = false; }
recMicroVU1::recMicroVU1() { m_Idx = 1; IsInterpreter = false; }

void recMicroVU0::Reserve()
{
	mVUinit(microVU0, 0);
}
void recMicroVU1::Reserve()
{
	mVUinit(microVU1, 1);
	vu1Thread.Open();
}

void recMicroVU0::Shutdown()
{
	mVUclose(microVU0);
}
void recMicroVU1::Shutdown()
{
	if (vu1Thread.IsOpen())
		vu1Thread.WaitVU();
	mVUclose(microVU1);
}

void recMicroVU0::Reset()
{
	mVUreset(microVU0, true);
}

void recMicroVU0::Step()
{
}

void recMicroVU1::Reset()
{
	vu1Thread.WaitVU();
	vu1Thread.Get_MTVUChanges();
	mVUreset(microVU1, true);
}

void recMicroVU0::SetStartPC(u32 startPC)
{
	VU0.start_pc = startPC;
}

void recMicroVU0::Execute(u32 cycles)
{
	VU0.flags &= ~VUFLAG_MFLAGSET;

	if (!(VU0.VI[REG_VPU_STAT].UL & 1))
		return;

	VU0.VI[REG_TPC].UL <<= 3;

	((mVUrecCall)microVU0.startFunct)(VU0.VI[REG_TPC].UL, cycles);
	VU0.VI[REG_TPC].UL >>= 3;
	if (microVU0.regs().flags & 0x4)
	{
		microVU0.regs().flags &= ~0x4;
		hwIntcIrq(6);
	}
}

void recMicroVU1::SetStartPC(u32 startPC)
{
	VU1.start_pc = startPC;
}

void recMicroVU1::Step()
{
}

void recMicroVU1::Execute(u32 cycles)
{

	if (!THREAD_VU1)
	{
		if (!(VU0.VI[REG_VPU_STAT].UL & 0x100))
			return;
	}
	VU1.VI[REG_TPC].UL <<= 3;
	((mVUrecCall)microVU1.startFunct)(VU1.VI[REG_TPC].UL, cycles);
	VU1.VI[REG_TPC].UL >>= 3;
	if (microVU1.regs().flags & 0x4 && !THREAD_VU1)
	{
		microVU1.regs().flags &= ~0x4;
		hwIntcIrq(7);
	}
}

void recMicroVU0::Clear(u32 addr, u32 size)
{
	mVUclear(microVU0, addr, size);
}
void recMicroVU1::Clear(u32 addr, u32 size)
{
	mVUclear(microVU1, addr, size);
}

void recMicroVU1::ResumeXGkick()
{
	if (!(VU0.VI[REG_VPU_STAT].UL & 0x100))
		return;
	((mVUrecCallXG)microVU1.startFunctXG)();
}

bool SaveStateBase::vuJITFreeze()
{
	if (IsSaving())
		vu1Thread.WaitVU();

	Freeze(microVU0.prog.lpState);
	Freeze(microVU1.prog.lpState);
	return IsOkay();
}

static void VU_JitGetBlockProfiles(int vuIndex, std::vector<JitBlockProfile>& outBlocks)
{
	microVU& mVU = (vuIndex == 0) ? microVU0 : microVU1;
	std::unordered_set<microProgram*> visited_programs;
	for (int i = 0; i < mProgSizeHalf; i++)
	{
		microProgramList* programs = mVU.prog.prog[i];
		if (!programs) continue;

		for (microProgram* prog : *programs)
		{
			if (!prog || !visited_programs.insert(prog).second)
				continue;

			for (int j = 0; j < mProgSizeHalf; j++)
			{
				microBlockManager* mgr = prog->block[j];
				if (!mgr) continue;

				u32 variant_index = 0;
				auto traverseList = [&](microBlockLink* listHead, bool exact_list) {
					for (microBlockLink* link = listHead; link != nullptr; link = link->next)
					{
						microBlock& b = link->block;
						JitBlockProfile p;
						p.startpc = j * 8;
						p.size = b.guest_size;
						p.host_size = b.host_size;
						p.execution_count = b.execution_count;
						p.type = (vuIndex == 0) ? 2 : 3; // VU0 or VU1
						p.state_hash = b.pState.quick64[0];
						p.variant_index = variant_index++;
						p.flags = static_cast<u32>(b.pState.needExactMatch) |
						          (static_cast<u32>(b.pState.blockType) << 8) |
						          (exact_list ? (1u << 16) : 0u) |
						          (static_cast<u32>(b.pState.vi15v) << 17);
						outBlocks.push_back(p);
					}
				};
				traverseList(mgr->getQBlockList(), false);
				traverseList(mgr->getFBlockList(), true);
			}
		}
	}
}

void VU0_JitGetBlockProfiles(std::vector<JitBlockProfile>& outBlocks)
{
	VU_JitGetBlockProfiles(0, outBlocks);
}

void VU1_JitGetBlockProfiles(std::vector<JitBlockProfile>& outBlocks)
{
	VU_JitGetBlockProfiles(1, outBlocks);
}
