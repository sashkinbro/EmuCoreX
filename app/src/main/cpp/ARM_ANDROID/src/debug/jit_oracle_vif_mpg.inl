// SPDX-License-Identifier: GPL-3.0+
// Real VIF MPG uploads followed by reuse of actual compiled microVU programs.
extern "C" void EmuCoreXOracleUploadVifMpg(u32, u32, const u32*, u32);
extern "C" const void* EmuCoreXOracleGetVUQuick(u32, u32);

void VifMpgTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate emulator memory for VIF MPG uploads");
        return;
    }
    CpuVU0 = &CpuMicroVU0;
    CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve();
    CpuMicroVU1.Reserve();
    EmuCoreXOracleSetVUIR(0, 0);
    EmuCoreXOracleSetVUIR(1, 0);
    struct RestoreIrOverrides
    {
        ~RestoreIrOverrides() { EmuCoreXOracleSetVUIR(0, -1); EmuCoreXOracleSetVUIR(1, -1); }
    } restore_ir;
    OpcodeFamilies::SetFamilyMask(OpcodeFamilies::CORE_VU0, 0);
    OpcodeFamilies::SetFamilyMask(OpcodeFamilies::CORE_VU1, 0);

    // Exact end, wrapped first instruction, wrapped delay slot, larger split,
    // and continuation at the one-past-end address left by an exact-end chunk.
    const u32 splits[][2] = {{16, 0}, {8, 8}, {8, 16}, {16, 24}, {0, 16},
        {2048, 0}, {8, 2040}, {2040, 8}, {0, 2048},
        {4, 4}, {4, 8}, {8, 4}, {0, 4}, {4, 2044}};
    for (u32 index = 0; index < 2; ++index)
    for (const auto& split : splits)
    {
        VURegs& vu = index ? VU1 : VU0;
        BaseVUmicroCPU* jit = index ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU1) : &CpuMicroVU0;
        const u32 size = index ? 0x4000 : 0x1000;
        const u32 tail_bytes = split[0], head_bytes = split[1];
        const u32 upload_addr = size - tail_bytes;
        const u32 middle_pc = ((head_bytes + upload_addr) / 2) & ~7u;
        std::printf("VIFMPG vu=%u addr=%04x tail=%u head=%u\n", index, upload_addr, tail_bytes, head_bytes);
        jit->Reset();
        for (u32 pc = 0; pc < size; pc += 8)
        {
            std::memcpy(vu.Micro + pc, &NOP_L, 4);
            std::memcpy(vu.Micro + pc + 4, &NOP_U, 4);
        }
        auto program = [&](u32 pc, u32 reg, u32 value) {
            const u32 pair[] = {(8u << 25) | (reg << 16) | value, NOP_U | 0x40000000u}; // IADDIU + E
            std::memcpy(vu.Micro + pc, pair, sizeof(pair));
        };
        program(0, 1, 17);
        program(middle_pc, 2, 5);
        program(size - 16, 3, 9);
        std::memset(vu.Mem, 0, size);
        VURegs initial{};
        initial.Micro = vu.Micro;
        initial.Mem = vu.Mem;
        initial.idx = index;
        initial.VF[0].f.w = 1.0f;
        auto execute = [&](bool recompiled, u32 pc) {
            const FPControlRegisterBackup caller_fpcr(EmuConfig.Cpu.FPUFPCR);
            vu = initial;
            VU0.VI[REG_VPU_STAT].UL = index ? 0x100 : 1;
            vu.VI[REG_TPC].UL = pc / 8;
            BaseVUmicroCPU* cpu = recompiled ? jit : index ? static_cast<BaseVUmicroCPU*>(&CpuIntVU1) : &CpuIntVU0;
            if (!recompiled)
                cpu->Reset();
            cpu->SetStartPC(pc);
            for (u32 call = 0; call < 128 && (VU0.VI[REG_VPU_STAT].UL & (index ? 0x100 : 1)); ++call)
                cpu->Execute(128);
        };
        auto run = [&](bool recompiled, u32 pc) {
            execute(recompiled, pc);
            return Snapshot{vu, std::vector<u8>(vu.Mem, vu.Mem + size), VU0.VI[REG_VPU_STAT].UL,
                false, std::vector<u8>(vu.Micro, vu.Micro + size), {}, false};
        };
        const auto old_head = run(true, 0);
        const auto old_middle = run(true, middle_pc);
        const auto old_tail = run(true, size - 16);
        const void* head_cache = EmuCoreXOracleGetVUQuick(index, 0);
        const void* middle_cache = EmuCoreXOracleGetVUQuick(index, middle_pc);
        const void* tail_cache = EmuCoreXOracleGetVUQuick(index, size - 16);
        Check(old_head.regs.VI[1].UL == 17 && old_middle.regs.VI[2].UL == 5 && old_tail.regs.VI[3].UL == 9 &&
            head_cache && middle_cache && tail_cache, "VIF MPG fixture warms head, middle and tail JIT programs");

        if (tail_bytes == 16 && head_bytes == 0)
        {
            // An E-bit delay slot at the last instruction consumes no head
            // bytes. Unrelated writes must retain this actual compiled entry.
            jit->Clear(middle_pc + 32, 4);
            Check(EmuCoreXOracleGetVUQuick(index, size - 16) == tail_cache,
                "microVU exact-boundary E-bit program survives a disjoint write");
            std::array<double, 9> warm_times{}, write_times{};
            auto timed = [&](bool clear) {
                const auto start = std::chrono::steady_clock::now();
                for (u32 repeat = 0; repeat < 10000; ++repeat)
                {
                    if (clear)
                        jit->Clear(middle_pc + 32, 4);
                    execute(true, size - 16);
                }
                return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 10000;
            };
            for (u32 batch = 0; batch < warm_times.size(); ++batch)
            {
                if (batch & 1) { write_times[batch] = timed(true); warm_times[batch] = timed(false); }
                else { warm_times[batch] = timed(false); write_times[batch] = timed(true); }
            }
            std::sort(warm_times.begin(), warm_times.end());
            std::sort(write_times.begin(), write_times.end());
            std::printf("VUBOUNDARY vu=%u warm_median_ns=%.1f disjoint_write_median_ns=%.1f\n",
                index, warm_times[4], write_times[4]);
            Check(vu.VI[3].UL == 9 && EmuCoreXOracleGetVUQuick(index, size - 16) == tail_cache,
                "microVU repeated disjoint writes retain exact-boundary code and behavior");
        }

        std::vector<u32> payload((tail_bytes + head_bytes) / 4);
        for (u32 word = 0; word < payload.size(); ++word)
            payload[word] = ((upload_addr / 4 + word) & 1) ? NOP_U : NOP_L;
        if (head_bytes)
        {
            payload[tail_bytes / 4] = (8u << 25) | (1u << 16) | 23u;
            if (head_bytes >= 8)
                payload[tail_bytes / 4 + 1] = NOP_U | 0x40000000u;
        }
        std::vector<u8> expected_memory(vu.Micro, vu.Micro + size);
        const u8* upload_bytes = reinterpret_cast<const u8*>(payload.data());
        for (u32 byte = 0; byte < payload.size() * 4; ++byte)
            expected_memory[(upload_addr + byte) % size] = upload_bytes[byte];
        vifStruct& vif = index ? vif1 : vif0;
        vif.ClearQueuedProgram();
        vif.tag.addr = upload_addr;
        EmuCoreXOracleUploadVifMpg(index, upload_addr, payload.data(), static_cast<u32>(payload.size()));
        Check(std::memcmp(expected_memory.data(), vu.Micro, size) == 0 && vif.tag.addr == (head_bytes ? head_bytes : size),
            "VIF MPG upload writes the wrapped byte stream and advances its continuation address");
        Check((head_bytes ? EmuCoreXOracleGetVUQuick(index, 0) == nullptr : EmuCoreXOracleGetVUQuick(index, 0) == head_cache) &&
            (tail_bytes ? EmuCoreXOracleGetVUQuick(index, size - 16) == nullptr :
                EmuCoreXOracleGetVUQuick(index, size - 16) == tail_cache) &&
            EmuCoreXOracleGetVUQuick(index, middle_pc) == middle_cache,
            "VIF MPG invalidates both written segments while preserving disjoint cached programs");

        // Do not reset the JIT here: the bug is reuse of a stale head entry.
        const auto actual = run(true, 0);
        const auto expected = run(false, 0);
        Compare(expected, actual, "cached microVU after VIF MPG upload");
        Check(actual.regs.VI[1].UL == (head_bytes ? 23u : 17u) && expected.regs.VI[1].UL == (head_bytes ? 23u : 17u),
            "VIF MPG cached execution observes the newly uploaded instruction");
        const auto middle = run(true, middle_pc);
        Check(middle.regs.VI[2].UL == 5 && EmuCoreXOracleGetVUQuick(index, middle_pc) == middle_cache,
            "VIF MPG disjoint program retains its compiled quick entry and behavior");

        if (tail_bytes == 16 && head_bytes == 0)
        {
            jit->Reset();
            // E at the last instruction really executes its delay at zero.
            // Its head coverage must remain tracked and invalidated on write.
            program(size - 8, 3, 9);
            const u32 head[] = {(8u << 25) | (1u << 16) | 17u, NOP_U};
            std::memcpy(vu.Micro, head, sizeof(head));
            const auto wrapped = run(true, size - 8);
            const void* wrapped_cache = EmuCoreXOracleGetVUQuick(index, size - 8);
            jit->Clear(middle_pc + 32, 4);
            Check(wrapped.regs.VI[1].UL == 17 && wrapped.regs.VI[3].UL == 9 && wrapped_cache &&
                EmuCoreXOracleGetVUQuick(index, size - 8) == wrapped_cache,
                "microVU wrapped E-bit delay program retains disjoint coverage");
            const u32 new_lower = (8u << 25) | (1u << 16) | 23u;
            vif.tag.addr = 0;
            EmuCoreXOracleUploadVifMpg(index, 0, &new_lower, 1);
            Check(EmuCoreXOracleGetVUQuick(index, size - 8) == nullptr,
                "microVU wrapped E-bit delay program invalidates on a head write");
            const auto new_wrapped = run(true, size - 8);
            Compare(run(false, size - 8), new_wrapped, "wrapped E-bit delay after head upload");
            Check(new_wrapped.regs.VI[1].UL == 23 && new_wrapped.regs.VI[3].UL == 9,
                "microVU wrapped E-bit delay executes the uploaded instruction");
        }
    }
}
