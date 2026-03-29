// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "MemoryTypes.h"
#include "R5900.h"

#include "common/StringUtil.h"

enum vif0_stat_flags
{
	VIF0_STAT_VPS_W 	= (1),
	VIF0_STAT_VPS_D 	= (2),
	VIF0_STAT_VPS_T		= (3),
	VIF0_STAT_VPS 		= (3),
	VIF0_STAT_VEW		= (1<<2),
	VIF0_STAT_MRK		= (1<<6),
	VIF0_STAT_DBF		= (1<<7),
	VIF0_STAT_VSS		= (1<<8),
	VIF0_STAT_VFS		= (1<<9),
	VIF0_STAT_VIS		= (1<<10),
	VIF0_STAT_INT		= (1<<11),
	VIF0_STAT_ER0		= (1<<12),
	VIF0_STAT_ER1		= (1<<13),
	VIF0_STAT_FQC		= (15<<24)
};

enum vif1_stat_flags
{
	VIF1_STAT_VPS_W 	= (1),
	VIF1_STAT_VPS_D 	= (2),
	VIF1_STAT_VPS_T		= (3),
	VIF1_STAT_VPS 		= (3),
	VIF1_STAT_VEW		= (1<<2),
	VIF1_STAT_VGW		= (1<<3),
	VIF1_STAT_MRK		= (1<<6),
	VIF1_STAT_DBF		= (1<<7),
	VIF1_STAT_VSS		= (1<<8),
	VIF1_STAT_VFS		= (1<<9),
	VIF1_STAT_VIS		= (1<<10),
	VIF1_STAT_INT		= (1<<11),
	VIF1_STAT_ER0		= (1<<12),
	VIF1_STAT_ER1		= (1<<13),
	VIF1_STAT_FDR 		= (1<<23),
	VIF1_STAT_FQC		= (31<<24)
};

// These are the stat flags that are the same for vif0 & vif1,
// for occassions where we don't neccessarily know which we are using.
enum vif_stat_flags
{
	VIF_STAT_VPS_W		= (1),
	VIF_STAT_VPS_D		= (2),
	VIF_STAT_VPS_T		= (3),
	VIF_STAT_VPS 		= (3),
	VIF_STAT_VEW		= (1<<2),
	VIF_STAT_MRK		= (1<<6),
	VIF_STAT_DBF		= (1<<7),
	VIF_STAT_VSS		= (1<<8),
	VIF_STAT_VFS		= (1<<9),
	VIF_STAT_VIS		= (1<<10),
	VIF_STAT_INT		= (1<<11),
	VIF_STAT_ER0		= (1<<12),
	VIF_STAT_ER1		= (1<<13)
};

enum vif_status
{
    VPS_IDLE		 = 0,
    VPS_WAITING		 = 1,
    VPS_DECODING	 = 2,
    VPS_TRANSFERRING = 3 // And decompressing.
};

enum vif_stallreasons
{
    VIF_TIMING_BREAK  = 1,
    VIF_IRQ_STALL	 = 2
};

#define VIF_STAT(value) ((tVIF_STAT)(value))

union tVIF_FBRST {
	struct {
		u32 RST : 1; // Resets Vif(0/1) when written.
		u32 FBK : 1; // Causes a Forcebreak to Vif((0/1) when true. (Stall)
		u32 STP : 1; // Stops after the end of the Vifcode in progress when true. (Stall)
		u32 STC : 1; // Cancels the Vif(0/1) stall and clears Vif Stats VSS, VFS, VIS, INT, ER0 & ER1.
		u32 _reserved : 28;
	};
	u32 _u32;

	tVIF_FBRST() = default;
	tVIF_FBRST(u32 val)					{ _u32 = val; }
	bool test		(u32 flags) const	{ return !!(_u32 & flags); }
	void set_flags	(u32 flags)			{ _u32 |=  flags; }
	void clear_flags(u32 flags)			{ _u32 &= ~flags; }
	void reset()						{ _u32 = 0; }
	std::string desc() const				{ return StringUtil::StdStringFromFormat("Fbrst: 0x%x", _u32); }
};

#define FBRST(value) ((tVIF_FBRST)(value))

struct VIFregistersMTVU {
	vifCycle cycle; //data write cycle
	u32 mode;
	u32 num;
	u32 mask;
	u32 itop;
	u32 top;       // Not used in VIF0
};

#define _vifT		template <int idx>
#define  GetVifX	(idx ? (vif1)     : (vif0))
#define  vifXch		(idx ? (vif1ch)   : (vif0ch))
#define  vifXRegs	(idx ? (vif1Regs) : (vif0Regs))

#define  MTVU_VifX     (idx ? ((THREAD_VU1) ? vu1Thread.vif     : vif1)     : (vif0))
#define  MTVU_VifXRegs (idx ? ((THREAD_VU1) ? vu1Thread.vifRegs : vif1Regs) : (vif0Regs))

#define VifStallEnable(vif) (vif.chcr.STR);

extern void dmaVIF0();
extern void dmaVIF1();
extern void mfifoVIF1transfer();
extern bool VIF0transfer(u32 *data, int size, bool TTE=0);
extern bool VIF1transfer(u32 *data, int size, bool TTE=0);
extern void vifMFIFOInterrupt();
