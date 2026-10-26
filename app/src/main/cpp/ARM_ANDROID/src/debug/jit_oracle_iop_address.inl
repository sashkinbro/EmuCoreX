// SPDX-License-Identifier: GPL-3.0+
// Direct IOP address arithmetic/allocator coverage. Included after snapshot and
// RunIOPProgram helpers; the enclosing iop-direct suite forces shared IR off.
void IOPLegacyAddressTests()
{
    u32 cases = 0;
    for (u32 op : {32u, 36u, 33u, 37u, 35u, 34u, 38u, 40u, 41u, 43u, 42u, 46u})
    for (u32 operand : {0u, 16u, 18u})
    {
        u32 reference_cycle = 0;
        bool have_reference = false;
        for (u32 mirror : {0u, 0x80000000u})
        for (s32 offset : {-32768, -4096, -4095, -1, 0, 1, 4095, 4096, 32767})
        {
            const bool merge = op == 34 || op == 38 || op == 42 || op == 46;
            const u32 target = (IOP_TEST_SCRATCH + (merge ? 131 : 128)) | mirror;
            const u32 base = target - static_cast<u32>(offset);
            const std::vector<u32> code = {
                MipsI(15, 0, 22, IOP_TEST_SCRATCH >> 16), MipsI(13, 22, 22, IOP_TEST_SCRATCH & 0xffff),
                MipsI(15, 0, 8, base >> 16), MipsI(13, 8, 8, base & 0xffff),
                MipsI(43, 22, 8, 0), MipsI(35, 22, 16, 0), 0, // dynamic cached r16
                MipsI(15, 0, 18, 0x89ab), MipsI(13, 18, 18, 0xcdef),
                MipsI(op, 16, operand, offset), 0,
                MipsR(16, 0, 20, 0, 0x21), MipsI(43, 22, 20, 160)};
            const IOPSnapshot expected = RunIOPProgram(false, code);
            const IOPSnapshot actual = RunIOPProgram(true, code);
            char name[128];
            std::snprintf(name, sizeof(name), "direct IOP address op=%u operand=%u offset=%d mirror=%08x", op, operand, offset, mirror);
            CompareIOP(expected, actual, name);
            Check(actual.gpr[0] == 0, "direct IOP address operations preserve zero register");
            // Every fixture in this group has identical guest opcode counts
            // and touches the same physical RAM. Folding changes host work only.
            if (have_reference)
                Check(actual.cycle == reference_cycle, "direct IOP folded/fallback offsets and RAM mirrors retain exact cycles");
            else
            {
                reference_cycle = actual.cycle;
                have_reference = true;
            }
            ++cases;
        }
    }

    // Rs==zero takes the constant operand allocator path. Seed and restore
    // these low-RAM words explicitly because the normal snapshot window is
    // at IOP_TEST_SCRATCH; copy the result into that window for comparison.
    for (u32 op : {32u, 36u, 33u, 37u, 35u, 34u, 38u, 40u, 41u, 43u, 42u, 46u})
    for (u32 word : {256u, 4092u})
    {
        const bool merge = op == 34 || op == 38 || op == 42 || op == 46;
        const u32 saved = iopMemRead32(word);
        const std::vector<u32> code = {
            MipsI(15, 0, 22, IOP_TEST_SCRATCH >> 16), MipsI(13, 22, 22, IOP_TEST_SCRATCH & 0xffff),
            MipsI(15, 0, 9, 0x8765), MipsI(13, 9, 9, 0x4321), MipsI(43, 0, 9, word),
            MipsI(15, 0, 18, 0x89ab), MipsI(13, 18, 18, 0xcdef),
            MipsI(op, 0, 18, word + (merge ? 3 : 0)), 0,
            MipsI(35, 0, 19, word), 0, MipsI(43, 22, 19, 192)};
        const IOPSnapshot expected = RunIOPProgram(false, code);
        const IOPSnapshot actual = RunIOPProgram(true, code);
        char name[96];
        std::snprintf(name, sizeof(name), "direct IOP zero-base address op=%u offset=%u", op, word);
        CompareIOP(expected, actual, name);
        Check(actual.gpr[0] == 0, "direct IOP zero-base address preserves zero register");
        iopMemWrite32(word, saved);
        ++cases;
    }

    // A dynamic zero-destination MMIO read must still execute its handler.
    // HW_ICTRL reads clear the register, making a skipped/misaddressed read
    // observable independently of the full CPU/memory snapshots.
    const u32 saved_ictrl = psxHu32(HW_ICTRL);
    for (u32 operand : {0u, 16u, 18u})
    for (s32 offset : {-4096, -4095, 0, 4095, 4096})
    {
        const u32 base = HW_ICTRL - static_cast<u32>(offset);
        const std::vector<u32> code = {
            MipsI(15, 0, 22, IOP_TEST_SCRATCH >> 16), MipsI(13, 22, 22, IOP_TEST_SCRATCH & 0xffff),
            MipsI(15, 0, 8, base >> 16), MipsI(13, 8, 8, base & 0xffff),
            MipsI(43, 22, 8, 0), MipsI(35, 22, 16, 0), 0,
            MipsI(35, 16, operand, offset), 0, MipsR(16, 0, 20, 0, 0x21)};
        psxHu32(HW_ICTRL) = 0x87654321u;
        const IOPSnapshot expected = RunIOPProgram(false, code);
        Check(psxHu32(HW_ICTRL) == 0, "direct IOP address MMIO interpreter executes read-clear handler");
        psxHu32(HW_ICTRL) = 0x87654321u;
        const IOPSnapshot actual = RunIOPProgram(true, code);
        char name[96];
        std::snprintf(name, sizeof(name), "direct IOP MMIO address operand=%u offset=%d", operand, offset);
        CompareIOP(expected, actual, name);
        Check(psxHu32(HW_ICTRL) == 0 && actual.gpr[0] == 0,
            "direct IOP address MMIO handler executes even for the zero destination");
        ++cases;
    }
    psxHu32(HW_ICTRL) = saved_ictrl;
    std::printf("DIRECT IOP address cases=%u\n", cases);
}
