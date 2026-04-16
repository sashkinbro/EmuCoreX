// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "Vif.h"
#include "Vif_Unpack.h"

struct vifCode {
   u32 addr;
   u32 size;
   u32 cmd;
   u16 wl;
   u16 cl;
};

union tBITBLTBUF {
	u64 _u64;
	struct {
		u32 SBP : 14;
		u32 _pad14 : 2;
		u32 SBW : 6;
		u32 _pad22 : 2;
		u32 SPSM : 6;
		u32 _pad30 : 2;
		u32 DBP : 14;
		u32 _pad46 : 2;
		u32 DBW : 6;
		u32 _pad54 : 2;
		u32 DPSM : 6;
		u32 _pad62 : 2;
	};
};

union tTRXPOS {
	u64 _u64;
	struct {
		u32 SSAX : 11;
		u32 _PAD1 : 5;
		u32 SSAY : 11;
		u32 _PAD2 : 5;
		u32 DSAX : 11;
		u32 _PAD3 : 5;
		u32 DSAY : 11;
		u32 DIRY : 1;
		u32 DIRX : 1;
		u32 _PAD4 : 3;
	};
};

union tTRXREG
{
	u64 _u64;
	struct
	{
		u32 RRW : 12;
		u32 _pad12 : 20;
		u32 RRH : 12;
		u32 _pad44 : 20;
	};
};

struct tVIF_CTRL {
   bool enabled;
   u32 value;
};

enum VifTransferProgressBits : u8
{
	VIF_TRANSFER_ACTIVE = 0x01,
	VIF_TRANSFER_MFIFO_EMPTY = 0x10
};

enum Vif1WaitReason : u32
{
	VIF1_WAIT_NONE = 0,
	VIF1_WAIT_VU = 1u << 0,
	VIF1_WAIT_GIF = 1u << 1,
	VIF1_WAIT_STALL = 1u << 2,
	VIF1_WAIT_RESUME = 1u << 3,
	VIF1_WAIT_MFIFO = 1u << 4
};

enum class Vif1GifWaitState : u8
{
	Idle = 0,
	Waiting = 1
};

enum VifBridgeStateBits : u32
{
	VIF_BRIDGE_NONE = 0,
	VIF_BRIDGE_STALL = 1u << 0,
	VIF_BRIDGE_WAIT_VU = 1u << 1,
	VIF_BRIDGE_WAIT_GIF = 1u << 2,
	VIF_BRIDGE_RESUME_OFFSET = 1u << 3,
	VIF_BRIDGE_TRANSFER_ACTIVE = 1u << 4,
	VIF_BRIDGE_MFIFO_EMPTY = 1u << 5,
	VIF_BRIDGE_QUEUED_PROGRAM = 1u << 6,
	VIF_BRIDGE_QUEUED_GIF_WAIT = 1u << 7
};

struct VifBridgeStateSnapshot
{
	u32 flags = VIF_BRIDGE_NONE;
	u32 stall_kind = 0;
	u32 resume_offset = 0;
	u32 queued_pc = 0;

	__fi bool HasAny(u32 mask) const { return (flags & mask) != 0; }
	__fi bool HasAll(u32 mask) const { return (flags & mask) == mask; }
};

__fi inline Vif1GifWaitState vif1GetGifWaitState()
{
	return vif1Regs.stat.VGW ? Vif1GifWaitState::Waiting : Vif1GifWaitState::Idle;
}

__fi inline bool vif1HasGifWait()
{
	return vif1GetGifWaitState() == Vif1GifWaitState::Waiting;
}

// NOTE, if debugging vif stalls, use sega classics, spyro, gt4, and taito
struct vifStruct {
	alignas(16) u128 MaskRow;
	alignas(16) u128 MaskCol;

	struct { // These must be together for MTVU
		vifCode tag;
		int cmd;
		int pass;
		int cl;
		u8  usn;
		u8 start_aligned;
		u8  StructEnd; // Address of this is used to calculate end of struct
	};

	int irq;

	bool done;
	tVIF_CTRL vifstalled;
	bool stallontag;
	bool waitforvu;
	int unpackcalls;
	// GS registers used for calculating the size of the last local->host transfer initiated on the GS
	// Transfer size calculation should be restricted to GS emulation in the future
	union
	{
		struct
		{
			tBITBLTBUF BITBLTBUF;
			tTRXPOS    TRXPOS;
			tTRXREG    TRXREG;
		};
		u64 transfer_registers[3];
	};

	u32        GSLastDownloadSize;

	tVIF_CTRL  irqoffset; // 32bit offset where next vif code is
	u32 vifpacketsize;
	u8  inprogress;
	u8  dmamode;

	bool queued_program;
	u32 queued_pc;
	bool queued_gif_wait;

	__fi bool IsTransferActive() const { return (inprogress & VIF_TRANSFER_ACTIVE) != 0; }
	__fi void SetTransferActive(bool active)
	{
		if (active)
			inprogress |= VIF_TRANSFER_ACTIVE;
		else
			inprogress &= ~VIF_TRANSFER_ACTIVE;
	}
	__fi void ClearTransferProgress() { inprogress = 0; }
	__fi void ResetTransferProgress(bool preserve_mfifo_wait)
	{
		// Use the constant directly: after a memset the current inprogress is
		// already 0, so (inprogress & VIF_TRANSFER_MFIFO_EMPTY) would always be 0.
		inprogress = preserve_mfifo_wait ? static_cast<u8>(VIF_TRANSFER_MFIFO_EMPTY) : 0u;
	}

	__fi bool IsMfifoAwaitingData() const { return (inprogress & VIF_TRANSFER_MFIFO_EMPTY) != 0; }
	__fi void SetMfifoAwaitingData(bool waiting)
	{
		if (waiting)
			inprogress |= VIF_TRANSFER_MFIFO_EMPTY;
		else
			inprogress &= ~VIF_TRANSFER_MFIFO_EMPTY;
	}

	__fi u32 GetResumeOffset() const { return irqoffset.value; }
	__fi void SetResumeOffset(u32 offset)
	{
		irqoffset.value = offset;
		irqoffset.enabled = (offset != 0);
	}
	__fi void ClearResumeOffset()
	{
		irqoffset.value = 0;
		irqoffset.enabled = false;
	}

	__fi bool HasResumeOffset() const { return irqoffset.enabled; }
	__fi bool IsIrqStalled() const { return vifstalled.enabled && vifstalled.value == VIF_IRQ_STALL; }
	__fi bool IsTimingStalled() const { return vifstalled.enabled && vifstalled.value == VIF_TIMING_BREAK; }
	__fi bool IsWaitingForVu() const { return waitforvu; }
	__fi void SetWaitForVu(bool waiting) { waitforvu = waiting; }
	__fi void BeginVuWait(bool enable_stall)
	{
		waitforvu = true;
		SetTimingBreakStall(enable_stall);
	}
	__fi void EndVuWait()
	{
		waitforvu = false;
		NormalizeBridgeState();
	}
	__fi bool IsWaitingForGif(const VIFregisters& vifRegs) const
	{
		pxAssert(&vifRegs == &vif1Regs);
		return vif1HasGifWait();
	}
	__fi bool IsWaitingForStallState() const { return vifstalled.enabled; }
	__fi void SetStallState(bool enabled, u32 value)
	{
		vifstalled.enabled = enabled;
		if (enabled)
			vifstalled.value = value;
	}
	__fi void SetTimingBreakStall(bool enabled) { SetStallState(enabled, VIF_TIMING_BREAK); }
	__fi void SetIrqStall(bool enabled) { SetStallState(enabled, VIF_IRQ_STALL); }
	__fi void ClearStallState() { vifstalled.enabled = false; }
	__fi bool IsWaitingForVuOrGif(const VIFregisters& vifRegs) const { return IsWaitingForVu() || IsWaitingForGif(vifRegs); }
	__fi bool HasStallResumeOffset() const { return HasResumeOffset() && IsWaitingForStallState(); }

	__fi bool HasQueuedProgram() const { return queued_program; }
	__fi void SetQueuedProgram(bool queued) { queued_program = queued; }
	__fi void ClearQueuedProgram()
	{
		queued_program = false;
		queued_gif_wait = false;
	}
	__fi void QueueMicroProgram(u32 pc, bool requires_gif_wait)
	{
		queued_program = true;
		queued_pc = pc;
		queued_gif_wait = requires_gif_wait;
	}
	__fi bool IsQueuedGifWaitPending() const { return queued_gif_wait; }
	__fi void SetQueuedGifWait(bool waiting) { queued_gif_wait = waiting; }
	__fi bool ShouldDelayQueuedProgramForGifPath() const { return HasQueuedProgram() && IsQueuedGifWaitPending(); }
	__fi void NormalizeBridgeState()
	{
		inprogress &= (VIF_TRANSFER_ACTIVE | VIF_TRANSFER_MFIFO_EMPTY);
		if (!vifstalled.enabled)
			vifstalled.value = 0;
		if (!irqoffset.enabled || irqoffset.value == 0)
			ClearResumeOffset();
		if (!queued_program)
		{
			queued_gif_wait = false;
			queued_pc = 0;
		}
	}
	__fi void PrepareForDmaStart()
	{
		ClearTransferProgress();
		NormalizeBridgeState();
	}
	__fi void CompleteDmaTransfer()
	{
		ClearStallState();
		ClearResumeOffset();
		NormalizeBridgeState();
	}
	__fi void ResetRuntimeBridgeState(bool preserve_mfifo_wait)
	{
		ResetTransferProgress(preserve_mfifo_wait);
		ClearStallState();
		ClearResumeOffset();
		NormalizeBridgeState();
	}
	__fi VifBridgeStateSnapshot CaptureBridgeState(const VIFregisters* vifRegs = nullptr) const
	{
		VifBridgeStateSnapshot snapshot;
		snapshot.flags |= IsWaitingForStallState() ? VIF_BRIDGE_STALL : VIF_BRIDGE_NONE;
		snapshot.flags |= IsWaitingForVu() ? VIF_BRIDGE_WAIT_VU : VIF_BRIDGE_NONE;
		snapshot.flags |= (vifRegs == &vif1Regs && IsWaitingForGif(*vifRegs)) ? VIF_BRIDGE_WAIT_GIF : VIF_BRIDGE_NONE;
		snapshot.flags |= HasResumeOffset() ? VIF_BRIDGE_RESUME_OFFSET : VIF_BRIDGE_NONE;
		snapshot.flags |= IsTransferActive() ? VIF_BRIDGE_TRANSFER_ACTIVE : VIF_BRIDGE_NONE;
		snapshot.flags |= IsMfifoAwaitingData() ? VIF_BRIDGE_MFIFO_EMPTY : VIF_BRIDGE_NONE;
		snapshot.flags |= HasQueuedProgram() ? VIF_BRIDGE_QUEUED_PROGRAM : VIF_BRIDGE_NONE;
		snapshot.flags |= IsQueuedGifWaitPending() ? VIF_BRIDGE_QUEUED_GIF_WAIT : VIF_BRIDGE_NONE;
		snapshot.stall_kind = IsWaitingForStallState() ? vifstalled.value : 0;
		snapshot.resume_offset = HasResumeOffset() ? GetResumeOffset() : 0;
		snapshot.queued_pc = HasQueuedProgram() ? queued_pc : 0;
		return snapshot;
	}
	__fi bool HasBridgeState(u32 mask, const VIFregisters* vifRegs = nullptr) const
	{
		return CaptureBridgeState(vifRegs).HasAny(mask);
	}

	// Transitional adapter: expose the current VIF1 wait sources as named reasons
	// while the legacy storage still lives in split flags and bitfields.
	__fi u32 GetVif1WaitReasonMask(const VIFregisters& vifRegs) const
	{
		const VifBridgeStateSnapshot snapshot = CaptureBridgeState(&vifRegs);
		u32 mask = VIF1_WAIT_NONE;
		mask |= snapshot.HasAny(VIF_BRIDGE_WAIT_VU) ? VIF1_WAIT_VU : 0;
		mask |= snapshot.HasAny(VIF_BRIDGE_WAIT_GIF) ? VIF1_WAIT_GIF : 0;
		mask |= snapshot.HasAny(VIF_BRIDGE_STALL) ? VIF1_WAIT_STALL : 0;
		mask |= snapshot.HasAny(VIF_BRIDGE_RESUME_OFFSET) ? VIF1_WAIT_RESUME : 0;
		mask |= snapshot.HasAny(VIF_BRIDGE_MFIFO_EMPTY) ? VIF1_WAIT_MFIFO : 0;
		return mask;
	}

	__fi bool HasVif1WaitReason(const VIFregisters& vifRegs, Vif1WaitReason reason) const
	{
		return (GetVif1WaitReasonMask(vifRegs) & static_cast<u32>(reason)) != 0;
	}
};

alignas(16) extern vifStruct  vif0, vif1;

__fi inline void vif1SetGifWait(bool waiting)
{
	vif1Regs.stat.VGW = (waiting ? 1u : 0u);
}

__fi inline void vif1ClearGifWait()
{
	vif1SetGifWait(false);
}

__fi inline void vif1ArmGifPathWait()
{
	vif1SetGifWait(true);
	vif1.SetTimingBreakStall(!!vif1ch.chcr.STR);
}

__fi inline bool vif1IsBridgeBlocked()
{
	const VifBridgeStateSnapshot bridge = vif1.CaptureBridgeState(&vif1Regs);
	return bridge.HasAny(VIF_BRIDGE_WAIT_VU | VIF_BRIDGE_WAIT_GIF);
}

__fi inline bool vif1CanResumeFromControlWrite()
{
	return vif1ch.chcr.STR && !vif1Regs.stat.test(VIF1_STAT_FDR);
}

_vifT extern u32 vifRead32(u32 mem);
_vifT extern bool vifWrite32(u32 mem, u32 value);
extern void vif0Interrupt();
extern void vif0VUFinish();
extern void vif0Reset();

extern void vif1Interrupt();
extern void vif1VUFinish();
extern void vif1Reset();
extern void vif1RequestDmaRetry(u32 cycles);
extern void vif1RequestDmaProgress(u32 cycles);
extern void vif1RequestDmaProgressWithVuBusy(u32 cycles);
extern void vif1SetActiveDmaStall(bool stalled);
extern bool vif1RequestResumeFromGifPathIdle(u32 resumeCycles);
extern bool vif1ResumeDmaFromGifPathSignal(u32 resumeCycles);

typedef int FnType_VifCmdHandler(int pass, const u32 *data);
typedef FnType_VifCmdHandler* Fnptr_VifCmdHandler;

alignas(16) extern const Fnptr_VifCmdHandler vifCmdHandler[2][128];

__fi static int _limit(int a, int max)
{
	return ((a > max) ? max : a);
}

enum VifModes
{
	VIF_NORMAL_TO_MEM_MODE = 0,
	VIF_NORMAL_FROM_MEM_MODE = 1,
	VIF_CHAIN_MODE = 2
};

// Generic constants
static const unsigned int VIF0intc = 4;
static const unsigned int VIF1intc = 5;

extern u32 g_vif0Cycles;
extern u32 g_vif1Cycles;

extern void vif0FLUSH();
extern void vif1FLUSH();

extern void vifExecQueue(int idx);
