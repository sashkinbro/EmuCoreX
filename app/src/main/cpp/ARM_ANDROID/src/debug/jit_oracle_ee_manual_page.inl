// Self-modifying code on a manually protected page. The first guest store into the
// compiled callee faults the page into ProtMode_Manual; the recompiled callee then
// carries the per-execution integrity check (NEON run for 4*n words plus a scalar
// tail). The second store hits an already manual page without a fault, so only that
// check can notice the change and discard the block.
void EEManualPageSmcTests()
{
    constexpr u32 callee_idx = 14;
    constexpr u32 callee_words = 11; // 9 ADDIU, JR ra, delay slot nop
    const u32 callee_pc = EE_TEST_PC + callee_idx * 4;
    const u32 end_pc = EE_TEST_PC + (callee_idx + callee_words) * 4;
    const u32 repl1 = MipsI(9, 10, 10, 1); // addiu r10,r10,1
    const u32 repl2 = MipsI(9, 11, 11, 1); // addiu r11,r11,1
    const std::pair<u32, u32> cases[] = {{0, 7}, {3, 8}, {8, 5}, {7, 1}, {4, 4}};
    for (const auto& [k1, k2] : cases)
    {
        std::vector<u32> code = {
            MipsJ(3, callee_pc), 0,
            MipsI(15, 0, 8, repl1 >> 16), MipsI(13, 8, 8, repl1 & 0xffff),
            MipsI(43, 22, 8, 0),
            MipsJ(3, callee_pc), 0,
            MipsI(15, 0, 8, repl2 >> 16), MipsI(13, 8, 8, repl2 & 0xffff),
            MipsI(43, 23, 8, 0),
            MipsJ(3, callee_pc), 0,
            MipsJ(2, end_pc), 0};
        for (u32 i = 0; i < 9; ++i)
            code.push_back(MipsI(9, 9, 9, 1));
        code.push_back(MipsR(31, 0, 0, 0, 8));
        code.push_back(0);
        GPR_reg initial[32] = {};
        initial[22].UD[0] = callee_pc + k1 * 4;
        initial[23].UD[0] = callee_pc + k2 * 4;
        const EESnapshot interpreter = RunEEProgram(false, code, true, nullptr, nullptr, initial);
        char name[96];
        for (int mode = 0; mode <= 1; ++mode)
        {
            const EESnapshot jit = RunEEProgramWithIrMode(mode, code, initial, nullptr, nullptr, nullptr, true);
            std::snprintf(name, sizeof(name), "EE manual page smc k1=%u k2=%u mode=%d matches interpreter", k1, k2, mode);
            CompareEE(interpreter, jit, name);
            // Three calls, each store rewrites one increment for the following calls.
            std::snprintf(name, sizeof(name), "EE manual page smc k1=%u k2=%u mode=%d counters", k1, k2, mode);
            const u32 r9 = k1 == k2 ? 9 + 8 + 8 : 9 + 8 + 7;
            Check(jit.gpr[9] == r9 && jit.gpr[10] == (k1 == k2 ? 1u : 2u) && jit.gpr[11] == 1, name);
        }
    }
}
