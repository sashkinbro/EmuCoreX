//
// Created by k2154 on 2025-07-30.
//

#ifndef PCSX2_CPUREGISTERSPACK_H
#define PCSX2_CPUREGISTERSPACK_H

#include "Config.h"
#include "R5900.h"
#include "R3000A.h"
#include "VU.h"
#include "vtlb.h"
#include "ShuffleLanes.h"

struct Arm64FPUdGlobals
{
    u32 neg[4], pos[4];

    u32 pos_inf[4], neg_inf[4],
            one_exp[4];

    u64 dbl_one_exp[2];

    u64 dbl_cvt_overflow,
            dbl_ps2_overflow,
            dbl_underflow;

    u64 padding;

    u64 dbl_s_pos[2];
};

struct Arm64SseMasks
{
    u32 MIN_MAX_1[4], MIN_MAX_2[4], ADD_SS[4];
};

struct Arm64MvuSse4
{
    u32 sse4_minvals[2][4] = {
            {0xff7fffff, 0xffffffff, 0xffffffff, 0xffffffff},
            {0xff7fffff, 0xff7fffff, 0xff7fffff, 0xff7fffff},
    };
    u32 sse4_maxvals[2][4] = {
            {0x7f7fffff, 0x7fffffff, 0x7fffffff, 0x7fffffff},
            {0x7f7fffff, 0x7f7fffff, 0x7f7fffff, 0x7f7fffff},
    };
    u32 sse4_compvals[2][4] = {
            {0x7f7fffff, 0x7f7fffff, 0x7f7fffff, 0x7f7fffff},
            {0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff},
    };
    u32 s_neg[4] = {0x80000000, 0xffffffff, 0xffffffff, 0xffffffff};
    u32 s_pos[4] = {0x7fffffff, 0xffffffff, 0xffffffff, 0xffffffff};
    u32 g_minvals[4] = {0xff7fffff, 0xff7fffff, 0xff7fffff, 0xff7fffff};
    u32 g_maxvals[4] = {0x7f7fffff, 0x7f7fffff, 0x7f7fffff, 0x7f7fffff};
    u32 result[4] = {0x3f490fda};
    u32 minmax_mask[8] = {
            0xffffffff, 0x80000000, 0, 0,
            0, 0x40000000, 0, 0,
    };
    Arm64FPUdGlobals s_const = {
            {0x80000000, 0xffffffff, 0xffffffff, 0xffffffff},
            {0x7fffffff, 0xffffffff, 0xffffffff, 0xffffffff},
            {0x7f800000, 0, 0, 0},
            {0xff800000, 0, 0, 0},
            {0x00800000, 0, 0, 0},
            {0x0010000000000000ULL, 0},
            0x47f0000000000000ULL,
            0x4800000000000000ULL,
            0x3810000000000000ULL,
            0,
            {0x7fffffffffffffffULL, 0},
    };
    Arm64SseMasks sseMasks = {
            {0xffffffff, 0x80000000, 0xffffffff, 0x80000000},
            {0x00000000, 0x40000000, 0x00000000, 0x40000000},
            {0x80000000, 0xffffffff, 0xffffffff, 0xffffffff},
    };
};

struct Arm64MvuGlobals
{
    u32 absclip[4] = {0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff};
    u32 signbit[4] = {0x80000000, 0x80000000, 0x80000000, 0x80000000};
    u32 minvals[4] = {0xff7fffff, 0xff7fffff, 0xff7fffff, 0xff7fffff};
    u32 maxvals[4] = {0x7f7fffff, 0x7f7fffff, 0x7f7fffff, 0x7f7fffff};
    u32 exponent[4] = {0x7f800000, 0x7f800000, 0x7f800000, 0x7f800000};
    u32 one[4] = {0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000};
    u32 Pi4[4] = {0x3f490fdb, 0x3f490fdb, 0x3f490fdb, 0x3f490fdb};
    u32 T1[4] = {0x3f7ffff5, 0x3f7ffff5, 0x3f7ffff5, 0x3f7ffff5};
    u32 T5[4] = {0xbeaaa61c, 0xbeaaa61c, 0xbeaaa61c, 0xbeaaa61c};
    u32 T2[4] = {0x3e4c40a6, 0x3e4c40a6, 0x3e4c40a6, 0x3e4c40a6};
    u32 T3[4] = {0xbe0e6c63, 0xbe0e6c63, 0xbe0e6c63, 0xbe0e6c63};
    u32 T4[4] = {0x3dc577df, 0x3dc577df, 0x3dc577df, 0x3dc577df};
    u32 T6[4] = {0xbd6501c4, 0xbd6501c4, 0xbd6501c4, 0xbd6501c4};
    u32 T7[4] = {0x3cb31652, 0x3cb31652, 0x3cb31652, 0x3cb31652};
    u32 T8[4] = {0xbb84d7e7, 0xbb84d7e7, 0xbb84d7e7, 0xbb84d7e7};
    u32 S2[4] = {0xbe2aaaa4, 0xbe2aaaa4, 0xbe2aaaa4, 0xbe2aaaa4};
    u32 S3[4] = {0x3c08873e, 0x3c08873e, 0x3c08873e, 0x3c08873e};
    u32 S4[4] = {0xb94fb21f, 0xb94fb21f, 0xb94fb21f, 0xb94fb21f};
    u32 S5[4] = {0x362e9c14, 0x362e9c14, 0x362e9c14, 0x362e9c14};
    u32 E1[4] = {0x3e7fffa8, 0x3e7fffa8, 0x3e7fffa8, 0x3e7fffa8};
    u32 E2[4] = {0x3d0007f4, 0x3d0007f4, 0x3d0007f4, 0x3d0007f4};
    u32 E3[4] = {0x3b29d3ff, 0x3b29d3ff, 0x3b29d3ff, 0x3b29d3ff};
    u32 E4[4] = {0x3933e553, 0x3933e553, 0x3933e553, 0x3933e553};
    u32 E5[4] = {0x36b63510, 0x36b63510, 0x36b63510, 0x36b63510};
    u32 E6[4] = {0x353961ac, 0x353961ac, 0x353961ac, 0x353961ac};
    u64 EFU_ATAN_D[9] = {
            0x3feffffea0000000ULL, 0xbfd554c380000000ULL, 0x3fc98814c0000000ULL,
            0xbfc0bfcda0000000ULL, 0x3fb8aefbe0000000ULL, 0xbfaca03880000000ULL,
            0x3f9662ca40000000ULL, 0xbf709afce0000000ULL, 0x3fe921fb60000000ULL};
    u64 EFU_EXP_D[7] = {
            0x3ff0000000000000ULL, 0x3fcffff500000000ULL, 0x3fa000fe80000000ULL,
            0x3f653a7fe0000000ULL, 0x3f267caa60000000ULL, 0x3ed6c6a200000000ULL,
            0x3ea72c3580000000ULL};
    u64 EFU_SIN_D[5] = {
            0x3ff0000000000000ULL, 0xbfc5555480000000ULL, 0x3f8110e7c0000000ULL,
            0xbf29f643e0000000ULL, 0x3ec5d38280000000ULL};
    u32 I32MAXF[4] = {0x4effffff, 0x4effffff, 0x4effffff, 0x4effffff};
    float FTOI_4[4] = {16.0f, 16.0f, 16.0f, 16.0f};
    float FTOI_12[4] = {4096.0f, 4096.0f, 4096.0f, 4096.0f};
    float FTOI_15[4] = {32768.0f, 32768.0f, 32768.0f, 32768.0f};
    float ITOF_4[4] = {0.0625f, 0.0625f, 0.0625f, 0.0625f};
    float ITOF_12[4] = {0.000244140625f, 0.000244140625f, 0.000244140625f, 0.000244140625f};
    float ITOF_15[4] = {0.000030517578125f, 0.000030517578125f, 0.000030517578125f, 0.000030517578125f};
};

struct Arm64BackendConfig
{
    // Only keep the FP control mirrors that the ARM64 JIT currently reads.
    FPControlRegister FPUFPCR{};
    FPControlRegister FPUDivFPCR{};
    FPControlRegister VU0FPCR{};
    FPControlRegister VU1FPCR{};
};

struct Arm64BackendRuntime
{
    // ARM64-only helper/config state which must not be treated as canonical VM state.
    Arm64BackendConfig config{};
    alignas(16) ShuffleLanes shuffle;
    alignas(16) Arm64MvuSse4 mVUss4{};
    // Currently unused by the live ARM64 JIT path, but still backend-local.
    alignas(32) Arm64MvuGlobals mVUglob{};
};

struct Arm64CpuRuntimePack
{
    // Canonical VM state first. ARM64 helper/config state lives under runtime.
    alignas(16) cpuRegisters cpuRegs{};
    alignas(16) fpuRegisters fpuRegs{};
    alignas(16) psxRegisters psxRegs{};
    alignas(16) VURegs vuRegs[2]{};
    alignas(64) vtlb_private::MapData vtlbdata;
    Arm64BackendRuntime runtime{};
};
alignas(64) extern Arm64CpuRuntimePack g_cpuRegistersPack;

#endif //PCSX2_CPUREGISTERSPACK_H
