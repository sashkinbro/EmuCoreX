// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
extern "C" void EmuCoreXOracleSetVUZeroTailClamp(bool enabled);
extern "C" u64 EmuCoreXOracleVUZeroTailClampEmits();

void EEVuZeroTailClampTests()
{
    const auto saved=EmuConfig;
    const auto qmtc=[](u32 r,u32 v){return(0x12u<<26)|(5u<<21)|(r<<16)|(v<<11);};
    const auto qmfc=[](u32 r,u32 v){return(0x12u<<26)|(1u<<21)|(r<<16)|(v<<11);};
    const auto upper=[](u32 op,u32 fs,u32 ft,u32 fd,u32 mask){
        return(0x12u<<26)|((0x10u|mask)<<21)|(ft<<16)|(fs<<11)|(fd<<6)|op;};
    constexpr std::array<std::array<u32,4>,2> patterns{{{0,0x80000000,0x3f800000,0xbf800000},
        {1,0x807fffff,0x7f800000,0xffa00001}}};
    u64 emitted=0;
    for(u32 clamp=0;clamp<4;++clamp)
    for(FPRoundMode round:{FPRoundMode::Nearest,FPRoundMode::ChopZero})
    for(bool ee_fz:{false,true}) for(bool vu_fz:{false,true})
    for(u32 op:{0x28u,0x2cu,0x2au}) for(u32 fd:{0u,7u,8u,3u})
    for(u32 mask:{8u,4u,2u,1u}) for(const auto& pattern:patterns)
    {
        EmuCoreXOracleSetVUIR(0,0);EmuConfig.Speedhacks.vuFlagHack=false;
        auto& rc=EmuConfig.Cpu.Recompiler;
        rc.vu0Overflow=clamp>=1;rc.vu0ExtraOverflow=clamp>=2;rc.vu0SignOverflow=clamp>=3;
        EmuConfig.Cpu.FPUFPCR.SetRoundMode(round);EmuConfig.Cpu.FPUFPCR.SetFlushToZero(ee_fz);
        EmuConfig.Cpu.VU0FPCR.SetRoundMode(round);EmuConfig.Cpu.VU0FPCR.SetFlushToZero(vu_fz);
        const FPControlRegisterBackup host_fpcr(EmuConfig.Cpu.FPUFPCR);
        std::array<GPR_reg,32> gpr{};std::array<u32,128> vf{};vf[3]=0x3f800000;
        for(u32 lane=0;lane<4;++lane){gpr[8].UL[lane]=pattern[lane];gpr[9].UL[lane]=pattern[(lane+1)&3];}
        const std::vector<u32> code{qmtc(8,7),qmtc(9,8),upper(0x2bc,7,8,0,15),
            upper(op,7,8,fd,mask),upper(0x29,0,0,4,15),qmfc(18,fd),qmfc(21,7),qmfc(22,8),qmfc(23,4),
            (0x12u<<26)|(2u<<21)|(19u<<16)|(16u<<11),
            (0x12u<<26)|(2u<<21)|(20u<<16)|(17u<<11)};
        EmuCoreXOracleSetVUZeroTailClamp(false);
        const auto baseline=RunEEProgramWithIrMode(0,code,gpr.data(),nullptr,nullptr,nullptr,false,vf.data());
        EmuCoreXOracleSetVUZeroTailClamp(true);const u64 before=EmuCoreXOracleVUZeroTailClampEmits();
        const auto actual=RunEEProgramWithIrMode(0,code,gpr.data(),nullptr,nullptr,nullptr,false,vf.data());
        const u64 delta=EmuCoreXOracleVUZeroTailClampEmits()-before;emitted+=delta;
        Check(delta==(clamp==2?1u:0u),"EE scalar result widens exactly once only in numeric extra clamp mode under actual EE FPCR");
        CompareEE(baseline,actual,"EE zero-tail clamp preserves dirty VF aliases live ACC consumers and CFC flag observers");
        Check(baseline.cycle==actual.cycle,"EE zero-tail result clamp preserves exact cycle accounting");
    }
    Check(emitted>0,"EE scalar arithmetic actually emits eligible zero-tail clamp path");
    std::printf("EE_VU_ZERO_TAIL emitted=%llu\n",(unsigned long long)emitted);
    EmuCoreXOracleSetVUZeroTailClamp(true);EmuCoreXOracleSetVUIR(0,-1);EmuConfig=saved;
}
