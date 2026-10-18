// SPDX-License-Identifier: GPL-3.0+
extern "C" u32 EmuCoreXOracleEmitDivNonzeroTest();

void VUDivZeroTests()
{
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate VU DIV zero-test code memory");
        return;
    }
    using Kernel = void (*)(u32, u32*, u32);
    auto compile = [&](u32 offset, bool reference, u32 unroll, u32& body_bytes) {
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000 + offset, 0x10000);
        u8* entry = oakStartBlock();
        oakAsm->MRS(oak::util::X3, oak::SystemReg::FPSR);
        oakAsm->MSR(oak::SystemReg::FPSR, oak::util::XZR);
        oakAsm->FMOV(oak::SReg(0), oak::util::W0);
        oak::Label loop;
        oakAsm->l(loop);
        body_bytes = 0;
        for (u32 operation = 0; operation < unroll; ++operation)
        {
            if (reference)
            {
                // Original DIV predicate: EQ after CMP means a nonzero input.
                oakAsm->EOR(oak::QReg(1).B16(), oak::QReg(1).B16(), oak::QReg(1).B16());
                oakAsm->FCMEQ(oak::SReg(1), oak::SReg(1), oak::SReg(0));
                oakAsm->FMOV(oak::util::W0, oak::SReg(1));
                oakAsm->CMP(oak::util::W0, 0);
                oakAsm->CSET(oak::util::W4, oak::Cond::EQ);
            }
            else
                body_bytes += EmuCoreXOracleEmitDivNonzeroTest();
        }
        oakAsm->SUBS(oak::util::W2, oak::util::W2, 1);
        oakAsm->B(oak::Cond::NE, loop);
        oakAsm->MRS(oak::util::X2, oak::SystemReg::FPSR);
        oakAsm->STR(oak::util::W4, oak::util::X1);
        oakAsm->STR(oak::util::W2, oak::util::X1, oak::POffset<14, 2>(4));
        oakAsm->MSR(oak::SystemReg::FPSR, oak::util::X3);
        oakAsm->RET();
        oakEndBlock();
        return reinterpret_cast<Kernel>(entry);
    };
    u32 bytes = 0, unused = 0;
    const Kernel current = compile(0, false, 1, bytes);
    const Kernel reference = compile(0x1000, true, 1, unused);
    Check(bytes == 8, "VU DIV nonzero predicate uses one FP comparison and one test-result instruction");
    std::vector<u32> inputs = {0u, 0x80000000u, 1u, 0x80000001u, 0x007fffffu, 0x807fffffu,
        0x00800000u, 0x80800000u, 0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu,
        0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00000u, 0x7fc12345u, 0xffc12345u,
        0x7f800001u, 0xff800001u, 0x7fffffffu, 0xffffffffu};
    u32 random = 0x13579bdf;
    for (u32 sample = 0; sample < 65536; ++sample)
    {
        random = random * 1664525u + 1013904223u;
        inputs.push_back(random);
    }
    for (u32 mode = 0; mode < 16; ++mode)
    {
        const FPControlRegister config{static_cast<u64>(mode & 3) << 22 |
            static_cast<u64>((mode >> 2) & 1) << 24 | static_cast<u64>((mode >> 3) & 1) << 25};
        const FPControlRegisterBackup fpcr(config);
        bool predicates_match = true, flags_match = true;
        for (const u32 raw : inputs)
        {
            std::array<u32, 2> actual{}, old{};
            current(raw, actual.data(), 1);
            reference(raw, old.data(), 1);
            const u32 magnitude = raw & 0x7fffffffu;
            const bool expected = magnitude != 0 && !((mode & 4) && magnitude < 0x00800000u);
            predicates_match &= actual[0] == expected && old[0] == expected;
            flags_match &= actual[1] == old[1];
        }
        std::printf("VUDIVZERO fpcr=0x%llx inputs=%zu\n", static_cast<unsigned long long>(config.bitmask), inputs.size());
        Check(predicates_match, "VU DIV nonzero predicate matches raw-bit expectation including signed zero, denormals and NaNs");
        Check(flags_match, "VU DIV nonzero predicate preserves original FPSR exception flags");
    }
    const FPControlRegisterBackup fpcr(FPControlRegister{3ull << 22 | 1ull << 24});
    const Kernel current_loop = compile(0x2000, false, 16, unused);
    const Kernel reference_loop = compile(0x3000, true, 16, unused);
    std::array<double, 9> current_times{}, reference_times{};
    bool same = true;
    auto timed = [&](Kernel kernel) {
        std::array<u32, 2> output{};
        const auto start = std::chrono::steady_clock::now();
        kernel(0x40000000u, output.data(), 65536);
        const auto end = std::chrono::steady_clock::now();
        same &= output[0] == 1 && output[1] == 0;
        return std::chrono::duration<double, std::nano>(end - start).count() / (65536 * 16);
    };
    for (u32 batch = 0; batch < current_times.size(); ++batch)
    {
        if (batch & 1) { reference_times[batch] = timed(reference_loop); current_times[batch] = timed(current_loop); }
        else { current_times[batch] = timed(current_loop); reference_times[batch] = timed(reference_loop); }
    }
    std::sort(current_times.begin(), current_times.end());
    std::sort(reference_times.begin(), reference_times.end());
    std::printf("VUDIVZERO_BENCH predicate_bytes=%u current_median_ns=%.3f reference_median_ns=%.3f\n",
        bytes, current_times[4], reference_times[4]);
    Check(same, "VU DIV zero-test benchmark preserves predicate and FPSR");
}
