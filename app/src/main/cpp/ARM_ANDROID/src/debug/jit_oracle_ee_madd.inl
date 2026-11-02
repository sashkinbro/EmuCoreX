// Focused MADD-family interpreter/JIT parity and actual native code-range probe.
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)
extern "C" bool EmuCoreXOracleCaptureMaddRange;
extern "C" void EmuCoreXOracleResetMaddRange();
extern "C" size_t EmuCoreXOracleGetMaddRange(u32* words, size_t capacity, bool* overflow);

static bool IsLongMultiplyAccumulate(u32 word)
{
    const u32 opcode = word & 0xffe00000u;
    return opcode == 0x9b200000u || opcode == 0x9ba00000u;
}

static void EeMaddNativeOracleTests()
{
    constexpr u32 opcodes[] = {0x00, 0x01, 0x20, 0x21};
    constexpr const char* names[] = {"MADD", "MADDU", "MADD1", "MADDU1"};
    constexpr u32 operands[][2] = {
        {0x7fffffffu, 0x80000000u}, {0xffffffffu, 0x80000001u},
        {0x80000000u, 0x80000000u}, {0x12345678u, 0xffffffffu},
        {0xffffffffu, 0xffffffffu}
    };
    constexpr u32 accumulators[][2] = {
        {0u, 0u}, {0xffffffffu, 0x7fffffffu}, {0x80000000u, 0x80000000u}
    };
    constexpr u32 rd_values[] = {0u, 8u, 9u, 10u};
    bool shape_checked[4]{};

    for (u32 op = 0; op < 4; ++op)
    for (const auto& pair : operands)
    for (const auto& accum : accumulators)
    for (u32 rd : rd_values)
    {
        u64 initial[32]{};
        initial[8] = pair[0];
        initial[9] = pair[1];
        initial[10] = 0x5a5aa5a5u;
        initial[12] = accum[1];
        initial[13] = accum[0];

        const bool upper = op >= 2;
        const u32 banked = upper ? 0x1cu << 26 : 0;
        std::vector<u32> code = {
            upper ? (banked | MipsR(12, 0, 0, 0, 0x11)) : MipsR(12, 0, 0, 0, 0x11),
            upper ? (banked | MipsR(13, 0, 0, 0, 0x13)) : MipsR(13, 0, 0, 0, 0x13),
            (0x1cu << 26) | MipsR(8, 9, rd, 0, opcodes[op])
        };
        char label[160];
        std::snprintf(label, sizeof(label), "EE %s interpreter/JIT full state rs=%08x rt=%08x acc=%08x:%08x rd=%u",
            names[op], static_cast<u32>(pair[0]), static_cast<u32>(pair[1]), accum[1], accum[0], rd);

        const EESnapshot interp = RunEEProgram(false, code, false, initial);
        const bool capture = !shape_checked[op];
        if (capture)
        {
            EmuCoreXOracleResetMaddRange();
            EmuCoreXOracleCaptureMaddRange = true;
        }
        const EESnapshot jit = RunEEProgram(true, code, false, initial);
        if (capture)
            EmuCoreXOracleCaptureMaddRange = false;
        CompareEE(interp, jit, label);

        if (capture)
        {
            u32 words[64]{};
            bool overflow = false;
            const size_t count = EmuCoreXOracleGetMaddRange(words, std::size(words), &overflow);
            u32 long_multiply_count = 0;
            u32 fused_count = 0;
            for (size_t i = 0; i < std::min(count, std::size(words)); ++i)
            {
                if (IsLongMultiplyAccumulate(words[i]))
                {
                    ++long_multiply_count;
                    if (((words[i] >> 10) & 31u) != 31u)
                        ++fused_count;
                }
            }
            std::printf("SHAPE EE %s emitted_words=%zu long_mul_acc=%u fused=%u overflow=%u\n",
                names[op], count, long_multiply_count, fused_count, overflow ? 1u : 0u);
            Check(count > 0 && !overflow && long_multiply_count == 1 && fused_count == 1,
                "EE MADD captures one fused SMADDL/UMADDL in the actual handler range");
            shape_checked[op] = true;
        }
    }
    Check(std::all_of(std::begin(shape_checked), std::end(shape_checked), [](bool checked) { return checked; }),
        "EE MADD probes all four signedness and accumulator-bank forms");

    for (u32 op = 0; op < 4; ++op)
    for (u32 rd : {0u, 10u})
    {
        u64 initial[32]{};
        initial[12] = 0x7fffffffu;
        initial[13] = 0xfffffff0u;
        const bool upper = op >= 2;
        const u32 banked = upper ? 0x1cu << 26 : 0;
        const u32 constant_rs = 0x80000001u;
        const u32 constant_rt = 0xffffffffu;
        std::vector<u32> code = {
            MipsI(15, 0, 8, constant_rs >> 16), MipsI(13, 8, 8, constant_rs & 0xffffu),
            MipsI(15, 0, 9, constant_rt >> 16), MipsI(13, 9, 9, constant_rt & 0xffffu),
            upper ? (banked | MipsR(12, 0, 0, 0, 0x11)) : MipsR(12, 0, 0, 0, 0x11),
            upper ? (banked | MipsR(13, 0, 0, 0, 0x13)) : MipsR(13, 0, 0, 0, 0x13),
            (0x1cu << 26) | MipsR(8, 9, rd, 0, opcodes[op])
        };
        char label[128];
        std::snprintf(label, sizeof(label), "EE %s constant-constant control rd=%u", names[op], rd);
        CompareEE(RunEEProgram(false, code, false, initial), RunEEProgram(true, code, false, initial), label);
    }

    for (u32 op = 0; op < 4; ++op)
    for (u32 rd : {0u, 8u, 16u})
    for (u32 source_style = 0; source_style < 3; ++source_style)
    {
        u64 initial[32]{};
        for (u32 r = 1; r < 32; ++r)
            initial[r] = 0x8100000000000000ull | (u64(r) * 0x01020305u);
        initial[8] = 0x7fffffffu;
        initial[9] = 0x80000001u;
        initial[12] = 0x12345678u;
        initial[13] = 0x87654321u;

        const bool upper = op >= 2;
        const u32 banked = upper ? 0x1cu << 26 : 0;
        std::vector<u32> code = {
            // These values stay live across MADD. Logical host register 4 is SP and
            // reserved, so the allocator maps the first two to W3 and W5.
            MipsI(9, 16, 16, 1), MipsI(9, 17, 17, 2), MipsI(9, 18, 18, 3)
        };
        if (source_style == 1)
        {
            code.push_back(MipsI(15, 0, 8, 0x8123));
            code.push_back(MipsI(13, 8, 8, 0x4567));
        }
        else if (source_style == 2)
        {
            code.push_back(MipsI(15, 0, 9, 0xfedc));
            code.push_back(MipsI(13, 9, 9, 0xba98));
        }
        code.push_back(upper ? (banked | MipsR(12, 0, 0, 0, 0x11)) : MipsR(12, 0, 0, 0, 0x11));
        code.push_back(upper ? (banked | MipsR(13, 0, 0, 0, 0x13)) : MipsR(13, 0, 0, 0, 0x13));
        code.push_back((0x1cu << 26) | MipsR(8, 9, rd, 0, opcodes[op]));
        code.push_back(MipsR(16, 17, 22, 0, 0x21));
        code.push_back(MipsR(18, 22, 23, 0, 0x21));

        char label[160];
        std::snprintf(label, sizeof(label), "EE %s live cached GPR sentinels across MADD rd=%u sources=%u",
            names[op], rd, source_style);
        CompareEE(RunEEProgram(false, code, false, initial), RunEEProgram(true, code, false, initial), label);
    }
}
#endif
