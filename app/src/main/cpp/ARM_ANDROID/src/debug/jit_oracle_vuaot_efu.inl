// SPDX-License-Identifier: GPL-3.0+
// VU1 EFU ops through the AOT lowerer versus microVU (AOT off), every clamp mode.
// The EFU result is read back with MFP so P, VF, flags and cycles are all compared.
void RunVuAotEfuTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate native EFU memory"); return; }
    CpuVU0 = &CpuMicroVU0;
    CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve();
    CpuMicroVU1.Reserve();
    mvu_aot::ForceSynchronous(true);
    const u64 endedBefore = mvu_aot::NativeEndCount();
    u32 nonzeroP = 0;
    struct Efu { const char* name; u32 fn, fd; bool efu2; };
    static const Efu ops[] = {
        {"ESADD", 0x3c, 28, true}, {"EATANxy", 0x3c, 29, true}, {"ESQRT", 0x3c, 30, false}, {"ESIN", 0x3c, 31, false},
        {"ERSADD", 0x3d, 28, true}, {"EATANxz", 0x3d, 29, true}, {"ERSQRT", 0x3d, 30, false}, {"EATAN", 0x3d, 31, false},
        {"ELENG", 0x3e, 28, true}, {"ESUM", 0x3e, 29, true}, {"ERCPR", 0x3e, 30, false}, {"EEXP", 0x3e, 31, false},
        {"ERLENG", 0x3f, 28, true}};
    static const u32 inputs[] = {
        0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x40490fdb, 0x3effffff, 0x7f7fffff, 0xff7fffff,
        0x00800000, 0x007fffff, 0x80000001, 0x7f800000, 0xff800000, 0x7fc12345, 0xffa12345, 0x4b800000,
        0x34000000, 0x3f000000, 0xc0a00000, 0x01000000};
    for (u32 clamp = 0; clamp < 4; ++clamp)
    for (const Efu& op : ops)
    for (u32 sel : op.efu2 ? std::vector<u32>{15u, 7u, 1u, 14u} : std::vector<u32>{0u, 1u, 2u, 3u})
    for (u32 pattern = 0; pattern < 12; ++pattern)
    {
        EmuConfig.Cpu.Recompiler.vu1Overflow = clamp != 0;
        EmuConfig.Cpu.Recompiler.vu1ExtraOverflow = clamp >= 2;
        EmuConfig.Cpu.Recompiler.vu1SignOverflow = clamp >= 3;
        std::array<u32, 128> vf{};
        vf[3] = 0x3f800000;
        for (u32 r = 1; r < 32; ++r)
            for (u32 lane = 0; lane < 4; ++lane)
                vf[r * 4 + lane] = 0x3f800000 + (r * 4 + lane) * 0x1000;
        for (u32 lane = 0; lane < 4; ++lane)
            vf[2 * 4 + lane] = inputs[(pattern * 5 + lane * 7 + clamp) % (sizeof(inputs) / sizeof(inputs[0]))];
        const u32 lower = 0x80000000u | (op.fd << 6) | op.fn | (2u << 11) | (sel << 21); // E<op> vf2
        const u32 readP = 0x80000000u | (25u << 6) | 0x3cu | (1u << 16) | (15u << 21);  // MFP vf1, P
        std::vector<u32> code{(0x20u << 25) | 3u, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U};
        code.push_back(lower);
        code.push_back(NOP_U);
        for (u32 n = 0; n < 60; ++n) code.insert(code.end(), {NOP_L, NOP_U});
        code.push_back(readP);
        code.push_back(NOP_U);
        code.insert(code.end(), {NOP_L, NOP_U | (1u << 30), NOP_L, NOP_U});
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        const auto baseline = RunVU(1, true, code, 0x80, 0, 128, false, nullptr, vf.data());
        EmuConfig.Cpu.Recompiler.EnableVUAot = true;
        const auto region = RunVU(1, true, code, 0x80, 0, 128, false, nullptr, vf.data());
        char name[110];
        std::snprintf(name, sizeof(name), "aot efu %s sel=%u clamp=%u pattern=%u", op.name, sel, clamp, pattern);
        Compare(baseline, region, name);
        nonzeroP += baseline.regs.VF[1].UL[0] != 0;
    }
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    Check(mvu_aot::NativeEndCount() > endedBefore, "EFU fixtures execute the native AOT tail");
    Check(nonzeroP > 500, "EFU fixtures produce non-zero P results");
    mvu_aot::ForceSynchronous(false);
}
