// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
extern "C" void EmuCoreXOracleSetVULocalIdentity(bool enabled);
extern "C" u64 EmuCoreXOracleVULocalIdentityEmits();

void EEVuLocalIdentityRewriteTests()
{
    const auto saved=EmuConfig;
    const auto bits=[](float value) {u32 out;std::memcpy(&out,&value,sizeof(out));return out;};
    const auto qmtc=[](u32 r,u32 v) {return (0x12u<<26)|(5u<<21)|(r<<16)|(v<<11);};
    const auto qmfc=[](u32 r,u32 v) {return (0x12u<<26)|(1u<<21)|(r<<16)|(v<<11);};
    const auto upper=[](u32 op,u32 fs,u32 ft,u32 fd=0,u32 mask=15) {
        return (0x12u<<26)|((0x10u|mask)<<21)|(ft<<16)|(fs<<11)|(fd<<6)|op;
    };
    constexpr std::array<std::array<u32,4>,2> patterns{{{0,0x80000000,0x3f800000,0xbf800000},
        {1,0x807fffff,0x7f800000,0xffa00001}}};
    for (bool ee_fz:{false,true})
    for (bool vu_fz:{false,true})
    for (u32 clamp=0;clamp<4;++clamp)
    for (u32 fs:{0u,7u})
    for (bool aform:{false,true})
    for (u32 fd:{0u,7u,3u})
    for (u32 mask:{8u,4u,7u,15u})
    for (const auto& pattern:patterns)
    {
        if(aform&&fd!=3) continue;
        EmuCoreXOracleSetVUIR(0,0);EmuConfig.Speedhacks.vuFlagHack=false;
        auto& rc=EmuConfig.Cpu.Recompiler;
        rc.vu0Overflow=clamp>=1;rc.vu0ExtraOverflow=clamp>=2;rc.vu0SignOverflow=clamp>=3;
        EmuConfig.Cpu.FPUFPCR.SetRoundMode(FPRoundMode::Nearest);EmuConfig.Cpu.FPUFPCR.SetFlushToZero(ee_fz);
        EmuConfig.Cpu.VU0FPCR.SetRoundMode(FPRoundMode::Nearest);EmuConfig.Cpu.VU0FPCR.SetFlushToZero(vu_fz);
        const FPControlRegisterBackup host_fpcr(EmuConfig.Cpu.FPUFPCR);
        const bool eligible=fs!=0&&clamp==1&&ee_fz&&(mask==7||mask==15);
        std::array<GPR_reg,32> gpr{};std::array<u32,128> vf{};vf[3]=0x3f800000;
        std::array<u32,4> initial_acc{};
        for(u32 lane=0;lane<4;++lane)
        {
            gpr[8].UL[lane]=pattern[lane];gpr[12].UL[lane]=bits(static_cast<float>(101+lane));
            initial_acc[lane]=bits(static_cast<float>(101+lane+(lane==3?1:0)));
        }
        const std::vector<u32> code{qmtc(8,7),qmtc(12,11),upper(0x2bc,11,0),
            upper(aform?0x1bfu:0x1bu,fs,0,aform?0:fd,mask),upper(0x29,0,0,4),
            qmfc(18,aform?4:fd),(0x12u<<26)|(2u<<21)|(19u<<16)|(16u<<11),
            (0x12u<<26)|(2u<<21)|(20u<<16)|(17u<<11)};
        EmuCoreXOracleSetVULocalIdentity(false);
        const auto baseline=RunEEProgramWithIrMode(0,code,gpr.data(),nullptr,nullptr,nullptr,false,vf.data());
        EmuCoreXOracleSetVULocalIdentity(true);const u64 before=EmuCoreXOracleVULocalIdentityEmits();
        const auto actual=RunEEProgramWithIrMode(0,code,gpr.data(),nullptr,nullptr,nullptr,false,vf.data());
        Check(EmuCoreXOracleVULocalIdentityEmits()-before==(eligible?1u:0u),
            "EE VF0.w identity follows actual EE FPCR FZ independently of VU0 FPCR");
        CompareEE(baseline,actual,"EE VF0.w identity preserves all baseline architectural registers and flag observers");
        Check(baseline.cycle==actual.cycle,"EE VF0.w identity preserves exact baseline cycle accounting");
        bool exact=true;
        if(eligible) for(u32 lane=0;lane<4;++lane)
        {
            const u32 raw=pattern[lane],abs=raw&0x7fffffffu;
            if(abs>0x7f800000u) continue;
            const u32 value=abs<0x00800000u?(raw&0x80000000u):
                abs==0x7f800000u?((raw&0x80000000u)|0x7f7fffffu):raw;
            const u32 old=aform?initial_acc[lane]:(fd==7?pattern[lane]:0u);
            const u32 expected=(!aform&&fd==0)?vf[lane]:(mask&(8u>>lane))?value:old;
            exact&=(aform?actual.vu0_acc[lane]:actual.vu0_vf[fd*4+lane])==expected;
        }
        Check(exact,"EE eligible VF0.w identity matches independent finite Inf and flushed-denormal bits");
    }
    EmuCoreXOracleSetVULocalIdentity(true);EmuCoreXOracleSetVUIR(0,-1);EmuConfig=saved;
}
