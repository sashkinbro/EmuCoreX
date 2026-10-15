// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// UNWIRED MAXi/MINIi RED fixture. Include in jit_oracle.cpp's anonymous namespace.
// Requires ReadVuScalarBits(I32, VuScalar::I), VSplat32(I32 -> V4I32),
// and BoundVfMaxMinPlan::rhs_kind with BoundVfMaxMinRhs::ScalarI.
// Entry: VUBoundVfMaxMinITests. No dependencies on the register-form fixture.
// No production-route assertion is implied by these bound-backend tests.
#include "arm64/ir/IrLower-arm64.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

struct VuMaxMinINativeCosts
{
    u32 full = 0, packed_single = 0, partial = 0;
};
static VuMaxMinINativeCosts s_vu_maxmin_i_native_costs;

static u32 VuMaxMinIExpected(u32 a, u32 b, bool maximum)
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

static ir::Function BoundVfMaxMinIFixture(u32 lhs, u32 dst, u32 mask,
    bool maximum, bool delay_slot = false)
{
    ir::Function fn;
    ir::Builder b(fn);
    b.CreateBlock(0x80);
    b.SetDelaySlot(delay_slot);
    // Exactly 18 nodes: one explicit raw RHS lane splat. The graph itself specifies the raw-bit operation;
    // there is no opaque helper or opcode tag naming a legacy emitter.
    const u32 a = b.Emit(ir::Op::ReadVfBits, ir::Type::V4I32, {}, lhs);
    const u32 raw_i = b.Emit(ir::Op::ReadVuScalarBits, ir::Type::I32, {}, static_cast<u64>(ir::VuScalar::I));
    const u32 c = b.Emit1(ir::Op::VSplat32, ir::Type::V4I32, raw_i);
    const u32 shift31 = b.ConstI32(31);
    const u32 shift1 = b.ConstI32(1);
    const u32 sign_a = b.Emit2(ir::Op::VShrS, ir::Type::V4I32, a, shift31);
    const u32 flip_a = b.Emit2(ir::Op::VShrU, ir::Type::V4I32, sign_a, shift1);
    const u32 key_a = b.Emit2(ir::Op::VXor, ir::Type::V4I32, a, flip_a);
    const u32 sign_b = b.Emit2(ir::Op::VShrS, ir::Type::V4I32, c, shift31);
    const u32 flip_b = b.Emit2(ir::Op::VShrU, ir::Type::V4I32, sign_b, shift1);
    const u32 key_b = b.Emit2(ir::Op::VXor, ir::Type::V4I32, c, flip_b);
    const u32 choose_a = b.Emit2(ir::Op::VCmpLtS, ir::Type::V4I32,
        maximum ? key_b : key_a, maximum ? key_a : key_b);
    const u32 selected_a = b.Emit2(ir::Op::VAnd, ir::Type::V4I32, a, choose_a);
    const u32 choose_b = b.Emit1(ir::Op::VNot, ir::Type::V4I32, choose_a);
    const u32 selected_b = b.Emit2(ir::Op::VAnd, ir::Type::V4I32, c, choose_b);
    const u32 result = b.Emit2(ir::Op::VOr, ir::Type::V4I32, selected_a, selected_b);
    b.Emit1(ir::Op::WriteVfBitsMasked, ir::Type::Void, result, dst,
        static_cast<u16>(mask | (delay_slot ? ir::IF_DELAY_SLOT : 0)));
    b.Return();
    return fn;
}

static u64 VuMaxMinIReadFpcr()
{
    u64 value;
    asm volatile("mrs %0, fpcr" : "=r"(value) : : "memory");
    return value;
}

static u64 VuMaxMinIReadFpsr()
{
    u64 value;
    asm volatile("mrs %0, fpsr" : "=r"(value) : : "memory");
    return value;
}

static std::vector<std::array<u32, 8>> VuMaxMinIInputPairs()
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

struct VuMaxMinIBindings
{
    u32 lhs_vf, dst_vf;
    u32 lhs_host, i_host, dst_host;
};

static bool VuMaxMinIDirectMatrix()
{
    using Layout = Arm64Ir::BoundVfMaxMinLayout;
    constexpr std::array<VuMaxMinIBindings, 6> bindings = {{
        {7, 9, 3, 4, 5}, {7, 7, 3, 4, 3}, {0, 9, 3, 4, 5},
        {7, 9, 0, 28, 27}, {7, 7, 28, 0, 28}, {0, 9, 27, 28, 0}}};
    constexpr std::array<FPRoundMode, 4> rounds = {FPRoundMode::Nearest,
        FPRoundMode::PositiveInfinity, FPRoundMode::NegativeInfinity, FPRoundMode::ChopZero};
    const auto inputs = VuMaxMinIInputPairs();
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
        const auto fn = BoundVfMaxMinIFixture(binding.lhs_vf, binding.dst_vf, mask, maximum);
        if (!Arm64Ir::PlanBoundVfMaxMinBlock(fn, &plan, &error))
        {
            std::printf("VU MAXi/MINIi matrix plan rejected maximum=%u mask=%u vf=%u/%u: %s\n",
                maximum ? 1u : 0u, mask, binding.lhs_vf, binding.dst_vf, error.c_str());
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
            binding.lhs_host, binding.i_host, binding.dst_host, &out, &error, layout);
        const size_t emitted = static_cast<size_t>(oakGetCurrentCodePointer() - before);
        u32* cost = mask == 15 ? &s_vu_maxmin_i_native_costs.full :
            (layout == Layout::MicroVuPartial ? &s_vu_maxmin_i_native_costs.packed_single : &s_vu_maxmin_i_native_costs.partial);
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
                    binding.lhs_vf == 7 && binding.dst_vf == 9 && binding.lhs_host == 3;
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
                std::array<u32, 4> lhs{};
                for (u32 lane = 0; lane < 4; ++lane)
                    lhs[lane] = binding.lhs_vf == 0 ? vf0[lane] : input[lane];
                const u32 scalar_i = input[4];
                // Only S0 is I. Poisoned upper lanes catch a missing VSplat32.
                seed[binding.lhs_host] = lhs;
                seed[binding.i_host] = {scalar_i, scalar_i ^ 0x80000000u,
                    scalar_i ^ 1u, scalar_i ^ 0xffffffffu};
                auto expected = seed;
                const u64 before_fpcr = VuMaxMinIReadFpcr(), before_fpsr = VuMaxMinIReadFpsr();
                reinterpret_cast<void (*)(const void*, void*)>(entry)(seed.data(), actual.data());
                correct &= VuMaxMinIReadFpcr() == before_fpcr && VuMaxMinIReadFpsr() == before_fpsr;
                const bool one_lane = (mask & (mask - 1)) == 0;
                for (u32 lane = 0; lane < 4; ++lane)
                    if (mask & (8u >> lane))
                        expected[binding.dst_host][layout == Layout::MicroVuPartial && one_lane ? 0 : lane] =
                            VuMaxMinIExpected(lhs[lane], scalar_i, maximum);
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
                            std::printf("VU MAXi/MINIi mismatch maximum=%u mask=%u layout=%u vf=%u/%u host=%u/%u/%u fpcr=%llx input=%u q=%u lane=%u expected=%08x actual=%08x\n",
                                maximum ? 1u : 0u, mask, static_cast<u32>(layout), binding.lhs_vf,
                                binding.dst_vf, binding.lhs_host, binding.i_host,
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

static void VUBoundVfMaxMinITests()
{
    using Plan = Arm64Ir::BoundVfMaxMinPlan;
    using Layout = Arm64Ir::BoundVfMaxMinLayout;
    using Rhs = Arm64Ir::BoundVfMaxMinRhs;
    Check(VuMaxMinIExpected(0, 0x80000000u, true) == 0 &&
        VuMaxMinIExpected(0, 0x80000000u, false) == 0x80000000u &&
        VuMaxMinIExpected(0x80000001u, 0x80000000u, true) == 0x80000000u &&
        VuMaxMinIExpected(1, 0, true) == 1 &&
        VuMaxMinIExpected(0xff800001u, 0xff800000u, true) == 0xff800000u &&
        VuMaxMinIExpected(0x7f800001u, 0x7f800000u, true) == 0x7f800001u &&
        VuMaxMinIExpected(0xffffffffu, 0x7fffffffu, false) == 0xffffffffu &&
        VuMaxMinIExpected(0xdeadbeefu, 0xdeadbeefu, true) == 0xdeadbeefu,
        "VU MAXi/MINIi independent raw model preserves zeros, subnormals and special payloads");

    bool graphs_valid = true;
    for (bool maximum : {false, true})
    for (u32 mask = 1; mask < 16; ++mask)
    for (bool delay : {false, true})
    for (u32 lhs_vf : {0u, 7u, 31u})
    {
        std::string error;
        const auto fn = BoundVfMaxMinIFixture(lhs_vf, 9, mask, maximum, delay);
        const bool verified = ir::Verify(fn, &error);
        if (!verified && graphs_valid)
            std::printf("VU MAXi/MINIi valid graph rejected by verifier: %s\n", error.c_str());
        graphs_valid &= verified;
    }
    Check(graphs_valid, "VU MAXi/MINIi valid expression graphs pass common IR verification independently of planner admission");

    bool admitted = true;
    for (bool maximum : {false, true})
    for (u32 mask = 1; mask < 16; ++mask)
    for (bool delay : {false, true})
    {
        auto fn = BoundVfMaxMinIFixture(7, 9, mask, maximum, delay);
        std::string error;
        Plan plan;
        const bool accepted = Arm64Ir::PlanBoundVfMaxMinBlock(fn, &plan, &error);
        admitted &= accepted && fn.blocks[0].insts.size() == 18 && plan.lhs_vf == 7 &&
            plan.rhs_vf == 0 && plan.dst_vf == 9 && plan.lane_mask == mask &&
            plan.maximum == maximum && plan.delay_slot == delay && plan.rhs_lane == 4 && plan.rhs_kind == Rhs::ScalarI;
    }
    Check(admitted, "shared VU MAXi/MINIi planner admits genuine expression graphs, masks and delay metadata");

    bool vf0_accepted = true;
    for (bool maximum : {false, true})
    {
        Plan plan;
        std::string error;
        vf0_accepted &= Arm64Ir::PlanBoundVfMaxMinBlock(
            BoundVfMaxMinIFixture(0, 9, 15, maximum), &plan, &error) &&
            plan.lhs_vf == 0 && plan.rhs_vf == 0 && plan.rhs_lane == 4 && plan.rhs_kind == Rhs::ScalarI;
    }
    Check(vf0_accepted, "shared VU MAXi/MINIi accepts canonical VF0 source");

    bool rejected = true;
    std::string error;
    const auto valid = BoundVfMaxMinIFixture(7, 9, 15, true);
    const auto reject = [&](const ir::Function& fn) {
        Plan plan{1, 2, 3, 4, true, true, 2, Rhs::ScalarI};
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        oakStartBlock();
        const u8* before = oakGetCurrentCodePointer();
        std::array<u8, 64> untouched;
        std::memcpy(untouched.data(), before, untouched.size());
        const bool accepted = Arm64Ir::PlanBoundVfMaxMinBlock(fn, &plan, &error);
        rejected &= !accepted && before == oakGetCurrentCodePointer() &&
            std::memcmp(untouched.data(), before, untouched.size()) == 0 &&
            plan.lhs_vf == 0 && plan.rhs_vf == 0 && plan.dst_vf == 0 &&
            plan.lane_mask == 0 && !plan.maximum && !plan.delay_slot && plan.rhs_lane == 4 && plan.rhs_kind == Rhs::Vector;
        oakAsm->RET();
        oakEndBlock();
    };
    for (u32 mask : {0u, 16u})
        reject(BoundVfMaxMinIFixture(7, 9, mask, true));
    reject(BoundVfMaxMinIFixture(32, 9, 15, true));
    reject(BoundVfMaxMinIFixture(7, 32, 15, true));
    reject(BoundVfMaxMinIFixture(7, 0, 15, true));
    for (u64 selector : {1ull, 2ull, 16ull, 21ull, 31ull, 32ull, 0x100000000ull, 0xffffffffffffffffull})
    {
        auto fn = valid;
        fn.blocks[0].insts[1].imm = selector;
        reject(fn);
    }
    for (u64 immediate : {1ull, 0x55ull, 0xffffffffull})
    {
        auto fn = valid;
        fn.blocks[0].insts[2].imm = immediate;
        reject(fn);
    }
    for (ir::Type type : {ir::Type::F32, ir::Type::I64, ir::Type::V4I32})
    {
        auto fn = valid;
        fn.blocks[0].insts[1].type = type;
        fn.value_types[fn.blocks[0].insts[1].value] = type;
        reject(fn);
    }
    for (u32 node : {3u, 4u})
    {
        auto fn = valid;
        fn.blocks[0].insts[node].imm = 2;
        reject(fn);
    }
    for (u32 node : {0u, 2u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u})
    {
        auto fn = valid;
        fn.blocks[0].insts[node].type = ir::Type::V4U32;
        fn.value_types[fn.blocks[0].insts[node].value] = ir::Type::V4U32;
        reject(fn);
    }
    // Change each value operand to another already-defined value. This covers
    // splatting LHS, bypassing the scalar splat, and select rewiring.
    for (u32 node = 2; node <= 16; ++node)
    for (u32 arg = 0; arg < valid.blocks[0].insts[node].num_args; ++arg)
    {
        auto fn = valid;
        auto& inst = fn.blocks[0].insts[node];
        const u32 a = fn.blocks[0].insts[0].value, b = fn.blocks[0].insts[1].value;
        inst.args[arg] = inst.args[arg] == a ? b : a;
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
        fn = valid;
        fn.blocks[0].insts[node].guest_pc += 4;
        reject(fn);
        fn = valid;
        auto& inst = fn.blocks[0].insts[node];
        if (inst.num_args < 4)
        {
            inst.args[inst.num_args] = fn.blocks[0].insts[0].value;
            reject(fn);
        }
    }
    {
        auto fn = valid;
        fn.blocks[0].insts[11].op = ir::Op::VCmpLtU;
        reject(fn);
        fn = valid;
        fn.blocks[0].insts[16].op = ir::Op::WriteVfMasked;
        reject(fn);
        fn = valid;
        fn.blocks[0].insts[7].imm = 1;
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
    {
        auto fn = valid;
        fn.blocks[0].flags = 1;
        reject(fn);
        fn = valid;
        fn.blocks[0].guest_end += 4;
        reject(fn);
        fn = valid;
        fn.guest_entry = 0x84;
        reject(fn);
        fn = valid;
        fn.blocks[0].insts[2].num_args = 2;
        fn.blocks[0].insts[2].args[1] = fn.blocks[0].insts[0].value;
        reject(fn);
    }
    Check(rejected, "shared VU MAXi/MINIi rejects malformed selectors, types, links, masks, metadata and shapes without emission");

    // Rejection runs even while admission is a RED stub.
    const Plan expected_plan{7, 0, 9, 15, true, false, 4, Rhs::ScalarI};
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
    reject_lower(expected_plan, 3, 3, 5, Layout::GuestLanes);
    // VF0 and ScalarI both carry numeric zero fields, but are distinct state.
    reject_lower(Plan{0, 0, 9, 15, true, false, 4, Rhs::ScalarI}, 3, 3, 5, Layout::GuestLanes);
    reject_lower(expected_plan, 3, 4, 4, Layout::GuestLanes); // I binding is read-only.
    reject_lower(expected_plan, 3, 4, 4, Layout::MicroVuPartial);
    reject_lower(expected_plan, 3, 4, 5, static_cast<Layout>(2));
    for (const Plan bad : {
        Plan{32, 0, 9, 15, true, false, 4, Rhs::ScalarI},
        Plan{7, 1, 9, 15, true, false, 4, Rhs::ScalarI},
        Plan{7, 33, 9, 15, true, false, 4, Rhs::ScalarI},
        Plan{7, 0, 0, 15, true, false, 4, Rhs::ScalarI},
        Plan{7, 0, 32, 15, true, false, 4, Rhs::ScalarI},
        Plan{7, 0, 9, 0, true, false, 4, Rhs::ScalarI},
        Plan{7, 0, 9, 16, true, false, 4, Rhs::ScalarI},
        Plan{7, 0, 9, 15, true, false, 0, Rhs::ScalarI},
        Plan{7, 0, 9, 15, true, false, 3, Rhs::ScalarI},
        Plan{7, 0, 9, 15, true, false, 5, Rhs::ScalarI},
        Plan{7, 0, 9, 15, true, false, 4, static_cast<Rhs>(2)}})
        reject_lower(bad, 3, 4, 5, Layout::GuestLanes);
    Check(lower_rejects, "shared VU MAXi/MINIi rejects invalid plans/bindings/layouts before emitting bytes");

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
    Check(generic_rejects, "generic EE/IOP lowering rejects bound VU MAXi/MINIi without touching its code buffer");

    bool scalar_graph_valid = true, scalar_controls_supported = true, scalar_domains_reject = true;
    for (Arm64Ir::GuestState state : {Arm64Ir::GuestState::EE, Arm64Ir::GuestState::IOP})
    {
        // Keep ReadI live without involving any unsupported vector operation.
        ir::Function scalar;
        ir::Builder b(scalar);
        b.CreateBlock(0x80);
        const u32 value = b.Emit(ir::Op::ReadVuScalarBits, ir::Type::I32, {},
            static_cast<u64>(ir::VuScalar::I));
        b.Emit1(ir::Op::WriteGpr, ir::Type::Void, value, 7);
        b.Resume(0x84);
        scalar_graph_valid &= ir::Verify(scalar, &error);
        // The identical ordinary scalar graph must be admissible when its
        // I read is replaced with a constant. This isolates the domain gate.
        auto control = scalar;
        control.blocks[0].insts[0].op = ir::Op::ConstI32;
        control.blocks[0].insts[0].imm = 0x12345678u;
        scalar_controls_supported &= ir::Verify(control, &error) &&
            Arm64Ir::CanLower(control, false, &error, state);
        std::array<u8, 512> code;
        code.fill(0xa5);
        Arm64Ir::LowerOptions options;
        options.guest_state = state;
        Arm64Ir::LowerOutput out{reinterpret_cast<u8*>(1), 1, 1};
        scalar_domains_reject &= !Arm64Ir::CanLower(scalar, false, &error, state);
        const bool lowered = Arm64Ir::LowerBlock(scalar, options, code.data(), code.size(), &out, &error);
        scalar_domains_reject &= !lowered && out.entry == nullptr && out.host_size == 0 &&
            out.frame_size == 0 && out.spill_values == 0 &&
            std::all_of(code.begin(), code.end(), [](u8 byte) { return byte == 0xa5; });
    }
    Check(scalar_graph_valid && scalar_controls_supported,
        "VU I scalar-only domain probes verify and their constant controls admit in EE/IOP");
    Check(scalar_domains_reject,
        "generic EE/IOP rejects a live scalar ReadI even without vector operations, emitting no bytes");

    // Bound code must not access guest state or flags through hidden globals.
    // Preserve the complete caller-owned VU structures around seeded flags.
    VURegs saved_vu0, saved_vu1, seeded_vu0, seeded_vu1;
    std::memcpy(&saved_vu0, &VU0, sizeof(VU0));
    std::memcpy(&saved_vu1, &VU1, sizeof(VU1));
    VU0.VI[REG_I].UL = 0x12345678u;
    VU1.VI[REG_I].UL = 0x87654321u;
    VU0.VI[REG_MAC_FLAG].UL = 0xa593;
    VU0.VI[REG_STATUS_FLAG].UL = 0xd27;
    VU0.VI[REG_CLIP_FLAG].UL = 0x13579b;
    VU1.VI[REG_MAC_FLAG].UL = 0x5a6c;
    VU1.VI[REG_STATUS_FLAG].UL = 0x6b1;
    VU1.VI[REG_CLIP_FLAG].UL = 0x2468ac;
    std::memcpy(&seeded_vu0, &VU0, sizeof(VU0));
    std::memcpy(&seeded_vu1, &VU1, sizeof(VU1));
    s_vu_maxmin_i_native_costs = {};
    const u64 saved_fpsr = VuMaxMinIReadFpsr();
    const u64 seeded_fpsr = saved_fpsr | 0x0800009full;
    asm volatile("msr fpsr, %0" : : "r"(seeded_fpsr) : "memory");
    const bool matrix_ok = admitted && VuMaxMinIDirectMatrix();
    asm volatile("msr fpsr, %0" : : "r"(saved_fpsr) : "memory");
    const bool state_unchanged = std::memcmp(&seeded_vu0, &VU0, sizeof(VU0)) == 0 &&
        std::memcmp(&seeded_vu1, &VU1, sizeof(VU1)) == 0;
    std::memcpy(&VU0, &saved_vu0, sizeof(VU0));
    std::memcpy(&VU1, &saved_vu1, sizeof(VU1));
    Check(matrix_ok, "shared VU MAXi/MINIi matches raw model across aliases/layouts/masks/VF0/FPCR and preserves caller SIMD");
    Check(matrix_ok && state_unchanged, "bound VU MAXi/MINIi leaves canonical flags and complete guest state untouched");
    Check(matrix_ok && s_vu_maxmin_i_native_costs.full <= 36 &&
        s_vu_maxmin_i_native_costs.packed_single <= 40 && s_vu_maxmin_i_native_costs.partial <= 48,
        "bound VU MAXi/MINIi emits at most 9 full-vector, 10 packed, or 12 partial-vector instructions");
    std::printf("VUMAXMIN_I_NATIVE max_full_bytes=%u max_micro_partial_bytes=%u max_guest_partial_bytes=%u\n",
        s_vu_maxmin_i_native_costs.full, s_vu_maxmin_i_native_costs.packed_single, s_vu_maxmin_i_native_costs.partial);
}
