// SPDX-License-Identifier: GPL-3.0+
// Focused local rewrite tests; include in jit_oracle.cpp after the VU suites.
extern "C" u32 EmuCoreXOracleEmitVULocalPrimitive(u32 kind, u32 argument, bool alias);
extern "C" void EmuCoreXOracleTestVUBlockTail(void (*check)(bool, const char*));
extern "C" u32 EmuCoreXOracleEmitVULocalCarryCompare(bool equality_only);


#include "jit_oracle_vu_local_integration.inl"
#include "jit_oracle_vu_local_natural.inl"
#include "jit_oracle_vu_ebit_target.inl"
#include "jit_oracle_vu_empty_field.inl"
#include "jit_oracle_vu_local_full_acc.inl"
#include "jit_oracle_vu_local_op.inl"
#include "jit_oracle_vu_local_clip.inl"

void RunVULocalRewriteTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate local VU oracle memory");
        return;
    }
    CpuVU0 = &CpuMicroVU0;
    CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve();
    CpuMicroVU1.Reserve();
    constexpr std::array<u32, 10> words = {0, 1, 0x7fff, 0x8000, 0xffff,
        0x10000, 0xffff0000, 0xdeadbeef, 0x80000000, 0xffffffff};
    constexpr std::array<u32, 8> immediates = {0, 1, 0x7ff, 0xfff, 0x1000, 0x1234, 0x7000, 0x7fff};
    bool branch_bits = true, branch_sizes = true;
    for (bool equality : {false, true})
    {
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        u8* entry = oakStartBlock();
        const u32 bytes = EmuCoreXOracleEmitVULocalCarryCompare(equality);
        oakAsm->CSET(oak::util::W0, equality ? oak::Cond::EQ : oak::Cond::LT);
        oakAsm->RET();
        oakEndBlock();
        branch_sizes &= bytes == (equality ? 4u : 8u);
        for (u32 word : words)
            branch_bits &= reinterpret_cast<u32 (*)(u32)>(entry)(word) ==
                (equality ? ((word & 0xffffu) == 0) : (static_cast<s16>(word) < 0));
    }
    Check(branch_bits, "carried VI branch compares ignore dirty high halfwords and preserve signed tests");
    Check(branch_sizes, "carried equality branches use one TST while signed branches retain SXTH/CMP");

    bool immediate_bits = true, immediate_sizes = true;
    for (u32 kind : {1u, 2u})
    for (u32 imm : immediates)
    for (bool alias : {false, true})
    {
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        u8* entry = oakStartBlock();
        const u32 bytes = EmuCoreXOracleEmitVULocalPrimitive(kind, imm, alias);
        oakAsm->RET();
        oakEndBlock();
        const u32 expected_bytes = (imm <= 0xfff || (imm & 0xfff) == 0) ? 4 : 8;
        immediate_sizes &= bytes == expected_bytes;
        for (u32 word : words)
        {
            const u32 result = reinterpret_cast<u32 (*)(u32, u32)>(entry)(word, word);
            immediate_bits &= result == (kind == 1 ? word + imm : word - imm);
        }
    }
    Check(immediate_bits, "VU unsigned immediate rewrites preserve full W results including dirty VI upper halves");
    Check(immediate_sizes, "VU unsigned immediate uses direct ADD/SUB or two encodable immediate pieces");

    bool fcor_bits = true, fcor_sizes = true;
    constexpr std::array<u32, 10> clip_masks = {0, 1, 2, 0x80, 0xffff, 0x10000, 0x555555, 0xaaaaaa, 0xfffffe, 0xffffff};
    for (u32 mask : clip_masks)
    {
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        u8* entry = oakStartBlock();
        const u32 bytes = EmuCoreXOracleEmitVULocalPrimitive(3, mask, false);
        oakAsm->RET();
        oakEndBlock();
        fcor_sizes &= bytes <= 16;
        for (u32 clip : clip_masks)
            fcor_bits &= reinterpret_cast<u32 (*)(u32)>(entry)(clip) ==
                (((clip | mask) & 0xffffff) == 0xffffff ? 1u : 0u);
    }
    Check(fcor_bits, "VU FCOR complement test matches independent 24-bit OR truth table");
    Check(fcor_sizes, "VU FCOR emits at most MOV/MOVK+BICS+CSET without add/shift normalization");

    // Clamp-off arithmetic may consume the allocator's last clone source.
    const bool saved_overflow = EmuConfig.Cpu.Recompiler.vu1Overflow;
    const bool saved_extra = EmuConfig.Cpu.Recompiler.vu1ExtraOverflow;
    EmuConfig.Cpu.Recompiler.vu1Overflow = false;
    EmuConfig.Cpu.Recompiler.vu1ExtraOverflow = false;
    oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
    oakStartBlock();
    const u32 folded_bytes = EmuCoreXOracleEmitVULocalPrimitive(4, 0, false);
    oakAsm->RET();
    oakEndBlock();
    Check(folded_bytes == 4, "VU vector arithmetic consumes the cached clone in its single arithmetic instruction");
    EmuConfig.Cpu.Recompiler.vu1Overflow = saved_overflow;
    EmuConfig.Cpu.Recompiler.vu1ExtraOverflow = saved_extra;

    bool status_address_bits = true, status_address_sizes = true;
    for (u32 kind : {7u, 8u})
    for (u32 shift = 0; shift < (kind == 7 ? 1u : 2u); ++shift)
    for (bool alias : {false, true})
    {
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        u8* entry = oakStartBlock();
        const u32 bytes = EmuCoreXOracleEmitVULocalPrimitive(kind, shift, alias);
        oakAsm->RET();
        oakEndBlock();
        status_address_sizes &= bytes == 4;
        for (u32 word : words)
        {
            const u32 actual = reinterpret_cast<u32 (*)(u32, u32)>(entry)(word, word);
            status_address_bits &= actual == (kind == 7 ? word & 0xfffc00ffu :
                ((word & 0x3ffu) << (shift ? 4 : 0)));
        }
    }
    Check(status_address_bits, "VU STATUS/address rewrites preserve exact bits with aliasing and dirty VI upper halves");
    Check(status_address_sizes, "VU STATUS clear and VI address extraction each use one instruction");

    bool rotation_bits = true, rotation_sizes = true;
    constexpr std::array<u32, 4> input = {0x80000000, 0x7fc12345, 0xdeadbeef, 0x01234567};
    for (u32 lane = 1; lane < 4; ++lane)
    for (bool alias : {false, true})
    {
        std::array<u32, 4> output{};
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        u8* entry = oakStartBlock();
        // Pshufd used a table rooted in X27 before this rewrite. Preserve X27
        // in the ABI wrapper and use the real production register pack.
        oakAsm->SUB(oak::util::SP, oak::util::SP, 16);
        oakAsm->STP(oak::util::X27, oak::util::X30, oak::util::SP, oak::SOffset<10, 3>(0));
        oakAsm->MOV(oak::util::X27, reinterpret_cast<uintptr_t>(&g_cpuRegistersPack));
        oakAsm->LDR(oak::QReg(alias ? 0 : 1), oak::util::X0);
        const u32 bytes = EmuCoreXOracleEmitVULocalPrimitive(0, lane, alias);
        oakAsm->STR(oak::QReg(0), oak::util::X1);
        oakAsm->LDP(oak::util::X27, oak::util::X30, oak::util::SP, oak::SOffset<10, 3>(0));
        oakAsm->ADD(oak::util::SP, oak::util::SP, 16);
        oakAsm->RET();
        oakEndBlock();
        reinterpret_cast<void (*)(const u32*, u32*)>(entry)(input.data(), output.data());
        rotation_bits &= output[0] == input[lane];
        rotation_sizes &= bytes == 4;
    }
    Check(rotation_bits, "VU cached single-lane clone selects exact raw lane bits with source/destination aliasing");
    Check(rotation_sizes, "VU cached single-lane clone uses one EXT");
    bool vf0_bits = true, vf0_sizes = true;
    for (u32 kind : {5u, 6u})
    for (u32 side = 0; side < 2; ++side)
    {
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        u8* entry = oakStartBlock();
        oakAsm->LDR(oak::QReg(0), oak::util::X0);
        oakAsm->LDR(oak::QReg(1), oak::util::X1);
        const u32 bytes = EmuCoreXOracleEmitVULocalPrimitive(kind, 0, false);
        oakAsm->STR(oak::QReg(0), oak::util::X2);
        oakAsm->RET();
        oakEndBlock();
        vf0_sizes &= bytes == 4;
        constexpr std::array<u32, 4> vf0 = {0, 0, 0, 0x3f800000};
        constexpr std::array<u32, 12> edges = {0, 0x80000000, 1, 0x80000001,
            0x7f800000, 0xff800000, 0x7fc12345, 0xffc54321,
            0x7fffffff, 0xffffffff, 0x3f800000, 0xbf800000};
        for (u32 base = 0; base < edges.size(); base += 4)
        {
            std::array<u32, 4> raw{}, actual{};
            for (u32 lane = 0; lane < 4; ++lane) raw[lane] = edges[base + lane];
            const auto& a = side ? raw : vf0;
            const auto& b = side ? vf0 : raw;
            reinterpret_cast<void (*)(const u32*, const u32*, u32*)>(entry)(a.data(), b.data(), actual.data());
            for (u32 lane = 0; lane < 4; ++lane)
            {
                const bool a_greater = static_cast<s32>(a[lane]) > static_cast<s32>(b[lane]);
                const u32 expected = (kind == 5 ? a_greater : !a_greater) ? a[lane] : b[lane];
                vf0_bits &= actual[lane] == expected;
            }
        }
    }
    Check(vf0_bits, "VU MAX/MINI with VF0 on either side preserves all raw ordering edge bits");
    Check(vf0_sizes, "VU MAX/MINI with a nonnegative VF0 operand is one SMAX/SMIN");
    EmuCoreXOracleTestVUBlockTail(Check);
    VULocalArchitecturalTests();
    VULocalFullAccCompositionTests();
    VULocalDualIssueCompositionTests();
    VULocalOpDirtyAliasTests();
    VULocalNaturalParityTests();
    VULocalEbitTargetTests();
    VULocalEmptyFieldTests();
}
