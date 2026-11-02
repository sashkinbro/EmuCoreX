// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+
// Exercise real owner Publish bytes, then the bounded recorder failure controls.
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)
void RunVuAotSpanDumpTests()
{
    using namespace vuaot::spandump;
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    ResetForTests();
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate VU span-dump live Publish fixture");
        return;
    }
    CpuVU0 = &CpuMicroVU0;
    CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve();
    CpuMicroVU1.Reserve();

    const auto previousCache = EmuFolders::Cache;
    const std::string dumpDirectory = FileSystem::GetWorkingDirectory() + "/artifacts/vu-aot-span-red";
    EmuFolders::Cache.clear();
    const ScopedGuard restore([&]() {
        mvu_aot::ForceSynchronous(false);
        mvu_aot::CountEntries(false);
        mvu_aot::SetCompileObservationForTest(false);
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        CpuMicroVU1.Reset();
        ResetForTests();
        EmuFolders::Cache = previousCache;
    });

    Check(FileSystem::EnsureDirectoryExists(dumpDirectory.c_str(), true),
        "create isolated span-dump oracle destination");
    if (!FileSystem::DirectoryExists(dumpDirectory.c_str()))
        return;

    std::vector<u32> code;
    for (u32 pc = 0x80; pc <= 0x100; pc += 8)
        code.insert(code.end(), {NOP_L, NOP_U});
    const auto put = [&](u32 pc, u32 lower, u32 upper = NOP_U) {
        code[(pc - 0x80) / 4] = lower;
        code[(pc - 0x80) / 4 + 1] = upper;
    };
    const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
    const u32 mul = (15u << 21) | (2u << 16) | (1u << 11) | (4u << 6) | 0x2au;
    put(0x80, NOP_L, add);
    put(0x88, (0x20u << 25) | 3u);
    put(0x90, (8u << 25) | (11u << 16) | 0x333u);
    put(0xa8, (8u << 25) | (10u << 16) | 0x222u, mul);
    put(0xb0, NOP_L, NOP_U | 0x40000000u);

    std::array<u32, 12> expectedRings{}, actualRings{};
    const auto expected = RunPersistedVU(1, code, 0x80, 128, true, expectedRings);
    CpuMicroVU1.Reset();
    mvu_aot::ForceSynchronous(true);
    mvu_aot::CountEntries(true);
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;

    // A successful real Publish with recording disabled must not touch the clock or filesystem.
    ResetForTests();
    BeginEpoch(false, dumpDirectory.c_str());
    mvu_aot::SetCompileObservationForTest(true);
    u64 enteredBefore = mvu_aot::NativeRegionEntryCount(0x80);
    u64 childBefore = mvu_aot::NativeRegionEntryCount(0xa8);
    u64 endBefore = mvu_aot::NativeEndCount();
    auto actual = RunPersistedVU(1, code, 0x80, 128, false, actualRings);
    Compare(expected, actual, "disabled span recorder leaves real native architecture unchanged");
    const u64 disabledRootEntries = mvu_aot::NativeRegionEntryCount(0x80) - enteredBefore;
    const u64 disabledChildEntryWrappers = mvu_aot::NativeRegionEntryCount(0xa8) - childBefore;
    const u64 disabledNativeEnds = mvu_aot::NativeEndCount() - endBefore;
    std::printf("VU_AOT_SPAN_EXEC recording=disabled root_entries=%llu child_entry_wrappers=%llu native_ends=%llu vi10=%08x vi11=%08x\n",
        static_cast<unsigned long long>(disabledRootEntries),
        static_cast<unsigned long long>(disabledChildEntryWrappers),
        static_cast<unsigned long long>(disabledNativeEnds), actual.regs.VI[10].UL, actual.regs.VI[11].UL);
    Check(actual.regs.cycle == expected.regs.cycle && actualRings == expectedRings &&
        expected.regs.VI[10].UL == 0x222 && expected.regs.VI[11].UL == 0x333 &&
        actual.regs.VI[10].UL == 0x222 && actual.regs.VI[11].UL == 0x333 &&
        disabledRootEntries > 0 && disabledNativeEnds > 0,
        "disabled recorder preserves real root, internal child effects, and terminal native end");
    const auto compile = mvu_aot::GetCompileObservationForTest();
    bool rootNode = false, childNode = false;
    for (const auto& node : compile.nodes)
    {
        rootNode |= node.pc == 0x80;
        childNode |= node.pc == 0xa8;
    }
    Check(compile.stats.acceptedScopes > 0 && compile.stats.emittedNodes >= 2 && rootNode && childNode,
        "span fixture is a real accepted two-node publish at 0x80 and 0xA8");
    mvu_aot::SetCompileObservationForTest(false);
    Stats stats = GetStats();
    Check(!stats.enabled && stats.clockReads == 0 && stats.spansWritten == 0 &&
        stats.codeBytesWritten == 0 && stats.manifestBytesWritten == 0 && BinaryPathForTests().empty() &&
        ManifestPathForTests().empty(),
        "disabled real Publish performs zero recorder clock reads, file opens, and byte writes");

    CpuMicroVU1.Reset();
    ResetForTests();
    BeginEpoch(true, dumpDirectory.c_str());
    mvu_aot::SetCompileObservationForTest(true);
    actualRings = {};
    enteredBefore = mvu_aot::NativeRegionEntryCount(0x80);
    childBefore = mvu_aot::NativeRegionEntryCount(0xa8);
    endBefore = mvu_aot::NativeEndCount();
    actual = RunPersistedVU(1, code, 0x80, 128, false, actualRings);
    Compare(expected, actual, "enabled span recorder leaves real native architecture unchanged");
    const u64 enabledRootEntries = mvu_aot::NativeRegionEntryCount(0x80) - enteredBefore;
    const u64 enabledChildEntryWrappers = mvu_aot::NativeRegionEntryCount(0xa8) - childBefore;
    const u64 enabledNativeEnds = mvu_aot::NativeEndCount() - endBefore;
    std::printf("VU_AOT_SPAN_EXEC recording=enabled root_entries=%llu child_entry_wrappers=%llu native_ends=%llu vi10=%08x vi11=%08x\n",
        static_cast<unsigned long long>(enabledRootEntries),
        static_cast<unsigned long long>(enabledChildEntryWrappers),
        static_cast<unsigned long long>(enabledNativeEnds), actual.regs.VI[10].UL, actual.regs.VI[11].UL);
    Check(actual.regs.cycle == expected.regs.cycle && actualRings == expectedRings &&
        expected.regs.VI[10].UL == 0x222 && expected.regs.VI[11].UL == 0x333 &&
        actual.regs.VI[10].UL == 0x222 && actual.regs.VI[11].UL == 0x333 &&
        enabledRootEntries > 0 && enabledNativeEnds > 0,
        "enabled recorder preserves real root, internal child effects, and terminal native end");
    mvu_aot::SetCompileObservationForTest(false);
    mvu_aot::PublishedSpanForTest live;
    Check(mvu_aot::CopyLastPublishedSpanForTest(live) && live.rootPC == 0x80 &&
        live.nodes.size() >= 2 && live.nodes[0].pc == 0x80,
        "recorder exposes the still-live owner allocation and nodes for byte-for-byte verification");

    stats = GetStats();
    Check(stats.enabled && !stats.failedThisEpoch && stats.spansWritten > 0 &&
        stats.lastSpanBytes == live.bytes.size() && stats.codeBytesWritten > 0,
        "actual Publish writes a complete bounded final owner span");
    const auto binary = FileSystem::ReadBinaryFile(BinaryPathForTests().c_str());
    const auto manifest = FileSystem::ReadFileToString(ManifestPathForTests().c_str());
    const bool fileRangeValid = binary && stats.lastBinaryOffset <= binary->size() &&
        stats.lastSpanBytes <= binary->size() - stats.lastBinaryOffset;
    Check(fileRangeValid, "recorded span binary offset and total length are within the file");
    if (fileRangeValid)
    {
        Check(std::equal(live.bytes.begin(), live.bytes.end(), binary->begin() + stats.lastBinaryOffset),
            "recorded executable bytes exactly match the still-live post-fixup owner allocation");
    }

    const auto hex64 = [](u64 value) {
        char text[17]{};
        std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
        return std::string(text);
    };
    bool manifestMatches = manifest && live.registeredCodeBytes > 0 &&
        live.registeredCodeBytes <= live.bytes.size() && stats.lastSpanBytes == live.bytes.size();
    const auto contains = [&](const std::string& needle) {
        return manifest && manifest->find(needle) != std::string::npos;
    };
    manifestMatches &= contains("\"event\":\"span\"") &&
        contains("\"generation\":" + std::to_string(live.generation)) &&
        contains("\"owner_ordinal\":" + std::to_string(live.ownerOrdinal)) &&
        contains("\"base\":\"0x" + hex64(live.base) + "\"") &&
        contains("\"total_bytes\":" + std::to_string(live.bytes.size())) &&
        contains("\"binary_offset\":" + std::to_string(stats.lastBinaryOffset)) &&
        contains("\"registered_code_bytes\":" + std::to_string(live.registeredCodeBytes)) &&
        contains("\"bridge_offset\":" + std::to_string(live.registeredCodeBytes)) &&
        contains("\"bridge_bytes\":" + std::to_string(live.bytes.size() - live.registeredCodeBytes)) &&
        contains("\"root_pc\":" + std::to_string(live.rootPC)) &&
        contains("\"sequence\":" + std::to_string(stats.processSequence)) &&
        contains("\"span_hash\":\"xxh3_64:" + hex64(GSXXH3_64bits(live.bytes.data(), live.bytes.size())) + "\"") &&
        contains("\"root_key_hash\":\"xxh3_64:" + hex64(GSXXH3_64bits(live.rootKey.data(), live.rootKey.size())) + "\"") &&
        contains("\"micro_snapshot_hash\":\"xxh3_64:" + hex64(live.microSnapshotHash) + "\"");
    for (const auto& node : live.nodes)
    {
        const u64 keyHash = GSXXH3_64bits(node.key.data(), node.key.size());
        const std::string nodeNeedle = "\"pc\":" + std::to_string(node.pc) +
            ",\"code_offset\":" + std::to_string(node.codeOffset) +
            ",\"entry_offset\":" + std::to_string(node.entryOffset) +
            ",\"internal_key_hash\":\"xxh3_64:" + hex64(keyHash) + "\"";
        manifestMatches &= node.codeOffset < live.registeredCodeBytes &&
            node.entryOffset < live.bytes.size() && contains(nodeNeedle);
    }
    manifestMatches &= !live.importedUnknown && contains("\"kind_origin\":\"live_region\"");
    Check(manifestMatches,
        "manifest hashes, generation, range split, and every live node offset and exact pipeline key match");

    const u64 firstSequence = stats.processSequence;
    const u64 firstGeneration = live.generation;
    CpuMicroVU1.Reset();
    stats = GetStats();
    const auto resetManifest = FileSystem::ReadFileToString(ManifestPathForTests().c_str());
    const size_t endStart = resetManifest ? resetManifest->find("\"event\":\"end_generation\"") : std::string::npos;
    const size_t endStop = resetManifest ? resetManifest->find('\n', endStart) : std::string::npos;
    const std::string endRow = resetManifest && endStart != std::string::npos ?
        resetManifest->substr(endStart, endStop == std::string::npos ? std::string::npos : endStop - endStart) : std::string();
    Check(stats.resetEventsWritten == 1 && stats.processSequence > firstSequence &&
        endRow.find("\"generation\":" + std::to_string(firstGeneration)) != std::string::npos &&
        endRow.find("\"sequence\":" + std::to_string(stats.processSequence)) != std::string::npos,
        "Reset writes the old generation end event before freeing its owner jobs");

    BeginEpoch(true, dumpDirectory.c_str());
    mvu_aot::SetCompileObservationForTest(true);
    actualRings = {};
    actual = RunPersistedVU(1, code, 0x80, 128, false, actualRings);
    Compare(expected, actual, "next generation recorder preserves native architecture");
    Check(actual.regs.cycle == expected.regs.cycle && actualRings == expectedRings,
        "next generation still matches exact cycles and flag rings");
    mvu_aot::SetCompileObservationForTest(false);
    mvu_aot::PublishedSpanForTest nextLive;
    const bool nextCopied = mvu_aot::CopyLastPublishedSpanForTest(nextLive);
    const Stats nextStats = GetStats();
    Check(nextCopied && nextLive.generation > firstGeneration && nextStats.processSequence > firstSequence &&
        nextStats.spansWritten >= 2,
        "next real generation keeps the process sequence monotonic across Reset");
    CpuMicroVU1.Reset();
    Check(GetStats().resetEventsWritten == 2,
        "generation end markers remain process-lifetime bounded events across Reset");

    // Exercise explicit whole-record cap behavior and partial-write failure without the JIT.
    ResetForTests();
    BeginEpoch(true, dumpDirectory.c_str());
    SetLimitsForTests(1, 1024, 4);
    const std::array<u8, 8> smallCode{1,2,3,4,5,6,7,8};
    std::array<u8, 96> smallKey{};
    PublishedSpan synthetic;
    synthetic.base = smallCode.data();
    synthetic.totalBytes = smallCode.size();
    synthetic.registeredCodeBytes = smallCode.size();
    synthetic.rootKey = smallKey.data();
    Check(RecordPublishedSpan(synthetic) == Result::SkippedLimit && GetStats().spansSkipped == 1 &&
        GetStats().codeBytesWritten == 0,
        "span cap skips the entire record and emits no partial executable range");

    ResetForTests();
    BeginEpoch(true, dumpDirectory.c_str());
    SetWriteFailureAfterBytesForTests(2);
    const Result writeResult = RecordPublishedSpan(synthetic);
    stats = GetStats();
    Check(writeResult == Result::Failed && stats.failedThisEpoch && stats.fileFailures == 1 &&
        stats.spansWritten == 0 && stats.manifestBytesWritten == 0,
        "short binary write disables the epoch without appending a false manifest span");
}
#else
void RunVuAotSpanDumpTests() {}
#endif
