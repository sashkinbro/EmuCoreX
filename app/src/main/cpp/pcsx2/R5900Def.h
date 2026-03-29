//
// Created by k2154 on 2025-07-30.
//

#ifndef PCSX2_R5900DEF_H
#define PCSX2_R5900DEF_H

#include "common/Pcsx2Defs.h"

union GPR_reg {   // Declare union type GPR register
    u128 UQ;
    s128 SQ;
    u64 UD[2];      //128 bits
    s64 SD[2];
    u32 UL[4];
    s32 SL[4];
    u16 US[8];
    s16 SS[8];
    u8  UC[16];
    s8  SC[16];
};

union GPRregs {
    struct {
        GPR_reg r0, at, v0, v1, a0, a1, a2, a3,
                t0, t1, t2, t3, t4, t5, t6, t7,
                s0, s1, s2, s3, s4, s5, s6, s7,
                t8, t9, k0, k1, gp, sp, s8, ra;
    } n;
    GPR_reg r[32];
};

union PERFregs {
    struct
    {
        union
        {
            struct
            {
                u32 pad0:1;			// LSB should always be zero (or undefined)
                u32 EXL0:1;			// enable PCR0 during Level 1 exception handling
                u32 K0:1;			// enable PCR0 during Kernel Mode execution
                u32 S0:1;			// enable PCR0 during Supervisor mode execution
                u32 U0:1;			// enable PCR0 during User-mode execution
                u32 Event0:5;		// PCR0 event counter (all values except 1 ignored at this time)

                u32 pad1:1;			// more zero/undefined padding [bit 10]

                u32 EXL1:1;			// enable PCR1 during Level 1 exception handling
                u32 K1:1;			// enable PCR1 during Kernel Mode execution
                u32 S1:1;			// enable PCR1 during Supervisor mode execution
                u32 U1:1;			// enable PCR1 during User-mode execution
                u32 Event1:5;		// PCR1 event counter (all values except 1 ignored at this time)

                u32 Reserved:11;
                u32 CTE:1;			// Counter enable bit, no counting if set to zero.
            } b;

            u32 val;
        } pccr;

        u32 pcr0, pcr1, pad;
    } n;
    u32 r[4];
};

union CP0regs {
    struct {
        u32	Index,    Random,    EntryLo0,  EntryLo1,
                Context,  PageMask,  Wired,     Reserved0,
                BadVAddr, Count,     EntryHi,   Compare;
        union {
            struct {
                u32 IE:1;		// Bit 0: Interrupt Enable flag.
                u32 EXL:1;		// Bit 1: Exception Level, set on any exception not covered by ERL.
                u32 ERL:1;		// Bit 2: Error level, set on Resetm NMI, perf/debug exceptions.
                u32 KSU:2;		// Bits 3-4: Kernel [clear] / Supervisor [set] mode
                u32 unused0:3;
                u32 IM:8;		// Bits 10-15: Interrupt mask (bits 12,13,14 are unused)
                u32 EIE:1;		// Bit 16: IE bit enabler.  When cleared, ints are disabled regardless of IE status.
                u32 _EDI:1;		// Bit 17: Interrupt Enable (set enables ints in all modes, clear enables ints in kernel mode only)
                u32 CH:1;		// Bit 18: Status of most recent cache instruction (set for hit, clear for miss)
                u32 unused1:3;
                u32 BEV:1;		// Bit 22: if set, use bootstrap for TLB/general exceptions
                u32 DEV:1;		// Bit 23: if set, use bootstrap for perf/debug exceptions
                u32 unused2:2;
                u32 FR:1;		// (?)
                u32 unused3:1;
                u32 CU:4;		// Bits 28-31: Co-processor Usable flag
            } b;
            u32 val;
        } Status;
        u32   Cause,    EPC,       PRid,
                Config,   LLAddr,    WatchLO,   WatchHI,
                XContext, Reserved1, Reserved2, Debug,
                DEPC,     PerfCnt,   ErrCtl,    CacheErr,
                TagLo,    TagHi,     ErrorEPC,  DESAVE;
    } n;
    u32 r[32];
};

struct cpuRegisters {
    GPRregs GPR;		// GPR regs
    // NOTE: don't change order since recompiler uses it
    GPR_reg HI;
    GPR_reg LO;			// hi & log 128bit wide
    CP0regs CP0;		// is COP0 32bit?
    u32 sa;				// shift amount (32bit), needs to be 16 byte aligned
    u32 IsDelaySlot;	// set true when the current instruction is a delay slot.
    u32 pc;				// Program counter, when changing offset in struct, check iR5900-X.S to make sure offset is correct
    u32 code;			// current instruction
    PERFregs PERF;
    u32 eCycle[32];
    u32 sCycle[32];		// for internal counters
    u32 cycle;			// calculate cpucycles..
    u32 interrupt;
    int branch;
    int opmode;			// operating mode
    u32 tempcycles;
    u32 dmastall;
    u32 pcWriteback;

    // if cpuRegs.cycle is greater than this cycle, should check cpuEventTest for updates
    u32 nextEventCycle;
    u32 lastEventCycle;
    u32 lastCOP0Cycle;
    u32 lastPERFCycle[2];
};

union FPRreg {
    float f;
    u32 UL;
    s32 SL;				// signed 32bit used for sign extension in interpreters.
};

struct fpuRegisters {
    FPRreg fpr[32];		// 32bit floating point registers
    u32 fprc[32];		// 32bit floating point control registers
    FPRreg ACC;			// 32 bit accumulator
    u32 ACCflag;        // an internal accumulator overflow flag
};

#endif //PCSX2_R5900DEF_H
