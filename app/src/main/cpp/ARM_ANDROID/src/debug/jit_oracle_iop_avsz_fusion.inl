// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// AVSZ expression planner and standalone fusion regressions. Include after
// jit_oracle_iop_avsz.inl inside the oracle's anonymous namespace.

extern "C" u64 EmuCoreXOracleIopIrAvszFusions();

namespace
{
	static u32 IOPAvszFusionStart(const ir::Function& fn, u32 guest_pc, u32 first_register)
	{
		const ir::Block* block = fn.GetBlock(1);
		if (!block)
			return 0xffffffffu;
		for (u32 i = 0; i < block->insts.size(); ++i)
			if (block->insts[i].guest_pc == guest_pc &&
				block->insts[i].op == ir::Op::ReadCp2Data &&
				block->insts[i].imm == first_register)
				return i >= 6 ? i - 6 : 0xffffffffu;
		return 0xffffffffu;
	}

	static bool IOPAvszFusionPlan(const ir::Function& fn, u32 first_inst,
		Arm64Ir::IopAvszPlan* plan = nullptr)
	{
		std::string error;
		Arm64Ir::IopAvszPlan local{};
		return Arm64Ir::PlanIopAvszWindow(fn, 0, first_inst, plan ? plan : &local, &error);
	}

	static bool IOPAvszFusionRejectsMutation(const ir::Function& source, u32 guest_pc,
		u32 first_register, ir::Op match_op, u32 occurrence,
		void (*mutate)(ir::Function&, u32), const char* label)
	{
		ir::Function fn = source;
		ir::Block* block = fn.GetBlock(1);
		if (!block)
		{
			Check(false, label);
			return false;
		}
		u32 seen = 0, selected = 0xffffffffu;
		for (u32 i = 0; i < block->insts.size(); ++i)
			if (block->insts[i].guest_pc == guest_pc && block->insts[i].op == match_op && seen++ == occurrence)
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
		std::string error;
		Arm64Ir::IopAvszPlan plan{0xffffffffu, true};
		const bool rejected = !Arm64Ir::PlanIopAvszWindow(fn, 0,
			IOPAvszFusionStart(source, guest_pc, first_register), &plan, &error);
		const bool cleared = plan.inst_count == 0 && !plan.avsz4;
		Check(rejected && cleared, label);
		return rejected && cleared;
	}

	static bool IOPAvszFusionRun(const ir::Function& fn, bool optimize,
		const IOPSnapshot& expected, const IOPAvszInputs& input,
		const std::array<u32, 4>* live_gprs, const char* label, u32 expected_fusions,
		bool require_spill = false)
	{
		Arm64Ir::LowerOptions options;
		options.guest_state = Arm64Ir::GuestState::IOP;
		options.optimize_ir = optimize;
		options.capture_exit_pc = true;
		Arm64Ir::LowerOutput out;
		std::string error;
		ir::Function lowering_fn = fn;
		u8* code = SysMemory::GetEERec() + 0x2000000;
		if (!Arm64Ir::LowerBlock(lowering_fn, options, code, 0x10000, &out, &error) || !out.entry)
		{
			std::printf("AVSZFUSION LOWERFAIL %s: %s\n", label, error.c_str());
			Check(false, label);
			return false;
		}

		bool same = out.fused_iop_avsz == expected_fusions &&
			(!require_spill || out.spill_values > 0);
		IOPAvszSeedGuestState(input, true);
		if (live_gprs)
			for (u32 i = 0; i < live_gprs->size(); ++i)
				psxRegs.GPR.r[21 + i] = (*live_gprs)[i];
		g_eeir_exit_pc = 0;
		g_eeir_exit_valid = 0;
		reinterpret_cast<void (*)()>(out.entry)();
		IOPSnapshot actual;
		CaptureIOP(actual);
		same &= g_eeir_exit_valid != 0 && g_eeir_exit_pc == expected.pc;
		actual.pc = g_eeir_exit_pc;
		same &= std::memcmp(actual.gpr, expected.gpr, sizeof(actual.gpr)) == 0;
		same &= actual.hi == expected.hi && actual.lo == expected.lo;
		same &= std::memcmp(actual.cp0, expected.cp0, sizeof(actual.cp0)) == 0;
		same &= std::memcmp(actual.cp2d, expected.cp2d, sizeof(actual.cp2d)) == 0;
		same &= std::memcmp(actual.cp2c, expected.cp2c, sizeof(actual.cp2c)) == 0;
		same &= std::memcmp(actual.scratch, expected.scratch, sizeof(actual.scratch)) == 0;
		Check(same, label);
		return same;
	}

	static ir::Function IOPAvszWithLiveGprs(const ir::Function& avsz,
		u32 guest_pc, bool* valid)
	{
		ir::Function out;
		ir::Builder b(out);
		b.CreateBlock(guest_pc);
		std::array<u32, 4> live{};
		for (u32 i = 0; i < live.size(); ++i)
		{
			b.SetGuestPc(guest_pc);
			live[i] = b.Emit(ir::Op::ReadGpr, ir::Type::I32, {}, 21 + i, 0, guest_pc);
		}

		std::vector<u32> remap(avsz.value_types.size(), 0);
		const ir::Block* source = avsz.GetBlock(1);
		bool ok = source != nullptr && !source->insts.empty() && source->insts.back().op == ir::Op::Resume;
		if (ok)
		{
			for (const ir::Inst& inst : source->insts)
			{
				if (inst.op == ir::Op::Resume)
					break;
				u32 args[4] = {};
				for (u32 a = 0; a < inst.num_args; ++a)
				{
					if (inst.args[a] == 0 || inst.args[a] >= remap.size() || remap[inst.args[a]] == 0)
					{
						ok = false;
						break;
					}
					args[a] = remap[inst.args[a]];
				}
				if (!ok)
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
					default: ok = false; break;
				}
				if (!ok)
					break;
				if (inst.value)
				{
					if (value == 0 || inst.value >= remap.size())
					{
						ok = false;
						break;
					}
					remap[inst.value] = value;
				}
			}
		}

		b.SetDelaySlot(false);
		for (u32 i = 0; ok && i < live.size(); ++i)
		{
			b.SetGuestPc(guest_pc + 4 + i * 4);
			b.Emit1(ir::Op::WriteGpr, ir::Type::Void, live[i], 25 + i);
		}
		b.SetBlockEnd(guest_pc + 20);
		if (ok)
			b.Resume(guest_pc + 20);
		*valid = ok && ir::Verify(out, nullptr);
		return out;
	}

	static IOPSnapshot IOPAvszFusionInterpreter(const IOPAvszInputs& input,
		const std::vector<u32>& program, const std::array<u32, 4>* live_gprs)
	{
		IOPAvszSeedGuestState(input, true);
		if (live_gprs)
			for (u32 i = 0; i < live_gprs->size(); ++i)
				psxRegs.GPR.r[21 + i] = (*live_gprs)[i];
		const u32 sentinel = IOP_TEST_PC + static_cast<u32>(program.size()) * 4;
		for (u32 i = 0; i < program.size(); ++i)
			iopMemWrite32(IOP_TEST_PC + i * 4, program[i]);
		iopMemWrite32(sentinel, 0x1000ffffu);
		iopMemWrite32(sentinel + 4, 0);
		psxRegs.pc = IOP_TEST_PC;
		OpcodeFamilies::SetFamilyMask(OpcodeFamilies::CORE_IOP, 0);
		psxCpu = &psxInt;
		psxInt.Reset();
		psxCpu->ExecuteBlock(static_cast<s32>(program.size() + 16) * 8);
		IOPSnapshot result;
		CaptureIOP(result);
		if (result.pc >= sentinel && result.pc < sentinel + 8)
			result.pc = sentinel;
		return result;
	}
}

void IOPSharedAvszFusionTests()
{
	const IOPAvszInputs input{{0xaaaa0001u, 0xbbbb7fffu, 0xcccc8000u, 0xddddffffu},
		0x00007000u, 0xffff9000u};
	for (bool avsz4 : {false, true})
	{
		const u32 command = IOPAvszWord(avsz4);
		ir::Function exact;
		std::string error;
		u32 accepted = 0;
		const bool lifted = IopIr::LiftSequence(&command, IOP_TEST_PC, 1,
			exact, &accepted, &error) && accepted == 1 && ir::Verify(exact, &error);
		Check(lifted, avsz4 ? "AVSZ4 exact source graph lifts and verifies" :
			"AVSZ3 exact source graph lifts and verifies");
		if (!lifted)
			continue;

		const u32 first_reg = avsz4 ? 16u : 17u;
		const u32 first_inst = IOPAvszFusionStart(exact, IOP_TEST_PC, first_reg);
		const ir::Block* block = exact.GetBlock(1);
		const u32 expected_inst_count = block && !block->insts.empty() ?
			static_cast<u32>(block->insts.size() - 1) : 0;
		Arm64Ir::IopAvszPlan plan{};
		Check(first_inst != 0xffffffffu && IOPAvszFusionPlan(exact, first_inst, &plan) &&
			plan.inst_count == expected_inst_count && plan.avsz4 == avsz4,
			"AVSZ planner returns exact source-window length and operation kind");
		Arm64Ir::IopAvszPlan rejected_plan{0xffffffffu, true};
		const bool shifted_rejected = !Arm64Ir::PlanIopAvszWindow(exact, 0,
			first_inst + 1, &rejected_plan, &error);
		Check(shifted_rejected && rejected_plan.inst_count == 0 && !rejected_plan.avsz4,
			"AVSZ planner clears its output and rejects a shifted first instruction");
		rejected_plan = {0xffffffffu, true};
		const bool block_rejected = !Arm64Ir::PlanIopAvszWindow(exact, 1,
			first_inst, &rejected_plan, &error);
		Check(block_rejected && rejected_plan.inst_count == 0 && !rejected_plan.avsz4,
			"AVSZ planner clears its output and rejects an invalid block index");

		ir::Function optimized = exact;
		ir::OptimizeIntegerValues(optimized);
		const u32 optimized_start = IOPAvszFusionStart(optimized, IOP_TEST_PC, first_reg);
		Check(optimized_start != 0xffffffffu && IOPAvszFusionPlan(optimized, optimized_start),
			"AVSZ planner accepts the exact source window after integer optimization");

		const auto wrong_op = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].op = ir::Op::Add; };
		const auto invalid_op = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].op = ir::Op::Nop; };
		const auto duplicate_definition = [](ir::Function& fn, u32 i) {
			fn.blocks[0].insts[i].value = fn.blocks[0].insts[0].value;
		};
		const auto wrong_type = [](ir::Function& fn, u32 i) {
			fn.blocks[0].insts[i].type = ir::Type::I64;
			fn.value_types[fn.blocks[0].insts[i].value] = ir::Type::I64;
		};
		const auto wrong_link = [](ir::Function& fn, u32 i) {
			fn.blocks[0].insts[i].args[1] = fn.blocks[0].insts[0].value;
		};
		const auto wrong_constant = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].imm ^= 1; };
		const auto wrong_pc = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].guest_pc += 4; };
		const auto wrong_aux = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].aux |= ir::IF_EXACT; };
		const auto wrong_index = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].imm ^= 1; };
		IOPAvszFusionRejectsMutation(exact, IOP_TEST_PC, first_reg, ir::Op::Mul, 0, wrong_op,
			"AVSZ planner rejects an unexpected arithmetic operation");
		IOPAvszFusionRejectsMutation(exact, IOP_TEST_PC, first_reg, ir::Op::Mul, 0, invalid_op,
			"AVSZ planner rejects an invalid opcode and clears its plan");
		IOPAvszFusionRejectsMutation(exact, IOP_TEST_PC, first_reg, ir::Op::Mul, 0, duplicate_definition,
			"AVSZ planner rejects duplicate SSA definitions and clears its plan");
		IOPAvszFusionRejectsMutation(exact, IOP_TEST_PC, first_reg, ir::Op::Mul, 0, wrong_type,
			"AVSZ planner rejects a mistyped multiplication value");
		IOPAvszFusionRejectsMutation(exact, IOP_TEST_PC, first_reg, ir::Op::Mul, 0, wrong_link,
			"AVSZ planner rejects a broken SSA operand link");
		IOPAvszFusionRejectsMutation(exact, IOP_TEST_PC, first_reg, ir::Op::ConstI32, 0, wrong_constant,
			"AVSZ planner rejects a changed shift or mask constant");
		IOPAvszFusionRejectsMutation(exact, IOP_TEST_PC, first_reg, ir::Op::Mul, 0, wrong_pc,
			"AVSZ planner rejects a mismatched guest PC");
		IOPAvszFusionRejectsMutation(exact, IOP_TEST_PC, first_reg, ir::Op::Mul, 0, wrong_aux,
			"AVSZ planner rejects unsupported auxiliary flags");
		IOPAvszFusionRejectsMutation(exact, IOP_TEST_PC, first_reg, ir::Op::ReadCp2Data, 0, wrong_index,
			"AVSZ planner rejects an unexpected CP2 data-register index");

		ir::Function external = exact;
		ir::Block& external_block = external.blocks[0];
		u32 mac0 = 0;
		bool found_mac0 = false;
		for (const ir::Inst& inst : external_block.insts)
			if (inst.op == ir::Op::WriteCp2Data && inst.imm == 24)
			{
				mac0 = inst.args[0];
				found_mac0 = true;
				break;
			}
		bool inserted = found_mac0 && !external_block.insts.empty() &&
			external_block.insts.back().op == ir::Op::Resume;
		if (inserted)
		{
			ir::Inst use;
			use.op = ir::Op::WriteGpr;
			use.type = ir::Type::Void;
			use.args[0] = mac0;
			use.num_args = 1;
			use.imm = 25;
			use.guest_pc = IOP_TEST_PC + 4;
			external_block.insts.insert(external_block.insts.end() - 1, use);
		}
		const bool external_rejected = inserted && ir::Verify(external, &error) &&
			!IOPAvszFusionPlan(external, first_inst);
		Check(external_rejected, "AVSZ planner rejects a whole-function external MAC0 use");

		const std::vector<u32> program = {command};
		const IOPSnapshot expected = IOPAvszFusionInterpreter(input, program, nullptr);
		const u64 before = EmuCoreXOracleIopIrAvszFusions();
		const bool baseline = IOPAvszFusionRun(exact, false, expected, input, nullptr,
			avsz4 ? "unoptimized AVSZ4 standalone lowering preserves full CPU state" :
			"unoptimized AVSZ3 standalone lowering preserves full CPU state", 0);
		const bool fused = IOPAvszFusionRun(exact, true, expected, input, nullptr,
			avsz4 ? "optimized AVSZ4 standalone lowering preserves full CPU state" :
			"optimized AVSZ3 standalone lowering preserves full CPU state", 1);
		Check(baseline && fused && EmuCoreXOracleIopIrAvszFusions() == before,
			"standalone AVSZ lowering remains separate from production route counters");

		Arm64Ir::LowerOptions ee_options;
		ee_options.guest_state = Arm64Ir::GuestState::EE;
		ee_options.optimize_ir = true;
		Arm64Ir::LowerOutput ee_out{};
		const bool ee_lowered = Arm64Ir::LowerBlock(exact, ee_options,
			SysMemory::GetEERec() + 0x2000000, 0x10000, &ee_out, &error);
		Check(!ee_lowered && ee_out.fused_iop_avsz == 0,
			"AVSZ fusion is rejected outside the IOP guest-state domain");
	}

	const bool avsz4 = true;
	const u32 command = IOPAvszWord(avsz4);
	ir::Function source;
	std::string error;
	u32 accepted = 0;
	const bool lifted = IopIr::LiftSequence(&command, IOP_TEST_PC, 1,
		source, &accepted, &error) && accepted == 1;
	constexpr std::array<u32, 4> live_inputs = {
		0x13579bdfu, 0x2468ace0u, 0xdeadbeefu, 0x89abcdefu};
	bool wrapper_valid = false;
	ir::Function wrapper = lifted ? IOPAvszWithLiveGprs(source, IOP_TEST_PC, &wrapper_valid) : ir::Function{};
	const u32 wrapper_start = lifted && wrapper_valid ?
		IOPAvszFusionStart(wrapper, IOP_TEST_PC, 16) : 0xffffffffu;
	Check(lifted && wrapper_valid && wrapper_start != 0xffffffffu &&
		IOPAvszFusionPlan(wrapper, wrapper_start),
		"AVSZ planner accepts a window with four guest integer values live across it");
	if (wrapper_valid)
	{
		const IOPAvszInputs live_input{{0xaaaa0001u, 0xbbbb7fffu, 0xcccc8000u, 0xddddffffu},
			0x7000u, 0xffff9000u};
		const IOPSnapshot live_expected = IOPAvszFusionInterpreter(live_input,
			{command, MipsI(9, 21, 25, 0), MipsI(9, 22, 26, 0),
				MipsI(9, 23, 27, 0), MipsI(9, 24, 28, 0)}, &live_inputs);
		const IOPAvszOutput live_output = IOPAvszExpectedOutput(live_input, true);
		bool model_ok = live_expected.cp2d[24] == live_output.mac0 &&
		live_expected.cp2d[7] == (((0x00112233u * 8u) & 0xffff0000u) | live_output.otz);
		for (u32 i = 0; i < live_inputs.size(); ++i)
			model_ok &= live_expected.gpr[25 + i] == live_inputs[i];
		Check(model_ok, "AVSZ live-value expected outputs match independent model and seeded GPRs");
		const bool baseline = IOPAvszFusionRun(wrapper, false, live_expected, live_input, &live_inputs,
			"unoptimized AVSZ preserves four live guest GPR values", 0);
		const bool fused = IOPAvszFusionRun(wrapper, true, live_expected, live_input, &live_inputs,
			"optimized AVSZ fusion preserves four live guest GPR values and spills", 1, true);
		Check(baseline && fused, "AVSZ fusion retains live SSA values across the window");
	}
}
