// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Shared-IR EE scalar-store regression tests. Included inside jit_oracle.cpp's
// anonymous namespace; the production lowering remains owned by the main task.
#include "arm64/ir/IrLower-arm64.h"

extern "C" u64 EmuCoreXOracleGetEeIrDirectStoreCounts();

struct EEScalarStoreTrace
{
    u32 helpers = 0, before = 0, after = 0, handlers = 0;
    u32 address = 0, pc = 0, bd = 0;
    u64 value = 0;
};

EEScalarStoreTrace s_ee_scalar_store_trace;

void EEScalarStoreRecord(u32 address, u64 value)
{
    ++s_ee_scalar_store_trace.handlers;
    s_ee_scalar_store_trace.address = address;
    s_ee_scalar_store_trace.value = value;
    s_ee_scalar_store_trace.pc = cpuRegs.pc;
    s_ee_scalar_store_trace.bd = cpuRegs.IsDelaySlot;
}

void EEScalarStore8(u32 address, u8 value) { EEScalarStoreRecord(address, value); }
void EEScalarStore16(u32 address, u16 value) { EEScalarStoreRecord(address, value); }
void EEScalarStore32(u32 address, u32 value) { EEScalarStoreRecord(address, value); }
void EEScalarStore64(u32 address, u64 value) { EEScalarStoreRecord(address, value); }

ir::Function EEScalarStoreFixture(ir::Op op)
{
    ir::Function fn;
    ir::Builder b(fn);
    b.CreateBlock(EE_TEST_PC);
    const u32 vector = b.Emit(ir::Op::ReadGpr, ir::Type::V4U32, {}, 11);
    const u32 address = b.Emit(ir::Op::ReadGpr, ir::Type::I32, {}, 22);
    const bool wide = op == ir::Op::Store64;
    const u32 value = b.Emit(ir::Op::ReadGpr, wide ? ir::Type::I64 : ir::Type::I32, {}, 8);
    b.SetGuestPc(EE_TEST_PC + 8);
    b.SetDelaySlot(true);
    b.Emit2(op, ir::Type::Void, address, value);
    b.SetDelaySlot(false);
    b.Emit1(ir::Op::WriteGpr, ir::Type::Void, vector, 12);
    b.Resume(EE_TEST_PC + 12);
    return fn;
}

void EEScalarStoreClobberVectors()
{
    for (u32 reg = 2; reg <= 7; ++reg)
        oakAsm->MOVI(oak::QReg(reg).B16(), 0xa5);
}

void EEIrScalarStoreTests()
{
    const auto saved_config = EmuConfig.Cpu.Recompiler;
    const auto saved_cpu = Cpu;
    Cpu = &recCpu;
    EmuConfig.Cpu.Recompiler.PauseOnTLBMiss = false;
    alignas(16) std::array<u8, 8192> ram_a{}, ram_b{};
    for (u32 i = 0; i < ram_a.size(); ++i)
    {
        ram_a[i] = static_cast<u8>(0x31 + i * 17);
        ram_b[i] = static_cast<u8>(0xc7 - i * 11);
    }
    const vtlbHandler handler = vtlb_RegisterHandler(nullptr, nullptr, nullptr, nullptr, nullptr,
        EEScalarStore8, EEScalarStore16, EEScalarStore32, EEScalarStore64, nullptr);

    for (const ir::Op op : {ir::Op::Store8, ir::Op::Store16, ir::Op::Store32, ir::Op::Store64})
    {
        const u32 width = op == ir::Op::Store8 ? 1 : op == ir::Op::Store16 ? 2 : op == ir::Op::Store32 ? 4 : 8;
        const u64 source = 0xfedcba98765432a1ull;
        const u64 expected = width == 8 ? source : source & ((u64(1) << (width * 8)) - 1);
        const ir::Function fixture = EEScalarStoreFixture(op);

        for (u32 base : {0x41000000u, 0xc1000000u, 0xffffe000u})
        {
            const u32 page = base >> 12;
            const auto saved_map0 = vtlb_private::vtlbdata.vmap[page];
            const auto saved_map1 = vtlb_private::vtlbdata.vmap[page + 1];

            // Optimized EE RAM writes avoid the helper; explicit fallback
            // modes below verify that helper behavior remains available.
            struct LowerMode { bool optimize, allocate, enable_ee, enable_cache, direct_stores; };
            const std::array<LowerMode, 6> modes = {{
                {false, true, true, false, true},  // unoptimized helper baseline
                {true, true, true, false, true},   // optimized direct-RAM path
                {true, false, true, false, true},  // register allocation disabled
                {true, true, false, true, true},   // EE-disabled cache fallback
                {true, true, true, true, true},    // EE-enabled cache policy permits direct RAM
                {true, true, true, false, false},  // explicit helper-only baseline
            }};
            for (u32 offset : {0u, 3u, 4095u})
            for (u32 mode = 0; mode < modes.size(); ++mode)
            for (bool preserve_vectors : {false, true})
            {
                const LowerMode& lower_mode = modes[mode];
                EmuConfig.Cpu.Recompiler.EnableEE = lower_mode.enable_ee;
                EmuConfig.Cpu.Recompiler.EnableEECache = lower_mode.enable_cache;
                vtlb_private::vtlbdata.vmap[page] = vtlb_private::VTLBVirtual::fromPointer(
                    reinterpret_cast<uptr>(ram_a.data()), base);
                vtlb_private::vtlbdata.vmap[page + 1] = vtlb_private::VTLBVirtual::fromPointer(
                    reinterpret_cast<uptr>(ram_a.data() + 4096), base + 4096);

                EeIr::LowerHooks hooks;
                hooks.ctx = &preserve_vectors;
                hooks.memory_preserves_vectors = preserve_vectors;
                hooks.before_memory = +[](void* ctx, u32 pc, bool bd) {
                    oakAsm->MOV(oak::util::W16, pc);
                    oakStore32(oak::util::W16, {oak::util::X19, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
                    oakAsm->MOV(oak::util::W16, bd ? 1 : 0);
                    oakStore32(oak::util::W16, {oak::util::X19, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.IsDelaySlot))});
                    oakMoveAddressToReg(oak::util::X17, &s_ee_scalar_store_trace.before);
                    oakLoad32(oak::util::W16, {oak::util::X17, 0});
                    oakAsm->ADD(oak::util::W16, oak::util::W16, 1);
                    oakStore32(oak::util::W16, {oak::util::X17, 0});
                    if (!*static_cast<bool*>(ctx)) EEScalarStoreClobberVectors();
                };
                hooks.after_memory = +[](void* ctx) {
                    oakMoveAddressToReg(oak::util::X17, &s_ee_scalar_store_trace.after);
                    oakLoad32(oak::util::W16, {oak::util::X17, 0});
                    oakAsm->ADD(oak::util::W16, oak::util::W16, 1);
                    oakStore32(oak::util::W16, {oak::util::X17, 0});
                    if (!*static_cast<bool*>(ctx)) EEScalarStoreClobberVectors();
                };
                hooks.before_helper = +[](void*) {
                    oakMoveAddressToReg(oak::util::X17, &s_ee_scalar_store_trace.helpers);
                    oakLoad32(oak::util::W16, {oak::util::X17, 0});
                    oakAsm->ADD(oak::util::W16, oak::util::W16, 1);
                    oakStore32(oak::util::W16, {oak::util::X17, 0});
                    EEScalarStoreClobberVectors();
                };
                hooks.after_helper = +[](void*) { EEScalarStoreClobberVectors(); };

                EeIr::LowerOptions options;
                options.optimize_ir = lower_mode.optimize;
                options.allocate_registers = lower_mode.allocate;
                options.direct_ee_stores = lower_mode.direct_stores;
                options.hooks = &hooks;
                EeIr::LowerOutput out;
                std::string error;
                ir::Function fn = fixture;
                bool same = EeIr::LowerBlock(fn, options, SysMemory::GetEERec() + 0x2000000,
                    0x10000, &out, &error);
                if (same)
                {
                    const std::array<u32, 4> vector_data = {0x80000001, 0xffffffff, 0x76543210, 0xabcdef01};
                    s_ee_scalar_store_trace = {};
                    const auto execute_mapped = [&](u8* memory, u8* other) {
                        std::array<u8, 8192> expected_memory{};
                        std::array<u8, 8192> other_before{};
                        std::memcpy(expected_memory.data(), memory, expected_memory.size());
                        std::memcpy(other_before.data(), other, other_before.size());
                        // Poison exactly this store's range on every execution so
                        // stale bytes cannot make a missing store look successful.
                        for (u32 i = 0; i < width; ++i)
                            memory[offset + i] = static_cast<u8>(~expected_memory[offset + i]);
                        std::memcpy(expected_memory.data(), memory, expected_memory.size());
                        for (u32 i = 0; i < width; ++i)
                            expected_memory[offset + i] = static_cast<u8>(expected >> (i * 8));
                        vtlb_private::vtlbdata.vmap[page] = vtlb_private::VTLBVirtual::fromPointer(
                            reinterpret_cast<uptr>(memory), base);
                        vtlb_private::vtlbdata.vmap[page + 1] = vtlb_private::VTLBVirtual::fromPointer(
                            reinterpret_cast<uptr>(memory + 4096), base + 4096);
                        std::memset(&cpuRegs, 0, sizeof(cpuRegs));
                        cpuRegs.GPR.r[22].UD[0] = base + offset;
                        cpuRegs.GPR.r[8].UD[0] = source;
                        cpuRegs.GPR.r[8].UD[1] = 0x8877665544332211ull;
                        std::memcpy(cpuRegs.GPR.r[11].UL, vector_data.data(), 16);
                        reinterpret_cast<void (*)()>(out.entry)();
                        u64 raw = 0;
                        std::memcpy(&raw, memory + offset, width);
                        return raw == expected && std::memcmp(memory, expected_memory.data(), expected_memory.size()) == 0 &&
                            std::memcmp(other, other_before.data(), other_before.size()) == 0 &&
                            cpuRegs.GPR.r[8].UD[1] == 0x8877665544332211ull &&
                            std::memcmp(cpuRegs.GPR.r[12].UL, vector_data.data(), 16) == 0 &&
                            cpuRegs.pc == EE_TEST_PC + 8 && cpuRegs.IsDelaySlot == 1;
                    };
                    same &= execute_mapped(ram_a.data(), ram_b.data());
                    same &= execute_mapped(ram_b.data(), ram_a.data()); // reuse same code after a live VTLB remap
                    const bool direct_expected = lower_mode.optimize && lower_mode.enable_ee &&
                        lower_mode.direct_stores && (mode != 3); // mode 3 is the EE-disabled cache fallback
                    const u32 expected_helpers = direct_expected ? 0u : 1u;
                    same &= s_ee_scalar_store_trace.before == 2 && s_ee_scalar_store_trace.after == 2 &&
                        s_ee_scalar_store_trace.handlers == 0 &&
                        s_ee_scalar_store_trace.helpers == expected_helpers * 2 &&
                        out.direct_ee_store_operations == (direct_expected ? 1u : 0u);
                }
                char name[160];
                std::snprintf(name, sizeof(name), "ir EE scalar %s mapped/remapped RAM alias=%08x offset=%u mode=%u preserve_vectors=%u direct store avoids helper",
                    ir::OpName(op), base, offset, mode, preserve_vectors);
                Check(same, name);
            }

            // A handler mapping must use the exact helper, translated address,
            // width truncation, and guest PC/BD established by before_memory.
            vtlb_private::vtlbdata.vmap[page] = vtlb_private::VTLBVirtual(
                vtlb_private::VTLBPhysical::fromHandler(handler), 0x1ff00000, base);
            for (u32 allocation_mode = 0; allocation_mode < 2; ++allocation_mode)
            for (bool preserve_vectors : {false, true})
            {
                EeIr::LowerHooks hooks;
                hooks.ctx = &preserve_vectors;
                hooks.memory_preserves_vectors = preserve_vectors;
                hooks.before_memory = +[](void* ctx, u32 pc, bool bd) {
                    oakAsm->MOV(oak::util::W16, pc);
                    oakStore32(oak::util::W16, {oak::util::X19, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
                    oakAsm->MOV(oak::util::W16, bd ? 1 : 0);
                    oakStore32(oak::util::W16, {oak::util::X19, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.IsDelaySlot))});
                    if (!*static_cast<bool*>(ctx)) EEScalarStoreClobberVectors();
                };
                hooks.after_memory = +[](void* ctx) {
                    if (!*static_cast<bool*>(ctx)) EEScalarStoreClobberVectors();
                };
                hooks.before_helper = +[](void*) {
                    oakMoveAddressToReg(oak::util::X17, &s_ee_scalar_store_trace.helpers);
                    oakLoad32(oak::util::W16, {oak::util::X17, 0});
                    oakAsm->ADD(oak::util::W16, oak::util::W16, 1);
                    oakStore32(oak::util::W16, {oak::util::X17, 0});
                    EEScalarStoreClobberVectors();
                };
                hooks.after_helper = +[](void*) { EEScalarStoreClobberVectors(); };
                EeIr::LowerOptions options;
                options.optimize_ir = allocation_mode != 0;
                options.hooks = &hooks;
                EeIr::LowerOutput out;
                std::string error;
                ir::Function handler_fn = fixture;
                bool same = EeIr::LowerBlock(handler_fn, options, SysMemory::GetEERec() + 0x2100000,
                    0x10000, &out, &error);
                if (same)
                {
                    std::memset(&cpuRegs, 0, sizeof(cpuRegs));
                    cpuRegs.GPR.r[22].UD[0] = base + 3;
                    cpuRegs.GPR.r[8].UD[0] = source;
                    const std::array<u32, 4> vector_data = {0x80000001, 0xffffffff, 0x76543210, 0xabcdef01};
                    std::memcpy(cpuRegs.GPR.r[11].UL, vector_data.data(), 16);
                    s_ee_scalar_store_trace = {};
                    reinterpret_cast<void (*)()>(out.entry)();
                    same &= s_ee_scalar_store_trace.helpers == 1 && s_ee_scalar_store_trace.handlers == 1 &&
                        s_ee_scalar_store_trace.address == 0x1ff00003 &&
                        s_ee_scalar_store_trace.value == expected &&
                        s_ee_scalar_store_trace.pc == EE_TEST_PC + 8 && s_ee_scalar_store_trace.bd == 1 &&
                        std::memcmp(cpuRegs.GPR.r[12].UL, vector_data.data(), 16) == 0;
                }
                char name[144];
                std::snprintf(name, sizeof(name), "ir EE scalar %s handler mapping preserves helper/PC/BD/vectors mode=%u preserve_vectors=%u",
                    ir::OpName(op), allocation_mode, preserve_vectors);
                Check(same, name);
            }

            // An unmapped virtual page must go through the VTLB helper so the
            // guest exception path remains intact; only the generated helper
            // counter is asserted here because exception cause differs by op.
            vtlb_VMapUnmap(base, 0x1000);
            EeIr::LowerHooks unmapped_hooks;
            unmapped_hooks.before_helper = +[](void*) {
                oakMoveAddressToReg(oak::util::X17, &s_ee_scalar_store_trace.helpers);
                oakLoad32(oak::util::W16, {oak::util::X17, 0});
                oakAsm->ADD(oak::util::W16, oak::util::W16, 1);
                oakStore32(oak::util::W16, {oak::util::X17, 0});
            };
            EeIr::LowerOptions unmapped_options;
            unmapped_options.hooks = &unmapped_hooks;
            EeIr::LowerOutput unmapped_out;
            std::string unmapped_error;
            ir::Function unmapped_fn = fixture;
            bool unmapped_same = EeIr::LowerBlock(unmapped_fn, unmapped_options,
                SysMemory::GetEERec() + 0x2180000, 0x10000, &unmapped_out, &unmapped_error);
            if (unmapped_same)
            {
                std::memset(&cpuRegs, 0, sizeof(cpuRegs));
                cpuRegs.GPR.r[22].UD[0] = base + 0x20;
                cpuRegs.GPR.r[8].UD[0] = source;
                s_ee_scalar_store_trace = {};
                reinterpret_cast<void (*)()>(unmapped_out.entry)();
                unmapped_same &= s_ee_scalar_store_trace.helpers == 1;
            }
            Check(unmapped_same, "ir EE scalar unmapped store retains VTLB exception helper");

            vtlb_private::vtlbdata.vmap[page] = saved_map0;
            vtlb_private::vtlbdata.vmap[page + 1] = saved_map1;
        }
    }
    Cpu = saved_cpu;
    EmuConfig.Cpu.Recompiler = saved_config;
}

void EELegacyCachedBaseStoreTests()
{
    // LW forces a dynamic cached base. Keep that base live after the store,
    // and exercise the legacy emitter with both fastmem and software VTLB.
    const bool saved_fastmem = EmuConfig.Cpu.Recompiler.EnableFastmem;
    for (u32 op : {40u, 41u, 43u, 63u, 31u, 57u}) // SB/SH/SW/SD/SQ/SWC1
    for (s32 offset : {-32768, -4096, -4095, -16, -1, 0, 1, 16, 4095, 4096, 32767})
    for (bool alias : {false, true})
    {
        if (op == 57 && alias)
            continue; // SWC1's FPR value cannot alias the GPR base.
        const u32 target = EE_TEST_SCRATCH + (op == 31 || op == 40 ? 131 : 128);
        GPR_reg dynamic_initial[32] = {};
        dynamic_initial[22].UD[0] = EE_TEST_SCRATCH;
        dynamic_initial[8].UD[0] = target - static_cast<u32>(offset);
        dynamic_initial[9].UD[0] = 0xfedcba98765432a1ull;
        dynamic_initial[9].UD[1] = 0x0123456789abcdefull;
        dynamic_initial[16].UD[1] = 0x8877665544332211ull;
        std::array<u32, 32> initial_fpr{};
        initial_fpr[9] = 0x8badf00du;
        const u32 source = alias ? 16 : 9;
        const std::vector<u32> dynamic_code = {
            MipsI(43, 22, 8, 0), MipsI(35, 22, 16, 0),
            MipsI(op, 16, source, offset), MipsR(16, 0, 18, 0, 0x2d)};
        EESnapshot vtlb_result{};
        for (bool fastmem : {false, true})
        {
            EmuConfig.Cpu.Recompiler.EnableFastmem = fastmem;
            const EESnapshot interpreter = RunEEProgram(false, dynamic_code, false,
                nullptr, nullptr, dynamic_initial, initial_fpr.data());
            const u64 count_before = EmuCoreXOracleGetEeIrDirectStoreCounts();
            const EESnapshot legacy = RunEEProgramWithIrMode(0, dynamic_code,
                dynamic_initial, nullptr, initial_fpr.data(), nullptr);
            char name[128];
            std::snprintf(name, sizeof(name), "legacy EE cached-base store op=%u offset=%d alias=%u fastmem=%u",
                op, offset, static_cast<u32>(alias), static_cast<u32>(fastmem));
            CompareEE(interpreter, legacy, name);
            Check(EmuCoreXOracleGetEeIrDirectStoreCounts() == count_before,
                "legacy cached-base stores keep the shared IR route disabled");
            if (fastmem)
            {
                CompareEE(vtlb_result, legacy, "legacy cached-base fastmem/vtlb complete state");
                Check(vtlb_result.cycle == legacy.cycle,
                    "legacy cached-base fastmem/vtlb exact cycles");
            }
            else
                vtlb_result = legacy;
        }
    }
    EmuConfig.Cpu.Recompiler.EnableFastmem = saved_fastmem;
}

void EEIrScalarStoreGuestTests()
{
    GPR_reg initial[32] = {};
    initial[22].UD[0] = EE_TEST_SCRATCH + 0x80;
    initial[8].UD[0] = 0xfedcba98765432a1ull;
    const std::array<std::pair<u32, const char*>, 4> stores = {{{40, "sb"}, {41, "sh"}, {43, "sw"}, {63, "sd"}}};
    for (const auto& [op, name] : stores)
    {
        std::vector<u32> code = {MipsI(op, 22, 8, op == 40 ? 5 : op == 41 ? 6 : op == 43 ? 8 : 16)};
        const EESnapshot interpreter = RunEEProgram(false, code, false, nullptr, nullptr, initial);
        const EESnapshot legacy = RunEEProgramWithIrMode(0, code, initial, nullptr, nullptr, nullptr);
        const EESnapshot shared = RunEEProgramWithIrMode(1, code, initial, nullptr, nullptr, nullptr);
        CompareEE(interpreter, legacy, name);
        CompareEE(interpreter, shared, name);
        Check(legacy.cycle == shared.cycle, "ir EE scalar store guest route cycles match");
    }

    // Production delay-slot coverage: ordinary branches always execute their
    // store slot; branch-likely slots execute only on the taken path. Counter
    // deltas measure compiled direct stores, including an annulled slot that
    // is admitted while compiling the block.
    for (const auto& [store_op, store_name] : stores)
    for (const auto& [branch_op, branch_name] : {std::pair<u32, const char*>{4, "beq"}, {20, "beql"}})
    for (bool taken : {false, true})
    {
        GPR_reg branch_initial[32] = {};
        branch_initial[22].UD[0] = EE_TEST_SCRATCH + 0x80;
        branch_initial[8].UD[0] = initial[8].UD[0];
        branch_initial[4].UD[0] = taken ? 1 : 0;
        branch_initial[5].UD[0] = 1;
        const s16 offset = store_op == 40 ? 5 : store_op == 41 ? 6 : store_op == 43 ? 8 : 16;
        const std::vector<u32> code = {MipsI(branch_op, 4, 5, 1), MipsI(store_op, 22, 8, offset)};
        const u64 legacy_before = EmuCoreXOracleGetEeIrDirectStoreCounts();
        const EESnapshot legacy = RunEEProgramWithIrMode(0, code, branch_initial, nullptr, nullptr, nullptr);
        const u64 legacy_after = EmuCoreXOracleGetEeIrDirectStoreCounts();
        const u64 shared_before = EmuCoreXOracleGetEeIrDirectStoreCounts();
        const EESnapshot shared = RunEEProgramWithIrMode(1, code, branch_initial, nullptr, nullptr, nullptr);
        const u64 shared_after = EmuCoreXOracleGetEeIrDirectStoreCounts();
        const EESnapshot interpreter = RunEEProgram(false, code, false, nullptr, nullptr, branch_initial);
        CompareEE(interpreter, legacy, store_name);
        CompareEE(interpreter, shared, store_name);
        const u32 width = store_op == 40 ? 1 : store_op == 41 ? 2 : store_op == 43 ? 4 : 8;
        const u32 start = 0x80 + static_cast<u32>(offset);
        const bool executed = branch_op == 4 || taken;
        Check(start + width <= EE_SCRATCH_SIZE,
            "ir EE delay-slot store expected bytes remain inside captured scratch window");
        bool memory_ok = true;
        for (u32 byte = 0; byte < width; ++byte)
        {
            const u8 expected = executed ? static_cast<u8>(branch_initial[8].UD[0] >> (byte * 8)) :
                static_cast<u8>((start + byte) * 13 + 7);
            memory_ok &= shared.scratch[start + byte] == expected;
        }
        char name_check[128];
        std::snprintf(name_check, sizeof(name_check), "ir EE %s %s taken=%u store result and annul behavior",
            branch_name, store_name, taken);
        Check(memory_ok, name_check);
        Check(legacy_after == legacy_before,
            "ir EE legacy delay-slot store does not increment shared direct-store counter");
        Check(shared_after - shared_before == 1,
            "ir EE shared delay-slot store is admitted by direct-store counter");
        Check(legacy.cycle == shared.cycle,
            "ir EE production delay-slot store exact legacy/shared cycles");
    }

    // The delay-slot SW replaces an already executed target instruction.
    // The first JAL runs ADDIU r9,zero,1; after rewriting the instruction, the second
    // JAL must compile and execute the replacement ADDIU r9,zero,2.
    GPR_reg smc_initial[32] = {};
    const u32 smc_target = EE_TEST_PC + 12 * 4;
    smc_initial[22].UD[0] = smc_target;
    smc_initial[8].UD[0] = 0;
    const u32 replacement = MipsI(9, 0, 9, 2);
    std::vector<u32> smc_code = {
        MipsJ(3, smc_target), 0,
        MipsI(15, 0, 8, replacement >> 16), MipsI(13, 8, 8, replacement & 0xffff),
        MipsI(4, 0, 0, 1), MipsI(43, 22, 8, 0), // store in taken branch delay slot
        MipsJ(3, smc_target), 0,
        MipsJ(2, EE_TEST_PC + 8 * 4), 0,
        0, 0,
        MipsI(9, 0, 9, 1), MipsR(31, 0, 0, 0, 8), 0};
    const EESnapshot smc_interpreter = RunEEProgram(false, smc_code, true, nullptr, nullptr, smc_initial);
    const EESnapshot smc_legacy = RunEEProgramWithIrMode(0, smc_code, smc_initial, nullptr, nullptr, nullptr, true);
    const u64 smc_count_before = EmuCoreXOracleGetEeIrDirectStoreCounts();
    const EESnapshot smc_shared = RunEEProgramWithIrMode(1, smc_code, smc_initial, nullptr, nullptr, nullptr, true);
    const u64 smc_count_after = EmuCoreXOracleGetEeIrDirectStoreCounts();
    CompareEE(smc_interpreter, smc_legacy, "ir EE scalar store self-modifying target legacy");
    CompareEE(smc_interpreter, smc_shared, "ir EE scalar store self-modifying target shared");
    Check(smc_interpreter.gpr[9] == 2 && smc_legacy.gpr[9] == 2 && smc_shared.gpr[9] == 2,
        "ir EE scalar store invalidates old target and executes replacement value");
    Check(smc_count_after > smc_count_before,
        "ir EE self-modifying delay-slot store compiles through direct-store counter");
    Check(smc_legacy.cycle == smc_shared.cycle,
        "ir EE self-modifying store exact legacy/shared cycles");
}

void EEIrScalarStoreBenchmark()
{
    // Same optimized 16-store workload, with only direct RAM stores toggled.
    // Timing is diagnostic; correctness, path counters, and code size are exact.
    const auto saved_config = EmuConfig.Cpu.Recompiler;
    EmuConfig.Cpu.Recompiler.EnableEE = true;
    EmuConfig.Cpu.Recompiler.EnableEECache = false;
    ir::Function fn;
    ir::Builder b(fn);
    b.CreateBlock(EE_TEST_PC);
    const u32 base = b.Emit(ir::Op::ReadGpr, ir::Type::I32, {}, 22);
    for (u32 i = 0; i < 16; ++i)
    {
        const u32 address = b.Emit2(ir::Op::Add, ir::Type::I32, base, b.ConstI32(i * 4));
        const u32 value = b.Emit(ir::Op::ReadGpr, ir::Type::I32, {}, 8);
        b.Emit2(ir::Op::Store32, ir::Type::Void, address, value);
    }
    b.Resume(EE_TEST_PC + 4);
    EeIr::LowerOutput outputs[2]{};
    double compile_us[2]{};
    bool same = true;
    for (u32 mode = 0; mode < 2; ++mode)
    {
        EeIr::LowerOptions options;
        options.optimize_ir = true;
        options.direct_ee_stores = mode != 0;
        ir::Function compile_fn = fn;
        std::string error;
        const auto start = std::chrono::steady_clock::now();
        same &= EeIr::LowerBlock(compile_fn, options, SysMemory::GetEERec() + 0x2200000 + mode * 0x10000,
            0x10000, &outputs[mode], &error);
        compile_us[mode] = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - start).count();
        same &= outputs[mode].direct_ee_store_operations == (mode ? 16u : 0u);
    }
    Check(same, "ir EE scalar 16-store helper/direct benchmarks compile same optimized IR");
    if (same)
    {
        cpuRegs.GPR.r[22].UD[0] = EE_TEST_SCRATCH;
        cpuRegs.GPR.r[8].UD[0] = 0x76543210;
        const auto run = [&](u32 mode) { reinterpret_cast<void (*)()>(outputs[mode].entry)(); };
        const auto check_memory = [&]() {
            const auto* words = reinterpret_cast<const u32*>(PSM(EE_TEST_SCRATCH));
            for (u32 i = 0; i < 16; ++i)
                if (words[i] != 0x76543210) return false;
            return true;
        };
        // Warm both compiled paths before timing and alternate order to reduce
        // first-entry and frequency drift between the helper/direct samples.
        for (u32 i = 0; i < 256; ++i)
        {
            run(i & 1);
            run((i & 1) ^ 1);
        }
        Check(check_memory(), "ir EE scalar 16-store warmup writes exact values on both paths");

        constexpr u32 rounds = 7;
        constexpr u32 iterations = 4096;
        std::vector<double> ns_per_call[2];
        for (u32 round = 0; round < rounds; ++round)
        {
            for (u32 slot = 0; slot < 2; ++slot)
            {
                const u32 mode = slot ^ (round & 1);
                const auto start = std::chrono::steady_clock::now();
                for (u32 i = 0; i < iterations; ++i) run(mode);
                const auto elapsed = std::chrono::duration<double, std::nano>(
                    std::chrono::steady_clock::now() - start).count() / iterations;
                ns_per_call[mode].push_back(elapsed);
                Check(check_memory(), "ir EE scalar 16-store timed batch writes exact values");
            }
        }
        for (u32 mode = 0; mode < 2; ++mode)
        {
            std::sort(ns_per_call[mode].begin(), ns_per_call[mode].end());
            std::printf("BENCH EE scalar stores path=%s rounds=%u iterations=%u stores=16 native_bytes=%u direct_ops=%u cold_compile_us=%.3f median_ns_per_call=%.3f\n",
                mode ? "direct" : "helper", rounds, iterations, outputs[mode].host_size,
                outputs[mode].direct_ee_store_operations, compile_us[mode],
                ns_per_call[mode][ns_per_call[mode].size() / 2]);
        }
    }
    EmuConfig.Cpu.Recompiler = saved_config;
}
