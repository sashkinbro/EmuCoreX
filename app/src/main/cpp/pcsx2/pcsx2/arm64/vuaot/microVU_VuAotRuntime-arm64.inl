// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+
// Kept in microVU's translation unit so baseline compilation remains on its owner.
#include "arm64/vuaot/VuAotRegion.h"
#include "arm64/vuaot/VuAotTier.h"
#include "arm64/vuaot/VuAotBackground.h"
#include "arm64/vuaot/VuAotOaknut.h"
#include "arm64/vuaot/VuAotLive.h"
#include "common/ScopedGuard.h"
#include <atomic>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "microVU_VuAotTripwire-arm64.inl"

namespace mvu_aot
{
	namespace
	{
		using namespace vuaot;
		struct BuildJob;
		struct Node
		{
			PipeState key{}; // Bail records expose this exact 96-byte pipeline prefix.
			u32 pc = 0;
			u32 codeOffset = 0;
			void* privateEntry = nullptr;
			void* baseline = nullptr;
			microBlock* baselineBlock = nullptr;
			void* entry = nullptr;
			void* bailBridge = nullptr;
			PipeState jumpEnd{};
			BuildJob* job = nullptr;
		};
		struct Site
		{
			u32 pc = 0;
			PipeState key{};
			Node* jump = nullptr;
			vuaot::ir::EndInfo end;
			void* target = nullptr;
		};
		struct ProgramCounters
		{
			u64 bail[2]{};
		};
		struct BuildJob final : bg::Job
		{
			std::array<u8, 0x4000> micro{};
			std::array<u8, 0x1000> read{};
			MvuBlockTrace trace;
			MvuEmitEnv env;
			Pcsx2Config::CpuOptions cpu;
			Pcsx2Config::GamefixOptions fixes;
			Pcsx2Config::SpeedhackOptions speed;
			microProgram* program = nullptr;
			u64 generation = 0;
			u64 readyOrder = 0;
			ProgramCounters counters;
			std::vector<std::unique_ptr<Node>> nodes;
			std::vector<Site> sites;
			std::vector<std::pair<u32, u32>> ranges;
			std::vector<u8> code;
			std::vector<bg::Fixup> fixups;
			std::string error;
			bool accepted = false, body = false;
			bool dropAllowed = false, dropped = false;
			bool viaJump = false, retier = false;
			std::map<u32, std::vector<u32>> jumpTargets;
			bg::CodeSink* sink = nullptr;
			void Run() override;
		};
		thread_local BuildJob* building = nullptr;
		struct Remembered
		{
			MvuBlockTrace trace;
			microBlock* block = nullptr;
		};
		struct AutomaticRoot
		{
			microBlock* block = nullptr;
			u32 pc = 0;
			PipeState key{};
		};
		using RootIdentity = std::pair<u32, std::array<u8, 96>>;
		struct Program
		{
			u32 runs = 0;
			u32 jumpRuns = 0, tiers = 0, targetCount = 0, emptyChecks = 0;
			u32 runsAtTier = 0, checkBucket = 0;
			u64 missesSeen = 0;
			bool settled = false;
			bool sidecarTried = false;
			std::deque<vuaot::persist::Record> savedPlans;
			std::map<u32, std::vector<u32>> jumpTargets;
			std::vector<Remembered> traces;
			std::unordered_map<microBlock*, size_t> traceIndex;
			std::deque<AutomaticRoot> automaticRoots;
			std::unordered_set<microBlock*> queuedRoots, coveredRoots;
			std::set<RootIdentity> queuedRootKeys, coveredRootKeys;
			bool rootsSeeded = false;
			std::vector<std::shared_ptr<BuildJob>> jobs;
			std::unordered_set<void*> entries;
			std::shared_ptr<BuildJob> pending;
			std::vector<std::shared_ptr<BuildJob>> ready;
			std::vector<vuaot::persist::DeclinedRoot> declined;
            bool IsDeclined(u32 pc, const void* key) const
            {
                return std::any_of(declined.begin(), declined.end(), [&](const auto& root) {
                    return root.pc == pc && std::memcmp(root.key.data(), key, 96) == 0;
                });
            }
            void NoteDeclined(const MvuBlockTrace& trace)
            {
                if (IsDeclined(trace.startPC, trace.entryState) || declined.size() >= vuaot::persist::kMaxDeclinedRoots)
                    return;
                vuaot::persist::DeclinedRoot root;
                root.pc = trace.startPC;
                std::memcpy(root.key.data(), trace.entryState, 96);
                declined.push_back(root);
            }
		};
		struct StaticBranch
		{
			microProgram* owner = nullptr;
			u32* site = nullptr;
			void* privateTarget = nullptr;
			void* liveTarget = nullptr;
			u32 originalWord = 0;
			u64 generation = 0;
		};
		struct NativeBinding
		{
			void* native = nullptr;
			void* baseline = nullptr;
			BuildJob* job = nullptr; // retained by Program.jobs through Reset
		};
		struct Runtime
		{
			bg::Worker worker;
			std::unordered_map<microProgram*, Program> programs;
			std::unordered_map<microBlock*, void*> original;
			std::vector<StaticBranch> staticBranches, pendingStaticBranches;
			std::unordered_map<void*, size_t> staticSiteIndex;
			std::unordered_map<void*, std::vector<size_t>> inboundBranches;
			std::unordered_map<const void*, NativeBinding> privateForNative;
			std::unordered_map<void*, NativeBinding> nativeForPrivate;
			u64 generation = 1;
			u64 jumpMisses = 0;
			u64 readyBytes = 0, readySerial = 0;
			u32 readyJobs = 0;
			Stats stats;
		};
		Runtime& runtime = *new Runtime(); // Generated entries outlive static destructor ordering.
		std::atomic<u64> regionCount{0};
		bool forceSync = false;
		u32 tierUpRuns = 200;
		u32 jumpTierUpRuns = 32;
		constexpr u64 kReadyCodeBytes = 64ull << 20;
		constexpr u32 kReadyJobs = 256, kReadyProgramJobs = 8;
		u64 readyByteLimit = kReadyCodeBytes;
		u32 readyJobLimit = kReadyJobs, readyProgramLimit = kReadyProgramJobs;
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
		bool holdReadyPublication = false;
		bool relinkJumpCachesForTest = true;
		u64 entryCount = 0;
		u64 nativeEndCount = 0;
		std::array<u64, 0x800> inlineBodyEntries{};
		std::array<u64, 0x800> nativeRegionEntries{};
		bool countEntries = false;
        bool manualEntryCount = false;
#if defined(__ANDROID__) && !defined(NDEBUG)
        bool logStats = false;
        u32 statsPoll = 0;
        std::chrono::steady_clock::time_point nextStats{};
#endif
#endif
		thread_local bool resolving = false;
		__fi void ReadyStat(u64 Stats::*counter)
		{
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
			++(runtime.stats.*counter);
#else
			(void)counter;
#endif
		}
		RootIdentity CanonicalRootIdentity(u32 pc, const void* key)
		{
			microRegInfo state;
			std::memcpy(&state, key, sizeof(state));
			// Internal edges may carry an active branch-delay backup even
			// when VI's ordinary latency is zero. Only external entry lookup
			// may discard a stale backup label; root identity preserves it.
			state.flagInfo = mVUcanonicalFlagInfo(state);
			RootIdentity identity;
			identity.first = pc;
			std::memcpy(identity.second.data(), &state, sizeof(state));
			return identity;
		}
		void QueueAutomaticRoot(Program& program, microBlock* block, u32 pc, const void* key)
		{
			const auto identity = CanonicalRootIdentity(pc, key);
			if (!block || program.coveredRoots.count(block) || program.coveredRootKeys.count(identity) ||
				!program.queuedRoots.insert(block).second || !program.queuedRootKeys.insert(identity).second)
				return;
			AutomaticRoot root;
			root.block = block; root.pc = pc;
			std::memcpy(root.key.bytes, identity.second.data(), sizeof(root.key.bytes));
			program.automaticRoots.push_back(root);
		}
		void SeedAutomaticRoots(Program& program)
		{
			if (program.rootsSeeded) return;
			program.rootsSeeded = true;
			for (size_t i = 0; i < program.traces.size(); ++i)
			{
				const auto& remembered = program.traces[i];
				program.traceIndex[remembered.block] = i;
				QueueAutomaticRoot(program, remembered.block, remembered.trace.startPC, remembered.trace.entryState);
			}
		}
        void LogStats()
        {
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && defined(__ANDROID__) && !defined(NDEBUG)
            if (!logStats || (++statsPoll & 255)) return;
            const auto now=std::chrono::steady_clock::now();
            if(now<nextStats) return;
            nextStats=now+std::chrono::seconds(1);
            u64 pending=0, bails=0, declines=0;
            for(const auto& [program,state]:runtime.programs)
            {
                pending+=state.pending!=nullptr; declines+=state.declined.size();
                for(const auto& job:state.jobs) bails+=job->counters.bail[0]+job->counters.bail[1];
            }
            const auto st=GetStats();
            __android_log_print(ANDROID_LOG_INFO,"EmuCoreX",
                "AOT_STATS regions=%llu entries=%llu ends=%llu dispatch=%llu jr=%llu jr_tiers=%llu targets=%llu cross=%llu retiers=%llu pending=%llu bails=%llu declines=%llu saved=%llu plans=%llu decline_single=%llu decline_timing=%llu decline_other=%llu publish_reject=%llu bodies=%llu jr_resolve=%llu sticky_drop=%llu sticky_trip=%llu sticky_rebuild=%llu",
                (unsigned long long)regionCount.load(),(unsigned long long)entryCount,(unsigned long long)nativeEndCount,
                (unsigned long long)st.dispatches,(unsigned long long)st.jumpEntries,(unsigned long long)st.jumpTierUps,
                (unsigned long long)st.targetsRecorded,(unsigned long long)st.crossProgramTargets,(unsigned long long)st.retiers,
                (unsigned long long)pending,(unsigned long long)bails,(unsigned long long)declines,
                (unsigned long long)st.savedRecords,(unsigned long long)st.savedPlanJobs,
                (unsigned long long)st.singleDeclines,(unsigned long long)st.timingDeclines,
                (unsigned long long)st.unsupportedDeclines,(unsigned long long)st.publishRejects,
                (unsigned long long)st.bodyBlocks,(unsigned long long)st.jumpResolverCalls,
                (unsigned long long)st.stickyDropped,(unsigned long long)st.stickyTrips,
                (unsigned long long)st.stickyResets);
#endif
        }

        void NoteDecline(const BuildJob& job)
        {
            if(job.accepted) ++runtime.stats.publishRejects;
            else if(job.error=="single block, no loop") ++runtime.stats.singleDeclines;
            else if(job.error.find("timing port disagrees")==0) ++runtime.stats.timingDeclines;
            else ++runtime.stats.unsupportedDeclines;
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && defined(__ANDROID__) && !defined(NDEBUG)
            const u64 count=runtime.stats.timingDeclines+runtime.stats.unsupportedDeclines+runtime.stats.publishRejects;
            if(logStats && count && count<=16 && job.error!="single block, no loop")
                __android_log_print(ANDROID_LOG_INFO,"EmuCoreX","AOT_DECLINE pc=%04x accepted=%u reason=%s",
                    job.trace.startPC,job.accepted,job.error.c_str());
#endif
        }
		u32 HarvestTargets(microProgram& prog, Program& state)
		{
			u32 added = 0;
			for (u32 slot = 0; slot < mProgSizeHalf; ++slot)
			{
				const auto* manager = prog.block[slot];
				if (!manager)
					continue;
				for (auto* head : {manager->getQBlockList(), manager->getFBlockList()})
					for (auto* link = head; link; link = link->next)
					{
						const auto* cache = link->block.jumpCache;
						if (!cache)
							continue;
						auto& targets = state.jumpTargets[slot * 8];
						for (u32 target = 0; target < mProgSizeHalf; ++target)
						{
							if (cache[target].prog != &prog)
							{
								runtime.stats.crossProgramTargets += cache[target].prog != nullptr;
								continue;
							}
							const u32 pc = target * 8;
							const auto at = std::lower_bound(targets.begin(), targets.end(), pc);
							if ((at != targets.end() && *at == pc) || targets.size() >= live::kMaxTargetsPerJump ||
								state.targetCount >= live::kMaxTargetsPerProgram)
								continue;
							targets.insert(at, pc);
							++state.targetCount;
							++added;
						}
					}
			}
			for (auto it = state.jumpTargets.begin(); it != state.jumpTargets.end();)
				it = it->second.empty() ? state.jumpTargets.erase(it) : std::next(it);
			runtime.stats.targetsRecorded += added;
			return added;
		}

		bool SameKey(const PipeState& a, const u8* raw, bool flagHack)
		{
			PipeState b;
			std::memcpy(b.bytes, raw, 96);
			if (a.needExactMatch || b.needExactMatch)
				return std::memcmp(a.bytes, b.bytes, 96) == 0;
			const u64 mask = flagHack ? ~u64(0x0c04) : ~u64(0);
			return (a.quick64[0] & mask) == (b.quick64[0] & mask) && a.vi15 == b.vi15 && a.vi15v == b.vi15v;
		}
		void* FindNode(u32, u32 pc, const u8* key)
		{
			for (auto& n : building->nodes)
				if (n->pc == pc && SameKey(n->key, key, building->env.flagHack))
					return n.get();
			return nullptr;
		}
		void* AddNode(u32, u32 pc, const u8* key)
		{
			auto n = std::make_unique<Node>();
			n->pc = pc;
			std::memcpy(n->key.bytes, key, 96);
			n->job = building;
			Node* ptr = n.get();
			building->nodes.push_back(std::move(n));
			return ptr;
		}
		void SetEntry(void* p, void* entry)
		{
			auto* n = static_cast<Node*>(p);
			n->privateEntry = entry;
			n->codeOffset = static_cast<u32>(static_cast<u8*>(entry) - building->sink->Base());
		}
		void* GetEntry(void* p) { return static_cast<Node*>(p)->privateEntry; }
		void* FetchExit(u32, u32 pc, const u8* key)
		{
			Site site;
			site.pc = pc;
			std::memcpy(site.key.bytes, key, 96);
			building->sites.push_back(site);
			return const_cast<void*>(building->sink->Placeholder(static_cast<u32>(building->sites.size() - 1)));
		}
		void NoteRange(u32, u32 start, u32 end) { building->ranges.emplace_back(start, end); }
		void NoteData(const u8* at, u64 value) { building->sink->NoteData64(at, value); }
		void EmitJump(u32, void* p, const u8* end)
		{
			auto* n = static_cast<Node*>(p);
			std::memcpy(n->jumpEnd.bytes, end, 96);
			Site site;
			site.jump = n;
			building->sites.push_back(site);
			vuaot::armEmitJmp(building->sink->Placeholder(static_cast<u32>(building->sites.size() - 1)));
		}
		void* BailTarget(u32, u8* record)
		{
			Node* n;
			std::memcpy(&n, record + 8, sizeof(n));
			void* target = n->bailBridge;
			return target; // Published records are seeded while the code cache is writable.
		}
		void EmitEnd(u32, void*, const vuaot::ir::EndInfo& end)
		{
			Site site;
			site.end = end;
			building->sites.push_back(site);
			vuaot::armEmitJmp(building->sink->Placeholder(static_cast<u32>(building->sites.size() - 1)));
		}
        bool LowerBody(BuildJob& job)
        {
            auto& env = job.env;
            BodyTier tier(1, job.micro.data());
            tier.schedule = true;
            tier.remapAtLeave = true;
            tier.regionEnds = true;
            a64::Label bail;
            vuaot::armAsm->Ldr(a64::w8, a64::MemOperand(a64::x24, env.offCycles));
            vuaot::armAsm->Cmp(a64::w8, job.trace.cycles);
            vuaot::armAsm->B(&bail, a64::lt);
            vuaot::armAsm->Sub(a64::w8, a64::w8, job.trace.cycles);
            vuaot::armAsm->Str(a64::w8, a64::MemOperand(a64::x24, env.offCycles));
            vuaot::armAsm->Ldp(a64::q26, a64::q25,
                a64::MemOperand(a64::x25, offsetof(vuaot::mVU_Globals, minvals)));
            if (!tier.TryLower(job.trace, env, &job.error))
                return false;
            const auto& br = tier.Block();
            const auto link = [&](u32 pc) { vuaot::armEmitJmp(FetchExit(1, pc, br.end.bytes)); };
            switch (br.endKind)
            {
                case BlockEnd::EBit:
                    if (!tier.End().valid) { job.error = "body end metadata absent"; return false; }
                    EmitEnd(1, env.rootBlock, tier.End());
                    break;
                case BlockEnd::Branch:
                    link(br.takenPC);
                    break;
                case BlockEnd::CondBranch:
                {
                    a64::Label taken;
                    vuaot::armAsm->Ldr(a64::w8, a64::MemOperand(a64::x24, env.offBranch));
                    vuaot::armAsm->Cmn(a64::wzr, a64::Operand(a64::w8, a64::LSL, 16));
                    const auto condition = br.branchKind == 3 ? a64::eq : br.branchKind == 4 ? a64::ge :
                        br.branchKind == 5 ? a64::gt : br.branchKind == 6 ? a64::le : br.branchKind == 7 ? a64::lt : a64::ne;
                    vuaot::armAsm->B(&taken, condition);
                    link(br.fallPC);
                    vuaot::armAsm->Bind(&taken);
                    link(br.takenPC);
                    break;
                }
                case BlockEnd::Jump:
                    EmitJump(1, env.rootBlock, br.end.bytes);
                    break;
                default:
                    job.error = "unsupported body boundary";
                    return false;
            }
            vuaot::armAsm->Bind(&bail);
            vuaot::armMoveAddressToReg(a64::x9, &job.counters);
            vuaot::armAsm->Ldr(a64::x10, a64::MemOperand(a64::x9));
            vuaot::armAsm->Add(a64::x10, a64::x10, 1);
            vuaot::armAsm->Str(a64::x10, a64::MemOperand(a64::x9));
            vuaot::armEmitJmp(FetchExit(1, job.trace.startPC, job.trace.entryState));
            job.body = true;
            return true;
        }
		void BuildJob::Run()
		{
			bg::CodeSink buffer;
			sink = &buffer;
			building = this;
			try
			{
				buffer.Begin();
				env.searchBlock = FindNode;
				env.addBlock = AddNode;
				env.setBlockEntry = SetEntry;
				env.blockEntry = GetEntry;
				env.fetchBlock = FetchExit;
				env.addRange = NoteRange;
				env.emitJumpTail = EmitJump;
				env.bailTarget = BailTarget;
				env.noteDataWord = NoteData;
				env.program = &counters;
				env.offProgBails = 0;
				env.rootBlock = AddNode(1, trace.startPC, trace.entryState);
				SetEntry(env.rootBlock, buffer.Base());
				RegionTier tier(1, micro.data());
				sticky::Observe(micro.data(), live::TripCause::Job);
                tier.dropSticky = dropAllowed && !sticky::state.tripped.load(std::memory_order_acquire);
				tier.schedule = true;
				tier.emitEndTail = EmitEnd;
				tier.jumpTargets = jumpTargets;
				g_pipeReadMask = read.data();
				accepted = tier.OnCompile(trace, env, &error);
                dropped = accepted && tier.StickyDropped() != 0;
                if (!accepted && buffer.failed.empty())
                {
                    // Region analysis can register tentative nodes. Discard all
                    // of them before producing the independent framed body.
                    buffer.End();
                    nodes.clear(); sites.clear(); ranges.clear(); read.fill(0);
                    buffer.Begin();
                    env.rootBlock = AddNode(1, trace.startPC, trace.entryState);
                    SetEntry(env.rootBlock, buffer.Base());
                    accepted = LowerBody(*this);
                }
				g_pipeReadMask = nullptr;
				const u32 size = buffer.End();
				if (!buffer.failed.empty())
				{
					accepted = false;
					error = buffer.failed;
				}
				if (accepted)
				{
					code.assign(buffer.Base(), buffer.Base() + size);
					fixups = std::move(buffer.fixups);
				}
			}
			catch (const std::exception& ex)
			{
				error = ex.what();
				accepted = false;
				g_pipeReadMask = nullptr;
				if (vuaot::armAsm)
					buffer.End();
			}
			building = nullptr;
			sink = nullptr; // The 16 MB temporary dies here, never in the program cache.
		}
		void EntryAbi(microVU& mvu)
		{
			using namespace oak::util;
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
			if (forceSync || countEntries)
			{
				oakMoveAddressToReg(X9, &entryCount);
				oakAsm->LDR(X10, X9, oak::POffset<15, 3>(0));
				oakAsm->ADD(X10, X10, 1);
				oakAsm->STR(X10, X9, oak::POffset<15, 3>(0));
			}
#endif
			oakAsm->MOV(W20, W3);
			oakAsm->MOV(W21, W12);
			oakAsm->MOV(W22, W13);
			oakAsm->MOV(W23, W14);
			oakAsm->MOV(oakQRegister(28).B16(), oakQRegister(15).B16());
			oakAsm->MOV(X29, X25);
			oakMoveAddressToReg(X19, &mvu.regs());
			oakMoveAddressToReg(X24, &mvu.macFlag[0]);
			oakMoveAddressToReg(X25, &vuaot::mVUglob);
		}
		void ExitAbi()
		{
			using namespace oak::util;
			oakAsm->MOV(W3, W20);
			oakAsm->MOV(W12, W21);
			oakAsm->MOV(W13, W22);
			oakAsm->MOV(W14, W23);
			oakAsm->MOV(oakQRegister(15).B16(), oakQRegister(28).B16());
			oakAsm->MOV(X25, X29);
			oakMoveAddressToReg(X26, &g_cpuRegistersPack.mVUglob);
			oakMoveAddressToReg(X27, &g_cpuRegistersPack);
			oakMoveAddressToReg(X28, &g_vuRegistersPack);
		}
		void* ResolveJump(Node* node)
		{
			++runtime.stats.jumpResolverCalls;
			microVU& mvu = microVU1;
			const u32 target = mvu.branch & 0x3ff8;
			// Reuse the baseline cache's program-identity guard and its slow-path
			// counting. In particular, cross-program JR must update start_pc.
			if (node->baselineBlock && node->baselineBlock->jumpCache)
				return mVUcompileJIT<1>(target, reinterpret_cast<uptr>(node->baselineBlock));
			mvu.regs().start_pc = target;
			OnJumpMiss(mvu);
			void* result = mVUsearchProg<1>(target, reinterpret_cast<uptr>(node->jumpEnd.bytes));
			return AfterJump(mvu, target, node->jumpEnd.bytes, result);
		}
		void* Baseline(microVU& mvu, u32 pc, const PipeState& key, microBlock** outBlock = nullptr)
		{
			microRegInfo state;
			std::memcpy(&state, key.bytes, 96);
			state.flagInfo = mVUcanonicalFlagInfo(state);
			// These are internal region/static/budget edges. Do not use the
			// external-entry getter: its stale-VI normalization would discard
			// an active delay-slot backup required by an immediate IBcc.
			pxAssert((pc & 7) == 0 && pc < mvu.microMemSize);
			pc &= mvu.microMemSize - 8;
			auto*& manager = mvu.prog.cur->block[pc >> 3];
			if (!manager) manager = new microBlockManager();
			microBlock* block = manager->search(mvu, &state);
			if (!block)
			{
				mVUcompile(mvu, pc, reinterpret_cast<uptr>(&state));
				block = manager->search(mvu, &state);
			}
			pxAssert(block);
			if (outBlock)
				*outBlock = block;
			auto it = runtime.original.find(block);
			return it == runtime.original.end() ? block->x86ptrStart : it->second;
		}
		bool ValidRuntime(const BuildJob& job, microVU& mvu)
		{
			if (job.dropped)
            {
                sticky::Observe(mvu.regs().Micro, live::TripCause::Job);
                if (sticky::state.tripped.load(std::memory_order_acquire))
                {
                    sticky::state.rejected.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }
            }
            if (job.generation != runtime.generation || !(job.cpu == EmuConfig.Cpu) || !(job.fixes == EmuConfig.Gamefixes) || !(job.speed == EmuConfig.Speedhacks))
				return false;
			return true;
		}
		bool ImageMatches(const BuildJob& job, const microVU& mvu)
		{
			for (u32 i = 0; i < 0x1000; ++i)
				if (job.read[i] && std::memcmp(job.micro.data() + i * 4, mvu.regs().Micro + i * 4, 4))
					return false;
			return true;
		}
		bool OwnerMatches(const BuildJob& job)
		{
			if (!job.program || !job.program->ranges) return false;
			// A partial owner can expand while an old job waits for its image.
			// Already observed required words also describe cached baseline bail
			// targets; they must still agree with this job's immutable snapshot.
			// Unobserved words have no such cached assumption and remain allowed.
			for (const auto& range : *job.program->ranges)
			{
				if (range.start < 0 || range.end <= range.start) continue;
				const u32 first = (static_cast<u32>(range.start) + 3) / 4;
				const u32 last = std::min(static_cast<u32>(range.end), 0x4000u) / 4;
				for (u32 word = first; word < last; ++word)
					if (job.read[word] && std::memcmp(job.micro.data() + word * 4,
						reinterpret_cast<const u8*>(job.program->data) + word * 4, 4))
						return false;
			}
			return true;
		}
		bool OwnerImageMatches(const microProgram* owner, const microVU& mvu)
		{
			if (!owner || !owner->ranges) return false;
			// A body may leave through a cached baseline child it never read.
			// Every already compiled owner range must therefore describe the
			// live image before Baseline can reuse its static or budget targets.
			// Unlike a required-read snapshot conflict, another live overlay is
			// temporary: keep the accepted job until these ranges agree again.
			for (const auto& range : *owner->ranges)
			{
				if (range.start < 0 || range.end <= range.start) continue;
				const u32 first = std::min(static_cast<u32>(range.start), 0x4000u);
				const u32 last = std::min(static_cast<u32>(range.end), 0x4000u);
				if (std::memcmp(reinterpret_cast<const u8*>(owner->data) + first,
					mvu.regs().Micro + first, last - first)) return false;
			}
			return true;
		}
		bool OwnerImageMatches(const BuildJob& job, const microVU& mvu) { return OwnerImageMatches(job.program, mvu); }
		bool Valid(const BuildJob& job, microVU& mvu)
		{
			return ValidRuntime(job, mvu) && OwnerMatches(job) && ImageMatches(job, mvu) && OwnerImageMatches(job, mvu);
		}
		bool StaticBranchWord(const StaticBranch& link, const void* target, u32& word)
		{
			const s64 delta = reinterpret_cast<intptr_t>(target) - reinterpret_cast<intptr_t>(link.site);
			if ((delta & 3) || delta < -(s64(1) << 27) || delta >= (s64(1) << 27)) return false;
			word = 0x14000000u | (static_cast<u32>(delta / 4) & 0x03ffffffu);
			return true;
		}
		bool PatchStaticBranch(StaticBranch& link, const void* target)
		{
			if (link.generation != runtime.generation) return false;
			u32 expected, replacement, current;
			if (!StaticBranchWord(link, link.liveTarget, expected) || !StaticBranchWord(link, target, replacement))
				return false;
			std::memcpy(&current, link.site, sizeof(current));
			if (current != expected || (current & 0xfc000000u) != 0x14000000u) return false;
			if (current != replacement)
			{
				std::memcpy(link.site, &replacement, sizeof(replacement));
				HostSys::FlushInstructionCache(link.site, sizeof(replacement));
			}
			link.liveTarget = const_cast<void*>(target);
			return true;
		}
		void FinishStaticBranches(microVU& mvu)
		{
			if (oakHasBlock() || mvu.index != 1) return;
			auto pending = std::move(runtime.pendingStaticBranches);
			runtime.pendingStaticBranches.clear();
			for (const auto& link : pending)
			{
				if (link.generation != runtime.generation || runtime.staticSiteIndex.count(link.site)) continue;
				const size_t index = runtime.staticBranches.size();
				runtime.staticSiteIndex.emplace(link.site, index);
				runtime.staticBranches.push_back(link);
				runtime.inboundBranches[link.privateTarget].push_back(index);
				const auto found = runtime.nativeForPrivate.find(link.privateTarget);
				if (found != runtime.nativeForPrivate.end() && found->second.job->program == link.owner &&
					Valid(*found->second.job, mvu))
					PatchStaticBranch(runtime.staticBranches[index], found->second.native);
			}
		}
		void BindStaticBranches(microVU& mvu, const std::shared_ptr<BuildJob>& job)
		{
			pxAssert(!oakHasBlock());
			FinishStaticBranches(mvu);
			for (const auto& node : job->nodes)
			{
				NativeBinding binding{node->entry, node->baseline, job.get()};
				runtime.privateForNative[node->entry] = binding;
				runtime.nativeForPrivate[node->baseline] = binding;
				const auto incoming = runtime.inboundBranches.find(node->baseline);
				if (incoming == runtime.inboundBranches.end()) continue;
				for (size_t index : incoming->second)
					if (runtime.staticBranches[index].owner == job->program)
						PatchStaticBranch(runtime.staticBranches[index], node->entry);
			}
		}
		void RestoreStaticBranches()
		{
			for (auto& link : runtime.staticBranches)
			{
				u32 expected, current;
				if (link.generation != runtime.generation || !StaticBranchWord(link, link.liveTarget, expected)) continue;
				std::memcpy(&current, link.site, sizeof(current));
				if (current == expected && current != link.originalWord)
				{
					std::memcpy(link.site, &link.originalWord, sizeof(link.originalWord));
					HostSys::FlushInstructionCache(link.site, sizeof(link.originalWord));
				}
			}
			runtime.staticBranches.clear(); runtime.pendingStaticBranches.clear();
			runtime.staticSiteIndex.clear(); runtime.inboundBranches.clear();
			runtime.privateForNative.clear(); runtime.nativeForPrivate.clear();
		}
        void RelinkCachedEntries(microVU& mvu, microProgram* target,
            const std::unordered_map<void*, void*>& replacements, std::vector<u32> slots)
        {
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
            if (!relinkJumpCachesForTest) return;
#endif
            if (replacements.empty()) return;
            std::sort(slots.begin(), slots.end());
            slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
            const auto visit = [&](microProgram* owner) {
                for (u32 pc = 0; pc < mProgSizeHalf; ++pc)
                {
                    auto* manager = owner->block[pc];
                    if (!manager) continue;
                    for (auto* head : {manager->getQBlockList(), manager->getFBlockList()})
                        for (auto* link = head; link; link = link->next)
                        {
                            auto* cache = link->block.jumpCache;
                            if (!cache) continue;
                            // Only slots replaced by this publication can name
                            // an affected entry: do not scan all2048cells per JR.
                            for (u32 slot : slots)
                            {
                                ++runtime.stats.jumpCellsInspected;
                                auto& cell = cache[slot];
                                if (cell.prog != target) continue;
                                const auto found = replacements.find(cell.x86ptrStart);
                                if (found == replacements.end()) continue;
                                cell.x86ptrStart = static_cast<u8*>(found->second);
                                ++runtime.stats.jumpCellsRelinked;
                            }
                        }
                }
            };
            for (const auto& entry : mvu.contentPrograms) visit(entry.second);
            for (auto* owner : mvu.orphanedPrograms) visit(owner);
        }
		bool Publish(microVU& mvu, const std::shared_ptr<BuildJob>& job)
		{
			if (!job->accepted || !Valid(*job, mvu) || oakHasBlock())
				return false;
			for (const auto& f : job->fixups)
			{
				const size_t width = f.kind == bg::Fixup::Data64                                                                        ? 8 :
				                     (f.kind == bg::Fixup::IntraAbs || f.kind == bg::Fixup::SiteAbs || f.kind == bg::Fixup::OutsideAbs) ? 16 :
				                                                                                                                          4;
				if (f.offset > job->code.size() || width > job->code.size() - f.offset)
					return false;
				if (f.kind == bg::Fixup::IntraAbs && f.value >= job->code.size())
					return false;
				if ((f.kind == bg::Fixup::SiteAbs || f.kind == bg::Fixup::Site26) && f.id >= job->sites.size())
					return false;
			}
			microProgram* saved = mvu.prog.cur;
			const microRegInfo savedPipeline = mvu.prog.lpState;
			const bool savedResolving = resolving;
			ScopedGuard restoreOwner([&] {
				mvu.prog.cur = saved;
				mvu.prog.lpState = savedPipeline;
				resolving = savedResolving;
			});
			mvu.prog.cur = job->program;
			resolving = true;
			// Resolve originals before patching any block entry or allocating the region.
			for (auto& n : job->nodes)
				n->baseline = Baseline(mvu, n->pc, n->key, &n->baselineBlock);
			for (auto& site : job->sites)
				if (!site.jump && !site.end.valid)
					site.target = Baseline(mvu, site.pc, site.key);
			resolving = false;
			const size_t reserve = job->code.size() + job->nodes.size() * 384 + job->sites.size() * 1024 + job->fixups.size() * 32 + 1024;
			if (static_cast<size_t>(mvu.prog.x86end - mvu.prog.x86ptr) < reserve)
			{
				mvu.prog.cur = saved;
				return false;
			}
			oakSetAsmPtr(mvu.prog.x86ptr, mvu.prog.x86end - mvu.prog.x86ptr);
			u8* base = oakStartBlock();
			std::memcpy(base, job->code.data(), job->code.size());
			oakAsm->set_offset(job->code.size());
			const auto exitBridge = [&](void* target) {void* start=oakGetCurrentCodePointer();ExitAbi();oakEmitJmp(target);return start; };
			for (auto& n : job->nodes)
				n->bailBridge = exitBridge(n->baseline);
			for (auto& site : job->sites)
			{
				if (site.end.valid)
				{
					site.target = oakGetCurrentCodePointer();
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
					if (forceSync || countEntries)
					{
						oakMoveAddressToReg(oak::util::X9, &nativeEndCount);
						oakAsm->LDR(oak::util::X10, oak::util::X9, oak::POffset<15, 3>(0));
						oakAsm->ADD(oak::util::X10, oak::util::X10, 1);
						oakAsm->STR(oak::util::X10, oak::util::X9, oak::POffset<15, 3>(0));
					}
#endif
					ExitAbi();
					mVUBranchClearLpState_emit_oaknut(mvu);
					mVUSavePQRegs_emit_oaknut(mvu, site.end.qInst, site.end.pInst);
					mVUSaveFlagRegs_emit_oaknut(mvu, site.end.fS, site.end.fM, site.end.fC, false);
					mVUBranchStoreTpcImm_emit_oaknut(mvu, site.end.tpc);
					if (!THREAD_VU1)
						mVUBranchAndVu0VpuStat_emit_oaknut(~0x100u);
					else
						mVUBranchCallEBit_emit_oaknut();
					oakEmitJmp(mvu.exitFunct);
					continue;
				}
				if (!site.jump)
				{
					site.target = exitBridge(site.target);
					continue;
				}
				site.target = oakGetCurrentCodePointer();
                // Match mVUcompileJIT's cache guard before crossing the C++ ABI.
                // Entries may name an older baseline block or an AOT trampoline;
                // both use the local ABI and remain alive until cache reset.
                if (site.jump->baselineBlock && doJumpCaching)
                {
                    using namespace oak::util;
                    static_assert(sizeof(microJumpCache) == 16 && sizeof(microProgramQuick) == 8);
                    oak::Label miss;
                    oakLoad32(W0, {X24, static_cast<s64>(offsetof(microVU, branch) - offsetof(microVU, macFlag))});
                    oakAsm->AND(W0, W0, 0x3ff8u);
                    if (!doJumpAsSameProgram)
                        oakStore32(W0, {X19, static_cast<s64>(offsetof(VURegs, start_pc))});
                    oakMoveAddressToReg(X9, site.jump->baselineBlock);
                    oakLoad64(X9, {X9, static_cast<s64>(offsetof(microBlock, jumpCache))});
                    oakAsm->CBZ(X9, miss);
                    oakAsm->LSR(X10, X0, 3);
                    oakAsm->ADD(X9, X9, X10, LSL, 4);
                    oakLoad64(X11, {X9, static_cast<s64>(offsetof(microJumpCache, prog))});
                    oakAsm->CBZ(X11, miss);
                    oakMoveAddressToReg(X12, mvu.prog.quick);
                    oakAsm->ADD(X12, X12, X10, LSL, 3);
                    oakLoad64(X12, {X12, static_cast<s64>(offsetof(microProgramQuick, prog))});
                    oakAsm->CMP(X11, X12);
                    oakAsm->B(oak::Cond::NE, miss);
                    oakLoad64(X0, {X9, static_cast<s64>(offsetof(microJumpCache, x86ptrStart))});
                    ExitAbi();
                    oakAsm->BR(X0);
                    oakAsm->l(miss);
                }
				oakStore128(oakQRegister(28), {oak::util::X24, static_cast<s64>(offsetof(microVU, aotBackup) - offsetof(microVU, macFlag) + 28 * 16)});
				oakMoveAddressToReg(oak::util::X0, site.jump);
				oakEmitCall(reinterpret_cast<void*>(ResolveJump));
				oakLoad128(oakQRegister(28), {oak::util::X24, static_cast<s64>(offsetof(microVU, aotBackup) - offsetof(microVU, macFlag) + 28 * 16)});
				ExitAbi();
				oakAsm->BR(oak::util::X0);
			}
			bool patched = true;
			for (const auto& f : job->fixups)
			{
				u32* insn = reinterpret_cast<u32*>(base + f.offset);
				const void* target = nullptr;
				switch (f.kind)
				{
					case bg::Fixup::IntraAbs:
						patched &= bg::PatchAbsMov(insn, reinterpret_cast<u64>(base + f.value));
						break;
					case bg::Fixup::SiteAbs:
						if (f.id >= job->sites.size())
						{
							patched = false;
							break;
						}
						patched &= bg::PatchAbsMov(insn, reinterpret_cast<u64>(job->sites[f.id].target));
						break;
					case bg::Fixup::Site26:
						if (f.id >= job->sites.size())
						{
							patched = false;
							break;
						}
						target = job->sites[f.id].target;
						break;
					case bg::Fixup::External26:
						target = reinterpret_cast<const void*>(f.value);
						break;
					case bg::Fixup::OutsideAbs:
						break;
					case bg::Fixup::Data64:
						// Every node's record is followed by its cached baseline exit. Seed
						// it now, avoiding writes to executable memory from generated code.
						for (const auto& n : job->nodes)
							if (f.value == reinterpret_cast<u64>(n.get()))
								std::memcpy(base + f.offset + 8, &n->bailBridge, sizeof(n->bailBridge));
						break;
				}
				if (target && !bg::PatchRel26(insn, base + f.offset, target))
				{
					void* veneer = oakGetCurrentCodePointer();
					oakMoveAddressToReg(oak::util::X17, target);
					oakAsm->BR(oak::util::X17);
					patched &= bg::PatchRel26(insn, base + f.offset, veneer);
				}
			}
			for (auto& n : job->nodes)
			{
				n->entry = oakGetCurrentCodePointer();
				EntryAbi(mvu);
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
				if (!job->body && (forceSync || countEntries))
				{
					// This wrapper is rebuilt by Publish for native imports too;
					// no counter address is persisted in the private region code.
					// Baseline status/PQ homes are already captured by EntryAbi.
					oakMoveAddressToReg(oak::util::X9, &nativeRegionEntries[(n->pc & 0x3ff8) >> 3]);
					oakAsm->LDR(oak::util::X10, oak::util::X9, oak::POffset<15, 3>(0));
					oakAsm->ADD(oak::util::X10, oak::util::X10, 1);
					oakAsm->STR(oak::util::X10, oak::util::X9, oak::POffset<15, 3>(0));
				}
#endif
				oakEmitJmp(base + n->codeOffset);
			}
			mvu.prog.x86ptr = oakEndBlock();
            if (patched && job->dropped)
            {
                sticky::state.dropLive.store(true, std::memory_order_release);
                if (sticky::state.tripped.load(std::memory_order_acquire))
                {
                    sticky::state.rebuildDue.store(true, std::memory_order_release);
                    sticky::state.rejected.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }
                sticky::state.dropped.fetch_add(1, std::memory_order_relaxed);
            }
			if (patched)
			{
				// Include every word read by the graph/lookahead, not only emitted bodies.
				for (u32 i = 0; i < 0x1000;)
				{
					if (!job->read[i])
					{
						++i;
						continue;
					}
					u32 start = i;
					while (i < 0x1000 && job->read[i])
						++i;
					mVUsetupRange(mvu, start * 4, true);
					mVUsetupRange(mvu, i * 4, false);
				}
                std::unordered_map<void*, void*> replacements;
                std::vector<u32> replacedSlots;
				for (auto& n : job->nodes)
				{
					// Use the exact internal-state block selected while resolving
					// private baselines, including active delay-slot VI backups.
					auto* block = n->baselineBlock;
					pxAssert(block);
					runtime.original.emplace(block, n->baseline);
                    replacements[block->x86ptrStart] = n->entry;
                    replacedSlots.push_back(n->pc >> 3);
					block->x86ptrStart = static_cast<u8*>(n->entry);
					runtime.programs[job->program].entries.insert(n->entry);
				}
                // Canonically equivalent nodes may replace the same block in
                // this batch. Resolve those short chains to its final entry.
                for (auto& entry : replacements)
                    for (size_t hop=0; hop<replacements.size(); ++hop)
                    {
                        const auto next=replacements.find(entry.second);
                        if (next==replacements.end() || next->second==entry.second) break;
                        entry.second=next->second;
                    }
                RelinkCachedEntries(mvu, job->program, replacements, std::move(replacedSlots));
				auto& program = runtime.programs[job->program];
				if (program.jobs.empty() || job->retier)
				{
					++program.tiers;
					program.runsAtTier = program.runs + program.jumpRuns;
					if (job->retier)
                    {
                        runtime.stats.retiers++;
                        program.declined.clear();
                    }
					else if (job->viaJump)
						runtime.stats.jumpTierUps++;
					if (program.tiers > live::kMaxRetiers)
						program.settled = true;
				}
                // Opt-in perf maps distinguish computation from ABI/dispatch
                // bridges without putting counters on the execution path.
                Perf::vu1.Register(base, job->code.size(), job->body ? "vu-aot-body" : "vu-aot-region");
                Perf::vu1.Register(base + job->code.size(),
                    static_cast<size_t>(mvu.prog.x86ptr - base) - job->code.size(), "vu-aot-bridge");
				program.jobs.push_back(job);
				for (const auto& node : job->nodes)
				{
					program.coveredRoots.insert(node->baselineBlock);
					program.coveredRootKeys.insert(CanonicalRootIdentity(node->pc, node->key.bytes));
				}
				BindStaticBranches(mvu, job);
				SeedAutomaticRoots(program);
				if (job->body) ++runtime.stats.bodyBlocks;
				regionCount.fetch_add(1, std::memory_order_relaxed);
			}
			mvu.prog.cur = saved;
			return patched;
		}
		MvuEmitEnv Environment(microVU& mvu)
		{
			MvuEmitEnv e;
			e.vuIndex = 1;
			const auto off = [&](const void* p) { return static_cast<s32>(static_cast<const u8*>(p) - reinterpret_cast<const u8*>(mvu.macFlag)); };
			e.offDivFlag = off(&mvu.divFlag);
			e.offVIbackup = off(&mvu.VIbackup);
			e.offVIxgkick = off(&mvu.VIxgkick);
			e.offBranch = off(&mvu.branch);
			e.offCycles = off(&mvu.cycles);
			e.offNeonBackup = off(mvu.aotBackup);
			e.xgkickFn = reinterpret_cast<void*>(mVU_XGKICK_);
			e.vifTop = &mvu.getVifRegs().top;
			e.vifItop = &mvu.getVifRegs().itop;
			e.clampOverflow = CHECK_VU_OVERFLOW(1);
			e.clampExtra = CHECK_VU_EXTRA_OVERFLOW(1);
			e.clampSign = CHECK_VU_SIGN_OVERFLOW(1);
			e.softwareFlush = !mVUUpperUseHardwareInputFlush_oaknut(1);
			e.flagHack = EmuConfig.Speedhacks.vuFlagHack;
			e.vuMemPinned = true;
			e.threadVU1 = THREAD_VU1;
			// This config snapshot is also validated at publication; affected gamefixes decline.
			e.xgkickHack = CHECK_XGKICKHACK;
			e.ibitHack = EmuConfig.Gamefixes.IbitHack;
			e.overflowHack = CHECK_VUOVERFLOWHACK;
			e.addSubHack = CHECK_VUADDSUBHACK;
			e.vuSyncHack = EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack;
			return e;
		}
		#include "microVU_VuAotSaved-arm64.inl"
	} // namespace
	bool Enabled() { return EmuConfig.Cpu.Recompiler.EnableVUAot && !resolving; }
	void Reset(microVU& mvu)
	{
		if (mvu.index != 1)
			return;
		runtime.worker.Stop();
		RestoreStaticBranches(); // before sidecars/save or any baseline owner can be freed
        sticky::state.active.store(EmuConfig.Cpu.Recompiler.EnableVUAot, std::memory_order_release);
        sticky::state.rebuildDue.store(false, std::memory_order_release);
        sticky::state.dropLive.store(false, std::memory_order_release);
        // Baseline persistence must never retain an inbound cell pointing into
        // an AOT slab. Restore every generation's entries before SaveAllPrograms.
        for (const auto& [program, state] : runtime.programs)
        {
            std::unordered_map<void*, void*> replacements;
            std::vector<u32> slots;
            for (const auto& job : state.jobs)
                for (const auto& node : job->nodes)
                {
                    replacements[node->entry] = node->baseline;
                    slots.push_back(node->pc >> 3);
                }
            RelinkCachedEntries(mvu, program, replacements, std::move(slots));
        }
		saved::Save(mvu);
		for (const auto& [block, entry] : runtime.original)
			block->x86ptrStart = static_cast<u8*>(entry);
		++runtime.generation;
		runtime.programs.clear();
		runtime.readyBytes = runtime.readyJobs = 0;
		runtime.readySerial = 0;
		runtime.original.clear();
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && defined(__ANDROID__) && !defined(NDEBUG)
        char property[PROP_VALUE_MAX]{};
        logStats=__system_property_get("debug.emucorex.vu_aot_stats",property)>0 && property[0]=='1';
        if(!manualEntryCount)
            countEntries=__system_property_get("debug.emucorex.vu_aot_count",property)>0 && property[0]=='1';
        if (!sticky::state.manualKeepExact)
            sticky::state.keepExact = !(__system_property_get("debug.emucorex.vu_drop_sticky", property)>0 && property[0]=='1');
        if (logStats)
            __android_log_print(ANDROID_LOG_INFO, "EmuCoreX",
                "AOT_CFG enabled=%d clamp=%d extra=%d sign=%d flaghack=%d xgkick=%d ibit=%d overflowhack=%d addsub=%d vusync=%d mtvu=%d sticky_exact=%d sticky_cause=%u",
                EmuConfig.Cpu.Recompiler.EnableVUAot, CHECK_VU_OVERFLOW(1), CHECK_VU_EXTRA_OVERFLOW(1), CHECK_VU_SIGN_OVERFLOW(1),
                EmuConfig.Speedhacks.vuFlagHack, CHECK_XGKICKHACK, EmuConfig.Gamefixes.IbitHack,
                CHECK_VUOVERFLOWHACK, CHECK_VUADDSUBHACK, EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack,
                THREAD_VU1, !sticky::Allowed(), sticky::state.cause.load(std::memory_order_relaxed));
#endif
	}
    void ObserveImage(microVU& mvu)
    {
        if (mvu.index == 1) sticky::Observe(mvu.regs().Micro, live::TripCause::ProgramImage);
    }
    void BeforeProgram(microVU& mvu)
    {
        // SetStartPC also occurs on MSCNT. Keep a carried pipeline intact;
        // rebuilding is deferred until an end cleared its entry-search state.
        if (!sticky::state.rebuildDue.load(std::memory_order_acquire)) return;
        static constexpr microRegInfo empty{};
        if (std::memcmp(&mvu.prog.lpState, &empty, sizeof(empty)) != 0) return;
        sticky::state.resets.fetch_add(1, std::memory_order_relaxed);
        mVUreset(mvu, false, true);
    }
	void Remember(microVU& mvu, const vuaot::MvuBlockTrace& trace)
	{
		if (mvu.index != 1)
			return;
        if (!sticky::state.tripped.load(std::memory_order_relaxed))
            for (const auto& op : trace.ops)
                if (!op.iBit)
                {
                    const auto pair = DecodePair(mvu.regs().Micro, 0x4000, op.pc);
                    const auto lower = pair.lower.op;
                    if ((lower == LOp::FSAND || lower == LOp::FSEQ || lower == LOp::FSOR) && (pair.lower.imm12 & 0xfc0))
                        sticky::Trip(live::TripCause::Compile);
                }
		auto& p = runtime.programs[mvu.prog.cur];
		microBlock* const block = mvu.prog.IRinfo.pBlock;
		const auto previous = p.traceIndex.find(block);
		if (previous != p.traceIndex.end()) p.traces[previous->second].trace = trace;
		else
		{
			p.traceIndex[block] = p.traces.size();
			p.traces.push_back({trace, block});
		}
		if (p.rootsSeeded) QueueAutomaticRoot(p, block, trace.startPC, trace.entryState);
	}
	void* CanonicalStaticTarget(const void* target)
	{
		const auto found = runtime.privateForNative.find(target);
		return found == runtime.privateForNative.end() ? const_cast<void*>(target) : found->second.baseline;
	}
	void* PrivateStaticTarget(microVU& mvu, u32 pc, const void* state)
	{
		PipeState key;
		std::memcpy(key.bytes, state, sizeof(key.bytes));
		return Baseline(mvu, pc, key);
	}
	void RememberStaticBranch(microVU& mvu, microProgram* owner, void* site, const void* privateTarget)
	{
		if (mvu.index != 1 || !EmuConfig.Cpu.Recompiler.EnableVUAot || !owner || !site || !privateTarget) return;
		StaticBranch link;
		link.owner = owner; link.site = static_cast<u32*>(site);
		link.privateTarget = link.liveTarget = CanonicalStaticTarget(privateTarget);
		link.generation = runtime.generation;
		std::memcpy(&link.originalWord, site, sizeof(link.originalWord));
		u32 expected;
		if (!StaticBranchWord(link, link.privateTarget, expected) || link.originalWord != expected) return;
		runtime.pendingStaticBranches.push_back(link);
	}
	void FinishBaselineCompile(microVU& mvu) { FinishStaticBranches(mvu); }
	void RememberHydratedBlock(microVU& mvu, microProgram* owner, microBlock* block, u32 pc, const void* state)
	{
		if (mvu.index == 1 && EmuConfig.Cpu.Recompiler.EnableVUAot)
			QueueAutomaticRoot(runtime.programs[owner], block, pc, state);
	}
	static void RemoveReady(Program& program, size_t index)
	{
		const auto& job = program.ready[index];
		pxAssert(runtime.readyJobs && runtime.readyBytes >= job->code.size());
		--runtime.readyJobs;
		runtime.readyBytes -= job->code.size();
		program.ready.erase(program.ready.begin() + index);
	}
	static bool EquivalentReady(const BuildJob& a, const BuildJob& b)
	{
		if (a.generation != b.generation || a.trace.startPC != b.trace.startPC ||
			std::memcmp(a.trace.entryState, b.trace.entryState, 96) ||
			!(a.cpu == b.cpu) || !(a.fixes == b.fixes) || !(a.speed == b.speed) ||
			a.viaJump != b.viaJump || a.retier != b.retier || a.dropAllowed != b.dropAllowed ||
			a.jumpTargets != b.jumpTargets)
			return false;
		// The pending snapshot need not have been analyzed yet. Every word
		// the already accepted job read must have the same meaning in it.
		for (u32 i = 0; i < 0x1000; ++i)
			if (a.read[i] && std::memcmp(a.micro.data() + i * 4, b.micro.data() + i * 4, 4))
				return false;
		return true;
	}
	static bool HasReady(const Program& program, const BuildJob& job)
	{
		return std::any_of(program.ready.begin(), program.ready.end(), [&](const auto& ready) {
			return EquivalentReady(*ready, job);
		});
	}
	static void EnforceReadyLimits(Program& program)
	{
		while (program.ready.size() > readyProgramLimit)
		{
			RemoveReady(program, 0);
			ReadyStat(&Stats::readyEvicted);
		}
		while (runtime.readyJobs > readyJobLimit || runtime.readyBytes > readyByteLimit)
		{
			Program* oldest = nullptr;
			for (auto& item : runtime.programs)
				if (!item.second.ready.empty() && (!oldest ||
					item.second.ready.front()->readyOrder < oldest->ready.front()->readyOrder))
					oldest = &item.second;
			pxAssert(oldest);
			RemoveReady(*oldest, 0);
			ReadyStat(&Stats::readyEvicted);
		}
	}
	static void AddReady(Program& program, const std::shared_ptr<BuildJob>& job)
	{
		if (HasReady(program, *job))
		{
			ReadyStat(&Stats::readyDuplicates);
			return;
		}
		if (job->code.size() > readyByteLimit || !readyJobLimit || !readyProgramLimit)
		{
			ReadyStat(&Stats::readyEvicted);
			return;
		}
		job->readyOrder = ++runtime.readySerial;
		program.ready.push_back(job);
		++runtime.readyJobs;
		runtime.readyBytes += job->code.size();
		EnforceReadyLimits(program);
	}
	static bool PublishReady(microVU& mvu, Program& program)
	{
		if (oakHasBlock()) return false;
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
		if (holdReadyPublication) return false;
#endif
		bool changed = false;
		for (size_t i = 0; i < program.ready.size();)
		{
			const auto job = program.ready[i];
			if (!ValidRuntime(*job, mvu) || !OwnerMatches(*job))
			{
				RemoveReady(program, i);
				ReadyStat(&Stats::readyStale);
				continue;
			}
			if (!ImageMatches(*job, mvu) || !OwnerImageMatches(*job, mvu))
			{
				++i;
				continue;
			}
			RemoveReady(program, i);
			// Publish retains the full validation immediately before touching
			// code, and owns its normal code-cache episode on this VU thread.
			if (Publish(mvu, job))
			{
				changed = true;
				ReadyStat(&Stats::readyPublished);
			}
			else NoteDecline(*job);
		}
		return changed;
	}
	bool CompileBody(microVU& mvu, const vuaot::MvuBlockTrace& trace)
	{
		if (mvu.index != 1 || !Enabled() || !oakHasBlock() ||
			vuaot::g_vuAotShadowCompile || trace.ops.empty())
			return false;
		const auto owner = runtime.programs.find(mvu.prog.cur);
		if (owner == runtime.programs.end() || owner->second.jobs.empty())
			return false; // only blocks encountered after this program tiered up
		if (trace.ops.back().pc < trace.startPC || trace.ops.back().pc + 8 >= mvu.microMemSize)
			return false; // preserve the baseline second pass's wrapped-range bookkeeping

		// Native arithmetic precedes the ordinary branch/conditional/JR tail.
		// Keep runtime boundaries on the baseline path; its second-pass
		// framing still owns conditions, flag/PQ remapping and jump caches.
		for (const auto& op : trace.ops)
			if (op.tBit || op.dBit || op.mBit || op.badBranch || op.evilBranch || op.branch > 10)
				return false;
		std::array<u8, 0x4000> image;
		std::memcpy(image.data(), mvu.regs().Micro, image.size());
		const MvuBlockTrace captured = trace;
		const auto env = Environment(mvu);
		BodyTier tier(1, image.data());
		tier.schedule = true;
		bg::CodeSink sink;
		sink.Begin();
		bool lowered = false;
		try
		{
			lowered = tier.TryLower(captured, env, nullptr);
		}
		catch (const std::exception&)
		{
			// The ordinary second pass remains untouched on any decline.
		}
		const u32 bytes = sink.End();
		if (!lowered || !sink.failed.empty() ||
			(tier.Block().endKind != BlockEnd::EBit && tier.Block().endKind != BlockEnd::Branch &&
			 tier.Block().endKind != BlockEnd::CondBranch && tier.Block().endKind != BlockEnd::Jump) ||
			tier.Block().eBitOnBranch)
			return false;
		// Inline bodies are part of the baseline persistence episode. Accept
		// only process-independent module/data relocations, then forward every
		// address-bearing site to its existing ASLR-safe recorder.
		for (const auto& fixup : sink.fixups)
		{
			if (fixup.kind != bg::Fixup::OutsideAbs && fixup.kind != bg::Fixup::External26)
				return false;
			const size_t width = fixup.kind == bg::Fixup::OutsideAbs ? 16 : 4;
			if (fixup.offset > bytes || width > bytes - fixup.offset)
				return false;
			mVUPersist::AddressRegion region;
			s64 offset;
			if (!mVUPersist::ClassifyAddressRegion(reinterpret_cast<const void*>(fixup.value), region, offset))
				return false;
		}

		EntryAbi(mvu);
		oakLoad128(oak::QReg(26), {oak::util::X25, offsetof(vuaot::mVU_Globals, minvals)});
		oakLoad128(oak::QReg(25), {oak::util::X25, offsetof(vuaot::mVU_Globals, maxvals)});
		u8* const body = oakGetCurrentCodePointer();
		for (u32 at = 0; at < bytes; at += 4)
		{
			u32 instruction;
			std::memcpy(&instruction, sink.Base() + at, 4);
			oakAsm->dw(instruction);
		}
		const bool hasCalls = std::any_of(sink.fixups.begin(), sink.fixups.end(), [](const auto& fixup) {
			return fixup.kind == bg::Fixup::External26;
		});
		oak::Label afterCalls;
		if (hasCalls) oakAsm->B(afterCalls);
		for (const auto& fixup : sink.fixups)
		{
			const auto* target = reinterpret_cast<const void*>(fixup.value);
			u8* const at = body + fixup.offset;
			if (fixup.kind == bg::Fixup::OutsideAbs)
			{
				if (oakAddressRecorder) oakAddressRecorder->OnMovRun(at, 4, target);
			}
			else
			{
				// A nearby veneer keeps BL in range from any code-cache placement.
				// Its absolute helper address is recorded by the ordinary emitter.
				const void* veneer = oakGetCurrentCodePointer();
				oakMoveAddressToReg(oak::util::X17, target);
				oakAsm->BR(oak::util::X17);
				const bool patched = bg::PatchRel26(reinterpret_cast<u32*>(at), at, veneer);
				pxAssertRel(patched, "inline VU body helper veneer out of range");
				if (oakAddressRecorder)
					oakAddressRecorder->OnBranch(at, veneer, OakAddressRecorder::BranchForm::Rel26);
			}
		}
		if (hasCalls) oakAsm->l(afterCalls);
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
		if (forceSync || countEntries)
		{
			// Lowering owns X9/X10 here. Count before restoring the baseline
			// ABI so none of its VI/status/PQ homes can be overwritten. The
			// ordinary recorder persists this module-data address for ASLR.
			oakMoveAddressToReg(oak::util::X9, &inlineBodyEntries[(captured.startPC & 0x3ff8) >> 3]);
			oakAsm->LDR(oak::util::X10, oak::util::X9, oak::POffset<15, 3>(0));
			oakAsm->ADD(oak::util::X10, oak::util::X10, 1);
			oakAsm->STR(oak::util::X10, oak::util::X9, oak::POffset<15, 3>(0));
		}
#endif
		ExitAbi();
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
		if ((forceSync || countEntries) && tier.Block().endKind == BlockEnd::EBit)
		{
			oakMoveAddressToReg(oak::util::X9, &nativeEndCount);
			oakAsm->LDR(oak::util::X10, oak::util::X9, oak::POffset<15, 3>(0));
			oakAsm->ADD(oak::util::X10, oak::util::X10, 1);
			oakAsm->STR(oak::util::X10, oak::util::X9, oak::POffset<15, 3>(0));
		}
#endif
		++runtime.stats.bodyBlocks;
		return true;
	}
	static bool TryAutomaticRoot(microVU& mvu, Program& program)
	{
		if (!program.rootsSeeded || program.jobs.empty() || program.pending || program.automaticRoots.empty())
			return false;
		const AutomaticRoot root = program.automaticRoots.front();
		program.automaticRoots.pop_front();
		if (program.coveredRoots.count(root.block) ||
			program.coveredRootKeys.count(CanonicalRootIdentity(root.pc, root.key.bytes)) ||
			program.IsDeclined(root.pc, root.key.bytes)) return true;
		if (!OwnerImageMatches(mvu.prog.cur, mvu))
		{
			program.automaticRoots.push_front(root); // an overlay may return later
			return true;
		}
		MvuBlockTrace trace;
		const auto known = program.traceIndex.find(root.block);
		if (known != program.traceIndex.end() && known->second < program.traces.size())
			trace = program.traces[known->second].trace;
		else
		{
			// Hydrated blocks have no compiler callback. Reconstruct only the
			// one offered root, as saved-plan replay already does, never scan all
			// micro memory or compile a replacement baseline block here.
			const auto env = Environment(mvu);
			PipeConfig cfg;
			cfg.vuIndex = 1; cfg.flagHack = env.flagHack;
			cfg.ibitHack = env.ibitHack; cfg.xgkickHack = env.xgkickHack;
			PipeAnalyzer(mvu.regs().Micro, cfg).Analyze(root.pc, root.key).ToTrace(trace, 1);
		}
		for (const auto& op : trace.ops)
			if (op.tBit || op.dBit || op.mBit || op.badBranch || op.evilBranch) return true;
		std::memcpy(trace.entryState, root.key.bytes, sizeof(root.key.bytes));
		auto job = std::make_shared<BuildJob>();
		job->trace = std::move(trace); // independent of Remember vector growth during Publish
		job->program = mvu.prog.cur;
		job->generation = runtime.generation;
		job->dropAllowed = sticky::Allowed();
		job->jumpTargets = program.jumpTargets;
		std::memcpy(job->micro.data(), mvu.regs().Micro, job->micro.size());
		job->env = Environment(mvu);
		job->cpu = EmuConfig.Cpu; job->fixes = EmuConfig.Gamefixes; job->speed = EmuConfig.Speedhacks;
		if (HasReady(program, *job)) return true;
		if (forceSync)
		{
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
			++runtime.stats.automaticJobs;
#endif
			job->Run();
			if (!Publish(mvu, job))
			{
				if (!job->accepted) program.NoteDeclined(job->trace);
				NoteDecline(*job);
			}
		}
		else
		{
			runtime.worker.Start();
			if (runtime.worker.Submit(job))
			{
				program.pending = job;
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
				++runtime.stats.automaticJobs;
#endif
			}
			else program.automaticRoots.push_front(root);
		}
		return true;
	}
	static void* RefetchPublishedEntry(Program& program, u32 pc, const microRegInfo& key, void* fallback)
	{
		for (auto job = program.jobs.rbegin(); job != program.jobs.rend(); ++job)
			for (const auto& node : (*job)->nodes)
				if (node->pc == pc && SameKey(node->key, reinterpret_cast<const u8*>(&key), (*job)->env.flagHack))
					return node->entry;
		return fallback;
	}
	static void* DispatchState(microVU& mvu, u32 pc, const void* state, void* baselineEntry, bool viaJump)
	{
		// Baseline resolution during publication can compile another node and
		// overwrite lpState. Selection must use this dispatch's original key.
		microRegInfo entryKey;
		std::memcpy(&entryKey, state, sizeof(entryKey));
        // Match the baseline entry lookup, including hydrated programs whose
        // raw carried state still contains expired backup/flag labels.
        entryKey.viBackUp = mVUcanonicalViBackup(entryKey);
        entryKey.flagInfo = mVUcanonicalFlagInfo(entryKey);
		auto& program = runtime.programs[mvu.prog.cur];
		bool changed = saved::Load(mvu, program);
		std::vector<std::shared_ptr<bg::Job>> completed;
		std::vector<microProgram*> touched;
		if (runtime.worker.CompletedCount())
			runtime.worker.TakeCompleted(completed);
		for (auto& item : completed)
		{
			auto job = std::static_pointer_cast<BuildJob>(item);
			auto it = runtime.programs.find(job->program);
			if (it != runtime.programs.end())
			{
				if (it->second.pending == job) it->second.pending.reset();
				if (!ValidRuntime(*job, mvu) || !OwnerMatches(*job))
				{
					ReadyStat(&Stats::readyStale);
					continue;
				}
				if (!job->accepted)
				{
					it->second.NoteDeclined(job->trace);
					NoteDecline(*job);
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
					if (countEntries)
						std::fprintf(stderr, "VU1 AOT worker rejected: accepted=%d reason=%s\n",
							job->accepted, job->error.c_str());
#endif
					continue;
				}
				if (!ImageMatches(*job, mvu) || !OwnerImageMatches(*job, mvu)) ReadyStat(&Stats::readyDeferred);
				AddReady(it->second, job);
				if (std::find(touched.begin(), touched.end(), job->program) == touched.end())
					touched.push_back(job->program);
			}
		}
		// Retry the current program even without a new worker completion:
		// its overlay may just have returned. Publishing may compile baseline
		// fallbacks, so never iterate the program hash table across that call.
		changed |= PublishReady(mvu, program);
		for (microProgram* owner : touched)
			if (owner != mvu.prog.cur)
			{
				const auto it = runtime.programs.find(owner);
				if (it != runtime.programs.end()) PublishReady(mvu, it->second);
			}
		if (viaJump)
		{
			++program.jumpRuns;
			++runtime.stats.jumpEntries;
		}
		else
		{
			++program.runs;
			++runtime.stats.dispatches;
		}
		LogStats();
		// A normal lookup already returns the latest patched entry. Only a job
		// published during this call needs a search through its new node keys.
		if (changed)
			for (auto job = program.jobs.rbegin(); job != program.jobs.rend(); ++job)
			{
				bool found = false;
				for (auto& n : (*job)->nodes)
					if (n->pc == pc && SameKey(n->key, reinterpret_cast<const u8*>(&entryKey), (*job)->env.flagHack))
					{
						baselineEntry = n->entry;
						found = true;
						break;
					}
				if (found)
					break;
			}
		saved::StartPlan(mvu, program);
		if (program.pending)
			return baselineEntry;
		const u32 total = program.runs + program.jumpRuns;
		bool retier = false;
		if (!program.jobs.empty() && !program.settled &&
			(total >> 10) != program.checkBucket)
		{
			program.checkBucket = total >> 10;
			if (total - program.runsAtTier >= live::kRetierMinRuns && program.missesSeen != runtime.jumpMisses)
			{
				program.missesSeen = runtime.jumpMisses;
				++runtime.stats.retierChecks;
				retier = HarvestTargets(*mvu.prog.cur, program) != 0;
				if (retier)
					program.emptyChecks = 0;
				else if (++program.emptyChecks >= live::kSettleChecks)
					program.settled = true;
			}
		}
		if (!retier)
		{
			const size_t priorJobs = program.jobs.size();
			if (TryAutomaticRoot(mvu, program))
				return program.jobs.size() != priorJobs ? RefetchPublishedEntry(program, pc, entryKey, baselineEntry) : baselineEntry;
			if (program.entries.count(baselineEntry))
				return baselineEntry;
			const bool hot = viaJump ? (program.jumpRuns >= jumpTierUpRuns &&
				static_cast<u64>(program.runs) * 16 <= program.jumpRuns) : program.runs >= tierUpRuns;
			if ((!forceSync && program.jobs.empty() && !hot) ||
				program.IsDeclined(pc, &entryKey))
				return baselineEntry;
		}
		const Remembered* root = nullptr;
		for (auto& r : program.traces)
			if (r.trace.startPC == pc && r.block->x86ptrStart == baselineEntry)
			{
				root = &r;
				break;
			}
        if (!root)
        {
            // Hydrated baseline blocks have no compiler-time Remember callback.
            // Reconstruct their analysis only when this exact entry is offered
            // to AOT, using the same analyzer as saved-plan replay.
            auto* manager = mvu.prog.cur->block[pc >> 3];
            if (!manager) return baselineEntry;
            microRegInfo canonical;
            auto* block = manager->search(mvu, mVUcanonicalizeSearchState(&entryKey, canonical));
            if (!block || block->x86ptrStart != baselineEntry) return baselineEntry;
            PipeState key;
            std::memcpy(key.bytes, &entryKey, 96);
            const auto env = Environment(mvu);
            PipeConfig cfg;
            cfg.vuIndex = 1; cfg.flagHack = env.flagHack;
            cfg.ibitHack = env.ibitHack; cfg.xgkickHack = env.xgkickHack;
            MvuBlockTrace trace;
            PipeAnalyzer(mvu.regs().Micro, cfg).Analyze(pc, key).ToTrace(trace, 1);
            program.traces.push_back({std::move(trace), block});
            root = &program.traces.back();
        }
		if (program.jobs.empty())
			HarvestTargets(*mvu.prog.cur, program);
		auto job = std::make_shared<BuildJob>();
		job->trace = root->trace;
        job->dropAllowed = sticky::Allowed();
		job->program = mvu.prog.cur;
		job->generation = runtime.generation;
		job->viaJump = viaJump;
		job->retier = retier;
		job->jumpTargets = program.jumpTargets;
		program.missesSeen = runtime.jumpMisses;
		std::memcpy(job->micro.data(), mvu.regs().Micro, 0x4000);
		job->env = Environment(mvu);
		job->cpu = EmuConfig.Cpu;
		job->fixes = EmuConfig.Gamefixes;
		job->speed = EmuConfig.Speedhacks;
		if (HasReady(program, *job))
		{
			ReadyStat(&Stats::readyDuplicates);
			return baselineEntry;
		}
		if (forceSync)
		{
			job->Run();
			if (Publish(mvu, job))
				return job->nodes[0]->entry;
			if (!job->accepted) program.NoteDeclined(job->trace);
			NoteDecline(*job);
			std::fprintf(stderr, "VU1 AOT decline: %s accepted=%d\n", job->error.c_str(), job->accepted);
		}
		else
		{
			runtime.worker.Start();
			if (runtime.worker.Submit(job))
				program.pending = job;
		}
		return baselineEntry;
	}
	void* Dispatch(microVU& mvu, u32 pc, void* baselineEntry)
	{
		return DispatchState(mvu, pc, &mvu.prog.lpState, baselineEntry, false);
	}
	void* AfterJump(microVU& mvu, u32 pc, const void* state, void* baselineEntry)
	{
		return mvu.index == 1 && Enabled() ? DispatchState(mvu, pc, state, baselineEntry, true) : baselineEntry;
	}
	void OnJumpMiss(microVU& mvu)
	{
		if (mvu.index == 1 && Enabled())
			++runtime.jumpMisses;
	}
	Stats GetStats()
    {
        auto stats = runtime.stats;
        stats.stickyTrips = sticky::state.trips.load(std::memory_order_relaxed);
        stats.stickyResets = sticky::state.resets.load(std::memory_order_relaxed);
        stats.stickyDropped = sticky::state.dropped.load(std::memory_order_relaxed);
        stats.stickyRejected = sticky::state.rejected.load(std::memory_order_relaxed);
        return stats;
    }
    u32 CodegenModes()
    {
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
        return ((forceSync || countEntries) ? 1u : 0u) | (sticky::state.keepExact ? 2u : 0u);
#else
        return 0;
#endif
    }
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
    bool CachedTargetsMatchForTest(u32 sourceA, u32 sourceB, u32 target)
    {
        const auto find = [&](u32 source) -> const microJumpCache* {
            auto* prog=microVU1.prog.quick[source>>3].prog;
            if (!prog || !prog->block[source>>3]) return nullptr;
            auto* manager=prog->block[source>>3];
            for(auto* head : {manager->getQBlockList(),manager->getFBlockList()})
                for(auto* link=head;link;link=link->next)
                    if(link->block.jumpCache && link->block.jumpCache[target>>3].prog)
                        return &link->block.jumpCache[target>>3];
            return nullptr;
        };
        const auto* a=find(sourceA); const auto* b=find(sourceB);
        return a && b && a->prog==b->prog && a->x86ptrStart==b->x86ptrStart;
    }
    void TestRelinkJumpCaches(bool enabled) { relinkJumpCachesForTest = enabled; }
    void SetPipelineForTest(const void* key, u32 viBackup)
    {
        std::memcpy(&microVU1.prog.lpState, key, 96);
        microVU1.VIbackup = viBackup;
    }
    bool ReadSavedForTest(vuaot::persist::File& file)
    {
        auto& mvu = microVU1;
        if (!mvu.prog.cur || !mvu.prog.cur->contentKeyValid) return false;
        const auto id = saved::Identity(mvu, *mvu.prog.cur);
        file = {}; file.id = id;
        std::vector<u8> bytes;
        return mVUProgCache::ReadSidecar(1,mVUProgCache::ProgramKey(mvu.prog.cur->contentKey),bytes) &&
            vuaot::persist::Parse(bytes.data(),bytes.size(),id,file)==vuaot::persist::ParseResult::Ok;
    }
    bool QueueSavedForTest(const vuaot::persist::File& file)
    {
        auto bytes=std::make_shared<std::vector<u8>>(vuaot::persist::Serialize(file));
        if (bytes->empty()) return false;
        mVUProgCache::QueueSidecar(1,{file.id.contentHashLo,file.id.contentHashHi},[bytes] { return *bytes; });
        mVUProgCache::FlushSidecars(1);
        return true;
    }
	void ForceSynchronous(bool enabled) { forceSync = enabled; }
    void AllowStickyElisionForTest(bool enabled) { sticky::state.keepExact = !enabled; sticky::state.manualKeepExact = true; }
	u64 RegionCount() { return regionCount.load(std::memory_order_relaxed); }
	u64 EntryCount() { return entryCount; }
	u64 NativeEndCount() { return nativeEndCount; }
	u64 InlineBodyEntryCount(u32 pc) { return inlineBodyEntries[(pc & 0x3ff8) >> 3]; }
	u64 NativeRegionEntryCount(u32 pc) { return nativeRegionEntries[(pc & 0x3ff8) >> 3]; }
	u64 CurrentOwnerForTest() { return reinterpret_cast<u64>(microVU1.prog.cur); }
	u32 BoundStaticLinksForTest()
	{
		return static_cast<u32>(std::count_if(runtime.staticBranches.begin(), runtime.staticBranches.end(),
			[](const StaticBranch& link) { return link.liveTarget != link.privateTarget; }));
	}
	bool TestStaticLinkWords(bool restore)
	{
		if (oakHasBlock() || !BoundStaticLinksForTest()) return false;
		const auto links = runtime.staticBranches;
		for (const auto& link : links)
		{
			u32 original, expected, actual;
			if (link.generation != runtime.generation || !StaticBranchWord(link, link.privateTarget, original) ||
				original != link.originalWord || !StaticBranchWord(link, link.liveTarget, expected)) return false;
			std::memcpy(&actual, link.site, sizeof(actual));
			if (actual != expected) return false;
		}
		if (restore)
		{
			RestoreStaticBranches(); // same routine used before Reset releases its owners
			for (const auto& link : links)
			{
				u32 actual; std::memcpy(&actual, link.site, sizeof(actual));
				if (actual != link.originalWord) return false;
			}
			if (!runtime.staticBranches.empty() || !runtime.pendingStaticBranches.empty() ||
				!runtime.staticSiteIndex.empty() || !runtime.inboundBranches.empty() ||
				!runtime.privateForNative.empty() || !runtime.nativeForPrivate.empty()) return false;
		}
		return true;
	}
	u64 BailCount()
	{
		u64 count = 0;
		for (const auto& program : runtime.programs)
			for (const auto& job : program.second.jobs)
				count += job->counters.bail[0] + job->counters.bail[1];
		return count;
	}
    void CountEntries(bool on) { countEntries = on; manualEntryCount = true; }
    void SetTierUpRuns(u32 runs) { tierUpRuns = std::max(1u, runs); }
	void SetJumpTierUpRuns(u32 runs) { jumpTierUpRuns = std::max(1u, runs); }
    void HoldWorker(bool hold) { runtime.worker.Hold(hold); }
    void WaitWorker() { runtime.worker.WaitIdle(); }
    u32 PendingCount()
    {
        u32 count = 0;
        for (const auto& entry : runtime.programs) count += entry.second.pending != nullptr;
        return count;
    }
	u32 ReadyCountForTest() { return runtime.readyJobs; }
	u64 ReadyBytesForTest() { return runtime.readyBytes; }
	void TestReadyLimits(u64 bytes, u32 jobs, u32 perProgram)
	{
		readyByteLimit = bytes; readyJobLimit = jobs; readyProgramLimit = perProgram;
		for (auto& item : runtime.programs) EnforceReadyLimits(item.second);
	}
	void TestHoldReadyPublication(bool hold) { holdReadyPublication = hold; }
	bool TestDuplicateReady()
	{
		for (auto& item : runtime.programs)
			if (!item.second.ready.empty())
			{
				const auto job = item.second.ready.front();
				AddReady(item.second, job);
				return true;
			}
		return false;
	}
	bool TestStaleReady(u32 kind)
	{
		for (auto& item : runtime.programs)
			if (!item.second.ready.empty())
			{
				auto& job = *item.second.ready.front();
				if (kind == 0) job.generation ^= 1;
				else job.cpu.VU1FPCR.bitmask ^= u64(1) << 22;
				return true;
			}
		return false;
	}
	bool TestExpandReadyOwner(bool observed)
	{
		microVU& mvu = microVU1;
		for (auto& item : runtime.programs)
			if (!item.second.ready.empty())
			{
				const auto job = item.second.ready.front();
				auto& owner = *job->program;
				for (u32 word = 0; word < 0x1000; ++word)
				{
					u32 snapshot, live;
					std::memcpy(&snapshot, job->micro.data() + word * 4, 4);
					std::memcpy(&live, mvu.regs().Micro + word * 4, 4);
					const s32 pc = word * 4;
					if (!job->read[word] || snapshot == live ||
						!std::any_of(owner.ranges->begin(), owner.ranges->end(), [&](const auto& range) {
							return range.start <= pc && pc < range.end;
						})) continue;
					// Model a partial owner before this word was observed, then
					// optionally expand it through the actual range/cache writer.
					std::deque<microRange> partial;
					for (const auto& range : *owner.ranges)
						if (range.start <= pc && pc < range.end)
						{
							if (range.start < pc) partial.push_back({range.start, pc});
							if (pc + 4 < range.end) partial.push_back({pc + 4, range.end});
						}
						else partial.push_back(range);
					owner.ranges->swap(partial);
					owner.data[word] = live;
					if (observed)
					{
						auto* previous = mvu.prog.cur;
						const auto isSame = mvu.prog.isSame;
						mvu.prog.cur = &owner; mvu.prog.isSame = -1;
						mVUsetupRange(mvu, pc, true); mVUsetupRange(mvu, pc + 4, false);
						mvu.prog.cur = previous; mvu.prog.isSame = isSame;
					}
					const bool known = std::any_of(owner.ranges->begin(), owner.ranges->end(), [&](const auto& range) {
						return range.start <= pc && pc < range.end;
					});
					return known == observed && owner.data[word] != snapshot;
				}
			}
		return false;
	}
	bool TestRetryReadySnapshot()
	{
		microVU& mvu = microVU1;
		for (auto& item : runtime.programs)
			if (!item.second.ready.empty())
			{
				const auto job = item.second.ready.front();
				std::array<u8, 0x4000> live;
				std::memcpy(live.data(), mvu.regs().Micro, live.size());
				std::memcpy(mvu.regs().Micro, job->micro.data(), live.size());
				ScopedGuard restore([&] { std::memcpy(mvu.regs().Micro, live.data(), live.size()); });
				return PublishReady(mvu, item.second);
			}
		return false;
	}
	bool TestReadyBodyExit(u32 mode, u32& targetPC)
	{
		targetPC = 0;
		microVU& mvu = microVU1;
		for (auto& item : runtime.programs)
			for (const auto& retained : item.second.ready)
			{
				const auto job = retained;
				if (!job->body) continue;
				for (const auto& site : job->sites)
				{
					if (site.jump || site.end.valid || site.pc == job->trace.startPC) continue;
					const auto known = [&](u32 word) {
						return std::any_of(job->program->ranges->begin(), job->program->ranges->end(), [&](const auto& range) {
							return range.start <= static_cast<s32>(word * 4) && static_cast<s32>(word * 4 + 4) <= range.end;
						});
					};
					// Flag lookahead reads the first T-bit pair and then stops.
					// Its later cached child instructions remain genuine baseline
					// dependencies even though BodyTier never decoded them.
					u32 childWord = site.pc / 4;
					const u32 childEnd = std::min(childWord + 16, 0x1000u);
					while (childWord < childEnd && (job->read[childWord] || !known(childWord))) ++childWord;
					if (childWord == childEnd) continue;
					u32 live, snapshot;
					std::memcpy(&live, mvu.regs().Micro + childWord * 4, 4);
					std::memcpy(&snapshot, job->micro.data() + childWord * 4, 4);
					if (live != snapshot || job->program->data[childWord] != live) continue;
					// The job and cached baseline child are real. Mutate only the
					// publication input named by this compatibility control; never
					// execute while the temporary live instruction is changed.
					u32 word = childWord;
					if (mode == 2)
					{
						for (word = 0xfff; word > 0 && (known(word) || job->read[word]); --word) {}
						if (word == 0) continue;
					}
					u8* const bytes = mode == 1 ? job->micro.data() : mvu.regs().Micro;
					u32 original;
					std::memcpy(&original, bytes + word * 4, 4);
					const u32 altered = original ^ 1u;
					std::memcpy(bytes + word * 4, &altered, 4);
					ScopedGuard restore([&] { std::memcpy(bytes + word * 4, &original, 4); });
					targetPC = site.pc;
					return PublishReady(mvu, item.second);
				}
			}
		return false;
	}
#endif
} // namespace mvu_aot
