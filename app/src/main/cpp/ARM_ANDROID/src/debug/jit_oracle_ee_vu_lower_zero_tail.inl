// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
extern "C" void EmuCoreXOracleSetVUZeroTailClamp(bool enabled);
extern "C" u64 EmuCoreXOracleVUZeroTailClampEmits();

void EEVuLowerZeroTailClampTests()
{
    const auto saved=EmuConfig;
    const auto qmtc=[](u32 r,u32 v){return(0x12u<<26)|(5u<<21)|(r<<16)|(v<<11);};
    const auto qmfc=[](u32 r,u32 v){return(0x12u<<26)|(1u<<21)|(r<<16)|(v<<11);};
    constexpr std::array<std::array<u32,2>,8> pairs{{{0x3f800000,0x40800000},{0,0x80000000},
        {1,0x807fffff},{0x7f800000,0xff800000},{0x7fc12345,0xffa00001},{0xbf800000,0x3e800000},
        {0x00800000,0x40800000},{0x80800000,0x40800000}}};
    u64 emitted=0;u32 cases=0;
    for(u32 clamp=0;clamp<4;++clamp)for(FPRoundMode round:{FPRoundMode::Nearest,FPRoundMode::ChopZero})
    for(bool ee_fz:{false,true})for(bool vu_fz:{false,true})for(bool rsqrt:{false,true})
    for(u32 lane:{0u,3u})for(u32 sample=0;sample<pairs.size();++sample)
    {
        EmuCoreXOracleSetVUIR(0,0);EmuConfig.Speedhacks.vuFlagHack=false;
        auto& rc=EmuConfig.Cpu.Recompiler;
        rc.vu0Overflow=clamp>=1;rc.vu0ExtraOverflow=clamp>=2;rc.vu0SignOverflow=clamp>=3;
        EmuConfig.Cpu.FPUFPCR.SetRoundMode(round);EmuConfig.Cpu.FPUFPCR.SetFlushToZero(ee_fz);
        EmuConfig.Cpu.VU0FPCR.SetRoundMode(round);EmuConfig.Cpu.VU0FPCR.SetFlushToZero(vu_fz);
        const FPControlRegisterBackup hostFPCR(EmuConfig.Cpu.FPUFPCR);
        std::array<GPR_reg,32> gpr{};std::array<u32,128> vf{};vf[3]=0x3f800000;
        for(u32 l=0;l<4;++l){gpr[8].UL[l]=0x7f800001u+(l<<8);gpr[9].UL[l]=0xff800001u+(l<<8);}
        gpr[8].UL[lane]=pairs[sample][0];gpr[9].UL[lane]=pairs[sample][1];
        const u32 macro=(0x12u<<26)|(0x10u<<21)|(8u<<16)|(7u<<11)|(14u<<6)|
            (rsqrt?0x3eu:0x3cu)|(lane<<21)|(lane<<23);
        const std::vector<u32> code{qmtc(8,7),qmtc(9,8),macro,
            (0x12u<<26)|(0x10u<<21)|(14u<<6)|0x3fu, // WAITQ
            (0x12u<<26)|(0x1fu<<21)|(3u<<6)|0x1cu, // MULq VF0 -> VF3
            (0x12u<<26)|(2u<<21)|(18u<<16)|(REG_Q<<11),
            (0x12u<<26)|(2u<<21)|(19u<<16)|(REG_STATUS_FLAG<<11),qmfc(20,3),qmfc(21,7),qmfc(22,8)};
        EmuCoreXOracleSetVUZeroTailClamp(false);
        const auto baseline=RunEEProgramWithIrMode(0,code,gpr.data(),nullptr,nullptr,nullptr,false,vf.data());
        EmuCoreXOracleSetVUZeroTailClamp(true);const u64 before=EmuCoreXOracleVUZeroTailClampEmits();
        const auto actual=RunEEProgramWithIrMode(0,code,gpr.data(),nullptr,nullptr,nullptr,false,vf.data());
        const u64 delta=EmuCoreXOracleVUZeroTailClampEmits()-before;emitted+=delta;
        Check(delta==(clamp==2?1u:0u),"EE DIV RSQRT widens one result clamp only in numeric extra mode under actual EE FPCR");
        CompareEE(baseline,actual,"EE lower zero-tail clamp preserves raw source VF Q STATUS and dependent macro consumers");
        Check(baseline.cycle==actual.cycle,"EE lower zero-tail clamp preserves exact macro cycle accounting");
        bool independent=true;
        if(sample==0)independent=static_cast<u32>(actual.gpr[18])==(rsqrt?0x3f000000u:0x3e800000u);
        if(sample>=6)independent=static_cast<u32>(actual.gpr[18])==
            ((pairs[sample][0]&0x80000000u)|((ee_fz||!vu_fz)?0u:rsqrt?0x00400000u:0x00200000u));
        Check(independent,"EE finite and tiny DIV RSQRT Q bits preserve explicit VU flush policy combined with actual EE FZ");
        ++cases;
    }
    Check(emitted>0,"EE DIV RSQRT actually emits guarded widened result clamp");
    std::printf("EE_VU_LOWER_ZERO_TAIL cases=%u emitted=%llu\n",cases,(unsigned long long)emitted);
    EmuCoreXOracleSetVUZeroTailClamp(true);EmuCoreXOracleSetVUIR(0,-1);EmuConfig=saved;
}
