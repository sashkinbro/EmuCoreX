// SPDX-License-Identifier: GPL-3.0+
// UNWIRED EE COP1 LWC1/SWC1 raw-memory integration fixture.
// Include in jit_oracle.cpp after the RunEEIrCop1Direct() definition
// (currently around line 4542) and the EE run/snapshot helpers.
// Entry: EEIrCop1MemoryTests().

extern "C" void EmuCoreXOracleGetEeIrCop1MemoryCounts(u64* lwc1, u64* swc1);

namespace
{
	constexpr u32 EECop1MemAlias = 0x4a000000u;
	constexpr u32 EECop1MemMmio = 0x1fd00000u;
	constexpr u32 EECop1MemZeroAlias = 0x00007000u;
	constexpr u32 EECop1MemPage = vtlb_private::VTLB_PAGE_SIZE;
	constexpr u32 EECop1MemHandlerValue = 0xff812345u;

	struct EECop1MemTrace
	{
		u32 reads = 0;
		u32 writes = 0;
		u32 address = 0;
		u32 value = 0;
		u32 pc = 0;
		u32 bd = 0;
		u32 cycle_before = 0;
		u32 cycle_after = 0;
		bool exit_immediately = false;
	};

	EECop1MemTrace s_ee_cop1_mem_trace;

	u32 EECop1MemRead32(u32 address)
	{
		auto& trace = s_ee_cop1_mem_trace;
		++trace.reads;
		trace.address = address;
		trace.pc = cpuRegs.pc;
		trace.bd = cpuRegs.IsDelaySlot;
		trace.cycle_before = cpuRegs.cycle;
		cpuRegs.cycle += 7;
		cpuRegs.nextEventCycle = cpuRegs.cycle + 4096;
		trace.cycle_after = cpuRegs.cycle;
		if (trace.exit_immediately)
			recExitExecutionImmediate();
		return EECop1MemHandlerValue;
	}

	void EECop1MemWrite32(u32 address, u32 value)
	{
		auto& trace = s_ee_cop1_mem_trace;
		++trace.writes;
		trace.address = address;
		trace.value = value;
		trace.pc = cpuRegs.pc;
		trace.bd = cpuRegs.IsDelaySlot;
		trace.cycle_before = cpuRegs.cycle;
		cpuRegs.cycle += 7;
		cpuRegs.nextEventCycle = cpuRegs.cycle + 4096;
		trace.cycle_after = cpuRegs.cycle;
	}

	struct EECop1MemRestore
	{
		Pcsx2Config::RecompilerOptions config = EmuConfig.Cpu.Recompiler;
		R5900cpu* cpu = Cpu;
		vtlb_private::VTLBPhysical physical = vtlb_private::vtlbdata.pmap[EECop1MemMmio >> 12];
		const bool zero_alias_is_identity =
			vtlb_private::vtlbdata.vmap[EECop1MemZeroAlias >> 12].raw() ==
			vtlb_private::VTLBVirtual(vtlb_private::vtlbdata.pmap[EECop1MemZeroAlias >> 12],
				EECop1MemZeroAlias, EECop1MemZeroAlias).raw();

		~EECop1MemRestore()
		{
			EmuConfig.Cpu.Recompiler = config;
			Cpu = cpu;
			vtlb_VMapUnmap(EECop1MemAlias, EECop1MemPage);
			// Memory.cpp documents the low 512 MiB virtual map as direct-mapped.
			// Recreate that default through VTLB so fastmem/reverse-map state agrees.
			if (zero_alias_is_identity)
				vtlb_VMap(EECop1MemZeroAlias, EECop1MemZeroAlias, EECop1MemPage);
			if (physical.isHandler())
				vtlb_MapHandler(physical.assumeHandler(), EECop1MemMmio, EECop1MemPage);
			else
				vtlb_MapBlock(reinterpret_cast<void*>(physical.assumePtr()), EECop1MemMmio, EECop1MemPage);
		}
	};

	std::array<u64, 2> EECop1MemoryCounts()
	{
		std::array<u64, 2> counts{};
		EmuCoreXOracleGetEeIrCop1MemoryCounts(&counts[0], &counts[1]);
		return counts;
	}

	std::array<GPR_reg, 32> EECop1MemoryGprs(u32 base, u32 base_register = 8)
	{
		std::array<GPR_reg, 32> gpr{};
		for (u32 reg = 1; reg < gpr.size(); ++reg)
		{
			gpr[reg].UD[0] = 0x1234567800000000ull | (0x76543210u + reg * 0x101u);
			gpr[reg].UD[1] = 0xabcdef0100000000ull | (0x89abcdefu + reg * 0x103u);
		}
		gpr[0] = {};
		if (base_register != 0)
			gpr[base_register].UD[0] = base;
		return gpr;
	}

	std::array<u32, 32> EECop1MemoryFprs(u32 target, u32 value)
	{
		std::array<u32, 32> fpr{};
		for (u32 reg = 0; reg < fpr.size(); ++reg)
			fpr[reg] = 0x5a13579bu ^ (reg * 0x01020408u);
		fpr[target] = value;
		return fpr;
	}

	std::array<u32, 32> EECop1MemoryFcr()
	{
		std::array<u32, 32> fcr{};
		for (u32 reg = 0; reg < fcr.size(); ++reg)
			fcr[reg] = 0x87654321u ^ (reg * 0x01020304u);
		fcr[0] = 0x2e00u;
		fcr[31] = 0xa5a5c058u;
		return fcr;
	}

	void EECop1MemResidentExecute(const std::array<GPR_reg, 32>& gpr,
		const std::array<u32, 32>& fpr)
	{
		std::memset(&cpuRegs, 0, sizeof(cpuRegs));
		std::memset(&fpuRegs, 0, sizeof(fpuRegs));
		std::memcpy(cpuRegs.GPR.r, gpr.data(), sizeof(cpuRegs.GPR.r));
		cpuRegs.CP0.n.Status.val = 0x20000000u; // Status.CU1.
		for (u32 reg = 0; reg < 32; ++reg)
			fpuRegs.fpr[reg].UL = fpr[reg];
		cpuRegs.pc = EE_TEST_PC;
		cpuRegs.nextEventCycle = 0x7fffffffu;
		EEsCycle = EEoCycle = 0;
		EmuCoreXEEForceExitAfterFirstBlock();
		Cpu->Execute();
	}

	u32 EECop1PatternWord(u32 offset)
	{
		return static_cast<u32>(static_cast<u8>(offset * 13u + 7u)) |
			(static_cast<u32>(static_cast<u8>((offset + 1) * 13u + 7u)) << 8) |
			(static_cast<u32>(static_cast<u8>((offset + 2) * 13u + 7u)) << 16) |
			(static_cast<u32>(static_cast<u8>((offset + 3) * 13u + 7u)) << 24);
	}

	std::array<u8, EE_SCRATCH_SIZE> EECop1ExpectedScratch(u32 offset, u32 raw, bool store)
	{
		std::array<u8, EE_SCRATCH_SIZE> bytes{};
		for (u32 i = 0; i < bytes.size(); ++i)
			bytes[i] = static_cast<u8>(i * 13u + 7u);
		if (store)
			for (u32 byte = 0; byte < 4; ++byte)
				bytes[offset + byte] = static_cast<u8>(raw >> (byte * 8));
		return bytes;
	}

	void CheckCop1MemoryCounts(const std::array<u64, 2>& before, u64 load_delta, u64 store_delta,
		const char* name)
	{
		const auto after = EECop1MemoryCounts();
		Check(after[0] == before[0] + load_delta && after[1] == before[1] + store_delta, name);
	}
}

void EEIrCop1MemoryTests()
{
	EECop1MemRestore restore;
	Cpu = &recCpu;
	EmuConfig.Cpu.Recompiler.EnableEE = true;
	EmuConfig.Cpu.Recompiler.EnableEECache = false;
	EmuConfig.Cpu.Recompiler.EnableFastmem = true;
	EmuConfig.Cpu.Recompiler.PauseOnTLBMiss = false;

	std::array<u32, 32> initial_cp0{};
	initial_cp0[12] = 0x20000000u; // Status.CU1.
	const auto initial_fcr = EECop1MemoryFcr();
	constexpr std::array<u32, 8> raw_edges = {
		0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u,
		0x7f800000u, 0xff800000u, 0x7fc12345u, 0xff812345u};
	constexpr std::array<u32, 3> fpr_indices = {0, 1, 31};
	constexpr u32 ram_offset = 0x40;

	// LWC1 must copy arbitrary words into an FPR without float conversion.
	for (u32 pattern = 0; pattern < raw_edges.size(); ++pattern)
	for (u32 fd : fpr_indices)
	{
		const u32 raw = raw_edges[pattern];
		auto gpr = EECop1MemoryGprs(EE_TEST_SCRATCH + ram_offset);
		gpr[10].UL[0] = raw;
		auto fpr = EECop1MemoryFprs(fd, 0xdeadbeefu);
		const std::vector<u32> code = {
			MipsI(43, 8, 10, 0), // Seed RAM with the exact raw word.
			MipsI(49, 8, fd, 0)};
		auto expected_fpr = fpr;
		expected_fpr[fd] = raw;
		const auto expected_scratch = EECop1ExpectedScratch(ram_offset, raw, true);
		const auto before = EECop1MemoryCounts();
		const EESnapshot interp = RunEEProgram(false, code, false, nullptr, initial_cp0.data(),
			gpr.data(), fpr.data(), initial_fcr.data());
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, gpr.data(), initial_cp0.data(),
			fpr.data(), initial_fcr.data());
		Check(EECop1MemoryCounts() == before, "COP1 LWC1 forced legacy leaves shared counter unchanged");
		const auto shared_before = EECop1MemoryCounts();
		const EESnapshot shared = RunEEProgramWithIrMode(1, code, gpr.data(), initial_cp0.data(),
			fpr.data(), initial_fcr.data());
		CheckCop1MemoryCounts(shared_before, 1, 0, "COP1 LWC1 shared route admission counter");
		EESnapshot direct{};
		const bool direct_ok = RunEEIrCop1Direct(code, gpr.data(), fpr.data(), initial_fcr.data(), direct);
		CompareEE(interp, legacy, "COP1 LWC1 interpreter/legacy full state");
		CompareEE(interp, shared, "COP1 LWC1 interpreter/shared full state");
		Check(direct_ok && direct.fpr[fd] == raw && direct.fcr31 == initial_fcr[31] &&
			std::memcmp(direct.scratch, expected_scratch.data(), sizeof(direct.scratch)) == 0 &&
			memRead32(EE_TEST_SCRATCH + ram_offset) == raw,
			"COP1 LWC1 direct frontend/lowering preserves raw RAM word including FPR0");
		Check(std::memcmp(interp.fpr, expected_fpr.data(), sizeof(interp.fpr)) == 0 &&
			std::memcmp(legacy.fpr, expected_fpr.data(), sizeof(legacy.fpr)) == 0 &&
			std::memcmp(shared.fpr, expected_fpr.data(), sizeof(shared.fpr)) == 0 &&
			std::memcmp(shared.scratch, expected_scratch.data(), sizeof(shared.scratch)) == 0 &&
			std::memcmp(shared.fcr, initial_fcr.data(), sizeof(shared.fcr)) == 0,
			"COP1 LWC1 raw independent FPR/FCR model");
		Check(legacy.cycle == shared.cycle, "COP1 LWC1 exact legacy/shared cycles");
	}

	// SWC1 stores the exact FPR payload and preserves all adjacent bytes.
	for (u32 pattern = 0; pattern < raw_edges.size(); ++pattern)
	for (u32 fs : fpr_indices)
	{
		const u32 raw = raw_edges[pattern];
		const u32 offset = 0x80;
		auto gpr = EECop1MemoryGprs(EE_TEST_SCRATCH + offset);
		auto fpr = EECop1MemoryFprs(fs, raw);
		const std::vector<u32> code = {MipsI(57, 8, fs, 0)};
		const auto expected_scratch = EECop1ExpectedScratch(offset, raw, true);
		const auto before = EECop1MemoryCounts();
		const EESnapshot interp = RunEEProgram(false, code, false, nullptr, initial_cp0.data(),
			gpr.data(), fpr.data(), initial_fcr.data());
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, gpr.data(), initial_cp0.data(),
			fpr.data(), initial_fcr.data());
		Check(EECop1MemoryCounts() == before, "COP1 SWC1 forced legacy leaves shared counter unchanged");
		const auto shared_before = EECop1MemoryCounts();
		const EESnapshot shared = RunEEProgramWithIrMode(1, code, gpr.data(), initial_cp0.data(),
			fpr.data(), initial_fcr.data());
		CheckCop1MemoryCounts(shared_before, 0, 1, "COP1 SWC1 shared route admission counter");
		EESnapshot direct{};
		const bool direct_ok = RunEEIrCop1Direct(code, gpr.data(), fpr.data(), initial_fcr.data(), direct);
		Check(direct_ok && std::memcmp(shared.scratch, expected_scratch.data(), sizeof(shared.scratch)) == 0 &&
			std::memcmp(direct.scratch, expected_scratch.data(), sizeof(direct.scratch)) == 0,
			"COP1 SWC1 direct lowering stores raw FPR bits to RAM");
		CompareEE(interp, legacy, "COP1 SWC1 interpreter/legacy full state");
		CompareEE(interp, shared, "COP1 SWC1 interpreter/shared full state");
		Check(std::memcmp(shared.fpr, fpr.data(), sizeof(shared.fpr)) == 0 &&
			std::memcmp(shared.fcr, initial_fcr.data(), sizeof(shared.fcr)) == 0 &&
			std::memcmp(interp.scratch, expected_scratch.data(), sizeof(interp.scratch)) == 0 &&
			std::memcmp(legacy.scratch, expected_scratch.data(), sizeof(legacy.scratch)) == 0 &&
			shared.cycle == legacy.cycle, "COP1 SWC1 preserves FPR/FCR and exact cycles");
	}

	// Load-to-store dependency proves same-block memory ordering and FPR0 source
	// handling. LWC1 writes FPR0, then SWC1 reads that freshly loaded word.
	{
		const u32 load_offset = 0xa0, store_offset = 0xa4, raw = 0xff812345u;
		auto gpr = EECop1MemoryGprs(EE_TEST_SCRATCH + load_offset);
		auto fpr = EECop1MemoryFprs(0, 0xdeadbeefu);
		gpr[10].UL[0] = raw;
		const std::vector<u32> code = {
			MipsI(43, 8, 10, 0), MipsI(49, 8, 0, 0), MipsI(57, 8, 0, 4)};
		auto expected_scratch = EECop1ExpectedScratch(load_offset, raw, true);
		for (u32 byte = 0; byte < 4; ++byte)
			expected_scratch[store_offset + byte] = static_cast<u8>(raw >> (byte * 8));
		const auto before = EECop1MemoryCounts();
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, gpr.data(), initial_cp0.data(),
			fpr.data(), initial_fcr.data());
		Check(EECop1MemoryCounts() == before, "COP1 overlapping LWC1/SWC1 legacy counter unchanged");
		const auto shared_before = EECop1MemoryCounts();
		const EESnapshot shared = RunEEProgramWithIrMode(1, code, gpr.data(), initial_cp0.data(),
			fpr.data(), initial_fcr.data());
		CheckCop1MemoryCounts(shared_before, 1, 1, "COP1 overlapping LWC1/SWC1 route counts");
		EESnapshot direct{};
		const bool direct_ok = RunEEIrCop1Direct(code, gpr.data(), fpr.data(), initial_fcr.data(), direct);
		Check(direct_ok && shared.fpr[0] == raw && direct.fpr[0] == raw &&
			std::memcmp(shared.scratch, expected_scratch.data(), sizeof(shared.scratch)) == 0 &&
			std::memcmp(direct.scratch, expected_scratch.data(), sizeof(direct.scratch)) == 0 &&
			std::memcmp(shared.fcr, initial_fcr.data(), sizeof(shared.fcr)) == 0,
			"COP1 LWC1-to-FPR0 then SWC1 observes same-block raw value");
		CompareEE(legacy, shared, "COP1 overlapping LWC1/SWC1 full state");
	}

	// GPR0 addressing uses the signed immediate from zero. A temporary virtual
	// alias maps that low address to the deterministic scratch RAM page.
	Check(restore.zero_alias_is_identity,
		"COP1 base-GPR0 fixture starts with the documented low direct-map VTLB alias");
	if (restore.zero_alias_is_identity)
	{
		vtlb_VMap(EECop1MemZeroAlias, EE_TEST_SCRATCH, EECop1MemPage);
		const u32 address = EECop1MemZeroAlias + 0x20;
		const u32 raw = EECop1PatternWord(0x20);
		auto gpr = EECop1MemoryGprs(0, 0);
		auto fpr = EECop1MemoryFprs(0, 0xdeadbeefu);
		const std::vector<u32> code = {MipsI(49, 0, 0, address)};
		const auto before = EECop1MemoryCounts();
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, gpr.data(), initial_cp0.data(),
			fpr.data(), initial_fcr.data());
		Check(EECop1MemoryCounts() == before, "COP1 LWC1 base GPR0 forced legacy counter unchanged");
		const auto shared_before = EECop1MemoryCounts();
		const EESnapshot shared = RunEEProgramWithIrMode(1, code, gpr.data(), initial_cp0.data(),
			fpr.data(), initial_fcr.data());
		Check(EECop1MemoryCounts() == std::array<u64, 2>{shared_before[0] + 1, shared_before[1]},
			"COP1 LWC1 base GPR0 route and zero-destination count");
		const auto expected_scratch = EECop1ExpectedScratch(0x20, raw, false);
		Check(legacy.fpr[0] == raw && shared.fpr[0] == raw && shared.gpr[0] == 0,
			"COP1 LWC1 base GPR0 and FPR0 are architectural data registers");
		Check(std::memcmp(shared.scratch, expected_scratch.data(), sizeof(shared.scratch)) == 0 &&
			std::memcmp(shared.fcr, initial_fcr.data(), sizeof(shared.fcr)) == 0,
			"COP1 LWC1 base GPR0 leaves adjacent RAM and FCR unchanged");
		CompareEE(legacy, shared, "COP1 LWC1 GPR0 base full state");
	}

	// MMIO must use the width-specific helper. The callbacks observe precise
	// delay-slot PC/BD, signed raw words, and cycle updates in legacy and IR.
	const vtlbHandler handler = vtlb_RegisterHandler(nullptr, nullptr, EECop1MemRead32, nullptr, nullptr,
		nullptr, nullptr, EECop1MemWrite32, nullptr, nullptr);
	vtlb_MapHandler(handler, EECop1MemMmio, EECop1MemPage);
	vtlb_VMap(EECop1MemAlias, EECop1MemMmio, EECop1MemPage);
	for (u32 kind = 0; kind < 3; ++kind)
	{
		const bool load = kind == 0;
		const bool annul = kind == 2;
		const u32 branch_op = annul ? 0x14u : 4u; // BEQL for annul, BEQ otherwise.
		auto gpr = EECop1MemoryGprs(EECop1MemAlias);
		gpr[10].UD[0] = 1;
		gpr[11].UD[0] = 2;
		const u32 op = load ? MipsI(49, 8, 7, 0) : MipsI(57, 8, 7, 0);
		const u32 branch = kind == 1 ? MipsI(branch_op, 10, 11, 1) :
			(annul ? MipsI(branch_op, 10, 11, 1) : MipsI(4, 0, 0, 1));
		const std::vector<u32> code = {branch, op};
		auto fpr = EECop1MemoryFprs(7, 0xff812345u);
		const u32 expected_fpr = annul ? fpr[7] : EECop1MemHandlerValue;
		const auto run = [&](int mode, EESnapshot& result, EECop1MemTrace& trace) {
			s_ee_cop1_mem_trace = {};
			if (mode < 0)
				result = RunEEProgram(false, code, false, nullptr, initial_cp0.data(), gpr.data(),
					fpr.data(), initial_fcr.data());
			else
				result = RunEEProgramWithIrMode(mode, code, gpr.data(), initial_cp0.data(),
					fpr.data(), initial_fcr.data());
			trace = s_ee_cop1_mem_trace;
		};
		EESnapshot interp{}, legacy{}, shared{};
		EECop1MemTrace interp_trace{}, legacy_trace{}, shared_trace{};
		const auto before = EECop1MemoryCounts();
		run(-1, interp, interp_trace);
		run(0, legacy, legacy_trace);
		Check(EECop1MemoryCounts() == before, "COP1 delay-slot forced legacy leaves counters unchanged");
		const auto shared_before = EECop1MemoryCounts();
		run(1, shared, shared_trace);
		CheckCop1MemoryCounts(shared_before, load ? 1 : 0, load ? 0 : 1,
			"COP1 delay-slot shared admission counter counts accepted compiled slot");
		CompareEE(interp, legacy, "COP1 memory delay interpreter/legacy full state");
		CompareEE(interp, shared, "COP1 memory delay interpreter/shared full state");
		const bool executes = !annul;
		const u32 expected_calls = executes ? 1u : 0u;
		Check(legacy_trace.reads == (load ? expected_calls : 0u) &&
			legacy_trace.writes == (load ? 0u : expected_calls) &&
			shared_trace.reads == legacy_trace.reads && shared_trace.writes == legacy_trace.writes &&
			(!executes || (shared_trace.address == EECop1MemMmio && shared_trace.pc == EE_TEST_PC + 4 &&
				shared_trace.bd == 1 && shared_trace.cycle_after == shared_trace.cycle_before + 7 &&
				(load || shared_trace.value == 0xff812345u))),
			"COP1 MMIO delay/annul handler observes exact address, PC, BD, raw word and cycles");
		Check((!load || shared.fpr[7] == expected_fpr) && legacy.cycle == shared.cycle,
			"COP1 MMIO LWC1/SWC1 raw result and exact legacy/shared cycles");
	}

	// Cache emulation must keep MMIO on its helper path while retaining correct
	// raw COP1 data; it is tested in both the ordinary and cache-enabled gates.
	for (bool cache_enabled : {false, true})
	{
		EmuConfig.Cpu.Recompiler.EnableEECache = cache_enabled;
		auto gpr = EECop1MemoryGprs(EECop1MemAlias);
		auto fpr = EECop1MemoryFprs(7, 0x7fc12345u);
		const std::vector<u32> code = {MipsI(49, 8, 7, 4), MipsI(57, 8, 7, 8)};
		const auto before = EECop1MemoryCounts();
		s_ee_cop1_mem_trace = {};
		const EESnapshot interp = RunEEProgram(false, code, false, nullptr, initial_cp0.data(),
			gpr.data(), fpr.data(), initial_fcr.data());
		s_ee_cop1_mem_trace = {};
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, gpr.data(), initial_cp0.data(),
			fpr.data(), initial_fcr.data());
		Check(EECop1MemoryCounts() == before, "COP1 MMIO cache legacy counter unchanged");
		const auto shared_before = EECop1MemoryCounts();
		s_ee_cop1_mem_trace = {};
		const EESnapshot shared = RunEEProgramWithIrMode(1, code, gpr.data(), initial_cp0.data(),
			fpr.data(), initial_fcr.data());
		const auto trace = s_ee_cop1_mem_trace;
		CheckCop1MemoryCounts(shared_before, 1, 1, "COP1 MMIO cache gate retains typed shared operations");
		CompareEE(interp, legacy, "COP1 MMIO cache interpreter/legacy full state");
		CompareEE(interp, shared, "COP1 MMIO cache interpreter/shared full state");
		Check(trace.reads == 1 && trace.writes == 1 && trace.address == EECop1MemMmio + 8 &&
			trace.value == EECop1MemHandlerValue && trace.pc == EE_TEST_PC + 4 && trace.bd == 0 &&
			shared.fpr[7] == EECop1MemHandlerValue && legacy.cycle == shared.cycle &&
			shared.fcr[31] == initial_fcr[31],
			"COP1 MMIO cache mode preserves read/write helper semantics and raw bits");
	}

	// Exercise LWC1's nonreturning fastmem fault thunk in a real recompiler
	// frame. The destination FPR must remain uncommitted at the fault, then the
	// same cached entry must replay through the helper and commit the raw word.
	EmuConfig.Cpu.Recompiler.EnableEECache = false;
	for (u32 cold = 0; cold < 2; ++cold)
	{
		const std::vector<u32> code = {
			MipsI(4, 0, 0, 1), MipsI(49, 8, 7, 0), MipsI(9, 0, 10, 0x1234)};
		auto gpr = EECop1MemoryGprs(EECop1MemAlias);
		gpr[7].UD[0] = 0x1122334455667788ull;
		gpr[10].UD[0] = 0x8877665544332211ull;
		auto fpr = EECop1MemoryFprs(7, 0x7fc54321u);

		// The normal legacy execution returns from the callback and establishes
		// its handler PC/BD/cycle contract for the later helper replay.
		s_ee_cop1_mem_trace = {};
		const EESnapshot legacy = RunEEProgramWithIrMode(0, code, gpr.data(), initial_cp0.data(),
			fpr.data(), initial_fcr.data(), false);
		const EECop1MemTrace legacy_trace = s_ee_cop1_mem_trace;

		EmuCoreXOracleSetEeIrMode(1);
		EmuCoreXOracleSetEeIrFastLoads(1);
		EmuCoreXOracleSetEeIrColdContext(static_cast<int>(cold));
		Cpu->Reset();
		s_ee_cop1_mem_trace = {};
		s_ee_cop1_mem_trace.exit_immediately = true;
		const auto compile_before = EECop1MemoryCounts();
		const u64 fast_before = EmuCoreXOracleGetEeIrFastLoadCounts();
		const u64 cold_before = EmuCoreXOracleGetEeIrColdContextCompiles();
		EECop1MemResidentExecute(gpr, fpr);
		const EECop1MemTrace exit_trace = s_ee_cop1_mem_trace;
		const auto exit_compile_counts = EECop1MemoryCounts();
		Check(exit_compile_counts[0] == compile_before[0] + 1 &&
			exit_compile_counts[1] == compile_before[1] &&
			EmuCoreXOracleGetEeIrFastLoadCounts() == fast_before + 1 &&
			EmuCoreXOracleGetEeIrColdContextCompiles() == cold_before + cold,
			"COP1 LWC1 nonreturning fault compiles one admitted typed and eligible fast load");
		Check(exit_trace.reads == 1 && exit_trace.address == EECop1MemMmio &&
			exit_trace.pc == EE_TEST_PC + 4 && exit_trace.bd == 1 &&
			exit_trace.cycle_after == exit_trace.cycle_before + 7 &&
			cpuRegs.pc == exit_trace.pc && cpuRegs.IsDelaySlot == 1 &&
			cpuRegs.GPR.r[10].UD[0] == gpr[10].UD[0] && fpuRegs.fpr[7].UL == fpr[7],
			"COP1 LWC1 immediate fault preserves FPR destination, following GPR, and delay PC/BD");

		s_ee_cop1_mem_trace = {};
		const auto replay_compile_before = EECop1MemoryCounts();
		const u64 replay_fast_before = EmuCoreXOracleGetEeIrFastLoadCounts();
		const u64 replay_cold_before = EmuCoreXOracleGetEeIrColdContextCompiles();
		EECop1MemResidentExecute(gpr, fpr);
		const EECop1MemTrace replay_trace = s_ee_cop1_mem_trace;
		const auto replay_compile_after = EECop1MemoryCounts();
		const u64 expected_load = EECop1MemHandlerValue;
		std::printf("EECOP1MEM_REPLAY cold=%u calls=%u address=%08x pc=%08x bd=%u cycle=%u->%u fpr7=%08x expected_fpr7=%08x gpr10=%016llx expected_gpr10=%016llx final_bd=%u lwc_delta=%llu fast_delta=%llu cold_delta=%llu legacy_cycle=%u\n",
			cold, replay_trace.reads, replay_trace.address, replay_trace.pc, replay_trace.bd,
			replay_trace.cycle_before, replay_trace.cycle_after, fpuRegs.fpr[7].UL,
			static_cast<u32>(expected_load),
			static_cast<unsigned long long>(cpuRegs.GPR.r[10].UD[0]),
			static_cast<unsigned long long>(legacy.gpr[10]), cpuRegs.IsDelaySlot,
			static_cast<unsigned long long>(replay_compile_after[0] - replay_compile_before[0]),
			static_cast<unsigned long long>(EmuCoreXOracleGetEeIrFastLoadCounts() - replay_fast_before),
			static_cast<unsigned long long>(EmuCoreXOracleGetEeIrColdContextCompiles() - replay_cold_before),
			static_cast<unsigned>(legacy.cycle));
		Check(replay_trace.reads == 1 && replay_trace.address == EECop1MemMmio &&
			replay_trace.pc == EE_TEST_PC + 4 && replay_trace.bd == 1 &&
			replay_trace.cycle_after == replay_trace.cycle_before + 7 &&
			fpuRegs.fpr[7].UL == expected_load && cpuRegs.GPR.r[10].UD[0] == legacy.gpr[10] &&
			legacy.gpr[10] == 0x1234u &&
			cpuRegs.IsDelaySlot == 0,
			"COP1 LWC1 same-entry helper replay commits raw FPR, completes its following GPR write, and clears BD");
		Check(replay_compile_after[0] == replay_compile_before[0] + 1 &&
			replay_compile_after[1] == replay_compile_before[1] &&
			EmuCoreXOracleGetEeIrFastLoadCounts() == replay_fast_before &&
			EmuCoreXOracleGetEeIrColdContextCompiles() == replay_cold_before &&
			cpuRegs.cycle == legacy.cycle && legacy_trace.pc == EE_TEST_PC + 4 &&
			legacy_trace.bd == 1 && legacy_trace.cycle_after == legacy_trace.cycle_before + 7,
			"COP1 LWC1 helper replay recompiles typed load without a new fastmem site and preserves legacy cycles");
	}

	EmuConfig.Cpu.Recompiler.EnableEECache = false;
	// Restore explicit cache policy through the guard at scope exit.
	EmuCoreXOracleSetEeIrColdContext(-1);
	EmuCoreXOracleSetEeIrFastLoads(-1);
	EmuCoreXOracleSetEeIrMode(-1);
}
