// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
extern "C" void EmuCoreXOracleSetVULocalReadonlyClip(bool enabled);
extern "C" u64 EmuCoreXOracleVULocalReadonlyClipEmits();

void RunVULocalClipRewriteTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate local CLIP oracle memory"); return; }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    const auto saved = EmuConfig;
    const auto move = [](u32 fs, u32 ft) { return 0x8000033cu | (fs << 11) | (ft << 16) | (15u << 21); };
    const auto bytes = [](u32 vu) {
        std::vector<JitBlockProfile> blocks;
        if (vu) VU1_JitGetBlockProfiles(blocks); else VU0_JitGetBlockProfiles(blocks);
        u64 size = 0; for (const auto& block : blocks) size += block.host_size; return size;
    };
    constexpr std::array<u32, 16> edges{0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,
        0x00800000,0x80800000,0x3f800000,0xbf800000,0x7f800000,0xff800000,
        0x7fc12345,0xffa00001,0x7fffffff,0xffffffff};
    constexpr std::array<std::array<u32, 2>, 5> sources{{{1,2},{1,1},{0,2},{1,0},{0,0}}};
    u64 old_bytes = 0, new_bytes = 0;
    for (u32 vu = 0; vu < 2; ++vu)
    for (FPRoundMode round : {FPRoundMode::Nearest, FPRoundMode::ChopZero})
    for (bool fz : {false, true})
    for (const auto& source : sources)
    for (u32 sample = 0; sample < edges.size(); ++sample)
    {
        EmuCoreXOracleSetVUIR(vu, 0);
        auto& fpcr = vu ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR;
        fpcr.SetRoundMode(round); fpcr.SetFlushToZero(fz); EmuConfig.Cpu.FPUFPCR = fpcr;
        std::array<u32, 128> input{};
        for (u32 vf = 1; vf < 32; ++vf)
            for (u32 lane = 0; lane < 4; ++lane) input[4 * vf + lane] = edges[(sample + vf + lane * 5) % edges.size()];
        input[3] = 0x3f800000; input[11] = edges[sample];
        const u32 fs = source[0], ft = source[1];
        const u32 history = (0x765432u ^ (sample * 0x12345u)) & 0xffffffu;
        std::vector<u32> program{move(fs, 20),NOP_U,move(ft, 21),NOP_U,(0x11u << 25) | history,NOP_U};
        for (u32 i = 0; i < 4; ++i) program.insert(program.end(), {NOP_L,NOP_U});
        program.insert(program.end(), {NOP_L,0x1ffu | (fs << 11) | (ft << 16)});
        for (u32 i = 0; i < 4; ++i) program.insert(program.end(), {NOP_L,NOP_U});
        program.insert(program.end(), {(0x1cu << 25) | (5u << 16),NOP_U,
            move(fs,22),NOP_U,move(ft,23),NOP_U,NOP_L,NOP_U | 0x40000000u,NOP_L,NOP_U});
        u32 clip = 0;
        const u32 ft_w = input[4 * ft + 3];
        const s32 threshold = static_cast<s32>((ft_w & 0x7f800000u) ? ft_w & 0x7fffffffu : 0x007fffffu);
        for (u32 lane = 0; lane < 3; ++lane)
        {
            const u32 word = input[4 * fs + lane];
            if (static_cast<s32>(word) > threshold) clip |= 1u << (lane * 2);
            if (static_cast<s32>(word ^ 0x80000000u) > threshold) clip |= 2u << (lane * 2);
        }
        const u32 expected_history = ((history << 6) | clip) & 0xffffffu;
        auto expected_vf = input;
        for (u32 lane = 0; lane < 4; ++lane)
            for (u32 dest : {20u,21u,22u,23u}) expected_vf[dest * 4 + lane] = input[4 * ((dest & 1) ? ft : fs) + lane];
        EmuCoreXOracleSetVULocalReadonlyClip(false);
        const auto reference = RunVU(vu,true,program,0x80,0,256,false,nullptr,input.data());
        old_bytes += bytes(vu);
        EmuCoreXOracleSetVULocalReadonlyClip(true);
        const u64 before = EmuCoreXOracleVULocalReadonlyClipEmits();
        const auto actual = RunVU(vu,true,program,0x80,0,256,false,nullptr,input.data());
        new_bytes += bytes(vu);
        Check(EmuCoreXOracleVULocalReadonlyClipEmits() > before,
            "legacy CLIP emits the readonly cached-source route");
        Check(actual.regs.VI[REG_CLIP_FLAG].UL == expected_history &&
            actual.regs.VI[5].UL == (expected_history & 0xfffu) &&
            std::memcmp(actual.regs.VF,expected_vf.data(),sizeof(expected_vf)) == 0,
            "readonly CLIP matches independent raw history and preserves every cached source word");
        Compare(reference,actual,"readonly CLIP preserves baseline VF VI ACC PQ flags and memory");
        Check(reference.regs.cycle == actual.regs.cycle,"readonly CLIP preserves exact baseline JIT cycle accounting");
        Compare(RunVU(vu,false,program,0x80,0,256,false,nullptr,input.data()),actual,
            "readonly CLIP matches interpreter raw thresholds aliases and VF0");
    }
    std::printf("VU_LOCAL_CLIP old_bytes=%llu new_bytes=%llu\n",
        static_cast<unsigned long long>(old_bytes),static_cast<unsigned long long>(new_bytes));
    Check(new_bytes < old_bytes,"readonly CLIP reduces aggregate actual compiled host bytes");
    EmuCoreXOracleSetVULocalReadonlyClip(true);
    EmuCoreXOracleSetVUIR(0,-1); EmuCoreXOracleSetVUIR(1,-1); EmuConfig = saved;
}
