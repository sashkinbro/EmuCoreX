// SPDX-License-Identifier: GPL-3.0+
// Actual microVU exit emitter coverage; requires initialized emulator memory.
extern "C" u32 EmuCoreXOracleEmitSavePQ(u32 index, int qInst, int pInst);

void VUSavePQTests()
{
    for (u32 vu = 0; vu < 2; ++vu)
    for (u32 q = 0; q < 2; ++q)
    for (u32 p = 0; p < 2; ++p)
    {
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        u8* entry = oakStartBlock();
        oakAsm->SUB(oak::util::SP, oak::util::SP, 32);
        oakAsm->STR(oak::util::X27, oak::util::SP);
        oakAsm->STR(oak::DReg(15), oak::util::SP, oak::POffset<15, 3>(8));
        oakMoveAddressToReg(oak::util::X27, &g_cpuRegistersPack);
        oakAsm->LDR(oak::QReg(15), oak::util::X0);
        const u32 bytes = EmuCoreXOracleEmitSavePQ(vu, q, p);
        oakAsm->STR(oak::QReg(15), oak::util::X1);
        oakAsm->LDR(oak::DReg(15), oak::util::SP, oak::POffset<15, 3>(8));
        oakAsm->LDR(oak::util::X27, oak::util::SP);
        oakAsm->ADD(oak::util::SP, oak::util::SP, 32);
        oakAsm->RET();
        oakEndBlock();
        bool same = true;
        for (u32 seed : {0u, 0x80000000u, 0x7fc12345u, 0xffffffffu})
        {
            const std::array<u32, 4> input = {seed, seed ^ 0x12345678u,
                seed ^ 0x9abcdef0u, seed ^ 0xfedcba98u};
            std::array<u32, 4> output{};
            VURegs& regs = vu ? VU1 : VU0;
            regs.VI[REG_P].UL = regs.pending_p = 0xdeadbeefu;
            reinterpret_cast<void (*)(const u32*, u32*)>(entry)(input.data(), output.data());
            same &= regs.VI[REG_Q].UL == input[q] && regs.pending_q == input[q ^ 1];
            same &= regs.VI[REG_P].UL == (vu ? input[2 + p] : 0xdeadbeefu);
            same &= regs.pending_p == (vu ? input[2 + (p ^ 1)] : 0xdeadbeefu);
            // Existing exits leave the selected instance in each pair's first lane.
            same &= output == std::array<u32, 4>{input[q], input[q ^ 1],
                input[vu ? 2 + p : 2], input[vu ? 2 + (p ^ 1) : 3]};
        }
        Check(same, "microVU P/Q save preserves raw bits, pending instances and outgoing SIMD layout");
        // Two scalar extracts/stores per unit. Instance selection may still shuffle once.
        Check(bytes == (vu ? 32u : 16u) + (q + (vu ? p : 0)) * 8u,
            "microVU P/Q save eliminates reversible table shuffles");
        std::printf("VUPQSAVE core=%u q=%u p=%u native_bytes=%u\n", vu, q, p, bytes);
    }
}

