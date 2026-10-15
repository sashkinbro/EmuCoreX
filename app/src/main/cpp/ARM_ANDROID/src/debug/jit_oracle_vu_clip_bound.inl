// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Bound CLIP verifier, raw-state, register-preservation and cost fixtures.
// Requires VClip(V4I32,V4I32)->I32, ReadVuClip/WriteVuClip logical history,
// and PlanBoundVfClipBlock/LowerBoundVfClipPlan. No production route claim.
#include "arm64/ir/IrLower-arm64.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

static u32 VuClipExpectedMask(const std::array<u32, 4>& fs, u32 ft_w)
{
    // Independent sign/magnitude model, with no host floating arithmetic or
    // signed-XOR comparison keys. Exponent-zero Fs lanes never clip; an
    // exponent-zero |Ft.w| has the largest subnormal as its threshold.
    const u32 magnitude_w = ft_w & 0x7fffffffu;
    const u32 threshold = magnitude_w < 0x00800000u ? 0x007fffffu : magnitude_w;
    u32 result = 0;
    for (u32 axis = 0; axis < 3; ++axis)
    {
        const u32 magnitude = fs[axis] & 0x7fffffffu;
        if (magnitude >= 0x00800000u && magnitude > threshold)
            result |= 1u << (axis * 2 + (fs[axis] >> 31));
    }
    return result;
}

static u32 VuClipExpectedHistory(u32 old, const std::array<u32, 4>& fs, u32 ft_w)
{
    // Compute in a wider unsigned type so truncation is explicit and separate
    // from the I32 Shl/Or/And graph under test.
    return static_cast<u32>((static_cast<u64>(old) * 64 + VuClipExpectedMask(fs, ft_w)) % 0x1000000ull);
}

static ir::Function BoundVfClipFixture(u32 lhs, u32 rhs, bool delay = false)
{
    ir::Function fn;
    ir::Builder b(fn);
    b.CreateBlock(0x80);
    b.SetDelaySlot(delay);
    const u32 fs = b.Emit(ir::Op::ReadVfBits, ir::Type::V4I32, {}, lhs);
    const u32 ft = b.Emit(ir::Op::ReadVfBits, ir::Type::V4I32, {}, rhs);
    const u32 six_bits = b.Emit2(ir::Op::VClip, ir::Type::I32, fs, ft);
    const u32 old = b.Emit(ir::Op::ReadVuClip, ir::Type::I32, {}, 0);
    const u32 shift = b.ConstI32(6);
    const u32 history = b.Emit2(ir::Op::Shl, ir::Type::I32, old, shift);
    const u32 joined = b.Emit2(ir::Op::Or, ir::Type::I32, history, six_bits);
    const u32 mask = b.ConstI32(0xffffff);
    const u32 result = b.Emit2(ir::Op::And, ir::Type::I32, joined, mask);
    b.Emit1(ir::Op::WriteVuClip, ir::Type::Void, result, 0);
    b.Return();
    return fn;
}

static u64 VuClipReadFpcr()
{
    u64 value;
    asm volatile("mrs %0, fpcr" : "=r"(value) : : "memory");
    return value;
}

static u64 VuClipReadFpsr()
{
    u64 value;
    asm volatile("mrs %0, fpsr" : "=r"(value) : : "memory");
    return value;
}

struct VuClipBinding
{
    u32 lhs_vf, rhs_vf, lhs_host, rhs_host, clip_host;
};

static u32 s_vu_clip_body_bytes;

static bool VuClipBoundMatrix()
{
    constexpr std::array<u32, 24> edges = {0, 0x80000000u, 1, 0x80000001u,
        0x007ffffeu, 0x007fffffu, 0x807fffffu, 0x00800000u, 0x80800000u,
        0x3f7fffffu, 0x3f800000u, 0x3f800001u, 0xbf7fffffu, 0xbf800000u, 0xbf800001u,
        0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u,
        0x7f800001u, 0xff800001u, 0x7fc12345u, 0x7fffffffu, 0xffffffffu};
    constexpr std::array<u32, 7> histories = {0, 1, 0x3f, 0x03ffff, 0xffffff, 0xffabcdef, 0xffffffff};
    constexpr std::array<u32, 4> vf0 = {0, 0, 0, 0x3f800000u};
    constexpr std::array<FPRoundMode, 4> rounds = {FPRoundMode::Nearest,
        FPRoundMode::PositiveInfinity, FPRoundMode::NegativeInfinity, FPRoundMode::ChopZero};
    std::vector<VuClipBinding> bindings;
    for (u32 clip = 0; clip < 16; ++clip) bindings.push_back({7, 8, 3, 4, clip});
    for (const VuClipBinding binding : {
        VuClipBinding{7, 7, 3, 3, 15}, VuClipBinding{7, 7, 0, 28, 0},
        VuClipBinding{7, 8, 28, 0, 2}, VuClipBinding{0, 8, 0, 28, 3},
        VuClipBinding{7, 0, 28, 0, 15}, VuClipBinding{0, 0, 28, 28, 1},
        VuClipBinding{31, 31, 27, 27, 14}})
        bindings.push_back(binding);
    bool correct = true, reported = false;
    for (u32 binding_index = 0; binding_index < bindings.size(); ++binding_index)
    {
        const auto& binding = bindings[binding_index];
        Arm64Ir::BoundVfClipPlan plan;
        std::string error;
        if (!Arm64Ir::PlanBoundVfClipBlock(BoundVfClipFixture(binding.lhs_vf,
            binding.rhs_vf, (binding_index & 1) != 0), &plan, &error))
        {
            std::printf("VU CLIP bound matrix admission failed: %s\n", error.c_str());
            return false;
        }
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        u8* entry = oakStartBlock();
        // Four C ABI arguments: Q seeds/results, then X0..X15 seeds/results.
        // Keep their pointers in saved callee registers, not scratch or inputs.
        oakAsm->SUB(oak::util::SP, oak::util::SP, 224);
        oakAsm->STP(oak::util::X19, oak::util::X20, oak::util::SP, oak::SOffset<10, 3>(128));
        oakAsm->STP(oak::util::X21, oak::util::X22, oak::util::SP, oak::SOffset<10, 3>(144));
        oakAsm->STP(oak::util::X23, oak::util::X24, oak::util::SP, oak::SOffset<10, 3>(160));
        oakAsm->STP(oak::util::X25, oak::util::X26, oak::util::SP, oak::SOffset<10, 3>(176));
        oakAsm->STP(oak::util::X27, oak::util::X28, oak::util::SP, oak::SOffset<10, 3>(192));
        oakAsm->STP(oak::util::X29, oak::util::X30, oak::util::SP, oak::SOffset<10, 3>(208));
        oakAsm->MOV(oak::util::X19, oak::util::X0);
        oakAsm->MOV(oak::util::X20, oak::util::X1);
        oakAsm->MOV(oak::util::X21, oak::util::X2);
        oakAsm->MOV(oak::util::X22, oak::util::X3);
        for (u32 reg = 8; reg < 16; ++reg)
            oakAsm->STR(oak::QReg(reg), oak::util::SP, oak::POffset<16, 4>((reg - 8) * 16));
        for (u32 reg = 0; reg < 29; ++reg)
            oakLoad128(oak::QReg(reg), {oak::util::X19, static_cast<s64>(reg * 16)});
        for (u32 reg = 0; reg < 16; ++reg)
            oakLoad64(oak::XReg(reg), {oak::util::X21, static_cast<s64>(reg * 8)});
        // Snapshot, without reseeding, platform/callee/frame/link registers.
        // X19..X22 currently contain wrapper pointers and must also survive.
        for (u32 reg = 18; reg < 31; ++reg)
            oakStore64(oak::XReg(reg), {oak::util::X21, static_cast<s64>(reg * 8)});
        const u8* before = oakGetCurrentCodePointer();
        Arm64Ir::LowerOutput out;
        const bool lowered = Arm64Ir::LowerBoundVfClipPlan(plan, binding.lhs_host,
            binding.rhs_host, binding.clip_host, &out, &error);
        const u32 emitted = static_cast<u32>(oakGetCurrentCodePointer() - before);
        s_vu_clip_body_bytes = std::max(s_vu_clip_body_bytes, emitted);
        for (u32 reg = 0; reg < 16; ++reg)
            oakStore64(oak::XReg(reg), {oak::util::X22, static_cast<s64>(reg * 8)});
        for (u32 reg = 18; reg < 31; ++reg)
            oakStore64(oak::XReg(reg), {oak::util::X22, static_cast<s64>(reg * 8)});
        for (u32 reg = 0; reg < 29; ++reg)
            oakStore128(oak::QReg(reg), {oak::util::X20, static_cast<s64>(reg * 16)});
        for (u32 reg = 8; reg < 16; ++reg)
            oakAsm->LDR(oak::QReg(reg), oak::util::SP, oak::POffset<16, 4>((reg - 8) * 16));
        oakAsm->LDP(oak::util::X29, oak::util::X30, oak::util::SP, oak::SOffset<10, 3>(208));
        oakAsm->LDP(oak::util::X27, oak::util::X28, oak::util::SP, oak::SOffset<10, 3>(192));
        oakAsm->LDP(oak::util::X25, oak::util::X26, oak::util::SP, oak::SOffset<10, 3>(176));
        oakAsm->LDP(oak::util::X23, oak::util::X24, oak::util::SP, oak::SOffset<10, 3>(160));
        oakAsm->LDP(oak::util::X21, oak::util::X22, oak::util::SP, oak::SOffset<10, 3>(144));
        oakAsm->LDP(oak::util::X19, oak::util::X20, oak::util::SP, oak::SOffset<10, 3>(128));
        oakAsm->ADD(oak::util::SP, oak::util::SP, 224);
        oakAsm->RET();
        oakEndBlock();
        correct &= lowered && emitted != 0 && out.host_size == emitted && out.frame_size == 0 && out.spill_values == 0;
        if (!lowered) continue;

        for (FPRoundMode round : rounds)
        for (bool fz : {false, true})
        for (bool dn : {false, true})
        {
            FPControlRegister fpcr = EmuConfig.Cpu.FPUFPCR;
            fpcr.SetRoundMode(round);
            fpcr.SetFlushToZero(fz);
            if (dn) fpcr.bitmask |= 1ull << 25;
            else fpcr.bitmask &= ~(1ull << 25);
            const FPControlRegisterBackup restore_fpcr(fpcr);
            for (u32 a = 0; a < edges.size(); ++a)
            for (u32 w = 0; w < edges.size(); ++w)
            {
                // Exhaust threshold/component pairs on the canonical binding;
                // every other binding retains edge diagonals and neighbors.
                if (binding_index != 0 && a != w && (a + 1) % edges.size() != w &&
                    (a + w) % edges.size() != 0) continue;
                std::array<u32, 4> fs = {edges[a], edges[(a + 5) % edges.size()],
                    edges[(a + 11) % edges.size()], edges[(a + 17) % edges.size()]};
                std::array<u32, 4> ft = {edges[(w + 3) % edges.size()],
                    edges[(w + 7) % edges.size()], edges[(w + 13) % edges.size()], edges[w]};
                if (binding.lhs_vf == 0) fs = vf0;
                if (binding.rhs_vf == 0) ft = vf0;
                if (binding.lhs_vf == binding.rhs_vf) ft = fs;
                for (u32 history : histories)
                {
                    std::array<std::array<u32, 4>, 29> qseed{}, qactual{};
                    std::array<u64, 31> xseed{}, xactual{};
                    for (u32 reg = 0; reg < qseed.size(); ++reg)
                        for (u32 lane = 0; lane < 4; ++lane)
                            qseed[reg][lane] = 0xa5d37129u ^ (reg * 0x01020408u) ^ (lane * 0x134579bdu);
                    for (u32 reg = 0; reg < xseed.size(); ++reg)
                        xseed[reg] = 0x13579bdf2468ace0ull ^ (0x0102030405060708ull * reg);
                    qseed[binding.lhs_host] = fs;
                    qseed[binding.rhs_host] = ft;
                    xseed[binding.clip_host] = 0xcafebabe00000000ull | history;
                    auto xexpected = xseed;
                    xexpected[binding.clip_host] = VuClipExpectedHistory(history, fs, ft[3]);
                    const u64 before_fpcr = VuClipReadFpcr(), before_fpsr = VuClipReadFpsr();
                    reinterpret_cast<void (*)(const void*, void*, void*, void*)>(entry)(
                        qseed.data(), qactual.data(), xseed.data(), xactual.data());
                    bool registers_same = true;
                    for (u32 reg = 0; reg < 16; ++reg)
                        registers_same &= xactual[reg] == xexpected[reg];
                    for (u32 reg = 18; reg < 31; ++reg)
                        registers_same &= xactual[reg] == xseed[reg];
                    const bool same = qactual == qseed && registers_same &&
                        before_fpcr == VuClipReadFpcr() && before_fpsr == VuClipReadFpsr();
                    if (!same && !reported)
                    {
                        std::printf("VU CLIP mismatch binding=%u a=%u w=%u history=%08x fpcr=%llx expected=%08x actual=%llx\n",
                            binding_index, a, w, history, static_cast<unsigned long long>(before_fpcr),
                            static_cast<u32>(xexpected[binding.clip_host]),
                            static_cast<unsigned long long>(xactual[binding.clip_host]));
                        reported = true;
                    }
                    correct &= same;
                }
            }
        }
    }
    return correct;
}

static void VUBoundVfClipTests()
{
    using Plan = Arm64Ir::BoundVfClipPlan;
    {
        Arm64Ir::LowerOutput out;
        out.host_size = 123;
        std::string inactive_error;
        Check(!oakHasBlock() &&
            !Arm64Ir::LowerBoundVfClipPlan(Plan{7, 8, false}, 2, 3, 0, &out, &inactive_error) &&
            out.entry == nullptr && out.host_size == 0 && out.frame_size == 0 && !oakHasBlock(),
            "VU CLIP rejects an inactive emission scope with cleared output");
    }

    const std::array<u32, 4> axes = {0x40000000u, 0xc0000000u, 0x3f800000u, 0xffffffffu};
    Check(VuClipExpectedMask(axes, 0x3f800000u) == 9 &&
        VuClipExpectedMask(axes, 0xbf800000u) == 9 &&
        VuClipExpectedMask({1, 0x807fffffu, 0, 0x7fffffffu}, 0) == 0 &&
        VuClipExpectedMask({0x00800000u, 0x80800000u, 0x007fffffu, 0}, 1) == 9 &&
        VuClipExpectedMask({0x7fffffffu, 0xffffffffu, 0x7f800001u, 0}, 0x7f800000u) == 25 &&
        VuClipExpectedHistory(0xffffffffu, axes, 0x3f800000u) == 0xffffc9u,
        "VU CLIP independent raw model proves strict bounds, signs, denormal threshold and 24-bit history");
    bool verified = true, admitted = true;
    std::string error;
    for (u32 lhs : {0u, 7u, 31u})
    for (u32 rhs : {0u, 8u, 31u})
    for (bool delay : {false, true})
    {
        const auto fn = BoundVfClipFixture(lhs, rhs, delay);
        verified &= fn.blocks[0].insts.size() == 11 && ir::Verify(fn, &error);
        Plan plan;
        admitted &= Arm64Ir::PlanBoundVfClipBlock(fn, &plan, &error) &&
            plan.lhs_vf == lhs && plan.rhs_vf == rhs && plan.delay_slot == delay;
    }
    Check(verified, "all 18 exact VU CLIP common graphs independently verify before bound admission");
    Check(admitted, "VU CLIP planner admits exact logical-history graphs including VF0 and delay metadata");

    const auto valid = BoundVfClipFixture(7, 8);
    bool verifier_rejects = true;
    for (u32 node : {0u, 1u, 2u, 3u})
    {
        auto fn = valid;
        const ir::Type wrong = node < 2 ? ir::Type::V4F32 : ir::Type::I64;
        auto& inst = fn.blocks[0].insts[node];
        inst.type = wrong;
        fn.value_types[inst.value] = wrong;
        verifier_rejects &= !ir::Verify(fn, &error);
    }
    for (u32 node : {2u, 3u, 9u})
    {
        auto fn = valid;
        fn.blocks[0].insts[node].imm = 1;
        verifier_rejects &= !ir::Verify(fn, &error);
    }
    Check(verifier_rejects, "VU CLIP vocabulary rejects wrong raw types and nonzero operation/history selectors");
    {
        auto fn = valid;
        // A state write defines no SSA value, but its instruction type must
        // still be Void. Keep value zero to isolate that verifier contract.
        fn.blocks[0].insts[9].type = ir::Type::I32;
        fn.blocks[0].insts[9].value = 0;
        Check(!ir::Verify(fn, &error), "VU CLIP verifier rejects non-Void WriteVuClip with no result value");
    }

    bool rejects = true;
    const auto reject = [&](const ir::Function& fn) {
        Plan plan{1, 2, true};
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        oakStartBlock();
        const u8* before = oakGetCurrentCodePointer();
        std::array<u8, 64> original;
        std::memcpy(original.data(), before, original.size());
        rejects &= !Arm64Ir::PlanBoundVfClipBlock(fn, &plan, &error) &&
            plan.lhs_vf == 0 && plan.rhs_vf == 0 && !plan.delay_slot &&
            before == oakGetCurrentCodePointer() && std::memcmp(before, original.data(), original.size()) == 0;
        oakAsm->RET();
        oakEndBlock();
    };
    reject(BoundVfClipFixture(32, 8));
    reject(BoundVfClipFixture(7, 32));
    for (u32 node = 0; node < 11; ++node)
    {
        auto fn = valid;
        fn.blocks[0].insts[node].guest_pc += 4;
        reject(fn);
        fn = valid;
        fn.blocks[0].insts[node].aux |= ir::IF_EXACT;
        reject(fn);
        fn = valid;
        fn.blocks[0].insts[node].aux ^= ir::IF_DELAY_SLOT;
        reject(fn);
        fn = valid;
        auto& inst = fn.blocks[0].insts[node];
        if (inst.num_args < 4) inst.args[inst.num_args] = fn.blocks[0].insts[0].value;
        reject(fn);
        fn = valid;
        fn.blocks[0].insts[node].op = static_cast<ir::Op>(static_cast<u32>(ir::Op::Count) + 1);
        reject(fn);
        fn = valid;
        fn.blocks[0].insts[node].imm ^= 1;
        // Different valid VF identities are legitimate plans, not rejects.
        if (node > 1) reject(fn);
        for (u32 arg = 0; arg < valid.blocks[0].insts[node].num_args; ++arg)
        {
            fn = valid;
            auto& target = fn.blocks[0].insts[node].args[arg];
            target = target == fn.blocks[0].insts[0].value ? fn.blocks[0].insts[1].value : fn.blocks[0].insts[0].value;
            reject(fn);
        }
    }
    for (u32 mutation = 0; mutation < 6; ++mutation)
    {
        auto fn = valid;
        if (mutation == 0) fn.blocks[0].flags = 1;
        if (mutation == 1) fn.blocks[0].guest_end += 4;
        if (mutation == 2) fn.guest_entry += 4;
        if (mutation == 3) ++fn.next_value;
        if (mutation == 4) fn.blocks[0].insts.erase(fn.blocks[0].insts.begin() + 4);
        if (mutation == 5) fn.blocks[0].insts[3].value = fn.blocks[0].insts[2].value;
        reject(fn);
    }
    Check(rejects, "VU CLIP strict planner rejects malformed shape, SSA, constants and metadata without emission");

    bool lower_rejects = true;
    const auto reject_lower = [&](const Plan& plan, u32 fs, u32 ft, u32 clip) {
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        oakStartBlock();
        const u8* before = oakGetCurrentCodePointer();
        std::array<u8, 64> original;
        std::memcpy(original.data(), before, original.size());
        Arm64Ir::LowerOutput out{reinterpret_cast<u8*>(1), 1, 1};
        const bool accepted = Arm64Ir::LowerBoundVfClipPlan(plan, fs, ft, clip, &out, &error);
        lower_rejects &= !accepted && before == oakGetCurrentCodePointer() &&
            std::memcmp(before, original.data(), original.size()) == 0 &&
            out.entry == nullptr && out.host_size == 0 && out.frame_size == 0 && out.spill_values == 0;
        oakAsm->RET();
        oakEndBlock();
    };
    for (u32 host : {29u, 30u, 31u, 32u, 0xffffffffu})
    {
        reject_lower(Plan{7, 8, false}, host, 4, 0);
        reject_lower(Plan{7, 8, false}, 3, host, 0);
    }
    for (u32 host : {16u, 17u, 18u, 19u, 30u, 31u, 32u, 0xffffffffu})
        reject_lower(Plan{7, 8, false}, 3, 4, host);
    reject_lower(Plan{7, 8, false}, 3, 3, 0);
    reject_lower(Plan{0, 8, false}, 3, 3, 0);
    reject_lower(Plan{32, 8, false}, 3, 4, 0);
    reject_lower(Plan{7, 32, false}, 3, 4, 0);
    Check(lower_rejects, "VU CLIP rejects reserved scratch, distinct-source host aliases and invalid plans before emission");

    bool domain_rejects = true;
    for (Arm64Ir::GuestState state : {Arm64Ir::GuestState::EE, Arm64Ir::GuestState::IOP})
    {
        auto fn = valid;
        std::array<u8, 512> code;
        code.fill(0xa5);
        Arm64Ir::LowerOptions options;
        options.guest_state = state;
        Arm64Ir::LowerOutput out;
        domain_rejects &= !Arm64Ir::CanLower(fn, false, &error, state) &&
            !Arm64Ir::LowerBlock(fn, options, code.data(), code.size(), &out, &error) &&
            out.entry == nullptr && out.host_size == 0 &&
            std::all_of(code.begin(), code.end(), [](u8 b) { return b == 0xa5; });
    }
    Check(domain_rejects, "generic EE/IOP lowering rejects bound VU CLIP without emission");

    bool history_graphs_valid = true, history_controls_admit = true, history_domains_reject = true;
    for (Arm64Ir::GuestState state : {Arm64Ir::GuestState::EE, Arm64Ir::GuestState::IOP})
    for (bool write : {false, true})
    {
        // Isolate logical history state from the unsupported vector operation.
        ir::Function fn;
        ir::Builder b(fn);
        b.CreateBlock(0x80);
        const u32 value = write ? b.ConstI32(0x123456) :
            b.Emit(ir::Op::ReadVuClip, ir::Type::I32, {}, 0);
        b.Emit1(write ? ir::Op::WriteVuClip : ir::Op::WriteGpr, ir::Type::Void, value, write ? 0 : 7);
        b.Resume(0x84);
        history_graphs_valid &= ir::Verify(fn, &error);
        auto control = fn;
        if (write)
        {
            control.blocks[0].insts[1].op = ir::Op::WriteGpr;
            control.blocks[0].insts[1].imm = 7;
        }
        else
        {
            control.blocks[0].insts[0].op = ir::Op::ConstI32;
            control.blocks[0].insts[0].imm = 0x123456;
        }
        history_controls_admit &= ir::Verify(control, &error) &&
            Arm64Ir::CanLower(control, false, &error, state);
        std::array<u8, 512> code;
        code.fill(0xa5);
        Arm64Ir::LowerOptions options;
        options.guest_state = state;
        Arm64Ir::LowerOutput out;
        history_domains_reject &= !Arm64Ir::CanLower(fn, false, &error, state) &&
            !Arm64Ir::LowerBlock(fn, options, code.data(), code.size(), &out, &error) &&
            out.entry == nullptr && out.host_size == 0 &&
            std::all_of(code.begin(), code.end(), [](u8 byte) { return byte == 0xa5; });
    }
    Check(history_graphs_valid && history_controls_admit,
        "VU CLIP scalar history probes verify and ordinary scalar controls admit in EE/IOP");
    Check(history_domains_reject,
        "generic EE/IOP rejects isolated logical clip-history reads and writes with zero emission");

    VURegs saved_vu0, saved_vu1, seeded_vu0, seeded_vu1;
    std::memcpy(&saved_vu0, &VU0, sizeof(VU0));
    std::memcpy(&saved_vu1, &VU1, sizeof(VU1));
    VU0.VI[REG_CLIP_FLAG].UL = 0x13579b;
    VU1.VI[REG_CLIP_FLAG].UL = 0x2468ac;
    VU0.VI[REG_MAC_FLAG].UL = 0xa593;
    VU1.VI[REG_STATUS_FLAG].UL = 0x6b1;
    std::memcpy(&seeded_vu0, &VU0, sizeof(VU0));
    std::memcpy(&seeded_vu1, &VU1, sizeof(VU1));
    const u64 saved_fpsr = VuClipReadFpsr();
    const u64 seeded_fpsr = saved_fpsr | 0x0800009full;
    asm volatile("msr fpsr, %0" : : "r"(seeded_fpsr) : "memory");
    s_vu_clip_body_bytes = 0;
    Check(VuClipBoundMatrix(), "VU CLIP bound raw edges, history, aliases and FPCR modes preserve every read-only Q and other GPR");
    Check(std::memcmp(&seeded_vu0, &VU0, sizeof(VU0)) == 0 &&
        std::memcmp(&seeded_vu1, &VU1, sizeof(VU1)) == 0,
        "VU CLIP logical history binding does not mutate global VU state or unscheduled flags");
    Check(VuClipReadFpsr() == seeded_fpsr, "VU CLIP preserves seeded FPSR flags");
    asm volatile("msr fpsr, %0" : : "r"(saved_fpsr) : "memory");
    std::memcpy(&VU0, &saved_vu0, sizeof(VU0));
    std::memcpy(&VU1, &saved_vu1, sizeof(VU1));
    Check(s_vu_clip_body_bytes != 0 && s_vu_clip_body_bytes <= 192,
        "VU CLIP emitted bound body stays within the initial 192-byte budget");
    std::printf("VUCLIPBOUND max_body_bytes=%u budget_bytes=192\n", s_vu_clip_body_bytes);
}
