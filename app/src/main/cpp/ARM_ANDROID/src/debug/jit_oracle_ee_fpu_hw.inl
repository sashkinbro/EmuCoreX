// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// EE FPU guard-bit ADD/SUB hardware vectors (ported from ARMSX2's
// ee_fpu_guarded_addsub_console_tests.cpp, GPL-3). Asserts the console value
// directly on both the JIT and the interpreter, plus exponent-difference
// boundaries against an integer-masked exact model, the guard-off switch, and
// compare/CFC1/BC1 state surviving an interposed guarded add.
// Included from jit_oracle.cpp's anonymous namespace.
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)
#include <cmath>

namespace
{
enum FpuHwForm { HW_ADD, HW_SUB, HW_ADDA, HW_SUBA, HW_MADD, HW_MSUB, HW_MADDA, HW_MSUBA };

constexpr const char* kFpuHwNames[] = {"ADD", "SUB", "ADDA", "SUBA", "MADD", "MSUB", "MADDA", "MSUBA"};

struct FpuHwCase
{
    int ordinal;
    FpuHwForm form;
    u32 fs, ft, acc, want, unguarded;
};

constexpr FpuHwCase kFpuHwRows[] = {
    {101, HW_ADD,   0xDEADBEEFu, 0x3F800000u, 0u,          0xDEADBEEFu, 0xDEADBEEEu},
    {116, HW_SUB,   0x3EAAAAABu, 0x40400000u, 0u,          0xC02AAAABu, 0xC02AAAAAu},
    {124, HW_SUB,   0x00800000u, 0x3F000000u, 0u,          0xBF000000u, 0xBEFFFFFFu},
    {125, HW_SUB,   0x00800001u, 0x3F000000u, 0u,          0xBF000000u, 0xBEFFFFFFu},
    {134, HW_SUB,   0x7F000000u, 0x40800000u, 0u,          0x7F000000u, 0x7EFFFFFFu},
    {141, HW_SUB,   0x4F000000u, 0x3F800000u, 0u,          0x4F000000u, 0x4EFFFFFFu},
    {490, HW_ADDA,  0xDEADBEEFu, 0x3F800000u, 0u,          0xDEADBEEFu, 0xDEADBEEEu},
    {496, HW_SUBA,  0x00800000u, 0x3F000000u, 0u,          0xBF000000u, 0xBEFFFFFFu},
    {501, HW_SUBA,  0x3EAAAAABu, 0x40400000u, 0u,          0xC02AAAABu, 0xC02AAAAAu},
    {539, HW_MSUB,  0x3EAAAAABu, 0x40400000u, 0x3EAAAAABu, 0xBF2AAAABu, 0xBF2AAAAAu},
    {567, HW_MSUBA, 0x3EAAAAABu, 0x40400000u, 0x3EAAAAABu, 0xBF2AAAABu, 0xBF2AAAAAu},
    {1078, HW_MSUB, 0x40400000u, 0x3EAAAAABu, 0x3EAAAAABu, 0xBF2AAAABu, 0xBF2AAAAAu},
    {1130, HW_MSUBA, 0x40400000u, 0x3EAAAAABu, 0x3EAAAAABu, 0xBF2AAAABu, 0xBF2AAAAAu},
};

// COP1.S function codes
constexpr u32 FpuHwFn(FpuHwForm f)
{
    constexpr u32 fn[] = {0x00, 0x01, 0x18, 0x19, 0x1c, 0x1d, 0x1e, 0x1f};
    return fn[f];
}

// Integer-masked exact model: the masked operands sum exactly in a double,
// then round once toward zero with denormal results flushed to signed zero.
u32 FpuHwModel(u32 a, u32 b, bool issub)
{
    const int d = static_cast<int>((a >> 23) & 0xff) - static_cast<int>((b >> 23) & 0xff);
    if (d >= 25)
        b &= 0x80000000u;
    else if (d >= 2)
        b &= (0xffffffffu << (d - 1));
    else if (d <= -25)
        a &= 0x80000000u;
    else if (d <= -2)
        a &= (0xffffffffu << (-d - 1));
    if (issub)
        b ^= 0x80000000u;
    float fa, fb;
    std::memcpy(&fa, &a, 4);
    std::memcpy(&fb, &b, 4);
    const double sum = static_cast<double>(fa) + static_cast<double>(fb);
    float r = static_cast<float>(sum);
    if (std::fabs(static_cast<double>(r)) > std::fabs(sum))
        r = std::nextafterf(r, 0.0f);
    u32 bits;
    std::memcpy(&bits, &r, 4);
    if ((bits & 0x7f800000u) == 0)
        bits &= 0x80000000u;
    return bits;
}

// Runs one form: f1=fs, f2=ft, result in f3 (or ACC). MADD/MSUB forms seed ACC
// first through ADDA.S ACC = f4 + f5 (f5 = +0.0).
EESnapshot RunFpuHwCase(bool jit, const FpuHwCase& c)
{
    u32 fpr[32]{};
    fpr[1] = c.fs;
    fpr[2] = c.ft;
    fpr[4] = c.acc;
    std::vector<u32> code;
    if (c.form >= HW_MADD)
        code.push_back(MipsCop1(16, 5, 4, 0, 0x18));
    code.push_back(MipsCop1(16, 2, 1, 3, FpuHwFn(c.form)));
    return RunEEProgram(jit, code, false, nullptr, nullptr, nullptr, fpr);
}

u32 FpuHwResult(const EESnapshot& s, FpuHwForm f)
{
    const bool to_acc = f == HW_ADDA || f == HW_SUBA || f == HW_MADDA || f == HW_MSUBA;
    return to_acc ? s.acc : s.fpr[3];
}

void FpuHwCheckRows()
{
    for (const FpuHwCase& c : kFpuHwRows)
    {
        for (int on = 1; on >= 0; --on)
        {
            EmuConfig.Cpu.Recompiler.fpuGuardedAddSub = on != 0;
            const u32 want = on ? c.want : c.unguarded;
            const EESnapshot interp = RunFpuHwCase(false, c);
            const EESnapshot jit = RunFpuHwCase(true, c);
            char name[160];
            std::snprintf(name, sizeof(name), "ee-fpu-hw #%d %s guard=%d interpreter want=%08x got=%08x",
                c.ordinal, kFpuHwNames[c.form], on, want, FpuHwResult(interp, c.form));
            Check(FpuHwResult(interp, c.form) == want, name);
            std::snprintf(name, sizeof(name), "ee-fpu-hw #%d %s guard=%d JIT want=%08x got=%08x",
                c.ordinal, kFpuHwNames[c.form], on, want, FpuHwResult(jit, c.form));
            Check(FpuHwResult(jit, c.form) == want, name);
            std::snprintf(name, sizeof(name), "ee-fpu-hw #%d %s guard=%d interpreter/JIT state", c.ordinal,
                kFpuHwNames[c.form], on);
            CompareEE(interp, jit, name);
        }
    }
    EmuConfig.Cpu.Recompiler.fpuGuardedAddSub = true;
}

void FpuHwCheckBoundaries()
{
    struct Boundary { u32 a, b; bool sub; const char* what; };
    const Boundary cases[] = {
        {0x3F800001u, 0x3F000001u, false, "d=1 add (no mask)"},
        {0x3F800001u, 0x3F000001u, true,  "d=1 sub (no mask)"},
        {0x3F800001u, 0x3E800001u, true,  "d=2 sub"},
        {0x3F800001u, 0x3E800001u, false, "d=2 add"},
        {0x3F800001u, 0x33FFFFFFu, true,  "d=24 sub"},
        {0x3F800001u, 0x33FFFFFFu, false, "d=24 add"},
        {0x3F800001u, 0x33000001u, true,  "d=25 sub (sign only)"},
        {0x3F800001u, 0x33000001u, false, "d=25 add (sign only)"},
        {0x3E800001u, 0x3F800001u, true,  "d=-2 sub"},
        {0x3E800001u, 0x3F800001u, false, "d=-2 add"},
        {0x33000001u, 0x3F800001u, true,  "d=-25 sub (sign only)"},
        {0x33000001u, 0x3F800001u, false, "d=-25 add (sign only)"},
        {0x3F800000u, 0x3F7FFFFFu, true,  "d=1 cancel"},
        {0x00800001u, 0x00800000u, true,  "flush-to-sign cancel"},
        {0xBF800001u, 0x3E800001u, false, "d=2 negative a"},
        {0x3F800001u, 0xBE800001u, true,  "d=2 negative b"},
    };
    for (const Boundary& b : cases)
    {
        u32 fpr[32]{};
        fpr[1] = b.a;
        fpr[2] = b.b;
        const std::vector<u32> code = {MipsCop1(16, 2, 1, 3, b.sub ? 0x01 : 0x00)};
        const EESnapshot interp = RunEEProgram(false, code, false, nullptr, nullptr, nullptr, fpr);
        const EESnapshot jit = RunEEProgram(true, code, false, nullptr, nullptr, nullptr, fpr);
        const u32 want = FpuHwModel(b.a, b.b, b.sub);
        char name[160];
        std::snprintf(name, sizeof(name), "ee-fpu-hw boundary %s interpreter want=%08x got=%08x", b.what, want, interp.fpr[3]);
        Check(interp.fpr[3] == want, name);
        std::snprintf(name, sizeof(name), "ee-fpu-hw boundary %s JIT want=%08x got=%08x", b.what, want, jit.fpr[3]);
        Check(jit.fpr[3] == want, name);
        std::snprintf(name, sizeof(name), "ee-fpu-hw boundary %s interpreter/JIT state", b.what);
        CompareEE(interp, jit, name);
    }
}

// A resident FCR31 / cached GPRs must survive the guard sequence's scratch use
// (the ARMSX2 Shadow of the Colossus bug): C.LT sets the C flag, guarded
// ADD/SUB/MADD on the slow path (|d| >= 2) run, then CFC1 / BC1T read the flag.
void FpuHwCheckCompareSurvives()
{
    constexpr u32 kFlagC = 0x00800000u;
    for (u32 variant = 0; variant < 2; ++variant)
    {
        u32 fpr[32]{};
        fpr[3] = 0x3F800000u; // 1.0
        fpr[4] = 0x40000000u; // 2.0 -> C.LT.S f3, f4 is true
        fpr[5] = 0x3F800001u;
        fpr[6] = 0x33000001u; // d = 25 for the guarded ops
        u64 gpr[32]{};
        for (u32 r = 8; r < 20; ++r)
            gpr[r] = 0x1111u * r; // cached GPRs that must be intact afterwards
        std::vector<u32> code = {
            MipsCop1(16, 4, 3, 0, 0x34),  // C.LT.S f3, f4
            MipsCop1(16, 6, 5, 7, 0x00),  // ADD.S f7 = f5 + f6 (guard slow path)
            MipsCop1(16, 6, 5, 8, 0x01),  // SUB.S f8 = f5 - f6
            MipsCop1(16, 6, 5, 9, 0x1c),  // MADD.S f9 = ACC + f5*f6
        };
        bool multi = false;
        if (variant == 0)
        {
            code.push_back(MipsCop1(2, 21, 31, 0, 0)); // CFC1 r21, fcr31
        }
        else
        {
            multi = true;
            code.push_back(MipsI(0x11, 8, 1, 2));      // BC1T +2 (skip one after the delay slot)
            code.push_back(MipsI(9, 0, 22, 1));        // delay slot: r22 = 1
            code.push_back(MipsI(9, 0, 23, 2));        // skipped when the flag survived
            code.push_back(MipsI(9, 0, 24, 3));        // target
        }
        const EESnapshot interp = RunEEProgram(false, code, multi, gpr, nullptr, nullptr, fpr);
        const EESnapshot jit = RunEEProgram(true, code, multi, gpr, nullptr, nullptr, fpr);
        char name[128];
        std::snprintf(name, sizeof(name), "ee-fpu-hw compare survives interposed guarded add (%s)", variant ? "BC1T" : "CFC1");
        CompareEE(interp, jit, name);
        if (variant == 0)
            Check((jit.gpr[21] & kFlagC) != 0, "ee-fpu-hw CFC1 after guarded add still sees C flag (JIT)");
        else
            Check(jit.gpr[23] == 0 && jit.gpr[22] == 1 && jit.gpr[24] == 3, "ee-fpu-hw BC1T after guarded add still taken (JIT)");
        for (u32 r = 8; r < 20; ++r)
        {
            if (variant == 1 && r >= 22)
                continue;
            if (jit.gpr[r] != gpr[r])
            {
                std::snprintf(name, sizeof(name), "ee-fpu-hw cached GPR r%u clobbered by guarded add", r);
                Check(false, name);
            }
        }
    }
}

void EeFpuHwTests()
{
    const auto saved = EmuConfig.Cpu.Recompiler;
    // The oracle thread runs at the host default FPCR (nearest, no FZ); pin the
    // EE's production chop-toward-zero + FZ for both engines, as the VM thread does.
    const FPControlRegisterBackup eeFpcr(EmuConfig.Cpu.FPUFPCR);
    EmuConfig.Cpu.Recompiler.fpuGuardedAddSub = true;
    FpuHwCheckRows();
    FpuHwCheckBoundaries();
    FpuHwCheckCompareSurvives();
    EmuConfig.Cpu.Recompiler = saved;
}
} // namespace
#endif
