//
// Created by k2154 on 2025-07-29.
//

#ifndef PCSX2_R3000ADEF_H
#define PCSX2_R3000ADEF_H

#include "common/Pcsx2Defs.h"

union GPRRegs {
    struct {
        u32 r0, at, v0, v1, a0, a1, a2, a3,
                t0, t1, t2, t3, t4, t5, t6, t7,
                s0, s1, s2, s3, s4, s5, s6, s7,
                t8, t9, k0, k1, gp, sp, s8, ra, hi, lo; // hi needs to be at index 32! don't change
    } n;
    u32 r[34]; /* Lo, Hi in r[33] and r[32] */
};

union CP0Regs {
    struct {
        u32 Index,     Random,    EntryLo0,  EntryLo1,
                Context,   PageMask,  Wired,     Reserved0,
                BadVAddr,  Count,     EntryHi,   Compare,
                Status,    Cause,     EPC,       PRid,
                Config,    LLAddr,    WatchLO,   WatchHI,
                XContext,  Reserved1, Reserved2, Reserved3,
                Reserved4, Reserved5, ECC,       CacheErr,
                TagLo,     TagHi,     ErrorEPC,  Reserved6;
    } n;
    u32 r[32];
};

struct SVector2D {
    short x, y;
};

struct SVector2Dz {
    short z, pad;
};

struct SVector3D {
    short x, y, z, pad;
};

struct LVector3D {
    short x, y, z, pad;
};

struct CBGR {
    unsigned char r, g, b, c;
};

struct SMatrix3D {
    short m11, m12, m13, m21, m22, m23, m31, m32, m33, pad;
};

union CP2Data {
    struct {
        SVector3D     v0, v1, v2;
        CBGR          rgb;
        s32          otz;
        s32          ir0, ir1, ir2, ir3;
        SVector2D     sxy0, sxy1, sxy2, sxyp;
        SVector2Dz    sz0, sz1, sz2, sz3;
        CBGR          rgb0, rgb1, rgb2;
        s32          reserved;
        s32          mac0, mac1, mac2, mac3;
        u32 irgb, orgb;
        s32          lzcs, lzcr;
    } n;
    u32 r[32];
};

union CP2Ctrl {
    struct {
        SMatrix3D rMatrix;
        s32      trX, trY, trZ;
        SMatrix3D lMatrix;
        s32      rbk, gbk, bbk;
        SMatrix3D cMatrix;
        s32      rfc, gfc, bfc;
        s32      ofx, ofy;
        s32      h;
        s32      dqa, dqb;
        s32      zsf3, zsf4;
        s32      flag;
    } n;
    u32 r[32];
};

struct psxRegisters {
    GPRRegs GPR;		/* General Purpose Registers */
    CP0Regs CP0;		/* Coprocessor0 Registers */
    CP2Data CP2D; 		/* Cop2 data registers */
    CP2Ctrl CP2C; 		/* Cop2 control registers */
    u32 pc;				/* Program counter */
    u32 code;			/* The instruction */
    u32 cycle;
    u32 interrupt;
    u32 pcWriteback;

    // Controls when branch tests are performed.
    u32 iopNextEventCycle;

    // This value is used when the IOP execution is broken to return control to the EE.
    // (which happens when the IOP throws EE-bound interrupts).  It holds the value of
    // iopCycleEE (which is set to zero to facilitate the code break), so that the unrun
    // cycles can be accounted for later.
    s32 iopBreak;

    // Tracks current number of cycles IOP can run in EE cycles. When it dips below zero,
    // control is returned to the EE.
    s32 iopCycleEE;
    u32 iopCycleEECarry;

    u32 sCycle[32];		// start cycle for signaled ints
    s32 eCycle[32];		// cycle delta for signaled ints (sCycle + eCycle == branch cycle)
    //u32 _msflag[32];
    //u32 _smflag[32];
};

//alignas(16) extern psxRegisters psxRegs;

#endif //PCSX2_R3000ADEF_H
