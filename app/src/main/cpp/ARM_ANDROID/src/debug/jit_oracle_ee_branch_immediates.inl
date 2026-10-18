// SPDX-License-Identifier: GPL-3.0+
// Legacy EE constant comparisons in the profiled GTA load/branch loop.

void EEBranchImmediateTests()
{
    for (u32 op : {4u, 5u, 20u, 21u})
    for (bool reverse : {false, true})
    for (bool clobber_delay : {false, true})
    for (s32 constant : {0, 1, 4095, 4096, -1})
    for (u32 value_case = 0; value_case < (constant == 0 ? 5u : 3u); ++value_case)
    for (u32 operand_mode = 0; operand_mode < (constant == 0 ? 3u : 1u); ++operand_mode)
    {
        std::printf("EEBRANCHIMM op=%u reverse=%u delay_clobber=%u constant=%d value_case=%u operand_mode=%u\n",
            op, reverse, clobber_delay, constant, value_case, operand_mode);
        const u64 literal = static_cast<u64>(static_cast<s64>(constant));
        const u32 constant_register = constant == 0 ? 0u : 9u;
        const u64 value = value_case == 0 ? literal : value_case == 1 ? literal ^ 1u :
            value_case == 2 ? literal | (1ull << 32) : value_case == 3 ? (1ull << 63) : ~0ull;
        std::array<GPR_reg, 32> initial{};
        initial[8].UD[0] = value;
        initial[8].UD[1] = 0xfedcba9876543210ull;
        initial[9].UD[1] = 0xabcdef0123456789ull;
        std::vector<u32> code = {MipsI(9, 0, 9, static_cast<u32>(constant))};
        // GPR10 is runtime zero: force the operand through a dirty scalar GPR
        // or SIMD mapping before the branch, preserving both halves of GPR8.
        if (operand_mode == 1)
            code.push_back(MipsR(8, 10, 8, 0, 0x2d)); // DADDU
        else if (operand_mode == 2)
            code.push_back((0x1cu << 26) | MipsR(8, 10, 8, 18, 41)); // POR (MMI3)
        const std::array<u32, 4> tail = {
            MipsI(op, reverse ? constant_register : 8, reverse ? 8 : constant_register, 2),
            clobber_delay ? MipsR(0, 0, 8, 0, 0x2d) : MipsI(9, 0, 23, 1),
            MipsI(9, 0, 24, 1),
            MipsI(9, 0, 25, 1),
        };
        code.insert(code.end(), tail.begin(), tail.end());
        const auto expected = RunEEProgram(false, code, true, nullptr, nullptr, initial.data());
        const auto actual = RunEEProgramWithIrMode(0, code, initial.data(), nullptr, nullptr, nullptr, true);
        CompareEE(expected, actual, "legacy EE constant branch full-width operands and delay-slot effects");
        const bool equal_branch = op == 4 || op == 20;
        const bool taken = equal_branch == (value == literal);
        const bool run_delay = op < 20 || taken;
        Check(actual.gpr[24] == (taken ? 0u : 1u) && actual.gpr[25] == 1 &&
            actual.gpr[23] == (!clobber_delay && run_delay ? 1u : 0u) &&
            actual.gpr[8] == (clobber_delay && run_delay ? 0u : value),
            "legacy EE immediate branch matches independent taken/annul/clobber expectations");
        if (constant >= 0 && constant <= 4095)
        {
            const void* host = nullptr;
            u32 bytes = 0;
            bool found = false;
            if (EmuCoreXOracleGetEeCompiledBlock(EE_TEST_PC, &host, &bytes) && host)
            {
                for (u32 offset = 0; offset + 4 <= bytes; offset += 4)
                {
                    u32 instruction;
                    std::memcpy(&instruction, static_cast<const u8*>(host) + offset, 4);
                    if (constant == 0)
                    {
                        // The early branch skips the taken path except for BNEL.
                        // BNE uses CBZ; BEQ/BEQL/BNEL use CBNZ. Require X, not W.
                        const u32 expected_opcode = op == 5 ? 0xb4000000u : 0xb5000000u;
                        found |= (instruction & 0xff000000u) == expected_opcode;
                    }
                    else
                    {
                        // CMP Xn, #imm12; ignore only the operand register field.
                        found |= (instruction & ~0x3e0u) == (0xf100001fu | (static_cast<u32>(constant) << 10));
                    }
                }
                if (constant == 0 && !reverse && !clobber_delay && value_case == 0 && operand_mode == 0)
                    std::printf("EEBRANCHZERO op=%u block_bytes=%u\n", op, bytes);
            }
            Check(found, constant == 0 ? "legacy EE zero comparison uses a fused 64-bit compare-and-branch" :
                "legacy EE branch encodes small constants directly in the 64-bit comparison");
        }
    }
}
