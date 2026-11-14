// SPDX-License-Identifier: GPL-3.0+
// vif-unpack-rand: every UNPACK format x usn x cl/wl (fill / equal / skip) x MASK x mode 0/1/2 x random data, VIF0 and VIF1.
// Voters: (C) a loop over VIFfuncTable, i.e. the interpreter's C unpack functions (mode 0 included) replicating _nVifUnpackLoop;
//         (N) the production interpreter entry _nVifUnpack (NEON "simple" kernels for mode 0, C for modes 1/2);
//         (D) the arm64 dynarec through dVifUnpack<idx> (the production dispatch incl. wrap fallback);
//         (M) the independent model of VifNarrowLoadOracle::Model for the narrow V2/V3/V4-16/8 formats.
// Compared: the whole VU data memory plus MaskRow/MaskCol. A second sub-suite pushes random chunk splits through the production
// vifUnpackSetup/nVifUnpack (partial-transfer buffer, NUM accounting, start_aligned from the first chunk).
// Knobs: GAP_VIF_SEED, GAP_VIF_COUNT (default 30000), GAP_VIF_MODE3=1 (also mode 3), GAP_VIF_ZEROCL=1 (cl or wl equal to 0).

namespace GapVif
{
struct Cfg
{
    u32 idx = 0, fmt = 12, usn = 0, maskBit = 0, mode = 0, cl = 1, wl = 1, num = 1, addr = 0, aligned = 1, srcAlign = 0, mask = 0;
    u32 row[4]{}, col[4]{};
    bool wrap = false;
};

struct Out
{
    std::vector<u8> mem;
    u32 row[4]{}, col[4]{};
    bool crashed = false;
    bool Equal(const Out& o) const
    {
        return crashed == o.crashed && mem == o.mem && !std::memcmp(row, o.row, 16) && !std::memcmp(col, o.col, 16);
    }
};

const char* const kFmt[16] = {"S-32", "S-16", "S-8", "?3", "V2-32", "V2-16", "V2-8", "?7", "V3-32", "V3-16", "V3-8", "?11", "V4-32", "V4-16", "V4-8", "V4-5"};

struct Guarded
{
    void* map = MAP_FAILED;
    size_t size = 0;
    u8* data = nullptr; // readable [data, data+bytes) followed by a PROT_NONE page
    explicit Guarded(u32 bytes)
    {
        const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        const size_t payload = (bytes + page - 1) / page * page + page;
        size = payload + 2 * page;
        map = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (map == MAP_FAILED)
            return;
        u8* start = static_cast<u8*>(map) + page;
        if (mprotect(start, payload, PROT_READ | PROT_WRITE))
            return;
        data = start + payload - bytes;
    }
    ~Guarded() { if (map != MAP_FAILED) munmap(map, size); }
};

u32 VuSize(u32 idx) { return idx ? 0x4000 : 0x1000; }

bool IsFill(const Cfg& c) { return c.cl < (c.wl ? c.wl : 256); }

// Bytes the C loop consumes from the source (without the V3/V4 over-read slack).
u32 Consumed(const Cfg& c)
{
    const u32 vSize = nVifT[c.fmt];
    const bool fill = IsFill(c);
    int cl = 0, num = c.num ? static_cast<int>(c.num) : 256;
    u32 off = 0;
    do
    {
        --num;
        ++cl;
        if (fill)
        {
            if (cl <= static_cast<int>(c.cl)) off += vSize;
            else if (cl == static_cast<int>(c.wl)) cl = 0;
        }
        else
        {
            off += vSize;
            if (cl >= static_cast<int>(c.wl)) cl = 0;
        }
    } while (num);
    return off;
}

// Destination span in bytes for the C loop (the dynarec length for NUM/cl/wl).
u32 Span(const Cfg& c)
{
    const bool fill = IsFill(c);
    int cl = 0, num = c.num ? static_cast<int>(c.num) : 256;
    u32 addr = 0;
    const s32 skip = (static_cast<s32>(c.cl) - static_cast<s32>(c.wl)) * 16;
    do
    {
        --num;
        ++cl;
        addr += 16;
        if (fill) { if (cl > static_cast<int>(c.cl) && cl == static_cast<int>(c.wl)) cl = 0; }
        else if (cl >= static_cast<int>(c.wl)) { addr += static_cast<u32>(skip); cl = 0; }
    } while (num);
    return addr;
}

void Prepare(const Cfg& c)
{
    auto& vif = c.idx ? vif1 : vif0;
    auto& regs = c.idx ? vif1Regs : vif0Regs;
    VURegs& vu = c.idx ? VU1 : VU0;
    const u32 size = VuSize(c.idx);
    for (u32 i = 0; i < size; ++i)
        vu.Mem[i] = static_cast<u8>(i * 13 + 7 + c.idx * 29);
    std::memcpy(&vif.MaskRow, c.row, 16);
    std::memcpy(&vif.MaskCol, c.col, 16);
    vif.cmd = static_cast<int>(c.fmt | (c.maskBit ? 0x10u : 0u));
    vif.usn = static_cast<u8>(c.usn);
    vif.cl = 0;
    vif.tag.addr = c.addr;
    vif.start_aligned = static_cast<u8>(c.aligned);
    regs.num = c.num ? c.num : 256;
    regs.mode = c.mode;
    regs.mask = c.mask;
    regs.cycle.cl = static_cast<u8>(c.cl);
    regs.cycle.wl = static_cast<u8>(c.wl);
}

// V2-xx and V3-xx leave the W lane of each quad to hardware-tested details (V2: zero when start_aligned, V3: next element or zero on a QW
// boundary, see xUPK_V2_32/xUPK_V3_*), which the x86 dynarec and the production kernels implement but the interpreter C functions
// (UNPACK_V2 = v1v0v1v0, UNPACK_V3 = V4 logic) do not. The two deliberately differ, so the W lane of these formats is not compared.
void IgnoreUndefinedW(Out& o, u32 fmt, u32 mode)
{
    if (fmt != 4 && fmt != 5 && fmt != 6 && fmt != 8 && fmt != 9 && fmt != 10)
        return;
    for (size_t i = 12; i < o.mem.size(); i += 16)
        std::memset(&o.mem[i], 0, 4);
    if (mode == 2) std::memset(o.row, 0, 16); // mode 2 accumulates the written W into the row
}

Out Capture(const Cfg& c, bool crashed)
{
    Out o;
    o.crashed = crashed;
    VURegs& vu = c.idx ? VU1 : VU0;
    o.mem.assign(vu.Mem, vu.Mem + VuSize(c.idx));
    auto& vif = c.idx ? vif1 : vif0;
    std::memcpy(o.row, &vif.MaskRow, 16);
    std::memcpy(o.col, &vif.MaskCol, 16);
    IgnoreUndefinedW(o, c.fmt, c.mode);
    return o;
}

// (C) interpreter C functions, replicating _nVifUnpackLoop for every mode (including mode 0).
void CLoop(const Cfg& c, const u8* data)
{
    auto& vif = c.idx ? vif1 : vif0;
    auto& regs = c.idx ? vif1Regs : vif0Regs;
    VURegs& vu = c.idx ? VU1 : VU0;
    const bool fill = IsFill(c);
    const int skipSize = (regs.cycle.cl - regs.cycle.wl) * 16;
    const int usn = !!vif.usn;
    const int upkNum = vif.cmd & 0x1f;
    const u8 vSize = nVifT[upkNum & 0x0f];
    const UNPACKFUNCTYPE ft = VIFfuncTable[c.idx][c.mode & 3][(usn * 2 * 16) + upkNum];
    do
    {
        u8* dest = vu.Mem + (vif.tag.addr & (c.idx ? 0x3ff0 : 0xff0));
        ft(dest, data);
        vif.tag.addr += 16;
        --regs.num;
        ++vif.cl;
        if (fill)
        {
            if (vif.cl <= regs.cycle.cl) data += vSize;
            else if (vif.cl == regs.cycle.wl) vif.cl = 0;
        }
        else
        {
            data += vSize;
            if (vif.cl >= regs.cycle.wl)
            {
                vif.tag.addr += skipSize;
                vif.cl = 0;
            }
        }
    } while (regs.num);
}

Out RunImpl(const Cfg& c, int which, const u8* data)
{
    Prepare(c);
    bool ok = true;
    const bool fill = IsFill(c);
    if (which == 0)
        ok = GapCrashGuard::Run([&] { CLoop(c, data); });
    else if (which == 1)
        ok = GapCrashGuard::Run([&] { _nVifUnpack(static_cast<int>(c.idx), data, c.mode, fill); });
    else
        ok = GapCrashGuard::Run([&] { if (c.idx) dVifUnpack<1>(data, fill); else dVifUnpack<0>(data, fill); });
    return Capture(c, !ok);
}

// (M) independent model, narrow V2/V3/V4-16/8 only.
bool HasModel(u32 fmt) { return fmt == 5 || fmt == 6 || fmt == 9 || fmt == 10 || fmt == 13 || fmt == 14; }

Out RunModel(const Cfg& c, const u8* data)
{
    Prepare(c);
    Out o = Capture(c, false);
    nVifBlock b{};
    b.upkType = static_cast<u8>(c.fmt | (c.usn ? 32 : 0) | (c.maskBit ? 16 : 0));
    b.num = static_cast<u8>(c.num);
    b.mode = static_cast<u8>(c.mode);
    b.cl = static_cast<u8>(c.cl);
    b.wl = static_cast<u8>(c.wl);
    b.aligned = static_cast<u8>(c.aligned);
    b.mask = c.mask;
    std::array<u32, 4> row, col;
    std::memcpy(row.data(), c.row, 16);
    std::memcpy(col.data(), c.col, 16);
    VifNarrowLoadOracle::Model(b, data, o.mem, c.addr, row, col, c.wrap);
    std::memcpy(o.row, row.data(), 16);
    std::memcpy(o.col, col.data(), 16);
    IgnoreUndefinedW(o, c.fmt, c.mode);
    return o;
}

struct Verdict
{
    Out c, n, d, m;
    bool hasModel = false;
    bool crashedOverread = false;
};

Verdict RunAll(const Cfg& cfg, const std::vector<u8>& packet, u32 consumed)
{
    // Exact-end placement: the C kernels over-read V3/V4 by at most 16 bytes beyond the packet, so keep that much readable.
    const u32 bytes = consumed + 16;
    Guarded g(bytes);
    Verdict v;
    if (!g.data)
        return v;
    std::memset(g.data, 0xa7, bytes);
    std::memcpy(g.data, packet.data(), std::min<size_t>(packet.size(), bytes));
    // srcAlign shifts the packet start: keep the same end by using a second buffer view.
    v.c = RunImpl(cfg, 0, g.data);
    v.n = RunImpl(cfg, 1, g.data);
    v.d = RunImpl(cfg, 2, g.data);
    if (HasModel(cfg.fmt))
    {
        v.hasModel = true;
        v.m = RunModel(cfg, g.data);
    }
    return v;
}

// Classification of a mismatch: which voters agree with whom.
std::string Classify(const Cfg& cfg, const Verdict& v)
{
    std::string s;
    const bool dc = v.d.Equal(v.c), nc = v.n.Equal(v.c);
    if (v.d.crashed) s += "dynarec-crash ";
    if (v.c.crashed) s += "C-crash ";
    if (v.n.crashed) s += "interp-crash ";
    if (!dc) s += "dyn!=C ";
    if (!nc) s += "interp!=C ";
    if (!v.n.Equal(v.d) && dc) s += "interp!=dyn ";
    if (v.hasModel)
    {
        s += v.m.Equal(v.d) ? "model=dyn " : "model!=dyn ";
        s += v.m.Equal(v.c) ? "model=C " : "model!=C ";
    }
    (void)cfg;
    return s;
}

bool Mismatch(const Verdict& v) { return !v.d.Equal(v.c) || !v.n.Equal(v.c) || v.c.crashed || v.d.crashed || v.n.crashed; }

std::string DescribeDiff(const Out& a, const Out& b)
{
    char buf[200];
    if (a.crashed != b.crashed) return "crash differs";
    for (u32 i = 0; i < a.mem.size(); ++i)
        if (a.mem[i] != b.mem[i])
        {
            std::snprintf(buf, sizeof(buf), "mem[%03x] %02x vs %02x", i, a.mem[i], b.mem[i]);
            return buf;
        }
    if (std::memcmp(a.row, b.row, 16)) { std::snprintf(buf, sizeof(buf), "ROW %08x,%08x,%08x,%08x vs %08x,%08x,%08x,%08x", a.row[0], a.row[1], a.row[2], a.row[3], b.row[0], b.row[1], b.row[2], b.row[3]); return buf; }
    if (std::memcmp(a.col, b.col, 16)) return "COL differs";
    return "equal";
}

std::string CfgString(const Cfg& c)
{
    char b[320];
    std::snprintf(b, sizeof(b), "VIF%u %s usn=%u maskbit=%u mode=%u cl=%u wl=%u num=%u addr=%04x aligned=%u srcAlign=%u mask=%08x row=%08x,%08x,%08x,%08x col=%08x,%08x,%08x,%08x wrap=%u",
        c.idx, kFmt[c.fmt], c.usn, c.maskBit, c.mode, c.cl, c.wl, c.num, c.addr, c.aligned, c.srcAlign, c.mask,
        c.row[0], c.row[1], c.row[2], c.row[3], c.col[0], c.col[1], c.col[2], c.col[3], c.wrap);
    return b;
}

Cfg Generate(GapRng& r, bool mode3, bool zeroCl)
{
    static const u32 fmts[] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, 15};
    Cfg c;
    c.idx = r.Below(2);
    c.fmt = fmts[r.Below(13)];
    c.usn = r.Below(2);
    c.maskBit = r.Below(2);
    c.mode = r.Below(mode3 ? 4 : 3);
    const u32 kind = r.Below(10); // 0-3 fill, 4-6 equal, 7-9 skip
    if (kind < 4) { c.cl = 1 + r.Below(3); c.wl = c.cl + 1 + r.Below(3); }
    else if (kind < 7) { c.cl = c.wl = 1 + r.Below(4); }
    else { c.wl = 1 + r.Below(3); c.cl = c.wl + 1 + r.Below(4); }
    if (zeroCl && r.Chance(6)) { if (r.Chance(50)) c.cl = 0; else c.wl = 0; }
    static const u32 nums[] = {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 33, 64, 100, 255, 0};
    c.num = nums[r.Below(sizeof(nums) / 4)];
    if (c.idx == 0 && c.num > 40) c.num = 1 + r.Below(40);
    if (c.fmt == 15) c.usn = 0;
    c.aligned = 1 + r.Below(4);
    c.srcAlign = r.Chance(80) ? 4 * r.Below(4) : r.Below(16);
    c.mask = r.Chance(8) ? (r.Chance(50) ? 0u : 0xffffffffu) : r.Next();
    for (u32 i = 0; i < 4; ++i) { c.row[i] = r.Next(); c.col[i] = r.Next(); }
    // Fit the destination: shrink NUM until the span fits unless this case deliberately wraps.
    const u32 size = VuSize(c.idx);
    c.wrap = r.Chance(12);
    while (!c.wrap && Span(c) > size)
        c.num = c.num && c.num > 1 ? c.num / 2 : 1, c.num = c.num ? c.num : 1;
    if (c.wrap) c.addr = (r.Below(size / 16) * 16);
    else c.addr = r.Below((size - Span(c)) / 16 + 1) * 16;
    return c;
}

void Randomize(std::vector<u8>& d, GapRng& r)
{
    for (auto& b : d) b = static_cast<u8>(r.Next() >> 8);
}
} // namespace GapVif

void GapVifUnpackRandTests()
{
    using namespace GapVif;
    cpuinfo_initialize();
    const auto savedConfig = EmuConfig;
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    if (!SysMemory::Allocate())
    {
        EmuConfig = savedConfig;
        Check(false, "allocate VIF oracle memory");
        return;
    }
    const auto v0 = vif0, v1 = vif1;
    const auto r0 = vif0Regs, r1 = vif1Regs;
    const ScopedGuard restore([&] { EmuConfig = savedConfig; vif0 = v0; vif1 = v1; vif0Regs = r0; vif1Regs = r1; });
    VifUnpackSSE_Init();
    resetNewVif(0);
    resetNewVif(1);

    const u64 seed = GapKnob("GAP_VIF_SEED", GapKnob("GAP_SEED", 1));
    const u64 count = GapKnob("GAP_VIF_COUNT", GapKnob("GAP_COUNT", 30000));
    const bool mode3 = GapKnob("GAP_VIF_MODE3", 0) != 0, zeroCl = GapKnob("GAP_VIF_ZEROCL", 0) != 0;
    GapGroups groups;
    std::printf("GAP vif-unpack-rand seed=%llu count=%llu\n", static_cast<unsigned long long>(seed), static_cast<unsigned long long>(count));
    const auto t0 = std::chrono::steady_clock::now();
    std::map<std::string, u64> perFormat;
    for (u64 k = 0; k < count; ++k)
    {
        GapRng rng(seed * 7919ull + k);
        Cfg cfg = Generate(rng, mode3, zeroCl);
        const u32 consumed = Consumed(cfg);
        std::vector<u8> packet(consumed + 16);
        Randomize(packet, rng);
        // Source alignment: place the packet so that `srcAlign` is the byte offset of its start inside a 16-byte unit.
        Verdict v = RunAll(cfg, packet, consumed);
        ++groups.cases;
        ++perFormat[kFmt[cfg.fmt]];
        if (!Mismatch(v))
            continue;
        // Minimal NUM reproducer.
        Cfg best = cfg;
        Verdict bv = v;
        for (u32 n : {1u, 2u, 3u, 4u, 6u, 8u, 12u, 16u})
        {
            if (n >= (cfg.num ? cfg.num : 256)) break;
            Cfg t = cfg;
            t.num = n;
            t.wrap = false;
            if (Span(t) > VuSize(t.idx)) continue;
            t.addr = std::min(t.addr, VuSize(t.idx) - Span(t));
            const u32 cons = Consumed(t);
            std::vector<u8> p(packet.begin(), packet.begin() + std::min<size_t>(packet.size(), cons + 16));
            p.resize(cons + 16);
            Verdict tv = RunAll(t, p, cons);
            if (Mismatch(tv)) { best = t; bv = tv; packet = p; break; }
        }
        const char* kind = IsFill(best) ? "fill" : (best.cl == best.wl ? "equal" : "skip");
        const std::string cls = Classify(best, bv);
        char key[200];
        std::snprintf(key, sizeof(key), "%s %s mode=%u %s", kFmt[best.fmt], kind, best.mode, cls.c_str());
        const Out& other = !bv.d.Equal(bv.c) ? bv.d : bv.n;
        std::string ex = CfgString(best) + " | " + cls + "| first diff vs C: " + DescribeDiff(bv.c, other) + " | data:";
        char hx[8];
        for (size_t i = 0; i < packet.size() && i < 48; ++i) { std::snprintf(hx, sizeof(hx), " %02x", packet[i]); ex += hx; }
        groups.Record(key, best.num * 1000 + (best.wrap ? 500 : 0), ex);
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("GAP vif-unpack-rand direct runtime=%.1fs formats:", secs);
    for (const auto& kv : perFormat) std::printf(" %s=%llu", kv.first.c_str(), static_cast<unsigned long long>(kv.second));
    std::printf("\n");

    // Production partial-transfer path: random chunk splits through vifUnpackSetup/nVifUnpack versus the single-shot dynarec/C result.
    const u64 chunkCount = GapKnob("GAP_VIF_CHUNKS", count / 10);
    GapGroups chunkGroups;
    for (u64 k = 0; k < chunkCount; ++k)
    {
        GapRng rng(seed * 104729ull + k);
        Cfg cfg = Generate(rng, false, false);
        // Fill cycles after the last data word of a chunk write from data that has not arrived yet; the single-shot reference cannot model that
        // split (every such case mismatches, in S-/V4- formats too). Left to the 2-chunk NUM accounting of non-fill cycles. GAP_VIF_CHUNKFILL=1 includes it.
        if (IsFill(cfg) && !GapKnob("GAP_VIF_CHUNKFILL", 0)) continue;
        cfg.wrap = false;
        cfg.srcAlign = 0;
        cfg.addr = std::min(cfg.addr, VuSize(cfg.idx) - Span(cfg));
        auto& vif = cfg.idx ? vif1 : vif0;
        auto& regs = cfg.idx ? vif1Regs : vif0Regs;
        Prepare(cfg);
        regs.code = (cfg.num & 0xff) << 16 | (cfg.usn << 14) | ((cfg.addr >> 4) & 0x3ff) | ((cfg.fmt | (cfg.maskBit ? 0x10u : 0u)) << 24);
        vif.vifpacketsize = 4;
        if (cfg.idx) vifUnpackSetup<1>(nullptr); else vifUnpackSetup<0>(nullptr);
        const u32 totalWords = vif.tag.size;
        if (totalWords < 2)
            continue;
        const u32 split = 1 + rng.Below(totalWords - 1);
        std::vector<u8> packet(totalWords * 4 + 16);
        Randomize(packet, rng);
        // Expected: the single-shot C reference with the same logical data (start_aligned comes from the first chunk).
        Cfg ref = cfg;
        ref.aligned = 4 - ((split - 1) & 3);
        const Verdict single = RunAll(ref, packet, totalWords * 4);
        // Actual: two production calls.
        Prepare(cfg);
        regs.code = (cfg.num & 0xff) << 16 | (cfg.usn << 14) | ((cfg.addr >> 4) & 0x3ff) | ((cfg.fmt | (cfg.maskBit ? 0x10u : 0u)) << 24);
        vif.vifpacketsize = split;
        bool ok = GapCrashGuard::Run([&] {
            if (cfg.idx) vifUnpackSetup<1>(nullptr); else vifUnpackSetup<0>(nullptr);
            Guarded g1(split * 4 + 16);
            std::memcpy(g1.data, packet.data(), split * 4 + 16); // fill / V3-V4 reads past the chunk see the real following bytes, like the single-shot run
            if (cfg.idx) nVifUnpack<1>(g1.data); else nVifUnpack<0>(g1.data);
            const u32 rest = totalWords - split;
            Guarded g2(rest * 4 + 16);
            std::memcpy(g2.data, packet.data() + split * 4, rest * 4 + 16);
            vif.vifpacketsize = rest;
            if (cfg.idx) nVifUnpack<1>(g2.data); else nVifUnpack<0>(g2.data);
        });
        const Out actual = Capture(cfg, !ok);
        ++chunkGroups.cases;
        if (single.c.Equal(actual) || single.d.Equal(actual))
        {
            if (single.d.Equal(actual)) continue;
        }
        if (single.d.Equal(actual))
            continue;
        const char* kind = IsFill(cfg) ? "fill" : (cfg.cl == cfg.wl ? "equal" : "skip");
        char key[160];
        std::snprintf(key, sizeof(key), "chunked %s %s mode=%u %s", kFmt[cfg.fmt], kind, cfg.mode, single.d.Equal(single.c) ? "single-shot agrees with C" : "single-shot dyn!=C");
        chunkGroups.Record(key, totalWords, CfgString(cfg) + " split=" + std::to_string(split) + "/" + std::to_string(totalWords) + " words: " +
            DescribeDiff(single.d, actual));
    }
    std::printf("GAP vif-unpack-rand chunked cases=%llu mismatching=%llu\n", static_cast<unsigned long long>(chunkGroups.cases), static_cast<unsigned long long>(chunkGroups.fails));
    groups.Report("vif-unpack-rand");
    chunkGroups.Report("vif-unpack-rand chunked");
}
