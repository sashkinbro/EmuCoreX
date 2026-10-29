// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
extern "C" void EmuCoreXOracleSetVULocalReadonlyClip(bool enabled);
extern "C" u64 EmuCoreXOracleVULocalReadonlyClipEmits();
void EEVuLocalClipDelayTests();

void EEVuLocalClipRewriteTests()
{
    const auto saved = EmuConfig;
    const auto qmtc2 = [](u32 rt,u32 vf) { return (0x12u << 26) | (5u << 21) | (rt << 16) | (vf << 11); };
    const auto qmfc2 = [](u32 rt,u32 vf) { return (0x12u << 26) | (1u << 21) | (rt << 16) | (vf << 11); };
    const auto cfc2 = [](u32 rt,u32 vi) { return (0x12u << 26) | (2u << 21) | (rt << 16) | (vi << 11); };
    const auto ctc2 = [](u32 rt,u32 vi) { return (0x12u << 26) | (6u << 21) | (rt << 16) | (vi << 11); };
    constexpr std::array<u32,8> edges{0,0x80000000,1,0x807fffff,0x3f800000,0xff800000,0x7fc12345,0xffa00001};
    constexpr std::array<std::array<u32,2>,5> sources{{{7,9},{7,7},{0,9},{7,0},{0,0}}};
    for (FPRoundMode round : {FPRoundMode::Nearest,FPRoundMode::ChopZero})
    for (bool fz : {false,true})
    for (bool flag_hack : {false,true})
    for (const auto& source : sources)
    for (u32 sample=0;sample<edges.size();++sample)
    {
        EmuCoreXOracleSetVUIR(0,0);
        EmuConfig.Speedhacks.vuFlagHack=flag_hack;
        EmuConfig.Cpu.FPUFPCR.SetRoundMode(round); EmuConfig.Cpu.FPUFPCR.SetFlushToZero(fz);
        EmuConfig.Cpu.VU0FPCR=EmuConfig.Cpu.FPUFPCR;
        const FPControlRegisterBackup host_fpcr(EmuConfig.Cpu.FPUFPCR);
        std::array<GPR_reg,32> gpr{};
        std::array<u32,128> input{}; input[3]=0x3f800000;
        for (u32 lane=0;lane<4;++lane)
        {
            gpr[8].UL[lane]=edges[(sample+lane*2)%edges.size()];
            gpr[10].UL[lane]=edges[(sample+lane*3+1)%edges.size()];
        }
        gpr[10].UL[3]=edges[sample];
        const u32 history=(0x6a5432u^(sample*0x12345u))&0xffffffu; gpr[12].UL[0]=history;
        const u32 fs=source[0],ft=source[1];
        const u32 clip=(0x12u<<26)|(0x1fu<<21)|(ft<<16)|(fs<<11)|0x1ffu;
        const std::vector<u32> code{ctc2(12,18),qmtc2(8,7),qmtc2(10,9),clip,cfc2(18,18),
            clip,qmfc2(20,fs),qmfc2(21,ft),cfc2(19,18)};
        const std::array<u32,4> vf0{0,0,0,0x3f800000};
        const auto word=[&](u32 vf,u32 lane) { return vf==0?vf0[lane]:gpr[vf==7?8:10].UL[lane]; };
        const u32 ftw=word(ft,3);
        const s32 threshold=static_cast<s32>((ftw&0x7f800000u)?ftw&0x7fffffffu:0x007fffffu);
        u32 mask=0;
        for (u32 lane=0;lane<3;++lane)
        {
            if (static_cast<s32>(word(fs,lane))>threshold) mask|=1u<<(lane*2);
            if (static_cast<s32>(word(fs,lane)^0x80000000u)>threshold) mask|=2u<<(lane*2);
        }
        const u32 first=((history<<6)|mask)&0xffffffu,final=((first<<6)|mask)&0xffffffu;
        EmuCoreXOracleSetVULocalReadonlyClip(false);
        const auto baseline=RunEEProgramWithIrMode(0,code,gpr.data(),nullptr,nullptr,nullptr,false,input.data());
        EmuCoreXOracleSetVULocalReadonlyClip(true);
        const u64 before=EmuCoreXOracleVULocalReadonlyClipEmits();
        const auto actual=RunEEProgramWithIrMode(0,code,gpr.data(),nullptr,nullptr,nullptr,false,input.data());
        const auto interp=RunEEProgram(false,code,false,nullptr,nullptr,gpr.data(),nullptr,nullptr,input.data());
        Check(EmuCoreXOracleVULocalReadonlyClipEmits()==before+2,"EE CLIP compiles both readonly cached-source macro operations");
        bool exact=actual.gpr[18]==first&&actual.gpr[19]==final&&actual.vu0_vi[REG_CLIP_FLAG]==final;
        for (u32 lane=0;lane<4;++lane)
        {
            const auto lane_bits=[&](u32 r) { return lane<2?static_cast<u32>(actual.gpr[r]>>(lane*32)):
                static_cast<u32>(actual.gpr_upper[r]>>((lane-2)*32)); };
            exact&=lane_bits(20)==word(fs,lane)&&lane_bits(21)==word(ft,lane)&&
                actual.vu0_vf[7*4+lane]==gpr[8].UL[lane]&&actual.vu0_vf[9*4+lane]==gpr[10].UL[lane];
        }
        Check(exact,"EE readonly CLIP matches independent history and preserves dirty cached raw sources and QMFC2 outputs");
        CompareEE(baseline,actual,"EE readonly CLIP preserves all baseline architectural registers");
        CompareEE(interp,actual,"EE readonly CLIP preserves interpreter flags aliases VF0 and raw cached inputs");
        Check(baseline.cycle==actual.cycle,"EE readonly CLIP preserves exact baseline cycle accounting");
    }
    EEVuLocalClipDelayTests();
    EmuCoreXOracleSetVULocalReadonlyClip(true); EmuCoreXOracleSetVUIR(0,-1); EmuConfig=saved;
}


void EEVuLocalClipDelayTests()
{
    const auto saved=EmuConfig;
    EmuConfig.Speedhacks.vuFlagHack=false;
    EmuConfig.Cpu.FPUFPCR.SetRoundMode(FPRoundMode::Nearest);
    EmuConfig.Cpu.FPUFPCR.SetFlushToZero(true);
    const FPControlRegisterBackup host_fpcr(EmuConfig.Cpu.FPUFPCR);
    const auto qmtc2=[](u32 r,u32 v) { return (0x12u<<26)|(5u<<21)|(r<<16)|(v<<11); };
    const auto qmfc2=[](u32 r,u32 v) { return (0x12u<<26)|(1u<<21)|(r<<16)|(v<<11); };
    constexpr std::array<std::array<u32,2>,5> sources{{{7,9},{7,7},{0,9},{7,0},{0,0}}};
    for (bool signed_branch : {false,true})
    for (bool taken : {false,true})
    for (const auto& source : sources)
    for (u32 threshold_word : {0u,0x7f800000u,0x7fa00001u})
    {
        EmuCoreXOracleSetVUIR(0,0);
        std::array<GPR_reg,32> gpr{};
        std::array<u32,128> vf{}; vf[3]=0x3f800000;
        for (u32 lane=0;lane<4;++lane)
        {
            gpr[8].UL[lane]=std::array<u32,4>{0x7fffffff,0x80000001,0x40000000,0xffa00001}[lane];
            gpr[10].UL[lane]=std::array<u32,4>{1,0xbf800000,0x7fc12345,threshold_word}[lane];
        }
        gpr[12].UD[0]=0x654321;
        gpr[13].SD[0]=signed_branch?(taken?1:-1):17;
        gpr[14].SD[0]=taken?17:18;
        const u32 fs=source[0],ft=source[1];
        const u32 clip=(0x12u<<26)|(0x1fu<<21)|(ft<<16)|(fs<<11)|0x1ffu;
        const u32 branch=signed_branch?(7u<<26)|(13u<<21)|2u:
            (4u<<26)|(13u<<21)|(14u<<16)|2u;
        const std::vector<u32> code{(0x12u<<26)|(6u<<21)|(12u<<16)|(18u<<11),qmtc2(8,7),qmtc2(10,9),
            branch,clip,(0x0du<<26)|(15u<<16)|0x55u,
            (0x12u<<26)|(2u<<21)|(19u<<16)|(18u<<11),qmfc2(20,fs),qmfc2(21,ft)};
        const std::array<u32,4> zero{0,0,0,0x3f800000};
        const auto word=[&](u32 v,u32 lane) { return v==0?zero[lane]:gpr[v==7?8:10].UL[lane]; };
        const u32 w=word(ft,3);
        const s32 threshold=static_cast<s32>((w&0x7f800000u)?w&0x7fffffffu:0x007fffffu);
        u32 mask=0;
        for (u32 lane=0;lane<3;++lane)
        {
            if (static_cast<s32>(word(fs,lane))>threshold) mask|=1u<<(lane*2);
            if (static_cast<s32>(word(fs,lane)^0x80000000u)>threshold) mask|=2u<<(lane*2);
        }
        const u32 expected=((0x654321u<<6)|mask)&0xffffffu;
        EmuCoreXOracleSetVULocalReadonlyClip(false);
        const auto baseline=RunEEProgramWithIrMode(0,code,gpr.data(),nullptr,nullptr,nullptr,true,vf.data());
        EmuCoreXOracleSetVULocalReadonlyClip(true);
        const u64 before=EmuCoreXOracleVULocalReadonlyClipEmits();
        const auto actual=RunEEProgramWithIrMode(0,code,gpr.data(),nullptr,nullptr,nullptr,true,vf.data());
        const auto interp=RunEEProgram(false,code,true,nullptr,nullptr,gpr.data(),nullptr,nullptr,vf.data());
        Check(EmuCoreXOracleVULocalReadonlyClipEmits()>before,"EE delay-slot CLIP emits readonly source bindings");
        Check(actual.gpr[19]==expected&&actual.gpr[15]==(taken?0u:0x55u),
            "EE delay-slot CLIP preserves independent history and both signed/equality branch outcomes");
        CompareEE(baseline,actual,"EE readonly delay-slot CLIP preserves baseline cache and architectural state");
        CompareEE(interp,actual,"EE readonly delay-slot CLIP preserves interpreter control flow and cached raw VFs");
        Check(baseline.cycle==actual.cycle,"EE readonly delay-slot CLIP preserves exact baseline cycle accounting");
    }
    EmuConfig=saved;
}
