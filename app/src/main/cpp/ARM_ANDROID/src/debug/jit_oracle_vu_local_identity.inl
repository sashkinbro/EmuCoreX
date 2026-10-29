// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
extern "C" void EmuCoreXOracleSetVULocalIdentity(bool enabled);
extern "C" u64 EmuCoreXOracleVULocalIdentityEmits();
extern "C" void EmuCoreXOracleSetVULocalNatural(bool enabled);
extern "C" void EmuCoreXOracleVULocalFlagRings(u32 index,u32* words,bool seed);

void RunVULocalIdentityRewriteTests()
{
    cpuinfo_initialize(); EmuConfig=Pcsx2Config();
    EmuConfig.Speedhacks.vuThread=false; EmuConfig.Speedhacks.vuFlagHack=false;
    if (!SysMemory::Allocate()) { Check(false,"allocate local identity oracle memory"); return; }
    CpuVU0=&CpuMicroVU0; CpuVU1=&CpuMicroVU1; CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    const auto saved=EmuConfig;
    const auto codebytes=[](u32 vu) {
        std::vector<JitBlockProfile> blocks;
        if (vu) VU1_JitGetBlockProfiles(blocks); else VU0_JitGetBlockProfiles(blocks);
        u64 total=0; for (const auto& b:blocks) total+=b.host_size; return total;
    };
    constexpr std::array<std::array<u32,4>,3> patterns{{{0,0x80000000,0x3f800000,0xbf800000},
        {1,0x807fffff,0x7f800000,0xff800000},{0x7fc12345,0xffa00001,0x7f7fffff,0xff7fffff}}};
    constexpr std::array<u32,4> acc{0x41200000,0xc1200000,0x41a00000,0xc1a00000};
    u64 old_bytes=0,new_bytes=0;u32 eligible_cases=0;
    for (u32 vu=0;vu<2;++vu)
    for (u32 clamp=0;clamp<4;++clamp)
    for (FPRoundMode round:{FPRoundMode::Nearest,FPRoundMode::ChopZero})
    for (bool fz:{false,true})
    for (bool natural:{false,true})
    for (u32 fs:{0u,1u})
    for (bool aform:{false,true})
    for (u32 fd:{0u,1u,3u})
    for (u32 mask:{8u,4u,2u,1u,7u,15u})
    for (u32 sample=0;sample<patterns.size();++sample)
    {
        if (aform&&fd!=3) continue;
        EmuCoreXOracleSetVUIR(vu,0);EmuCoreXOracleSetVULocalNatural(natural);
        auto& rc=EmuConfig.Cpu.Recompiler;
        if (vu) {rc.vu1Overflow=clamp>=1;rc.vu1ExtraOverflow=clamp>=2;rc.vu1SignOverflow=clamp>=3;}
        else {rc.vu0Overflow=clamp>=1;rc.vu0ExtraOverflow=clamp>=2;rc.vu0SignOverflow=clamp>=3;}
        auto& fpcr=vu?EmuConfig.Cpu.VU1FPCR:EmuConfig.Cpu.VU0FPCR;
        fpcr.SetRoundMode(round);fpcr.SetFlushToZero(fz);EmuConfig.Cpu.FPUFPCR=fpcr;
        const bool one=(mask&(mask-1))==0;
        const bool natural_body=vu&&natural&&one&&mask!=8&&clamp<2;
        const bool eligible=fs!=0&&clamp==1&&fz&&(!one||natural_body)&&!(aform&&vu==1);
        std::array<u32,128> input{};input[3]=0x3f800000;
        for (u32 r=1;r<32;++r) for (u32 lane=0;lane<4;++lane)
            input[r*4+lane]=patterns[(sample+r-1)%patterns.size()][lane];
        const u32 upper=(aform?0x1bfu:0x1bu)|(fs<<11)|((aform?0:fd)<<6)|(mask<<21);
        const u32 cache=0x8000033cu|(fs<<11)|(20u<<16)|(15u<<21);
        std::vector<u32> program{cache,NOP_U,NOP_L,upper};
        for (u32 n=0;n<5;++n) program.insert(program.end(),{NOP_L,NOP_U});
        program.insert(program.end(),{NOP_L,NOP_U|0x40000000u,NOP_L,NOP_U});
        std::array<u32,12> old_rings{},new_rings{};
        EmuCoreXOracleSetVULocalIdentity(false);EmuCoreXOracleVULocalFlagRings(vu,old_rings.data(),true);
        const auto baseline=RunVU(vu,true,program,0x80,0,128,false,nullptr,input.data(),acc.data());
        old_bytes+=codebytes(vu);EmuCoreXOracleVULocalFlagRings(vu,old_rings.data(),false);
        EmuCoreXOracleSetVULocalIdentity(true);EmuCoreXOracleVULocalFlagRings(vu,new_rings.data(),true);
        const u64 before=EmuCoreXOracleVULocalIdentityEmits();
        const auto actual=RunVU(vu,true,program,0x80,0,128,false,nullptr,input.data(),acc.data());
        new_bytes+=codebytes(vu);EmuCoreXOracleVULocalFlagRings(vu,new_rings.data(),false);
        Check(EmuCoreXOracleVULocalIdentityEmits()-before==(eligible?1u:0u),
            "VF0.w identity is emitted only after safe vector numeric clamp under FZ");
        Compare(baseline,actual,"VF0.w identity preserves baseline raw VF VI ACC PQ flags and memory");
        Check(old_rings==new_rings&&baseline.regs.cycle==actual.regs.cycle,
            "VF0.w identity preserves all flag ring instances and exact JIT cycle accounting");
        bool finite_bits=true;
        if (eligible)
        {
            ++eligible_cases;
            for (u32 lane=0;lane<4;++lane)
            {
                const u32 word=input[fs*4+lane],abs=word&0x7fffffffu;
                if (abs>0x7f800000u) continue; // Own numeric-NaN clamp policy stays checked against baseline above.
                const u32 sanitized=abs<0x00800000u?(word&0x80000000u):
                    abs==0x7f800000u?((word&0x80000000u)|0x7f7fffffu):word;
                const u32 expected=(!aform&&fd==0)?input[lane]:(mask&(8u>>lane))?sanitized:
                    (aform?acc[lane]:input[fd*4+lane]);
                finite_bits&=(aform?actual.regs.ACC.UL[lane]:actual.regs.VF[fd].UL[lane])==expected;
            }
        }
        Check(finite_bits,"eligible VF0.w identity matches independent finite Inf and flushed-denormal result bits");
    }
    std::printf("VU_LOCAL_IDENTITY eligible=%u old_bytes=%llu new_bytes=%llu\n",eligible_cases,
        static_cast<unsigned long long>(old_bytes),static_cast<unsigned long long>(new_bytes));
    Check(new_bytes<old_bytes,"VF0.w identity reduces aggregate actual compiled host bytes");
    EmuCoreXOracleSetVULocalIdentity(true);EmuCoreXOracleSetVULocalNatural(true);
    EmuCoreXOracleSetVUIR(0,-1);EmuCoreXOracleSetVUIR(1,-1);EmuConfig=saved;
}
