// SPDX-License-Identifier: GPL-3.0+
extern "C" u32 EmuCoreXOracleEmitBroadcastClamp(u32 lane, bool overflow, bool reference);
extern "C" u32 EmuCoreXOracleEmitVectorClamp(bool overflow, bool reference);

void VUOperandClampTests()
{
    using Kernel = void (*)(const u32*, u32*, u32);
    alignas(16) static const u32 bounds[8] = {0xff7fffffu, 0xff7fffffu, 0xff7fffffu, 0xff7fffffu,
        0x7f7fffffu, 0x7f7fffffu, 0x7f7fffffu, 0x7f7fffffu};
    constexpr u32 count = 4096;
    std::vector<u32> input(count * 4), output(count * 4), old(count * 4);
    u32 random = 0x13579bdf;
    const auto next = [&]() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    for (u32 lane = 0; lane < 5; ++lane)
    {
        const bool vector = lane == 4;
        for (bool overflow : {false, true})
        {
            u32 body_bytes = 0;
            const auto compile = [&](bool reference, bool chain = false) {
                const u32 offset = (lane * 2 + overflow) * 0x4000 + (reference ? 0x1000 : 0) + (chain ? 0x2000 : 0);
                oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000 + offset, 0x1000);
                u8* entry = oakStartBlock();
                oakMoveAddressToReg(OAK_XSCRATCH, bounds);
                oakAsm->LDP(OAK_QSCRATCH3, OAK_QSCRATCH2, OAK_XSCRATCH);
                if (chain) oakAsm->LDR(oak::util::Q0, oak::util::X0);
                oak::Label loop;
                oakAsm->l(loop);
                if (!chain) oakAsm->LDR(oak::util::Q0, oak::util::X0);
                const u32 bytes = vector ? EmuCoreXOracleEmitVectorClamp(overflow, reference) :
                    EmuCoreXOracleEmitBroadcastClamp(lane, overflow, reference);
                if (!reference) body_bytes = bytes;
                if (chain)
                    oakAsm->MOV(oak::util::Q0.B16(), oak::util::Q1.B16());
                else
                {
                    oakAsm->STR(oak::util::Q1, oak::util::X1);
                    oakAsm->ADD(oak::util::X0, oak::util::X0, 16);
                    oakAsm->ADD(oak::util::X1, oak::util::X1, 16);
                }
                oakAsm->SUBS(oak::util::W2, oak::util::W2, 1);
                oakAsm->B(oak::Cond::NE, loop);
                if (chain) oakAsm->STR(oak::util::Q1, oak::util::X1);
                oakAsm->RET();
                oakEndBlock();
                return reinterpret_cast<Kernel>(entry);
            };
            const Kernel current = compile(false), reference = compile(true);
            Check(body_bytes == (vector ? (overflow ? 32u : 24u) : (overflow ? 40u : 20u)),
                "VU operand clamp emits compact scalar-broadcast or integer-vector normalization");
            bool exact = true;
            for (u32 batch = 0; batch < 64; ++batch)
            {
                for (u32& bits : input) bits = next();
                if (batch == 0)
                    for (u32 i = 0; i < count; ++i)
                        input[4 * i + (lane & 3)] = ((i >> 8) & 1) * 0x80000000u | ((i & 255) << 23) |
                            (((i >> 9) & 7) * 0x124924u);
                if (batch == 0)
                {
                    constexpr u32 edges[] = {0u, 0x80000000u, 1u, 0x80000001u, 0x007fffffu, 0x807fffffu,
                        0x00800000u, 0x80800000u, 0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u,
                        0x7f800001u, 0xff800001u, 0x7fffffffu, 0xffffffffu};
                    for (u32 i = 0; i < std::size(edges); ++i)
                        std::fill_n(input.data() + 4 * (count - 1 - i), 4, edges[i]);
                }
                current(input.data(), output.data(), count);
                reference(input.data(), old.data(), count);
                exact &= output == old;
                for (u32 i = 0; i < count; ++i)
                {
                    for (u32 component = 0; component < 4; ++component)
                    {
                        u32 bits = input[4 * i + (vector ? component : lane)];
                        const u32 exponent = bits & 0x7f800000u;
                        if (!exponent) bits &= 0x80000000u;
                        else if (overflow && exponent == 0x7f800000u) bits = (bits & 0x80000000u) | 0x7f7fffffu;
                        exact &= output[4 * i + component] == bits;
                    }
                }
            }
            Check(exact, "VU operand clamp matches independent exponent/sign bits and the original vector path");
            for (u32 distribution = 0; distribution < 4; ++distribution)
            {
                for (u32 i = 0; i < input.size(); ++i)
                {
                    u32 bits = next();
                    if (distribution == 0) bits = (bits & 0x807fffffu) | (((bits >> 24) % 254 + 1) << 23);
                    else if (distribution == 1) bits = (i & 3) ? 0 : 0x3f800000u;
                    else if (distribution == 2) bits = (bits & 1) ? 0 : 0x3f800000u;
                    input[i] = bits;
                }
                std::array<double, 9> current_times{}, old_times{};
                const auto timed = [&](Kernel kernel, std::vector<u32>& result) {
                    const auto start = std::chrono::steady_clock::now();
                    for (u32 iteration = 0; iteration < 64; ++iteration)
                        kernel(input.data(), result.data(), count);
                    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / (64 * count);
                };
                for (u32 run = 0; run < current_times.size(); ++run)
                {
                    if (run & 1) { old_times[run] = timed(reference, old); current_times[run] = timed(current, output); }
                    else { current_times[run] = timed(current, output); old_times[run] = timed(reference, old); }
                }
                std::sort(current_times.begin(), current_times.end());
                std::sort(old_times.begin(), old_times.end());
                std::printf("VUOPERANDCLAMP kind=%s lane=%u overflow=%u distribution=%u bytes=%u current_median_ns=%.3f reference_median_ns=%.3f\n",
                    vector ? "vector" : "broadcast", lane, overflow, distribution, body_bytes, current_times[4], old_times[4]);
                Check(output == old, "VU operand clamp timing retains exact output for normal, structured, mixed-zero and raw-bit inputs");
            }
            const Kernel current_chain = compile(false, true), old_chain = compile(true, true);
            std::array<u32, 4> chain_result{}, old_result{};
            std::array<double, 9> chain_times{}, old_chain_times{};
            const auto timed_chain = [&](Kernel kernel, std::array<u32, 4>& result) {
                const auto start = std::chrono::steady_clock::now();
                kernel(input.data(), result.data(), 1048576);
                return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 1048576;
            };
            for (u32 run = 0; run < chain_times.size(); ++run)
            {
                if (run & 1) { old_chain_times[run] = timed_chain(old_chain, old_result); chain_times[run] = timed_chain(current_chain, chain_result); }
                else { chain_times[run] = timed_chain(current_chain, chain_result); old_chain_times[run] = timed_chain(old_chain, old_result); }
            }
            std::sort(chain_times.begin(), chain_times.end());
            std::sort(old_chain_times.begin(), old_chain_times.end());
            std::printf("VUOPERANDCLAMP_CHAIN kind=%s lane=%u overflow=%u current_median_ns=%.3f reference_median_ns=%.3f\n",
                vector ? "vector" : "broadcast", lane, overflow, chain_times[4], old_chain_times[4]);
            Check(chain_result == old_result, "VU operand clamp dependent chain preserves the original result");
        }
    }
}
