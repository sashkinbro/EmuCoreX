// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// UNWIRED RED draft. Include inside jit_oracle.cpp's anonymous namespace once
// BoundVfMaxMinPlan/PlanBoundVfMaxMinBlock/LowerBoundVfMaxMinPlan exist.
// No production-route assertion is implied by these bound-backend tests.
#include "arm64/ir/IrLower-arm64.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

struct VuMaxMinNativeCosts
{
    u32 full = 0, packed_single = 0, partial = 0;
};
static VuMaxMinNativeCosts s_vu_maxmin_native_costs;

static u32 VuMaxMinExpected(u32 a, u32 b, bool maximum)
{
    // Independent raw sign/magnitude ordering. Do not reuse the native
    // sign-key expression or host float min/max: every NaN payload is data.
    const bool negative_a = (a & 0x80000000u) != 0;
    const bool negative_b = (b & 0x80000000u) != 0;
    bool a_greater;
    if (negative_a != negative_b)
        a_greater = !negative_a;
    else if (negative_a)
        a_greater = (a & 0x7fffffffu) < (b & 0x7fffffffu);
    else
        a_greater = a > b;
    if (a == b)
        return b;
    return maximum == a_greater ? a : b;
}

static ir::Function BoundVfMaxMinFixture(u32 lhs, u32 rhs, u32 dst, u32 mask,
    bool maximum, bool delay_slot = false)
{
    ir::Function fn;
    ir::Builder b(fn);
    b.CreateBlock(0x80);
    b.SetDelaySlot(delay_slot);
    // Exactly 17 nodes. The graph itself specifies the raw-bit operation;
    // there is no opaque helper or opcode tag naming a legacy emitter.
    const u32 a = b.Emit(ir::Op::ReadVfBits, ir::Type::V4I32, {}, lhs); // 0
    const u32 c = b.Emit(ir::Op::ReadVfBits, ir::Type::V4I32, {}, rhs); // 1
    const u32 shift31 = b.ConstI32(31);                              // 2
    const u32 shift1 = b.ConstI32(1);                                // 3
    const u32 sign_a = b.Emit2(ir::Op::VShrS, ir::Type::V4I32, a, shift31); // 4
    const u32 flip_a = b.Emit2(ir::Op::VShrU, ir::Type::V4I32, sign_a, shift1); // 5
    const u32 key_a = b.Emit2(ir::Op::VXor, ir::Type::V4I32, a, flip_a); // 6
    const u32 sign_b = b.Emit2(ir::Op::VShrS, ir::Type::V4I32, c, shift31); // 7
    const u32 flip_b = b.Emit2(ir::Op::VShrU, ir::Type::V4I32, sign_b, shift1); // 8
    const u32 key_b = b.Emit2(ir::Op::VXor, ir::Type::V4I32, c, flip_b); // 9
    const u32 choose_a = b.Emit2(ir::Op::VCmpLtS, ir::Type::V4I32,
        maximum ? key_b : key_a, maximum ? key_a : key_b);             // 10
    const u32 selected_a = b.Emit2(ir::Op::VAnd, ir::Type::V4I32, a, choose_a); // 11
    const u32 choose_b = b.Emit1(ir::Op::VNot, ir::Type::V4I32, choose_a); // 12
    const u32 selected_b = b.Emit2(ir::Op::VAnd, ir::Type::V4I32, c, choose_b); // 13
    const u32 result = b.Emit2(ir::Op::VOr, ir::Type::V4I32, selected_a, selected_b); // 14
    b.Emit1(ir::Op::WriteVfBitsMasked, ir::Type::Void, result, dst,
        static_cast<u16>(mask | (delay_slot ? ir::IF_DELAY_SLOT : 0))); // 15
    b.Return();                                                      // 16
    return fn;
}

static u64 VuMaxMinReadFpcr()
{
    u64 value;
    asm volatile("mrs %0, fpcr" : "=r"(value) : : "memory");
    return value;
}

static u64 VuMaxMinReadFpsr()
{
    u64 value;
    asm volatile("mrs %0, fpsr" : "=r"(value) : : "memory");
    return value;
}

static std::vector<std::array<u32, 8>> VuMaxMinInputPairs()
{
    constexpr std::array<u32, 24> edges = {
        0, 0x80000000u, 1, 0x80000001u, 0x007fffffu, 0x807fffffu,
        0x00800000u, 0x80800000u, 0x3f000000u, 0xbf000000u,
        0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu,
        0x7f800000u, 0xff800000u, 0x7f800001u, 0xff800001u,
        0x7fa12345u, 0xffa54321u, 0x7fc12345u, 0xffc54321u,
        0x7fffffffu, 0xffffffffu};
    std::vector<std::array<u32, 8>> pairs;
    // Each guest lane independently sees the full ordered Cartesian corpus.
    for (u32 a = 0; a < edges.size(); ++a)
    for (u32 b = 0; b < edges.size(); ++b)
    {
        std::array<u32, 8> pair{};
        for (u32 lane = 0; lane < 4; ++lane)
        {
            pair[lane] = edges[(a + lane * 5) % edges.size()];
            pair[4 + lane] = edges[(b + lane * 7) % edges.size()];
        }
        pairs.push_back(pair);
    }
    u32 random = 0x6d2b79f5u;
    for (u32 n = 0; n < 64; ++n)
    {
        std::array<u32, 8> pair{};
        for (u32& word : pair)
        {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            word = random;
        }
        pairs.push_back(pair);
    }
    return pairs;
}

struct VuMaxMinBindings
{
    u32 lhs_vf, rhs_vf, dst_vf;
    u32 lhs_host, rhs_host, dst_host;
};

static bool VuMaxMinDirectMatrix()
{
    using Layout = Arm64Ir::BoundVfMaxMinLayout;
    constexpr std::array<VuMaxMinBindings, 9> bindings = {{
        {7, 8, 9, 3, 4, 5}, {7, 8, 7, 3, 4, 3}, {7, 8, 8, 3, 4, 4},
        {7, 7, 9, 3, 3, 5}, {7, 7, 7, 3, 3, 3}, {7, 7, 9, 3, 4, 5},
        {0, 8, 9, 3, 4, 5}, {7, 0, 9, 3, 4, 5}, {0, 0, 9, 3, 3, 5}}};
    constexpr std::array<FPRoundMode, 4> rounds = {FPRoundMode::Nearest,
        FPRoundMode::PositiveInfinity, FPRoundMode::NegativeInfinity, FPRoundMode::ChopZero};
    const auto inputs = VuMaxMinInputPairs();
    constexpr std::array<u32, 4> vf0 = {0, 0, 0, 0x3f800000u};
    bool correct = true;
    bool reported = false;

    for (bool maximum : {false, true})
    for (u32 mask = 1; mask < 16; ++mask)
    for (const auto& binding : bindings)
    for (Layout layout : {Layout::GuestLanes, Layout::MicroVuPartial})
    {
        Arm64Ir::BoundVfMaxMinPlan plan;
        std::string error;
        const auto fn = BoundVfMaxMinFixture(binding.lhs_vf, binding.rhs_vf,
            binding.dst_vf, mask, maximum);
        if (!Arm64Ir::PlanBoundVfMaxMinBlock(fn, &plan, &error))
        {
            std::printf("VU MAX/MINI matrix plan rejected maximum=%u mask=%u vf=%u/%u/%u: %s\n",
                maximum ? 1u : 0u, mask, binding.lhs_vf, binding.rhs_vf, binding.dst_vf, error.c_str());
            return false;
        }

        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        u8* entry = oakStartBlock();
        // The wrapper temporarily seeds all Q0..Q28. Save ABI-preserved
        // Q8..Q15 first so the C++ caller's low halves remain untouched.
        oakAsm->SUB(oak::util::SP, oak::util::SP, 128);
        for (u32 reg = 8; reg < 16; ++reg)
            oakAsm->STR(oak::QReg(reg), oak::util::SP, oak::POffset<16, 4>((reg - 8) * 16));
        for (u32 reg = 0; reg < 29; ++reg)
            oakLoad128(oak::QReg(reg), {oak::util::X0, static_cast<s64>(reg * 16)});
        const u8* before = oakGetCurrentCodePointer();
        Arm64Ir::LowerOutput out;
        const bool lowered = Arm64Ir::LowerBoundVfMaxMinPlan(plan,
            binding.lhs_host, binding.rhs_host, binding.dst_host, &out, &error, layout);
        const size_t emitted = static_cast<size_t>(oakGetCurrentCodePointer() - before);
        u32* cost = mask == 15 ? &s_vu_maxmin_native_costs.full :
            (layout == Layout::MicroVuPartial ? &s_vu_maxmin_native_costs.packed_single : &s_vu_maxmin_native_costs.partial);
        *cost = std::max(*cost, static_cast<u32>(emitted));
        for (u32 reg = 0; reg < 29; ++reg)
            oakStore128(oak::QReg(reg), {oak::util::X1, static_cast<s64>(reg * 16)});
        for (u32 reg = 8; reg < 16; ++reg)
            oakAsm->LDR(oak::QReg(reg), oak::util::SP, oak::POffset<16, 4>((reg - 8) * 16));
        oakAsm->ADD(oak::util::SP, oak::util::SP, 128);
        oakAsm->RET();
        oakEndBlock();
        correct &= lowered && out.host_size == emitted && out.frame_size == 0 && out.spill_values == 0;
        if (!lowered)
            continue; // Never execute rejected/partially emitted code.

        for (FPRoundMode rounding : rounds)
        for (bool fz : {false, true})
        for (bool dn : {false, true})
        {
            FPControlRegister fpcr = EmuConfig.Cpu.FPUFPCR;
            fpcr.SetRoundMode(rounding);
            fpcr.SetFlushToZero(fz);
            if (dn) fpcr.bitmask |= (1ull << 25);
            else fpcr.bitmask &= ~(1ull << 25);
            const FPControlRegisterBackup restore_fpcr(fpcr);
            for (u32 input_index = 0; input_index < inputs.size(); ++input_index)
            {
                // Exhaust the Cartesian corpus for the canonical full-vector
                // binding in every FPCR mode. Other masks/aliases/layouts use
                // two edge diagonals plus 16 random vectors, keeping this
                // backend matrix bounded without losing any raw edge value.
                const bool exhaustive = mask == 15 && layout == Layout::GuestLanes &&
                    binding.lhs_vf == 7 && binding.rhs_vf == 8 && binding.dst_vf == 9;
                if (!exhaustive)
                {
                    if (input_index < 24 * 24)
                    {
                        const u32 a = input_index / 24, b = input_index % 24;
                        if (a != b && (a + b) % 24 != 0)
                            continue;
                    }
                    else if (input_index >= 24 * 24 + 16)
                        continue;
                }
                const auto& input = inputs[input_index];
                std::array<std::array<u32, 4>, 29> seed{}, actual{};
                for (u32 reg = 0; reg < seed.size(); ++reg)
                    for (u32 lane = 0; lane < 4; ++lane)
                        seed[reg][lane] = 0xa5d37129u ^ (reg * 0x01020408u) ^ (lane * 0x134579bdu);
                std::array<u32, 4> lhs{}, rhs{};
                for (u32 lane = 0; lane < 4; ++lane)
                {
                    lhs[lane] = binding.lhs_vf == 0 ? vf0[lane] : input[lane];
                    rhs[lane] = binding.rhs_vf == 0 ? vf0[lane] :
                        binding.rhs_vf == binding.lhs_vf ? lhs[lane] : input[4 + lane];
                }
                seed[binding.lhs_host] = lhs;
                seed[binding.rhs_host] = rhs;
                auto expected = seed;
                const u64 before_fpcr = VuMaxMinReadFpcr(), before_fpsr = VuMaxMinReadFpsr();
                reinterpret_cast<void (*)(const void*, void*)>(entry)(seed.data(), actual.data());
                correct &= VuMaxMinReadFpcr() == before_fpcr && VuMaxMinReadFpsr() == before_fpsr;
                const bool one_lane = (mask & (mask - 1)) == 0;
                for (u32 lane = 0; lane < 4; ++lane)
                    if (mask & (8u >> lane))
                        expected[binding.dst_host][layout == Layout::MicroVuPartial && one_lane ? 0 : lane] =
                            VuMaxMinExpected(lhs[lane], rhs[lane], maximum);
                for (u32 reg = 0; reg < seed.size(); ++reg)
                for (u32 lane = 0; lane < 4; ++lane)
                {
                    // MicroVuPartial has no promise for unused destination
                    // lanes. Every other caller-owned register is preserved.
                    if (reg == binding.dst_host && layout == Layout::MicroVuPartial &&
                        (one_lane ? lane != 0 : (mask & (8u >> lane)) == 0))
                        continue;
                    if (actual[reg][lane] != expected[reg][lane])
                    {
                        if (!reported)
                        {
                            std::printf("VU MAX/MINI mismatch maximum=%u mask=%u layout=%u vf=%u/%u/%u host=%u/%u/%u fpcr=%llx input=%u q=%u lane=%u expected=%08x actual=%08x\n",
                                maximum ? 1u : 0u, mask, static_cast<u32>(layout), binding.lhs_vf,
                                binding.rhs_vf, binding.dst_vf, binding.lhs_host, binding.rhs_host,
                                binding.dst_host, static_cast<unsigned long long>(before_fpcr), input_index,
                                reg, lane, expected[reg][lane], actual[reg][lane]);
                            reported = true;
                        }
                        correct = false;
                    }
                }
            }
        }
    }
    return correct;
}

static void VUBoundVfMaxMinTests()
{
    using Plan = Arm64Ir::BoundVfMaxMinPlan;
    using Layout = Arm64Ir::BoundVfMaxMinLayout;
    Check(VuMaxMinExpected(0, 0x80000000u, true) == 0 &&
        VuMaxMinExpected(0, 0x80000000u, false) == 0x80000000u &&
        VuMaxMinExpected(0x80000001u, 0x80000000u, true) == 0x80000000u &&
        VuMaxMinExpected(1, 0, true) == 1 &&
        VuMaxMinExpected(0xff800001u, 0xff800000u, true) == 0xff800000u &&
        VuMaxMinExpected(0x7f800001u, 0x7f800000u, true) == 0x7f800001u &&
        VuMaxMinExpected(0xffffffffu, 0x7fffffffu, false) == 0xffffffffu &&
        VuMaxMinExpected(0xdeadbeefu, 0xdeadbeefu, true) == 0xdeadbeefu,
        "VU MAX/MINI independent raw model preserves zeros, subnormals and special payloads");

    bool graphs_valid = true;
    for (bool maximum : {false, true})
    for (u32 mask = 1; mask < 16; ++mask)
    for (bool delay : {false, true})
    for (const std::array<u32, 2> source : {std::array<u32, 2>{7, 8}, {7, 7}, {0, 8}, {7, 0}, {0, 0}})
    {
        std::string error;
        const auto fn = BoundVfMaxMinFixture(source[0], source[1], 9, mask, maximum, delay);
        const bool verified = ir::Verify(fn, &error);
        if (!verified && graphs_valid)
            std::printf("VU MAX/MINI valid graph rejected by verifier: %s\n", error.c_str());
        graphs_valid &= verified;
    }
    Check(graphs_valid, "VU MAX/MINI valid expression graphs pass common IR verification independently of planner admission");

    bool admitted = true;
    for (bool maximum : {false, true})
    for (u32 mask = 1; mask < 16; ++mask)
    for (bool delay : {false, true})
    {
        auto fn = BoundVfMaxMinFixture(7, 8, 9, mask, maximum, delay);
        std::string error;
        Plan plan;
        const bool accepted = Arm64Ir::PlanBoundVfMaxMinBlock(fn, &plan, &error);
        admitted &= accepted && fn.blocks[0].insts.size() == 17 && plan.lhs_vf == 7 &&
            plan.rhs_vf == 8 && plan.dst_vf == 9 && plan.lane_mask == mask &&
            plan.maximum == maximum && plan.delay_slot == delay;
    }
    Check(admitted, "shared VU MAX/MINI planner admits genuine expression graphs, masks and delay metadata");

    bool vf0_accepted = true;
    for (const std::array<u32, 2> source : {std::array<u32, 2>{0, 8}, {7, 0}, {0, 0}})
    {
        Plan plan;
        std::string error;
        vf0_accepted &= Arm64Ir::PlanBoundVfMaxMinBlock(
            BoundVfMaxMinFixture(source[0], source[1], 9, 15, true), &plan, &error) &&
            plan.lhs_vf == source[0] && plan.rhs_vf == source[1];
    }
    Check(vf0_accepted, "shared VU MAX/MINI accepts canonical VF0 sources");

    bool rejected = true;
    std::string error;
    const auto valid = BoundVfMaxMinFixture(7, 8, 9, 15, true);
    const auto reject = [&](const ir::Function& fn) {
        Plan plan{1, 2, 3, 4, true, true};
        // Planner preflight must never emit, even when a caller has an active block.
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        oakStartBlock();
        const u8* before = oakGetCurrentCodePointer();
        std::array<u8, 64> untouched;
        std::memcpy(untouched.data(), before, untouched.size());
        const bool accepted = Arm64Ir::PlanBoundVfMaxMinBlock(fn, &plan, &error);
        rejected &= !accepted && before == oakGetCurrentCodePointer() &&
            std::memcmp(untouched.data(), before, untouched.size()) == 0 &&
            plan.lhs_vf == 0 && plan.rhs_vf == 0 && plan.dst_vf == 0 &&
            plan.lane_mask == 0 && !plan.maximum && !plan.delay_slot;
        oakAsm->RET();
        oakEndBlock();
    };
    for (u32 mask : {0u, 16u}) reject(BoundVfMaxMinFixture(7, 8, 9, mask, true));
    reject(BoundVfMaxMinFixture(32, 8, 9, 15, true));
    reject(BoundVfMaxMinFixture(7, 32, 9, 15, true));
    reject(BoundVfMaxMinFixture(7, 8, 32, 15, true));
    reject(BoundVfMaxMinFixture(7, 8, 0, 15, true));
    for (u32 node : {2u, 3u})
    {
        auto fn = valid;
        fn.blocks[0].insts[node].imm = 2;
        reject(fn);
    }
    for (u32 node : {0u, 1u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u})
    {
        auto fn = valid;
        fn.blocks[0].insts[node].type = ir::Type::V4U32;
        fn.value_types[fn.blocks[0].insts[node].value] = ir::Type::V4U32;
        reject(fn);
    }
    for (u32 node : {4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u})
    {
        auto fn = valid;
        fn.blocks[0].insts[node].args[0] = fn.blocks[0].insts[0].value;
        // Nodes 4,6,11 already consume A. Corrupt their other operand instead.
        if (node == 4 || node == 6 || node == 11)
            fn.blocks[0].insts[node].args[1] = fn.blocks[0].insts[1].value;
        reject(fn);
    }
    {
        auto fn = valid;
        fn.blocks[0].insts[10].op = ir::Op::VCmpLtU;
        reject(fn);
        fn = valid;
        fn.blocks[0].insts[15].op = ir::Op::WriteVfMasked;
        reject(fn);
    }
    for (u32 node = 0; node < valid.blocks[0].insts.size(); ++node)
    {
        auto fn = valid;
        fn.blocks[0].insts[node].aux |= ir::IF_EXACT;
        reject(fn);
        fn = valid;
        fn.blocks[0].insts[node].aux ^= ir::IF_DELAY_SLOT;
        reject(fn);
    }
    {
        auto fn = valid;
        fn.blocks[0].insts[6].imm = 1;
        reject(fn);
        fn = valid;
        fn.blocks[0].insts[7].guest_pc += 4;
        reject(fn);
        fn = valid;
        ir::Inst extra;
        extra.op = ir::Op::Nop;
        fn.blocks[0].insts.insert(fn.blocks[0].insts.end() - 1, extra);
        reject(fn);
        fn = valid;
        ir::Builder b(fn);
        b.CreateBlock(0x90);
        b.Return();
        reject(fn);
    }
    Check(rejected, "shared VU MAX/MINI rejects and clears malformed types, links, masks, indices, metadata and shapes");

    // Construct the expected valid plan directly: rejection tests must run
    // even while the planner is a RED stub and admits nothing.
    const Plan expected_plan{7, 8, 9, 15, true, false};
    bool lower_rejects = true;
    const auto reject_lower = [&](const Plan& plan, u32 lhs, u32 rhs, u32 dst, Layout layout) {
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        oakStartBlock();
        const u8* before = oakGetCurrentCodePointer();
        std::array<u8, 64> untouched;
        std::memcpy(untouched.data(), before, untouched.size());
        Arm64Ir::LowerOutput out{reinterpret_cast<u8*>(1), 1, 1};
        const bool lowered = Arm64Ir::LowerBoundVfMaxMinPlan(plan, lhs, rhs, dst, &out, &error, layout);
        lower_rejects &= !lowered && before == oakGetCurrentCodePointer() && out.entry == nullptr &&
            std::memcmp(untouched.data(), before, untouched.size()) == 0 &&
            out.host_size == 0 && out.frame_size == 0 && out.spill_values == 0;
        oakAsm->RET();
        oakEndBlock();
    };
    for (u32 reg : {29u, 30u, 31u, 32u, 0xffffffffu})
    {
        reject_lower(expected_plan, reg, 4, 5, Layout::GuestLanes);
        reject_lower(expected_plan, 3, reg, 5, Layout::GuestLanes);
        reject_lower(expected_plan, 3, 4, reg, Layout::GuestLanes);
    }
    reject_lower(expected_plan, 3, 3, 5, Layout::GuestLanes); // Distinct guest inputs cannot share one binding.
    reject_lower(expected_plan, 3, 4, 5, static_cast<Layout>(2));
    for (const Plan bad : {Plan{32, 8, 9, 15, true, false}, Plan{7, 32, 9, 15, true, false},
        Plan{7, 8, 0, 15, true, false}, Plan{7, 8, 32, 15, true, false},
        Plan{7, 8, 9, 0, true, false}, Plan{7, 8, 9, 16, true, false}})
        reject_lower(bad, 3, 4, 5, Layout::GuestLanes);
    Check(lower_rejects, "shared VU MAX/MINI rejects invalid plans/bindings/layouts before emitting any bytes");

    bool generic_rejects = true;
    for (Arm64Ir::GuestState state : {Arm64Ir::GuestState::EE, Arm64Ir::GuestState::IOP})
    {
        auto fn = valid;
        std::array<u8, 512> code;
        code.fill(0xa5);
        Arm64Ir::LowerOptions options;
        options.guest_state = state;
        Arm64Ir::LowerOutput out{reinterpret_cast<u8*>(1), 1, 1};
        generic_rejects &= !Arm64Ir::CanLower(fn, false, &error, state);
        const bool lowered = Arm64Ir::LowerBlock(fn, options, code.data(), code.size(), &out, &error);
        generic_rejects &= !lowered && out.entry == nullptr && out.host_size == 0 &&
            std::all_of(code.begin(), code.end(), [](u8 byte) { return byte == 0xa5; });
    }
    Check(generic_rejects, "generic EE/IOP lowering rejects bound VU MAX/MINI without touching its code buffer");

    // Bound code must not access guest state or flags through hidden globals.
    // Preserve the complete caller-owned VU structures around seeded flags.
    VURegs saved_vu0, saved_vu1, seeded_vu0, seeded_vu1;
    std::memcpy(&saved_vu0, &VU0, sizeof(VU0));
    std::memcpy(&saved_vu1, &VU1, sizeof(VU1));
    VU0.VI[REG_MAC_FLAG].UL = 0xa593;
    VU0.VI[REG_STATUS_FLAG].UL = 0xd27;
    VU0.VI[REG_CLIP_FLAG].UL = 0x13579b;
    VU1.VI[REG_MAC_FLAG].UL = 0x5a6c;
    VU1.VI[REG_STATUS_FLAG].UL = 0x6b1;
    VU1.VI[REG_CLIP_FLAG].UL = 0x2468ac;
    std::memcpy(&seeded_vu0, &VU0, sizeof(VU0));
    std::memcpy(&seeded_vu1, &VU1, sizeof(VU1));
    s_vu_maxmin_native_costs = {};
    const bool matrix_ok = admitted && VuMaxMinDirectMatrix();
    const bool state_unchanged = std::memcmp(&seeded_vu0, &VU0, sizeof(VU0)) == 0 &&
        std::memcmp(&seeded_vu1, &VU1, sizeof(VU1)) == 0;
    std::memcpy(&VU0, &saved_vu0, sizeof(VU0));
    std::memcpy(&VU1, &saved_vu1, sizeof(VU1));
    Check(matrix_ok, "shared VU MAX/MINI matches raw model across aliases/layouts/masks/VF0/FPCR and preserves caller SIMD");
    Check(matrix_ok && state_unchanged, "bound VU MAX/MINI leaves canonical flags and complete guest state untouched");
    Check(matrix_ok && s_vu_maxmin_native_costs.full <= 32 &&
        s_vu_maxmin_native_costs.packed_single <= 36 && s_vu_maxmin_native_costs.partial <= 44,
        "bound VU MAX/MINI emits at most 8 full-vector, 9 packed, or 11 partial-vector instructions");
    std::printf("VUMAXMIN_NATIVE max_full_bytes=%u max_micro_partial_bytes=%u max_guest_partial_bytes=%u\n",
        s_vu_maxmin_native_costs.full, s_vu_maxmin_native_costs.packed_single, s_vu_maxmin_native_costs.partial);
}
