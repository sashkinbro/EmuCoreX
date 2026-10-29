// SPDX-License-Identifier: GPL-3.0+

namespace VuAotOracle
{
struct Context
{
    std::unique_ptr<vuaot::PipeAnalyzer> analyzer;
    unsigned blocks = 0;
};

static void Trace(const vuaot::MvuBlockTrace& actual, void* user)
{
    auto& ctx = *static_cast<Context*>(user);
    if (!ctx.analyzer)
    {
        vuaot::PipeConfig config;
        config.vuIndex = actual.vuIndex;
        config.flagHack = actual.flagHack != 0;
        config.ibitHack = EmuConfig.Gamefixes.IbitHack;
        config.xgkickHack = EmuConfig.Gamefixes.XgKickHack;
        ctx.analyzer = std::make_unique<vuaot::PipeAnalyzer>(
            actual.vuIndex ? VU1.Micro : VU0.Micro, config);
    }
    vuaot::PipeState key;
    std::memcpy(key.bytes, actual.entryState, sizeof(key.bytes));
    vuaot::MvuBlockTrace expected;
    ctx.analyzer->Analyze(actual.startPC, key).ToTrace(expected, actual.vuIndex);
    const auto differences = vuaot::CompareBlockTraces(actual, expected);
    for (const auto& difference : differences)
        std::fprintf(stderr, "VUAOT_TRACE vu=%u pc=%x %s\n", actual.vuIndex,
            actual.startPC, difference.c_str());
    Check(differences.empty(), "VU AOT analyzer matches live microVU pipeline decisions");
    ++ctx.blocks;
}
}

void RunVuAotTraceTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate VU AOT trace memory");
        return;
    }
    CpuVU0 = &CpuMicroVU0;
    CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve();
    CpuMicroVU1.Reserve();
    const auto savedHook = vuaot::g_mvuBlockHook;
    void* const savedUser = vuaot::g_mvuBlockHookUser;
    const auto savedConfig = EmuConfig;
    for (u32 vu = 0; vu != 2; ++vu)
    {
        for (bool flagHack : {false, true})
        {
            EmuConfig.Speedhacks.vuFlagHack = flagHack;
            for (u32 operation : {0x28u, 0x29u, 0x2au, 0x2bu, 0x2cu, 0x2du, 0x2fu})
            {
                const u32 upper = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | operation;
                const std::vector<u32> program{
                    NOP_L, upper, NOP_L, NOP_U, NOP_L, NOP_U | (1u << 30), NOP_L, NOP_U};
                VuAotOracle::Context context;
                vuaot::g_mvuBlockHook = &VuAotOracle::Trace;
                vuaot::g_mvuBlockHookUser = &context;
                RunVU(vu, true, program, 0x80);
                Check(context.blocks > 0, "VU AOT receives a real microVU analysis trace");
            }
            VuAotOracle::Context context;
            vuaot::g_mvuBlockHookUser = &context;
            const std::vector<u32> branch{
                (0x28u << 25) | (2u << 16) | (1u << 11) | 3u, NOP_U,
                NOP_L, NOP_U, NOP_L, NOP_U | (1u << 30), NOP_L, NOP_U,
                NOP_L, NOP_U | (1u << 30), NOP_L, NOP_U};
            RunVU(vu, true, branch, 0x80);
            Check(context.blocks > 0, "VU AOT traces conditional successors");
        }
    }
    vuaot::g_mvuBlockHook = savedHook;
    vuaot::g_mvuBlockHookUser = savedUser;
    EmuConfig = savedConfig;
}
