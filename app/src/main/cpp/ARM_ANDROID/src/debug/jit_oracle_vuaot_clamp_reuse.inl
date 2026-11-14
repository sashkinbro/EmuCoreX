// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Real lowerer/code-shape and native execution regression fixture. Requires
// VuAotLower.h, VuAotIR.h and VuAotBackground.h included at translation-unit scope.
namespace
{
std::vector<u32> MakeAotClampReuseProgram(bool mula, u32 bc, bool mixed)
{
    std::vector<u32> code;
    for (u32 i = 0; i < 3; ++i)
    {
        const u32 operation = (15u << 21) | (2u << 16) | (1u << 11) |
            (mula ? (0x1bcu | bc) : (((3u + i) << 6) | 0x08u | bc));
        code.insert(code.end(), {NOP_L, operation});
        if (mula)
        {
            // Consume each ACC result in a distinct observable VF. Three
            // overwritten ACC definitions alone could be removed by DCE.
            const u32 observe = (15u << 21) | ((3u + i) << 6) | 0x29u;
            code.insert(code.end(), {NOP_L, observe});
        }
    }
    if (mixed)
    {
        // A genuine numeric clamp of the same invariant VF must still be
        // counted; zeroing every clamped-live-in count is not a valid fix.
        const u32 sub = (15u << 21) | (2u << 16) | (1u << 11) | (6u << 6) | 0x2cu;
        code.insert(code.end(), {NOP_L, sub});
    }
    code.insert(code.end(), {NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U});
    return code;
}

void CheckAotClampReuseEmission(bool mula, u32 bc, bool mixed)
{
    alignas(16) std::array<u32, 4096> image{};
    for (size_t i = 0; i < image.size(); i += 2)
    {
        image[i] = NOP_L;
        image[i + 1] = NOP_U;
    }
    const auto code = MakeAotClampReuseProgram(mula, bc, mixed);
    std::copy(code.begin(), code.end(), image.begin() + 0x80 / 4);
    vuaot::PipeConfig config;
    config.vuIndex = 1;
    config.flagHack = false;
    auto block = vuaot::PipeAnalyzer(reinterpret_cast<const u8*>(image.data()), config).Analyze(0x80, {});
    vuaot::ir::BuildOptions options;
    options.vuIndex = 1;
    options.clampIImmediate = true;
    options.remapAtLeave = true;
    options.regionEnds = true;
    options.dropSticky = false;
    auto function = vuaot::ir::BuildNodeLocal(block, options);
    vuaot::LowerEnv env;
    env.clampOverflow = true;
    env.clampExtra = false;
    env.clampSign = false;
    env.softwareFlush = false;
    env.vuMemPinned = true;
    env.schedule = true;
    u32 numericUses[33]{};
    auto lowering = vuaot::NodeLowering::Check(function, env, numericUses);
    Check(lowering != nullptr, "repeated exact-clamp node remains eligible for real AOT lowering");
    if (!lowering)
        return;
    Check(numericUses[1] == (mixed ? 1u : 0u),
        "numeric home counts describe consumed numeric clamps, excluding bit-sign-only consumers");
    vuaot::HomeMap homes;
    homes.Seal();
    vuaot::bg::CodeSink sink;
    sink.Begin();
    const auto lowered = lowering->Emit(homes);
    const u32 bytes = sink.End();
    Check(lowered.ok && lowered.bytes == bytes && sink.failed.empty(),
        "exact-clamp code-shape test emits the actual lowerer, not a model");
    u32 signedMin = 0, unsignedMin = 0;
    for (u32 offset = 0; offset < bytes; offset += 4)
    {
        u32 instruction;
        std::memcpy(&instruction, sink.Base() + offset, 4);
        signedMin += (instruction & 0xffe0fc00u) == 0x4ea06c00u;
        unsignedMin += (instruction & 0xffe0fc00u) == 0x6ea06c00u;
    }
    // MULA repeats one immutable Fs transform. MADD repeats Fs/Ft/ACC
    // transforms but keeps the three distinct result transforms unchanged.
    // Normal clamp mode is x86 mVUclamp1 (numeric FMINNM/FMAXNM), so no bitwise sign transform (SMIN/UMIN pair) is
    // emitted any more (that is the sign-preserving mode 3 clamp); MADD now clamps only Fs (x86 cFs).
    const u32 expectedPairs = 0u;
    Check(signedMin == expectedPairs && unsignedMin == expectedPairs,
        "Normal clamp mode emits numeric clamps and no bitwise sign transforms in actual A64 code");
    std::printf("AOT_CLAMP_REUSE mula=%u bc=%u mixed=%u numeric_vf1=%u smin=%u umin=%u bytes=%u\n",
        mula, bc, mixed, numericUses[1], signedMin, unsignedMin, bytes);
}

using ClampNodeKernel = void (*)(VURegs*, void*, u64, u64, u64*);

void EmitClampNodeFrame(bool begin)
{
    if (begin)
    {
        oakAsm->SUB(oak::util::SP, oak::util::SP, 256);
        for (u32 reg = 19; reg <= 30; ++reg)
            oakAsm->STR(oak::XReg(reg), oak::util::SP, oak::POffset<15, 3>((reg - 19) * 8));
        for (u32 reg = 8; reg < 16; ++reg)
            oakAsm->STR(oak::QReg(reg), oak::util::SP, oak::POffset<16, 4>(96 + (reg - 8) * 16));
        oakAsm->MRS(oak::util::X5, oak::SystemReg::FPCR);
        oakAsm->STR(oak::util::X5, oak::util::SP, oak::POffset<15, 3>(224));
        oakAsm->MRS(oak::util::X5, oak::SystemReg::FPSR);
        oakAsm->STR(oak::util::X5, oak::util::SP, oak::POffset<15, 3>(232));
        oakAsm->STR(oak::util::X4, oak::util::SP, oak::POffset<15, 3>(240));
        oakAsm->MOV(oak::util::X19, oak::util::X0);
        oakAsm->MOV(oak::util::X24, oak::util::X1);
        oakMoveAddressToReg(oak::util::X25, &vuaot::mVUglob);
        oakLoad64(oak::util::X29, {oak::util::X19, offsetof(VURegs, Mem)});
        oakAsm->MSR(oak::SystemReg::FPCR, oak::util::X2);
        oakAsm->MSR(oak::SystemReg::FPSR, oak::util::X3);
        oakLoad128(oak::QReg(25), {oak::util::X25, offsetof(vuaot::mVU_Globals, maxvals)});
        oakLoad128(oak::QReg(26), {oak::util::X25, offsetof(vuaot::mVU_Globals, minvals)});
        for (u32 reg = 20; reg <= 23; ++reg) oakAsm->MOV(oak::WReg(reg), 0);
    }
    else
    {
        oakAsm->MRS(oak::util::X0, oak::SystemReg::FPSR);
        oakAsm->LDR(oak::util::X1, oak::util::SP, oak::POffset<15, 3>(240));
        oakStore64(oak::util::X0, {oak::util::X1, 0});
        oakAsm->LDR(oak::util::X0, oak::util::SP, oak::POffset<15, 3>(224));
        oakAsm->MSR(oak::SystemReg::FPCR, oak::util::X0);
        oakAsm->LDR(oak::util::X0, oak::util::SP, oak::POffset<15, 3>(232));
        oakAsm->MSR(oak::SystemReg::FPSR, oak::util::X0);
        for (u32 reg = 8; reg < 16; ++reg)
            oakAsm->LDR(oak::QReg(reg), oak::util::SP, oak::POffset<16, 4>(96 + (reg - 8) * 16));
        for (u32 reg = 19; reg <= 30; ++reg)
            oakAsm->LDR(oak::XReg(reg), oak::util::SP, oak::POffset<15, 3>((reg - 19) * 8));
        oakAsm->ADD(oak::util::SP, oak::util::SP, 256);
        oakAsm->RET();
    }
}

ClampNodeKernel MakeClampNodeKernel(const std::vector<u32>& program, bool flush, u32 pool, u32 offset)
{
    alignas(16) std::array<u32, 4096> image{};
    for (size_t i = 0; i < image.size(); i += 2) { image[i] = NOP_L; image[i + 1] = NOP_U; }
    std::copy(program.begin(), program.end(), image.begin() + 0x80 / 4);
    vuaot::PipeConfig config; config.vuIndex = 1; config.flagHack = false;
    auto block = vuaot::PipeAnalyzer(reinterpret_cast<const u8*>(image.data()), config).Analyze(0x80, {});
    vuaot::ir::BuildOptions options;
    options.vuIndex = 1; options.clampIImmediate = true; options.remapAtLeave = true;
    options.regionEnds = true; options.dropSticky = false;
    auto function = vuaot::ir::BuildNodeLocal(block, options);
    // This callable tests arithmetic alone. Ordinary RunVU cases below check
    // every architectural flag ring, cycle and pending value independently.
    for (auto& inst : function.insts)
        if (inst.op == vuaot::ir::Op::Leave)
            for (u32 var = 0; var < inst.args.size(); ++var)
                if (var >= 132) inst.args[var] = 0;
    vuaot::LowerEnv env;
    env.clampOverflow = true; env.clampExtra = false; env.clampSign = false;
    env.softwareFlush = flush; env.vuMemPinned = true; env.schedule = true;
    env.poolLimitV = pool; env.poolLimitG = pool ? 8 : 0;
    u32 numericUses[33]{};
    auto lowering = vuaot::NodeLowering::Check(function, env, numericUses);
    Check(lowering != nullptr, "native clamp node is accepted with requested register pressure");
    if (!lowering) return nullptr;
    vuaot::HomeMap homes; homes.Seal();
    vuaot::bg::CodeSink sink; sink.Begin();
    const auto emitted = lowering->Emit(homes);
    const u32 bytes = sink.End();
    Check(emitted.ok && sink.failed.empty() && sink.fixups.empty(),
        "native arithmetic body emits without hidden relocation or fallback");
    if (!emitted.ok || !sink.fixups.empty()) return nullptr;
    oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000 + offset, 0x10000);
    u8* entry = oakStartBlock();
    EmitClampNodeFrame(true);
    for (u32 at = 0; at < bytes; at += 4)
    {
        u32 instruction; std::memcpy(&instruction, sink.Base() + at, 4);
        oakAsm->dw(instruction);
    }
    EmitClampNodeFrame(false); oakEndBlock();
    return reinterpret_cast<ClampNodeKernel>(entry);
}

ClampNodeKernel MakeClampReferenceKernel(bool mula, u32 bc, bool mixed, bool flush, u32 offset)
{
    oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000 + offset, 0x10000);
    u8* entry = oakStartBlock(); EmitClampNodeFrame(true);
    const auto load = [](u32 host, u32 vf) {
        oakLoad128(oak::QReg(host), {oak::util::X19, static_cast<s64>(offsetof(VURegs, VF) + vf * 16)});
    };
    const auto sign = [&](u32 host, bool denormal, bool clamp = true) {
        const auto q = oak::QReg(host);
        if (clamp) {
        // Normal clamp mode (c1) is x86 mVUclamp1: numeric clamp, any NaN becomes +max (since 765e433ae1;
        // the old reference used the bitwise sign-preserving clamp, which now belongs to clamp mode 3 only).
        oakAsm->FMINNM(q.S4(), q.S4(), oak::QReg(25).S4());
        oakAsm->FMINNM(q.S4(), q.S4(), oak::QReg(25).S4());
        oakAsm->FMAXNM(q.S4(), q.S4(), oak::QReg(26).S4());
        }
        if (denormal)
        {
            const auto e = oak::QReg(7);
            oakAsm->SHL(e.S4(), q.S4(), 1); oakAsm->USHR(e.S4(), e.S4(), 24);
            oakAsm->CMEQ(e.S4(), e.S4(), 0); oakAsm->USHR(e.S4(), e.S4(), 1);
            oakAsm->BIC(q.B16(), q.B16(), e.B16());
        }
    };
    const auto numeric = [](u32 host) {
        const auto q = oak::QReg(host);
        oakAsm->FMINNM(q.S4(), q.S4(), oak::QReg(25).S4());
        oakAsm->FMINNM(q.S4(), q.S4(), oak::QReg(25).S4()); // second FMINNM: sNaN -> +max like x86 MINPS
        oakAsm->FMAXNM(q.S4(), q.S4(), oak::QReg(26).S4());
    };
    // Deliberately re-read and transform each immutable input on every step:
    // this is the previous arithmetic sequence, independent of cache metadata.
    for (u32 iteration = 0; iteration < 3; ++iteration)
    {
        load(0, 1); load(1, 2); sign(0, false); // no software denormal flush at Normal mode: x86 relies on MXCSR.DAZ
        if (mula)
        {
            if (bc == 3)
            {
                oakAsm->DUP(oak::QReg(1).S4(), oak::QReg(1).Selem()[bc]);
                sign(1, false); // x86 MULAw (full vector field): cFs | cFt
                oakAsm->FMUL(oak::QReg(0).S4(), oak::QReg(0).S4(), oak::QReg(1).S4());
            }
            else oakAsm->FMUL(oak::QReg(0).S4(), oak::QReg(0).S4(), oak::QReg(1).Selem()[bc]);
            oakStore128(oak::QReg(0), {oak::util::X19, offsetof(VURegs, ACC)});
            load(1, 0);
            oakAsm->FMUL(oak::QReg(1).S4(), oak::QReg(1).S4(), oak::QReg(1).S4());
            oakAsm->FADD(oak::QReg(0).S4(), oak::QReg(0).S4(), oak::QReg(1).S4());
        }
        else
        {
            // x86 MADDx/MADDw (VU1): cFs only; Ft, ACC and the result are not clamped at Normal mode.
            oakAsm->DUP(oak::QReg(1).S4(), oak::QReg(1).Selem()[bc]);
            oakLoad128(oak::QReg(2), {oak::util::X19, offsetof(VURegs, ACC)});
            oakAsm->FMUL(oak::QReg(0).S4(), oak::QReg(0).S4(), oak::QReg(1).S4());
            oakAsm->FADD(oak::QReg(0).S4(), oak::QReg(2).S4(), oak::QReg(0).S4());
        }
        oakStore128(oak::QReg(0), {oak::util::X19, static_cast<s64>(offsetof(VURegs, VF) + (3 + iteration) * 16)});
    }
    if (mixed)
    {
        load(0, 1); load(1, 2); numeric(0); numeric(1);
        oakAsm->FSUB(oak::QReg(0).S4(), oak::QReg(0).S4(), oak::QReg(1).S4());
        oakStore128(oak::QReg(0), {oak::util::X19, offsetof(VURegs, VF) + 6 * 16});
    }
    EmitClampNodeFrame(false); oakEndBlock();
    return reinterpret_cast<ClampNodeKernel>(entry);
}

void CheckClampNativeFpAndPressure()
{
    constexpr std::array<u32, 16> edges{0, 0x80000000, 1, 0x807fffff, 0x00800000, 0x80800000,
        0x3f801001, 0xbf803001, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000,
        0x7fc12345, 0xffa00001, 0x7f800001, 0xffffffff};
    for (bool mula : {true, false}) for (u32 bc : {0u, 3u}) for (bool mixed : {false, true})
    for (bool flush : {false, true})
    {
        const auto program = MakeAotClampReuseProgram(mula, bc, mixed);
        const auto reference = MakeClampReferenceKernel(mula, bc, mixed, flush, 0);
        const auto actual = MakeClampNodeKernel(program, flush, 0, 0x10000);
        const auto pressured = MakeClampNodeKernel(program, flush, 16, 0x20000);
        if (!actual || !pressured) continue;
        bool exact = true, pressureExact = true;
        u32 cases = 0;
        for (u64 rounding = 0; rounding < 4; ++rounding) for (bool fz : {false, true})
        for (bool dn : {false, true}) for (u64 seed : {0ull, 0x0800009full})
        for (u32 pattern = 0; pattern < edges.size(); ++pattern)
        {
            VURegs initial; std::memset(static_cast<void*>(&initial), 0, sizeof(initial)); initial.VF[0].UL[3] = 0x3f800000;
            initial.Mem = VU1.Mem;
            for (u32 reg = 1; reg < 32; ++reg) for (u32 lane = 0; lane < 4; ++lane)
                initial.VF[reg].UL[lane] = edges[(pattern + reg * 3 + lane) % edges.size()];
            for (u32 lane = 0; lane < 4; ++lane) initial.ACC.UL[lane] = edges[(pattern + lane * 3) % edges.size()];
            VURegs expected = initial, observed = initial, stressed = initial;
            alignas(16) std::array<u8, 4096> flags{};
            const u64 fpcr = (rounding << 22) | (u64(fz) << 24) | (u64(dn) << 25);
            u64 expectedFpsr = 0, actualFpsr = 0, pressureFpsr = 0;
            reference(&expected, flags.data(), fpcr, seed, &expectedFpsr);
            actual(&observed, flags.data(), fpcr, seed, &actualFpsr);
            pressured(&stressed, flags.data(), fpcr, seed, &pressureFpsr);
            exact &= !std::memcmp(expected.VF, observed.VF, sizeof(expected.VF)) &&
                !std::memcmp(&expected.ACC, &observed.ACC, sizeof(expected.ACC)) && expectedFpsr == actualFpsr;
            pressureExact &= !std::memcmp(expected.VF, stressed.VF, sizeof(expected.VF)) &&
                !std::memcmp(&expected.ACC, &stressed.ACC, sizeof(expected.ACC)) && expectedFpsr == pressureFpsr;
            ++cases;
        }
        Check(exact, "actual emitted node preserves independent pre-reuse raw arithmetic and seeded FPSR in all FP modes");
        Check(pressureExact, "restricted actual allocator preserves the same raw outputs and FPSR");
        std::printf("AOT_CLAMP_NATIVE mula=%u bc=%u mixed=%u flush=%u cases=%u\n", mula, bc, mixed, flush, cases);
    }
}

void CheckClampSourceShapeAndHelper()
{
    const auto upper = [](u32 op, u32 fd, u32 mask = 15) {
        return (mask << 21) | (2u << 16) | (1u << 11) | (fd << 6) | op;
    };
    const auto end = [](std::vector<u32>& code) {
        code.insert(code.end(), {NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U});
    };
    std::vector<std::vector<u32>> programs;
    for (u32 mask : {15u, 8u})
    {
        // Two distinct broadcast keys, then a full or partial source write.
        // Partial writes assemble old/new SSA lanes and must use the fallback.
        std::vector<u32> code{NOP_L, upper(0x08, 3), NOP_L, upper(0x0b, 4),
            NOP_L, upper(0x28, 1, mask), NOP_L, upper(0x08, 5), NOP_L, upper(0x0b, 6)};
        end(code); programs.push_back(std::move(code));
    }
    // One immutable source needs both Sign (MULA) and SignFlush (MADD)
    // in the same node when DAZ is disabled. Subnormal patterns distinguish them.
    std::vector<u32> mixed{NOP_L, upper(0x1bc, 0), NOP_L, upper(0x29, 3),
        NOP_L, upper(0x08, 4), NOP_L, upper(0x0b, 5)};
    end(mixed); programs.push_back(std::move(mixed));
    const u32 load1 = (15u << 21) | (1u << 16) | (8u << 11);
    const u32 store2 = (1u << 25) | (15u << 21) | (8u << 16) | (2u << 11);
    std::vector<u32> alias{load1, NOP_U, NOP_L, upper(0x08, 3), store2, NOP_U,
        load1, NOP_U, NOP_L, upper(0x08, 4), NOP_L, upper(0x0b, 5)};
    end(alias); programs.push_back(std::move(alias));
    // Force a genuine owned-copy spill/reload through the production helper.
    // MADD -> SQ -> Kick -> LQ -> MADD is ordered by memory SSA; the scheduler
    // cannot move both users of immutable VF1/ACC to one side of the call.
    const u32 store3 = (1u << 25) | (15u << 21) | (8u << 16) | (3u << 11);
    const u32 load2 = (15u << 21) | (2u << 16) | (8u << 11);
    std::vector<u32> helper{NOP_L, upper(0x08, 3), store3, NOP_U,
        0x800006fcu | (1u << 11), NOP_U,
        NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U,
        load2, NOP_U, NOP_L, upper(0x08, 4), NOP_L, upper(0x0b, 5)};
    end(helper); programs.push_back(std::move(helper));
    constexpr std::array<std::array<u32, 4>, 4> patterns{{
        {0x3f801001, 0xbf803001, 0x40000001, 0x3eaaaaab},
        {0, 0x80000000, 1, 0x807fffff},
        {0x7f800000, 0xff800000, 0x7fc12345, 0xffa12345},
        {0x7f7fffff, 0xff7fffff, 0x00800000, 0x80800000}}};
    const auto savedFpcr = EmuConfig.Cpu.VU1FPCR;
    for (u32 fixture = 0; fixture < programs.size(); ++fixture)
    for (bool daz : {false, true}) for (u32 pattern = 0; pattern < patterns.size(); ++pattern)
    {
        EmuConfig.Cpu.VU1FPCR.SetDenormalsAreZero(daz);
        std::array<u32, 128> vf{}; vf[3] = 0x3f800000;
        for (u32 reg = 1; reg < 32; ++reg) for (u32 lane = 0; lane < 4; ++lane)
            vf[reg * 4 + lane] = patterns[(pattern + reg - 1) % patterns.size()][lane];
        std::vector<u8> memory(0x4000);
        std::memcpy(memory.data() + 128, patterns[pattern].data(), 16);
        Gif_Tag::HW_Gif_Tag tag{}; tag.NLOOP = 1; tag.NREG = 1;
        std::memcpy(memory.data() + 16, &tag, sizeof(tag));
        tag.EOP = 1; std::memcpy(memory.data() + 48, &tag, sizeof(tag));
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        const auto baseline = RunVU(1, true, programs[fixture], 0x80, 0, 256, false,
            &memory, vf.data(), patterns[(pattern + 1) % patterns.size()].data());
        EmuConfig.Cpu.Recompiler.EnableVUAot = true;
        const auto entries = mvu_aot::EntryCount();
        const auto actual = RunVU(1, true, programs[fixture], 0x80, 0, 256, false,
            &memory, vf.data(), patterns[(pattern + 1) % patterns.size()].data());
        Check(mvu_aot::EntryCount() > entries, "source shape and helper fixture really enters AOT");
        Compare(baseline, actual, "typed cache preserves source writes broadcast shapes memory alias and helper GIF output");
        Check(baseline.regs.cycle == actual.regs.cycle &&
            baseline.regs.pending_p == actual.regs.pending_p && baseline.regs.pending_q == actual.regs.pending_q &&
            !std::memcmp(baseline.regs.micro_statusflags, actual.regs.micro_statusflags, sizeof(baseline.regs.micro_statusflags)) &&
            !std::memcmp(baseline.regs.micro_macflags, actual.regs.micro_macflags, sizeof(baseline.regs.micro_macflags)) &&
            !std::memcmp(baseline.regs.micro_clipflags, actual.regs.micro_clipflags, sizeof(baseline.regs.micro_clipflags)),
            "source shape and helper fixture preserves exact cycles pending values and all flag rings");
        if (fixture == 4)
            Check(actual.gif.size() == 64 && !actual.gifOverflowed,
                "live typed copies survive real XGKICK helper spill reload and complete packet submission");
        std::printf("AOT_CLAMP_LIFETIME fixture=%u daz=%u pattern=%u\n", fixture, daz, pattern);
    }
    EmuConfig.Cpu.VU1FPCR = savedFpcr;
}
}

void RunVuAotClampReuseTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate exact-clamp reuse oracle memory");
        return;
    }
    CpuVU0 = &CpuMicroVU0;
    CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve();
    CpuMicroVU1.Reserve();
    CheckClampNativeFpAndPressure();
    for (bool mula : {true, false})
        for (u32 bc : {0u, 3u})
            for (bool mixed : {false, true})
                CheckAotClampReuseEmission(mula, bc, mixed);

    constexpr std::array<std::array<u32, 4>, 4> patterns{{
        {0x3f801001, 0xbf803001, 0x40000001, 0x3eaaaaab},
        {0, 0x80000000, 1, 0x807fffff},
        {0x7f800000, 0xff800000, 0x7fc12345, 0xffa12345},
        {0x7f7fffff, 0xff7fffff, 0x00800000, 0x80800000}
    }};
    mvu_aot::ForceSynchronous(true);
    for (bool mula : {true, false})
        for (u32 bc : {0u, 3u})
            for (bool mixed : {false, true})
                for (u32 pattern = 0; pattern < patterns.size(); ++pattern)
                {
                    const auto code = MakeAotClampReuseProgram(mula, bc, mixed);
                    std::array<u32, 128> vf{};
                    vf[3] = 0x3f800000;
                    for (u32 reg = 1; reg < 32; ++reg)
                        for (u32 lane = 0; lane < 4; ++lane)
                            vf[reg * 4 + lane] = patterns[(pattern + reg - 1) % patterns.size()][lane];
                    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
                    const auto baseline = RunVU(1, true, code, 0x80, 0, 256, false, nullptr, vf.data());
                    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
                    const auto entered = mvu_aot::EntryCount();
                    const auto actual = RunVU(1, true, code, 0x80, 0, 256, false, nullptr, vf.data());
                    Check(mvu_aot::EntryCount() > entered,
                        "exact-clamp raw fixture executes generated AOT code");
                    Compare(baseline, actual, "exact-clamp reuse preserves raw baseline architecture and memory");
                    Check(baseline.regs.cycle == actual.regs.cycle &&
                        baseline.regs.pending_p == actual.regs.pending_p &&
                        baseline.regs.pending_q == actual.regs.pending_q &&
                        !std::memcmp(baseline.regs.micro_statusflags, actual.regs.micro_statusflags, sizeof(baseline.regs.micro_statusflags)) &&
                        !std::memcmp(baseline.regs.micro_macflags, actual.regs.micro_macflags, sizeof(baseline.regs.micro_macflags)) &&
                        !std::memcmp(baseline.regs.micro_clipflags, actual.regs.micro_clipflags, sizeof(baseline.regs.micro_clipflags)),
                        "exact-clamp reuse preserves exact cycles, pending PQ and flag rings");
                }
    CheckClampSourceShapeAndHelper();
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    mvu_aot::ForceSynchronous(false);
}
