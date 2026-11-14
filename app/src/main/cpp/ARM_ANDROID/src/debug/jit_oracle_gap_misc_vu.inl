// SPDX-License-Identifier: GPL-3.0+
// vu-prog-rand: random VU0 / VU1 micro programs (upper + lower pairs, dense FMAC/flag/Q/P hazards, forward branches with delay slots,
// I-bit pairs, mid-program E-bit, M/D/T bits, VI/VF/ACC/memory traffic) run through the arm64 VU JIT and the VU interpreter, both started
// from the same deterministic state through the x86-parity harness (x86p::refExec, x86p::refInitData). The interpreter is the reference
// where it equals the x86 microVU: the O/U bits of MAC/STATUS are masked on both sides (x86 microVU never computes them), operands avoid
// NaN/Inf/denormals and integer bit patterns (covered by x86-parity-vu-*), Q/P operations use exact power-of-two operands (the interpreter
// rounds DIV/SQRT/EFU differently from x86), FSSET (the interpreter re-derives sticky Z/S bits), OPMULA/OPMSUB (interpreter MAC w lane) and
// M+T bits on one word are generated only on request. Every failing case is classified: minimized (delta debugging), "cause isolation"
// (remove MAC readers / M / D / T bits and see whether it then matches) and, for upper-only programs, judged against the x86 model
// (JIT!=x86model = the JIT deviates, INTERP!=x86model = the reference deviates).
// Compared: VF, VI0..15, ACC, MAC/STATUS/CLIP, Q, P (VU1), I, R, VU data memory, VPU_STAT busy, and for VU0 the VU1 register window.
// Also runs directed cases (GAP_VU_DIRECTED=0 to skip): T-bit with a DIV in flight, MAC/STATUS/CLIP readers 1..6 pairs after an FMAC,
// conditional branches on a VI written directly before them.
// Knobs: GAP_VU_SEED (default 1), GAP_VU_COUNT (programs per VU, default 3000), GAP_VU_MIN=0 (no minimizer), GAP_VU_ISOLATE=0,
//        GAP_VU_NOBRANCH/NOEBIT/NOIBIT/NOMDT=1 (generator switches), GAP_VU_CLASSES (lower classes: 1 int/move, 2 memory, 4 flag ops,
//        8 Q/P/EFU; default 15), GAP_VU_UPPER=0, GAP_VU_MDT (1 M, 2 D, 4 T; default 7), GAP_VU_MDTFORCE=<bits> (bits on every upper),
//        GAP_VU_OPMUL=1, GAP_VU_FSSET=1.

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)
namespace GapVu
{
namespace E = x86p::enc;
using x86p::Rng;
using x86p::VProg;
using x86p::RefInit;
using x86p::RefObs;

struct Feat
{
    bool branch = true, ebit = true, ibit = true, mdt = true;
};

// Lower-op classes (GAP_VU_CLASSES bit mask): 1 integer/move, 2 memory, 4 flag ops, 8 Q/P/EFU. GAP_VU_UPPER=0 drops the upper instructions, GAP_VU_OPMUL=1 allows OPMULA/OPMSUB.
static u32 g_classes = 15;
static bool g_upper = true, g_opmul = false, g_fsset = false;
static u32 g_mdtForce = 0; // GAP_VU_MDTFORCE: put exactly these bits on every upper word (diagnostic)
static u32 g_mdt = 7; // GAP_VU_MDT: 1 M-bit (VU0 only), 2 D-bit, 4 T-bit

constexpr u32 kPtrA = 14, kPtrB = 15; // VI pointer registers (kept small), never written by random integer ops

static u32 SpecialVal(Rng& r)
{
    static const u32 v[] = {0x00000000u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x40000000u, 0xc0000000u, 0x3f000000u, 0x41200000u};
    return v[r.below(sizeof(v) / sizeof(v[0]))];
}

static void GenData(RefInit& ri, Rng& r, u32 index, bool qp)
{
    x86p::refInitData(ri, r, true);
    for (u32 i = 4; i < 4 * 12; ++i)
        if (r.below(6) == 0)
            ri.fm.vf[i] = SpecialVal(r);
    for (u32 l = 0; l < 4; ++l)
        if (r.below(8) == 0)
            ri.fm.acc[l] = SpecialVal(r);
    ri.fm.r = 0x3f800000u | (r.next() & 0x7fffffu);
    for (u32 i = 1; i < 14; ++i)
        if (r.below(3) == 0)
            ri.vi[i] = r.below(0x80);
    ri.vi[kPtrA] = r.below(0x30);
    ri.vi[kPtrB] = r.below(0x30);
    (void)index;
    if (qp)
    {
        static const u32 ex[] = {0x3f800000u, 0x40800000u, 0x41800000u, 0x3e800000u, 0x3d800000u, 0x40000000u, 0x3f000000u};
        for (u32 reg = 13; reg < 16; ++reg)
            for (u32 l = 0; l < 4; ++l)
            {
                u32 v = ex[r.below(7)];
                if (r.below(10) == 0) v = 0;
                if (reg == 15 && r.below(3) == 0) v |= 0x80000000u; // negative operands only in VF15 (DIV); sqrt-type ops read VF13/VF14
                ri.fm.vf[reg * 4 + l] = v;
            }
    }
}

// One random upper instruction word. Destination mostly outside the source pool so values do not compound into overflow.
static u32 GenUpper(Rng& r)
{
    const auto& ops = x86p::upperOps();
    u32 oi;
    do oi = r.below(static_cast<u32>(ops.size()));
    while (!g_opmul && (ops[oi].kind == x86p::K_OPMULA || ops[oi].kind == x86p::K_OPMSUB));
    const x86p::UOp& o = ops[oi];
    u32 mask = r.below(3) == 0 ? 0xf : 1 + r.below(15);
    mask = x86p::legalMask(o, mask);
    if (o.kind == x86p::K_CLIP)
        mask = 0xf;
    const u32 fs = 1 + r.below(8);
    u32 ft = 1 + r.below(8);
    u32 fd = r.below(5) == 0 ? 1 + r.below(8) : 9 + r.below(4);
    // FTOI/ITOF/ABS write the Ft field; integer bit patterns must not feed later FMAC ops as (NaN/denormal) operands
    if (o.kind == x86p::K_FTOI || o.kind == x86p::K_ITOF || o.kind == x86p::K_ABS)
        ft = 16 + r.below(4);
    return x86p::encodeUpper(o, mask, fs, ft, fd);
}

// Lower instruction that is safe in every position (no VI3/branch/EFU timing hazards). `vu1` enables VU1-only forms.
static u32 GenLower(Rng& r, u32 index, bool qp, bool& slow)
{
    const u32 a = 1 + r.below(6), b = r.below(7), c = r.below(7);
    const u32 mask = 1 + r.below(15);
    const u32 ptr = r.below(2) ? kPtrA : kPtrB;
    const u32 imm = r.below(0x20);
    slow = false;
    u32 pick = r.below(index ? 34 : 31);
    if (!qp && (pick == 27 || pick == 28 || pick == 29 || pick == 31 || pick == 32))
        pick = r.below(27);
    {
        const u32 cls = (pick <= 10 || pick == 33) ? 1u : pick <= 20 ? 2u : (pick <= 26 || pick == 30) ? 4u : 8u;
        if (!(g_classes & cls))
            return NOP_L;
    }
    const u32 qs = 13 + r.below(3), qp2 = 13 + r.below(2); // Q/P sources: VF13..15 hold exact (power-of-two) operands so DIV/SQRT/EFU are bit-exact in every engine
    switch (pick)
    {
        case 0: return E::IADD(a, b, c);
        case 1: return E::ISUB(a, b, c);
        case 2: return E::IAND(a, b, c);
        case 3: return E::IOR(a, b, c);
        case 4: return E::IADDI(a, b, static_cast<int>(r.below(32)) - 16);
        case 5: return E::IADDIU(a, b, r.next() & 0x7fff);
        case 6: return E::ISUBIU(a, b, r.next() & 0x7fff);
        case 7: return E::MTIR(r.below(4), a, 1 + r.below(8));
        case 8: return E::MFIR(mask, 20 + r.below(4), b);
        case 9: return E::MOVE(mask, 9 + r.below(4), 1 + r.below(8));
        case 10: return E::MR32(mask, 9 + r.below(4), 1 + r.below(8));
        case 11: return E::LQ(mask, 24 + r.below(4), 0, imm);
        case 12: return E::SQ(mask, 1 + r.below(12), 0, 0x40 + imm);
        case 13: return E::ILW(mask, a, 0, imm);
        case 14: return E::ISW(mask, a, 0, 0x40 + imm);
        case 15: return E::LQI(mask, 24 + r.below(4), ptr);
        case 16: return E::SQI(mask, 1 + r.below(12), ptr);
        case 17: return E::LQD(mask, 24 + r.below(4), ptr);
        case 18: return E::SQD(mask, 1 + r.below(12), ptr);
        case 19: return E::ILWR(mask, a, ptr);
        case 20: return E::ISWR(mask, a, ptr);
        case 21: return g_fsset ? E::FSSET(r.next() & 0xcf3) : NOP_L;
        case 22: return E::FCSET(r.next() & 0xffffff);
        case 23: return r.below(2) ? E::FSAND(a, r.next() & 0xcf3) : E::FSEQ(a, r.next() & 0xcf3);
        case 24: return r.below(2) ? E::FCAND(r.next() & 0xffffff) : E::FCEQ(r.next() & 0xffffff);
        case 25: return r.below(3) == 0 ? E::FSOR(a, r.next() & 0xcf3) : (r.below(2) ? E::FMAND(a, b) : E::FMEQ(a, b));
        case 26: return r.below(2) ? E::FCOR(r.next() & 0xffffff) : E::FCGET(a);
        case 27: slow = true; return E::DIVq(r.below(4), r.below(4), qs, 13 + r.below(3));
        case 28: slow = true; return r.below(2) ? E::SQRTq(r.below(4), qp2) : E::RSQRTq(r.below(4), r.below(4), qp2, qp2 ^ 1);
        case 29: return E::WAITQ();
        case 30: return E::FMOR(a, b);
        case 31: slow = true; return r.below(2) ? E::ESQRT(r.below(4), qp2) : E::ESUMv(qp2);
        case 32: return r.below(2) ? E::MFP(mask, 28 + r.below(2)) : E::WAITP();
        default: return r.below(2) ? E::XTOP(a) : E::XITOP(a);
    }
}

struct Gen
{
    Rng& r;
    u32 index;
    Feat feat;
    VProg p;
    bool slow = false;
    bool qp = false;
    explicit Gen(Rng& rng, u32 idx, const Feat& f, bool qpMode) : r(rng), index(idx), feat(f), qp(qpMode) {}

    u32 Mdt(u32 w)
    {
        if (g_mdtForce)
            return w | ((g_mdtForce & 1 && index == 0) ? x86p::kMbit : 0) | ((g_mdtForce & 2) ? x86p::kDbit : 0) | ((g_mdtForce & 4) ? x86p::kTbit : 0);
        if (!feat.mdt)
            return w;
        if (index == 0 && r.below(40) == 0 && (g_mdt & 1)) w |= x86p::kMbit;
        if (r.below(40) == 0 && (g_mdt & 2)) w |= x86p::kDbit;
        if (r.below(40) == 0 && (g_mdt & 4)) w |= x86p::kTbit;
        // M+T on one word is left out: microVU skips the M-bit flag synchronisation when the T-bit handler runs (else-if chain, identical to
        // upstream), so the result is neither the interpreter nor the x86-parity model; GAP_VU_MDTFORCE=5 exposes it.
        if ((w & x86p::kMbit) && (w & x86p::kTbit))
            w &= ~x86p::kTbit;
        return w;
    }
    void Slot(bool allowI)
    {
        u32 lo = NOP_L;
        u32 up = NOP_U;
        if (g_upper && r.below(100) < 60) up = GenUpper(r);
        if (r.below(100) < 55)
        {
            bool s;
            lo = GenLower(r, index, qp, s);
            slow |= s;
            // A flag-writing lower (FSSET/FCSET) in the same pair as an upper that updates the same flags has an unspecified order (microVU: upper
            // then lower; the interpreter: the other way round), so those pairs carry no upper.
            if ((lo >> 25) == 0x15 || (lo >> 25) == 0x11)
                up = NOP_U;
        }
        up = Mdt(up);
        if (allowI && feat.ibit && r.below(14) == 0)
        {
            // LOI: the lower word is the 32-bit I constant
            static const u32 iv[] = {0x3f800000u, 0x40000000u, 0xbf800000u, 0x3e800000u, 0x00000000u, 0x41000000u};
            lo = r.below(3) ? x86p::refNormal(r) : iv[r.below(6)];
            up |= x86p::kIbit;
        }
        p.pair(lo, up);
    }
    // Delay slot content: never a branch, never writes the branch operand registers 1..4 (x86 viBackUp quirk avoided as in x86-parity-vu-branch).
    void DelaySlot(u32 b)
    {
        switch (r.below(5))
        {
            case 0: p.nop(1); break;
            case 1: p.lower(E::IADDIU(7 + (b % 3), 0, 0x100 + b)); break;
            case 2: p.upper(GenUpper(r)); break;
            case 3: p.pair(E::MFIR(1 + r.below(15), 20 + r.below(4), 5 + r.below(2)), GenUpper(r)); break;
            default: p.lower(E::IADDIU(5 + r.below(2), 0, r.below(40))); break;
        }
    }
    void BranchUnit(u32 b)
    {
        constexpr u32 base = 0x80 / 8;
        const u32 bk = r.below(10);
        const u32 a = 1 + r.below(4), c = 1 + r.below(4);
        const bool writePrev = r.below(2) != 0;
        if (r.below(2)) p.lower(E::IADDI(1 + r.below(4), 1 + r.below(4), static_cast<int>(r.below(32)) - 16));
        const u32 fill = r.below(3);
        const int d = static_cast<int>(1 + fill + r.below(2));
        const u32 branchPair = p.size() + ((writePrev || bk == 2 || bk == 3) ? 1 : 0);
        const u32 target = base + branchPair + 1 + static_cast<u32>(d);
        if (bk == 2 || bk == 3) p.lower(E::IADDIU(a, 0, target));
        else if (writePrev) p.lower(E::IADDI(a, a, 1 - static_cast<int>(r.below(3))));
        switch (bk)
        {
            case 0: p.lower(E::B(d)); break;
            case 1: p.lower(E::BAL(5 + r.below(2), d)); break;
            case 2: p.lower(E::JR(a)); break;
            case 3: p.lower(E::JALR(5 + r.below(2), a)); break;
            case 4: p.lower(E::IBEQ(a, c, d)); break;
            case 5: p.lower(E::IBNE(a, c, d)); break;
            case 6: p.lower(E::IBLTZ(a, d)); break;
            case 7: p.lower(E::IBGTZ(a, d)); break;
            case 8: p.lower(E::IBLEZ(a, d)); break;
            default: p.lower(E::IBGEZ(a, d)); break;
        }
        DelaySlot(b);
        for (u32 f = 0; f < fill + (static_cast<u32>(d) > fill + 1 ? 1 : 0); ++f)
            p.pair(r.below(2) ? NOP_L : E::IADDIU(10 + (b % 2), 10 + (b % 2), 1 + f), r.below(2) ? NOP_U : GenUpper(r)); // skipped when taken
        p.lower(E::IADDIU(13, 13, 1 + b)); // landing marker
    }
    void Build()
    {
        const u32 len = 5 + r.below(18);
        u32 branches = 0;
        for (u32 s = 0; s < len; ++s)
        {
            if (feat.branch && branches < 3 && r.below(100) < 9)
            {
                BranchUnit(branches++);
                continue;
            }
            Slot(true);
            if (feat.ebit && r.below(100) < 2)
            {
                // E-bit on the last plain slot: the delay slot pair still executes and the rest of the program is dead code.
                p.w.back() |= x86p::kEbit;
                Slot(false);
            }
        }
        if (slow)
            p.nop(64);
        p.pair(NOP_L, NOP_U | x86p::kEbit);
        p.nop(2);
    }
};

struct Diff
{
    std::string field;
    u32 a, b;
};

// Compare interpreter vs JIT observations; returns the differing fields (empty = match).
static void Compare(u32 index, const RefObs& e, const RefObs& g, std::vector<Diff>& d)
{
    for (u32 i = 0; i < 128; ++i)
        if (e.s.regs.VF[i >> 2].UL[i & 3] != g.s.regs.VF[i >> 2].UL[i & 3])
            d.push_back({"VF" + std::to_string(i >> 2) + "." + "xyzw"[i & 3], e.s.regs.VF[i >> 2].UL[i & 3], g.s.regs.VF[i >> 2].UL[i & 3]});
    for (u32 i = 0; i < 16; ++i)
        if ((e.s.regs.VI[i].UL & 0xffff) != (g.s.regs.VI[i].UL & 0xffff))
            d.push_back({"VI" + std::to_string(i), e.s.regs.VI[i].UL & 0xffff, g.s.regs.VI[i].UL & 0xffff});
    const u32* ea = reinterpret_cast<const u32*>(&e.s.regs.ACC);
    const u32* ga = reinterpret_cast<const u32*>(&g.s.regs.ACC);
    for (u32 l = 0; l < 4; ++l)
        if (ea[l] != ga[l])
            d.push_back({std::string("ACC.") + "xyzw"[l], ea[l], ga[l]});
    const u32 es = e.s.regs.VI[REG_STATUS_FLAG].UL & ~0x30cu, gs = g.s.regs.VI[REG_STATUS_FLAG].UL & ~0x30cu;
    const u32 em = e.s.regs.VI[REG_MAC_FLAG].UL & ~0xff00u, gm = g.s.regs.VI[REG_MAC_FLAG].UL & ~0xff00u;
    if (es != gs) d.push_back({"STATUS", es, gs});
    if (em != gm) d.push_back({"MAC", em, gm});
    if (e.s.regs.VI[REG_CLIP_FLAG].UL != g.s.regs.VI[REG_CLIP_FLAG].UL) d.push_back({"CLIP", e.s.regs.VI[REG_CLIP_FLAG].UL, g.s.regs.VI[REG_CLIP_FLAG].UL});
    if (e.s.regs.VI[REG_Q].UL != g.s.regs.VI[REG_Q].UL) d.push_back({"Q", e.s.regs.VI[REG_Q].UL, g.s.regs.VI[REG_Q].UL});
    if (index == 1 && e.s.regs.VI[REG_P].UL != g.s.regs.VI[REG_P].UL) d.push_back({"P", e.s.regs.VI[REG_P].UL, g.s.regs.VI[REG_P].UL});
    if (e.s.regs.VI[REG_I].UL != g.s.regs.VI[REG_I].UL) d.push_back({"I", e.s.regs.VI[REG_I].UL, g.s.regs.VI[REG_I].UL});
    if (e.s.regs.VI[REG_R].UL != g.s.regs.VI[REG_R].UL) d.push_back({"R", e.s.regs.VI[REG_R].UL, g.s.regs.VI[REG_R].UL});
    for (u32 i = 0; i < e.s.memory.size() && i < g.s.memory.size(); ++i)
        if (e.s.memory[i] != g.s.memory[i])
        {
            d.push_back({"MEM@" + x86p::hex(i & ~3u), e.s.memory[i], g.s.memory[i]});
            break;
        }
    if (index == 0)
    {
        for (u32 i = 0; i < 128; ++i)
            if (e.vu1vf[i] != g.vu1vf[i]) { d.push_back({"VU1.VF" + std::to_string(i >> 2), e.vu1vf[i], g.vu1vf[i]}); break; }
        for (u32 i = 0; i < 32; ++i)
            if (i != REG_TPC && i != REG_VPU_STAT && e.vu1vi[i] != g.vu1vi[i]) { d.push_back({"VU1.VI" + std::to_string(i), e.vu1vi[i], g.vu1vi[i]}); break; }
    }
    if (e.s.busy != g.s.busy) d.push_back({"BUSY", e.s.busy, g.s.busy});
}

static bool g_crashed = false; // set by Fails when an engine raised SIGSEGV/SIGBUS/SIGILL (the case then counts as "no diff")

static bool Fails(u32 index, const VProg& prog, const RefInit& ri, std::vector<Diff>* out = nullptr)
{
    bool result = false;
    g_crashed = false;
    if (!GapCrashGuard::Run([&] {
            RefObs e{}, g{};
            x86p::refExec(static_cast<int>(index), false, prog, ri, e);
            x86p::refExec(static_cast<int>(index), true, prog, ri, g);
            std::vector<Diff> d;
            Compare(index, e, g, d);
            if (out) *out = d;
            result = !d.empty();
        }))
    {
        g_crashed = true;
        return false;
    }
    return result;
}

// Signature of the non-nop words of a (minimized) program, used as the group key.
// Non-nop pairs only, as pairIndex:lower/upper.
static std::string Sparse(const VProg& p)
{
    std::string s;
    char b[40];
    for (size_t i = 0; i + 1 < p.w.size(); i += 2)
        if (p.w[i] != NOP_L || p.w[i + 1] != NOP_U)
        {
            std::snprintf(b, sizeof(b), "%zu:%08x/%08x ", i / 2, p.w[i], p.w[i + 1]);
            s += b;
        }
    return s;
}

static std::string Signature(const VProg& p)
{
    std::set<std::string> s;
    char b[32];
    for (size_t i = 0; i + 1 < p.w.size(); i += 2)
    {
        const u32 lo = p.w[i], up = p.w[i + 1];
        if (up & x86p::kIbit)
            s.insert("I");
        else if (lo != NOP_L)
        {
            if (!(lo & 0x80000000u)) std::snprintf(b, sizeof(b), "L%02x", lo >> 25);
            else std::snprintf(b, sizeof(b), "L40.%02x.%02x", (lo >> 6) & 0x1f, lo & 0x3f);
            s.insert(b);
        }
        if ((up & 0x7fffffffu & ~(x86p::kEbit | x86p::kMbit | x86p::kDbit | x86p::kTbit)) != NOP_U)
        {
            const u32 fn = up & 0x3f;
            if (fn >= 0x3c) std::snprintf(b, sizeof(b), "U%02x.%02x", fn, (up >> 6) & 0x1f);
            else std::snprintf(b, sizeof(b), "U%02x", fn);
            s.insert(b);
        }
        if (up & x86p::kMbit) s.insert("Mbit");
        if (up & x86p::kDbit) s.insert("Dbit");
        if (up & x86p::kTbit) s.insert("Tbit");
    }
    std::string o;
    for (const std::string& x : s) o += x + " ";
    return o;
}

// Cause isolation: the first feature whose removal makes the program match is the root-cause class of the mismatch (0 = none found).
// Known classes: MAC-read pipeline window (FMAND/FMOR/FMEQ read within 4 cycles of the first FMACs of a program), M-bit, D-bit, T-bit.
static const char* const kCauseName[] = {"", "MAC-reader-in-pipeline-window", "M-bit", "D-bit", "T-bit", "MAC-reader+M-bit", "any of MAC-reader/M/D/T-bit"};

static VProg StripCause(const VProg& in, u32 cause)
{
    VProg p = in;
    const bool mac = cause == 1 || cause == 5 || cause == 6, mb = cause == 2 || cause == 5 || cause == 6, db = cause == 3 || cause == 6, tb = cause == 4 || cause == 6;
    for (size_t i = 0; i + 1 < p.w.size(); i += 2)
    {
        if (mac && !(p.w[i] & 0x80000000u) && !(p.w[i + 1] & x86p::kIbit))
        {
            const u32 op = p.w[i] >> 25;
            if (op == 0x18 || op == 0x1a || op == 0x1b)
                p.w[i] = NOP_L;
        }
        if (mb) p.w[i + 1] &= ~x86p::kMbit;
        if (db) p.w[i + 1] &= ~x86p::kDbit;
        if (tb) p.w[i + 1] &= ~x86p::kTbit;
    }
    return p;
}

static u32 IsolateCause(u32 index, const VProg& p, const RefInit& ri)
{
    for (u32 c : {1u, 2u, 3u, 4u, 5u, 6u})
    {
        const VProg q = StripCause(p, c);
        if (q.w == p.w)
            continue;
        if (!Fails(index, q, ri))
            return g_crashed ? 0 : c;
    }
    return 0;
}

// Triage aid: for programs made only of upper FMAC instructions (no lower, no I-bit) the x86 model of x86-parity runs the same program
// and says which engine deviates from it. Empty string when the program is outside the model.
// Coarse root-cause signature: lower-op classes present (reads of MAC/STATUS/CLIP, flag sets, branches, memory, integer, Q/P) plus the
// special bits. The exact-op signature is only used for tiny programs.
static std::string CoarseSignature(const VProg& p)
{
    std::set<std::string> s;
    bool upper = false;
    for (size_t i = 0; i + 1 < p.w.size(); i += 2)
    {
        const u32 lo = p.w[i], up = p.w[i + 1];
        if (up & x86p::kIbit) s.insert("I-bit");
        if (up & x86p::kMbit) s.insert("M-bit");
        if (up & x86p::kDbit) s.insert("D-bit");
        if (up & x86p::kTbit) s.insert("T-bit");
        if ((up & 0x07ffffffu) != NOP_U && !(up & x86p::kIbit)) upper = true;
        if (lo == NOP_L || (up & x86p::kIbit))
            continue;
        if (lo & 0x80000000u)
        {
            const u32 idx = (lo >> 6) & 31, fn = lo & 0x3f;
            if (fn >= 0x3c && (idx == 14 || (idx == 30 && fn <= 0x3e && false))) s.insert("DIV/SQRT/RSQRT");
            else if (idx >= 28 || idx == 25 || (idx == 29 && fn == 0x3e)) s.insert("EFU/MFP");
            else if (idx == 12 || idx == 13 || idx == 15) s.insert("move/mem-indexed");
            else if (idx == 26) s.insert("XTOP");
            else if (idx == 0x1b) s.insert("XGKICK");
            else s.insert("int/other-special");
            continue;
        }
        switch (lo >> 25)
        {
            case 0x18: case 0x1a: case 0x1b: s.insert("MAC-read(FMEQ/FMAND/FMOR)"); break;
            case 0x14: case 0x16: case 0x17: s.insert("STATUS-read"); break;
            case 0x10: case 0x12: case 0x13: case 0x1c: s.insert("CLIP-read/FCGET"); break;
            case 0x11: s.insert("FCSET"); break;
            case 0x15: s.insert("FSSET"); break;
            case 0x00: case 0x01: case 0x04: case 0x05: s.insert("LQ/SQ/ILW/ISW"); break;
            case 0x08: case 0x09: s.insert("IADDIU/ISUBIU"); break;
            case 0x24: case 0x25: s.insert("JR/JALR"); break;
            default: s.insert((lo >> 25) >= 0x20 && (lo >> 25) <= 0x2f ? "branch" : "other-lower"); break;
        }
    }
    if (upper) s.insert("upper");
    std::string o;
    for (const std::string& x : s) o += x + " ";
    return o;
}

static std::string ModelVerdict(u32 index, const VProg& prog, const RefInit& ri, const RefObs& e, const RefObs& g)
{
    std::vector<x86p::Ins> ins;
    const auto& ops = x86p::upperOps();
    for (size_t i = 0; i + 1 < prog.w.size(); i += 2)
    {
        const u32 lo = prog.w[i], up = prog.w[i + 1];
        if (lo != NOP_L || (up & x86p::kIbit))
            return std::string();
        const u32 w = up & 0x07ffffffu;
        if (w == NOP_U)
            continue;
        int match = -1;
        for (size_t k = 0; k < ops.size() && match < 0; ++k)
            if (ops[k].fn6 == (w & 0x3f) && (ops[k].usesFd || ops[k].idx == ((w >> 6) & 31)))
                match = static_cast<int>(k);
        if (match < 0)
            return std::string();
        x86p::Ins in;
        in.op = match;
        in.mask = (w >> 21) & 0xf;
        in.fs = (w >> 11) & 31;
        in.ft = (w >> 16) & 31;
        in.fd = ops[match].usesFd ? (w >> 6) & 31 : 0;
        ins.push_back(in);
    }
    if (ins.empty())
        return std::string();
    x86p::Cfg cfg;
    cfg.mode = 1;
    cfg.env = x86p::makeEnv(0);
    cfg.cop2 = false;
    cfg.vu1 = index == 1;
    x86p::Obs m{};
    x86p::runModel(static_cast<int>(index) + 1, ins, ri.fm, cfg, m);
    const auto obs = [](const RefObs& o) {
        x86p::Obs x{};
        std::memcpy(x.vf, o.s.regs.VF, sizeof(x.vf));
        std::memcpy(x.acc, &o.s.regs.ACC, sizeof(x.acc));
        for (u32 i = 0; i < 32; ++i) x.vi[i] = (i >= 1 && i < 16) ? 0u : o.s.regs.VI[i].UL; // the model has no integer registers
        x.p = o.s.regs.VI[REG_P].UL;
        return x;
    };
    std::vector<x86p::Diff> de, dg;
    x86p::compareObs(static_cast<int>(index) + 1, m, obs(e), de, 0);
    x86p::compareObs(static_cast<int>(index) + 1, m, obs(g), dg, 0);
    if (de.empty() && dg.empty()) return "model==both";
    if (de.empty()) return std::string("JIT!=x86model(") + dg[0].field + ")";
    if (dg.empty()) return std::string("INTERP!=x86model(") + de[0].field + ")";
    return "both!=x86model";
}

static VProg Minimize(u32 index, VProg p, const RefInit& ri)
{
    for (u32 pass = 0; pass < 2; ++pass)
    {
        bool changed = false;
        for (size_t i = 0; i < p.w.size(); ++i)
        {
            const bool upper = i & 1;
            const u32 old = p.w[i];
            const u32 nop = upper ? (NOP_U | (old & x86p::kEbit)) : NOP_L;
            if (old == nop)
                continue;
            p.w[i] = nop;
            if (Fails(index, p, ri)) changed = true;
            else p.w[i] = old;
        }
        if (!changed)
            break;
    }
    return p;
}

// Directed cases for findings of the random runs (kept so a fix can be verified without the random search).
static void Directed(GapGroups& groups)
{
    struct D { const char* name; u32 pairs[3][2]; };
    const D cases[] = {
        {"DIV then E|T pair (T-bit on the E-bit instruction)", {{E::DIVq(0, 0, 14, 13), NOP_U}, {NOP_L, NOP_U | x86p::kEbit | x86p::kTbit}, {NOP_L, NOP_U}}},
        {"DIV then E pair + T-bit in the delay slot", {{E::DIVq(0, 0, 14, 13), NOP_U}, {NOP_L, NOP_U | x86p::kEbit}, {NOP_L, NOP_U | x86p::kTbit}}},
        {"control: DIV then E pair, no T-bit", {{E::DIVq(0, 0, 14, 13), NOP_U}, {NOP_L, NOP_U | x86p::kEbit}, {NOP_L, NOP_U}}},
        {"DIV, T-bit pair, then E pair (pending Q must survive a T-bit with FBRST.TE=0)", {{E::DIVq(0, 0, 14, 13), NOP_U}, {NOP_L, NOP_U | x86p::kTbit}, {NOP_L, NOP_U | x86p::kEbit}}},
        {"control: DIV, D-bit pair, then E pair", {{E::DIVq(0, 0, 14, 13), NOP_U}, {NOP_L, NOP_U | x86p::kDbit}, {NOP_L, NOP_U | x86p::kEbit}}},
    };
    for (u32 index = 0; index < 2; ++index)
        for (const D& c : cases)
        {
            Rng r(0xd1ec7ull + index);
            RefInit ri;
            GenData(ri, r, index, true);
            for (u32 l = 0; l < 4; ++l) { ri.fm.vf[13 * 4 + l] = 0x3f800000u; ri.fm.vf[14 * 4 + l] = 0x40800000u; } // 1.0 / 4.0 -> Q = 0.25
            VProg p;
            for (const auto& pr : c.pairs)
                p.pair(pr[0], pr[1]);
            p.nop(8);
            p.pair(NOP_L, NOP_U | x86p::kEbit);
            p.nop(2);
            ++groups.cases;
            std::vector<Diff> d;
            if (!Fails(index, p, ri, &d) || d.empty())
                continue;
            char ex[600];
            std::snprintf(ex, sizeof(ex), "%s interp=%08x jit=%08x init Q=%08x prog{%s}", d[0].field.c_str(), d[0].a, d[0].b, ri.fm.q, Sparse(p).c_str());
            groups.Record("directed vu" + std::to_string(index) + " " + c.name, p.w.size(), ex);
        }
}

// FMAC followed by a MAC/STATUS/CLIP reader `dist` pairs later (0 = the next pair), 64 random start states per distance and reader.
static void DirectedFlagWindow(GapGroups& groups)
{
    static const char* const rn[6] = {"FMAND", "FMEQ", "FMOR", "FSAND", "FCAND", "FCGET"};
    for (u32 index = 0; index < 2; ++index)
        for (u32 reader = 0; reader < 6; ++reader)
            for (u32 dist = 0; dist < 6; ++dist)
              for (u32 pre = 0; pre < 3; ++pre)
                for (u32 k = 0; k < 32; ++k)
                {
                    Rng r(0xf1a9ull + index * 977 + reader * 131 + dist * 17 + k * 7919);
                    RefInit ri;
                    GenData(ri, r, index, false);
                    VProg p;
                    p.nop(r.below(3));
                    for (u32 q = 0; q < (pre == 0 ? 0u : pre == 1 ? 1u : 3u); ++q)
                        p.upper(x86p::encodeUpper(x86p::upperOps()[x86p::findUpper("MUL")], 1 + r.below(15), 1 + r.below(8), 1 + r.below(8), 9 + q));
                    p.upper(x86p::encodeUpper(x86p::upperOps()[r.below(8) * 0 + x86p::findUpper("ADD")], 1 + r.below(15), 1 + r.below(8), 1 + r.below(8), 9));
                    p.nop(dist);
                    switch (reader)
                    {
                        case 0: p.lower(E::FMAND(7, 1 + r.below(8))); break;
                        case 1: p.lower(E::FMEQ(7, 1 + r.below(8))); break;
                        case 2: p.lower(E::FMOR(7, 1 + r.below(8))); break;
                        case 3: p.lower(E::FSAND(7, r.next() & 0xcf3)); break;
                        case 4: p.lower(E::FCAND(r.next() & 0xffffff)); break;
                        default: p.lower(E::FCGET(7)); break;
                    }
                    p.nop(8);
                    p.pair(NOP_L, NOP_U | x86p::kEbit);
                    p.nop(2);
                    ++groups.cases;
                    std::vector<Diff> d;
                    if (!Fails(index, p, ri, &d) || d.empty())
                        continue;
                    char ex[700];
                    std::snprintf(ex, sizeof(ex), "%s interp=%08x jit=%08x flags0{S=%x M=%x C=%x} vi0{%x %x %x %x %x %x %x %x} prog{%s}", d[0].field.c_str(), d[0].a, d[0].b, ri.fm.status, ri.fm.mac, ri.fm.clip,
                        ri.vi[1], ri.vi[2], ri.vi[3], ri.vi[4], ri.vi[5], ri.vi[6], ri.vi[7], ri.vi[8], Sparse(p).c_str());
                    groups.Record("directed vu" + std::to_string(index) + " " + rn[reader] + " read " + std::to_string(dist + 1) + " pair(s) after an FMAC, " + std::to_string(pre == 0 ? 0 : pre == 1 ? 1 : 3) + " earlier FMACs", p.w.size(), ex);
                }
}

// Integer-register hazards at a conditional branch: VI4 is written by an ISUBIU `gap` pairs earlier (or not at all), then by an IADDI directly
// before the branch, and the branch tests VI4 (negative results are the interesting ones).
static void DirectedBranchHazard(GapGroups& groups)
{
    static const char* const bn[4] = {"IBGEZ", "IBLTZ", "IBEQ", "IBNE"};
    for (u32 index = 0; index < 2; ++index)
        for (u32 bk = 0; bk < 4; ++bk)
            for (u32 variant = 0; variant < 3; ++variant) // 0: no earlier writer, 1: earlier ISUBIU, 2: earlier ISUBIU + one more pair
                for (u32 k = 0; k < 48; ++k)
                {
                    Rng r(0xb2a9ull + index * 977 + bk * 131 + variant * 17 + k * 7919);
                    RefInit ri;
                    GenData(ri, r, index, false);
                    VProg p;
                    if (variant >= 1) p.lower(E::ISUBIU(4, 3, 0x100 + r.below(0x2000)));
                    p.nop(1 + r.below(3));
                    if (variant == 2) p.lower(E::IADDIU(5, 5, 1));
                    p.lower(E::IADDI(4, 4, -1));
                    const int d = 3;
                    switch (bk)
                    {
                        case 0: p.lower(E::IBGEZ(4, d)); break;
                        case 1: p.lower(E::IBLTZ(4, d)); break;
                        case 2: p.lower(E::IBEQ(4, 6, d)); break;
                        default: p.lower(E::IBNE(4, 6, d)); break;
                    }
                    p.nop(1);
                    p.lower(E::IADDIU(10, 10, 3)); // skipped when taken
                    p.lower(E::IADDIU(10, 10, 5)); // skipped when taken
                    p.lower(E::IADDIU(11, 11, 1)); // landing
                    p.nop(4);
                    p.pair(NOP_L, NOP_U | x86p::kEbit);
                    p.nop(2);
                    ++groups.cases;
                    std::vector<Diff> d2;
                    if (!Fails(index, p, ri, &d2) || d2.empty())
                        continue;
                    char ex[700];
                    std::snprintf(ex, sizeof(ex), "%s interp=%08x jit=%08x vi0{%x %x %x %x %x %x} prog{%s}", d2[0].field.c_str(), d2[0].a, d2[0].b, ri.vi[1], ri.vi[2], ri.vi[3], ri.vi[4], ri.vi[5], ri.vi[6],
                        Sparse(p).c_str());
                    groups.Record("directed vu" + std::to_string(index) + " " + bn[bk] + " on VI written by IADDI directly before it, variant " + std::to_string(variant), p.w.size(), ex);
                }
}

static void Run()
{
    const u64 seed = GapKnob("GAP_VU_SEED", GapKnob("GAP_SEED", 1));
    const u32 count = static_cast<u32>(GapKnob("GAP_VU_COUNT", GapKnob("GAP_COUNT", 0) ? GapKnob("GAP_COUNT", 0) : 3000));
    const bool minimize = GapKnob("GAP_VU_MIN", 1) != 0;
    Feat feat;
    feat.branch = !GapKnob("GAP_VU_NOBRANCH", 0);
    feat.ebit = !GapKnob("GAP_VU_NOEBIT", 0);
    feat.ibit = !GapKnob("GAP_VU_NOIBIT", 0);
    feat.mdt = !GapKnob("GAP_VU_NOMDT", 0);
    g_classes = static_cast<u32>(GapKnob("GAP_VU_CLASSES", 15));
    g_upper = GapKnob("GAP_VU_UPPER", 1) != 0;
    g_opmul = GapKnob("GAP_VU_OPMUL", 0) != 0;
    g_fsset = GapKnob("GAP_VU_FSSET", 0) != 0;
    g_mdt = static_cast<u32>(GapKnob("GAP_VU_MDT", 7));
    g_mdtForce = static_cast<u32>(GapKnob("GAP_VU_MDTFORCE", 0));
    x86p::configure(1, 0);
    setenv("X86P_IGNORE_UO", "1", 1); // compareObs hides the O/U bits (x86 never computes them); read once on first use
    GapGroups groups;
    std::map<std::string, u32> minimized;
    if (GapKnob("GAP_VU_DIRECTED", 1))
        Directed(groups);
    if (GapKnob("GAP_VU_DIRECTED", 1))
        DirectedFlagWindow(groups);
    if (GapKnob("GAP_VU_DIRECTED", 1))
        DirectedBranchHazard(groups);
    for (u32 index = 0; index < 2; ++index)
    {
        for (u32 k = 0; k < count; ++k)
        {
            Rng r(seed * 0x9e3779b97f4a7c15ull + index * 0x51ed27ull + k * 7919ull + 17);
            RefInit ri;
            const bool qp = r.below(4) == 0;
            GenData(ri, r, index, qp);
            Gen g(r, index, feat, qp);
            g.Build();
            ++groups.cases;
            std::vector<Diff> d;
            const bool failed = Fails(index, g.p, ri, &d);
            if (g_crashed)
            {
                char ex[1700];
                std::snprintf(ex, sizeof(ex), "CRASH signal=%d addr=%llx pc=%llx k=%u seed=%llu prog{%s}", GapCrashGuard::lastSig, static_cast<unsigned long long>(GapCrashGuard::lastAddr),
                    static_cast<unsigned long long>(GapCrashGuard::lastPc), k, static_cast<unsigned long long>(seed), Sparse(g.p).c_str());
                groups.Record("vu" + std::to_string(index) + " CRASH", g.p.w.size(), ex);
                continue;
            }
            if (!failed)
                continue;
            std::string fam = d[0].field;
            if (fam.rfind("VF", 0) == 0) fam = "VF";
            else if (fam.rfind("MEM", 0) == 0) fam = "MEM";
            else if (fam.rfind("VU1.VF", 0) == 0) fam = "VU1.VF";
            else if (fam.rfind("VI", 0) == 0 && fam.size() <= 4) fam = "VI";
            const u32 cause = GapKnob("GAP_VU_ISOLATE", 1) ? IsolateCause(index, g.p, ri) : 0;
            const std::string famKey = "vu" + std::to_string(index) + " " + fam + (cause ? std::string(" <") + kCauseName[cause] + ">" : std::string());
            VProg prog = g.p;
            bool hasJr = false;
            for (size_t i = 0; i < g.p.w.size(); i += 2)
                hasJr |= !(g.p.w[i] & 0x80000000u) && ((g.p.w[i] >> 25) == 0x24 || (g.p.w[i] >> 25) == 0x25);
            if (minimize && !hasJr && minimized[famKey]++ < 40) // a minimized JR would jump through a stale register into the nop sea
                prog = Minimize(index, prog, ri);
            std::vector<Diff> md;
            Fails(index, prog, ri, &md);
            if (md.empty()) { prog = g.p; md = d; }
            char ex[1800];
            std::snprintf(ex, sizeof(ex), "%s interp=%08x jit=%08x (+%zu more) k=%u seed=%llu prog{%s} vi0{", md[0].field.c_str(), md[0].a, md[0].b, md.size() - 1, k,
                static_cast<unsigned long long>(seed), Sparse(prog).c_str());
            std::string repro = ex;
            for (u32 i = 1; i < 16; ++i) { char t[16]; std::snprintf(t, sizeof(t), "%x ", ri.vi[i] & 0xffff); repro += t; }
            repro += "}";
            {
                char t[96];
                std::snprintf(t, sizeof(t), " flags0{S=%x M=%x C=%x I=%08x Q=%08x}", ri.fm.status, ri.fm.mac, ri.fm.clip, ri.fm.i, ri.fm.q);
                repro += t;
            }
            std::string verdict;
            GapCrashGuard::Run([&] {
                RefObs e2{}, g2{};
                x86p::refExec(static_cast<int>(index), false, prog, ri, e2);
                x86p::refExec(static_cast<int>(index), true, prog, ri, g2);
                verdict = ModelVerdict(index, prog, ri, e2, g2);
            });
            const std::string sig = Signature(prog);
            const bool small = std::count(sig.begin(), sig.end(), ' ') <= 4;
            groups.Record(famKey + " | " + (small && prog.w.size() <= 4 ? sig : "~" + CoarseSignature(prog)) + (verdict.empty() ? "" : "| " + verdict), prog.w.size(), repro);
        }
    }
    groups.Report("vu-prog-rand");
}
} // namespace GapVu

void GapVuProgRandTests()
{
    GapEnv env;
    if (env.ok)
        GapVu::Run();
}
#else
void GapVuProgRandTests() { Check(false, "vu-prog-rand needs a debug oracle build (EMUCOREX_ENABLE_NATIVE_SELF_TESTS, !NDEBUG)"); }
#endif
