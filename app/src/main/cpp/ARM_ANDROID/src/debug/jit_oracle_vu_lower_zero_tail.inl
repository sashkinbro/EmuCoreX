// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
extern "C" u32 EmuCoreXOracleEmitZeroTailClamp(bool optimized);
extern "C" void EmuCoreXOracleSetVUZeroTailClamp(bool enabled);
extern "C" u64 EmuCoreXOracleVUZeroTailClampEmits();

void VUZeroTailDividePrimitiveTests()
{
    using Kernel=void(*)(const u32*,u32*,u64,u64);
    const auto compile=[&](bool optimized,u32 offset,u32& bytes){
        oakSetAsmPtr(SysMemory::GetEERec()+0x2000000+offset,0x1000);u8* entry=oakStartBlock();
        oakAsm->SUB(oak::util::SP,oak::util::SP,16);oakAsm->STR(oak::util::X26,oak::util::SP);
        oakMoveAddressToReg(OAK_XVUCLAMP,&g_cpuRegistersPack.mVUglob);
        oakAsm->MRS(oak::util::X4,oak::SystemReg::FPCR);oakAsm->MRS(oak::util::X5,oak::SystemReg::FPSR);
        oakAsm->MSR(oak::SystemReg::FPCR,oak::util::X2);oakAsm->MSR(oak::SystemReg::FPSR,oak::util::X3);
        oakAsm->LDP(oak::QReg(0),oak::QReg(1),oak::util::X0);
        oakAsm->FDIV(oak::SReg(0),oak::SReg(0),oak::SReg(1));
        bytes=EmuCoreXOracleEmitZeroTailClamp(optimized);oakAsm->STR(oak::QReg(0),oak::util::X1);
        oakAsm->MRS(oak::util::X2,oak::SystemReg::FPSR);oakAsm->STR(oak::util::X2,oak::util::X1,16);
        oakAsm->MSR(oak::SystemReg::FPCR,oak::util::X4);oakAsm->MSR(oak::SystemReg::FPSR,oak::util::X5);
        oakAsm->LDR(oak::util::X26,oak::util::SP);oakAsm->ADD(oak::util::SP,oak::util::SP,16);
        oakAsm->RET();oakEndBlock();return reinterpret_cast<Kernel>(entry);
    };
    u32 oldBytes=0,newBytes=0;const auto reference=compile(false,0,oldBytes),actual=compile(true,0x1000,newBytes);
    Check(oldBytes==16&&newBytes==12,"FDIV zero-tail numeric clamp removes one INS");
    constexpr std::array<u32,16> edges{0,0x80000000,1,0x807fffff,0x00800000,0x80800000,
        0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,
        0x7fc12345,0xffa00001,0x7f800001,0xffffffff};
    bool exact=true,zero=true,unit=true;u32 cases=0;
    for(u64 rounding=0;rounding<4;++rounding)for(bool fz:{false,true})for(bool dn:{false,true})
    for(u64 seed:{0ull,0x0800009full})for(u32 a:edges)for(u32 b:edges)
    {
        const std::array<u32,8> input{a,0x7f800001,0xffffffff,0x12345678,b,0xff800001,1,0x80000000};
        std::array<u32,6> before{},after{};const u64 fpcr=(rounding<<22)|(u64(fz)<<24)|(u64(dn)<<25);
        reference(input.data(),before.data(),fpcr,seed);actual(input.data(),after.data(),fpcr,seed);
        exact&=before==after;zero&=after[1]==0&&after[2]==0&&after[3]==0;
        if(a==0x3f800000&&b==0x3f800000)unit&=after[0]==0x3f800000;
        ++cases;
    }
    Check(exact,"FDIV widened clamp preserves all result words and seeded FPSR across all round/FZ/DN modes");
    Check(zero&&unit,"FDIV clears arbitrary upper raw words and unit division remains exact one");
    std::printf("VU_LOWER_ZERO_TAIL_PRIMITIVE cases=%u old_bytes=%u new_bytes=%u\n",cases,oldBytes,newBytes);
}

void RunVULowerZeroTailClampTests()
{
    cpuinfo_initialize();EmuConfig=Pcsx2Config();
    EmuConfig.Speedhacks.vuThread=false;EmuConfig.Speedhacks.vuFlagHack=false;
    if(!SysMemory::Allocate()){Check(false,"allocate lower zero-tail oracle memory");return;}
    CpuVU0=&CpuMicroVU0;CpuVU1=&CpuMicroVU1;CpuMicroVU0.Reserve();CpuMicroVU1.Reserve();
    VUZeroTailDividePrimitiveTests();
    const auto saved=EmuConfig;
    constexpr std::array<std::array<u32,4>,7> patterns{{{0x3f800000,0x3f800000,0x3f800000,0x3f800000},
        {0,0x80000000,0x00800000,0x80800000},{1,0x807fffff,0x7f800000,0xff800000},
        {0x7fc12345,0xffa00001,0x7f7fffff,0xff7fffff},
        {0x3e800000,0xbf000000,0x40000000,0xc0400000},{0x7f800001,0xff800001,0x7fffffff,0xffffffff},
        {0x00800000,0x80800000,0x00800000,0x80800000}}};
    const auto codebytes=[](u32 vu){std::vector<JitBlockProfile> blocks;
        if(vu)VU1_JitGetBlockProfiles(blocks);else VU0_JitGetBlockProfiles(blocks);
        u64 size=0;for(const auto& b:blocks)size+=b.host_size;return size;};
    u64 oldBytes=0,newBytes=0,emitted=0;u32 cases=0;
    for(u32 vu=0;vu<2;++vu)for(u32 kind=0;kind<(vu?5u:2u);++kind)
    for(u32 clamp=0;clamp<4;++clamp)for(FPRoundMode round:{FPRoundMode::Nearest,FPRoundMode::ChopZero})
    for(bool fz:{false,true})for(u32 lane=0;lane<4;++lane)for(u32 sample=0;sample<patterns.size();++sample)
    {
        if(kind>=3&&lane!=0)continue; // XY/XZ forms have no component selector.
        EmuCoreXOracleSetVUIR(vu,0);auto& rc=EmuConfig.Cpu.Recompiler;
        if(vu){rc.vu1Overflow=clamp>=1;rc.vu1ExtraOverflow=clamp>=2;rc.vu1SignOverflow=clamp>=3;}
        else{rc.vu0Overflow=clamp>=1;rc.vu0ExtraOverflow=clamp>=2;rc.vu0SignOverflow=clamp>=3;}
        auto& fpcr=vu?EmuConfig.Cpu.VU1FPCR:EmuConfig.Cpu.VU0FPCR;
        fpcr.SetRoundMode(round);fpcr.SetFlushToZero(fz);EmuConfig.Cpu.FPUFPCR=fpcr;
        std::array<u32,128> input{};input[3]=0x3f800000;
        for(u32 r=1;r<32;++r)for(u32 l=0;l<4;++l)input[r*4+l]=patterns[(sample+r-1)%patterns.size()][l];
        if(sample==0||sample==6)for(u32 l=0;l<4;++l)input[8+l]=0x40800000;
        u32 lower=kind==0?0x800003bcu:kind==1?0x800003beu:kind==2?0x800007fdu:kind==3?0x8000077cu:0x8000077du;
        lower|=1u<<11;if(kind<2)lower|=(2u<<16)|(lane<<21)|(lane<<23);else if(kind==2)lower|=lane<<21;
        std::vector<u32> program{0x8000033cu|(1u<<11)|(20u<<16)|(15u<<21),NOP_U,lower,NOP_U,
            kind<2?0x800003bfu:0x800007bfu,NOP_U};
        if(kind<2)program.insert(program.end(),{NOP_L,(15u<<21)|(4u<<6)|0x1cu}); // MULq VF0 -> VF4
        else program.insert(program.end(),{0x8000067cu|(4u<<16)|(15u<<21),NOP_U}); // MFP VF4
        for(u32 n=0;n<4;++n)program.insert(program.end(),{NOP_L,NOP_U});
        program.insert(program.end(),{NOP_L,NOP_U|0x40000000u,NOP_L,NOP_U});
        std::array<u32,12> oldRings{},newRings{};
        EmuCoreXOracleSetVUZeroTailClamp(false);EmuCoreXOracleVULocalFlagRings(vu,oldRings.data(),true);
        const auto baseline=RunVU(vu,true,program,0x80,0,128,false,nullptr,input.data());oldBytes+=codebytes(vu);
        EmuCoreXOracleVULocalFlagRings(vu,oldRings.data(),false);
        EmuCoreXOracleSetVUZeroTailClamp(true);const u64 before=EmuCoreXOracleVUZeroTailClampEmits();
        EmuCoreXOracleVULocalFlagRings(vu,newRings.data(),true);
        const auto actual=RunVU(vu,true,program,0x80,0,128,false,nullptr,input.data());newBytes+=codebytes(vu);
        EmuCoreXOracleVULocalFlagRings(vu,newRings.data(),false);
        const u64 delta=EmuCoreXOracleVUZeroTailClampEmits()-before;emitted+=delta;
        Check(clamp==2?delta>0:delta==0,"lower scalar results use widened clamp only in numeric extra mode");
        Compare(baseline,actual,"lower zero-tail clamp preserves all raw architecture and memory");
        Check(oldRings==newRings&&baseline.regs.cycle==actual.regs.cycle&&
            baseline.regs.q.UL==actual.regs.q.UL&&baseline.regs.p.UL==actual.regs.p.UL&&
            baseline.regs.pending_q==actual.regs.pending_q&&baseline.regs.pending_p==actual.regs.pending_p,
            "lower zero-tail clamp preserves exact cycles PQ pending state and all flag rings");
        bool unit=true;
        if(sample==0)unit=(kind<2?actual.regs.VI[REG_Q].UL:actual.regs.VI[REG_P].UL)==
            (kind==0?0x3e800000u:kind==1?0x3f000000u:0x3f490fdbu);
        if(sample==6&&kind<2)unit=actual.regs.VI[REG_Q].UL==
            (input[4+lane]&0x80000000u); // Outer Q wrapper explicitly flushes when FZ is off.
        Check(unit,"finite and tiny DIV RSQRT and EATAN inputs match independent result bits including outer Q flush policy");
        ++cases;
    }
    Check(emitted>0&&newBytes<oldBytes,"lower widened result clamps remove instructions from actual Q and EFU blocks");
    std::printf("VU_LOWER_ZERO_TAIL cases=%u emitted=%llu old_bytes=%llu new_bytes=%llu\n",cases,
        (unsigned long long)emitted,(unsigned long long)oldBytes,(unsigned long long)newBytes);
    EmuCoreXOracleSetVUZeroTailClamp(true);EmuCoreXOracleSetVUIR(0,-1);EmuCoreXOracleSetVUIR(1,-1);EmuConfig=saved;
}
