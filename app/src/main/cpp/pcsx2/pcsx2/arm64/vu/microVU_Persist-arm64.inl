// SPDX-FileCopyrightText: 2026 yaps2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
// Adapted for the EmuCoreX oaknut ABI from upstream aa52a1fdc7cd03fb02bd404ebaa9940944e7fc60.

// Implementation of microVU_Persist-arm64.h. Included at the bottom of
// microVU-arm64.cpp (same pattern as microVU_ProgCache-arm64.inl) so it sees
// the full microVU/microProgram/microBlock types plus the TU-local helpers
// (mVUopenCodeCache / mVUcloseCodeCache / mVUcreateProg / mVUcomputeProgramHash).

#include "arm64/vu/microVU_Persist-arm64.h"
#include "Memory.h"

#ifdef __linux__
#include <link.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#endif

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

thread_local OakAddressRecorder* oakAddressRecorder = nullptr;

namespace mVUPersist
{
    template<unsigned N> static bool FitsSigned(s64 value) { return value >= -(s64{1} << (N-1)) && value < (s64{1} << (N-1)); }
    static s64 PCDisplacement(const u8* at, u64 value) { return (static_cast<s64>(value) - static_cast<s64>(reinterpret_cast<uptr>(at))) / 4; }
    static void OpenHydration(microVU& vu) {
        oakSetAsmPtr(vu.prog.x86ptr, static_cast<size_t>(vu.prog.x86end-vu.prog.x86ptr)); oakStartBlock();
    }
    static void CloseHydration(microVU& vu) { vu.prog.x86ptr = oakEndBlock(); }
    static void EmitNop() { u32 word=0xd503201f; std::memcpy(oakGetCurrentCodePointer(), &word,4); oakAsm->set_offset(oakAsm->offset()+4); }
    static void CopyCode(const u8* bytes, size_t size) { std::memcpy(oakGetCurrentCodePointer(),bytes,size); oakAsm->set_offset(oakAsm->offset()+size); }

	enum : u8
	{
		kFormMovRun = 0, // MOVZ + MOVK×(width-1) over halfwords 0..width-1, one Rd
		kFormAdrp,       // ADRP page21; the page offset rides in the next ADD/ORR
		kFormRel26,      // B / BL imm26
		kFormRel19,      // B.cond / CBZ / CBNZ imm19
		kFormCount,
	};

	enum : u8
	{
		kTargetBlockObject = 0, // log.blocks[index]'s microBlock + offset (heap)
		kTargetBlockEntry,      // log.blocks[index]'s code entry
		kTargetStub,            // per-VU dispatcher stub, id = index
		kTargetImage,           // image base + offset (code, data, bss of the emulator module)
		kTargetData,            // data arena base + offset
		kTargetCount,
	};

	enum : u8 { kStubStartFunct, kStubExitFunct, kStubStartFunctXG, kStubExitFunctXG,
        kStubWaitMTVU, kStubCopyPLState, kStubResumeXG, kStubBudgetExit, kStubCount };

	struct PersistFixup
	{
		u32 codeOffset; // chunk-relative offset of the first patched insn
		u8 form;
		u8 target;
		u8 width; // kFormMovRun: instruction count (1..4)
		u8 _pad0;
		u32 index; // block index (block targets) or stub id
		u32 _pad1;
		s64 offset; // byte offset from the target's base (block object, image, data arena)
	};
	static_assert(sizeof(PersistFixup) == 24);

	static u32 FixupSpanInsns(const PersistFixup& f)
	{
		return (f.form == kFormMovRun) ? f.width : 1;
	}

	struct PersistChunk
	{
		std::vector<u8> code;
		std::vector<PersistFixup> fixups;
		const u8* liveBase = nullptr; // where this chunk runs in this process; not serialized
	};

	struct PersistBlockRec
	{
		u32 chunkIndex;
		u32 entryOffset; // within chunk
		u32 startPC; // microMem byte offset
		u32 hasJumpCache;
		microRegInfo pState;
		microRegInfo pStateEnd;
		microBlock* live; // manager's copy; not serialized
	};
} // namespace mVUPersist

// Global-scope definition (fwd-declared in the header so microProgram can
// hold a pointer without pulling the implementation types).
struct MvuPersistLog
{
	XXH128_hash_t options{}; // The options at emission, never the later reset/save configuration.
	std::vector<mVUPersist::PersistChunk> chunks;
	std::vector<mVUPersist::PersistBlockRec> blocks;
	// x86ptrStart → blocks[] index, for cross-chunk branch resolution. Keyed on
	// the manager's x86ptrStart (what armEmitJmp call sites actually target).
	std::unordered_map<const void*, u32> blockByEntry;
};

namespace mVUPersist
{
	static std::atomic<bool> s_recordingEnabled{false};
	// When true, mVUinit/mVUreset's SyncRecordingFromConfig is a no-op so the
	// unit-test harness keeps manual control of recording (ABI-digest pins,
	// in-process round-trips, disk round-trips). Default false = production,
	// where recording follows the program cache setting at every reset.
	static bool s_manualRecording = false;
	static u64 s_blockCompiles[2] = {};
	static Stats s_stats[2] = {};

	// Process-wide determinism kill switch for the offline tools (pcsx2-vurunner
	// --no-progcache). When set, both the on-disk program cache and emit-time
	// recording are forced off for the whole process, so a JIT-vs-interp diff run
	// is byte-reproducible. Replaces the former PCSX2_VU_PROGCACHE_DISABLE env var
	// — no env gate in production code. Default false = no effect.
	static std::atomic<bool> s_processDisabled{false};

	//------------------------------------------------------------------
	// Address classification
	//------------------------------------------------------------------

	// The two address regions a block may embed besides its own program's
	// blocks and the dispatcher stubs: the emulator's module image (its code,
	// data and bss; for Android that is the shared library, not the app
	// process) and the data arena. Each region moves as a unit under ASLR, so
	// an address in it is recorded as an offset from the region's base and
	// re-based at hydration. The bases are page-aligned, which keeps the low 12
	// bits an ADRP + ADD pair splits off unchanged by any move.
	struct Regions
	{
		uptr imgBegin = 0, imgEnd = 0;
		uptr dataBegin = 0, dataEnd = 0;
		uptr imageBase = 0; // imgBegin rounded down to 4KB
		uptr dataBase = 0;
		bool valid = false;
	};

#if defined(__linux__)
	struct ModuleSearch
	{
		uptr probe;
		uptr begin, end;
		bool found;
	};

	static int FindModuleCallback(dl_phdr_info* info, size_t, void* data)
	{
		ModuleSearch* ms = static_cast<ModuleSearch*>(data);
		uptr lo = ~static_cast<uptr>(0), hi = 0;
		bool contains = false;
		for (int i = 0; i < info->dlpi_phnum; i++)
		{
			const auto& ph = info->dlpi_phdr[i];
			if (ph.p_type != PT_LOAD || ph.p_memsz == 0)
				continue;
			const uptr b = static_cast<uptr>(info->dlpi_addr + ph.p_vaddr);
			const uptr e = b + static_cast<uptr>(ph.p_memsz);
			lo = std::min(lo, b);
			hi = std::max(hi, e);
			if (ms->probe >= b && ms->probe < e)
				contains = true;
		}
		if (!contains)
			return 0;
		ms->begin = lo;
		ms->end = hi;
		ms->found = true;
		return 1;
	}
#endif

	// The extent of the loaded module that holds `probe`: the union of its
	// PT_LOAD segments (bss included, as p_memsz) on Linux and Android, of its
	// mapped segments on Apple platforms.
	static bool ResolveModuleRange(const void* probe, uptr& out_begin, uptr& out_end)
	{
#if defined(__linux__)
		ModuleSearch ms = {reinterpret_cast<uptr>(probe), 0, 0, false};
		dl_iterate_phdr(FindModuleCallback, &ms);
		if (!ms.found || ms.end <= ms.begin)
			return false;
		out_begin = ms.begin;
		out_end = ms.end;
		return true;
#elif defined(__APPLE__)
		const uptr p = reinterpret_cast<uptr>(probe);
		const u32 count = _dyld_image_count();
		for (u32 img = 0; img < count; img++)
		{
			const mach_header_64* hdr =
				reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(img));
			if (!hdr || hdr->magic != MH_MAGIC_64)
				continue;
			const intptr_t slide = _dyld_get_image_vmaddr_slide(img);
			uptr begin = ~static_cast<uptr>(0), end = 0;
			const load_command* lc = reinterpret_cast<const load_command*>(hdr + 1);
			for (u32 i = 0; i < hdr->ncmds; i++)
			{
				if (lc->cmd == LC_SEGMENT_64)
				{
					const segment_command_64* seg = reinterpret_cast<const segment_command_64*>(lc);
					// __PAGEZERO is the unmapped 4 GB null-deref guard at vmaddr 0;
					// absorbing it would classify most of the address space as
					// image.
					if (seg->vmsize && std::strcmp(seg->segname, SEG_PAGEZERO) != 0)
					{
						begin = std::min(begin, static_cast<uptr>(seg->vmaddr) + slide);
						end = std::max(end, static_cast<uptr>(seg->vmaddr + seg->vmsize) + slide);
					}
				}
				lc = reinterpret_cast<const load_command*>(
					reinterpret_cast<const u8*>(lc) + lc->cmdsize);
			}
			if (end > begin && p >= begin && p < end)
			{
				out_begin = begin;
				out_end = end;
				return true;
			}
		}
		return false;
#elif defined(EMUCOREX_VU_PERSIST_HOST_TEST)
        out_begin = reinterpret_cast<uptr>(ImageArena); out_end = out_begin + sizeof(ImageArena); return true;
#else
		return false;
#endif
	}

	static const Regions& GetRegions()
	{
		static Regions r = []() {
			Regions out;
			// microVU0 lives in the module that holds the JIT and every C
			// helper and global it calls or addresses.
			if (!ResolveModuleRange(&microVU0, out.imgBegin, out.imgEnd))
			{
				DevCon.Warning("mVUPersist: could not resolve the emulator module's range — recording will mark all programs non-persistable");
				return out;
			}
			out.imageBase = out.imgBegin & ~static_cast<uptr>(0xFFF);
			out.dataBegin = reinterpret_cast<uptr>(SysMemory::GetDataPtr(0));
			out.dataEnd = out.dataBegin + HostMemoryMap::MainSize;
			out.dataBase = out.dataBegin & ~static_cast<uptr>(0xFFF);
			out.valid = true;
			DevCon.WriteLn("mVUPersist: emulator module %p-%p, data arena %p",
				reinterpret_cast<void*>(out.imgBegin), reinterpret_cast<void*>(out.imgEnd),
				reinterpret_cast<void*>(out.dataBegin));
			return out;
		}();
		return r;
	}

	// Which region an address lies in: kTargetImage, kTargetData, or
	// kTargetCount for neither.
	static u8 RegionOf(const void* addr)
	{
		const Regions& r = GetRegions();
		if (!r.valid)
			return kTargetCount;
		const uptr a = reinterpret_cast<uptr>(addr);
		if (a >= r.imgBegin && a < r.imgEnd)
			return kTargetImage;
		if (a >= r.dataBegin && a < r.dataEnd)
			return kTargetData;
		return kTargetCount;
	}

	static uptr RegionBase(u8 target)
	{
		const Regions& r = GetRegions();
		return (target == kTargetImage) ? r.imageBase : r.dataBase;
	}

	//------------------------------------------------------------------
	// Build identity
	//------------------------------------------------------------------

#if defined(__linux__)
	struct BuildIdSearch
	{
		uptr probe;
		std::vector<u8> id;
	};

	// The NT_GNU_BUILD_ID note of the module that holds `probe`, from its PT_NOTE segments.
	static int FindBuildIdCallback(dl_phdr_info* info, size_t, void* data)
	{
		BuildIdSearch* bs = static_cast<BuildIdSearch*>(data);
		bool contains = false;
		for (int i = 0; i < info->dlpi_phnum && !contains; i++)
		{
			const auto& ph = info->dlpi_phdr[i];
			const uptr b = static_cast<uptr>(info->dlpi_addr + ph.p_vaddr);
			contains = ph.p_type == PT_LOAD && bs->probe >= b && bs->probe < b + ph.p_memsz;
		}
		if (!contains)
			return 0;
		for (int i = 0; i < info->dlpi_phnum; i++)
		{
			const auto& ph = info->dlpi_phdr[i];
			if (ph.p_type != PT_NOTE)
				continue;
			const u8* p = reinterpret_cast<const u8*>(info->dlpi_addr + ph.p_vaddr);
			const u8* const end = p + ph.p_memsz;
			while (p + 12 <= end)
			{
				u32 namesz, descsz, type;
				std::memcpy(&namesz, p, 4);
				std::memcpy(&descsz, p + 4, 4);
				std::memcpy(&type, p + 8, 4);
				const u8* name = p + 12;
				const u8* desc = name + ((namesz + 3) & ~3u);
				const u8* next = desc + ((descsz + 3) & ~3u);
				if (next > end)
					break;
				if (type == 3 /* NT_GNU_BUILD_ID */ && namesz == 4 && std::memcmp(name, "GNU", 4) == 0 && descsz)
				{
					bs->id.assign(desc, desc + descsz);
					return 1;
				}
				p = next;
			}
		}
		return 1;
	}
#endif

	static std::vector<u8> ReadBuildIdBytes()
	{
#if defined(__linux__)
		BuildIdSearch bs{reinterpret_cast<uptr>(&microVU0), {}};
		dl_iterate_phdr(FindBuildIdCallback, &bs);
		if (!bs.id.empty())
			return bs.id;
#elif defined(__APPLE__)
		const uptr p = reinterpret_cast<uptr>(&microVU0);
		const u32 count = _dyld_image_count();
		for (u32 img = 0; img < count; img++)
		{
			const mach_header_64* hdr = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(img));
			if (!hdr || hdr->magic != MH_MAGIC_64)
				continue;
			uptr begin = 0, end = 0;
			if (!ResolveModuleRange(hdr, begin, end) || p < begin || p >= end)
				continue;
			const load_command* lc = reinterpret_cast<const load_command*>(hdr + 1);
			for (u32 i = 0; i < hdr->ncmds; i++)
			{
				if (lc->cmd == LC_UUID)
				{
					const uuid_command* u = reinterpret_cast<const uuid_command*>(lc);
					return std::vector<u8>(u->uuid, u->uuid + sizeof(u->uuid));
				}
				lc = reinterpret_cast<const load_command*>(reinterpret_cast<const u8*>(lc) + lc->cmdsize);
			}
		}
#endif
		static const char kWhen[] = __DATE__ " " __TIME__;
		return std::vector<u8>(kWhen, kWhen + sizeof(kWhen));
	}

#ifdef PCSX2_RECOMPILER_TESTS
	static bool s_buildIdOverride = false;
	static u64 s_buildIdOverrideLo = 0, s_buildIdOverrideHi = 0;
	void TestOverrideBuildId(bool on, u64 lo, u64 hi)
	{
		s_buildIdOverride = on;
		s_buildIdOverrideLo = lo;
		s_buildIdOverrideHi = hi;
	}
#endif

	void GetBuildId(u64& lo, u64& hi)
	{
#ifdef PCSX2_RECOMPILER_TESTS
		if (s_buildIdOverride)
		{
			lo = s_buildIdOverrideLo;
			hi = s_buildIdOverrideHi;
			return;
		}
#endif
		static const XXH128_hash_t id = []() {
			const std::vector<u8> bytes = ReadBuildIdBytes();
			return XXH3_128bits(bytes.data(), bytes.size());
		}();
		lo = id.low64;
		hi = id.high64;
	}

	static u64 BuildTag()
	{
		u64 lo, hi;
		GetBuildId(lo, hi);
		return lo ^ hi;
	}

    XXH128_hash_t OptionsSentinel(const microVU& vu) {
        std::vector<u64> words;
        auto add=[&](u64 value){words.push_back(value);};
        u64 lo,hi; GetBuildId(lo,hi); add(lo);add(hi);add(kMvuCompilerAbiVersion);
        add(sizeof(microVU));add(sizeof(microProgram));add(sizeof(microBlock));add(sizeof(microRegInfo));add(vu.index);
        add(doRegAlloc);add(noFlagOpts);add(doSFlagInsts);add(doMFlagInsts);add(doCFlagInsts);add(doBranchInDelaySlot);
        add(doConstProp);add(doJumpCaching);add(doJumpAsSameProgram);add(doDBitHandling);add(doWholeProgCompare);
        add(EmuConfig.Cpu.Recompiler.vu0Overflow);add(EmuConfig.Cpu.Recompiler.vu0ExtraOverflow);add(EmuConfig.Cpu.Recompiler.vu0SignOverflow);
        add(EmuConfig.Cpu.Recompiler.vu1Overflow);add(EmuConfig.Cpu.Recompiler.vu1ExtraOverflow);add(EmuConfig.Cpu.Recompiler.vu1SignOverflow);
        add(EmuConfig.Cpu.Recompiler.vu0Underflow);add(EmuConfig.Cpu.Recompiler.vu1Underflow);
        add(EmuConfig.Speedhacks.vuFlagHack);add(EmuConfig.Speedhacks.EECycleSkip);
        add(vu.index ? 0 : EmuConfig.Speedhacks.EECycleRate);add(THREAD_VU1);
        add(EmuConfig.Gamefixes.IbitHack);add(EmuConfig.Gamefixes.VUSyncHack);add(EmuConfig.Gamefixes.FullVU0SyncHack);
        add(EmuConfig.Gamefixes.VuAddSubHack);add(EmuConfig.Gamefixes.VUOverflowHack);add(EmuConfig.Gamefixes.XgKickHack);
        add(EmuConfig.Cpu.FPUFPCR.bitmask);add(EmuConfig.Cpu.VU0FPCR.bitmask);add(EmuConfig.Cpu.VU1FPCR.bitmask);
        add(IsRecordingEnabled());
        const auto& rewrites = mVUlocalRewriteOptions(); add(rewrites.enabled);add(rewrites.natural);add(rewrites.rule_mask);
        add(mVUSharedIrEnabled(vu.index));
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
        add(s_vuLocalNaturalEnabled);
#endif
        return XXH3_128bits(words.data(),words.size()*sizeof(u64));
    }

	//------------------------------------------------------------------
	// Dispatcher stub table
	//------------------------------------------------------------------

	static const void* StubAddress(microVU& mVU, u8 id)
    {
        const void* stubs[] = {mVU.startFunct,mVU.exitFunct,mVU.startFunctXG,mVU.exitFunctXG,
            mVU.waitMTVU,mVU.copyPLState,nullptr,mVU.budgetExitStub};
        // Slot 6 is reserved: resumePtrXG is a dynamic compiled continuation,
        // not a stable dispatcher stub that can be resolved in another run.
        return id < kStubCount ? stubs[id] : nullptr;
    }

	static bool ResolveStub(microVU& mVU, const void* target, u8& out_id)
	{
		for (u8 i = 0; i < kStubCount; i++)
		{
			if (StubAddress(mVU, i) == target && target)
			{
				out_id = i;
				return true;
			}
		}
		return false;
	}


	//------------------------------------------------------------------
	// Emit-time recorder
	//------------------------------------------------------------------

	class Recorder final : public OakAddressRecorder
	{
	public:
		struct EpisodeBlock
		{
			microBlock* live;
			u32 entryOffset;
			u32 startPC;
		};

		microVU* mvu = nullptr;
		u8* chunkBase = nullptr;
		microProgram* prog = nullptr;
		MvuPersistLog* log = nullptr;
		std::vector<PersistFixup> fixups;
		std::vector<EpisodeBlock> episodeBlocks;
		XXH128_hash_t episodeOptions{};
		bool active = false;
		bool failed = false;
		const char* failReason = nullptr;

		void Begin(microVU& m, u8* base)
		{
			pxAssert(!active);
			mvu = &m;
			episodeOptions = OptionsSentinel(m);
			chunkBase = base;
			prog = nullptr;
			log = nullptr;
			fixups.clear();
			episodeBlocks.clear();
			active = true;
			failed = false;
			failReason = nullptr;
		}

		void Fail(const char* reason)
		{
			if (!failed)
			{
				failed = true;
				failReason = reason;
			}
		}

		bool InSlab(const void* addr) const
		{
			// Slab extends to the PHYSICAL rec-region end, mVUcacheSafeZone
			// past the x86end reset threshold — compile sessions may
			// legitimately overshoot x86end into the safe zone, and chunks
			// recorded there still reference intra-slab targets.
			const u8* a = static_cast<const u8*>(addr);
			return a >= mvu->cache && a < mvu->prog.x86end + (mVUcacheSafeZone * _1mb);
		}

		bool InChunk(const void* addr) const
		{
			const u8* a = static_cast<const u8*>(addr);
			// A patched forward branch commonly targets the current cursor,
			// immediately before its destination instruction is emitted. The
			// final independent audit still requires the target to be < end.
			return a >= chunkBase && a <= oakGetCurrentCodePointer();
		}

		u32 OffsetOf(u8* at) const
		{
			pxAssert(at >= chunkBase);
			return static_cast<u32>(at - chunkBase);
		}

		// Index a block of this episode will get once committed.
		u32 ProvisionalIndex(size_t episode_pos) const
		{
			return static_cast<u32>((log ? log->blocks.size() : 0) + episode_pos);
		}

		// --- OakAddressRecorder ---

		// The episode block whose microBlock object holds `addr`, or -1.
		int EpisodeBlockHolding(const void* addr) const
		{
			const u8* a = static_cast<const u8*>(addr);
			for (size_t i = 0; i < episodeBlocks.size(); i++)
			{
				const u8* blk = reinterpret_cast<const u8*>(episodeBlocks[i].live);
				if (a >= blk && a < blk + sizeof(microBlock))
					return static_cast<int>(i);
			}
			return -1;
		}

		AddressClass ClassifyAddress(const void* addr) override
		{
			AddressClass c;
			if (EpisodeBlockHolding(addr) >= 0)
			{
				// Heap: the whole address moves, low bits included, so no ADRP.
				// The run is as wide as this pointer needs (two halfwords for a
				// non-PIE brk heap below 4GB, three for a PIE heap or a thread's
				// mmap arena, four for an Android tagged pointer). A process
				// whose allocator hands out a wider pointer for the same program
				// is rare (the heap a VU compiles on is fixed by the build and by
				// MTVU, which is in the options sentinel); hydration then
				// discards the program and it recompiles.
				c.relocatable = true;
				c.allowAdrp = false;
				c.minMovWidth = 1;
				return c;
			}
			if (RegionOf(addr) != kTargetCount)
			{
				c.relocatable = true;
				c.allowAdrp = true;
				c.minMovWidth = 1;
				return c;
			}
			if (InSlab(addr))
				Fail("address-of-code materialized outside branch helpers");
			else
				Fail("unclassifiable pointer materialized (heap?)");
			return c;
		}

		void OnMovRun(u8* at, u32 width, const void* addr) override
		{
			PersistFixup f = {};
			f.codeOffset = OffsetOf(at);
			f.form = kFormMovRun;
			f.width = static_cast<u8>(width);
			const int bi = EpisodeBlockHolding(addr);
			if (bi >= 0)
			{
				f.target = kTargetBlockObject;
				f.index = ProvisionalIndex(static_cast<size_t>(bi));
				f.offset = static_cast<const u8*>(addr) - reinterpret_cast<const u8*>(episodeBlocks[bi].live);
				fixups.push_back(f);
				return;
			}
			const u8 region = RegionOf(addr);
			if (region != kTargetCount)
			{
				f.target = region;
				f.offset = static_cast<s64>(reinterpret_cast<uptr>(addr) - RegionBase(region));
				fixups.push_back(f);
				return;
			}
			Fail("move target lost between classify and emit");
		}

		void OnAdrp(u8* at, const void* addr) override
		{
			const u8 region = RegionOf(addr);
			if (region != kTargetCount)
			{
				PersistFixup f = {};
				f.codeOffset = OffsetOf(at);
				f.form = kFormAdrp;
				f.target = region;
				f.offset = static_cast<s64>(reinterpret_cast<uptr>(addr) - RegionBase(region));
				fixups.push_back(f);
				return;
			}
			Fail("ADRP to an address outside the image and data arena");
		}

		void OnBranch(u8* at, const void* target, BranchForm form) override
		{
			// Branches may first target a slow path, then be patched after a
			// recursive compile. Record the final target exactly once. A final
			// intra-chunk target also removes the obsolete external fixup.
			const u32 offset = OffsetOf(at);
			fixups.erase(std::remove_if(fixups.begin(), fixups.end(), [offset](const PersistFixup& f) {
				return f.codeOffset == offset && (f.form == kFormRel26 || f.form == kFormRel19);
			}), fixups.end());
			if (InChunk(target))
				return; // relative distance preserved on relocation

			PersistFixup f = {};
			f.codeOffset = OffsetOf(at);
			f.form = (form == BranchForm::Rel19) ? kFormRel19 : kFormRel26;

			u8 stub_id = 0;
			if (ResolveStub(*mvu, target, stub_id))
			{
				f.target = kTargetStub;
				f.index = stub_id;
				fixups.push_back(f);
				return;
			}

			if (log)
			{
				const auto it = log->blockByEntry.find(target);
				if (it != log->blockByEntry.end())
				{
					f.target = kTargetBlockEntry;
					f.index = it->second;
					fixups.push_back(f);
					return;
				}
			}

			const u8 region = RegionOf(target);
			if (region != kTargetCount)
			{
				f.target = region;
				f.offset = static_cast<s64>(reinterpret_cast<uptr>(target) - RegionBase(region));
				fixups.push_back(f);
				return;
			}

			Fail(InSlab(target) ? "direct branch to unrecorded slab target"
			                    : "direct branch outside the slab, image and data arena");
		}

		// --- episode lifecycle ---

		void RegisterBlock(microBlock* block, u8* entry, u32 startPC_bytes)
		{
			if (failed)
				return;

			microProgram* cur = mvu->prog.cur;
			if (!cur)
				return;
            // Region publication owns a separate sidecar and never invokes this baseline hook.

			if (!prog)
			{
				prog = cur;
				if (!prog->persist) {
                    prog->persist = new MvuPersistLog();
                    prog->persist->options = episodeOptions;
                }
                log = prog->persist;
                if (log->options.low64 != episodeOptions.low64 || log->options.high64 != episodeOptions.high64) {
                    Fail("program compiled under different options"); return;
                }
			}
			else if (prog != cur)
			{
				// mVUsearchProg switched programs mid-episode (JR/JALR slow
				// path). Splitting the chunk at the switch is possible but
				// not worth it — drop the episode.
				Fail("program switch mid-episode");
				return;
			}

			episodeBlocks.push_back({block, OffsetOf(entry), startPC_bytes});
		}

        // Independent decoding catches a helper that emitted an address without notifying us.
        // Oaknut has no literal pools in these episodes. Unknown external encodings are refused.
        void Audit(u8* end)
        {
            const size_t size=static_cast<size_t>(end-chunkBase);
            if (size&3) { Fail("unaligned episode");return; }
            std::vector<u8> covered(size/4,0);
            for (const auto& f:fixups) {
                const size_t span=FixupSpanInsns(f)*4;
                if ((f.codeOffset&3) || static_cast<u64>(f.codeOffset)+span>size) {Fail("fixup outside episode");return;}
                for(size_t off=f.codeOffset;off<f.codeOffset+span;off+=4) {
                    if(covered[off/4]) {Fail("overlapping fixups");return;}
                    covered[off/4]=1;
                }
            }
            const auto in_chunk=[&](u64 value){return value>=reinterpret_cast<uptr>(chunkBase)&&value<reinterpret_cast<uptr>(end);};
            const auto signed_bits=[](u64 v,unsigned bits)->s64 {return static_cast<s64>(v ^ (u64{1}<<(bits-1)))-static_cast<s64>(u64{1}<<(bits-1));};
            const auto address=[&](u64 v) {
                const void* ptr=reinterpret_cast<const void*>(v);
                if(RegionOf(ptr)!=kTargetCount || InSlab(ptr) || EpisodeBlockHolding(ptr)>=0)return true;
                if(log)for(const auto& b:log->blocks) {const auto begin=reinterpret_cast<uptr>(b.live);if(v>=begin&&v<begin+sizeof(microBlock))return true;}
                return false;
            };
            for(size_t off=0;off<size;off+=4) {
                if(covered[off/4])continue;
                u32 word;std::memcpy(&word,chunkBase+off,4);
                const u64 pc=reinterpret_cast<uptr>(chunkBase+off);
                bool relative=false;u64 target=0;
                if((word&0x7c000000u)==0x14000000u) {relative=true;target=pc+signed_bits(word&0x03ffffffu,26)*4;}
                else if((word&0xff000010u)==0x54000000u || (word&0x7e000000u)==0x34000000u) {relative=true;target=pc+signed_bits((word>>5)&0x7ffffu,19)*4;}
                else if((word&0x7e000000u)==0x36000000u) {relative=true;target=pc+signed_bits((word>>5)&0x3fffu,14)*4;}
                else if((word&0x9f000000u)==0x10000000u) {relative=true;target=pc+signed_bits(((word>>5)&0x7ffffu)*4+((word>>29)&3u),21);}
                else if((word&0x3b000000u)==0x18000000u) {relative=true;target=pc+signed_bits((word>>5)&0x7ffffu,19)*4;}
                else if((word&0x9f000000u)==0x90000000u) {Fail("unrecorded ADRP");return;}
                if(relative && !in_chunk(target)) {Fail("unrecorded external relative address");return;}
                const u32 op=word&0xff800000u;
                if(op==0xd2800000u || op==0x92800000u) {
                    const unsigned shift=((word>>21)&3)*16;
                    u64 value=u64{(word>>5)&65535}<<shift;
                    if(op==0x92800000u)value=~value;
                    const u32 rd=word&31;
                    size_t next=off+4;
                    for(;next<size&&!covered[next/4];next+=4) {
                        u32 part;std::memcpy(&part,chunkBase+next,4);
                        if((part&0xff800000u)!=0xf2800000u || (part&31)!=rd)break;
                        const unsigned bit=((part>>21)&3)*16;
                        value=(value&~(u64{65535}<<bit))|(u64{(part>>5)&65535}<<bit);
                    }
                    if(address(value)) {Fail("unrecorded immediate address");return;}
                    off=next-4;
                }
            }
        }

		void End(u8* chunkEnd)
		{
			pxAssert(active);
			active = false;

			const u32 vu = mvu->index & 1;
			if (episodeBlocks.empty() && fixups.empty())
				return; // nothing emitted (pure lookup episode / hydration)

			if (!failed) Audit(chunkEnd);
			if (failed || episodeBlocks.empty() || !log)
			{
				s_stats[vu].chunksDropped++;
				if (failed)
					DevCon.WriteLn("mVUPersist: VU%u chunk dropped (%s)", vu, failReason);
				return;
			}

			// add() may have deduped a block against an existing variant (the
			// manager then keeps the OLD entry and this episode's copy of the
			// code is unreachable). Skipping just that record would shift the
			// provisional indices already baked into block-object fixups —
			// drop the whole episode instead. Rare corner, conservative.
			for (const EpisodeBlock& eb : episodeBlocks)
			{
				if (eb.live->x86ptrStart != chunkBase + eb.entryOffset)
				{
					s_stats[vu].chunksDropped++;
					DevCon.WriteLn("mVUPersist: VU%u chunk dropped (block manager deduped a variant)", vu);
					return;
				}
			}

			const u32 chunkIndex = static_cast<u32>(log->chunks.size());
			for (const EpisodeBlock& eb : episodeBlocks)
			{
				PersistBlockRec rec = {};
				rec.chunkIndex = chunkIndex;
				rec.entryOffset = eb.entryOffset;
				rec.startPC = eb.startPC;
				rec.hasJumpCache = (eb.live->jumpCache != nullptr) ? 1 : 0;
				std::memcpy(&rec.pState, &eb.live->pState, sizeof(microRegInfo));
				std::memcpy(&rec.pStateEnd, &eb.live->pStateEnd, sizeof(microRegInfo));
				rec.live = eb.live;
				log->blockByEntry.emplace(eb.live->x86ptrStart, static_cast<u32>(log->blocks.size()));
				log->blocks.push_back(rec);
				s_stats[vu].blocksRecorded++;
			}

			PersistChunk chunk;
			chunk.code.assign(chunkBase, chunkEnd);
			chunk.fixups = std::move(fixups);
			chunk.liveBase = chunkBase;
			s_stats[vu].fixupsRecorded += chunk.fixups.size();
			log->chunks.push_back(std::move(chunk));
			s_stats[vu].chunksRecorded++;

		}
	};

	// VU0 episodes run on the EE thread, VU1 on the MTVU thread — same
	// affinity as oakAsm itself, so per-VU recorder objects are safe.
	static Recorder s_recorder[2];

	void SetRecordingEnabled(bool enabled)
	{
		// The offline-tooling determinism gate trumps every enable request:
		// recording changes emitted code forms, and SetProcessDisable(true)
		// (pcsx2-vurunner --no-progcache) promises a byte-reproducible JIT.
		if (enabled && s_processDisabled)
			enabled = false;
		if (enabled == s_recordingEnabled)
			return;
		s_recordingEnabled = enabled;

        // OptionsSentinel reads this atomically; callers reset the VU owners before changing options.

	}
	bool IsRecordingEnabled() { return s_recordingEnabled.load(std::memory_order_relaxed) && !IsProcessDisabled(); }

	void SetProcessDisable(bool disable) { s_processDisabled.store(disable,std::memory_order_relaxed); }
	bool IsProcessDisabled() { return s_processDisabled; }

	void SyncRecordingFromConfig(bool config_enabled)
	{
		// Production: recording follows the program cache setting, called from
		// mVUinit/mVUreset before the options sentinel is rebuilt. The
		// test-manual override lets the recompiler-test harness drive
		// recording itself without these reset-time syncs clobbering it.
		if (s_manualRecording)
			return;
		const bool was = s_recordingEnabled;
		SetRecordingEnabled(config_enabled);
		if (s_recordingEnabled != was)
		{
			// One line per state change — the authoritative signal that the
			// persisted-JIT cache will (or won't) write .vuprog payloads this
			// session. Cheap to grep in emulog when diagnosing a "no payloads"
			// report. DevCon so it stays out of production logs but remains
			// greppable in Devel / raised-loglevel during bring-up.
			DevCon.WriteLn(Color_StrongBlue,
				"mVUProgCache: persisted-JIT recording %s (VuJitProgramCache=%d)",
				s_recordingEnabled ? "ENABLED" : "disabled",
				config_enabled ? 1 : 0);
		}
	}

	void SetTestManualRecording(bool manual) { s_manualRecording = manual; }

	void BeginEpisode(microVU& mVU, u8* chunkBase)
	{
		if (!IsRecordingEnabled())
			return;
		Recorder& r = s_recorder[mVU.index & 1];
		r.Begin(mVU, chunkBase);
		oakAddressRecorder = &r;
	}

	void EndEpisode(microVU& mVU, u8* chunkStart, u8* chunkEnd)
	{
		s_stats[mVU.index & 1].emittedCodeBytes += static_cast<u64>(chunkEnd - chunkStart);
		Recorder& r = s_recorder[mVU.index & 1];
		if (!r.active)
			return;
		pxAssert(oakAddressRecorder == &r);
		oakAddressRecorder = nullptr;
		r.End(chunkEnd);
	}

	void OnBlockCompiled(microVU& mVU, microBlock* block, u8* entry, u32 startPC_bytes)
	{
		const u32 vu = mVU.index & 1;
		s_blockCompiles[vu]++;
		Recorder& r = s_recorder[vu];
		if (r.active && oakAddressRecorder == &r)
			r.RegisterBlock(block, entry, startPC_bytes);
	}

    void MarkUnpersistable(microVU& vu, const char* reason) { auto& r=s_recorder[vu.index&1]; if(r.active)r.Fail(reason); }

	void OnProgramDeleted(microProgram& prog)
	{
		delete prog.persist;
		prog.persist = nullptr;
	}

	//------------------------------------------------------------------
	// Serialization
	//------------------------------------------------------------------

	static constexpr u32 kImageMagic = 0x5055564Du; // 'MVUP'
	// 2: fixups as form + target, image and data-arena addresses recorded as
	//    region offsets; the checksum covers the header.
	// 3: the header names the build that recorded the payload (buildTag). An
	//    image fixup is an offset into the emulator module, which any other
	//    build lays out differently, so a payload is only good for the build
	//    that made it.
	static constexpr u32 kImageVersion = 4;

	struct ImageHeader
	{
		u32 magic;
		u32 version;
		u32 vuIndex;
		u32 flags;
		u64 hashLo;
		u64 hashHi;
		// The recording process's region bases. Informational only: every
		// address in the payload is a fixup resolved against the hydrating
		// process, so none of these has to match.
		u64 imageBase;
		u64 dataBase;
		u64 codeArena;
		u32 progStartPC; // microMem byte offset of the creating entry
		u32 rangeCount;
		u32 blockCount;
		u32 chunkCount;
		u64 payloadHash; // XXH3-64 of the whole image with this field zeroed —
		                 // guards the on-disk .vuprog against bit rot / partial
		                 // writes the structural checks can't see (a flipped
		                 // CODE byte is valid structure but wrong code)
		u64 buildTag;    // BuildTag() of the recording build
		u64 buildLo, buildHi;
		u32 abiVersion, regInfoSize, blockSize, reserved;
		XXH128_hash_t options;
	};
	static_assert(sizeof(ImageHeader) == 136);

	struct DiskRange
	{
		s32 start;
		s32 end;
	};

	struct DiskBlock
	{
		u32 chunkIndex;
		u32 entryOffset;
		u32 startPC;
		u32 hasJumpCache;
		u8 pState[sizeof(microRegInfo)];
		u8 pStateEnd[sizeof(microRegInfo)];
	};

	struct DiskChunkHeader
	{
		u32 codeSize;
		u32 fixupCount;
	};

	template <typename T>
	static void AppendPod(std::vector<u8>& out, const T& v)
	{
		const u8* p = reinterpret_cast<const u8*>(&v);
		out.insert(out.end(), p, p + sizeof(T));
	}

	bool SerializeProgram(microVU& mVU, const microProgram& prog, std::vector<u8>& out)
	{
		const MvuPersistLog* log = prog.persist;
		if (!log || log->blocks.empty() || !prog.contentKeyValid)
			return false;

		out.clear();
		ImageHeader hdr = {};
		hdr.magic = kImageMagic;
		hdr.version = kImageVersion;
		hdr.vuIndex = mVU.index;
		hdr.hashLo = prog.contentKey.low64;
		hdr.hashHi = prog.contentKey.high64;
		hdr.imageBase = GetRegions().imageBase;
		hdr.dataBase = GetRegions().dataBase;
		hdr.codeArena = reinterpret_cast<u64>(SysMemory::GetCodePtr(0));
		hdr.buildTag = BuildTag();
		GetBuildId(hdr.buildLo, hdr.buildHi);
		hdr.abiVersion = kMvuCompilerAbiVersion;
		hdr.regInfoSize = sizeof(microRegInfo);
		hdr.blockSize = sizeof(microBlock);
		hdr.options = log->options;
		hdr.progStartPC = static_cast<u32>(prog.startPC) * 8u;
		hdr.rangeCount = prog.ranges ? static_cast<u32>(prog.ranges->size()) : 0;
		hdr.blockCount = static_cast<u32>(log->blocks.size());
		hdr.chunkCount = static_cast<u32>(log->chunks.size());
		AppendPod(out, hdr);

		if (prog.ranges)
		{
			for (const microRange& mr : *prog.ranges)
				AppendPod(out, DiskRange{mr.start, mr.end});
		}

		for (const PersistBlockRec& rec : log->blocks)
		{
			DiskBlock db = {};
			db.chunkIndex = rec.chunkIndex;
			db.entryOffset = rec.entryOffset;
			db.startPC = rec.startPC;
			db.hasJumpCache = rec.hasJumpCache;
			std::memcpy(db.pState, &rec.pState, sizeof(microRegInfo));
			std::memcpy(db.pStateEnd, &rec.pStateEnd, sizeof(microRegInfo));
			AppendPod(out, db);
		}

		for (const PersistChunk& chunk : log->chunks)
		{
			AppendPod(out, DiskChunkHeader{static_cast<u32>(chunk.code.size()),
				static_cast<u32>(chunk.fixups.size())});
			for (const PersistFixup& f : chunk.fixups)
				AppendPod(out, f);
			out.insert(out.end(), chunk.code.begin(), chunk.code.end());
		}

		// Stamp the checksum into the already-appended header (computed with
		// the field still zero).
		const u64 payload_hash = XXH3_64bits(out.data(), out.size());
		std::memcpy(out.data() + offsetof(ImageHeader, payloadHash),
			&payload_hash, sizeof(payload_hash));
		return true;
	}

	static u64 ShapeOf(u32 ranges, u32 blocks, u32 chunks)
	{
		const u32 words[3] = {ranges, blocks, chunks};
		return XXH3_64bits(words, sizeof(words)) | 1u; // (never 0, which means "none")
	}

	u64 LogShape(const microProgram& prog)
	{
		const MvuPersistLog* log = prog.persist;
		if (!log || log->blocks.empty() || !prog.contentKeyValid)
			return 0;
		return ShapeOf(prog.ranges ? static_cast<u32>(prog.ranges->size()) : 0, static_cast<u32>(log->blocks.size()),
			static_cast<u32>(log->chunks.size()));
	}

	u64 ImageShape(const u8* data, size_t size)
	{
		if (!data || size < sizeof(ImageHeader))
			return 0;
		ImageHeader hdr;
		std::memcpy(&hdr, data, sizeof(hdr));
		if (hdr.magic != kImageMagic || hdr.version != kImageVersion)
			return 0;
		return ShapeOf(hdr.rangeCount, hdr.blockCount, hdr.chunkCount);
	}

	// Checksum of a serialized image as SerializeProgram computed it.
	static u64 ImageChecksum(const u8* data, size_t size)
	{
		std::vector<u8> copy(data, data + size);
		std::memset(copy.data() + offsetof(ImageHeader, payloadHash), 0, sizeof(u64));
		return XXH3_64bits(copy.data(), copy.size());
	}

	//------------------------------------------------------------------
	// Instruction patchers
	//------------------------------------------------------------------

	enum class PatchResult
	{
		Ok,
		BadEncoding, // the code at the site is not the form the fixup names
		OutOfRange,  // the target cannot be encoded from this placement
	};

	// Encode `value` into the fixup's instruction(s). `at` is the address the
	// code executes from (the PC for relative forms); `insn` is where its words
	// are read and written (the RW alias under iOS dual mapping). With `write`
	// false it only checks, so a placement can be validated before anything is
	// installed.
	static PatchResult PatchSite(u32* insn, const u8* at, const PersistFixup& f, u64 value, bool write)
    {
        if ((f.form == kFormRel26 || f.form == kFormRel19) &&
            ((reinterpret_cast<uptr>(at) | value) & 3)) return PatchResult::OutOfRange;
        if (f.form == kFormMovRun && (f.width < 1 || f.width > 4)) return PatchResult::BadEncoding;
		switch (f.form)
		{
			case kFormMovRun:
			{
				// movz xN, #imm16 (hw=0), then movk xN, #imm16, lsl 16*i, one Rd.
				const u32 w = f.width;
				const u32 rd = insn[0] & 31u;
				for (u32 i = 0; i < w; i++)
				{
					const u32 expect_op = (i == 0) ? 0xD2800000u : 0xF2800000u;
					if ((insn[i] & 0xFF800000u) != expect_op || ((insn[i] >> 21) & 3u) != i || (insn[i] & 31u) != rd)
						return PatchResult::BadEncoding;
				}
				if (w < 4 && (value >> (16 * w)) != 0)
					return PatchResult::OutOfRange;
				if (write)
				{
					for (u32 i = 0; i < w; i++)
						insn[i] = (insn[i] & ~(0xFFFFu << 5)) | (static_cast<u32>((value >> (16 * i)) & 0xFFFFu) << 5);
				}
				return PatchResult::Ok;
			}
			case kFormAdrp:
			{
				// ADRP = 0x90000000 | immlo[30:29] | immhi[23:5].
				if ((*insn & 0x9F000000u) != 0x90000000u)
					return PatchResult::BadEncoding;
				const s64 page_disp = static_cast<s64>(value >> 12) -
				                      static_cast<s64>(reinterpret_cast<uptr>(at) >> 12);
				if (!FitsSigned<21>(page_disp))
					return PatchResult::OutOfRange;
				if (write)
				{
					const u32 immlo = static_cast<u32>(page_disp) & 3u;
					const u32 immhi = (static_cast<u32>(page_disp) >> 2) & 0x7FFFFu;
					*insn = (*insn & 0x9F00001Fu) | (immlo << 29) | (immhi << 5);
				}
				return PatchResult::Ok;
			}
			case kFormRel26:
			{
				// B = 0x14000000, BL = 0x94000000; imm26 in [25:0].
				if ((*insn & 0x7C000000u) != 0x14000000u)
					return PatchResult::BadEncoding;
				const s64 disp = PCDisplacement(at, value);
				if (!FitsSigned<26>(disp))
					return PatchResult::OutOfRange;
				if (write)
					*insn = (*insn & 0xFC000000u) | (static_cast<u32>(disp) & 0x03FFFFFFu);
				return PatchResult::Ok;
			}
			case kFormRel19:
			{
				// B.cond (0x54000000, bit 4 clear) or CBZ/CBNZ (0x34000000); imm19 in [23:5].
				const bool bcond = (*insn & 0xFF000010u) == 0x54000000u;
				const bool cbz = (*insn & 0x7E000000u) == 0x34000000u;
				if (!bcond && !cbz)
					return PatchResult::BadEncoding;
				const s64 disp = PCDisplacement(at, value);
				if (!FitsSigned<19>(disp))
					return PatchResult::OutOfRange;
				if (write)
					*insn = (*insn & ~(0x7FFFFu << 5)) | ((static_cast<u32>(disp) & 0x7FFFFu) << 5);
				return PatchResult::Ok;
			}
			default:
				return PatchResult::BadEncoding;
		}
	}

	//------------------------------------------------------------------
	// Hydration
	//------------------------------------------------------------------

#ifdef PCSX2_RECOMPILER_TESTS
	// Placement of the most recent hydration, for TestVerifyHydratedCode.
	// Test-only state — never written in a hooks-off Release build.
	static std::vector<u8*> s_lastHydratedChunkBase[2];
#endif

	template <typename T>
	static bool ReadPod(const u8*& p, const u8* end, T& out)
	{
		if (static_cast<size_t>(end - p) < sizeof(T))
			return false;
		std::memcpy(&out, p, sizeof(T));
		p += sizeof(T);
		return true;
	}

	// Take a just-created program back out: it is in the content map only
	// (the caller has not pushed it into any per-PC list yet). Its code stays
	// in the slab, unreferenced, until the next cache reset.
	static void DiscardHydratedProgram(microVU& mVU, microProgram*& prog)
	{
		const auto it = mVU.contentPrograms.find(prog->contentKey);
		if (it != mVU.contentPrograms.end() && it->second == prog)
			mVU.contentPrograms.erase(it);
		mVUdeleteProg(mVU, prog);
		prog = nullptr;
	}

	microProgram* HydrateProgram(microVU& mVU, const u8* data, size_t size)
	{
		const u32 vu = mVU.index & 1;
#ifdef PCSX2_RECOMPILER_TESTS
		s_lastHydratedChunkBase[vu].clear();
#endif
		if (!data || size < sizeof(ImageHeader) || size > (256u << 20)) return nullptr;
		const u8* p = data;
		const u8* const end = data + size;

		ImageHeader hdr;
		if (!ReadPod(p, end, hdr) || hdr.magic != kImageMagic || hdr.version != kImageVersion ||
			hdr.vuIndex != mVU.index)
		{
			s_stats[vu].hydrationRejects++;
			return nullptr;
		}

		// A payload from another build: its image fixups are offsets into that build's module, so
		// patching them here would call whatever sits at those offsets in this one.
		u64 buildLo, buildHi; GetBuildId(buildLo, buildHi);
		const auto options = OptionsSentinel(mVU);
		if (hdr.buildLo != buildLo || hdr.buildHi != buildHi || hdr.abiVersion != kMvuCompilerAbiVersion ||
			hdr.regInfoSize != sizeof(microRegInfo) || hdr.blockSize != sizeof(microBlock) ||
			hdr.options.low64 != options.low64 || hdr.options.high64 != options.high64)
		{
			DevCon.Warning("mVUPersist: VU%u hydration rejected — payload recorded by another build", vu);
			s_stats[vu].hydrationRejects++;
			return nullptr;
		}

		// Image integrity. The structural checks below validate shape, not
		// content — a flipped code byte parses fine and runs wrong.
		if (ImageChecksum(data, size) != hdr.payloadHash)
		{
			DevCon.Warning("mVUPersist: VU%u hydration rejected — payload checksum mismatch", vu);
			s_stats[vu].hydrationRejects++;
			return nullptr;
		}

		if (!GetRegions().valid)
		{
			s_stats[vu].hydrationRejects++;
			return nullptr;
		}

		// Identity: the image must describe the program bytes currently in
		// micro memory.
		// The image stores byte PCs; the local content map partitions by
		// micro-instruction pair index, matching mVUsearchProg/createProg.
		const MvuContentKey live_key = mVUcomputeContentKey(mVU, hdr.progStartPC / 8);
		const XXH128_hash_t live_hash{live_key.low64, live_key.high64};
		if (hdr.hashLo != live_hash.low64 || hdr.hashHi != live_hash.high64)
		{
			s_stats[vu].hydrationRejects++;
			return nullptr;
		}

		if (!hdr.blockCount || !hdr.chunkCount || hdr.progStartPC >= mVU.microMemSize || (hdr.progStartPC & 7) ||
			hdr.rangeCount > size / sizeof(DiskRange) || hdr.blockCount > size / sizeof(DiskBlock) || hdr.chunkCount > size / sizeof(DiskChunkHeader)) return nullptr;
		std::vector<DiskRange> ranges(hdr.rangeCount);
		for (DiskRange& r : ranges)
			if (!ReadPod(p, end, r))
				return nullptr;

		std::vector<DiskBlock> blocks(hdr.blockCount);
		for (DiskBlock& b : blocks)
			if (!ReadPod(p, end, b))
				return nullptr;

		struct ParsedChunk
		{
			const u8* code;
			u32 codeSize;
			std::vector<PersistFixup> fixups;
		};
		std::vector<ParsedChunk> chunks(hdr.chunkCount);
		size_t total_code = 0;
		for (ParsedChunk& c : chunks)
		{
			DiskChunkHeader ch;
			if (!ReadPod(p, end, ch))
				return nullptr;
			if (!ch.codeSize || (ch.codeSize & 3) || ch.fixupCount > static_cast<size_t>(end-p)/sizeof(PersistFixup)) return nullptr;
			c.fixups.resize(ch.fixupCount);
			for (PersistFixup& f : c.fixups)
				if (!ReadPod(p, end, f))
					return nullptr;
			if (static_cast<size_t>(end - p) < ch.codeSize)
				return nullptr;
			c.code = p;
			c.codeSize = ch.codeSize;
			p += ch.codeSize;
			total_code += ch.codeSize + 16;
		}

		if (p != end) return nullptr;
		for (const DiskRange& range : ranges)
			if (range.start < 0 || range.end <= range.start || static_cast<u32>(range.end) > mVU.microMemSize) return nullptr;

		// Structural validation before any side effects.
		for (const DiskBlock& b : blocks)
		{
			if (b.chunkIndex >= chunks.size() || b.startPC >= mVU.microMemSize ||
				(b.startPC & 7) || (b.entryOffset & 3) || b.entryOffset >= chunks[b.chunkIndex].codeSize)
				return nullptr;
		}
		for (const ParsedChunk& c : chunks)
		{
			for (const PersistFixup& f : c.fixups)
			{
				if (f.form >= kFormCount || f.target >= kTargetCount)
					return nullptr;
				if (f.form == kFormMovRun && (f.width < 1 || f.width > 4))
					return nullptr;
				if ((f.codeOffset & 3) || static_cast<u64>(f.codeOffset) + FixupSpanInsns(f) * 4 > c.codeSize)
					return nullptr;
				if ((f.target == kTargetBlockObject || f.target == kTargetBlockEntry) &&
					f.index >= blocks.size())
					return nullptr;
				if (f.target == kTargetBlockObject && (f.offset < 0 || f.offset >= static_cast<s64>(sizeof(microBlock))))
					return nullptr;
				if (f.target == kTargetStub && (f.index >= kStubCount || !StubAddress(mVU, static_cast<u8>(f.index))))
					return nullptr;
			}
		}

		// Capacity: refuse rather than trip the mid-hydration cache-full reset.
		// Signed math: x86ptr may legitimately sit PAST x86end when an earlier
		// session in this execution overshot into the safe zone — an unsigned
		// subtraction would wrap huge and let the hydration through instead of
		// refusing it.
		const ptrdiff_t cache_free = mVU.prog.x86end - mVU.prog.x86ptr;
		if (cache_free < 0 ||
			total_code + (mVUcacheSafeZone * _1mb) > static_cast<size_t>(cache_free))
		{
			s_stats[vu].hydrationRejects++;
			return nullptr;
		}

        if (mVU.contentPrograms.count(live_key)) return nullptr;
        struct CursorTransaction {
            microVU& vu; u8* cursor; int total; bool committed=false;
            ~CursorTransaction() { if (!committed) { if (oakHasBlock()) CloseHydration(vu); vu.prog.x86ptr=cursor; vu.prog.total=total; } }
        } transaction{mVU,mVU.prog.x86ptr,static_cast<int>(mVU.prog.total)};
        pxAssert(!oakAsm); // caller must not have an open emission episode
		OpenHydration(mVU);

		// Where each chunk will land: the copy loop below pads the cursor
		// offset (from x86start, which need not itself be aligned) to 16 bytes
		// before each chunk, exactly as here.
		std::vector<u8*> chunk_base(chunks.size());
		{
			ptrdiff_t cursor = oakAsm->offset();
			for (size_t i = 0; i < chunks.size(); i++)
			{
				cursor = (cursor + 15) & ~static_cast<ptrdiff_t>(15);
				chunk_base[i] = oakGetAsmPtr() + cursor;
				cursor += chunks[i].codeSize;
			}
		}

		// Every target except a block object (heap, known only once the blocks
		// are registered) resolves now, against this process.
		auto resolve = [&](const PersistFixup& f, u64& out) -> bool {
			switch (f.target)
			{
				case kTargetBlockEntry:
				{
					const DiskBlock& b = blocks[f.index];
					out = reinterpret_cast<u64>(chunk_base[b.chunkIndex] + b.entryOffset);
					return true;
				}
				case kTargetStub:
					out = reinterpret_cast<u64>(StubAddress(mVU, static_cast<u8>(f.index)));
					return true;
				case kTargetImage:
				case kTargetData:
					out = static_cast<u64>(RegionBase(f.target)) + static_cast<u64>(f.offset);
                    return f.offset >= 0 && RegionOf(reinterpret_cast<const void*>(out)) == f.target;
				default:
					return false;
			}
		};

		// Validate every placement-dependent encoding against the original
		// bytes before anything is installed: a target out of reach (a far data
		// arena for an ADRP, a narrower MOVZ/MOVK run than a new address needs)
		// rejects the payload cleanly.
		for (size_t i = 0; i < chunks.size(); i++)
		{
			for (const PersistFixup& f : chunks[i].fixups)
			{
				u64 value = 0;
				if (!resolve(f, value)) { if (f.target == kTargetBlockObject) continue; CloseHydration(mVU); return nullptr; }
				u32 words[4] = {};
				std::memcpy(words, chunks[i].code + f.codeOffset, FixupSpanInsns(f) * 4);
				const PatchResult r = PatchSite(words, chunk_base[i] + f.codeOffset, f, value, false);
				if (r != PatchResult::Ok)
				{
					DevCon.Warning("mVUPersist: VU%u hydration rejected — fixup (form %u target %u) %s at the new placement",
						vu, f.form, f.target, (r == PatchResult::OutOfRange) ? "out of range" : "does not match its code");
					CloseHydration(mVU);
					s_stats[vu].hydrationRejects++;
					return nullptr;
				}
			}
		}

		microProgram* prog = mVUcreateProg(mVU, static_cast<int>(hdr.progStartPC / 8), live_key);

		// Restore compiled ranges + the range-compare image (mVUcmpProg under
		// !doWholeProgCompare diffs prog.data inside these windows; the live
		// microMem is identity-verified above, so copying from it is exact).
		prog->ranges->clear();
		for (const DiskRange& r : ranges)
		{
			prog->ranges->push_back(microRange{r.start, r.end});
			if (!doWholeProgCompare && r.start >= 0 && r.end > r.start &&
				static_cast<u32>(r.end) <= mVU.microMemSize)
			{
				std::memcpy(reinterpret_cast<u8*>(prog->data) + r.start,
					mVU.regs().Micro + r.start, static_cast<size_t>(r.end - r.start));
			}
		}

		// Copy each chunk to the current cursor (16-aligned, as recorded).
		for (size_t i = 0; i < chunks.size(); i++)
		{
			while (oakAsm->offset() & 15)
				EmitNop();
			pxAssertRel(oakGetCurrentCodePointer() == chunk_base[i],
				"mVUPersist: hydration placement differs from the validated one");
			CopyCode(chunks[i].code, chunks[i].codeSize);
		}

		// Register every block with the program's managers. add() dedupes by
		// pState, mirroring compile-time behavior.
		std::vector<microBlock*> final_block(blocks.size());
		for (size_t i = 0; i < blocks.size(); i++)
		{
			const DiskBlock& b = blocks[i];
			const u32 slot = b.startPC / 8;
			if (!prog->block[slot])
				prog->block[slot] = new microBlockManager();

			microBlock tmp{};
			std::memcpy(&tmp.pState, b.pState, sizeof(microRegInfo));
			std::memcpy(&tmp.pStateEnd, b.pStateEnd, sizeof(microRegInfo));
			tmp.x86ptrStart = chunk_base[b.chunkIndex] + b.entryOffset;
			tmp.jumpCache = nullptr;

			microBlock* installed = prog->block[slot]->add(mVU, &tmp);
			// mVUcompileJIT dereferences jumpCache unguarded (it was allocated
			// at compile time by normJumpCompile) — restore it eagerly.
			if (b.hasJumpCache && !installed->jumpCache)
				installed->jumpCache = new microJumpCache[mProgSize / 2];
			final_block[i] = installed;
		}

		// Patch fixups directly in the slab (still inside the BeginCodeWrite
		// window; mVUcloseCodeCache flushes the icache over the whole span).
		// Nothing can run these blocks yet: the program is reachable only
		// through the content map, from this thread.
		for (size_t i = 0; i < chunks.size(); i++)
		{
			for (const PersistFixup& f : chunks[i].fixups)
			{
				u8* at = chunk_base[i] + f.codeOffset;
				// iOS dual-map W^X: `at` is the RX address the displacement math
				// must use; the store itself goes through the RW alias. Identity
				// on every other platform.
				u32* insn = reinterpret_cast<u32*>(at);
				u64 value = 0;
				if (f.target == kTargetBlockObject)
					value = reinterpret_cast<u64>(final_block[f.index]) + static_cast<u64>(f.offset);
				else if (f.target == kTargetBlockEntry)
					value = reinterpret_cast<u64>(final_block[f.index]->x86ptrStart);
				else
					resolve(f, value);

				const PatchResult r = PatchSite(insn, at, f, value, true);
				if (r != PatchResult::Ok)
				{
					// Only a block object can fail here (the rest was validated
					// above): an allocator that hands out a pointer wider than the
					// recorded run. Take the program back out and recompile.
					DevCon.Warning("mVUPersist: VU%u hydration rejected — block pointer %p does not fit a %u-instruction run",
						vu, reinterpret_cast<void*>(value), f.width);
					DiscardHydratedProgram(mVU, prog);
					CloseHydration(mVU);
					s_stats[vu].hydrationRejects++;
					return nullptr;
				}
			}
		}

		CloseHydration(mVU);

		// Rebuild the persist log so the hydrated program can grow new chunks
		// and be re-serialized. The recorded bytes (not the patched ones) are
		// kept: every address operand in them is a fixup, resolved afresh by
		// whichever process hydrates the re-saved payload.
		delete prog->persist;
		prog->persist = new MvuPersistLog();
		prog->persist->options = hdr.options;
		MvuPersistLog* log = prog->persist;
		for (size_t i = 0; i < chunks.size(); i++)
		{
			PersistChunk pc;
			pc.code.assign(chunks[i].code, chunks[i].code + chunks[i].codeSize);
			pc.fixups = chunks[i].fixups;
			pc.liveBase = chunk_base[i];
			log->chunks.push_back(std::move(pc));
		}
		for (size_t i = 0; i < blocks.size(); i++)
		{
			PersistBlockRec rec = {};
			rec.chunkIndex = blocks[i].chunkIndex;
			rec.entryOffset = blocks[i].entryOffset;
			rec.startPC = blocks[i].startPC;
			rec.hasJumpCache = blocks[i].hasJumpCache;
			std::memcpy(&rec.pState, blocks[i].pState, sizeof(microRegInfo));
			std::memcpy(&rec.pStateEnd, blocks[i].pStateEnd, sizeof(microRegInfo));
			rec.live = final_block[i];
			log->blockByEntry.emplace(final_block[i]->x86ptrStart, static_cast<u32>(i));
			log->blocks.push_back(rec);
		}


#ifdef PCSX2_RECOMPILER_TESTS
		s_lastHydratedChunkBase[vu] = chunk_base;
#endif
		transaction.committed = true;
		s_stats[vu].programsHydrated++;
		s_stats[vu].blocksHydrated += blocks.size();
		return prog;
	}

	//------------------------------------------------------------------
	// Test hooks
	//------------------------------------------------------------------

	bool TestSerializeNewestProgram(u32 vu_index, std::vector<u8>& out)
	{
		microVU& mVU = (vu_index & 1) ? microVU1 : microVU0;
		microProgram* newest = nullptr;
		for (const auto& entry : mVU.contentPrograms)
		{
			if (!newest || entry.second->idx > newest->idx)
				newest = entry.second;
		}
		if (!newest)
			return false;
		return SerializeProgram(mVU, *newest, out);
	}

	bool TestHydrate(u32 vu_index, const u8* data, size_t size)
	{
		microVU& mVU = (vu_index & 1) ? microVU1 : microVU0;
		return HydrateProgram(mVU, data, size) != nullptr;
	}

#ifdef PCSX2_RECOMPILER_TESTS
	bool TestVerifyHydratedCode(u32 vu_index, const u8* image, size_t size)
	{
		if (!image || size < sizeof(ImageHeader) || size > (256u << 20)) return false;
		const u32 vu = vu_index & 1;
		const std::vector<u8*>& placed = s_lastHydratedChunkBase[vu];
		if (placed.empty())
			return false;

		// Re-parse the image (cheap, and avoids holding parse state).
		const u8* p = image;
		const u8* const end = image + size;
		ImageHeader hdr;
		if (!ReadPod(p, end, hdr) || hdr.chunkCount != placed.size())
			return false;
		// Bounds-check each section advance before adding (mirror ReadPod):
		// an unchecked p += count*size can step past end, after which the
		// per-chunk `end - p` guard below underflows to a huge size_t.
		if (static_cast<size_t>(end - p) < static_cast<size_t>(hdr.rangeCount) * sizeof(DiskRange))
			return false;
		p += static_cast<size_t>(hdr.rangeCount) * sizeof(DiskRange);
		if (static_cast<size_t>(end - p) < static_cast<size_t>(hdr.blockCount) * sizeof(DiskBlock))
			return false;
		p += static_cast<size_t>(hdr.blockCount) * sizeof(DiskBlock);

		for (u32 i = 0; i < hdr.chunkCount; i++)
		{
			DiskChunkHeader ch;
			if (!ReadPod(p, end, ch))
				return false;
			if (ch.fixupCount > static_cast<size_t>(end - p) / sizeof(PersistFixup)) return false;
			std::vector<PersistFixup> fixups(ch.fixupCount);
			for (PersistFixup& f : fixups)
				if (!ReadPod(p, end, f))
					return false;
			if (static_cast<size_t>(end - p) < ch.codeSize)
				return false;
			const u8* expect = p;
			p += ch.codeSize;

			std::vector<u8> mask(ch.codeSize, 0);
			for (const PersistFixup& f : fixups)
			{
				const u32 span = FixupSpanInsns(f) * 4;
				for (u32 b = 0; b < span && f.codeOffset + b < ch.codeSize; b++)
					mask[f.codeOffset + b] = 1;
			}

			const u8* live = placed[i];
			for (u32 b = 0; b < ch.codeSize; b++)
			{
				if (!mask[b] && live[b] != expect[b])
				{
					DevCon.Error("mVUPersist: hydrated code mismatch at chunk %u offset 0x%x (live %02x != image %02x)",
						i, b, live[b], expect[b]);
					return false;
				}
			}
		}
		return true;
	}
#endif // PCSX2_RECOMPILER_TESTS


	u64 GetBlockCompileCount(u32 vu_index) { return s_blockCompiles[vu_index & 1]; }
	Stats GetStats(u32 vu_index) { return s_stats[vu_index & 1]; }

	//------------------------------------------------------------------
	// ABI-digest guard support
	//------------------------------------------------------------------

	// Zero every operand field that may legitimately differ between two
	// correct emissions of the same program (addresses + PC-relative
	// displacements to relocatable or layout-dependent targets). Everything
	// left — opcode bits, register fields, data immediates, instruction
	// order — is the emitter's shape.
	static u32 CanonicalizeInsn(u32 insn)
	{
		// 64-bit MOVZ/MOVN/MOVK (sf=1): address materialization. 32-bit
		// forms carry data constants (cycle counts etc.) and stay intact.
		const u32 mov_op = insn & 0xFF800000u;
		if (mov_op == 0xD2800000u || mov_op == 0xF2800000u || mov_op == 0x92800000u)
			return insn & ~(0xFFFFu << 5); // clear imm16, keep hw + Rd
		// B / BL: stub + cross-chunk displacements depend on placement.
		if ((insn & 0x7C000000u) == 0x14000000u)
			return insn & 0xFC000000u;
		// ADRP: page displacement is placement-dependent by construction.
		if ((insn & 0x9F000000u) == 0x90000000u)
			return insn & ~((3u << 29) | (0x7FFFFu << 5));
		return insn;
	}

	bool TestComputeEmitDigest(u32 vu_index, u64& out_digest)
	{
		out_digest = 0;
		std::vector<u8> image;
		if (!TestSerializeNewestProgram(vu_index, image))
			return false;

		const u8* p = image.data();
		const u8* const end = p + image.size();
		ImageHeader hdr;
		if (!ReadPod(p, end, hdr))
			return false;

		// Canonical buffer: structure counts + ranges + block geometry +
		// fixup records (sans address payloads) + canonicalized code.
		// Header hashes/bases and microRegInfo bytes are excluded — the
		// former are per-run, the latter may carry padding.
		std::vector<u8> canon;
		AppendPod(canon, hdr.vuIndex);
		AppendPod(canon, hdr.progStartPC);
		AppendPod(canon, hdr.rangeCount);
		AppendPod(canon, hdr.blockCount);
		AppendPod(canon, hdr.chunkCount);

		for (u32 i = 0; i < hdr.rangeCount; i++)
		{
			DiskRange r;
			if (!ReadPod(p, end, r))
				return false;
			AppendPod(canon, r);
		}
		for (u32 i = 0; i < hdr.blockCount; i++)
		{
			DiskBlock b;
			if (!ReadPod(p, end, b))
				return false;
			AppendPod(canon, b.chunkIndex);
			AppendPod(canon, b.entryOffset);
			AppendPod(canon, b.startPC);
			AppendPod(canon, b.hasJumpCache);
		}
		for (u32 i = 0; i < hdr.chunkCount; i++)
		{
			DiskChunkHeader ch;
			if (!ReadPod(p, end, ch))
				return false;
			AppendPod(canon, ch);
			// Image and data-arena offsets are left out: they move with any
			// change anywhere in the binary, not with the emitter's shape.
			std::vector<u8> rel19_site;
			for (u32 f = 0; f < ch.fixupCount; f++)
			{
				PersistFixup fx;
				if (!ReadPod(p, end, fx))
					return false;
				AppendPod(canon, fx.codeOffset);
				AppendPod(canon, fx.form);
				AppendPod(canon, fx.target);
				AppendPod(canon, fx.width);
				AppendPod(canon, fx.index);
				if (fx.target == kTargetBlockObject)
					AppendPod(canon, fx.offset);
				if (fx.form == kFormRel19)
				{
					if (rel19_site.size() < ch.codeSize / 4)
						rel19_site.resize(ch.codeSize / 4, 0);
					if (fx.codeOffset / 4 < rel19_site.size())
						rel19_site[fx.codeOffset / 4] = 1;
				}
			}
			if (static_cast<size_t>(end - p) < ch.codeSize || (ch.codeSize & 3))
				return false;
			for (u32 off = 0; off < ch.codeSize; off += 4)
			{
				u32 insn;
				std::memcpy(&insn, p + off, 4);
				u32 c = CanonicalizeInsn(insn);
				// A recorded B.cond / CBZ across chunks: its displacement depends
				// on what was compiled between the chunks.
				if (off / 4 < rel19_site.size() && rel19_site[off / 4])
					c &= ~(0x7FFFFu << 5);
				AppendPod(canon, c);
			}
			p += ch.codeSize;
		}

		out_digest = XXH3_64bits(canon.data(), canon.size());
		return true;
	}
} // namespace mVUPersist
