// SPDX-License-Identifier: GPL-3.0+
void RunVuAotStickyTests()
{
    const u32 sub = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x2cu;
    const u32 add = (sub & ~0x3fu) | 0x28u;
    const std::vector<u32> code{(0x20u << 25) | 3u, sub, NOP_L, NOP_U,
        NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, add,
        NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U,
        NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
    mvu_aot::ForceSynchronous(true);
    mvu_aot::CountEntries(true);
    mvu_aot::AllowStickyElisionForTest(false);
    vuaot::live::SetTitle({}, 0);
    for (bool flagHack : {false, true})
    {
        EmuConfig.Speedhacks.vuFlagHack = flagHack;
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        std::array<u32, 12> expectedRings{}, rings{};
        const auto expected = RunPersistedVU(1, code, 0x80, 128, true, expectedRings);
        EmuConfig.Cpu.Recompiler.EnableVUAot = true;
        vuaot::live::ResetStickyTripwire();
        const auto actual = RunPersistedVU(1, code, 0x80, 128, true, rings);
        const u64 trips = mvu_aot::GetStats().stickyTrips;
        GET_VU_MEM(&VU0, 0x4200); // unrelated VI window must not trip
        Check(!vuaot::live::StickyTripped(), "unrelated VU0 register window leaves sticky tripwire clear");
        const u32 first = *GET_VU_MEM(&VU0, 0x4300);
        Check(first == expected.regs.VI[REG_STATUS_FLAG].UL,
            "first outside STATUS read already equals baseline, before any rebuild");
        Check(vuaot::live::StickyTripped() &&
            vuaot::live::StickyTripCause() == vuaot::live::TripCause::OutsideRead &&
            mvu_aot::GetStats().stickyTrips == trips + 1,
            "STATUS window records exactly one outside-reader trip");
        GET_VU_MEM(&VU0, 0x430c);
        Check(mvu_aot::GetStats().stickyTrips == trips + 1, "mirrored STATUS lane does not retrip");
        Compare(expected, actual, "exact sticky policy preserves the entire region result");
        Check(expected.regs.cycle == actual.regs.cycle && expectedRings == rings,
            "exact sticky policy preserves cycles and internal rings");
        CpuMicroVU1.Reset();
        Check(vuaot::live::StickyTripped(), "ordinary code-cache reset never rearms sticky policy");
    }

    // Negative control: the upstream optimistic policy really loses history
    // before its first outside reader. It must not become the shipped policy.
    EmuConfig.Speedhacks.vuFlagHack = false;
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    std::array<u32, 12> expectedRings{}, rings{};
    const auto expected = RunPersistedVU(1, code, 0x80, 128, true, expectedRings);
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    vuaot::live::ResetStickyTripwire();
    mvu_aot::AllowStickyElisionForTest(true);
    const auto before = mvu_aot::GetStats();
    RunPersistedVU(1, code, 0x80, 128, true, rings);
    Check(mvu_aot::GetStats().stickyDropped > before.stickyDropped,
        "negative control actually executes a region built with sticky history elided");
    const u32 lost = *GET_VU_MEM(&VU0, 0x4300);
    Check(lost != expected.regs.VI[REG_STATUS_FLAG].UL,
        "negative control exposes first-read history loss from optimistic elision");
    const auto rebuilt = RunPersistedVU(1, code, 0x80, 128, false, rings);
    Check(mvu_aot::GetStats().stickyResets == before.stickyResets + 1,
        "outside trip requests one rebuild at the next fresh-program boundary");
    Compare(expected, rebuilt, "fresh-program rebuild restores exact sticky execution");
    Check(expected.regs.cycle == rebuilt.regs.cycle && expectedRings == rings,
        "fresh-program rebuild preserves exact cycles and rings");
    mvu_aot::AllowStickyElisionForTest(false);
    CpuMicroVU1.Reset();

    // A worker can finish optimistic code before an outside reader trips.
    // It must be refused at publication even when no dropped code was live.
    vuaot::live::ResetStickyTripwire();
    mvu_aot::AllowStickyElisionForTest(true);
    mvu_aot::ForceSynchronous(false);
    mvu_aot::SetTierUpRuns(1);
    const auto rejected = mvu_aot::GetStats().stickyRejected;
    RunPersistedVU(1, code, 0x80, 128, true, rings);
    mvu_aot::WaitWorker();
    Check(mvu_aot::PendingCount() == 1, "completed optimistic worker job waits for owner publication");
    GET_VU_MEM(&VU0, 0x4300);
    const auto published = RunPersistedVU(1, code, 0x80, 128, false, rings);
    Check(mvu_aot::GetStats().stickyRejected > rejected,
        "owner rejects completed dropped-sticky job after an outside reader trips");
    Compare(expected, published, "rejected optimistic job leaves exact baseline execution");
    Check(expected.regs.cycle == published.regs.cycle && expectedRings == rings,
        "rejected job preserves exact cycles and rings");
    const auto exactEntry = mvu_aot::EntryCount();
    mvu_aot::WaitWorker();
    RunPersistedVU(1, code, 0x80, 128, false, rings);
    Check(mvu_aot::EntryCount() > exactEntry,
        "publication rejection does not permanently decline a root that can rebuild exact");
    mvu_aot::AllowStickyElisionForTest(false);
    mvu_aot::ForceSynchronous(true);
    mvu_aot::SetTierUpRuns(200);
    CpuMicroVU1.Reset();

    // A dormant reader still prevents a dropped region from being installed.
    vuaot::live::ResetStickyTripwire();
    auto reader = code;
    reader.insert(reader.end(), {(0x16u << 25) | (1u << 16) | 0x40u, NOP_U}); // FSAND sticky Z
    RunPersistedVU(1, reader, 0x80, 128, true, rings);
    Check(vuaot::live::StickyTripped() &&
        vuaot::live::StickyTripCause() == vuaot::live::TripCause::ProgramImage,
        "new program image preflights even an unexecuted sticky reader");

    const auto savedCache = EmuFolders::Cache;
    EmuFolders::Cache = "/data/local/tmp/emucorex-sticky-title-" + std::to_string(getpid());
    vuaot::live::SetTitle("ORACLE-STICKY-A", 0x1234);
    vuaot::live::ResetStickyTripwire();
    GET_VU_MEM(&VU0, 0x4300);
    vuaot::live::ResetStickyTripwire();
    Check(vuaot::live::StickyTripCause() == vuaot::live::TripCause::Remembered,
        "title that exposed a sticky reader starts its next session exact");
    vuaot::live::SetTitle("ORACLE-STICKY-B", 0x5678);
    vuaot::live::ResetStickyTripwire();
    Check(!vuaot::live::StickyTripped(), "remembered reader policy does not leak into another title");
    vuaot::live::SetTitle({}, 0);
    EmuFolders::Cache = savedCache;
    vuaot::live::ResetStickyTripwire();
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    CpuMicroVU1.Reset();
    mvu_aot::ForceSynchronous(false);
    mvu_aot::CountEntries(false);
}
