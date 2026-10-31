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
			std::vector<std::shared_ptr<BuildJob>> jobs;
			std::unordered_set<void*> entries;
			std::shared_ptr<BuildJob> pending;
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
		struct Runtime
		{
			bg::Worker worker;
			std::unordered_map<microProgram*, Program> programs;
			std::unordered_map<microBlock*, void*> original;
			u64 generation = 1;
			u64 jumpMisses = 0;
			Stats stats;
		};
		Runtime& runtime = *new Runtime(); // Generated entries outlive static destructor ordering.
		std::atomic<u64> regionCount{0};
		bool forceSync = false;
		u32 tierUpRuns = 200;
		u32 jumpTierUpRuns = 32;
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
		bool relinkJumpCachesForTest = true;
		u64 entryCount = 0;
		u64 nativeEndCount = 0;
		bool countEntries = false;
        bool manualEntryCount = false;
#if defined(__ANDROID__) && !defined(NDEBUG)
        bool logStats = false;
        u32 statsPoll = 0;
        std::chrono::steady_clock::time_point nextStats{};
#endif
#endif
		thread_local bool resolving = false;
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
			mVUblockFetch(mvu, pc, reinterpret_cast<uptr>(&state));
			microRegInfo canonical;
			microRegInfo* search = mVUcanonicalizeSearchState(&state, canonical);
			microBlock* block = mvu.prog.cur->block[pc >> 3]->search(mvu, search);
			if (outBlock)
				*outBlock = block;
			auto it = runtime.original.find(block);
			return it == runtime.original.end() ? block->x86ptrStart : it->second;
		}
		bool Valid(const BuildJob& job, microVU& mvu)
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
			for (u32 i = 0; i < 0x1000; ++i)
				if (job.read[i] && std::memcmp(job.micro.data() + i * 4, mvu.regs().Micro + i * 4, 4))
					return false;
			return true;
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
					microRegInfo key;
					std::memcpy(&key, n->key.bytes, 96);
					microRegInfo storage;
					auto* canonical = mVUcanonicalizeSearchState(&key, storage);
					auto* block = job->program->block[n->pc >> 3]->search(mvu, canonical);
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
		for (auto& r : p.traces)
			if (r.block == mvu.prog.IRinfo.pBlock)
			{
				r.trace = trace;
				return;
			}
		p.traces.push_back({trace, mvu.prog.IRinfo.pBlock});
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
		bool changed = saved::Load(mvu, runtime.programs[mvu.prog.cur]);
		std::vector<std::shared_ptr<bg::Job>> completed;
		if (runtime.worker.CompletedCount())
			runtime.worker.TakeCompleted(completed);
		for (auto& item : completed)
		{
			auto job = std::static_pointer_cast<BuildJob>(item);
			auto it = runtime.programs.find(job->program);
			if (it != runtime.programs.end())
			{
				it->second.pending.reset();
				if (Publish(mvu, job))
					changed |= job->program == mvu.prog.cur;
				else
				{
					if (!job->accepted) it->second.NoteDeclined(job->trace);
					NoteDecline(*job);
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
					if (countEntries)
						std::fprintf(stderr, "VU1 AOT worker rejected: accepted=%d reason=%s\n",
							job->accepted, job->error.c_str());
#endif
				}
			}
		}
		auto& program = runtime.programs[mvu.prog.cur];
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
#endif
} // namespace mvu_aot
