// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// NCLIP allocator-liveness test. Included after
// jit_oracle_iop_nclip_fusion.inl in the oracle's anonymous namespace.

static ir::Function IOPNclipWithLiveIntegerValues(const ir::Function& nclip,
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

    std::vector<u32> remap(nclip.value_types.size(), 0);
    const ir::Block* source = nclip.GetBlock(1);
    bool ok = source != nullptr && !source->insts.empty() &&
        source->insts.back().op == ir::Op::Resume;
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
    b.SetGuestPc(guest_pc + 4);
    for (u32 i = 0; ok && i < live.size(); ++i)
        b.Emit1(ir::Op::WriteGpr, ir::Type::Void, live[i], 25 + i);
    b.SetBlockEnd(guest_pc + 20);
    if (ok)
        b.Resume(guest_pc + 20);
    *valid = ok && ir::Verify(out, nullptr);
    return out;
}

static IOPSnapshot IOPNclipLiveInterpreter(const std::vector<u32>& program,
    const std::array<u32, 4>& live_inputs)
{
    std::memset(&psxRegs, 0, sizeof(psxRegs));
    for (u32 i = 0; i < 32; ++i)
    {
        psxRegs.CP2D.r[i] = 0x00112233u * (i + 1);
        psxRegs.CP2C.r[i] = 0x44556677u ^ (i * 0x01020304u);
    }
    for (u32 i = 0; i < live_inputs.size(); ++i)
        psxRegs.GPR.r[21 + i] = live_inputs[i];
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

static void IOPNclipLiveRestore(const IOPSnapshot& state, u32 entry_pc)
{
    std::memset(&psxRegs, 0, sizeof(psxRegs));
    std::memcpy(psxRegs.GPR.r, state.gpr, sizeof(state.gpr));
    psxRegs.GPR.r[32] = state.hi;
    psxRegs.GPR.r[33] = state.lo;
    std::memcpy(psxRegs.CP0.r, state.cp0, sizeof(state.cp0));
    std::memcpy(psxRegs.CP2D.r, state.cp2d, sizeof(state.cp2d));
    std::memcpy(psxRegs.CP2C.r, state.cp2c, sizeof(state.cp2c));
    for (u32 i = 0; i < IOP_SCRATCH_SIZE; ++i)
        iopMemWrite8(IOP_TEST_SCRATCH + i, state.scratch[i]);
    psxRegs.pc = entry_pc;
    psxRegs.cycle = 0;
    psxRegs.iopNextEventCycle = 0x7fffffffu;
}

static bool IOPNclipLiveLowerAndRun(const ir::Function& wrapper, bool optimize,
    const IOPSnapshot& pre_nclip, const IOPSnapshot& expected, u32 nclip_pc,
    const char* label, bool require_spill)
{
    ir::Function fn = wrapper;
    Arm64Ir::LowerOptions options;
    options.guest_state = Arm64Ir::GuestState::IOP;
    options.optimize_ir = optimize;
    options.capture_exit_pc = true;
    Arm64Ir::LowerOutput out;
    std::string error;
    u8* code = SysMemory::GetEERec() + 0x2000000;
    if (!Arm64Ir::LowerBlock(fn, options, code, 0x10000, &out, &error) || !out.entry)
    {
        std::printf("NCLIPLIVE LOWERFAIL %s: %s\n", label, error.c_str());
        Check(false, label);
        return false;
    }
    bool same = out.fused_iop_nclips == (optimize ? 1u : 0u);
    if (require_spill)
        same &= out.spill_values > 0;

    IOPNclipLiveRestore(pre_nclip, nclip_pc);
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

void IOPSharedNclipLiveValueTests()
{
    constexpr u32 input_count = 4;
    const std::array<u32, input_count> live_inputs = {
        0x13579bdfu, 0x2468ace0u, 0xdeadbeefu, 0x89abcdefu};
    const std::array<u32, 3> triangle = {
        IOPNclipPack(0, 0), IOPNclipPack(1, 0), IOPNclipPack(0, 1)};
    const std::vector<u32> setup = IOPNclipSetup(triangle, true);
    std::vector<u32> program = setup;
    program.push_back(IOPNclipWord());
    const u32 nclip_pc = IOP_TEST_PC + static_cast<u32>(setup.size()) * 4;
    for (u32 i = 0; i < input_count; ++i)
        program.push_back(MipsI(9, 21 + i, 25 + i, 0));

    const IOPSnapshot pre_nclip = IOPNclipLiveInterpreter(setup, live_inputs);
    const IOPSnapshot expected = IOPNclipLiveInterpreter(program, live_inputs);
    bool interpreter_safe = false;
    const u32 independent = IOPNclipExpected(pre_nclip.cp2d, &interpreter_safe);
    Check(interpreter_safe && expected.cp2d[24] == independent && expected.cp2c[31] == 0,
        "NCLIP live-value fixture matches independent determinant and FLAG model");
    for (u32 i = 0; i < input_count; ++i)
        Check(expected.gpr[25 + i] == live_inputs[i],
            "NCLIP live-value interpreter tail consumes the seeded dynamic GPR");

    ir::Function nclip;
    std::string error;
    u32 accepted = 0;
    const u32 word = IOPNclipWord();
    const bool lifted = IopIr::LiftSequence(&word, nclip_pc, 1, nclip, &accepted, &error) && accepted == 1;
    bool wrapper_valid = false;
    ir::Function wrapper = lifted ? IOPNclipWithLiveIntegerValues(nclip, nclip_pc, &wrapper_valid) : ir::Function{};
    const u32 first_inst = lifted && wrapper_valid ? IOPNclipFusionStart(wrapper, nclip_pc) : 0xffffffffu;
    Check(lifted && wrapper_valid && first_inst != 0xffffffffu &&
        IOPNclipFusionPlan(wrapper, first_inst),
        "NCLIP window with four cross-window SSA values remains fusion eligible");

    if (wrapper_valid)
    {
        const u64 before = EmuCoreXOracleIopIrNclipFusions();
        const bool baseline = IOPNclipLiveLowerAndRun(wrapper, false, pre_nclip, expected,
            nclip_pc, "unoptimized NCLIP wrapper preserves four SSA-live guest values", false);
        const bool fused = IOPNclipLiveLowerAndRun(wrapper, true, pre_nclip, expected,
            nclip_pc, "optimized NCLIP wrapper fuses with live SSA values and spills", true);
        Check(baseline && fused && EmuCoreXOracleIopIrNclipFusions() == before,
            "standalone NCLIP liveness lowering does not increment production fusion counter");
    }
}
