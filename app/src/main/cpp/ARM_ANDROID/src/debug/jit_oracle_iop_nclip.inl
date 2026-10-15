// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// NCLIP coverage tests. Included after jit_oracle_iop_cop2.inl, inside the oracle
// anonymous namespace. Dedicated compilation/fusion counters prove actual shared
// execution; state parity alone cannot establish which backend compiled it.
extern "C" u64 EmuCoreXOracleIopIrNclipCompiles();
extern "C" u64 EmuCoreXOracleIopIrNclipFusions();

static u32 IOPNclipWord(u32 flags = 0, u32 rs = 16, bool lm = false)
{
    return (18u << 26) | (rs << 21) | ((flags & 0x1fu) << 16) | (lm ? 0x400u : 0u) | 6u;
}

static u32 IOPNclipPack(s32 x, s32 y)
{
    return (static_cast<u32>(x) & 0xffffu) | (static_cast<u32>(y) << 16);
}

static u32 IOPNclipExpected(const u32* data, bool* interpreter_safe)
{
    // Signed extraction without implementation-defined unsigned-to-signed casts.
    const auto signed16 = [](u32 word) -> s64 {
        const u32 low = word & 0xffffu;
        return low < 0x8000u ? static_cast<s64>(low) : static_cast<s64>(low) - 65536;
    };
    const s64 x0 = signed16(data[12]), y0 = signed16(data[12] >> 16);
    const s64 x1 = signed16(data[13]), y1 = signed16(data[13] >> 16);
    const s64 x2 = signed16(data[14]), y2 = signed16(data[14] >> 16);
    const s64 a = x0 * (y1 - y2), b = x1 * (y2 - y0), c = x2 * (y0 - y1);
    const auto fits = [](s64 value) { return value >= -2147483648ll && value <= 2147483647ll; };
    // The current C++ interpreter uses signed-int products and a left-associated
    // sum. Do not use it as an oracle when any intermediate would overflow.
    *interpreter_safe = fits(a) && fits(b) && fits(c) && fits(a + b) && fits(a + b + c);
    return static_cast<u32>(a + b + c); // Defined modulo 2^32, including negative results.
}

static std::vector<u32> IOPNclipSetup(const std::array<u32, 3>& vertices, bool fifo)
{
    std::vector<u32> code;
    for (u32 point = 0; point < 3; ++point)
    {
        const u32 reg = 8 + point;
        code.push_back(MipsI(15, 0, reg, vertices[point] >> 16));
        code.push_back(MipsI(13, reg, reg, vertices[point]));
        // FIFO writes exercise the MTC2 SXYP alias before the NCLIP consumer.
        code.push_back(IOPCop2Word(4, reg, fifo ? 15 : 12 + point));
    }
    code.push_back(MipsI(15, 0, 11, 0xfedc));
    code.push_back(MipsI(13, 11, 11, 0xba98));
    code.push_back(IOPCop2Word(6, 11, 31)); // Dirty FLAG must be cleared, not merged.
    code.push_back(MipsR(11, 0, 0, 0, 0x11)); // Nonzero HI/LO sentinels.
    code.push_back(MipsR(10, 0, 0, 0, 0x13));
    return code;
}

static void IOPNclipStateCase(const std::vector<u32>& setup, std::vector<u32> tail,
    IOPSnapshot expected, u32 compiles, bool safe_interpreter, const char* label)
{
    std::vector<u32> code = setup;
    code.insert(code.end(), tail.begin(), tail.end());
    expected.pc = IOP_TEST_PC + static_cast<u32>(code.size()) * 4;
    std::printf("IOPNCLIPCASE %s words=%zu interpreter_safe=%u\n", label, code.size(), safe_interpreter ? 1u : 0u);
    EmuCoreXOracleSetIOPIR(0);
    const u64 disabled_before = EmuCoreXOracleIopIrNclipCompiles();
    if (safe_interpreter)
        CompareIOP(expected, RunIOPProgram(false, code), "NCLIP interpreter matches independent complete state");
    const IOPSnapshot legacy = RunIOPProgram(true, code);
    const s32 legacy_budget = psxRegs.iopCycleEE;
    const u32 legacy_carry = psxRegs.iopCycleEECarry, legacy_next_event = psxRegs.iopNextEventCycle;
    CompareIOP(expected, legacy, "NCLIP legacy matches independent complete state");
    Check(EmuCoreXOracleIopIrNclipCompiles() == disabled_before,
        "disabled common IOP NCLIP leaves successful compile counter unchanged");
    EmuCoreXOracleSetIOPIR(1);
    const u64 before = EmuCoreXOracleIopIrNclipCompiles();
    const IOPSnapshot shared = RunIOPProgram(true, code);
    CompareIOP(expected, shared, "NCLIP shared matches independent GPR/HI/LO/CP0/CP2/scratch/PC state");
    Check(shared.cycle == legacy.cycle && psxRegs.iopCycleEE == legacy_budget &&
        psxRegs.iopCycleEECarry == legacy_carry && psxRegs.iopNextEventCycle == legacy_next_event,
        "NCLIP common route retains exact legacy cycle/budget/carry/event tail");
    Check(EmuCoreXOracleIopIrNclipCompiles() == before + compiles,
        "NCLIP increments only the exact successful common compilation count");
    if (safe_interpreter)
    {
        const u64 forced_before = EmuCoreXOracleIopIrNclipCompiles();
        const IOPSnapshot forced = RunIOPProgram(true, code, -1, true);
        CompareIOP(expected, forced, "forced GTE interpreter NCLIP retains independent state");
        Check(EmuCoreXOracleIopIrNclipCompiles() == forced_before,
            "forced GTE interpretation never increments NCLIP compilation count");
    }
}

void IOPSharedNclipTests()
{
    // Independent fixed answers expose both orientation and modulo semantics.
    std::array<u32, 32> model_data{};
    model_data[12] = IOPNclipPack(0, 0);
    model_data[13] = IOPNclipPack(1, 0);
    model_data[14] = IOPNclipPack(0, 1);
    bool safe = false;
    Check(IOPNclipExpected(model_data.data(), &safe) == 1 && safe, "NCLIP positive unit triangle model");
    std::swap(model_data[13], model_data[14]);
    Check(IOPNclipExpected(model_data.data(), &safe) == 0xffffffffu && safe, "NCLIP negative unit triangle model");
    model_data[12] = IOPNclipPack(-32768, -32768);
    model_data[13] = IOPNclipPack(32767, -32768);
    model_data[14] = IOPNclipPack(-32768, 32767);
    Check(IOPNclipExpected(model_data.data(), &safe) == 0xfffe0001u && !safe,
        "NCLIP extreme determinant wraps independently and excludes interpreter signed overflow");

    for (u32 flags : {0u, 8u, 16u, 24u, 2u, 18u, 20u, 28u})
    for (bool lm : {false, true})
    {
        const u32 word = IOPNclipWord(flags, 16, lm);
        ir::Function fn;
        std::string error;
        u32 accepted = 0;
        const bool lifted = IopIr::LiftSequence(&word, IOP_TEST_PC, 1, fn, &accepted, &error) && accepted == 1;
        Check(lifted && ir::Verify(fn, &error) &&
            Arm64Ir::CanLower(fn, false, &error, Arm64Ir::GuestState::IOP),
            "NCLIP canonical and ignored SF/LM encodings admit typed common integer graphs");
        if (lifted)
        {
            std::array<u8, 512> poison;
            poison.fill(0xa5);
            Arm64Ir::LowerOutput out;
            Arm64Ir::LowerOptions options;
            options.guest_state = Arm64Ir::GuestState::EE;
            Check(!Arm64Ir::LowerBlock(fn, options, poison.data(), poison.size(), &out, &error) &&
                out.host_size == 0 && out.entry == nullptr &&
                std::all_of(poison.begin(), poison.end(), [](u8 byte) { return byte == 0xa5; }),
                "IOP-only NCLIP state graph rejects EE lowering before any emission");
        }
    }
    for (u32 word : {IOPNclipWord(0, 0), IOPNclipWord(0, 2), IOPNclipWord(0, 4),
        IOPNclipWord(0, 6), IOPNclipWord(0, 31), (18u << 26) | (16u << 21) | 1u})
    {
        const u64 before = EmuCoreXOracleIopIrNclipCompiles();
        ir::Function fn;
        std::string error;
        u32 accepted = 99;
        const bool rejected = !IopIr::LiftSequence(&word, IOP_TEST_PC, 1, fn, &accepted, &error);
        Check(rejected && accepted == 0 && fn.blocks.empty(),
            "NCLIP bounded frontend rejects noncanonical Rs and other GTE commands");
        Check(EmuCoreXOracleIopIrNclipCompiles() == before,
            "rejected noncanonical NCLIP encoding leaves successful compile count unchanged");
    }

    const std::array<std::array<u32, 3>, 9> triangles = {{
        {IOPNclipPack(0, 0), IOPNclipPack(1, 0), IOPNclipPack(0, 1)},
        {IOPNclipPack(0, 0), IOPNclipPack(0, 1), IOPNclipPack(1, 0)},
        {IOPNclipPack(-4, 7), IOPNclipPack(-4, 7), IOPNclipPack(-4, 7)},
        {IOPNclipPack(-100, -100), IOPNclipPack(0, 0), IOPNclipPack(100, 100)},
        {IOPNclipPack(-32768, -32768), IOPNclipPack(32767, -32768), IOPNclipPack(-32768, 32767)},
        {IOPNclipPack(-32768, -32768), IOPNclipPack(-32768, 32767), IOPNclipPack(32767, -32768)},
        {IOPNclipPack(32767, 32767), IOPNclipPack(-32768, 32767), IOPNclipPack(32767, -32768)},
        {IOPNclipPack(-32768, 0), IOPNclipPack(32767, 1), IOPNclipPack(1, -32768)},
        {IOPNclipPack(-1, 1), IOPNclipPack(1, -1), IOPNclipPack(32767, 32767)}
    }};
    for (const auto& triangle : triangles)
    for (bool fifo : {false, true})
    for (u32 flags : {0u, 8u, 16u, 24u})
    for (bool lm : {false, true})
    {
        const auto setup = IOPNclipSetup(triangle, fifo);
        EmuCoreXOracleSetIOPIR(0);
        IOPSnapshot expected = RunIOPProgram(false, setup);
        Check(expected.cp2d[12] == triangle[0] && expected.cp2d[13] == triangle[1] &&
            expected.cp2d[14] == triangle[2] && expected.cp2c[31] == 0xfedcba98u,
            "NCLIP setup establishes independent packed vertices and dirty FLAG");
        const u32 value = IOPNclipExpected(expected.cp2d, &safe);
        expected.cp2d[24] = value;
        expected.cp2c[31] = 0;
        expected.gpr[18] = value;
        expected.gpr[19] = 0;
        IOPNclipStateCase(setup, {IOPNclipWord(flags, 16, lm), IOPCop2Word(0, 18, 24), IOPCop2Word(2, 19, 31)},
            expected, 1, safe, fifo ? "MTC2 FIFO alias -> NCLIP -> MFC2/CFC2" : "signed edges and SF/LM");
    }

    const auto setup = IOPNclipSetup(triangles[0], false);
    for (bool taken : {false, true})
    {
        EmuCoreXOracleSetIOPIR(0);
        IOPSnapshot expected = RunIOPProgram(false, setup);
        expected.cp2d[24] = IOPNclipExpected(expected.cp2d, &safe);
        expected.cp2c[31] = 0;
        expected.gpr[18] = expected.cp2d[24];
        expected.gpr[20] = taken ? 0 : 0x55;
        IOPNclipStateCase(setup, {MipsI(taken ? 4 : 5, 0, 0, 2), IOPNclipWord(),
            MipsI(9, 0, 20, 0x55), IOPCop2Word(0, 18, 24)}, expected, 1, safe,
            taken ? "taken branch NCLIP delay" : "untaken branch NCLIP delay");
    }
    {
        EmuCoreXOracleSetIOPIR(0);
        IOPSnapshot expected = RunIOPProgram(false, setup);
        expected.cp2d[24] = IOPNclipExpected(expected.cp2d, &safe);
        const bool first_safe = safe;
        expected.gpr[18] = expected.cp2d[24];
        expected.cp2d[12] = expected.gpr[18];
        expected.cp2d[24] = IOPNclipExpected(expected.cp2d, &safe);
        expected.cp2c[31] = 0;
        expected.gpr[19] = expected.cp2d[24];
        IOPNclipStateCase(setup, {IOPNclipWord(), IOPCop2Word(0, 18, 24), IOPCop2Word(4, 18, 12),
            IOPNclipWord(24), IOPCop2Word(0, 19, 24)}, expected, 2, safe && first_safe,
            "NCLIP/MFC2/MTC2 chained source replacement");
    }

    // Do not execute reserved/noncanonical Rs variants as NCLIP tests. They are
    // rejected above because the command contract is canonical Rs=16; baseline
    // interpreter and legacy JIT behavior already diverges for those malformed
    // COP2 words, outside this slice's semantic oracle.

    // Existing hooks suffice for bounded cold/hot and whole-block byte evidence.
    // These times include setup/state reset and event tails; they are not isolated
    // instruction timings or whole-game FPS. Capture each mode before replacing
    // its resident code cache with the other mode.
    constexpr u32 commands = 32, samples = 7;
    std::vector<u32> benchmark = setup;
    benchmark.insert(benchmark.end(), commands, IOPNclipWord());
    const s32 budget = static_cast<s32>(benchmark.size() + 16) * 8;
    IOPSnapshot expected = RunIOPProgram(false, setup);
    expected.cp2d[24] = IOPNclipExpected(expected.cp2d, &safe);
    expected.cp2c[31] = 0;
    expected.pc = IOP_TEST_PC + static_cast<u32>(benchmark.size()) * 4;
    std::array<IOPSnapshot, 2> cold{};
    std::array<u64, 2> native_bytes{};
    for (u32 mode = 0; mode < 2; ++mode)
    {
        EmuCoreXOracleSetIOPIR(mode);
        const u64 before = EmuCoreXOracleIopIrNclipCompiles();
        const u64 fused_before = EmuCoreXOracleIopIrNclipFusions();
        const auto start = std::chrono::steady_clock::now();
        cold[mode] = RunIOPProgram(true, benchmark, budget);
        const double cold_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        CompareIOP(expected, cold[mode], "NCLIP benchmark cold full-state expectation");
        Check(EmuCoreXOracleIopIrNclipCompiles() == before + (mode ? commands : 0),
            "NCLIP benchmark cold compilation count proves selected route");
        Check(EmuCoreXOracleIopIrNclipFusions() == fused_before + (mode ? commands : 0),
            "NCLIP benchmark uses the fused common integer graph for every selected command");
        std::vector<JitBlockProfile> profiles;
        IOP_JitGetBlockProfiles(profiles);
        u64 bytes = 0;
        bool bounds = true;
        for (const auto& profile : profiles)
            if (profile.startpc >= IOP_TEST_PC && profile.startpc <= expected.pc)
            {
                bytes += profile.host_size;
                bounds &= profile.host_size != 0 && profile.host_size < 0x10000;
            }
        Check(bounds && bytes != 0, "NCLIP benchmark has bounded native block byte metadata");
        native_bytes[mode] = bytes;
        const u64 hot_before = EmuCoreXOracleIopIrNclipCompiles();
        const u64 all_hot_before = EmuCoreXOracleIOPIRInstructions();
        std::array<double, samples> hot_us{};
        for (u32 sample = 0; sample < samples; ++sample)
        {
            const auto hot_start = std::chrono::steady_clock::now();
            const auto hot = IOPCop2ReplayResidentProgram(true, budget, static_cast<u32>(benchmark.size()));
            hot_us[sample] = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - hot_start).count();
            CompareIOP(expected, hot, "NCLIP resident hot replay full-state expectation");
            Check(hot.cycle == cold[mode].cycle, "NCLIP resident hot replay keeps cold cycle accounting");
        }
        Check(EmuCoreXOracleIopIrNclipCompiles() == hot_before &&
            EmuCoreXOracleIOPIRInstructions() == all_hot_before,
            "NCLIP resident replay has no target or aggregate common recompilation");
        std::sort(hot_us.begin(), hot_us.end());
        std::printf("IOPNCLIPBENCH mode=%u commands=%u cold_reset_setup_compile_execute_us=%.3f hot_state_reset_execute_median_us=%.3f native_block_bytes=%llu samples=%u\n",
            mode, commands, cold_us, hot_us[samples / 2], static_cast<unsigned long long>(bytes), samples);
    }
    Check(cold[0].cycle == cold[1].cycle, "NCLIP benchmark retains exact legacy/shared cycles");
    Check(native_bytes[1] <= native_bytes[0], "NCLIP shared native code does not exceed the legacy workload size");
    EmuCoreXOracleSetIOPIR(-1);
}
