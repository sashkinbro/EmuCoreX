// SPDX-FileCopyrightText: 2026 ARMSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
// Adapted for EmuCoreX's owner-thread cache lifecycle.
namespace mvu_aot::sticky
{
    using Cause = vuaot::live::TripCause;
    struct State
    {
        std::atomic<bool> active{false}, tripped{false}, dropLive{false}, rebuildDue{false};
        std::atomic<u32> cause{0};
        std::atomic<u64> trips{0}, resets{0}, dropped{0}, rejected{0};
        std::mutex titleMutex;
        std::string title;
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
        bool keepExact = true, manualKeepExact = false;
#endif
    };
    State& state = *new State(); // Referenced by generated code until process shutdown.
    std::string PathName()
    {
        return EmuFolders::Cache.empty() ? std::string{} :
            Path::Combine(Path::Combine(EmuFolders::Cache, "vu_jit"), "vuaot_tripped.txt");
    }
    bool RememberedLocked(const std::string& path)
    {
        if (state.title.empty() || path.empty()) return false;
        const auto text = FileSystem::ReadFileToString(path.c_str());
        if (!text) return false;
        size_t start = 0;
        while (start < text->size())
        {
            size_t end = text->find('\n', start);
            if (end == std::string::npos) end = text->size();
            if (text->compare(start, end - start, state.title) == 0) return true;
            start = end + 1;
        }
        return false;
    }
    void Remember()
    {
        std::lock_guard<std::mutex> lock(state.titleMutex);
        const std::string path = PathName();
        if (state.title.empty() || path.empty() || RememberedLocked(path)) return;
        Error error;
        if (!FileSystem::EnsureDirectoryExists(std::string(Path::GetDirectory(path)).c_str(), true, &error)) return;
        if (FILE* file = FileSystem::OpenCFile(path.c_str(), "ab"))
        {
            const std::string line = state.title + "\n";
            std::fwrite(line.data(), 1, line.size(), file);
            std::fclose(file);
        }
    }
    void Trip(Cause cause)
    {
        if (!state.active.load(std::memory_order_acquire) || state.tripped.exchange(true, std::memory_order_acq_rel)) return;
        state.cause.store(static_cast<u32>(cause), std::memory_order_relaxed);
        if (cause != Cause::Remembered) state.trips.fetch_add(1, std::memory_order_relaxed);
        if (state.dropLive.load(std::memory_order_acquire)) state.rebuildDue.store(true, std::memory_order_release);
        if (cause != Cause::Remembered) Remember();
    }
    bool Allowed()
    {
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
        if (state.keepExact) return false;
#else
        // Enable only after the actual outside-reader and rebuild gates pass.
        return false;
#endif
        return state.active.load(std::memory_order_acquire) && !state.tripped.load(std::memory_order_acquire);
    }
    void Observe(const u8* micro, Cause cause)
    {
        if (state.active.load(std::memory_order_acquire) && !state.tripped.load(std::memory_order_acquire) &&
            vuaot::RegionTier::MicroReadsSticky(micro, 1)) Trip(cause);
    }
}
void vuAotNoteOutsideStatusRead()
{
    mvu_aot::sticky::Trip(vuaot::live::TripCause::OutsideRead);
}
namespace vuaot::live
{
    bool StickyTripped() { return mvu_aot::sticky::state.tripped.load(std::memory_order_acquire); }
    TripCause StickyTripCause() { return static_cast<TripCause>(mvu_aot::sticky::state.cause.load(std::memory_order_relaxed)); }
    void ResetStickyTripwire()
    {
        // Called only at a quiescent VM boot/reset, never at a cache/savestate reset.
        auto& state = mvu_aot::sticky::state;
        std::lock_guard<std::mutex> lock(state.titleMutex);
        const bool remembered = mvu_aot::sticky::RememberedLocked(mvu_aot::sticky::PathName());
        state.cause.store(static_cast<u32>(remembered ? TripCause::Remembered : TripCause::None), std::memory_order_relaxed);
        state.tripped.store(remembered, std::memory_order_release);
    }
    void SetTitle(const std::string& serial, u32 crc)
    {
        auto& state = mvu_aot::sticky::state;
        bool remembered;
        {
            std::lock_guard<std::mutex> lock(state.titleMutex);
            state.title = serial.empty() ? std::string{} : fmt::format("{} {:08X}", serial, crc);
            remembered = mvu_aot::sticky::RememberedLocked(mvu_aot::sticky::PathName());
        }
        if (remembered) mvu_aot::sticky::Trip(TripCause::Remembered);
    }
}
