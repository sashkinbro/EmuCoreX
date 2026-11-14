// SPDX-License-Identifier: GPL-3.0+
// mtvu-xgkick: random VU1 programs with XGKICK (valid GIF tag chains in VU data memory, incl. address wrap at the end of the 16 KiB
// memory, one to three kicks per program, FMAC/integer/memory traffic around them, stores into packet data after the kick) run with
//   (off)  MTVU off: arm64 VU1 JIT, XGKICK data captured at the GIF-path entry (the existing oracle hook),
//   (int)  MTVU off: VU1 interpreter, same capture,
//   (on)   MTVU on:  the same JIT program executed the way VU_Thread::ExecuteRingBuffer does it (a real second thread runs
//          SetStartPC/Execute(vu1RunCycles)/FinishGSPacketMTVU against the real Gif_Path ring, EmuConfig.Speedhacks.vuThread = true), while
//          this thread consumes the queued GS packets like the MTGS thread (concurrently in half of the cases, after the join otherwise).
// Compared: the bytes of every GIF packet (concatenated), VU1 VF/VI0..15/ACC/MAC/STATUS/CLIP/Q/P/I/R and the 16 KiB data memory,
// plus the MTVU interrupt flags (the E-bit must leave exactly InterruptFlagVUEBit pending, nothing else, as no A+D register is used).
// "on vs off" is the MTVU check; "off vs int" is reported as a separate, informational-but-failing group because it is a plain
// JIT/interpreter XGKICK difference that does not involve MTVU.
// Knobs: GAP_MTVU_SEED (default 1), GAP_MTVU_COUNT (default 600), GAP_MTVU_XGHACK=0/1 (force Gamefixes.XgKickHack, default alternates).

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)
namespace GapMtvu
{
namespace E = x86p::enc;
using x86p::Rng;
using x86p::VProg;

constexpr u32 kXgkickWord = 0x800006fcu; // XGKICK is: (0x40<<25) | (is << 11) | (27 << 6) | 0x3c

struct Case
{
    VProg prog;
    std::vector<u8> mem; // 0x4000 bytes initial VU1 data memory
    u32 vi[16];
    u32 vf[128];
    std::vector<u32> kicks; // qword address of every packet (diagnostics)
    bool postWrite = false; // a store into packet data follows a kick: the interpreter transfers progressively (8 bytes per cycle) while the JIT copies at the
                            // kick (XgKickHack decides when), so such programs are only compared MTVU on vs off
};

struct Out
{
    std::vector<u8> gif;
    u32 vf[128]{};
    u32 vi[16]{};
    u32 acc[4]{};
    u32 mac = 0, status = 0, clip = 0, q = 0, p = 0, i = 0, r = 0;
    std::vector<u8> mem;
    u32 interrupts = 0;
    u32 packets = 0;
    bool crashed = false;
};

static void PutQw(std::vector<u8>& mem, u32 qword, u64 lo, u64 hi)
{
    const u32 a = (qword & 0x3ff) * 16;
    std::memcpy(&mem[a], &lo, 8);
    std::memcpy(&mem[a + 8], &hi, 8);
}

// Builds a chain of 1..3 tags (the last has EOP) starting at qword `at`; returns the number of qwords used.
static u32 BuildPacket(Rng& r, std::vector<u8>& mem, u32 at)
{
    const u32 tags = 1 + r.below(3);
    u32 pos = at;
    for (u32 t = 0; t < tags; ++t)
    {
        const u32 flg = r.below(3); // 0 PACKED, 1 REGLIST, 2 IMAGE
        const u32 nloop = 1 + r.below(flg == 2 ? 24 : 8);
        u32 nreg = flg == 2 ? 0 : 1 + r.below(3);
        u64 regs = 0;
        for (u32 k = 0; k < nreg; ++k)
        {
            static const u32 ok[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0xf}; // never 0xe (A+D: it raises GS side effects)
            regs |= static_cast<u64>(ok[r.below(9)]) << (4 * k);
        }
        const bool eop = t + 1 == tags;
        const u64 lo = nloop | (static_cast<u64>(eop) << 15) | (static_cast<u64>(r.below(2)) << 46) | (static_cast<u64>(r.next() & 0x7ff) << 47) | (static_cast<u64>(flg) << 58) |
            (static_cast<u64>(nreg & 3) << 60);
        PutQw(mem, pos++, lo, regs);
        const u32 data = flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop;
        for (u32 d = 0; d < data; ++d)
            PutQw(mem, pos++, (static_cast<u64>(r.next()) << 32) | r.next(), (static_cast<u64>(r.next()) << 32) | r.next());
    }
    return pos - at;
}

static Case Generate(u64 seed, u32 k)
{
    Rng r(seed * 0x9e3779b97f4a7c15ull + k * 7919ull + 33);
    Case c;
    c.mem.resize(0x4000);
    for (u32 i = 0; i < 0x4000; ++i)
        c.mem[i] = static_cast<u8>(i * 13 + 7);
    x86p::RefInit ri;
    GapVu::GenData(ri, r, 1, false);
    std::memcpy(c.vf, ri.fm.vf, sizeof(c.vf));
    std::memcpy(c.vi, ri.vi, sizeof(c.vi));
    const u32 kicks = 1 + r.below(3);
    u32 next = 0x80 + r.below(0x40); // packets live in [0x80, 0x3d0); qwords 0x60..0x6f are the program scratch stores; a wrapping chain spills into [0, 0x50)
    const u32 wrapIdx = r.below(kicks * 4); // at most one chain wraps (two overlapping chains would corrupt each other's tags)
    for (u32 n = 0; n < kicks; ++n)
    {
        u32 at = next;
        if (n == wrapIdx)
            at = 0x400 - 1 - r.below(12); // the chain runs over the end of VU memory and wraps to qword 0
        const u32 used = BuildPacket(r, c.mem, at);
        c.kicks.push_back(at);
        next = at + used + 1 + r.below(16);
        if (next > 0x3d0 || next < 0x80) next = 0x80 + r.below(0x40) + 0x200 * n / 4;
    }
    VProg& p = c.prog;
    const auto filler = [&](u32 n) {
        for (u32 i = 0; i < n; ++i)
        {
            u32 lo = NOP_L, up = NOP_U;
            if (r.below(100) < 60) up = GapVu::GenUpper(r);
            if (r.below(100) < 40)
            {
                switch (r.below(4))
                {
                    case 0: lo = E::IADDIU(1 + r.below(6), 0, r.next() & 0x7ff); break;
                    case 1: lo = E::IADD(1 + r.below(6), r.below(7), r.below(7)); break;
                    case 2: lo = E::SQ(1 + r.below(15), 1 + r.below(12), 0, 0x10 + r.below(0x20)); break; // low qwords: never packet data
                    default: lo = E::MOVE(1 + r.below(15), 9 + r.below(4), 1 + r.below(8)); break;
                }
            }
            p.pair(lo, up);
        }
    };
    filler(r.below(4));
    for (u32 n = 0; n < kicks; ++n)
    {
        p.lower(E::IADDIU(9, 0, c.kicks[n] & 0x3ff));
        filler(r.below(4));
        p.lower(kXgkickWord | (9u << 11));
        filler(r.below(5));
        if (r.below(3) == 0) // overwrite data words of the packet that was just kicked: the transfer reads them at kick/delay time in every mode
        {
            p.lower(E::SQ(0xf, 1 + r.below(12), 0, (c.kicks[n] + 1) & 0x3ff)); // the first data qword of the first tag
            c.postWrite = true;
        }
        filler(r.below(3));
    }
    p.nop(r.below(24));
    p.pair(NOP_L, NOP_U | x86p::kEbit);
    p.nop(2);
    return c;
}

static Out FromSnapshot(const Snapshot& s)
{
    Out o;
    o.gif = s.gif;
    for (u32 i = 0; i < 128; ++i) o.vf[i] = s.regs.VF[i >> 2].UL[i & 3];
    for (u32 i = 0; i < 16; ++i) o.vi[i] = s.regs.VI[i].UL & 0xffff;
    std::memcpy(o.acc, &s.regs.ACC, 16);
    o.mac = s.regs.VI[REG_MAC_FLAG].UL;
    o.status = s.regs.VI[REG_STATUS_FLAG].UL;
    o.clip = s.regs.VI[REG_CLIP_FLAG].UL;
    o.q = s.regs.VI[REG_Q].UL;
    o.p = s.regs.VI[REG_P].UL;
    o.i = s.regs.VI[REG_I].UL;
    o.r = s.regs.VI[REG_R].UL;
    o.mem = s.memory;
    return o;
}

static Out RunOff(const Case& c, bool jit)
{
    EmuConfig.Speedhacks.vuThread = false;
    Out o;
    if (!GapCrashGuard::Run([&] {
            Snapshot s = RunVU(1, jit, c.prog.w, 0x80, 0, 512, false, &c.mem, c.vf, nullptr, c.vi);
            o = FromSnapshot(s);
        }))
        o.crashed = true;
    return o;
}

// resetPath: start from an empty Gif_Path ring (false continues a previous run, like consecutive VU programs of one frame); keepPath: leave the
// ring untouched afterwards so the next call can continue it.
static Out RunOn(const Case& c, bool concurrent, bool resetPath = true, bool keepPath = false)
{
    Out o;
    EmuConfig.Speedhacks.vuThread = true;
    auto& path = gifUnit.gifPath[GIF_PATH_1];
    const u32 savedSize = path.buffSize, savedLimit = path.buffLimit;
    if (resetPath)
        path.Reset();
    vu1Thread.mtvuInterrupts.store(0);
    vu1Thread.gsSignal.store(0);
    vu1Thread.gsLabel.store(0);
    std::memset(&vu1Thread.vifRegs, 0, sizeof(vu1Thread.vifRegs));
    // Same initial state as RunVU, VU1 only.
    VURegs& vu = VU1;
    u8* const micro = vu.Micro;
    u8* const mem = vu.Mem;
    std::memset(&vu, 0, sizeof(vu));
    vu.Micro = micro;
    vu.Mem = mem;
    vu.idx = 1;
    vu.VF[0].f.w = 1.0f;
    for (u32 r = 1; r < 32; ++r)
        for (u32 lane = 0; lane < 4; ++lane)
            vu.VF[r].UL[lane] = 0x3f800000 + (r * 4 + lane) * 0x1000;
    for (u32 r = 1; r < 16; ++r)
        vu.VI[r].UL = r;
    for (u32 r = 1; r < 16; ++r)
        vu.VI[r].UL = c.vi[r];
    std::memcpy(vu.VF, c.vf, sizeof(vu.VF));
    for (u32 i = 0; i < 0x4000; i += 8)
    {
        std::memcpy(micro + i, &NOP_L, 4);
        std::memcpy(micro + i + 4, &NOP_U, 4);
    }
    std::memcpy(micro + 0x80, c.prog.w.data(), c.prog.w.size() * sizeof(u32));
    std::memcpy(mem, c.mem.data(), 0x4000);
    VU0.VI[REG_VPU_STAT].UL = 0;
    vu.VI[REG_TPC].UL = 0x80 / 8;
    CpuMicroVU1.Reset();
    OpcodeFamilies::SetFamilyMask(OpcodeFamilies::CORE_VU1, 0);
    std::atomic<bool> done{false};
    std::atomic<bool> ok{true};
    const FPControlRegisterBackup callerFPCR(EmuConfig.Cpu.FPUFPCR);
    // The worker thread must see the guest FP control state the VU dispatcher expects: run the whole execution under the same backup.
    std::thread worker([&] {
        const bool fine = GapCrashGuard::Run([&] {
            const FPControlRegisterBackup workerFPCR(EmuConfig.Cpu.FPUFPCR);
            VU1.cycle = 0;
            CpuMicroVU1.SetStartPC(0x80);
            CpuMicroVU1.Execute(vu1RunCycles);
            path.FinishGSPacketMTVU();
        });
        ok.store(fine);
        done.store(true, std::memory_order_release);
    });
    const auto consume = [&]() {
        while (path.GetPendingGSPackets())
        {
            const GS_Packet packet = path.GetGSPacketMTVU();
            if (static_cast<u64>(packet.offset) + packet.size > path.buffSize || (packet.size & 15))
                o.crashed = true; // malformed packet descriptor
            else
                o.gif.insert(o.gif.end(), path.buffer + packet.offset, path.buffer + packet.offset + packet.size);
            const s32 amount = static_cast<s32>(packet.size) + packet.readAmount;
            path.readAmount.fetch_sub(amount, std::memory_order_release);
            path.PopGSPacketMTVU();
            ++o.packets;
        }
    };
    if (concurrent)
    {
        while (!done.load(std::memory_order_acquire))
        {
            consume();
            std::this_thread::yield();
        }
    }
    worker.join();
    consume();
    if (!ok.load())
        o.crashed = true;
    o.interrupts = vu1Thread.mtvuInterrupts.load();
    for (u32 i = 0; i < 128; ++i) o.vf[i] = vu.VF[i >> 2].UL[i & 3];
    for (u32 i = 0; i < 16; ++i) o.vi[i] = vu.VI[i].UL & 0xffff;
    std::memcpy(o.acc, &vu.ACC, 16);
    o.mac = vu.VI[REG_MAC_FLAG].UL;
    o.status = vu.VI[REG_STATUS_FLAG].UL;
    o.clip = vu.VI[REG_CLIP_FLAG].UL;
    o.q = vu.VI[REG_Q].UL;
    o.p = vu.VI[REG_P].UL;
    o.i = vu.VI[REG_I].UL;
    o.r = vu.VI[REG_R].UL;
    o.mem.assign(mem, mem + 0x4000);
    vu1Thread.mtvuInterrupts.store(0);
    if (!keepPath)
    {
        path.Reset();
        path.buffSize = savedSize;
        path.buffLimit = savedLimit;
    }
    EmuConfig.Speedhacks.vuThread = false;
    return o;
}

// First difference between two runs (empty = equal). Flags compare with the O/U bits masked: x86 microVU never computes them.
static std::string Diff(const Out& a, const Out& b, bool cmpInterrupts)
{
    char t[160];
    if (a.crashed != b.crashed)
        return a.crashed ? "CRASH(first)" : "CRASH(second)";
    if (a.crashed)
        return std::string();
    if (a.gif.size() != b.gif.size())
    {
        std::snprintf(t, sizeof(t), "GIF.length first=%zu second=%zu", a.gif.size(), b.gif.size());
        return t;
    }
    for (size_t i = 0; i < a.gif.size(); ++i)
        if (a.gif[i] != b.gif[i])
        {
            std::snprintf(t, sizeof(t), "GIF.byte@%zu(of %zu) first=%02x second=%02x", i, a.gif.size(), a.gif[i], b.gif[i]);
            return t;
        }
    for (u32 i = 0; i < 128; ++i)
        if (a.vf[i] != b.vf[i])
        {
            std::snprintf(t, sizeof(t), "VF%u.%c first=%08x second=%08x", i >> 2, "xyzw"[i & 3], a.vf[i], b.vf[i]);
            return t;
        }
    for (u32 i = 0; i < 16; ++i)
        if (a.vi[i] != b.vi[i])
        {
            std::snprintf(t, sizeof(t), "VI%u first=%04x second=%04x", i, a.vi[i], b.vi[i]);
            return t;
        }
    if (std::memcmp(a.acc, b.acc, 16)) return "ACC";
    if ((a.mac & ~0xff00u) != (b.mac & ~0xff00u)) return "MAC";
    if ((a.status & ~0x30cu) != (b.status & ~0x30cu)) return "STATUS";
    if (a.clip != b.clip) return "CLIP";
    if (a.q != b.q) return "Q";
    if (a.p != b.p) return "P";
    if (a.i != b.i) return "I";
    if (a.r != b.r) return "R";
    for (u32 i = 0; i < 0x4000; ++i)
        if (a.mem[i] != b.mem[i])
        {
            std::snprintf(t, sizeof(t), "MEM@%04x first=%02x second=%02x", i, a.mem[i], b.mem[i]);
            return t;
        }
    if (cmpInterrupts && a.interrupts != b.interrupts)
    {
        std::snprintf(t, sizeof(t), "interrupts first=%x second=%x", a.interrupts, b.interrupts);
        return t;
    }
    return std::string();
}

static std::string Family(const std::string& d)
{
    const size_t sp = d.find_first_of(" @.");
    std::string f = d.substr(0, sp);
    if (f.rfind("VF", 0) == 0) f = "VF";
    else if (f.rfind("VI", 0) == 0) f = "VI";
    return f;
}

static std::string CaseDesc(const Case& c, u32 k, u64 seed, bool xg, bool conc)
{
    std::string s = "k=" + std::to_string(k) + " seed=" + std::to_string(seed) + " XgKickHack=" + (xg ? "1" : "0") + " concurrentConsume=" + (conc ? "1" : "0") + " kicks@qw{";
    for (u32 a : c.kicks) s += std::to_string(a) + " ";
    s += "} prog{" + GapVu::Sparse(c.prog) + "}";
    return s;
}

static void Run()
{
    const u64 seed = GapKnob("GAP_MTVU_SEED", GapKnob("GAP_SEED", 1));
    const u32 count = static_cast<u32>(GapKnob("GAP_MTVU_COUNT", GapKnob("GAP_COUNT", 0) ? GapKnob("GAP_COUNT", 0) : 600));
    const u64 forceHack = GapKnob("GAP_MTVU_XGHACK", 2);
    x86p::configure(1, 0);
    setenv("X86P_IGNORE_UO", "1", 1);
    GapGroups groups;
    const bool savedHack = EmuConfig.Gamefixes.XgKickHack;
    const bool savedEnableVU1 = EmuConfig.Cpu.Recompiler.EnableVU1;
    EmuConfig.Cpu.Recompiler.EnableVU1 = true;
    CpuVU1 = &CpuMicroVU1;
    u64 packets = 0;
    for (u32 k = 0; k < count; ++k)
    {
        const bool xg = forceHack == 2 ? (k & 1) != 0 : forceHack != 0;
        const bool conc = (k & 2) != 0;
        EmuConfig.Gamefixes.XgKickHack = xg;
        const Case c = Generate(seed, k);
        const Out off = RunOff(c, true);
        const Out ref = RunOff(c, false);
        const Out on = RunOn(c, conc);
        packets += on.packets;
        ++groups.cases;
        const std::string tag = CaseDesc(c, k, seed, xg, conc);
        bool bad = false;
        if (on.crashed || off.crashed)
        {
            char ex[1800];
            std::snprintf(ex, sizeof(ex), "CRASH off=%d on=%d signal=%d pc=%llx %s", off.crashed, on.crashed, GapCrashGuard::lastSig, static_cast<unsigned long long>(GapCrashGuard::lastPc), tag.c_str());
            groups.Record(std::string("CRASH ") + (on.crashed ? "MTVU on" : "MTVU off"), c.prog.w.size(), ex);
            continue;
        }
        std::string d = Diff(off, on, false);
        if (!d.empty())
        {
            groups.Record("MTVU on vs off [" + Family(d) + "]" + std::string(xg ? " XgKickHack" : "") + (conc ? " concurrent" : ""), c.prog.w.size(), d + " " + tag);
            bad = true;
        }
        if (on.interrupts != VU_Thread::InterruptFlagVUEBit)
        {
            char ex[200];
            std::snprintf(ex, sizeof(ex), "interrupts=%x expected=%x %s", on.interrupts, static_cast<u32>(VU_Thread::InterruptFlagVUEBit), tag.c_str());
            groups.Record("MTVU interrupt flags after E-bit", c.prog.w.size(), ex);
            bad = true;
        }
        if (on.packets != 1)
        {
            char ex[200];
            std::snprintf(ex, sizeof(ex), "GS packets queued=%u expected=1 (one FinishGSPacketMTVU per VU program) %s", on.packets, tag.c_str());
            groups.Record("MTVU queued packet count", c.prog.w.size(), ex);
            bad = true;
        }
        d = c.postWrite ? std::string() : Diff(off, ref, false);
        if (!d.empty())
        {
            groups.Record("JIT vs interpreter XGKICK program (MTVU off) [" + Family(d) + "]" + std::string(xg ? " XgKickHack" : ""), c.prog.w.size(), d + " " + tag);
            bad = true;
        }
        (void)bad;
    }
    // Consecutive VU programs through one ring without resetting the path (packet offsets, realign, the carried gsPack.offset): three programs
    // per case, MTVU on, versus the three MTVU-off runs concatenated.
    const u32 seqCount = std::max<u32>(1, count / 6);
    for (u32 k = 0; k < seqCount; ++k)
    {
        const bool xg = (k & 1) != 0, conc = (k & 2) != 0;
        EmuConfig.Gamefixes.XgKickHack = xg;
        Case cs[3];
        Out offs[3], ons[3];
        std::vector<u8> expectGif, gotGif;
        bool crashed = false;
        for (u32 s = 0; s < 3; ++s)
        {
            cs[s] = Generate(seed + 7777, k * 3 + s);
            offs[s] = RunOff(cs[s], true);
            expectGif.insert(expectGif.end(), offs[s].gif.begin(), offs[s].gif.end());
            crashed |= offs[s].crashed;
        }
        for (u32 s = 0; s < 3; ++s)
        {
            ons[s] = RunOn(cs[s], conc, s == 0, s != 2);
            crashed |= ons[s].crashed;
            gotGif.insert(gotGif.end(), ons[s].gif.begin(), ons[s].gif.end());
        }
        ++groups.cases;
        if (crashed)
        {
            groups.Record("CRASH MTVU sequence", cs[0].prog.w.size(), "CRASH k=" + std::to_string(k));
            continue;
        }
        for (u32 s = 0; s < 3; ++s)
        {
            const std::string d = Diff(offs[s], ons[s], false);
            if (!d.empty())
            {
                groups.Record("MTVU sequence on vs off, program " + std::to_string(s) + " [" + Family(d) + "]", cs[s].prog.w.size(), d + " " + CaseDesc(cs[s], k * 3 + s, seed + 7777, xg, conc));
                break;
            }
        }
        if (expectGif != gotGif)
        {
            char ex[200];
            std::snprintf(ex, sizeof(ex), "expected %zu bytes got %zu seqK=%u XgKickHack=%d conc=%d", expectGif.size(), gotGif.size(), k, xg, conc);
            groups.Record("MTVU sequence: concatenated GIF stream differs from the MTVU-off stream", cs[0].prog.w.size(), ex);
        }
    }
    std::printf("GAP mtvu-xgkick: %llu GS packets consumed through the MTVU ring\n", static_cast<unsigned long long>(packets));
    EmuConfig.Gamefixes.XgKickHack = savedHack;
    EmuConfig.Cpu.Recompiler.EnableVU1 = savedEnableVU1;
    EmuConfig.Speedhacks.vuThread = false;
    groups.Report("mtvu-xgkick");
}
} // namespace GapMtvu

void GapMtvuXgkickTests()
{
    GapEnv env;
    if (env.ok)
        GapMtvu::Run();
}
#else
void GapMtvuXgkickTests() { Check(false, "mtvu-xgkick needs a debug oracle build (EMUCOREX_ENABLE_NATIVE_SELF_TESTS, !NDEBUG)"); }
#endif
