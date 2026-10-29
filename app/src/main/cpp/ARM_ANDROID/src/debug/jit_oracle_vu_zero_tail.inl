// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
extern "C" u32 EmuCoreXOracleEmitZeroTailClamp(bool optimized);
extern "C" void EmuCoreXOracleSetVUZeroTailClamp(bool enabled);
extern "C" u64 EmuCoreXOracleVUZeroTailClampEmits();

void RunVUZeroTailClampTests()
{
    cpuinfo_initialize(); EmuConfig=Pcsx2Config();
    EmuConfig.Speedhacks.vuThread=false; EmuConfig.Speedhacks.vuFlagHack=false;
    if (!SysMemory::Allocate()) {Check(false,"allocate zero-tail clamp code memory");return;}
    CpuVU0=&CpuMicroVU0;CpuVU1=&CpuMicroVU1;CpuMicroVU0.Reserve();CpuMicroVU1.Reserve();
    using Kernel=void(*)(const u32*,u32*,u64,u64);
    constexpr std::array<u32,16> edges{0,0x80000000,1,0x807fffff,0x00800000,0x80800000,
        0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,
        0x7fc12345,0xffa00001,0x7f800001,0xffffffff};
    for(u32 op=0;op<3;++op)
    {
        const auto compile=[&](bool optimized,u32 offset,u32& bytes) {
            oakSetAsmPtr(SysMemory::GetEERec()+0x2000000+offset,0x1000);
            u8* entry=oakStartBlock();
            oakAsm->SUB(oak::util::SP,oak::util::SP,16);
            oakAsm->STR(oak::util::X26,oak::util::SP);
            oakMoveAddressToReg(OAK_XVUCLAMP,&g_cpuRegistersPack.mVUglob);
            oakAsm->MRS(oak::util::X4,oak::SystemReg::FPCR);
            oakAsm->MRS(oak::util::X5,oak::SystemReg::FPSR);
            oakAsm->MSR(oak::SystemReg::FPCR,oak::util::X2);
            oakAsm->MSR(oak::SystemReg::FPSR,oak::util::X3);
            oakAsm->LDP(oak::QReg(0),oak::QReg(1),oak::util::X0);
            if(op==0) oakAsm->FADD(oak::SReg(0),oak::SReg(0),oak::SReg(1));
            else if(op==1) oakAsm->FSUB(oak::SReg(0),oak::SReg(0),oak::SReg(1));
            else oakAsm->FMUL(oak::SReg(0),oak::SReg(0),oak::SReg(1));
            bytes=EmuCoreXOracleEmitZeroTailClamp(optimized);
            oakAsm->STR(oak::QReg(0),oak::util::X1);
            oakAsm->MRS(oak::util::X2,oak::SystemReg::FPSR);
            oakAsm->STR(oak::util::X2,oak::util::X1,16);
            oakAsm->MSR(oak::SystemReg::FPCR,oak::util::X4);
            oakAsm->MSR(oak::SystemReg::FPSR,oak::util::X5);
            oakAsm->LDR(oak::util::X26,oak::util::SP);
            oakAsm->ADD(oak::util::SP,oak::util::SP,16);oakAsm->RET();oakEndBlock();
            return reinterpret_cast<Kernel>(entry);
        };
        u32 old_bytes=0,new_bytes=0;
        const auto reference=compile(false,0,old_bytes),actual=compile(true,0x1000,new_bytes);
        Check(old_bytes==16&&new_bytes==12,"zero-tail numeric result clamp removes exactly one INS");
        bool exact=true,zero=true,unit=true;
        u32 cases=0;
        for(u64 rounding=0;rounding<4;++rounding)
        for(bool fz:{false,true}) for(bool dn:{false,true})
        for(u64 seed:{0ull,0x0800009full})
        for(u32 a:edges) for(u32 b:edges)
        {
            // Upper input lanes deliberately contain sNaNs and nonzero values;
            // the scalar arithmetic instruction must discard them before clamp.
            const std::array<u32,8> input{a,0x7f800001,0xffffffff,0x12345678,
                b,0xff800001,1,0x80000000};
            std::array<u32,6> before{},after{};
            const u64 fpcr=(rounding<<22)|(u64(fz)<<24)|(u64(dn)<<25);
            reference(input.data(),before.data(),fpcr,seed);actual(input.data(),after.data(),fpcr,seed);
            exact&=before==after;
            zero&=after[1]==0&&after[2]==0&&after[3]==0;
            if(a==0x3f800000u&&b==0x3f800000u)
                unit&=after[0]==(op==0?0x40000000u:op==1?(rounding==2?0x80000000u:0u):0x3f800000u);
            ++cases;
        }
        Check(exact,"zero-tail numeric clamp preserves every result word and seeded FPSR across all round/FZ/DN modes");
        Check(zero,"scalar ADD SUB MUL clear arbitrary upper raw inputs before widened clamp");
        Check(unit,"unit ADD SUB MUL result bits match independent exact arithmetic including downward-rounding signed zero");
        std::printf("VU_ZERO_TAIL_PRIMITIVE op=%u cases=%u old_bytes=%u new_bytes=%u\n",op,cases,old_bytes,new_bytes);
    }
    const auto saved=EmuConfig;
    constexpr std::array<std::array<u32,4>,3> patterns{{{0,0x80000000,0x3f800000,0xbf800000},
        {1,0x807fffff,0x7f800000,0xff800000},{0x7fc12345,0xffa00001,0x7f7fffff,0xff7fffff}}};
    const auto codebytes=[](u32 vu){std::vector<JitBlockProfile> blocks;
        if(vu)VU1_JitGetBlockProfiles(blocks);else VU0_JitGetBlockProfiles(blocks);
        u64 size=0;for(const auto& b:blocks)size+=b.host_size;return size;};
    u64 old_bytes=0,new_bytes=0,emitted=0;
    for(u32 vu=0;vu<2;++vu) for(u32 clamp=0;clamp<4;++clamp)
    for(FPRoundMode round:{FPRoundMode::Nearest,FPRoundMode::ChopZero})
    for(bool fz:{false,true}) for(u32 op:{0x28u,0x2cu,0x2au})
    for(u32 fd:{0u,1u,2u,3u}) for(u32 mask:{8u,4u,2u,1u})
    for(u32 sample=0;sample<patterns.size();++sample)
    {
        EmuCoreXOracleSetVUIR(vu,0);
        auto& rc=EmuConfig.Cpu.Recompiler;
        if(vu){rc.vu1Overflow=clamp>=1;rc.vu1ExtraOverflow=clamp>=2;rc.vu1SignOverflow=clamp>=3;}
        else{rc.vu0Overflow=clamp>=1;rc.vu0ExtraOverflow=clamp>=2;rc.vu0SignOverflow=clamp>=3;}
        auto& fpcr=vu?EmuConfig.Cpu.VU1FPCR:EmuConfig.Cpu.VU0FPCR;
        fpcr.SetRoundMode(round);fpcr.SetFlushToZero(fz);EmuConfig.Cpu.FPUFPCR=fpcr;
        std::array<u32,128> input{};input[3]=0x3f800000;
        for(u32 r=1;r<32;++r)for(u32 lane=0;lane<4;++lane)input[r*4+lane]=patterns[(sample+r-1)%3][lane];
        const u32 upper=(mask<<21)|(2u<<16)|(1u<<11)|(fd<<6)|op;
        std::vector<u32> program{0x8000033cu|(1u<<11)|(20u<<16)|(15u<<21),NOP_U,NOP_L,upper};
        for(u32 n=0;n<5;++n)program.insert(program.end(),{NOP_L,NOP_U});
        program.insert(program.end(),{NOP_L,NOP_U|0x40000000u,NOP_L,NOP_U});
        std::array<u32,12> beforeRings{},afterRings{};
        EmuCoreXOracleSetVUZeroTailClamp(false);EmuCoreXOracleVULocalFlagRings(vu,beforeRings.data(),true);
        const auto baseline=RunVU(vu,true,program,0x80,0,128,false,nullptr,input.data());old_bytes+=codebytes(vu);
        EmuCoreXOracleVULocalFlagRings(vu,beforeRings.data(),false);
        EmuCoreXOracleSetVUZeroTailClamp(true);const u64 counter=EmuCoreXOracleVUZeroTailClampEmits();
        EmuCoreXOracleVULocalFlagRings(vu,afterRings.data(),true);
        const auto actual=RunVU(vu,true,program,0x80,0,128,false,nullptr,input.data());new_bytes+=codebytes(vu);
        EmuCoreXOracleVULocalFlagRings(vu,afterRings.data(),false);
        const u64 delta=EmuCoreXOracleVUZeroTailClampEmits()-counter;emitted+=delta;
        Check(delta==(clamp==2?1u:0u),"scalar result widens exactly once only in numeric extra clamp mode");
        Compare(baseline,actual,"zero-tail clamp preserves all baseline VF VI ACC PQ flags and memory");
        Check(beforeRings==afterRings&&baseline.regs.cycle==actual.regs.cycle,
            "zero-tail clamp preserves exact cycles and every flag ring instance");
    }
    Check(emitted>0&&new_bytes<old_bytes,"zero-tail result clamp is used in real scalar VU blocks and reduces emitted bytes");
    std::printf("VU_ZERO_TAIL emitted=%llu old_bytes=%llu new_bytes=%llu\n",
        (unsigned long long)emitted,(unsigned long long)old_bytes,(unsigned long long)new_bytes);
    EmuCoreXOracleSetVUZeroTailClamp(true);EmuCoreXOracleSetVUIR(0,-1);EmuCoreXOracleSetVUIR(1,-1);EmuConfig=saved;
}
