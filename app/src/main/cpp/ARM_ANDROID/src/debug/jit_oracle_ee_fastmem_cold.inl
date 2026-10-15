// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Targeted tests for moving only eligible EE IR fast Load32 PC/BD context
// stores from the successful hot body into its cold VTLB backpatch thunk.
// Include after jit_oracle_ee_fastmem_exit.inl and call
// EEFastmemColdContextTests().

extern "C" void EmuCoreXOracleSetEeIrColdContext(int mode);
extern "C" u64 EmuCoreXOracleGetEeIrColdContextCompiles();
extern "C" bool EmuCoreXOracleGetEeCompiledBlock(u32 guest_pc, const void** host, u32* bytes);

namespace
{
	struct EEFastmemColdTlbTrace
	{
		u32 calls = 0;
		u32 address = 0;
		u32 pc = 0;
		u32 bd = 0;
		u32 exception_pc = 0;
		u32 cause = 0;
		u32 resulting_pc = 0;
	};

	EEFastmemColdTlbTrace s_ee_fastmem_cold_tlb_trace;

	u32 EEFastmemColdTlbRead32(u32 address)
	{
		auto& trace = s_ee_fastmem_cold_tlb_trace;
		++trace.calls;
		trace.address = address;
		trace.pc = cpuRegs.pc;
		trace.bd = cpuRegs.IsDelaySlot;
		cpuRegs.CP0.n.BadVAddr = address;
		cpuException(EXC_CODE_TLBL, trace.bd);
		trace.exception_pc = cpuRegs.CP0.n.EPC;
		trace.cause = cpuRegs.CP0.n.Cause;
		trace.resulting_pc = cpuRegs.pc;
		recExitExecutionImmediate();
		return 0;
	}

	void EEFastmemColdContextBodyTests()
	{
		EEFastmemRestore restore;
		Cpu = &recCpu;
		EmuConfig.Cpu.Recompiler.EnableEE = true;
		EmuConfig.Cpu.Recompiler.EnableEECache = false;
		EmuConfig.Cpu.Recompiler.EnableFastmem = true;
		EmuConfig.Cpu.Recompiler.PauseOnTLBMiss = false;
		vtlb_VMap(EEFastmemAlias, EEFastmemRam, EEFastmemPage);
		constexpr u32 expected_word = 0x89abcdefu;
		memWrite32(EEFastmemRam, expected_word);
		const std::vector<u32> code = {
			MipsI(35, 8, 9, 0),                 // eligible fast Load32
			MipsI(4, 0, 0, 1), 0,              // bound the compiled body
			0x1000ffffu, 0};
		for (u32 i = 0; i < code.size(); ++i)
			memWrite32(EE_TEST_PC + i * 4, code[i]);
		const auto gpr = EEFastmemGprs(EEFastmemAlias);
		std::array<u32, 2> host_bytes{};
		std::array<std::vector<u8>, 2> bodies{};
		bool identical_site_count = true;
		for (u32 mode = 0; mode < 2; ++mode)
		{
			EmuCoreXOracleSetEeIrMode(1);
			EmuCoreXOracleSetEeIrFastLoads(1);
			EmuCoreXOracleSetEeIrColdContext(static_cast<int>(mode));
			Cpu->Reset();
			const u64 before_fast = EmuCoreXOracleGetEeIrFastLoadCounts();
			const u64 before_cold = EmuCoreXOracleGetEeIrColdContextCompiles();
			EEFastmemResidentExecute(gpr, 1); // No block-entry BD-clear invariant.
			const u64 fast_delta = EmuCoreXOracleGetEeIrFastLoadCounts() - before_fast;
			const u64 cold_delta = EmuCoreXOracleGetEeIrColdContextCompiles() - before_cold;
			const void* host = nullptr;
			Check(EmuCoreXOracleGetEeCompiledBlock(EE_TEST_PC, &host, &host_bytes[mode]) &&
				host != nullptr && host_bytes[mode] != 0,
				"EE fastmem cold-context test captures the compiled hot guest block before cache reset");
			if (host && host_bytes[mode])
			{
				bodies[mode].resize(host_bytes[mode]);
				std::memcpy(bodies[mode].data(), host, host_bytes[mode]);
			}
			identical_site_count &= fast_delta == 1 && cold_delta == mode;
			const u64 expected_lw = static_cast<u64>(static_cast<s64>(static_cast<s32>(expected_word)));
			std::printf("EEFASTMEMCOLD_BODY_SAMPLE mode=%u fast=%llu cold=%llu r9=%016llx expected=%016llx pc=%08x bd=%u\n",
				mode, static_cast<unsigned long long>(fast_delta), static_cast<unsigned long long>(cold_delta),
				static_cast<unsigned long long>(cpuRegs.GPR.r[9].UD[0]),
				static_cast<unsigned long long>(expected_lw), cpuRegs.pc, cpuRegs.IsDelaySlot);
			Check(cpuRegs.GPR.r[9].UD[0] == expected_lw &&
				cpuRegs.GPR.r[10].UD[0] == gpr[10].UD[0],
				"EE fastmem cold-context hot-body comparison preserves the successful sign-extended Load32 result");
			Check(cpuRegs.IsDelaySlot == 0,
				"EE successful fast Load32 clears a stale BD marker supplied at block entry");
		}
		Check(identical_site_count,
			"EE fastmem cold-context toggle retains one eligible site and selects exactly zero/one cold sites");
		Check(host_bytes[1] < host_bytes[0] && bodies[0].size() == host_bytes[0] &&
			bodies[1].size() == host_bytes[1] && bodies[0] != bodies[1],
			"EE cold-context lowering removes PC/BD stores from the successful hot native body");
		std::printf("EEFASTMEMCOLD_BODY baseline_hot_bytes=%u cold_context_hot_bytes=%u fast_sites=1 cold_sites=0/1\n",
			host_bytes[0], host_bytes[1]);
		EmuCoreXOracleSetEeIrColdContext(-1);
	}

	void EEFastmemColdContextSuccessClearTests()
	{
		EEFastmemRestore restore;
		Cpu = &recCpu;
		EmuConfig.Cpu.Recompiler.EnableEE = true;
		EmuConfig.Cpu.Recompiler.EnableEECache = false;
		EmuConfig.Cpu.Recompiler.EnableFastmem = true;
		EmuConfig.Cpu.Recompiler.PauseOnTLBMiss = false;
		const vtlbHandler handler = vtlb_RegisterHandler(nullptr, nullptr, EEFastmemRead32,
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
		vtlb_MapHandler(handler, EEFastmemMmio, EEFastmemPage);
		vtlb_VMap(EEFastmemAlias, EEFastmemRam, EEFastmemPage);
		vtlb_VMap(EEFastmemHighAlias, EEFastmemMmio, EEFastmemPage);
		constexpr u32 ram_word = 0x44332211u;
		memWrite32(EEFastmemRam, ram_word);
		const std::vector<u32> code = {
			MipsI(4, 0, 0, 1),
			MipsI(35, 8, 9, 0),              // successful direct RAM load in taken delay slot
			MipsI(35, 11, 10, 0),            // following ordinary MMIO helper observes PC/BD
			0x1000ffffu, 0};
		const auto gpr = [&] {
			auto result = EEFastmemGprs(EEFastmemAlias);
			result[11].UD[0] = EEFastmemHighAlias;
			return result;
		}();
		EmuCoreXOracleSetEeIrColdContext(0);
		EmuCoreXOracleSetEeIrFastLoads(0);
		s_ee_fastmem_trace = {};
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, gpr.data(), nullptr, nullptr, nullptr, true);
		for (u32 mode = 0; mode < 2; ++mode)
		{
			EmuCoreXOracleSetEeIrMode(1);
			EmuCoreXOracleSetEeIrFastLoads(1);
			EmuCoreXOracleSetEeIrColdContext(static_cast<int>(mode));
			s_ee_fastmem_trace = {};
			const u64 fast_before = EmuCoreXOracleGetEeIrFastLoadCounts();
			const EESnapshot actual = RunEEProgramWithIrMode(1, code, gpr.data(), nullptr,
				nullptr, nullptr, true);
			CompareEE(legacy, actual, "EE successful delay-slot RAM load then helper full-state parity");
			Check(legacy.cycle == actual.cycle,
				"EE successful delay-slot RAM load then helper preserves exact cycles");
			const u64 expected_helper_lw = static_cast<u64>(static_cast<s64>(
				static_cast<s32>(0x76543210u ^ EEFastmemMmio)));
			std::printf("EEFASTMEMCOLD_SUCCESS mode=%u calls=%u addr=%08x pc=%08x bd=%u final_bd=%u r9=%016llx expected_ram=%016llx r10=%016llx expected_helper=%016llx snapshot_pc=%08x cycles=%u/%u\n",
				mode, s_ee_fastmem_trace.calls, s_ee_fastmem_trace.address[0], s_ee_fastmem_trace.pc[0],
				s_ee_fastmem_trace.bd[0], cpuRegs.IsDelaySlot,
				static_cast<unsigned long long>(cpuRegs.GPR.r[9].UD[0]),
				static_cast<unsigned long long>(static_cast<u64>(static_cast<s64>(static_cast<s32>(ram_word)))),
				static_cast<unsigned long long>(actual.gpr[10]), static_cast<unsigned long long>(expected_helper_lw),
				actual.pc, static_cast<unsigned>(legacy.cycle), static_cast<unsigned>(actual.cycle));
			Check(s_ee_fastmem_trace.calls == 1 && s_ee_fastmem_trace.address[0] == EEFastmemMmio &&
				s_ee_fastmem_trace.pc[0] == EE_TEST_PC + 8 && s_ee_fastmem_trace.bd[0] == 0 &&
				cpuRegs.IsDelaySlot == 0 && EmuCoreXOracleGetEeIrFastLoadCounts() > fast_before &&
				cpuRegs.GPR.r[9].UD[0] ==
					static_cast<u64>(static_cast<s64>(static_cast<s32>(ram_word))),
				"EE fast RAM delay load clears BD before the following normal helper access");
			Check(actual.gpr[10] == expected_helper_lw,
				"EE following helper completes after successful delay-slot fast load");
		}
		EmuCoreXOracleSetEeIrColdContext(-1);
	}

	void EEFastmemColdContextScopeGuardTests()
	{
		EEFastmemRestore restore;
		Cpu = &recCpu;
		EmuConfig.Cpu.Recompiler.EnableEE = true;
		EmuConfig.Cpu.Recompiler.EnableEECache = false;
		EmuConfig.Cpu.Recompiler.EnableFastmem = true;
		vtlb_VMap(EEFastmemAlias, EEFastmemRam, EEFastmemPage);
		memWrite64(EEFastmemRam, 0x0123456789abcdefull);
		const auto gpr = EEFastmemGprs(EEFastmemAlias);
		EmuCoreXOracleSetEeIrColdContext(1);
		EmuCoreXOracleSetEeIrFastLoads(1);
		const u64 before_cold = EmuCoreXOracleGetEeIrColdContextCompiles();
		const u64 before_fast = EmuCoreXOracleGetEeIrFastLoadCounts();
		const std::array<u32, 4> generic_ee_ops = {
			MipsI(32, 8, 9, 0), MipsI(33, 8, 9, 0), // LB, LH
			MipsI(55, 8, 10, 0), MipsI(43, 8, 9, 8)}; // LD, SW
		for (u32 op : generic_ee_ops)
			RunEEProgramWithIrMode(1, {op}, gpr.data(), nullptr, nullptr, nullptr, false);
		const u64 after_generic_cold = EmuCoreXOracleGetEeIrColdContextCompiles();
		const u64 after_generic_fast = EmuCoreXOracleGetEeIrFastLoadCounts();
		RunEEProgramWithIrMode(0, {MipsI(35, 8, 9, 0)}, gpr.data(), nullptr, nullptr, nullptr, false);
		Check(after_generic_cold == before_cold && after_generic_fast == before_fast &&
			EmuCoreXOracleGetEeIrColdContextCompiles() == after_generic_cold &&
			EmuCoreXOracleGetEeIrFastLoadCounts() == after_generic_fast,
			"EE cold-context option leaves non-Load32 memory, stores, and legacy load adapters unchanged");
		const u32 iop_lw = MipsI(35, 8, 9, 0);
		ir::Function iop_fn;
		std::string error;
		u32 accepted = 0;
		const bool iop_lifted = IopIr::LiftSequence(&iop_lw, IOP_TEST_PC, 1, iop_fn, &accepted, &error) &&
			accepted == 1;
		EeIr::LowerHooks hooks;
		hooks.ee_fastmem_abi = true;
		hooks.before_memory = +[](void*, u32, bool) {};
		hooks.after_memory = +[](void*) {};
		hooks.guest_exit = +[](void*, u32, bool) { oakAsm->RET(); };
		EeIr::LowerOptions iop_options;
		iop_options.inline_body = true;
		iop_options.guest_state = Arm64Ir::GuestState::IOP;
		iop_options.fastmem_ee_loads = true;
		iop_options.fastmem_ee_cold_context = true;
		iop_options.hooks = &hooks;
		u8* const iop_code = SysMemory::GetEERec() + 0x2000000;
		oakSetAsmPtr(iop_code, 0x10000);
		oakStartBlock();
		EeIr::LowerOutput iop_out;
		const bool iop_lowered = iop_lifted && EeIr::LowerBlock(iop_fn, iop_options,
			iop_code, 0x10000, &iop_out, &error);
		oakEndBlock();
		Check(iop_lowered && iop_out.fastmem_ee_load_operations == 0 &&
			iop_out.cold_context_ee_load_operations == 0,
			"EE cold-context lowering remains excluded from IOP Load32 domain lowering");
		EmuCoreXOracleSetEeIrColdContext(-1);
	}

	void EEFastmemColdContextTlbExceptionTests();

	void EEFastmemColdContextTests()
	{
		EEFastmemColdContextBodyTests();
		// Existing fixtures execute the real custom-handler/backpatch route for
		// normal and delay loads (including taken, untaken, and annulled-likely
		// branches), then prove immediate exit and same-entry helper replay.
		for (u32 cold = 0; cold < 2; ++cold)
		{
			EmuCoreXOracleSetEeIrColdContext(static_cast<int>(cold));
			const u64 before_fault = EmuCoreXOracleGetEeIrColdContextCompiles();
			EEFastmemFaultReplayTests();
			const u64 after_fault = EmuCoreXOracleGetEeIrColdContextCompiles();
			Check(cold ? after_fault > before_fault : after_fault == before_fault,
				"EE cold-context branch/fault/replay suite selects only opted-in cold context sites");
			const u64 before_exit = EmuCoreXOracleGetEeIrColdContextCompiles();
			EEFastmemNonreturningHelperTests();
			const u64 after_exit = EmuCoreXOracleGetEeIrColdContextCompiles();
			Check(after_exit - before_exit == cold,
				"EE nonreturning custom VTLB helper fixture observes the selected cold-context route");
		}
		EEFastmemColdContextSuccessClearTests();
		EEFastmemColdContextScopeGuardTests();
		EEFastmemColdContextTlbExceptionTests();
		EmuCoreXOracleSetEeIrColdContext(-1);
		EmuCoreXOracleSetEeIrFastLoads(-1);
		EmuCoreXOracleSetEeIrMode(-1);
	}

	void EEFastmemColdContextTlbExceptionTests()
	{
		EEFastmemExitRestore restore;
		Cpu = &recCpu;
		EmuConfig.Cpu.Recompiler.EnableEE = true;
		EmuConfig.Cpu.Recompiler.EnableEECache = false;
		EmuConfig.Cpu.Recompiler.EnableFastmem = true;
		EmuConfig.Cpu.Recompiler.PauseOnTLBMiss = false;
		const vtlbHandler handler = vtlb_RegisterHandler(nullptr, nullptr, EEFastmemColdTlbRead32,
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
		vtlb_MapHandler(handler, EEFastmemExitHandlerPaddr, EEFastmemExitPage);
		vtlb_VMap(EEFastmemExitAlias, EEFastmemExitHandlerPaddr, EEFastmemExitPage);
		for (bool delay_slot : {false, true})
		for (u32 cold = 0; cold < 2; ++cold)
		{
			const std::vector<u32> code = delay_slot ?
				std::vector<u32>{MipsI(4, 0, 0, 1), MipsI(35, 8, 9, 0), MipsI(9, 0, 10, 0x1234)} :
				std::vector<u32>{MipsI(35, 8, 9, 0), MipsI(9, 0, 10, 0x1234)};
			for (u32 i = 0; i < code.size(); ++i)
				memWrite32(EE_TEST_PC + i * 4, code[i]);
			std::array<GPR_reg, 32> gpr{};
			gpr[8].UD[0] = EEFastmemExitAlias;
			gpr[9].UD[0] = 0x1122334455667788ull;
			gpr[10].UD[0] = 0x8877665544332211ull;
			EmuCoreXOracleSetEeIrMode(1);
			EmuCoreXOracleSetEeIrFastLoads(1);
			EmuCoreXOracleSetEeIrColdContext(static_cast<int>(cold));
			Cpu->Reset();
			s_ee_fastmem_cold_tlb_trace = {};
			const u64 before_fast = EmuCoreXOracleGetEeIrFastLoadCounts();
			const u64 before_cold = EmuCoreXOracleGetEeIrColdContextCompiles();
			EEFastmemResidentExecute(gpr);
			const u32 fault_pc = EE_TEST_PC + (delay_slot ? 4u : 0u);
			const u32 expected_epc = delay_slot ? EE_TEST_PC : fault_pc;
			Check(EmuCoreXOracleGetEeIrFastLoadCounts() == before_fast + 1 &&
				EmuCoreXOracleGetEeIrColdContextCompiles() == before_cold + cold,
				"EE cold-context CP0 exception fixture selects exactly one compiled fast/cold site");
			Check(s_ee_fastmem_cold_tlb_trace.calls == 1 &&
				s_ee_fastmem_cold_tlb_trace.address == EEFastmemExitHandlerPaddr &&
				s_ee_fastmem_cold_tlb_trace.pc == fault_pc &&
				s_ee_fastmem_cold_tlb_trace.bd == (delay_slot ? 1u : 0u),
				"EE custom VTLB callback receives exact cold-thunk guest PC/BD before CP0 exception");
			Check(s_ee_fastmem_cold_tlb_trace.exception_pc == expected_epc &&
				(s_ee_fastmem_cold_tlb_trace.cause & 0x7cu) == EXC_CODE_TLBL &&
				((s_ee_fastmem_cold_tlb_trace.cause >> 31) & 1u) == (delay_slot ? 1u : 0u) &&
				s_ee_fastmem_cold_tlb_trace.resulting_pc == 0x80000000u &&
				(cpuRegs.CP0.n.Status.val & 2u) != 0 && cpuRegs.IsDelaySlot == 0,
				"EE cold-context callback raises a real CP0 TLBL with architectural EPC/BD and exits immediately");
			Check(cpuRegs.GPR.r[9].UD[0] == gpr[9].UD[0] &&
				cpuRegs.GPR.r[10].UD[0] == gpr[10].UD[0],
				"EE CP0 TLB exception immediately exits before the faulting load or following instruction commits");
		}
		EmuCoreXOracleSetEeIrColdContext(-1);
	}
}
