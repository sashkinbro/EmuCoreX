//
// Created by k2154 on 2025-07-30.
//

#ifndef PCSX2_VUDEF_H
#define PCSX2_VUDEF_H

#include "VifDef.h"

union VECTOR
{
    struct
    {
        float x, y, z, w;
    } f;
    struct
    {
        u32 x, y, z, w;
    } i;

    float F[4];

    u128 UQ;
    s128 SQ;
    u64 UD[2]; //128 bits
    s64 SD[2];
    u32 UL[4];
    s32 SL[4];
    u16 US[8];
    s16 SS[8];
    u8 UC[16];
    s8 SC[16];
};

struct REG_VI
{
    union
    {
        float F;
        s32 SL;
        u32 UL;
        s16 SS[2];
        u16 US[2];
        s8 SC[4];
        u8 UC[4];
    };
    u32 padding[3]; // needs padding to make them 128bit; VU0 maps VU1's VI regs as 128bits to addr 0x4xx0 in
    // VU0 mem, with only lower 16 bits valid, and the upper 112bits are hardwired to 0 (cottonvibes)
};

struct fdivPipe
{
    int enable;
    REG_VI reg;
    u32 sCycle;
    u32 Cycle;
    u32 statusflag;
};

struct efuPipe
{
    int enable;
    REG_VI reg;
    u32 sCycle;
    u32 Cycle;
};

struct fmacPipe
{
    u32 regupper;
    u32 reglower;
    int flagreg;
    u32 xyzwupper;
    u32 xyzwlower;
    u32 sCycle;
    u32 Cycle;
    u32 macflag;
    u32 statusflag;
    u32 clipflag;
};

struct ialuPipe
{
    int reg;
    u32 sCycle;
    u32 Cycle;
};

struct alignas(16) VURegs
{
    VECTOR VF[32]; // VF and VI need to be first in this struct for proper mapping
    REG_VI VI[32]; // needs to be 128bit x 32 (cottonvibes)

    VECTOR ACC;
    REG_VI q;
    REG_VI p;

    uint idx; // VU index (0 or 1)

    // flags/cycle are needed by VIF dma code, so they have to be here (for now)
    // We may replace these by accessors in the future, if merited.
    u32 cycle;
    u32 flags;

    // Current opcode being interpreted or recompiled (this var is used by Interps
    // but not microVU.  Would like to have it local to their respective classes... someday)
    u32 code;
    u32 start_pc;

    // branch/branchpc are used by interpreter only, but making them local to the interpreter
    // classes requires considerable code refactoring.  Maybe later. >_<
    u32 branch;
    u32 branchpc;
    u32 delaybranchpc;
    bool takedelaybranch;
    u32 ebit;
    u32 pending_q;
    u32 pending_p;

    alignas(16) u32 micro_macflags[4];
    alignas(16) u32 micro_clipflags[4];
    alignas(16) u32 micro_statusflags[4];
    // MAC/Status flags -- these are used by interpreters but are kind of hacky
    // and shouldn't be relied on for any useful/valid info.  Would like to move them out of
    // this struct eventually.
    u32 macflag;
    u32 statusflag;
    u32 clipflag;

    s32 nextBlockCycles;

    u8* Mem;
    u8* Micro;

    u32 xgkickaddr;
    u32 xgkickdiff;
    u32 xgkicksizeremaining;
    u32 xgkicklastcycle;
    u32 xgkickcyclecount;
    u32 xgkickenable;
    u32 xgkickendpacket;

    u8 VIBackupCycles;
    u32 VIOldValue;
    u32 VIRegNumber;

    fmacPipe fmac[4];
    u32 fmacreadpos;
    u32 fmacwritepos;
    u32 fmaccount;
    fdivPipe fdiv;
    efuPipe efu;
    ialuPipe ialu[4];
    u32 ialureadpos;
    u32 ialuwritepos;
    u32 ialucount;

    VURegs()
    {
        Mem = NULL;
        Micro = NULL;
    }

    bool IsVU1() const;
    bool IsVU0() const;

    VIFregisters& GetVifRegs() const
    {
        return IsVU1() ? vif1Regs : vif0Regs;
    }
};

struct mVU_Globals
{
#define __four(val) { val, val, val, val }
    u32   absclip [4] = __four(0x7fffffff);
    u32   signbit [4] = __four(0x80000000);
    u32   minvals [4] = __four(0xff7fffff);
    u32   maxvals [4] = __four(0x7f7fffff);
    u32   exponent[4] = __four(0x7f800000);
    u32   one     [4] = __four(0x3f800000);
    u32   Pi4     [4] = __four(0x3f490fdb);
    u32   T1      [4] = __four(0x3f7ffff5);
    u32   T5      [4] = __four(0xbeaaa61c);
    u32   T2      [4] = __four(0x3e4c40a6);
    u32   T3      [4] = __four(0xbe0e6c63);
    u32   T4      [4] = __four(0x3dc577df);
    u32   T6      [4] = __four(0xbd6501c4);
    u32   T7      [4] = __four(0x3cb31652);
    u32   T8      [4] = __four(0xbb84d7e7);
    u32   S2      [4] = __four(0xbe2aaaa4);
    u32   S3      [4] = __four(0x3c08873e);
    u32   S4      [4] = __four(0xb94fb21f);
    u32   S5      [4] = __four(0x362e9c14);
    u32   E1      [4] = __four(0x3e7fffa8);
    u32   E2      [4] = __four(0x3d0007f4);
    u32   E3      [4] = __four(0x3b29d3ff);
    u32   E4      [4] = __four(0x3933e553);
    u32   E5      [4] = __four(0x36b63510);
    u32   E6      [4] = __four(0x353961ac);
    u32   I32MAXF [4] = __four(0x4effffff);
    float FTOI_4  [4] = __four(16.0);
    float FTOI_12 [4] = __four(4096.0);
    float FTOI_15 [4] = __four(32768.0);
    float ITOF_4  [4] = __four(0.0625f);
    float ITOF_12 [4] = __four(0.000244140625);
    float ITOF_15 [4] = __four(0.000030517578125);
#undef __four
};

#define SINGLE(sign, exp, mant) (((u32)(sign) << 31) | ((u32)(exp) << 23) | (u32)(mant))
#define DOUBLE(sign, exp, mant) (((sign##ULL) << 63) | ((exp##ULL) << 52) | (mant##ULL))

struct FPUd_Globals
{
    u32 neg[4], pos[4];

    u32 pos_inf[4], neg_inf[4],
            one_exp[4];

    u64 dbl_one_exp[2];

    u64 dbl_cvt_overflow, // needs special code if above or equal
    dbl_ps2_overflow, // overflow & clamp if above or equal
    dbl_underflow;    // underflow if below

    u64 padding;

    u64 dbl_s_pos[2];
    //u64		dlb_s_neg[2];
};

struct SSEMasks
{
    u32 MIN_MAX_1[4], MIN_MAX_2[4], ADD_SS[4];
};

struct mVU_SSE4
{
    u32 sse4_minvals[2][4] = {
        {0xff7fffff, 0xffffffff, 0xffffffff, 0xffffffff}, //1000
        {0xff7fffff, 0xff7fffff, 0xff7fffff, 0xff7fffff}, //1111
    };
    u32 sse4_maxvals[2][4] = {
        {0x7f7fffff, 0x7fffffff, 0x7fffffff, 0x7fffffff}, //1000
        {0x7f7fffff, 0x7f7fffff, 0x7f7fffff, 0x7f7fffff}, //1111
    };
    ////
    u32 sse4_compvals[2][4] = {
        {0x7f7fffff, 0x7f7fffff, 0x7f7fffff, 0x7f7fffff}, //1111
        {0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff}, //1111
    };
    ////
    u32 s_neg[4] = {0x80000000, 0xffffffff, 0xffffffff, 0xffffffff};
    u32 s_pos[4] = {0x7fffffff, 0xffffffff, 0xffffffff, 0xffffffff};
    ////
    u32 g_minvals[4] = {0xff7fffff, 0xff7fffff, 0xff7fffff, 0xff7fffff};
    u32 g_maxvals[4] = {0x7f7fffff, 0x7f7fffff, 0x7f7fffff, 0x7f7fffff};
    ////
    u32 result[4] = { 0x3f490fda };
    ////
    u32 minmax_mask[8] =
    {
        0xffffffff, 0x80000000, 0, 0,
        0,          0x40000000, 0, 0,
    };
    ////
    FPUd_Globals s_const =
    {
        {0x80000000, 0xffffffff, 0xffffffff, 0xffffffff},
        {0x7fffffff, 0xffffffff, 0xffffffff, 0xffffffff},

        {SINGLE(0, 0xff, 0), 0, 0, 0},
        {SINGLE(1, 0xff, 0), 0, 0, 0},
        {SINGLE(0,    1, 0), 0, 0, 0},

        {DOUBLE(0, 1, 0), 0},

        DOUBLE(0, 1151, 0), // cvt_overflow
        DOUBLE(0, 1152, 0), // ps2_overflow
        DOUBLE(0,  897, 0), // underflow

        0,                  // Padding!!

        {0x7fffffffffffffffULL, 0},
        //{0x8000000000000000ULL, 0},
    };
    ////
    SSEMasks sseMasks =
    {
        {0xffffffff, 0x80000000, 0xffffffff, 0x80000000},
        {0x00000000, 0x40000000, 0x00000000, 0x40000000},
        {0x80000000, 0xffffffff, 0xffffffff, 0xffffffff},
    };
};

#endif //PCSX2_VUDEF_H
