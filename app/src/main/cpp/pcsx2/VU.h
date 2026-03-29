// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once
#include "Vif.h"

enum VURegFlags
{
	REG_STATUS_FLAG = 16,
	REG_MAC_FLAG = 17,
	REG_CLIP_FLAG = 18,
	REG_ACC_FLAG = 19, // dummy flag that indicates that VFACC is written/read (nothing to do with VI[19])
	REG_R = 20,
	REG_I = 21,
	REG_Q = 22,
	REG_P = 23, // only exists in micromode
	REG_VF0_FLAG = 24, // dummy flag that indicates VF0 is read (nothing to do with VI[24])
	REG_TPC = 26,
	REG_CMSAR0 = 27,
	REG_FBRST = 28,
	REG_VPU_STAT = 29,
	REG_CMSAR1 = 31
};

//interpreter hacks, WIP
//#define INT_VUSTALLHACK //some games work without those, big speedup
//#define INT_VUDOUBLEHACK

enum VUStatus
{
	VU_Ready = 0,
	VU_Run = 1,
	VU_Stop = 2,
};

//#define VUFLAG_BREAKONMFLAG		0x00000001
#define VUFLAG_MFLAGSET 0x00000002
#define VUFLAG_INTCINTERRUPT 0x00000004

enum VUPipeState
{
	VUPIPE_NONE = 0,
	VUPIPE_FMAC,
	VUPIPE_FDIV,
	VUPIPE_EFU,
	VUPIPE_IALU,
	VUPIPE_BRANCH,
	VUPIPE_XGKICK
};

// Obsolete(?)  -- I think I'd rather use vu0Regs/vu1Regs or actually have these explicit to any
// CPP file that needs them only. --air

// Do not use __fi here because it fires 'multiple definition' error in GCC
inline bool VURegs::IsVU1() const { return this == &g_cpuRegistersPack.vuRegs[1]; }
inline bool VURegs::IsVU0() const { return this == &g_cpuRegistersPack.vuRegs[0]; }

extern void vuMemAllocate();
extern void vuMemReset();
extern void vuMemRelease();
extern u32* GET_VU_MEM(VURegs* VU, u32 addr);
