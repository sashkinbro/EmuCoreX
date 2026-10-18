// SPDX-License-Identifier: GPL-3.0+
// Legacy EE constant comparisons in the profiled GTA load/branch loop.

void EEBranchImmediateTests()
{
    for (u32 op : {4u, 5u, 20u, 21u})
    for (bool reverse : {false, true})
    for (bool clobber_delay : {false, true})
    for (s32 constant : {0, 1, 4095, 4096, -1})
    for (u32 value_case = 0; value_case < 3; ++value_case)
    {
        std::printf("EEBRANCHIMM op=%u reverse=%u delay_clobber=%u constant=%d value_case=%u\n",
            op, reverse, clobber_delay, constant, value_case);
        const u64 literal = static_cast<u64>(static_cast<s64>(constant));
        const u32 constant_register = constant == 0 ? 0u : 9u;
        const u64 value = value_case == 0 ? literal : value_case == 1 ? literal ^ 1u : literal | (1ull << 32);
        std::array<GPR_reg, 32> initial{};
        initial[8].UD[0] = value;
        initial[8].UD[1] = 0xfedcba9876543210ull;
        initial[9].UD[1] = 0xabcdef0123456789ull;
        const std::vector<u32> code = {
            MipsI(9, 0, 9, static_cast<u32>(constant)),
            MipsI(op, reverse ? constant_register : 8, reverse ? 8 : constant_register, 2),
            clobber_delay ? MipsR(0, 0, 8, 0, 0x2d) : MipsI(9, 0, 23, 1),
            MipsI(9, 0, 24, 1),
            MipsI(9, 0, 25, 1),
        };
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
                    // CMP Xn, #imm12; ignore only the operand register field.
                    found |= (instruction & ~0x3e0u) == (0xf100001fu | (static_cast<u32>(constant) << 10));
                }
            }
            Check(found, "legacy EE branch encodes small constants directly in the 64-bit comparison");
        }
    }
}
