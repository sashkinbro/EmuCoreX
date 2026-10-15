// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// NCLIP expression planner/lowering tests. Included after
// jit_oracle_iop_nclip.inl inside the oracle's anonymous namespace.

static u32 IOPNclipFusionStart(const ir::Function& fn, u32 pc)
{
    const ir::Block* block = fn.GetBlock(1);
    if (!block)
        return 0xffffffffu;
    for (u32 i = 0; i < block->insts.size(); ++i)
        if (block->insts[i].guest_pc == pc && block->insts[i].op == ir::Op::ReadCp2Data &&
            block->insts[i].imm == 12)
            return i;
    return 0xffffffffu;
}

static bool IOPNclipFusionPlan(const ir::Function& fn, u32 first_inst)
{
    std::string error;
    return Arm64Ir::PlanIopNclipWindow(fn, 0, first_inst, &error);
}

static bool IOPNclipFusionRun(ir::Function fn, bool optimize, const IOPSnapshot& expected,
    const char* label, u32 expected_fusions)
{
    Arm64Ir::LowerOptions options;
    options.guest_state = Arm64Ir::GuestState::IOP;
    options.optimize_ir = optimize;
    options.capture_exit_pc = true;
    Arm64Ir::LowerOutput out;
    std::string error;
    u8* code = SysMemory::GetEERec() + 0x2000000;
    if (!Arm64Ir::LowerBlock(fn, options, code, 0x10000, &out, &error) || !out.entry)
    {
        std::printf("NCLIPFUSION LOWERFAIL %s: %s\n", label, error.c_str());
        return false;
    }
    bool same = out.fused_iop_nclips == expected_fusions;

    ResetIOPMemoryFixture();
    for (u32 i = 0; i < 32; ++i)
    {
        psxRegs.CP2D.r[i] = 0x00112233u * (i + 1);
        psxRegs.CP2C.r[i] = 0x44556677u ^ (i * 0x01020304u);
    }
    psxRegs.pc = IOP_TEST_PC;
    psxRegs.cycle = 0;
    psxRegs.iopNextEventCycle = 0x7fffffffu;
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

static bool IOPNclipFusionRejectsMutation(const ir::Function& source, u32 pc,
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
    for (u32 i = 0; i < block->insts.size(); ++i)
        if (block->insts[i].guest_pc == pc && block->insts[i].op == match_op && seen++ == occurrence)
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
    const bool rejected = !Arm64Ir::PlanIopNclipWindow(fn, 0, IOPNclipFusionStart(source, pc), &error);
    Check(rejected, label);
    return rejected;
}

void IOPSharedNclipFusionTests()
{
    const u32 command = IOPNclipWord();
    ir::Function exact;
    std::string error;
    u32 accepted = 0;
    const bool exact_lifted = IopIr::LiftSequence(&command, IOP_TEST_PC, 1, exact, &accepted, &error) && accepted == 1;
    Check(exact_lifted && ir::Verify(exact, &error), "NCLIP fusion source graph lifts and verifies");
    if (exact_lifted)
    {
        const u32 nonterminators = exact.blocks.empty() ? 0u :
            static_cast<u32>(exact.blocks[0].insts.size() - 1);
        Check(exact.blocks.size() == 1 && nonterminators == 24,
            "canonical NCLIP frontend graph retains 24 source operations before Resume");
        Check(IOPNclipFusionPlan(exact, 0), "NCLIP planner accepts the exact canonical source window");
        Check(!IOPNclipFusionPlan(exact, 1), "NCLIP planner rejects a shifted first-instruction index");
        Check(!Arm64Ir::PlanIopNclipWindow(exact, 1, 0, &error),
            "NCLIP planner rejects an out-of-range block index");

        ir::Function optimized = exact;
        ir::OptimizeIntegerValues(optimized);
        const u32 optimized_start = IOPNclipFusionStart(optimized, IOP_TEST_PC);
        Check(optimized_start != 0xffffffffu && IOPNclipFusionPlan(optimized, optimized_start),
            "NCLIP planner accepts the canonical window after integer optimization");

        const auto wrong_op = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].op = ir::Op::Add; };
        const auto wrong_type = [](ir::Function& fn, u32 i) {
            fn.blocks[0].insts[i].type = ir::Type::I64;
            fn.value_types[fn.blocks[0].insts[i].value] = ir::Type::I64;
        };
        const auto wrong_link = [](ir::Function& fn, u32 i) {
            fn.blocks[0].insts[i].args[1] = fn.blocks[0].insts[0].value;
        };
        const auto wrong_constant = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].imm = 15; };
        const auto wrong_pc = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].guest_pc += 4; };
        const auto wrong_aux = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].aux |= ir::IF_EXACT; };
        const auto wrong_index = [](ir::Function& fn, u32 i) { fn.blocks[0].insts[i].imm = 15; };

        IOPNclipFusionRejectsMutation(exact, IOP_TEST_PC, ir::Op::Mul, 0, wrong_op,
            "NCLIP planner rejects an unexpected arithmetic opcode");
        IOPNclipFusionRejectsMutation(exact, IOP_TEST_PC, ir::Op::Mul, 0, wrong_type,
            "NCLIP planner rejects a mistyped arithmetic value");
        IOPNclipFusionRejectsMutation(exact, IOP_TEST_PC, ir::Op::Mul, 0, wrong_link,
            "NCLIP planner rejects a broken SSA operand link");
        IOPNclipFusionRejectsMutation(exact, IOP_TEST_PC, ir::Op::ConstI32, 0, wrong_constant,
            "NCLIP planner rejects a changed shift constant");
        IOPNclipFusionRejectsMutation(exact, IOP_TEST_PC, ir::Op::Mul, 0, wrong_pc,
            "NCLIP planner rejects a mismatched guest PC in the window");
        IOPNclipFusionRejectsMutation(exact, IOP_TEST_PC, ir::Op::Mul, 0, wrong_aux,
            "NCLIP planner rejects unsupported instruction auxiliary flags");
        IOPNclipFusionRejectsMutation(exact, IOP_TEST_PC, ir::Op::ReadCp2Data, 0, wrong_index,
            "NCLIP planner rejects an unexpected CP2 register index");

        ir::Function external = exact;
        ir::Block& external_block = external.blocks[0];
        u32 determinant = 0, write_index = 0xffffffffu;
        for (u32 i = 0; i < external_block.insts.size(); ++i)
        {
            const ir::Inst& inst = external_block.insts[i];
            if (inst.op == ir::Op::WriteCp2Data && inst.imm == 24)
            {
                determinant = inst.args[0];
                write_index = i;
                break;
            }
        }
        bool external_inserted = determinant != 0 && write_index != 0xffffffffu &&
            !external_block.insts.empty() && external_block.insts.back().op == ir::Op::Resume;
        if (external_inserted)
        {
            ir::Inst use;
            use.op = ir::Op::WriteGpr;
            use.type = ir::Type::Void;
            use.args[0] = determinant;
            use.num_args = 1;
            use.imm = 25;
            use.guest_pc = IOP_TEST_PC + 4;
            external_block.insts.insert(external_block.insts.end() - 1, use);
        }
        const bool external_valid = external_inserted && ir::Verify(external, &error);
        const bool external_rejected = external_valid &&
            !Arm64Ir::PlanIopNclipWindow(external, 0, IOPNclipFusionStart(exact, IOP_TEST_PC), &error);
        Check(external_rejected,
            "NCLIP planner rejects a whole-function external use of the determinant");
    }

    std::array<u32, 3> triangle = {IOPNclipPack(0, 0), IOPNclipPack(1, 0), IOPNclipPack(0, 1)};
    std::vector<u32> code = IOPNclipSetup(triangle, true);
    code.push_back(MipsI(9, 0, 21, 0x1357));
    code.push_back(MipsR(21, 0, 0, 0, 0x11)); // HI remains live across NCLIP.
    const u32 nclip_pc = IOP_TEST_PC + static_cast<u32>(code.size()) * 4;
    code.push_back(command);
    code.push_back(MipsR(0, 0, 22, 0, 0x10)); // MFHI observes the value across the fused window.
    code.push_back(MipsI(9, 22, 23, 0x2468));
    code.push_back(MipsR(23, 0, 0, 0, 0x13));
    code.push_back(IOPCop2Word(0, 24, 24));
    ir::Function surrounded;
    accepted = 0;
    const bool surrounded_lifted = IopIr::LiftSequence(code.data(), IOP_TEST_PC,
        static_cast<u32>(code.size()), surrounded, &accepted, &error) && accepted == code.size();
    const u32 surrounded_start = surrounded_lifted ? IOPNclipFusionStart(surrounded, nclip_pc) : 0xffffffffu;
    Check(surrounded_lifted && surrounded_start != 0xffffffffu &&
        IOPNclipFusionPlan(surrounded, surrounded_start),
        "NCLIP planner accepts a window after prefix and before live-state tail");

    std::vector<u32> delayed = {MipsI(4, 0, 0, 2), command};
    ir::Function delay_fn;
    accepted = 0;
    const bool delay_lifted = IopIr::LiftSequence(delayed.data(), IOP_TEST_PC,
        static_cast<u32>(delayed.size()), delay_fn, &accepted, &error) && accepted == delayed.size();
    const u32 delay_start = delay_lifted ? IOPNclipFusionStart(delay_fn, IOP_TEST_PC + 4) : 0xffffffffu;
    Check(delay_lifted && delay_start != 0xffffffffu && IOPNclipFusionPlan(delay_fn, delay_start),
        "NCLIP planner accepts the canonical expression in a branch delay slot");

    if (surrounded_lifted)
    {
        const IOPSnapshot expected = RunIOPProgram(false, code);
        const u64 before = EmuCoreXOracleIopIrNclipFusions();
        const bool unoptimized = IOPNclipFusionRun(surrounded, false, expected,
            "unoptimized source window preserves GPR/HI/LO/CP2 and writes no fusion", 0);
        const bool optimized_run = IOPNclipFusionRun(surrounded, true, expected,
            "optimized fused NCLIP preserves live GPR/HI/LO/CP2 state", 1);
        Check(unoptimized && optimized_run && EmuCoreXOracleIopIrNclipFusions() == before,
            "standalone NCLIP lowering is separate from the production compile counter");
    }

    if (delay_lifted)
    {
        Arm64Ir::LowerOptions optimized_options;
        optimized_options.guest_state = Arm64Ir::GuestState::IOP;
        optimized_options.optimize_ir = true;
        Arm64Ir::LowerOptions baseline_options = optimized_options;
        baseline_options.optimize_ir = false;
        Arm64Ir::LowerOutput optimized_out, baseline_out;
        u8* code_buffer = SysMemory::GetEERec() + 0x2000000;
        ir::Function optimized_delay = delay_fn, baseline_delay = delay_fn;
        bool lowered = Arm64Ir::LowerBlock(optimized_delay, optimized_options, code_buffer, 0x10000,
            &optimized_out, &error) && optimized_out.fused_iop_nclips == 1;
        lowered &= Arm64Ir::LowerBlock(baseline_delay, baseline_options, code_buffer, 0x10000,
            &baseline_out, &error) && baseline_out.fused_iop_nclips == 0;
        Check(lowered, "NCLIP delay-slot lowering fuses only when integer optimization is enabled");
    }

    if (exact_lifted)
    {
        std::array<u8, 512> poison;
        poison.fill(0xa5);
        Arm64Ir::LowerOptions wrong_domain;
        wrong_domain.guest_state = Arm64Ir::GuestState::EE;
        Arm64Ir::LowerOutput rejected_out;
        const bool rejected = !Arm64Ir::LowerBlock(exact, wrong_domain, poison.data(), poison.size(),
            &rejected_out, &error);
        Check(rejected && rejected_out.host_size == 0 && rejected_out.entry == nullptr &&
            rejected_out.fused_iop_nclips == 0 &&
            std::all_of(poison.begin(), poison.end(), [](u8 byte) { return byte == 0xa5; }),
            "NCLIP fusion graph rejects EE lowering before any emission");
    }
}
