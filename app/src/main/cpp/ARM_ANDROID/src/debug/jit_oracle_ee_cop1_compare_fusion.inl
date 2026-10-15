// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// COP1 C.EQ.S/C.LT.S/C.LE.S strict-window planner and lowering tests.
// Include after jit_oracle_ee_cop1_compare.inl and the EE oracle helpers.

extern "C" u64 EmuCoreXOracleEeIrCompareFusions();

namespace
{
	static u32 EeCop1CompareFusionStart(const ir::Function& fn, u32 guest_pc)
	{
		const ir::Block* block = fn.GetBlock(1);
		if (!block)
			return 0xffffffffu;
		for (u32 i = 0; i < block->insts.size(); ++i)
			if (block->insts[i].guest_pc == guest_pc && block->insts[i].op == ir::Op::ReadFprBits)
				return i >= 7 ? i - 7 : 0xffffffffu;
		return 0xffffffffu;
	}

	static bool EeCop1CompareFusionPlan(const ir::Function& fn, u32 first_inst,
		Arm64Ir::EeCop1ComparePlan* plan = nullptr)
	{
		std::string error;
		Arm64Ir::EeCop1ComparePlan local{};
		return Arm64Ir::PlanEeCop1CompareWindow(fn, 0, first_inst, plan ? plan : &local, &error);
	}

	static bool EeCop1CompareFusionRejectMutation(const ir::Function& source, u32 first_inst,
		ir::Op match_op, u32 occurrence, void (*mutate)(ir::Function&, u32), const char* label)
	{
		ir::Function fn = source;
		ir::Block* block = fn.GetBlock(1);
		if (!block)
		{
			Check(false, label);
			return false;
		}
		u32 seen = 0, selected = 0xffffffffu;
		for (u32 i = first_inst; i < block->insts.size(); ++i)
			if (block->insts[i].op == match_op && seen++ == occurrence)
			{
				selected = i;
				break;
			}
		if (selected == 0xffffffffu)
		{
			Check(false, label);
			return false;
		}
		mutate(fn, selected);
		Arm64Ir::EeCop1ComparePlan plan{0xffffffffu, 0xffu, 0xffu, ir::Op::Add};
		std::string error;
		const bool rejected = !Arm64Ir::PlanEeCop1CompareWindow(fn, 0, first_inst, &plan, &error);
		const bool cleared = plan.inst_count == 0 && plan.lhs_fpr == 0 && plan.rhs_fpr == 0 &&
			plan.predicate == ir::Op::Nop;
		Check(rejected && cleared, label);
		return rejected && cleared;
	}

	static bool EeCop1CompareFusionRun(ir::Function fn, bool optimize,
		const std::vector<u32>& code, const GPR_reg* initial_gpr, const u32* initial_cp0,
		const u32* initial_fpr, const u32* initial_fcr, const EESnapshot& expected,
		u32 expected_fusions, bool force_spills, const char* label)
	{
		EeIr::LowerOptions options;
		options.optimize_ir = optimize;
		options.allocate_registers = true;
		options.capture_exit_pc = true;
		EeIr::LowerOutput out{};
		std::string error;
		u8* host_code = SysMemory::GetEERec() + 0x2000000;
		if (!EeIr::LowerBlock(fn, options, host_code, 0x10000, &out, &error) || !out.entry)
		{
			std::printf("COP1COMPAREFUSION LOWERFAIL %s: %s\n", label, error.c_str());
			Check(false, label);
			return false;
		}
		bool same = out.fused_ee_cop1_compares == expected_fusions;
		if (force_spills)
			same &= out.spill_values > 0;

		std::memset(&cpuRegs, 0, sizeof(cpuRegs));
		std::memset(&fpuRegs, 0, sizeof(fpuRegs));
		std::memset(&VU0.VF, 0, sizeof(VU0.VF));
		std::memset(&VU0.VI, 0, sizeof(VU0.VI));
		std::memset(&VU0.ACC, 0, sizeof(VU0.ACC));
		VU0.q.UL = VU0.p.UL = 0;
		VU0.macflag = VU0.statusflag = VU0.clipflag = 0;
		if (initial_gpr)
			std::memcpy(cpuRegs.GPR.r, initial_gpr, sizeof(cpuRegs.GPR.r));
		if (initial_cp0)
			std::memcpy(cpuRegs.CP0.r, initial_cp0, sizeof(cpuRegs.CP0.r));
		cpuRegs.CP0.n.Status.val |= 0x20000000u;
		if (initial_fpr)
			for (u32 reg = 0; reg < 32; ++reg)
				fpuRegs.fpr[reg].UL = initial_fpr[reg];
		if (initial_fcr)
			std::memcpy(fpuRegs.fprc, initial_fcr, sizeof(fpuRegs.fprc));
		for (u32 i = 0; i < EE_SCRATCH_SIZE; ++i)
			memWrite8(EE_TEST_SCRATCH + i, static_cast<u8>(i * 13 + 7));
		cpuRegs.pc = EE_TEST_PC;
		cpuRegs.cycle = 0;
		cpuRegs.branch = 0;
		cpuRegs.nextEventCycle = 0x7fffffffu;
		EEsCycle = EEoCycle = 0;
		for (u32 i = 0; i < code.size(); ++i)
			memWrite32(EE_TEST_PC + i * 4, code[i]);
		g_eeir_exit_pc = 0;
		g_eeir_exit_valid = 0;
		reinterpret_cast<void (*)()>(out.entry)();
		EESnapshot actual{};
		CaptureEE(actual);
		same &= g_eeir_exit_valid != 0 && g_eeir_exit_pc == expected.pc;
		actual.pc = g_eeir_exit_pc;
		CompareEE(expected, actual, label);
		Check(same, label);
		return same;
	}

	static bool EeCop1CompareFusionWithLiveGprs(const ir::Function& source,
		ir::Function& output, u32 guest_pc)
	{
		const ir::Block* source_block = source.GetBlock(1);
		if (!source_block || source_block->insts.empty() || source_block->insts.back().op != ir::Op::Resume)
			return false;
		output = ir::Function();
		output.guest_entry = guest_pc;
		ir::Builder b(output);
		b.CreateBlock(guest_pc);
		std::array<u32, 4> live{};
		for (u32 i = 0; i < live.size(); ++i)
		{
			b.SetGuestPc(guest_pc);
			live[i] = b.Emit(ir::Op::ReadGpr, ir::Type::I64, {}, 21 + i);
		}

		std::vector<u32> remap(source.value_types.size(), 0);
		bool valid = true;
		for (const ir::Inst& inst : source_block->insts)
		{
			if (inst.op == ir::Op::Resume)
				break;
			u32 args[4] = {};
			for (u32 a = 0; a < inst.num_args; ++a)
			{
				if (inst.args[a] == 0 || inst.args[a] >= remap.size() || remap[inst.args[a]] == 0)
				{
					valid = false;
					break;
				}
				args[a] = remap[inst.args[a]];
			}
			if (!valid)
				break;
			b.SetGuestPc(inst.guest_pc);
			b.SetDelaySlot((inst.aux & ir::IF_DELAY_SLOT) != 0);
			u32 value = 0;
			switch (inst.num_args)
			{
				case 0: value = b.Emit(inst.op, inst.type, {}, inst.imm, inst.aux, inst.guest_pc); break;
				case 1: value = b.Emit(inst.op, inst.type, {args[0]}, inst.imm, inst.aux, inst.guest_pc); break;
				case 2: value = b.Emit(inst.op, inst.type, {args[0], args[1]}, inst.imm, inst.aux, inst.guest_pc); break;
				case 3: value = b.Emit(inst.op, inst.type, {args[0], args[1], args[2]}, inst.imm, inst.aux, inst.guest_pc); break;
				case 4: value = b.Emit(inst.op, inst.type, {args[0], args[1], args[2], args[3]}, inst.imm, inst.aux, inst.guest_pc); break;
				default: valid = false; break;
			}
			if (!valid)
				break;
			if (inst.value)
			{
				if (value == 0 || inst.value >= remap.size())
				{
					valid = false;
					break;
				}
				remap[inst.value] = value;
			}
		}
		b.SetDelaySlot(false);
		for (u32 i = 0; valid && i < live.size(); ++i)
		{
			b.SetGuestPc(guest_pc + 4);
			b.Emit1(ir::Op::WriteGpr, ir::Type::Void, live[i], 25 + i, ir::IF_WIDE_WRITE);
		}
		b.SetBlockEnd(guest_pc + 4);
		if (valid)
			b.Resume(guest_pc + 4);
		return valid && ir::Verify(output, nullptr);
	}
}

void EECop1CompareFusionTests()
{
	constexpr std::array<u32, 3> functs = {0x32u, 0x34u, 0x36u};
	constexpr std::array<ir::Op, 3> predicates = {ir::Op::CmpEq, ir::Op::CmpLtU, ir::Op::CmpLeU};
	constexpr std::array<std::array<u32, 2>, 8> raw_pairs = {{
		{{0x00000000u, 0x80000000u}}, {{0x00000001u, 0x80000001u}},
		{{0x007fffffu, 0x00800000u}}, {{0x807fffffu, 0x80800000u}},
		{{0x3f800000u, 0xbf800000u}}, {{0xc0400000u, 0xc0000000u}},
		{{0x7f800000u, 0x7f7fffffu}}, {{0x7fc12345u, 0x7f812345u}},
	}};
	std::array<GPR_reg, 32> initial_gpr{};
	std::array<u32, 32> initial_fpr{}, initial_fcr{}, initial_cp0{};
	initial_cp0[12] = 0x20000000u;
	for (u32 reg = 1; reg < 32; ++reg)
	{
		initial_gpr[reg].UD[0] = 0x1234567800000000ull | (0x76543210u + reg);
		initial_gpr[reg].UD[1] = 0xabcdef0100000000ull | (0x89abcdefu + reg);
		initial_fpr[reg] = 0x51000000u ^ (reg * 0x01020408u);
		initial_fcr[reg] = 0x87654321u ^ (reg * 0x01020304u);
	}
	initial_gpr[0] = {};
	initial_fcr[0] = 0x2e00u;
	for (u32 i = 0; i < 4; ++i)
	{
		initial_gpr[21 + i].UD[0] = 0x0123456789abcdefull ^ (0x0101010101010101ull * i);
		initial_gpr[21 + i].UD[1] = 0xfedcba9876543210ull ^ (0x1010101010101010ull * i);
	}

	for (u32 case_index = 0; case_index < functs.size(); ++case_index)
	{
		const u32 funct = functs[case_index];
		const u32 fs = 5, ft = 9;
		const u32 word = MipsCop1(0x10, ft, fs, 0, funct);
		ir::Function exact;
		u32 end_pc = 0;
		std::string error;
		const bool lifted = EeIr::LiftBlock(&word, {EE_TEST_PC, 1}, exact, &end_pc, &error) &&
			ir::Verify(exact, &error);
		Check(lifted, "COP1 compare fusion source graph lifts and verifies");
		if (!lifted)
			continue;
		const ir::Block* block = exact.GetBlock(1);
		const u32 first_inst = EeCop1CompareFusionStart(exact, EE_TEST_PC);
		Arm64Ir::EeCop1ComparePlan plan{};
		const bool exact_plan = exact.blocks.size() == 1 && block && first_inst != 0xffffffffu &&
			EeCop1CompareFusionPlan(exact, first_inst, &plan);
		Check(exact_plan && block->insts.size() == 36 && plan.inst_count == 35 &&
			plan.lhs_fpr == fs && plan.rhs_fpr == ft && plan.predicate == predicates[case_index],
			"COP1 compare planner accepts the exact 35-node canonical predicate window");
		if (!exact_plan)
			continue;
		Check(!EeCop1CompareFusionPlan(exact, first_inst + 1),
			"COP1 compare planner rejects a shifted first instruction");
		Arm64Ir::EeCop1ComparePlan invalid_plan{0xffffffffu, 0xffu, 0xffu, ir::Op::Add};
		const bool bad_block = !Arm64Ir::PlanEeCop1CompareWindow(exact, 1, first_inst, &invalid_plan, &error);
		Check(bad_block && invalid_plan.inst_count == 0 && invalid_plan.lhs_fpr == 0 &&
			invalid_plan.rhs_fpr == 0 && invalid_plan.predicate == ir::Op::Nop,
			"COP1 compare planner rejects an invalid block and clears its result");

		ir::Function optimized = exact;
		ir::OptimizeIntegerValues(optimized);
		const u32 optimized_start = EeCop1CompareFusionStart(optimized, EE_TEST_PC);
		Arm64Ir::EeCop1ComparePlan optimized_plan{};
		Check(optimized_start != 0xffffffffu &&
			EeCop1CompareFusionPlan(optimized, optimized_start, &optimized_plan) &&
			optimized_plan.inst_count == 35,
			"COP1 compare planner accepts the exact predicate after integer optimization");

		const auto wrong_type = [](ir::Function& fn, u32 i) {
			fn.blocks[0].insts[i].type = ir::Type::I64;
			fn.value_types[fn.blocks[0].insts[i].value] = ir::Type::I64;
		};
		const auto wrong_link = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].args[0] = 0xffffffffu; };
		const auto wrong_pc = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].guest_pc += 4; };
		const auto wrong_aux = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].aux |= ir::IF_EXACT; };
		const auto duplicate_definition = [](ir::Function& fn, u32 i) {
			fn.blocks[0].insts[i].value = fn.blocks[0].insts[i + 1].value;
		};
		const auto out_of_range_fpr = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].imm = 32; };
		const auto wrong_predicate = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].op = ir::Op::CmpNe; };
		const auto changed_constant = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].imm ^= 1; };
		EeCop1CompareFusionRejectMutation(exact, first_inst, ir::Op::ReadFprBits, 0,
			out_of_range_fpr, "COP1 compare planner rejects an out-of-range FPR source");
		EeCop1CompareFusionRejectMutation(exact, first_inst, predicates[case_index], 0,
			wrong_predicate, "COP1 compare planner rejects a substituted ordered predicate opcode");
		EeCop1CompareFusionRejectMutation(exact, first_inst, ir::Op::ReadFprBits, 0,
			wrong_type, "COP1 compare planner rejects a mistyped raw FPR read");
		EeCop1CompareFusionRejectMutation(exact, first_inst, predicates[case_index], 0,
			wrong_link, "COP1 compare planner rejects a broken SSA operand link");
		EeCop1CompareFusionRejectMutation(exact, first_inst, ir::Op::ReadFprBits, 0,
			wrong_pc, "COP1 compare planner rejects mismatched guest-PC metadata");
		EeCop1CompareFusionRejectMutation(exact, first_inst, ir::Op::ReadFprBits, 0,
			wrong_aux, "COP1 compare planner rejects unsupported auxiliary metadata");
		EeCop1CompareFusionRejectMutation(exact, first_inst, ir::Op::ReadFprBits, 0,
			duplicate_definition, "COP1 compare planner rejects duplicate SSA definitions");
		if (block)
		{
			u32 constant_index = 0xffffffffu;
			for (u32 i = first_inst; i < block->insts.size(); ++i)
				if (block->insts[i].op == ir::Op::ConstI32)
				{
					constant_index = i;
					break;
				}
			if (constant_index != 0xffffffffu)
			{
				ir::Function changed = exact;
				changed.blocks[0].insts[constant_index].imm ^= 1;
				Arm64Ir::EeCop1ComparePlan rejected{0xffffffffu, 0xffu, 0xffu, ir::Op::Add};
				const bool invalid = !Arm64Ir::PlanEeCop1CompareWindow(changed, 0, first_inst, &rejected, &error);
				Check(invalid && rejected.inst_count == 0 && rejected.predicate == ir::Op::Nop,
					"COP1 compare planner rejects a changed normalization constant and clears output");
			}
			else
				Check(false, "COP1 compare canonical window contains a normalization constant to mutate");
		}

		// An intermediate predicate value escaping to a state write must prevent
		// fusion, even when the mutated whole function remains verifier-valid.
		{
			ir::Function escaped = exact;
			ir::Block& escaped_block = escaped.blocks[0];
			u32 escaped_value = 0;
			for (u32 i = first_inst; i < escaped_block.insts.size(); ++i)
				if (escaped_block.insts[i].op == predicates[case_index])
				{
					escaped_value = escaped_block.insts[i].value;
					break;
				}
			ir::Inst use{};
			use.op = ir::Op::WriteGpr;
			use.type = ir::Type::Void;
			use.args[0] = escaped_value;
			use.num_args = 1;
			use.imm = 20;
			use.guest_pc = EE_TEST_PC + 4;
			escaped_block.insts.insert(escaped_block.insts.end() - 1, use);
			Arm64Ir::EeCop1ComparePlan rejected{};
			const bool valid = escaped_value != 0 && ir::Verify(escaped, &error);
			const bool not_fusable = valid && !EeCop1CompareFusionPlan(escaped, first_inst, &rejected);
			Check(not_fusable && rejected.inst_count == 0,
				"COP1 compare planner rejects a whole-function intermediate SSA escape");
		}

		// C.F is a four-node state clear, not a compare-predicate window.
		if (case_index == 0)
		{
			const u32 clear_word = MipsCop1(0x10, 0, 0, 0, 0x30);
			ir::Function clear_fn;
			u32 clear_end = 0;
			const bool clear_lifted = EeIr::LiftBlock(&clear_word, {EE_TEST_PC, 1}, clear_fn,
				&clear_end, &error) && ir::Verify(clear_fn, &error);
			EeIr::LowerOutput clear_out{};
			EeIr::LowerOptions clear_options;
			clear_options.optimize_ir = true;
			const bool clear_lowered = clear_lifted && EeIr::LowerBlock(clear_fn, clear_options,
				SysMemory::GetEERec() + 0x2000000, 0x10000, &clear_out, &error);
			Check(clear_lowered && clear_out.fused_ee_cop1_compares == 0,
				"COP1 C.F clear is never mistaken for an EQ/LT/LE compare fusion");
		}

		for (u32 raw_index = 0; raw_index < raw_pairs.size(); ++raw_index)
		{
			auto fpr = initial_fpr;
			auto fcr = initial_fcr;
			fpr[fs] = raw_pairs[raw_index][0];
			fpr[ft] = raw_pairs[raw_index][1];
			fcr[31] = 0xa5c35a69u ^ (raw_index * 0x01010101u);
			std::vector<u32> code = {word};
			const EESnapshot expected = RunEEProgram(false, code, false, nullptr, initial_cp0.data(),
				initial_gpr.data(), fpr.data(), fcr.data());
			const bool condition = EeCop1CompareExpected(funct, fpr[fs], fpr[ft]);
			const u32 expected_fcr31 = (fcr[31] & ~0x00800000u) | (condition ? 0x00800000u : 0u);
			auto expected_fcr = fcr;
			expected_fcr[31] = expected_fcr31;
			Check(expected.fcr31 == expected_fcr31 && expected.fcr[31] == expected_fcr31 &&
				std::memcmp(expected.fcr, expected_fcr.data(), sizeof(expected.fcr)) == 0 &&
				std::memcmp(expected.fpr, fpr.data(), sizeof(expected.fpr)) == 0,
				"COP1 compare fusion oracle agrees with independent raw predicate/FCR model");
			const u64 before_standalone = EmuCoreXOracleEeIrCompareFusions();
			EeCop1CompareFusionRun(exact, false, code, initial_gpr.data(), initial_cp0.data(),
				fpr.data(), fcr.data(), expected, 0, false,
				"COP1 compare generic unoptimized direct body matches interpreter and emits no fusion");
			EeCop1CompareFusionRun(exact, true, code, initial_gpr.data(), initial_cp0.data(),
				fpr.data(), fcr.data(), expected, 1, false,
				"COP1 compare fused direct body matches complete interpreter state");
			Check(EmuCoreXOracleEeIrCompareFusions() == before_standalone,
				"Standalone COP1 compare lowering does not change production successful-compile counters");
		}

		if (case_index == 1)
		{
			bool live_valid = false;
			ir::Function live;
			live_valid = EeCop1CompareFusionWithLiveGprs(exact, live, EE_TEST_PC);
			Check(live_valid && EeCop1CompareFusionPlan(live, EeCop1CompareFusionStart(live, EE_TEST_PC)),
				"COP1 compare fusion keeps four true GPR values live across the private window");
			if (live_valid)
			{
				auto fpr = initial_fpr;
				auto fcr = initial_fcr;
				fpr[fs] = 0x7fc12345u;
				fpr[ft] = 0xffc54321u;
				fcr[31] = 0x35a5c057u;
				const std::vector<u32> code = {word};
				EESnapshot expected = RunEEProgram(false, code, false, nullptr, initial_cp0.data(),
					initial_gpr.data(), fpr.data(), fcr.data());
				for (u32 i = 0; i < 4; ++i)
				{
					expected.gpr[25 + i] = initial_gpr[21 + i].UD[0];
					// An I64 architectural write preserves the destination upper 64 bits.
					expected.gpr_upper[25 + i] = initial_gpr[25 + i].UD[1];
				}
				EeCop1CompareFusionRun(live, true, code, initial_gpr.data(), initial_cp0.data(),
					fpr.data(), fcr.data(), expected, 1, true,
					"COP1 compare fusion preserves four spilled live GPR values and full state");
			}
		}
	}

	// A fused EE expression must not be admitted by IOP lowering.
	const u32 eq_word = MipsCop1(0x10, 9, 5, 0, 0x32);
	ir::Function ee_fn;
	u32 ee_end = 0;
	std::string error;
	const bool ee_lifted = EeIr::LiftBlock(&eq_word, {EE_TEST_PC, 1}, ee_fn, &ee_end, &error);
	if (ee_lifted)
	{
		EeIr::LowerOptions wrong_domain;
		wrong_domain.guest_state = Arm64Ir::GuestState::IOP;
		std::array<u8, 512> poison;
		poison.fill(0xa5);
		EeIr::LowerOutput out{};
		const bool rejected = !EeIr::LowerBlock(ee_fn, wrong_domain, poison.data(), poison.size(), &out, &error);
		Check(rejected && out.entry == nullptr && out.host_size == 0 && out.fused_ee_cop1_compares == 0 &&
			std::all_of(poison.begin(), poison.end(), [](u8 byte) { return byte == 0xa5; }),
			"COP1 compare fusion and EE FPR state are rejected in the IOP lowering domain");
	}
}
