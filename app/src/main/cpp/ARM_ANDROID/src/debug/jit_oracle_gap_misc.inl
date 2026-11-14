// SPDX-License-Identifier: GPL-3.0+
// Coverage-gap oracle suites (IOP random blocks, VIF unpack, VU programs, MTVU/XGKICK, EE SMC).
// Included from jit_oracle.cpp inside the anonymous namespace, after jit_oracle_iop_thread.inl.
// Common knobs (environment): GAP_SEED (base seed), GAP_COUNT (cases per sub-suite, 0 = suite default),
// plus per-suite GAP_<SUITE>_SEED / GAP_<SUITE>_COUNT. Every mismatch FAILs; nothing is allow-listed.

struct GapRng
{
    u64 s;
    explicit GapRng(u64 seed) : s(seed * 0x9e3779b97f4a7c15ull + 0xd1b54a32d192ed03ull) { Next64(); }
    u64 Next64()
    {
        s += 0x9e3779b97f4a7c15ull;
        u64 z = s;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    u32 Next() { return static_cast<u32>(Next64() >> 32); }
    u32 Below(u32 n) { return n ? Next() % n : 0; }
    bool Chance(u32 percent) { return Below(100) < percent; }
};

u64 GapKnob(const char* name, u64 def)
{
    const char* v = std::getenv(name);
    return (v && *v) ? std::strtoull(v, nullptr, 0) : def;
}

// Per-group failure aggregation: first (smallest) minimized reproducer is printed once per group.
struct GapGroups
{
    struct G { u64 count = 0; std::string example; size_t exampleSize = ~size_t(0); };
    std::map<std::string, G> groups;
    u64 cases = 0, fails = 0;
    void Record(const std::string& key, size_t size, const std::string& example)
    {
        G& g = groups[key];
        ++g.count;
        ++fails;
        if (size < g.exampleSize) { g.exampleSize = size; g.example = example; }
    }
    // Prints the summary and issues one FAIL-gated Check per mismatch group plus one overall check.
    void Report(const char* suite)
    {
        std::printf("GAP %s: cases=%llu mismatching=%llu groups=%zu\n", suite,
            static_cast<unsigned long long>(cases), static_cast<unsigned long long>(fails), groups.size());
        for (const auto& kv : groups)
        {
            std::printf("GAPGROUP %s [%s] count=%llu\n  repro: %s\n", suite, kv.first.c_str(),
                static_cast<unsigned long long>(kv.second.count), kv.second.example.c_str());
            char name[200];
            std::snprintf(name, sizeof(name), "%s group [%s] has no mismatches", suite, kv.first.c_str());
            Check(false, name);
        }
        char name[96];
        std::snprintf(name, sizeof(name), "%s: all %llu cases match the reference", suite, static_cast<unsigned long long>(cases));
        Check(fails == 0 && cases > 0, name);
    }
};

// Runs `f` and survives SIGSEGV/SIGBUS/SIGILL raised inside it (recompiled-code crashes become a reportable mismatch group).
struct GapCrashGuard
{
    static inline sigjmp_buf env;
    static inline volatile sig_atomic_t active = 0;
    static inline int lastSig = 0;
    static inline u64 lastAddr = 0, lastPc = 0;
    static void Handler(int sig, siginfo_t* info, void* ctx)
    {
        if (!active)
        {
            std::signal(sig, SIG_DFL);
            raise(sig);
            return;
        }
        lastSig = sig;
        lastAddr = reinterpret_cast<u64>(info->si_addr);
        lastPc = static_cast<u64>(static_cast<ucontext_t*>(ctx)->uc_mcontext.pc);
        active = 0;
        siglongjmp(env, 1);
    }
    // Returns true when `f` completed without a fatal signal.
    template <class F>
    static bool Run(F&& f)
    {
        struct sigaction sa {}, old[3];
        sa.sa_sigaction = Handler;
        sa.sa_flags = SA_SIGINFO | SA_NODEFER;
        sigemptyset(&sa.sa_mask);
        const int sigs[3] = {SIGSEGV, SIGBUS, SIGILL};
        for (int i = 0; i < 3; ++i)
            sigaction(sigs[i], &sa, &old[i]);
        bool ok = false;
        if (sigsetjmp(env, 1) == 0)
        {
            active = 1;
            f();
            active = 0;
            ok = true;
        }
        for (int i = 0; i < 3; ++i)
            sigaction(sigs[i], &old[i], nullptr);
        return ok;
    }
};

std::string GapHexWords(const std::vector<u32>& w, size_t limit = 160)
{
    std::string s;
    char b[16];
    for (size_t i = 0; i < w.size() && i < limit; ++i)
    {
        std::snprintf(b, sizeof(b), "%08x ", w[i]);
        s += b;
    }
    return s;
}

// Sparse listing: only non-zero words as idx:word (indices are word offsets from the program base).
std::string GapSparseWords(const std::vector<u32>& w, size_t limit = 120)
{
    std::string s;
    char b[32];
    size_t shown = 0;
    for (size_t i = 0; i < w.size() && shown < limit; ++i)
    {
        if (!w[i])
            continue;
        std::snprintf(b, sizeof(b), "%zu:%08x ", i, w[i]);
        s += b;
        ++shown;
    }
    return s;
}

// Greedy minimizer: replaces words by `nop` (0) while the predicate keeps failing, then trims the tail.
template <class Pred>
std::vector<u32> GapMinimize(std::vector<u32> code, Pred stillFails, u32 maxPasses = 2)
{
    for (u32 pass = 0; pass < maxPasses; ++pass)
    {
        bool changed = false;
        for (size_t i = 0; i < code.size(); ++i)
        {
            if (code[i] == 0)
                continue;
            const u32 old = code[i];
            code[i] = 0;
            if (stillFails(code)) changed = true;
            else code[i] = old;
        }
        if (!changed)
            break;
    }
    return code;
}

// Same emulator environment as the EETests() preamble (the EE/VU gap suites need allocated guest memory, recompilers and an idle IOP);
// RunGapMiscSuite dispatches vu-prog-rand / mtvu-xgkick / ee-smc-stress outside EETests, so they build it themselves.
// `vuThread` selects the MTVU speedhack (set before the VU recompilers are reserved).
struct GapEnv
{
    bool ok = false;
    explicit GapEnv(bool vuThread = false)
    {
        EmuCoreXOracleSetEeIrMode(0);
        EmuCoreXOracleSetVUIR(0, 0);
        EmuCoreXOracleSetVUIR(1, 0);
        EmuCoreXOracleSetSkipEvents(1);
        cpuinfo_initialize();
        EmuConfig = Pcsx2Config();
        EmuConfig.Speedhacks.vuThread = vuThread;
        EmuConfig.Speedhacks.vuFlagHack = false;
        if (!SysMemory::Allocate())
        {
            Check(false, "allocate emulator memory");
            return;
        }
        SysMemory::Reset();
        Cpu = &recCpu;
        Cpu->Reserve();
        CpuVU0 = &CpuMicroVU0;
        CpuVU1 = &CpuMicroVU1;
        CpuMicroVU0.Reserve();
        CpuMicroVU1.Reserve();
        CpuMicroVU0.Reset();
        psxCpu = &psxInt;
        psxInt.Reset();
        iopMemWrite32(IOP_TEST_SCRATCH, 0x1000ffffu);
        iopMemWrite32(IOP_TEST_SCRATCH + 4, 0);
        psxRegs.pc = IOP_TEST_SCRATCH;
        psxRegs.cycle = 0;
        ok = true;
    }
    ~GapEnv()
    {
        EmuCoreXOracleSetEeIrMode(-1);
        EmuCoreXOracleSetVUIR(0, -1);
        EmuCoreXOracleSetVUIR(1, -1);
    }
};

#include "jit_oracle_gap_misc_iop.inl"
#include "jit_oracle_gap_misc_vif.inl"
#include "jit_oracle_gap_misc_vu.inl"
#include "jit_oracle_gap_misc_mtvu.inl"
#include "jit_oracle_gap_misc_ee_smc.inl"

// Returns true (and runs the suite) when `suite` is one of the gap-oracle suite names.
bool RunGapMiscSuite(const char* suite)
{
    if (std::strcmp(suite, "iop-rand-block") == 0) { GapIopRandBlockTests(); return true; }
    if (std::strcmp(suite, "vif-unpack-rand") == 0) { GapVifUnpackRandTests(); return true; }
    if (std::strcmp(suite, "vu-prog-rand") == 0) { GapVuProgRandTests(); return true; }
    if (std::strcmp(suite, "mtvu-xgkick") == 0) { GapMtvuXgkickTests(); return true; }
    if (std::strcmp(suite, "ee-smc-stress") == 0) { GapEeSmcStressTests(); return true; }
    return false;
}
