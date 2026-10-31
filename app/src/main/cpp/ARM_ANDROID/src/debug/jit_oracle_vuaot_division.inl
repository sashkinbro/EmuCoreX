// SPDX-License-Identifier: GPL-3.0+
void RunVuAotDivisionTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate native E-bit PQ memory"); return; }
    CpuVU0 = &CpuMicroVU0;
    CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve();
    CpuMicroVU1.Reserve();
    mvu_aot::ForceSynchronous(true);
    const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
    for (u32 clamp = 0; clamp < 4; ++clamp)
    for (u32 kind = 0; kind < 3; ++kind)
    for (u32 latency : {0u,15u})
    for (u32 zero = 0; zero < 10; ++zero)
    {
        EmuConfig.Cpu.Recompiler.vu1Overflow = clamp != 0;
        EmuConfig.Cpu.Recompiler.vu1ExtraOverflow = clamp >= 2;
        EmuConfig.Cpu.Recompiler.vu1SignOverflow = clamp >= 3;
        std::array<u32, 128> vf{};
        vf[3] = 0x3f800000;
        for (u32 r = 1; r < 32; ++r)
            for (u32 lane = 0; lane < 4; ++lane)
                vf[r * 4 + lane] = 0x3f800000 + (r * 4 + lane) * 0x1000;
        constexpr u32 inputs[10][2] = {
            {0,0},{0x80000000,0x80000000},{0x3f800000,0x80000000},
            {0x7fc12345,0x3f800000},{0xffa12345,0xbf800000},
            {0x3f800000,0x7fc12345},{0xbf800000,0xff800000},
            {0x7f800000,0x7f800000},{1,0x80000001},{0x7f7fffff,0x00800000}};
        vf[4]=inputs[zero][0]; vf[8]=inputs[zero][1];
        std::vector<u32> code{(0x20u << 25) | 3u, NOP_U, NOP_L, NOP_U,
            NOP_L, NOP_U, NOP_L, NOP_U};
        code.push_back((0x800003bcu + kind) | (1u << 11) | (2u << 16));
        code.push_back(add);
        for (u32 n = 0; n < latency; ++n) code.insert(code.end(), {NOP_L, NOP_U});
        code.insert(code.end(), {NOP_L, NOP_U | (1u << 30), NOP_L, NOP_U});
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        const auto baseline = RunVU(1, true, code, 0x80, 0, 128, false, nullptr, vf.data());
        const u64 ended = mvu_aot::NativeEndCount();
        EmuConfig.Cpu.Recompiler.EnableVUAot = true;
        const auto region = RunVU(1, true, code, 0x80, 0, 128, false, nullptr, vf.data());
        char name[100];
        std::snprintf(name, sizeof(name), "division kind=%u latency=%u clamp=%u zero=%u", kind, latency, clamp, zero);
        Check(mvu_aot::NativeEndCount() > ended, "PQ fixture executes native E-bit tail");
        Compare(baseline, region, name);
        Check(baseline.regs.cycle == region.regs.cycle &&
            baseline.regs.pending_p == region.regs.pending_p &&
            baseline.regs.pending_q == region.regs.pending_q &&
            !std::memcmp(baseline.regs.micro_statusflags, region.regs.micro_statusflags, sizeof(baseline.regs.micro_statusflags)) &&
            !std::memcmp(baseline.regs.micro_macflags, region.regs.micro_macflags, sizeof(baseline.regs.micro_macflags)) &&
            !std::memcmp(baseline.regs.micro_clipflags, region.regs.micro_clipflags, sizeof(baseline.regs.micro_clipflags)),
            "native end preserves PQ runout, cycles and complete flag rings");
    }
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    mvu_aot::ForceSynchronous(false);
}
