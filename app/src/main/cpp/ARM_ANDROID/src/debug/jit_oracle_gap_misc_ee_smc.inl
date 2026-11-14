// SPDX-License-Identifier: GPL-3.0+
// ee-smc-stress: self-modifying EE code against the EE interpreter (gapee::gapRun, cold run and recompile run; the warm variant is left out
// because the first run already changed the code memory the interpreter reference started from).
// Groups (every program loops 3 times so that a block compiled in iteration 1 is invalidated by the store of iteration 2):
//   victim-call   a leaf function in another page ("far") or right behind the main code ("near") is patched by SW / SH+SH / SD / SQ / SWR+SWL
//                 stores between calls; the patch value alternates between two instructions; "near+data" adds data stores to the page that
//                 holds the code (manual-protection / page-counter paths) and loads them back
//   next-block    a store in block A rewrites a word of the following block B (compiled in a previous iteration)
//   self-earlier  a store rewrites an already executed word of its own block (seen by the next iteration)
//   ahead-in-block a store rewrites a LATER word of the block that is executing. x86 microVU/iR5900 (and the arm64 port) keep running the stale
//                 block until it ends, so the reference is the interpreter running the same program with the store redirected to a dummy
//                 scratch word (the "x86 stale-rest-of-block model"); the code word itself is not compared
//   external-write code words are rewritten between two executions by the host (memWrite32, like DMA/loader writes) with the JIT caches kept
// Knobs: GAP_SMC_SEED (default 1), GAP_SMC_COUNT (programs per group, default 250).

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)
namespace GapSmc
{
using namespace gapee;

constexpr u32 kFarWord = 0x440; // victim at EE_TEST_PC + 0x1100: another 4 KiB page; the scratch fill only covers the first 256 bytes of 0x101000
constexpr u32 kIters = 3;

static const u32 kPool[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 18, 19, 20, 21, 22, 23, 24, 25};

static BodyCtx MakeCtx()
{
    BodyCtx cx;
    for (u32 reg : kPool) cx.pool.push_back(reg);
    cx.allowMem = false;
    return cx;
}

static u32 AluWord(Rng& r, const BodyCtx& cx) { return genAlu(r, cx).word; }

static void Pad(std::vector<u32>& c, u32 words) { while (c.size() < words) c.push_back(NOP); }

static void InitRegs(Rng& r, GPR_reg* init, u32 victimBase)
{
    std::memset(init, 0, 32 * sizeof(GPR_reg));
    for (u32 reg : kPool) rand128(r, init[reg]);
    init[16].UD[0] = EE_TEST_SCRATCH;
    init[17].UD[0] = victimBase;
}

static void Swap2829(std::vector<u32>& c)
{
    c.push_back(OR(26, 28, 0));
    c.push_back(OR(28, 29, 0));
    c.push_back(OR(29, 26, 0));
}

static void Loop(std::vector<u32>& c, u32 loopIdx)
{
    c.push_back(ADDIU(30, 30, -1));
    const u32 here = EE_TEST_PC + static_cast<u32>(c.size()) * 4;
    c.push_back(BR(5, 30, 0, BREL(here, EE_TEST_PC + loopIdx * 4)));
    c.push_back(NOP);
}

struct Result
{
    std::vector<u32> code;
    GPR_reg init[32];
    GPR_reg initRef[32];
    u32 dummyWord = ~0u; // scratch word index excluded from the compare (ahead-in-block)
};

static void RunPair(Result& p, const std::string& tag, bool refDiffInit)
{
    RunOpt ro;
    ro.interpSteps = 6000;
    const EESnapshot ref = gapRun(false, p.code, refDiffInit ? p.initRef : p.init, nullptr, ro);
    gCase();
    g_suppressRepro = false;
    static const char* const names[3] = {"cold", "warm", "recompile"};
    for (u32 v : {0u, 2u})
    {
        RunOpt o = ro;
        o.jitVariant = v;
        const EESnapshot got = gapRun(true, p.code, p.init, nullptr, o);
        DiffOpt d;
        d.cp0Mask = 0;
        if (refDiffInit) d.gprSkip |= 1u << 17;
        if (p.dummyWord != ~0u) d.scratchSkipWords.push_back(p.dummyWord);
        const u64 before = g_grp.fails;
        compareRecord(ref, got, d, std::string(names[v]) + " " + tag, p.code, p.init);
        if (g_grp.fails != before) g_suppressRepro = true;
    }
    g_suppressRepro = false;
}

//------------------------------------------------------------------
// victim-call
//------------------------------------------------------------------
static void VictimCall(u32 count, u32 where /*0 far, 1 near, 2 near+data*/)
{
    for (u32 k = 0; k < count; ++k)
    {
        Rng r(GapKnob("GAP_SMC_SEED", 1) * 0x9e3779b97full + where * 977 + k * 6151ull + 5);
        const BodyCtx cx = MakeCtx();
        const u32 n = 6 + r.below(7);
        const u32 kind = r.below(6); // 0 SW 1 SH+SH 2 SD 3 SQ 4 SWR+SWL 5 SW into the delay-slot word
        std::vector<u32> body;
        for (u32 i = 0; i < n; ++i) body.push_back(AluWord(r, cx));
        u32 pos = kind == 3 ? (r.below(n / 4) * 4) : kind == 2 ? (r.below(n / 2) * 2) : r.below(n);
        u32 pa[4], pb[4];
        for (u32 i = 0; i < 4; ++i) { pa[i] = AluWord(r, cx); pb[i] = AluWord(r, cx); }
        Result p;
        std::vector<u32>& c = p.code;
        // main code is generated first with a placeholder victim index, then fixed up.
        u32 victimWord = kFarWord;
        u32 jalIdx = 0;
        u32 mainSizeGuess = 0;
        (void)mainSizeGuess;
        auto build = [&](u32 vw) {
            c.clear();
            const u32 vbase = EE_TEST_PC + vw * 4;
            emitConst64(c, 17, vbase, 26);
            const u64 va = kind == 2 ? (static_cast<u64>(pa[1]) << 32 | pa[0]) : pa[0];
            const u64 vb = kind == 2 ? (static_cast<u64>(pb[1]) << 32 | pb[0]) : pb[0];
            emitConst64(c, 28, va, 26);
            emitConst64(c, 29, vb, 26);
            if (kind == 3) emitConst128(c, 28, pa[0] | (static_cast<u64>(pa[1]) << 32), pa[2] | (static_cast<u64>(pa[3]) << 32), 26, 27);
            c.push_back(ADDIU(30, 0, kIters));
            const u32 loop = static_cast<u32>(c.size());
            jalIdx = static_cast<u32>(c.size());
            c.push_back(JTYPE(3, vbase));
            c.push_back(r.below(2) ? NOP : ADDIU(2, 2, 1));
            if (where == 2)
            {
                // data in the page that holds the code
                c.push_back(iI(0x2b, 17, 1, 0x300));
                c.push_back(iI(0x23, 17, 2, 0x300));
            }
            const u32 off = pos * 4;
            switch (kind)
            {
                case 0: case 5: c.push_back(iI(0x2b, 17, 28, off)); break;
                case 1: c.push_back(iI(0x29, 17, 28, off)); c.push_back(iR(0, 28, 26, 16, 2)); c.push_back(iI(0x29, 17, 26, off + 2)); break;
                case 2: c.push_back(iI(0x3f, 17, 28, off)); break;
                case 3: c.push_back(iI(0x1f, 17, 28, off)); break;
                default: c.push_back(iI(0x2e, 17, 28, off)); c.push_back(iI(0x2a, 17, 28, off + 3)); break;
            }
            if (kind != 3) Swap2829(c);
            if (where == 2) c.push_back(iI(0x2b, 17, 3, 0x304));
            Loop(c, loop);
            return loop;
        };
        build(victimWord);
        if (where != 0)
        {
            victimWord = (static_cast<u32>(c.size()) + 2 + 3) & ~3u;
            build(victimWord);
        }
        // exit jump after the loop, then padding up to the victim
        const u32 sentinelWord = victimWord + static_cast<u32>(body.size()) + 2 + (where == 2 ? 0x100 : 0);
        c.push_back(JTYPE(2, EE_TEST_PC + sentinelWord * 4));
        c.push_back(NOP);
        Pad(c, victimWord);
        for (u32 w : body) c.push_back(w);
        c.push_back(JR(31));
        c.push_back(r.below(2) ? NOP : ADDIU(2, 2, 3));
        c[jalIdx] = JTYPE(3, EE_TEST_PC + victimWord * 4);
        if (where == 2) Pad(c, victimWord + static_cast<u32>(body.size()) + 2 + 0x100);
        // fixed sentinel position must match
        while (c.size() < sentinelWord) c.push_back(NOP);
        appendSentinel(c);
        InitRegs(r, p.init, EE_TEST_PC + victimWord * 4);
        // the jump to the sentinel was encoded for sentinelWord; appendSentinel placed 'b .' exactly there
        RunPair(p, "k=" + std::to_string(k) + " kind=" + std::to_string(kind), false);
    }
}

//------------------------------------------------------------------
// next-block / self-earlier / ahead-in-block
//------------------------------------------------------------------
static void BlockPrograms(u32 count, u32 mode /*0 next-block, 1 self-earlier, 2 ahead-in-block*/)
{
    for (u32 k = 0; k < count; ++k)
    {
        Rng r(GapKnob("GAP_SMC_SEED", 1) * 0xd1b54a32d192ed03ull + mode * 977 + k * 40503ull + 9);
        const BodyCtx cx = MakeCtx();
        const u32 na = 2 + r.below(6), nb = 2 + r.below(6);
        const u32 pa = AluWord(r, cx), pb = AluWord(r, cx);
        Result p;
        std::vector<u32>& c = p.code;
        emitConst64(c, 17, EE_TEST_PC, 26);
        emitConst64(c, 28, pa, 26);
        emitConst64(c, 29, pb, 26);
        c.push_back(ADDIU(30, 0, mode == 2 ? 1 : kIters));
        const u32 loop = static_cast<u32>(c.size());
        std::vector<u32> aw, bw;
        for (u32 i = 0; i < na; ++i) aw.push_back(AluWord(r, cx));
        for (u32 i = 0; i < nb; ++i) bw.push_back(AluWord(r, cx));
        const u32 storeIdx = loop + na; // the store follows the A words
        u32 target;
        if (mode == 0) target = storeIdx + 1 + 1 + 3 + 2 + r.below(nb); // inside B (after: store, swap(3), addiu, b, nop => computed below)
        else if (mode == 1) target = loop + r.below(na);
        else target = storeIdx + 1 + r.below(na); // later in the same block: 'a2' words follow the store
        for (u32 w : aw) c.push_back(w);
        if (mode == 2)
        {
            // second half of the block (after the store), still before the terminating jump
            c.push_back(iI(0x2b, 17, 28, 0)); // offset fixed below
            std::vector<u32> a2;
            for (u32 i = 0; i < na; ++i) a2.push_back(AluWord(r, cx));
            for (u32 w : a2) c.push_back(w);
            const u32 sentinelIdx = static_cast<u32>(c.size()) + 2;
            c.push_back(JTYPE(2, EE_TEST_PC + sentinelIdx * 4));
            c.push_back(NOP);
            c[storeIdx] = iI(0x2b, 17, 28, target * 4);
            appendSentinel(c);
            InitRegs(r, p.init, EE_TEST_PC);
            std::memcpy(p.initRef, p.init, sizeof(p.init));
            const u32 dummy = EE_TEST_SCRATCH + 0xf0;
            p.initRef[17].UD[0] = dummy - target * 4;
            p.dummyWord = 0xf0 / 4;
            RunPair(p, "k=" + std::to_string(k), true);
            continue;
        }
        const u32 storeAt = static_cast<u32>(c.size());
        c.push_back(NOP); // store, fixed below
        Swap2829(c);
        c.push_back(ADDIU(30, 30, -1));
        const u32 bIdx = static_cast<u32>(c.size()) + 2;
        c.push_back(BR(4, 0, 0, BREL(EE_TEST_PC + static_cast<u32>(c.size()) * 4, EE_TEST_PC + bIdx * 4)));
        c.push_back(NOP);
        const u32 bStart = static_cast<u32>(c.size());
        for (u32 w : bw) c.push_back(w);
        const u32 here = EE_TEST_PC + static_cast<u32>(c.size()) * 4;
        c.push_back(BR(5, 30, 0, BREL(here, EE_TEST_PC + loop * 4)));
        c.push_back(NOP);
        const u32 sentinelIdx = static_cast<u32>(c.size()) + 2;
        c.push_back(JTYPE(2, EE_TEST_PC + sentinelIdx * 4));
        c.push_back(NOP);
        if (mode == 0) target = bStart + r.below(nb);
        c[storeAt] = iI(0x2b, 17, 28, target * 4);
        appendSentinel(c);
        InitRegs(r, p.init, EE_TEST_PC);
        RunPair(p, "k=" + std::to_string(k), false);
    }
}

//------------------------------------------------------------------
// external-write
//------------------------------------------------------------------
struct MidCtx { u32 addr[3]; u32 word[3]; u32 n; };
static void MidWrite(void* p)
{
    const MidCtx* m = static_cast<const MidCtx*>(p);
    for (u32 i = 0; i < m->n; ++i) memWrite32(m->addr[i], m->word[i]);
}

static void ExternalWrite(u32 count)
{
    for (u32 k = 0; k < count; ++k)
    {
        Rng r(GapKnob("GAP_SMC_SEED", 1) * 0x2545f4914f6cdd1dull + k * 7919ull + 3);
        const BodyCtx cx = MakeCtx();
        const u32 n = 5 + r.below(8);
        const bool far = r.below(2);
        std::vector<u32> body;
        for (u32 i = 0; i < n; ++i) body.push_back(AluWord(r, cx));
        Result p;
        std::vector<u32>& c = p.code;
        const u32 vw = far ? kFarWord : 16;
        c.push_back(JTYPE(3, EE_TEST_PC + vw * 4));
        c.push_back(NOP);
        c.push_back(JTYPE(2, EE_TEST_PC + (vw + n + 2) * 4));
        c.push_back(NOP);
        Pad(c, vw);
        for (u32 w : body) c.push_back(w);
        c.push_back(JR(31));
        c.push_back(NOP);
        appendSentinel(c);
        InitRegs(r, p.init, 0);
        MidCtx m{};
        m.n = 1 + r.below(3);
        for (u32 i = 0; i < m.n; ++i)
        {
            m.addr[i] = EE_TEST_PC + (vw + r.below(n)) * 4;
            m.word[i] = AluWord(r, cx);
        }
        RunOpt ro;
        ro.interpSteps = 3000;
        ro.twoPhase = true;
        ro.gpr2 = p.init;
        ro.mid = MidWrite;
        ro.midCtx = &m;
        const EESnapshot ref = gapRun(false, p.code, p.init, nullptr, ro);
        gCase();
        const EESnapshot got = gapRun(true, p.code, p.init, nullptr, ro);
        DiffOpt d;
        d.cp0Mask = 0;
        compareRecord(ref, got, d, "k=" + std::to_string(k) + (far ? " far" : " near") + " writes=" + std::to_string(m.n), p.code, p.init);
    }
}

static void Run()
{
    const u32 n = static_cast<u32>(GapKnob("GAP_SMC_COUNT", GapKnob("GAP_COUNT", 0) ? GapKnob("GAP_COUNT", 0) : 250));
    static const char* const vn[3] = {"far", "near", "near+data"};
    for (u32 w = 0; w < 3; ++w)
    {
        gBegin(std::string("ee-smc-stress victim-call ") + vn[w]);
        VictimCall(n, w);
        gEnd();
    }
    static const char* const bn[3] = {"next-block", "self-earlier", "ahead-in-block (stale rest-of-block model)"};
    for (u32 m = 0; m < 3; ++m)
    {
        gBegin(std::string("ee-smc-stress ") + bn[m]);
        BlockPrograms(n, m);
        gEnd();
    }
    gBegin("ee-smc-stress external-write");
    ExternalWrite(n);
    gEnd();
    std::printf("GAPEE SUMMARY suite=ee-smc-stress cases=%llu mismatches=%llu\n", static_cast<unsigned long long>(g_totalCases), static_cast<unsigned long long>(g_totalFails));
    Check(g_totalCases > 0, "ee-smc-stress ran cases");
}
} // namespace GapSmc

void GapEeSmcStressTests()
{
    GapEnv env;
    if (env.ok)
        GapSmc::Run();
}
#else
void GapEeSmcStressTests() { Check(false, "ee-smc-stress needs a debug oracle build (EMUCOREX_ENABLE_NATIVE_SELF_TESTS, !NDEBUG)"); }
#endif
