// SPDX-License-Identifier: GPL-3.0+
// iop-rand-block: random R3000A programs, IOP recompiler vs IOP interpreter.
// Covers ALU, shifts, mult/div + HI/LO, branches with delay slots, loads/stores (aligned and LWL/LWR/SWL/SWR),
// COP0 reads/writes, SYSCALL/BREAK through a real exception handler at 0x80, small loops and IOP RAM SMC.
// Knobs: GAP_IOP_SEED, GAP_IOP_COUNT (default 20000), GAP_IOP_MAXLEN (default 90 constructs), GAP_IOP_ISC=1 (also toggle Status.IsC).

namespace GapIop
{
constexpr u32 kBase = IOP_TEST_SCRATCH + 0x200; // r22 points here; offsets -0x200..+0x1ff stay inside the compared window
constexpr u32 kWindow = 0x400;                  // compared scratch bytes starting at IOP_TEST_SCRATCH

struct Result
{
    IOPSnapshot s{};
    bool crashed = false;
    u64 crashPc = 0, crashAddr = 0;
    int crashSig = 0;
    std::vector<u8> mem;
    std::vector<u32> code;
};

void InstallHandler()
{
    const u32 handler[] = {
        MipsI(0x10, 0, 26, 14u << 11),      // mfc0 k0, EPC
        MipsI(0x10, 0, 27, 13u << 11),      // mfc0 k1, Cause
        MipsI(9, 26, 26, 4),                // addiu k0, k0, 4
        MipsI(1, 27, 1, 2),                 // bgez k1, +2 (Cause.BD clear: skip the extra advance)
        0,
        MipsI(9, 26, 26, 4),                // addiu k0, k0, 4 (branch delay slot: skip the delay slot instruction too)
        MipsI(43, 22, 26, 0x1f0),           // sw k0, 0x1f0(r22)
        MipsI(43, 22, 27, 0x1f4),           // sw k1, 0x1f4(r22)
        MipsR(26, 0, 0, 0, 8),              // jr k0
        0x42000010u,                        // rfe (delay slot)
    };
    for (u32 i = 0; i < sizeof(handler) / 4; ++i)
        iopMemWrite32(0x80 + i * 4, handler[i]);
}

Result Run(bool jit, const std::vector<u32>& code)
{
    InstallHandler();
    for (u32 i = 0; i < kWindow; i += 4)
        iopMemWrite32(IOP_TEST_SCRATCH + i, 0x9e3779b1u * (i + 1));
    Result r;
    if (!GapCrashGuard::Run([&] { r.s = RunIOPProgram(jit, code, 40000); }))
    {
        r.crashed = true;
        r.crashPc = GapCrashGuard::lastPc;
        r.crashAddr = GapCrashGuard::lastAddr;
        r.crashSig = GapCrashGuard::lastSig;
        psxCpu = &psxInt;
    }
    r.mem.resize(kWindow);
    for (u32 i = 0; i < kWindow; ++i)
        r.mem[i] = iopMemRead8(IOP_TEST_SCRATCH + i);
    r.code.resize(code.size() + 2);
    for (u32 i = 0; i < r.code.size(); ++i)
        r.code[i] = iopMemRead32(IOP_TEST_PC + i * 4);
    return r;
}

// Returns the name of the first differing architectural field, or nullptr when equal.
const char* Diff(const Result& a, const Result& b, std::string& detail)
{
    char buf[160];
    if (a.crashed || b.crashed)
    {
        const Result& c = a.crashed ? a : b;
        std::snprintf(buf, sizeof(buf), "%s crashed sig=%d pc=%llx addr=%llx", a.crashed ? "interpreter" : "recompiler", c.crashSig,
            static_cast<unsigned long long>(c.crashPc), static_cast<unsigned long long>(c.crashAddr));
        detail = buf;
        return a.crashed ? "interpreter crash" : "recompiler crash";
    }
    for (u32 i = 0; i < 32; ++i)
        if (a.s.gpr[i] != b.s.gpr[i])
        {
            std::snprintf(buf, sizeof(buf), "r%u interp=%08x jit=%08x", i, a.s.gpr[i], b.s.gpr[i]);
            detail = buf;
            return "GPR";
        }
    if (a.s.hi != b.s.hi) { std::snprintf(buf, sizeof(buf), "HI interp=%08x jit=%08x", a.s.hi, b.s.hi); detail = buf; return "HI"; }
    if (a.s.lo != b.s.lo) { std::snprintf(buf, sizeof(buf), "LO interp=%08x jit=%08x", a.s.lo, b.s.lo); detail = buf; return "LO"; }
    for (u32 i = 0; i < 32; ++i)
    {
        if (a.s.cp2d[i] != b.s.cp2d[i]) { std::snprintf(buf, sizeof(buf), "CP2D[%u] interp=%08x jit=%08x", i, a.s.cp2d[i], b.s.cp2d[i]); detail = buf; return "CP2 data"; }
        if (a.s.cp2c[i] != b.s.cp2c[i]) { std::snprintf(buf, sizeof(buf), "CP2C[%u] interp=%08x jit=%08x", i, a.s.cp2c[i], b.s.cp2c[i]); detail = buf; return "CP2 control"; }
    }
    if (a.s.pc != b.s.pc) { std::snprintf(buf, sizeof(buf), "PC interp=%08x jit=%08x", a.s.pc, b.s.pc); detail = buf; return "PC"; }
    for (u32 i = 0; i < 32; ++i)
    {
        if (i == 1 || i == 9 || i == 11)
            continue; // Random/Count/Compare depend on cycle accounting
        if (a.s.cp0[i] != b.s.cp0[i])
        {
            std::snprintf(buf, sizeof(buf), "CP0[%u] interp=%08x jit=%08x", i, a.s.cp0[i], b.s.cp0[i]);
            detail = buf;
            return i == 12 ? "COP0 Status" : i == 13 ? "COP0 Cause" : i == 14 ? "COP0 EPC" : "COP0 other";
        }
    }
    for (u32 i = 0; i < a.mem.size(); ++i)
        if (a.mem[i] != b.mem[i])
        {
            std::snprintf(buf, sizeof(buf), "mem[scratch+%03x] interp=%02x jit=%02x", i, a.mem[i], b.mem[i]);
            detail = buf;
            return "memory";
        }
    for (u32 i = 0; i < a.code.size(); ++i)
        if (a.code[i] != b.code[i])
        {
            std::snprintf(buf, sizeof(buf), "code[%u] interp=%08x jit=%08x", i, a.code[i], b.code[i]);
            detail = buf;
            return "code memory";
        }
    return nullptr;
}

struct Gen
{
    GapRng& r;
    std::vector<u32>& c;
    bool smc = true, exc = true, cop0 = true, isc = false, sameBlock = false, bdExc = false;
    u32 smcPct = 8;

    u32 Hot() { return 8 + r.Below(8); }
    u32 Any()
    {
        static const u8 pool[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 24, 25};
        return r.Chance(65) ? Hot() : pool[r.Below(sizeof(pool))];
    }
    u32 Dst() { return r.Chance(4) ? 0 : Any(); }
    u32 Imm()
    {
        static const u32 e[] = {0, 1, 2, 3, 0x7fff, 0x8000, 0xffff, 0xfffe, 0x00ff, 0x0100, 0x4000, 0xc000};
        return r.Chance(60) ? e[r.Below(sizeof(e) / 4)] : (r.Next() & 0xffff);
    }
    u32 Val()
    {
        static const u32 e[] = {0, 1, 2, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0xfffffffeu, 0x0000ffffu, 0xffff0000u, 0x12345678u, 0x80000001u, 0x7ffffffeu};
        return r.Chance(55) ? e[r.Below(sizeof(e) / 4)] : r.Next();
    }
    void LoadConst(u32 reg, u32 v)
    {
        c.push_back(MipsI(15, 0, reg, v >> 16));
        c.push_back(MipsI(13, reg, reg, v & 0xffff));
    }

    // A single, control-flow free, non-exception instruction (safe in delay slots and skipped regions).
    void Simple(bool allowStore = true, bool allowLoad = true)
    {
        switch (r.Below(10))
        {
            case 0: case 1: // R-type ALU
            {
                static const u8 fn[] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2a, 0x2b};
                c.push_back(MipsR(Any(), Any(), Dst(), 0, fn[r.Below(sizeof(fn))]));
                break;
            }
            case 2: case 3: // I-type
            {
                static const u8 op[] = {8, 9, 10, 11, 12, 13, 14, 15};
                const u32 o = op[r.Below(sizeof(op))];
                c.push_back(MipsI(o, o == 15 ? 0 : Any(), Dst(), Imm()));
                break;
            }
            case 4: // shifts
            {
                if (r.Chance(55))
                {
                    static const u8 fn[] = {0, 2, 3};
                    c.push_back(MipsR(0, Any(), Dst(), r.Below(32), fn[r.Below(3)]));
                }
                else
                {
                    static const u8 fn[] = {4, 6, 7};
                    const u32 rs = Any();
                    if (r.Chance(85) && rs != 0)
                        c.push_back(MipsI(12, rs, rs, 31)); // keep variable shift counts architectural (interpreter shifts are C++)
                    c.push_back(MipsR(rs, Any(), Dst(), 0, fn[r.Below(3)]));
                }
                break;
            }
            case 5: // multiply/divide/HI/LO
            {
                static const u8 fn[] = {0x18, 0x19, 0x1a, 0x1b, 0x10, 0x11, 0x12, 0x13};
                const u32 f = fn[r.Below(sizeof(fn))];
                if (f >= 0x18) c.push_back(MipsR(Any(), Any(), 0, 0, f));
                else if (f == 0x10 || f == 0x12) c.push_back(MipsR(0, 0, Dst(), 0, f));
                else c.push_back(MipsR(Any(), 0, 0, 0, f));
                break;
            }
            case 6: case 7: // memory
            {
                if (!allowLoad && !allowStore) { c.push_back(0); break; }
                static const u8 ld[] = {32, 33, 34, 35, 36, 37, 38};
                static const u8 st[] = {40, 41, 42, 43, 46};
                bool load = allowLoad && (!allowStore || r.Chance(55));
                u32 op = load ? ld[r.Below(sizeof(ld))] : st[r.Below(sizeof(st))];
                s32 off = static_cast<s32>(r.Below(0x1c0 + 0x200)) - 0x200;
                const bool wordLike = op == 35 || op == 43, half = op == 33 || op == 37 || op == 41;
                if (r.Chance(90) && wordLike) off &= ~3;
                if (r.Chance(90) && half) off &= ~1;
                c.push_back(MipsI(op, 22, load ? Dst() : Any(), static_cast<u32>(off)));
                break;
            }
            case 8: // COP0
            {
                if (!cop0) { c.push_back(0); break; }
                static const u8 rd[] = {3, 5, 6, 7, 8, 12, 13, 14, 15};
                const u32 k = r.Below(10);
                if (k < 6) c.push_back(MipsI(0x10, 0, Dst(), static_cast<u32>(rd[r.Below(sizeof(rd))]) << 11)); // mfc0
                else if (k < 9)
                {
                    static const u8 wr[] = {3, 5, 6, 7, 12, 14};
                    const u32 reg = wr[r.Below(sizeof(wr))], src = Any();
                    if (reg == 12 && src != 0)
                    {
                        // Keep BEV/IsC/TS/CM out of Status unless explicitly enabled (see GAP_IOP_ISC).
                        c.push_back(MipsI(15, 0, 28, isc ? 0xf001 : 0xf000));
                        c.push_back(MipsI(13, 28, 28, 0xff3f));
                        c.push_back(MipsR(src, 28, 28, 0, 0x24));
                        c.push_back(MipsI(0x10, 4, 28, 12u << 11)); // mtc0 r28, Status
                    }
                    else
                        c.push_back(MipsI(0x10, 4, src, static_cast<u32>(reg) << 11)); // mtc0
                }
                else c.push_back(0x42000010u); // rfe
                break;
            }
            default: // lui/ori value loads keep operands interesting
                LoadConst(Hot(), Val());
                break;
        }
    }

    // Exactly one word (delay slots and skipped regions must never contain multi-word sequences).
    void Simple1(bool allowStore = true, bool allowLoad = true)
    {
        for (u32 attempt = 0; attempt < 12; ++attempt)
        {
            const size_t n = c.size();
            Simple(allowStore, allowLoad);
            if (c.size() == n + 1)
                return;
            c.resize(n);
        }
        c.push_back(MipsR(Any(), Any(), Dst(), 0, 0x21));
    }

    // Delay slot content: simple instruction, rarely a SYSCALL/BREAK.
    void DelaySlot()
    {
        if (exc && bdExc && r.Chance(4))
            c.push_back(MipsR(0, 0, 0, 0, r.Chance(50) ? 0x0c : 0x0d) | (r.Below(0x1000) << 6));
        else
            Simple1();
    }

    void Branch()
    {
        const u32 k = r.Below(12);
        const u32 skip = r.Below(4);
        if (k < 8)
        {
            const u32 rs = Any();
            const u32 rt = r.Chance(15) ? rs : (r.Chance(15) ? 0 : Any());
            u32 word;
            switch (k)
            {
                case 0: word = MipsI(4, rs, rt, skip + 1); break;
                case 1: word = MipsI(5, rs, rt, skip + 1); break;
                case 2: word = MipsI(6, rs, 0, skip + 1); break;
                case 3: word = MipsI(7, rs, 0, skip + 1); break;
                case 4: word = MipsI(1, rs, 0, skip + 1); break;   // bltz
                case 5: word = MipsI(1, rs, 1, skip + 1); break;   // bgez
                case 6: word = MipsI(1, rs, 16, skip + 1); break;  // bltzal
                default: word = MipsI(1, rs, 17, skip + 1); break; // bgezal
            }
            if (r.Chance(30)) // bias operands so both outcomes are exercised
                LoadConst(rs, r.Chance(50) ? 0 : Val());
            c.push_back(word);
            DelaySlot();
            for (u32 i = 0; i < skip; ++i) Simple1();
            return;
        }
        if (k < 10) // J / JAL to the landing instruction
        {
            const size_t at = c.size();
            c.push_back(0);
            DelaySlot();
            for (u32 i = 0; i < skip; ++i) Simple1();
            const u32 target = IOP_TEST_PC + static_cast<u32>(c.size()) * 4;
            c[at] = MipsJ(k == 8 ? 2 : 3, target);
            return;
        }
        // JR / JALR through a register holding an aliased absolute address.
        const u32 reg = Hot();
        const size_t lui = c.size();
        c.push_back(0);
        c.push_back(0);
        const u32 rd = k == 10 ? 0 : (r.Chance(70) ? 31 : Hot());
        c.push_back(k == 10 ? MipsR(reg, 0, 0, 0, 8) : MipsR(reg, 0, rd, 0, 9));
        const size_t slotAt = c.size();
        DelaySlot();
        // Upstream x86 quirk (GAP_KNOWN=1 includes it): psxTrySwapDelaySlot swaps MULT/MULTU/DIV/DIVU/MTC0/CTC0/MTC2/CTC2/LWC2/SWC2/GTE
        // slots without checking their sources against the JALR link register, so they read the stale rd. The interpreter reads the new
        // link. The arm64 port copies that logic (jit == x86); keep the random slot from reading the link in those forms.
        if (k == 11 && rd != 0 && rd != reg && c.size() == slotAt + 1 && GapKnob("GAP_KNOWN", 0) == 0)
        {
            const u32 w = c.back(), op = w >> 26, f = w & 0x3f, wrs = (w >> 21) & 31, wrt = (w >> 16) & 31;
            const bool unchecked = (op == 0 && f >= 0x18 && f <= 0x1b) || op == 0x10 || op == 0x11 || op == 0x12 || op == 0x13 || op == 50 || op == 58;
            if (unchecked && (wrs == rd || wrt == rd)) c.back() = 0;
        }
        for (u32 i = 0; i < skip; ++i) Simple1();
        static const u32 alias[] = {0u, 0x80000000u, 0xa0000000u};
        const u32 target = alias[r.Below(3)] | (IOP_TEST_PC + static_cast<u32>(c.size()) * 4);
        c[lui] = MipsI(15, 0, reg, target >> 16);
        c[lui + 1] = MipsI(13, reg, reg, target & 0xffff);
        if (k == 11 && rd == reg) { /* link overwrites the target register after the jump: fine */ }
    }

    void Loop()
    {
        const u32 n = 2 + r.Below(4);
        c.push_back(MipsI(9, 0, 23, n));
        const size_t top = c.size();
        const u32 body = 1 + r.Below(5);
        size_t placeholder = ~size_t(0), patchAt = ~size_t(0);
        const u32 patchedWord = MipsI(9, 8, 8, 0x40 + r.Below(0x40)); // addiu r8, r8, imm
        const u32 placeholderWord = MipsI(9, 8, 8, 1 + r.Below(0x3f));
        const bool doSmc = smc && r.Chance(35);
        for (u32 i = 0; i < body; ++i)
        {
            if (doSmc && i == 0)
            {
                placeholder = c.size();
                c.push_back(placeholderWord);
            }
            else
                Simple();
        }
        if (doSmc)
        {
            // Patch the already executed placeholder: iterations 2..n must see the new instruction.
            const u32 hi = 0x0010 | (r.Chance(30) ? 0x8000u : 0u);
            c.push_back(MipsI(15, 0, 28, hi));
            c.push_back(MipsI(15, 0, 27, patchedWord >> 16));
            c.push_back(MipsI(13, 27, 27, patchedWord & 0xffff));
            patchAt = c.size();
            c.push_back(MipsI(43, 28, 27, static_cast<u32>(placeholder) * 4));
            (void)patchAt;
        }
        c.push_back(MipsI(9, 23, 23, 0xffff));
        c.push_back(MipsI(5, 23, 0, static_cast<u32>(static_cast<s32>(top) - static_cast<s32>(c.size() + 1))));
        Simple1(true, false);
    }

    void Smc()
    {
        // Store a new instruction over a later slot of the same / next block, then run it.
        const u32 mode = sameBlock ? r.Below(5) : 3 + r.Below(2);
        const u32 hi = 0x0010 | (r.Chance(30) ? (r.Chance(50) ? 0x8000u : 0xa000u) : 0u);
        const u32 oldWord = MipsI(9, 9, 9, 1 + r.Below(0x3f));   // addiu r9, r9, imm
        const u32 newWord = MipsI(9, 9, 9, 0x100 + r.Below(0x300));
        const u32 sKind = r.Below(6); // 0 sw, 1 sh imm, 2 sb, 3 swl, 4 swr, 5 sw of a different opcode
        c.push_back(MipsI(15, 0, 28, hi));
        u32 stored = newWord;
        if (sKind == 5) stored = MipsR(9, 10, 9, 0, 0x21) ; // addu r9, r9, r10
        c.push_back(MipsI(15, 0, 27, stored >> 16));
        c.push_back(MipsI(13, 27, 27, stored & 0xffff));
        const size_t store = c.size();
        c.push_back(0);
        size_t slot = ~size_t(0);
        if (mode == 0) // immediately next instruction
        {
            slot = c.size();
            c.push_back(oldWord);
        }
        else if (mode == 1) // a few instructions later, same block
        {
            const u32 d = 1 + r.Below(4);
            for (u32 i = 0; i < d; ++i) Simple1(false);
            slot = c.size();
            c.push_back(oldWord);
        }
        else if (mode == 2) // the slot is the delay slot of the next branch
        {
            c.push_back(MipsI(r.Chance(50) ? 4 : 5, Hot(), r.Chance(50) ? 0 : Hot(), 2));
            slot = c.size();
            c.push_back(oldWord);
            Simple1(false);
        }
        else if (mode == 3) // linked successor: slot sits at a taken-branch target
        {
            c.push_back(MipsI(4, 0, 0, 2)); // beq r0,r0 -> skips one instruction after the delay slot
            Simple1(false);
            Simple1(false);
            slot = c.size();
            c.push_back(oldWord);
        }
        else // jump through a register into the slot
        {
            const u32 reg = Hot();
            const size_t lui = c.size();
            c.push_back(0); c.push_back(0);
            c.push_back(MipsR(reg, 0, 0, 0, 8));
            Simple1(false);
            slot = c.size();
            c.push_back(oldWord);
            const u32 target = (IOP_TEST_PC + static_cast<u32>(slot) * 4) | ((hi & 0x8000) ? 0x80000000u : 0u);
            c[lui] = MipsI(15, 0, reg, target >> 16);
            c[lui + 1] = MipsI(13, reg, reg, target & 0xffff);
        }
        // Fix the store now that the slot index is known.
        s32 off = static_cast<s32>(slot) * 4;
        switch (sKind)
        {
            case 1: c[store] = MipsI(41, 28, 27, static_cast<u32>(off)); stored = 0; break;
            case 2: c[store] = MipsI(40, 28, 27, static_cast<u32>(off)); break;
            case 3: c[store] = MipsI(42, 28, 27, static_cast<u32>(off + 3)); break;
            case 4: c[store] = MipsI(46, 28, 27, static_cast<u32>(off)); break;
            default: c[store] = MipsI(43, 28, 27, static_cast<u32>(off)); break;
        }
        if (sKind == 1)
        {
            // sh: the low halfword of the stored value is the new immediate of the placeholder.
            c[store - 1] = MipsI(13, 27, 27, 0x100 + r.Below(0x300));
        }
    }

    void Prologue()
    {
        for (u32 reg = 8; reg < 20; ++reg)
            LoadConst(reg, Val());
        c.push_back(MipsI(15, 0, 22, kBase >> 16));
        c.push_back(MipsI(13, 22, 22, kBase & 0xffff));
    }

    void Generate(u32 constructs)
    {
        Prologue();
        for (u32 i = 0; i < constructs; ++i)
        {
            const u32 k = r.Below(100);
            const u32 smcEnd = 70 + (smc ? smcPct : 0);
            if (k < 45) Simple();
            else if (k < 62) Branch();
            else if (k < 70) Loop();
            else if (k < smcEnd) Smc();
            else if (k < smcEnd + 6 && exc) c.push_back(MipsR(0, 0, 0, 0, r.Chance(60) ? 0x0c : 0x0d) | (r.Below(0x1000) << 6));
            else Simple();
        }
        for (u32 i = 0; i < 6; ++i) c.push_back(0); // landing pad for forward branches
    }
};

// Directed SMC matrix: store kind x address alias x placeholder position. Each group names the store and alias.
void SmcMatrix(GapGroups& groups, bool sameBlock)
{
    static const char* const posNames[6] = {"next", "+3", "delay-slot(taken)", "delay-slot(not-taken)", "successor-block", "loop-reexec"};
    std::map<std::string, std::string> failing, sameBlockInfo;
    static const struct { u32 op; const char* name; } stores[] = {{40, "SB"}, {41, "SH"}, {43, "SW"}, {42, "SWL"}, {46, "SWR"}};
    static const struct { u32 hi; const char* name; } aliases[] = {{0x0010, "kuseg"}, {0x8010, "kseg0"}, {0xa010, "kseg1"}};
    for (const auto& st : stores)
        for (const auto& al : aliases)
            for (u32 pos = 0; pos < 6; ++pos)
                for (u32 low = 0; low < ((st.op == 42 || st.op == 46) ? 4u : 1u); ++low)
                {
                    const u32 oldWord = MipsI(9, 9, 9, 5), newWord = MipsI(9, 9, 9, 0x70);
                    std::vector<u32> c;
                    c.push_back(MipsI(15, 0, 8, 0x1234));
                    c.push_back(MipsI(15, 0, 28, al.hi));
                    c.push_back(MipsI(15, 0, 27, newWord >> 16));
                    c.push_back(MipsI(13, 27, 27, newWord & 0xffff));
                    size_t store = 0, slot = 0;
                    if (pos == 5)
                    {
                        c.push_back(MipsI(9, 0, 23, 2));
                        slot = c.size();
                        c.push_back(oldWord);
                        store = c.size();
                        c.push_back(0);
                        c.push_back(MipsI(9, 23, 23, 0xffff));
                        c.push_back(MipsI(5, 23, 0, static_cast<u32>(static_cast<s32>(slot) - static_cast<s32>(c.size() + 1))));
                        c.push_back(0);
                    }
                    else
                    {
                        store = c.size();
                        c.push_back(0);
                        if (pos == 0) { slot = c.size(); c.push_back(oldWord); }
                        else if (pos == 1) { for (int i = 0; i < 3; ++i) c.push_back(MipsI(9, 10, 10, 1)); slot = c.size(); c.push_back(oldWord); }
                        else if (pos == 2) { c.push_back(MipsI(4, 0, 0, 2)); slot = c.size(); c.push_back(oldWord); c.push_back(MipsI(9, 11, 11, 1)); }
                        else if (pos == 3) { c.push_back(MipsI(5, 0, 0, 2)); slot = c.size(); c.push_back(oldWord); c.push_back(MipsI(9, 11, 11, 1)); }
                        else { c.push_back(MipsI(4, 0, 0, 2)); c.push_back(MipsI(9, 11, 11, 1)); c.push_back(MipsI(9, 11, 11, 1)); slot = c.size(); c.push_back(oldWord); }
                    }
                    c[store] = MipsI(st.op, 28, 27, static_cast<u32>(slot * 4 + low));
                    for (int i = 0; i < 6; ++i) c.push_back(0);
                    const Result e = Run(false, c), a = Run(true, c);
                    ++groups.cases;
                    std::string detail;
                    const char* f = Diff(e, a, detail);
                    if (f && pos < 4 && !sameBlock) { sameBlockInfo[std::string(st.name) + "/" + al.name] += std::string(posNames[pos]) + " "; }
                    else if (f)
                    {
                        char key[96], ex[400];
                        std::snprintf(key, sizeof(key), "smc-matrix store=%s alias=%s", st.name, al.name);
                        std::snprintf(ex, sizeof(ex), "%s pos=%s lowbits=%u: %s [%s] code@0x100000: ", key, posNames[pos], low, f, detail.c_str());
                        groups.Record(key, c.size(), std::string(ex) + GapSparseWords(c));
                        failing[std::string(st.name) + "/" + al.name] += std::string(posNames[pos]) + (low ? "+" + std::to_string(low) : std::string()) + " ";
                    }
                }
    for (const auto& kv : failing)
        std::printf("GAP smc-matrix FAILING %s: %s\n", kv.first.c_str(), kv.second.c_str());
    for (const auto& kv : sameBlockInfo)
        std::printf("GAP smc-matrix INFO same-block patch not observed (R3000A-unspecified, see GAP_IOP_SAMEBLOCK) %s: %s\n", kv.first.c_str(), kv.second.c_str());
}

// SYSCALL/BREAK in the delay slot of every branch kind. The IOP interpreter's doBranch() overwrites the exception vector with the
// branch target when the branch is taken (R3000AInterpreter.cpp), so taken/unconditional branches differ from the recompiler (and from
// hardware, which takes the exception with EPC=branch). Reported as INFO unless GAP_IOP_BDEXC=1.
void BdExcMatrix(GapGroups& groups, bool bdExc)
{
    static const struct { const char* name; u32 kind; u32 r8; } cases[] = {
        {"BEQ taken", 0, 0}, {"BEQ not-taken", 1, 1}, {"BNE taken", 2, 1}, {"BLTZ taken", 3, 0xffffffffu}, {"BGEZ not-taken", 4, 0xffffffffu},
        {"BLTZAL taken", 5, 0xffffffffu}, {"J", 6, 0}, {"JAL", 7, 0}, {"JR", 8, 0}, {"JALR", 9, 0}};
    std::string info;
    for (u32 brk = 0; brk < 2; ++brk)
        for (const auto& cs : cases)
        {
            std::vector<u32> c;
            c.push_back(MipsI(15, 0, 8, cs.r8 >> 16));
            c.push_back(MipsI(13, 8, 8, cs.r8 & 0xffff));
            c.push_back(MipsI(15, 0, 22, kBase >> 16));
            c.push_back(MipsI(13, 22, 22, kBase & 0xffff));
            const u32 target = IOP_TEST_PC + (static_cast<u32>(c.size()) + 1 + 1 + 2) * 4 + (cs.kind >= 8 ? 8 : 0);
            u32 br = 0;
            switch (cs.kind)
            {
                case 0: br = MipsI(4, 0, 0, 3); break;
                case 1: br = MipsI(4, 8, 0, 3); break;
                case 2: br = MipsI(5, 8, 0, 3); break;
                case 3: br = MipsI(1, 8, 0, 3); break;
                case 4: br = MipsI(1, 8, 1, 3); break;
                case 5: br = MipsI(1, 8, 16, 3); break;
                case 6: br = MipsJ(2, target); break;
                case 7: br = MipsJ(3, target); break;
                default: break;
            }
            if (cs.kind >= 8)
            {
                c.push_back(MipsI(15, 0, 12, target >> 16));
                c.push_back(MipsI(13, 12, 12, target & 0xffff));
                br = cs.kind == 8 ? MipsR(12, 0, 0, 0, 8) : MipsR(12, 0, 31, 0, 9);
            }
            c.push_back(br);
            c.push_back(MipsR(0, 0, 0, 0, brk ? 0x0d : 0x0c) | (0x55 << 6));
            c.push_back(MipsI(9, 9, 9, 1));
            c.push_back(MipsI(9, 9, 9, 2));
            c.push_back(MipsI(9, 9, 9, 4));
            for (int i = 0; i < 6; ++i) c.push_back(0);
            const Result e = Run(false, c), a = Run(true, c);
            ++groups.cases;
            std::string detail;
            const char* f = Diff(e, a, detail);
            if (!f)
                continue;
            if (bdExc)
            {
                char key[96];
                std::snprintf(key, sizeof(key), "bd-exception %s in delay slot", cs.name);
                groups.Record(key, c.size(), std::string(key) + ": " + f + " [" + detail + "] code@0x100000: " + GapSparseWords(c));
            }
            else
                info += std::string(cs.name) + (brk ? "/BREAK " : "/SYSCALL ") + f + " [" + detail + "]; ";
        }
    if (!info.empty())
        std::printf("GAP bd-exception INFO (interpreter drops the exception of a taken/unconditional branch delay slot): %s\n", info.c_str());
    ++groups.cases;
}

// The link register written by JAL/JALR/BLTZAL/BGEZAL must already be visible to the delay slot instruction (interpreter and x86 JIT
// write it before executing the slot). Delay slot = every kind of reader of the link register.
void LinkDelayMatrix(GapGroups& groups)
{
    static const struct { const char* name; u32 kind; u32 rd; u32 r8; } cases[] = {
        {"JAL r31", 0, 31, 0}, {"JALR rd=r11 rs=r13", 1, 11, 0}, {"JALR rd=r31", 1, 31, 0}, {"JALR rd=rs=r13", 2, 13, 0},
        {"BLTZAL taken", 3, 31, 0xffffffffu}, {"BLTZAL not-taken", 3, 31, 1}, {"BGEZAL taken", 4, 31, 1}, {"BGEZAL not-taken", 4, 31, 0xffffffffu},
        {"JR (rd=r14 const reader, no link)", 5, 14, 0}, {"J (rd=r14 const reader, no link)", 6, 14, 0}};
    static const char* const slotNames[] = {"addu r9,rd,r0", "sw rd,0(r22)", "mult rd,rd", "mtlo rd", "sltu r9,rd,r13", "sll r9,rd,4", "lw r9,0(rd)", "mtc0 rd,$3", "mtc2 rd,$0", "ctc2 rd,$0", "lwc2 $1,0(rd)", "divu rd,rd", "multu rd,r14"};
    for (const auto& cs : cases)
        for (u32 slotKind = 0; slotKind < 13; ++slotKind)
        {
            std::vector<u32> c;
            c.push_back(MipsI(15, 0, 8, cs.r8 >> 16));
            c.push_back(MipsI(13, 8, 8, cs.r8 & 0xffff));
            c.push_back(MipsI(15, 0, 22, kBase >> 16));
            c.push_back(MipsI(13, 22, 22, kBase & 0xffff));
            c.push_back(MipsI(15, 0, 14, 0x1234));
            c.push_back(MipsI(13, 14, 14, 0x5678));
            const size_t jl = c.size();
            c.push_back(0);
            c.push_back(0);
            const u32 rd = cs.rd;
            const u32 br = cs.kind == 0 || cs.kind == 6 ? 0u : cs.kind == 5 ? MipsR(13, 0, 0, 0, 8) : cs.kind == 1 ? MipsR(13, 0, rd, 0, 9) : cs.kind == 2 ? MipsR(13, 0, 13, 0, 9)
                : cs.kind == 3 ? MipsI(1, 8, 16, 3) : MipsI(1, 8, 17, 3);
            const size_t at = c.size();
            c.push_back(br);
            switch (slotKind)
            {
                case 0: c.push_back(MipsR(rd, 0, 9, 0, 0x21)); break;
                case 1: c.push_back(MipsI(43, 22, rd, 0x10)); break;
                case 2: c.push_back(MipsR(rd, rd, 0, 0, 0x18)); break;
                case 3: c.push_back(MipsR(rd, 0, 0, 0, 0x13)); break;
                case 4: c.push_back(MipsR(rd, 13, 9, 0, 0x2b)); break;
                case 5: c.push_back(MipsR(0, rd, 9, 4, 0x00)); break;
                case 6: c.push_back(MipsI(35, rd, 9, 0)); break;
                case 7: c.push_back(MipsI(0x10, 4, rd, 3u << 11)); break;
                case 8: c.push_back(MipsI(0x12, 4, rd, 0)); break;
                case 9: c.push_back(MipsI(0x12, 6, rd, 0)); break;
                case 10: c.push_back(MipsI(50, rd, 1, 0)); break;
                case 11: c.push_back(MipsR(rd, rd, 0, 0, 0x1b)); break;
                default: c.push_back(MipsR(rd, 14, 0, 0, 0x19)); break;
            }
            c.push_back(MipsI(9, 10, 10, 1));
            c.push_back(MipsI(9, 10, 10, 2));
            c.push_back(MipsI(9, 10, 10, 4));
            for (int i = 0; i < 6; ++i) c.push_back(0);
            const u32 landing = IOP_TEST_PC + static_cast<u32>(at + 4) * 4;
            if (cs.kind == 0) c[at] = MipsJ(3, landing);
            if (cs.kind == 6) c[at] = MipsJ(2, landing);
            if (cs.kind == 1 || cs.kind == 2 || cs.kind == 5)
            {
                c[jl] = MipsI(15, 0, 13, landing >> 16);
                c[jl + 1] = MipsI(13, 13, 13, landing & 0xffff);
            }
            // Upstream x86 quirk (GAP_KNOWN=1 includes it): JALR (rd != rs) swaps a whitelisted delay slot before the link write, and
            // psxTrySwapDelaySlot does not check MULT/MULTU/DIV/DIVU/MTC0/MTC2/CTC2/LWC2 sources against rd, so those slots read the stale
            // register; the interpreter writes the link first. The arm64 port copies that logic, so jit == x86 here.
            if (cs.kind == 1 && (slotKind == 2 || slotKind >= 7) && GapKnob("GAP_KNOWN", 0) == 0)
                continue;
            const Result e = Run(false, c), a = Run(true, c);
            ++groups.cases;
            std::string detail;
            if (const char* f = Diff(e, a, detail))
            {
                char key[128];
                std::snprintf(key, sizeof(key), "link-in-delay-slot %s", cs.name);
                groups.Record(key, c.size(), std::string(key) + " slot=[" + slotNames[slotKind] + "]: " + f + " [" + detail + "] code@0x100000: " + GapSparseWords(c));
                std::printf("GAP link-in-delay-slot FAIL %s slot=[%s]: %s [%s]\n", cs.name, slotNames[slotKind], f, detail.c_str());
            }
        }
}
} // namespace GapIop

void GapIopRandBlockTests()
{
    using namespace GapIop;
    EmuCoreXOracleSetSkipEvents(1);
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.WaitLoop = false;
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate emulator memory");
        return;
    }
    SysMemory::Reset();
    psxCpu = &psxRec;
    psxCpu->Reserve();

    const u64 seed = GapKnob("GAP_IOP_SEED", GapKnob("GAP_SEED", 1));
    const u64 count = GapKnob("GAP_IOP_COUNT", GapKnob("GAP_COUNT", 20000));
    const u32 maxLen = static_cast<u32>(GapKnob("GAP_IOP_MAXLEN", 90));
    const bool isc = GapKnob("GAP_IOP_ISC", 0) != 0;
    const bool bdExc = GapKnob("GAP_IOP_BDEXC", 0) != 0;
    const bool sameBlock = GapKnob("GAP_IOP_SAMEBLOCK", 0) != 0; // same-block SMC is architecturally unspecified (R3000A prefetch); info-only by default
    const bool verbose = GapKnob("GAP_VERBOSE", 0) != 0;
    const int familyOnly = static_cast<int>(GapKnob("GAP_IOP_FAMILY", 99)) > 3 ? -1 : static_cast<int>(GapKnob("GAP_IOP_FAMILY", 99));
    GapGroups groups;
    const auto t0 = std::chrono::steady_clock::now();
    std::printf("GAP iop-rand-block seed=%llu count=%llu maxlen=%u\n", static_cast<unsigned long long>(seed), static_cast<unsigned long long>(count), maxLen);
    for (u64 k = 0; k < count; ++k)
    {
        GapRng rng(seed * 1000003ull + k);
        // Sub-families keep SMC/exception-heavy programs separate so a group is attributable.
        const u32 family = familyOnly >= 0 ? static_cast<u32>(familyOnly) : static_cast<u32>(k % 4); // 0 mixed, 1 smc-heavy, 2 exception-heavy, 3 alu/mem/branch only
        std::vector<u32> code;
        Gen g{rng, code};
        g.smc = family != 3 && family != 2;
        g.exc = family == 2 || family == 0;
        g.cop0 = family != 3;
        g.isc = isc;
        g.sameBlock = sameBlock;
        g.bdExc = bdExc;
        g.smcPct = family == 1 ? 30 : 8;
        g.Generate(8 + rng.Below(maxLen));
        if (verbose) { std::printf("IOPR k=%llu len=%zu code: %s\n", static_cast<unsigned long long>(k), code.size(), GapHexWords(code, 400).c_str()); std::fflush(stdout); }
        const Result expected = Run(false, code);
        if (verbose) std::printf("IOPR interp done\n");
        const Result actual = Run(true, code);
        ++groups.cases;
        std::string detail;
        const char* field = Diff(expected, actual, detail);
        if (!field)
            continue;
        const auto fails = [&](const std::vector<u32>& candidate) {
            std::string d;
            const Result e = Run(false, candidate), a = Run(true, candidate);
            const char* df = Diff(e, a, d);
            return df && std::strcmp(df, field) == 0;
        };
        std::vector<u32> minimal = GapMinimize(code, fails, 1);
        std::string d2;
        const Result e2 = Run(false, minimal), a2 = Run(true, minimal);
        const char* f2 = Diff(e2, a2, d2);
        char head[160];
        std::snprintf(head, sizeof(head), "seed=%llu k=%llu family=%u %s: %s | minimized(%s): ", static_cast<unsigned long long>(seed),
            static_cast<unsigned long long>(k), family, field, detail.c_str(), f2 ? f2 : "?");
        size_t nonzero = 0;
        for (u32 w : minimal) nonzero += w != 0;
        groups.Record(std::string(field) + (family == 1 ? " (smc-heavy)" : family == 2 ? " (exception-heavy)" : ""), nonzero,
            std::string(head) + d2 + " code@0x100000 (idx:word, rest nop): " + GapSparseWords(minimal));
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("GAP iop-rand-block runtime=%.1fs\n", secs);
    SmcMatrix(groups, sameBlock);
    BdExcMatrix(groups, bdExc);
    LinkDelayMatrix(groups);
    groups.Report("iop-rand-block");
    psxCpu = &psxInt;
}
