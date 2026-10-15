// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// IOP GTE AVSZ3/AVSZ4 frontend, integration, and measured-code fixtures.

extern "C" u64 EmuCoreXOracleIopIrAvszFusions();
extern "C" void EmuCoreXOracleGetIopIrGteAvszCounts(u64* avsz3, u64* avsz4);

namespace
{
	struct IOPAvszInputs
	{
		std::array<u32, 4> sz;
		u32 zsf3;
		u32 zsf4;
	};

	struct IOPAvszRunResult
	{
		IOPSnapshot state{};
		s32 cycle_budget = 0;
		u32 cycle_carry = 0;
		u32 next_event = 0;
	};

	struct IOPAvszOutput
	{
		u32 mac0;
		u32 otz;
		u32 flag;
	};

	static u32 IOPAvszWord(bool avsz4, u32 aliases = 0)
	{
		return (0x12u << 26) | (16u << 21) | (avsz4 ? 0x2eu : 0x2du) | aliases;
	}

	static u32 IOPAvszIWord(u32 op, u32 rs, u32 rt, u32 imm)
	{
		return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xffffu);
	}

	static u32 IOPAvszTransfer(u32 rs, u32 rt, u32 rd)
	{
		return (0x12u << 26) | (rs << 21) | (rt << 16) | (rd << 11);
	}

	static IOPAvszRunResult IOPAvszRun(bool jit, bool force_gte_interpreter,
		const IOPAvszInputs& input, const std::vector<u32>& program)
	{
		std::memset(&psxRegs, 0, sizeof(psxRegs));
		for (u32 i = 0; i < 32; ++i)
		{
			psxRegs.CP2D.r[i] = 0x00112233u * (i + 1);
			psxRegs.CP2C.r[i] = 0x44556677u ^ (i * 0x01020304u);
		}
		// Direct fixture seeding preserves deliberately nonzero upper halves in
		// raw SZ registers; MTC2-to-SZ masks them and cannot test that contract.
		for (u32 i = 0; i < 4; ++i)
			psxRegs.CP2D.r[16 + i] = input.sz[i];
		psxRegs.CP2C.r[29] = input.zsf3;
		psxRegs.CP2C.r[30] = input.zsf4;

		const u32 sentinel = IOP_TEST_PC + static_cast<u32>(program.size()) * 4;
		for (u32 i = 0; i < program.size(); ++i)
			iopMemWrite32(IOP_TEST_PC + i * 4, program[i]);
		iopMemWrite32(sentinel, 0x1000ffffu);
		iopMemWrite32(sentinel + 4, 0);
		for (u32 i = 0; i < IOP_SCRATCH_SIZE; ++i)
			iopMemWrite8(IOP_TEST_SCRATCH + i, static_cast<u8>(i * 13 + 7));
		psxRegs.pc = IOP_TEST_PC;
		psxRegs.cycle = 0;
		psxRegs.iopNextEventCycle = 0x7fffffffu;
		const s32 budget = static_cast<s32>(program.size() + 16) * 8;
		if (jit)
		{
			OpcodeFamilies::SetFamilyMask(OpcodeFamilies::CORE_IOP,
				force_gte_interpreter ? (1ull << OpcodeFamilies::IOP::FAM_GTE) : 0);
			psxCpu = &psxRec;
			psxRec.Reset();
		}
		else
		{
			OpcodeFamilies::SetFamilyMask(OpcodeFamilies::CORE_IOP, 0);
			psxCpu = &psxInt;
			psxInt.Reset();
		}
		psxCpu->ExecuteBlock(budget);
		OpcodeFamilies::SetFamilyMask(OpcodeFamilies::CORE_IOP, 0);

		IOPAvszRunResult result;
		CaptureIOP(result.state);
		if (result.state.pc >= sentinel && result.state.pc < sentinel + 8)
			result.state.pc = sentinel;
		result.cycle_budget = psxRegs.iopCycleEE;
		result.cycle_carry = psxRegs.iopCycleEECarry;
		result.next_event = psxRegs.iopNextEventCycle;
		return result;
	}

	static u32 IOPAvszExpectedMac0Product(const IOPAvszInputs& input, bool avsz4)
	{
		const u32 first = avsz4 ? 0u : 1u;
		const u32 raw_scale = (avsz4 ? input.zsf4 : input.zsf3) & 0xffffu;
		const u32 scale_bits = raw_scale < 0x8000u ? raw_scale : raw_scale - 0x10000u;
		u32 sum = 0;
		for (u32 i = first; i < 4; ++i)
			sum += input.sz[i] & 0xffffu;
		// The target keeps the wrapped 32-bit product before the arithmetic shift.
		return static_cast<u32>(static_cast<u64>(scale_bits) * static_cast<u64>(sum));
	}

	static IOPAvszOutput IOPAvszExpectedOutput(const IOPAvszInputs& input, bool avsz4)
	{
		const u32 wrapped_product = IOPAvszExpectedMac0Product(input, avsz4);
		const s64 signed_product = (wrapped_product & 0x80000000u) ?
			static_cast<s64>(wrapped_product) - 0x100000000ll : static_cast<s64>(wrapped_product);
		// AVSZ writes the signed, arithmetic-shifted product to MAC0. Keep the
		// product wrap before shifting, matching the target's 32-bit datapath.
		const s64 shifted = signed_product >= 0 ? signed_product / 4096 :
			-((-signed_product + 4095) / 4096);
		return {static_cast<u32>(shifted),
			static_cast<u32>(std::clamp<s64>(shifted, 0, 0xffff)),
			(shifted < 0 || shifted > 0xffff) ? 0x80040000u : 0};
	}

	static void IOPAvszSeedGuestState(const IOPAvszInputs& input, bool initialize_scratch)
	{
		std::memset(&psxRegs, 0, sizeof(psxRegs));
		for (u32 i = 0; i < 32; ++i)
		{
			psxRegs.CP2D.r[i] = 0x00112233u * (i + 1);
			psxRegs.CP2C.r[i] = 0x44556677u ^ (i * 0x01020304u);
		}
		for (u32 i = 0; i < 4; ++i)
			psxRegs.CP2D.r[16 + i] = input.sz[i];
		psxRegs.CP2C.r[29] = input.zsf3;
		psxRegs.CP2C.r[30] = input.zsf4;
		if (initialize_scratch)
			for (u32 i = 0; i < IOP_SCRATCH_SIZE; ++i)
				iopMemWrite8(IOP_TEST_SCRATCH + i, static_cast<u8>(i * 13 + 7));
		psxRegs.pc = IOP_TEST_PC;
		psxRegs.cycle = 0;
		psxRegs.iopNextEventCycle = 0x7fffffffu;
	}

	static IOPSnapshot IOPAvszExpectedState(const IOPAvszInputs& input, bool avsz4,
		const std::vector<u32>& program)
	{
		IOPSnapshot expected{};
		for (u32 i = 0; i < 32; ++i)
		{
			expected.cp2d[i] = 0x00112233u * (i + 1);
			expected.cp2c[i] = 0x44556677u ^ (i * 0x01020304u);
		}
		for (u32 i = 0; i < 4; ++i)
			expected.cp2d[16 + i] = input.sz[i];
		expected.cp2c[29] = input.zsf3;
		expected.cp2c[30] = input.zsf4;
		for (u32 i = 0; i < IOP_SCRATCH_SIZE; ++i)
			expected.scratch[i] = static_cast<u8>(i * 13 + 7);
		expected.pc = IOP_TEST_PC + static_cast<u32>(program.size()) * 4;

		const IOPAvszOutput output = IOPAvszExpectedOutput(input, avsz4);
		expected.cp2d[24] = output.mac0;
		expected.cp2d[7] = (expected.cp2d[7] & 0xffff0000u) | output.otz;
		expected.cp2c[31] = output.flag;
		expected.gpr[18] = output.mac0;
		expected.gpr[19] = expected.cp2d[7];
		expected.gpr[20] = output.flag;
		return expected;
	}

	static IOPAvszRunResult IOPAvszReplayResident(bool jit, const IOPAvszInputs& input,
		const std::vector<u32>& program, s32 budget)
	{
		// Restore guest state only. The code and selected JIT cache stay resident,
		// and no guest memory writes occur in this timed replay path.
		IOPAvszSeedGuestState(input, false);
		OpcodeFamilies::SetFamilyMask(OpcodeFamilies::CORE_IOP, 0);
		psxCpu = jit ? &psxRec : &psxInt;
		psxCpu->ExecuteBlock(budget);
		IOPAvszRunResult result;
		CaptureIOP(result.state);
		const u32 sentinel = IOP_TEST_PC + static_cast<u32>(program.size()) * 4;
		if (result.state.pc >= sentinel && result.state.pc < sentinel + 8)
			result.state.pc = sentinel;
		result.cycle_budget = psxRegs.iopCycleEE;
		result.cycle_carry = psxRegs.iopCycleEECarry;
		result.next_event = psxRegs.iopNextEventCycle;
		return result;
	}

	static void IOPAvszReadCounts(u64& avsz3, u64& avsz4)
	{
		avsz3 = avsz4 = 0;
		EmuCoreXOracleGetIopIrGteAvszCounts(&avsz3, &avsz4);
	}

	static void IOPAvszCheckFrontend()
	{
		for (bool avsz4 : {false, true})
		{
			const u32 word = IOPAvszWord(avsz4);
			ir::Function fn;
			std::string error;
			u32 accepted = 0;
			const bool lifted = IopIr::LiftSequence(&word, IOP_TEST_PC, 1, fn, &accepted, &error) && accepted == 1;
			Check(lifted && ir::Verify(fn, &error) &&
				Arm64Ir::CanLower(fn, false, &error, Arm64Ir::GuestState::IOP),
				avsz4 ? "canonical IOP AVSZ4 lifts and admits common lowering" :
				"canonical IOP AVSZ3 lifts and admits common lowering");
		}
		for (u32 rs : {0u, 15u, 17u, 31u})
			for (bool avsz4 : {false, true})
			{
				const u32 word = (0x12u << 26) | (rs << 21) | (avsz4 ? 0x2eu : 0x2du);
				ir::Function fn;
				std::string error;
				u32 accepted = 99;
				Check(!IopIr::LiftSequence(&word, IOP_TEST_PC, 1, fn, &accepted, &error) &&
					accepted == 0 && fn.blocks.empty(),
					"IOP AVSZ rejects noncanonical GTE command Rs");
			}
		for (u32 funct : {0u, 0x2cu, 0x2fu, 0x3fu})
		{
			const u32 word = (0x12u << 26) | (16u << 21) | funct;
			ir::Function fn;
			std::string error;
			u32 accepted = 99;
			Check(!IopIr::LiftSequence(&word, IOP_TEST_PC, 1, fn, &accepted, &error) &&
				accepted == 0 && fn.blocks.empty(),
				"IOP AVSZ rejects adjacent or non-GTE command funct values");
		}
	}
}

void IOPSharedAvszTests()
{
	IOPAvszCheckFrontend();
	std::vector<IOPAvszInputs> inputs = {
		{{0xaaaa0000u, 0xbbbb0000u, 0xcccc0000u, 0xdddd0000u}, 0x00001000u, 0x00001000u},
		{{0x12340001u, 0x56780002u, 0x9abc0003u, 0xdef00004u}, 0x00001000u, 0x00001000u},
		{{0x0001ffffu, 0x0002ffffu, 0x0003ffffu, 0x0004ffffu}, 0x00000001u, 0x00000001u},
		{{0x1010ffffu, 0x2020ffffu, 0x3030ffffu, 0x4040ffffu}, 0x00007fffu, 0x00007fffu},
		{{0x1111ffffu, 0x2222ffffu, 0x3333ffffu, 0x4444ffffu}, 0x00008000u, 0x00008000u},
		{{0xabcd0001u, 0xbcde0002u, 0xcdef0003u, 0xdef00004u}, 0xfffff000u, 0xfffff000u},
		{{0x1357ffffu, 0x2468ffffu, 0x369affffu, 0x48abffffu}, 0x00007000u, 0x00007000u},
		{{0xfedc0010u, 0xdcba0020u, 0xba980030u, 0x98760040u}, 0x00004000u, 0x00002000u},
		{{0xaaaa8000u, 0xbbbb8000u, 0xcccc8000u, 0xdddd8000u}, 0x00007fffu, 0xffff8000u},
		{{0x00010000u, 0x00020001u, 0x00030002u, 0x00040003u}, 0x00000000u, 0x00001000u},
		{{0xfedc0001u, 0xdcba7fffu, 0xba988000u, 0x9876ffffu}, 0xffffffffu, 0x00010000u}
	};
	u32 random = 0x6d2b79f5u;
	auto next_random = [&]() {
		random ^= random << 13;
		random ^= random >> 17;
		random ^= random << 5;
		return random;
	};
	for (u32 i = 0; i < 8; ++i)
	{
		IOPAvszInputs value{};
		for (u32 lane = 0; lane < value.sz.size(); ++lane)
			value.sz[lane] = next_random();
		value.zsf3 = next_random();
		value.zsf4 = next_random();
		inputs.push_back(value);
	}
	const u32 boundary_start = static_cast<u32>(inputs.size());
	inputs.push_back({{0xaaaa0000u, 0xbbbb0001u, 0xcccc0001u, 0xdddd0002u},
		0x0000fc00u, 0x0000fc00u}); // shifted MAC0 = -1 for AVSZ3 and AVSZ4
	inputs.push_back({{0xaaaa0000u, 0xbbbb0001u, 0xcccc0001u, 0xdddd0002u},
		0, 0}); // shifted MAC0 = 0
	inputs.push_back({{0xaaaa0000u, 0xbbbbaaaaU, 0xccccaaaaU, 0xddddaaaaU},
		0x00000800u, 0x00000800u}); // shifted MAC0 = 65535
	inputs.push_back({{0xaaaa0000u, 0xbbbbaaabu, 0xccccaaaau, 0xddddaaabu},
		0x00000800u, 0x00000800u}); // shifted MAC0 = 65536

	for (u32 case_index = 0; case_index < inputs.size(); ++case_index)
	for (bool avsz4 : {false, true})
	{
		const IOPAvszInputs& input = inputs[case_index];
		if (case_index >= boundary_start)
		{
			constexpr std::array<s32, 4> expected_mac0 = {-1, 0, 65535, 65536};
			const u32 boundary = case_index - boundary_start;
			Check(IOPAvszExpectedOutput(input, avsz4).mac0 ==
				static_cast<u32>(expected_mac0[boundary]),
				"IOP AVSZ boundary fixture independently produces the requested shifted MAC0");
		}
		const std::vector<u32> program = {IOPAvszWord(avsz4),
			IOPAvszTransfer(0, 18, 24), IOPAvszTransfer(0, 19, 7), IOPAvszTransfer(2, 20, 31)};
		const IOPSnapshot expected = IOPAvszExpectedState(input, avsz4, program);
		u64 before3 = 0, before4 = 0;
		IOPAvszReadCounts(before3, before4);
		EmuCoreXOracleSetIOPIR(0);
		const IOPAvszRunResult interp = IOPAvszRun(false, false, input, program);
		const IOPAvszRunResult legacy = IOPAvszRun(true, false, input, program);
		u64 after3 = 0, after4 = 0;
		IOPAvszReadCounts(after3, after4);
		Check(after3 == before3 && after4 == before4,
			"IOP AVSZ interpreter and legacy leave successful common counters unchanged");
		CompareIOP(expected, interp.state, "IOP AVSZ interpreter matches independent wrapped-product and OTZ model");
		CompareIOP(expected, legacy.state, "IOP AVSZ legacy matches independent complete-state model");
		EmuCoreXOracleSetIOPIR(1);
		const IOPAvszRunResult shared = IOPAvszRun(true, false, input, program);
		IOPAvszReadCounts(after3, after4);
		Check(after3 == before3 + (avsz4 ? 0u : 1u) && after4 == before4 + (avsz4 ? 1u : 0u),
			"IOP AVSZ successful compilation increments exactly the selected operation counter");
		CompareIOP(expected, shared.state, "IOP AVSZ shared matches independent complete-state model");
		Check(shared.state.cycle == legacy.state.cycle && shared.cycle_budget == legacy.cycle_budget &&
			shared.cycle_carry == legacy.cycle_carry && shared.next_event == legacy.next_event,
			"IOP AVSZ shared route preserves exact legacy cycle and event accounting");
		const u64 forced_before3 = after3, forced_before4 = after4;
		const IOPAvszRunResult forced = IOPAvszRun(true, true, input, program);
		IOPAvszReadCounts(after3, after4);
		Check(after3 == forced_before3 && after4 == forced_before4,
			"forced GTE interpreter AVSZ never increments shared compile counters");
		CompareIOP(expected, forced.state, "IOP AVSZ forced GTE interpreter matches independent model");
		std::printf("IOPAVSZCASE op=%u case=%u mac0=%08x otz=%04x flag=%08x legacy_cycles=%u shared_cycles=%u\n",
			avsz4 ? 4u : 3u, case_index, expected.cp2d[24], expected.cp2d[7] & 0xffffu,
			expected.cp2c[31], legacy.state.cycle, shared.state.cycle);
	}

	const IOPAvszInputs& alias_input = inputs[boundary_start + 3];
	for (bool avsz4 : {false, true})
		for (u32 aliases : {0u, 1u << 19, 1u << 10, (1u << 19) | (1u << 10)})
		{
			const std::vector<u32> program = {IOPAvszWord(avsz4, aliases),
				IOPAvszTransfer(0, 18, 24), IOPAvszTransfer(0, 19, 7), IOPAvszTransfer(2, 20, 31)};
			const IOPSnapshot expected = IOPAvszExpectedState(alias_input, avsz4, program);
			u64 before3 = 0, before4 = 0, after3 = 0, after4 = 0;
			IOPAvszReadCounts(before3, before4);
			EmuCoreXOracleSetIOPIR(0);
			const auto interp = IOPAvszRun(false, false, alias_input, program);
			const auto legacy = IOPAvszRun(true, false, alias_input, program);
			IOPAvszReadCounts(after3, after4);
			Check(after3 == before3 && after4 == before4,
				"IOP AVSZ SF/LM aliases do not enter common IR in interpreter or legacy mode");
			CompareIOP(expected, interp.state, "IOP AVSZ SF/LM aliases retain independent state semantics");
			CompareIOP(expected, legacy.state, "IOP AVSZ SF/LM aliases match full legacy-state model");
			EmuCoreXOracleSetIOPIR(1);
			const auto shared = IOPAvszRun(true, false, alias_input, program);
			IOPAvszReadCounts(after3, after4);
			Check(after3 == before3 + (avsz4 ? 0u : 1u) &&
				after4 == before4 + (avsz4 ? 1u : 0u),
				"IOP AVSZ SF/LM alias compilation counts exactly one selected op");
			CompareIOP(expected, shared.state, "IOP AVSZ SF/LM aliases match full shared-state model");
			Check(shared.state.cycle == legacy.state.cycle &&
				shared.cycle_budget == legacy.cycle_budget && shared.cycle_carry == legacy.cycle_carry &&
				shared.next_event == legacy.next_event,
				"IOP AVSZ SF/LM aliases preserve exact legacy cycles and events");
		}

	struct BranchCase { u32 op; u32 rt; bool taken; };
	const std::array<BranchCase, 4> branch_cases = {{{4, 0, true}, {4, 1, false},
		{5, 1, true}, {5, 0, false}}};
	for (u32 i = 0; i < branch_cases.size(); ++i)
	{
		const BranchCase branch = branch_cases[i];
		const bool avsz4 = (i & 1) != 0;
		const std::vector<u32> program = {
			IOPAvszIWord(9, 0, 1, 1), // establish a nonzero comparison operand
			IOPAvszIWord(branch.op, 0, branch.rt, 2),
			IOPAvszWord(avsz4), // AVSZ executes in the branch delay slot
			IOPAvszIWord(9, 0, 10, 0x55), // executes only on the untaken path
			IOPAvszTransfer(0, 18, 24), IOPAvszTransfer(0, 19, 7),
			IOPAvszTransfer(2, 20, 31)};
		IOPSnapshot expected = IOPAvszExpectedState(alias_input, avsz4, {IOPAvszWord(avsz4)});
		expected.gpr[1] = 1;
		expected.gpr[10] = branch.taken ? 0u : 0x55u;
		expected.pc = IOP_TEST_PC + static_cast<u32>(program.size()) * 4;
		EmuCoreXOracleSetIOPIR(0);
		const auto interp = IOPAvszRun(false, false, alias_input, program);
		const auto legacy = IOPAvszRun(true, false, alias_input, program);
		CompareIOP(expected, interp.state, "IOP AVSZ branch-delay interpreter matches full model");
		CompareIOP(expected, legacy.state, "IOP AVSZ branch-delay legacy matches full model");
		u64 before3 = 0, before4 = 0, after3 = 0, after4 = 0;
		IOPAvszReadCounts(before3, before4);
		EmuCoreXOracleSetIOPIR(1);
		const auto shared = IOPAvszRun(true, false, alias_input, program);
		IOPAvszReadCounts(after3, after4);
		Check(after3 == before3 + (avsz4 ? 0u : 1u) &&
			after4 == before4 + (avsz4 ? 1u : 0u),
			"IOP AVSZ branch-delay compiles exactly its selected operation");
		CompareIOP(expected, shared.state, "IOP AVSZ branch-delay shared matches full model");
		Check(shared.state.cycle == legacy.state.cycle &&
			shared.cycle_budget == legacy.cycle_budget && shared.cycle_carry == legacy.cycle_carry &&
			shared.next_event == legacy.next_event,
			"IOP AVSZ branch-delay preserves exact legacy cycles and events");
	}
	EmuCoreXOracleSetIOPIR(-1);
}

void IOPSharedAvszBenchmarks()
{
	constexpr u32 repeats = 16;
	constexpr u32 samples = 7;
	std::vector<u32> program = {
		IOPAvszIWord(15, 0, 8, 0x1234), // LUI r8, 0x1234
		IOPAvszIWord(13, 8, 8, 0x5678)  // ORI r8, r8, 0x5678
	};
	for (u32 i = 0; i < repeats; ++i)
	{
		program.push_back(IOPAvszIWord(9, 9, 9, 3)); // ADDIU r9, r9, 3
		program.push_back(IOPAvszWord(false));
		program.push_back(IOPAvszIWord(9, 9, 10, 5)); // ADDIU r10, r9, 5
		program.push_back(IOPAvszWord(true));
		program.push_back(IOPAvszTransfer(0, 18, 24)); // MFC2 MAC0
		program.push_back(IOPAvszTransfer(0, 19, 7));  // MFC2 OTZ word
		program.push_back(IOPAvszTransfer(2, 20, 31)); // CFC2 FLAG
		program.push_back(IOPAvszIWord(9, 11, 11, 1)); // ADDIU r11, r11, 1
	}
	const s32 budget = static_cast<s32>((program.size() + 16) * 8);
	const u32 sentinel = IOP_TEST_PC + static_cast<u32>(program.size()) * 4;
	const IOPAvszInputs input{{0xaaaa0001u, 0xbbbb7fffu, 0xcccc8000u, 0xddddffffu},
		0x00007000u, 0xffff9000u};

	IOPSnapshot expected{};
	for (u32 i = 0; i < 32; ++i)
	{
		expected.cp2d[i] = 0x00112233u * (i + 1);
		expected.cp2c[i] = 0x44556677u ^ (i * 0x01020304u);
	}
	for (u32 i = 0; i < 4; ++i)
		expected.cp2d[16 + i] = input.sz[i];
	expected.cp2c[29] = input.zsf3;
	expected.cp2c[30] = input.zsf4;
	for (u32 i = 0; i < IOP_SCRATCH_SIZE; ++i)
		expected.scratch[i] = static_cast<u8>(i * 13 + 7);
	expected.gpr[8] = 0x12345678u;
	for (u32 i = 0; i < repeats; ++i)
	{
		expected.gpr[9] += 3;
		IOPAvszOutput output = IOPAvszExpectedOutput(input, false);
		expected.cp2d[24] = output.mac0;
		expected.cp2d[7] = (expected.cp2d[7] & 0xffff0000u) | output.otz;
		expected.cp2c[31] = output.flag;
		expected.gpr[10] = expected.gpr[9] + 5;
		output = IOPAvszExpectedOutput(input, true);
		expected.cp2d[24] = output.mac0;
		expected.cp2d[7] = (expected.cp2d[7] & 0xffff0000u) | output.otz;
		expected.cp2c[31] = output.flag;
		expected.gpr[18] = output.mac0;
		expected.gpr[19] = expected.cp2d[7];
		expected.gpr[20] = output.flag;
		++expected.gpr[11];
	}
	expected.pc = sentinel;

	std::array<double, samples> cold_us[2]{};
	std::array<double, samples> hot_us[2]{};
	std::array<u64, 2> native_bytes{};
	std::array<IOPAvszRunResult, 2> cold_result{};
	constexpr u32 hot_replays_per_sample = 256;
	for (u32 mode = 0; mode < 2; ++mode)
	{
		EmuCoreXOracleSetIOPIR(static_cast<int>(mode));
		for (u32 sample = 0; sample < samples; ++sample)
		{
			u64 before3 = 0, before4 = 0;
			IOPAvszReadCounts(before3, before4);
			const u64 fusions_before = EmuCoreXOracleIopIrAvszFusions();
			const auto start = std::chrono::steady_clock::now();
			cold_result[mode] = IOPAvszRun(true, false, input, program);
			cold_us[mode][sample] = std::chrono::duration<double, std::micro>(
				std::chrono::steady_clock::now() - start).count();
			u64 after3 = 0, after4 = 0;
			IOPAvszReadCounts(after3, after4);
			Check(after3 == before3 + (mode ? repeats : 0) &&
				after4 == before4 + (mode ? repeats : 0),
				"AVSZ mixed benchmark cold compile counts match both 16-op routes");
			Check(EmuCoreXOracleIopIrAvszFusions() == fusions_before + (mode ? repeats * 2 : 0),
				"AVSZ benchmark fuses exactly the shared source commands");
			CompareIOP(expected, cold_result[mode].state,
				"AVSZ mixed benchmark cold full state matches independent model");
			Check(cold_result[mode].cycle_budget == cold_result[0].cycle_budget &&
				cold_result[mode].cycle_carry == cold_result[0].cycle_carry &&
				cold_result[mode].next_event == cold_result[0].next_event,
				"AVSZ mixed benchmark cold preserves legacy cycle and event accounting");

			std::vector<JitBlockProfile> profiles;
			IOP_JitGetBlockProfiles(profiles);
			u64 bytes = 0;
			bool bounded = true;
			for (const JitBlockProfile& profile : profiles)
				if (profile.startpc >= IOP_TEST_PC && profile.startpc <= sentinel)
				{
					bytes += profile.host_size;
					bounded &= profile.host_size != 0 && profile.host_size < 0x10000;
				}
			Check(bounded && bytes != 0,
				"AVSZ mixed benchmark observes bounded compiled IOP blocks");
			native_bytes[mode] = bytes;
		}

		u64 before3 = 0, before4 = 0;
		IOPAvszReadCounts(before3, before4);
		const u64 common_before = EmuCoreXOracleIOPIRInstructions();
		IOPAvszRunResult last{};
		bool replay_state_matches = true;
		bool replay_cycles_match = true;
		for (u32 sample = 0; sample < samples; ++sample)
		{
			const auto start = std::chrono::steady_clock::now();
			for (u32 replay = 0; replay < hot_replays_per_sample; ++replay)
			{
				last = IOPAvszReplayResident(true, input, program, budget);
				const IOPSnapshot& actual = last.state;
				replay_state_matches &= std::memcmp(expected.gpr, actual.gpr, sizeof(expected.gpr)) == 0 &&
					std::memcmp(expected.cp2d, actual.cp2d, sizeof(expected.cp2d)) == 0 &&
					std::memcmp(expected.cp2c, actual.cp2c, sizeof(expected.cp2c)) == 0 &&
					std::memcmp(expected.scratch, actual.scratch, sizeof(expected.scratch)) == 0 &&
					std::memcmp(&expected.hi, &actual.hi, sizeof(expected.hi)) == 0 &&
					std::memcmp(&expected.lo, &actual.lo, sizeof(expected.lo)) == 0 &&
					actual.pc == expected.pc;
				for (u32 cp0 = 0; cp0 < 32; ++cp0)
					if (cp0 != 1 && cp0 != 9 && cp0 != 11)
						replay_state_matches &= expected.cp0[cp0] == actual.cp0[cp0];
				replay_cycles_match &= actual.cycle == cold_result[mode].state.cycle &&
					last.cycle_budget == cold_result[mode].cycle_budget &&
					last.cycle_carry == cold_result[mode].cycle_carry &&
					last.next_event == cold_result[mode].next_event;
			}
			hot_us[mode][sample] = std::chrono::duration<double, std::micro>(
				std::chrono::steady_clock::now() - start).count() * 1000.0 /
				hot_replays_per_sample;
		}
		Check(replay_state_matches,
			"AVSZ mixed benchmark every resident replay matches independent full-state model");
		Check(replay_cycles_match,
			"AVSZ mixed benchmark every resident replay preserves exact cycles and event state");
		CompareIOP(expected, last.state,
			"AVSZ mixed benchmark resident replay detailed state validation");
		u64 after3 = 0, after4 = 0;
		IOPAvszReadCounts(after3, after4);
		Check(after3 == before3 && after4 == before4 &&
			EmuCoreXOracleIOPIRInstructions() == common_before,
			"AVSZ mixed benchmark resident replay performs no recompilation");
		std::sort(cold_us[mode].begin(), cold_us[mode].end());
		std::sort(hot_us[mode].begin(), hot_us[mode].end());
		std::printf("IOPAVSZBENCH mode=%s avsz3=%u avsz4=%u scalar_integer_ops=%u cold_reset_setup_compile_execute_median_us=%.3f hot_state_reset_execute_snapshot_validate_median_ns_per_replay=%.3f native_block_bytes=%llu samples=%u hot_replays_per_sample=%u\n",
			mode ? "shared" : "legacy", repeats, repeats, repeats * 3 + 2,
			cold_us[mode][samples / 2], hot_us[mode][samples / 2],
			static_cast<unsigned long long>(native_bytes[mode]), samples, hot_replays_per_sample);
	}
	Check(native_bytes[1] <= native_bytes[0], "AVSZ shared native code does not exceed legacy byte budget");
	Check(cold_result[0].state.cycle == cold_result[1].state.cycle &&
		cold_result[0].cycle_budget == cold_result[1].cycle_budget &&
		cold_result[0].cycle_carry == cold_result[1].cycle_carry &&
		cold_result[0].next_event == cold_result[1].next_event,
		"AVSZ mixed benchmark legacy/shared exact cycle and event parity");
	EmuCoreXOracleSetIOPIR(-1);
}
