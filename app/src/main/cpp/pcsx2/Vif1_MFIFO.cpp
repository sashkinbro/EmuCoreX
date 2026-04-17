// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "Vif.h"
#include "Gif_Unit.h"
#include "Vif_Dma.h"

static __fi VifBridgeStateSnapshot vif1CaptureRuntimeBridgeState()
{
	return vif1.CaptureBridgeState(&vif1Regs);
}

static __fi bool vif1HasActiveBridgeGifWait(const VifBridgeStateSnapshot& bridge)
{
	return bridge.HasAny(VIF_BRIDGE_WAIT_GIF) && gifUnit.gifPath[GIF_PATH_3].state != GIF_PATH_IDLE;
}

static __fi void vif1ScheduleBridgeProgress(const VifBridgeStateSnapshot& bridge, u32 cycles)
{
	if (bridge.HasAny(VIF_BRIDGE_WAIT_VU))
		vif1RequestDmaProgressWithVuBusy(cycles);
	else
		vif1RequestDmaProgress(cycles);
}

static __fi void vif1RefreshMfifoActiveTransferFqc()
{
	vif1Regs.stat.FQC = std::min((u32)0x10, vif1ch.qwc);
}

static __fi void vif1ClearCompletedMfifoPath2DownloadWait()
{
	if (gifRegs.stat.APATH != 2 || !gifUnit.gifPath[1].isDone())
		return;

	gifRegs.stat.APATH = 0;
	gifRegs.stat.OPH = 0;

	if (gifUnit.GetVifPath13BusyMask() != 0)
		gifUnit.Execute(false, true);
}

static __fi bool vif1TryHandleMfifoGsDownloadPathWait()
{
	if (!vif1ch.chcr.DIR)
		return false;

	const bool isDirect = (vif1.cmd & 0x7f) == 0x50;
	const bool isDirectHL = (vif1.cmd & 0x7f) == 0x51;
	if ((isDirect && !gifUnit.CanDoPath2()) || (isDirectHL && !gifUnit.CanDoPath2HL()))
	{
		GUNIT_WARN("vifMFIFOInterrupt() - Waiting for Path 2 to be ready");
		vif1RequestDmaRetry(128);
		vif1SetActiveDmaStall(true);
		return true;
	}

	return false;
}

static __fi bool vif1TryHandleMfifoVuExecutionWait(const VifBridgeStateSnapshot& bridge)
{
	if (!bridge.HasAny(VIF_BRIDGE_WAIT_VU))
		return false;

	CPU_INT(VIF_VU1_FINISH, std::max(16, cpuGetCycles(VU_MTVU_BUSY)));
	vif1SetActiveDmaStall(true);
	return true;
}

static __fi bool vif1TryHoldMfifoIrqStall()
{
	if (!(vif1.irq && vif1.IsIrqStalled()))
		return false;

	VIF_LOG("VIF MFIFO Code Interrupt detected");
	vif1Regs.stat.INT = true;

	if (((vif1Regs.code >> 24) & 0x7f) != 0x7)
		vif1Regs.stat.VIS = true;

	hwIntcIrq(INTC_VIF1);
	--vif1.irq;

	if (vif1Regs.stat.test(VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS))
	{
		vif1RefreshMfifoActiveTransferFqc();
		VIF_LOG("VIF1 MFIFO Stalled qwc = %x done = %x inprogress = %x", vif1ch.qwc, vif1.done, vif1.IsMfifoAwaitingData());
		if ((vif1ch.qwc > 0 || !vif1.done))
		{
			vif1Regs.stat.VPS = VPS_DECODING;
			VIF_LOG("VIF1 MFIFO Stalled");
			vif1SetActiveDmaStall(true);
			return true;
		}
	}

	return false;
}

static __fi void vif1UpdateMfifoVpsAfterInterruptProgress()
{
	if (vif1.cmd)
	{
		if (vif1.done && vif1ch.qwc == 0)
			vif1Regs.stat.VPS = VPS_WAITING;
	}
	else
	{
		vif1Regs.stat.VPS = VPS_IDLE;
	}
}

static __fi bool vif1TryHandleMfifoEmptyBridgeWait(const VifBridgeStateSnapshot& bridge)
{
	if (!bridge.HasAny(VIF_BRIDGE_MFIFO_EMPTY))
		return false;

	FireMFIFOEmpty();
	vif1SetActiveDmaStall(true);
	return true;
}

static __fi void vif1FinalizeCompletedMfifoInterrupt()
{
	vif1.CompleteDmaTransfer();
	vif1.done = 1;

	if (spr0ch.madr == vif1ch.tadr)
		FireMFIFOEmpty();

	g_vif1Cycles = 0;
	vif1RefreshMfifoActiveTransferFqc();
	vif1ch.chcr.STR = false;
	hwDmacIrq(DMAC_VIF1);
	DMA_LOG("VIF1 MFIFO DMA End");
	vif1SetActiveDmaStall(false);
	vif1Regs.stat.FQC = 0;
}

static u32 qwctag(u32 mask)
{
	return (dmacRegs.rbor.ADDR + (mask & dmacRegs.rbsr.RMSK));
}

static u32 QWCinVIFMFIFO(u32 DrainADDR, u32 qwc)
{
	u32 ret;

	//Calculate what we have in the fifo.
	if (DrainADDR <= spr0ch.madr)
	{
		//Drain is below the tadr, calculate the difference between them
		ret = (spr0ch.madr - DrainADDR) >> 4;
	}
	else
	{
		const u32 limit = dmacRegs.rbor.ADDR + dmacRegs.rbsr.RMSK + 16;
		//Drain is higher than SPR so it has looped round,
		//calculate from base to the SPR tag addr and what is left in the top of the ring
		ret = ((spr0ch.madr - dmacRegs.rbor.ADDR) + (limit - DrainADDR)) >> 4;
	}

	VIF_LOG("VIF MFIFO Requesting %x QWC of %x Available from the MFIFO Base %x MFIFO Top %x, SPR MADR %x Drain %x", qwc, ret, dmacRegs.rbor.ADDR, dmacRegs.rbor.ADDR + dmacRegs.rbsr.RMSK + 16, spr0ch.madr, DrainADDR);

	return ret;
}
static __fi bool mfifoVIF1rbTransfer()
{
	const u32 msize = dmacRegs.rbor.ADDR + dmacRegs.rbsr.RMSK + 16;
	const u32 mfifoqwc = std::min(QWCinVIFMFIFO(vif1ch.madr, vif1ch.qwc), vif1ch.qwc);
	u32* src;
	bool ret;

	if (mfifoqwc == 0)
	{
		DevCon.Warning("VIF MFIFO no QWC before transfer (in transfer function, bit late really)");
		return true; //Cant do anything, lets forget it
	}

	/* Check if the transfer should wrap around the ring buffer */
	if ((vif1ch.madr + (mfifoqwc << 4)) > (msize))
	{
		const int s1 = ((msize)-vif1ch.madr) >> 2;
		const VifBridgeStateSnapshot bridge = vif1.CaptureBridgeState();

		VIF_LOG("Split MFIFO");

		/* it does, so first copy 's1' bytes from 'addr' to 'data' */
		vif1ch.madr = qwctag(vif1ch.madr);

		src = (u32*)PSM(vif1ch.madr);
		if (src == nullptr)
			return false;

		if (bridge.HasAny(VIF_BRIDGE_RESUME_OFFSET))
			ret = VIF1transfer(src + bridge.resume_offset, s1 - bridge.resume_offset);
		else
			ret = VIF1transfer(src, s1);

		if (ret)
		{
			if (bridge.resume_offset != 0)
				DevCon.Warning("VIF1 MFIFO Offest != 0! vifoffset=%x", bridge.resume_offset);
			/* and second copy 's2' bytes from 'maddr' to '&data[s1]' */
			//DevCon.Warning("Loopyloop");
			vif1ch.tadr = qwctag(vif1ch.tadr);
			vif1ch.madr = qwctag(vif1ch.madr);

			src = (u32*)PSM(vif1ch.madr);
			if (src == nullptr)
				return false;
			VIF1transfer(src, ((mfifoqwc << 2) - s1));
		}
	}
	else
	{
		VIF_LOG("Direct MFIFO");
		const VifBridgeStateSnapshot bridge = vif1.CaptureBridgeState();

		/* it doesn't, so just transfer 'qwc*4' words */
		src = (u32*)PSM(vif1ch.madr);
		if (src == nullptr)
			return false;

		if (bridge.HasAny(VIF_BRIDGE_RESUME_OFFSET))
			ret = VIF1transfer(src + bridge.resume_offset, mfifoqwc * 4 - bridge.resume_offset);
		else
			ret = VIF1transfer(src, mfifoqwc << 2);
	}
	return ret;
}

static __fi void mfifo_VIF1chain()
{
	/* Is QWC = 0? if so there is nothing to transfer */
	if (vif1ch.qwc == 0)
	{
		vif1.SetTransferActive(false);
		return;
	}

	if (vif1ch.madr >= dmacRegs.rbor.ADDR &&
		vif1ch.madr < (dmacRegs.rbor.ADDR + dmacRegs.rbsr.RMSK + 16u))
	{
		//if(vif1ch.madr == (dmacRegs.rbor.ADDR + dmacRegs.rbsr.RMSK + 16)) DevCon.Warning("Edge VIF1");
		if (QWCinVIFMFIFO(vif1ch.madr, vif1ch.qwc) == 0)
		{
			VIF_LOG("VIF MFIFO Empty before transfer");
			vif1.SetMfifoAwaitingData(true);
			g_vif1Cycles += 4;
			return;
		}

		mfifoVIF1rbTransfer();
		vif1ch.madr = qwctag(vif1ch.madr);
		//When transferring direct from the MFIFO, the TADR needs to be after the data last read
		//FF7 DoC Expects the transfer to end with an Empty interrupt, so the TADR has to match SPR0_MADR
		//It does an END tag (which normally doesn't increment TADR because it breaks Soul Calibur 2)
		//with a QWC of 1 (rare) so we need to increment the TADR in the case of MFIFO.
		vif1ch.tadr = vif1ch.madr;
	}
	else
	{
		tDMA_TAG* pMem = dmaGetAddr(vif1ch.madr, !vif1ch.chcr.DIR);
		VIF_LOG("Non-MFIFO Location");

		//No need to exit on non-mfifo as it is indirect anyway, so it can be transferring this while spr refills the mfifo

		if (pMem == nullptr)
			return;

		if (vif1.HasResumeOffset())
			VIF1transfer((u32*)pMem + vif1.irqoffset.value, vif1ch.qwc * 4 - vif1.irqoffset.value);
		else
			VIF1transfer((u32*)pMem, vif1ch.qwc << 2);
	}
}

void mfifoVifMaskMem(int id)
{
	switch (id)
	{
		//These five transfer data following the tag, need to check its within the buffer (Front Mission 4)
		case TAG_CNT:
		case TAG_NEXT:
		case TAG_CALL:
		case TAG_RET:
		case TAG_END:
			if (vif1ch.madr < dmacRegs.rbor.ADDR) //probably not needed but we will check anyway.
			{
				//DevCon.Warning("VIF MFIFO MADR below bottom of ring buffer, wrapping VIF MADR = %x Ring Bottom %x", vif1ch.madr, dmacRegs.rbor.ADDR);
				vif1ch.madr = qwctag(vif1ch.madr);
			}
			if (vif1ch.madr > (dmacRegs.rbor.ADDR + static_cast<u32>(dmacRegs.rbsr.RMSK))) //Usual scenario is the tag is near the end (Front Mission 4)
			{
				//DevCon.Warning("VIF MFIFO MADR outside top of ring buffer, wrapping VIF MADR = %x Ring Top %x", vif1ch.madr, (dmacRegs.rbor.ADDR + dmacRegs.rbsr.RMSK)+16);
				vif1ch.madr = qwctag(vif1ch.madr);
			}
			break;
		default:
			//Do nothing as the MADR could be outside
			break;
	}
}

void mfifoVIF1transfer()
{
	tDMA_TAG* ptag;

	g_vif1Cycles = 0;

	if (vif1ch.qwc == 0)
	{
		if (QWCinVIFMFIFO(vif1ch.tadr, 1) == 0)
		{
			VIF_LOG("VIF MFIFO Empty before tag");
			vif1.SetMfifoAwaitingData(true);
			g_vif1Cycles += 4;
			return;
		}

		vif1ch.tadr = qwctag(vif1ch.tadr);
		ptag = dmaGetAddr(vif1ch.tadr, false);

		if (dmacRegs.ctrl.STD == STD_VIF1 && (ptag->ID == TAG_REFS))
		{
			Console.WriteLn("VIF MFIFO DMA Stall not implemented - Report which game to PCSX2 Team");
		}

		if (vif1ch.chcr.TTE)
		{
			bool ret;

			alignas(16) static u128 masked_tag;

			masked_tag._u64[0] = 0;
			masked_tag._u64[1] = *((u64*)ptag + 1);

			VIF_LOG("\tVIF1 SrcChain TTE=1, data = 0x%08x.%08x", masked_tag._u32[3], masked_tag._u32[2]);

			if (vif1.HasResumeOffset())
			{
				ret = VIF1transfer((u32*)&masked_tag + vif1.GetResumeOffset(), 4 - vif1.GetResumeOffset(), true); //Transfer Tag on stall
				//ret = VIF1transfer((u32*)ptag + (2 + vif1.irqoffset), 2 - vif1.irqoffset);  //Transfer Tag on stall
			}
			else
			{
				vif1.SetResumeOffset(2);
				ret = VIF1transfer((u32*)&masked_tag + 2, 2, true); //Transfer Tag
			}

			if (!ret && vif1.HasResumeOffset())
			{
				vif1.SetTransferActive(false);
				return; //IRQ set by VIFTransfer
			}
			g_vif1Cycles += 2;
		}

		vif1.ClearResumeOffset();

		vif1ch.unsafeTransfer(ptag);

		vif1ch.madr = ptag[1]._u32;

		VIF_LOG("dmaChain %8.8x_%8.8x size=%d, id=%d, madr=%lx, tadr=%lx spr0 madr = %x",
			ptag[1]._u32, ptag[0]._u32, vif1ch.qwc, ptag->ID, vif1ch.madr, vif1ch.tadr, spr0ch.madr);

		vif1.done |= hwDmacSrcChainWithStack(vif1ch, ptag->ID);

		mfifoVifMaskMem(ptag->ID);

		if (vif1ch.chcr.TIE && ptag->IRQ)
		{
			VIF_LOG("dmaIrq Set");
			vif1.done = true;
		}

		vif1ch.tadr = qwctag(vif1ch.tadr);

		if (vif1ch.qwc > 0)
			vif1.SetTransferActive(true);
	}
	else
	{
		DevCon.Warning("Vif MFIFO QWC not 0 on tag");
	}


	VIF_LOG("mfifoVIF1transfer end %x madr %x, tadr %x", vif1ch.chcr._u32, vif1ch.madr, vif1ch.tadr);
}

void vifMFIFOInterrupt()
{
	g_vif1Cycles = 0;
	VIF_LOG("vif mfifo interrupt");

	if (dmacRegs.ctrl.MFD != MFD_VIF1)
	{
		vif1Interrupt();
		return;
	}

	vif1ClearCompletedMfifoPath2DownloadWait();

	if (vif1TryHandleMfifoGsDownloadPathWait())
		return;

	const VifBridgeStateSnapshot bridge = vif1CaptureRuntimeBridgeState();

	if (vif1TryHandleMfifoVuExecutionWait(bridge))
		return;

	if (vif1TryHoldMfifoIrqStall())
		return;

	vif1UpdateMfifoVpsAfterInterruptProgress();

	if (vif1TryHandleMfifoEmptyBridgeWait(bridge))
		return;

	vif1.ClearStallState();

	if (!vif1.done || vif1ch.qwc)
	{
		switch (bridge.HasAny(VIF_BRIDGE_TRANSFER_ACTIVE) ? 1 : 0)
		{
			case 0: //Set up transfer
				mfifoVIF1transfer();
				vif1RefreshMfifoActiveTransferFqc();
				[[fallthrough]];

			case 1: //Transfer data
			{
				const VifBridgeStateSnapshot runtime_bridge = vif1CaptureRuntimeBridgeState();
				if (runtime_bridge.HasAny(VIF_BRIDGE_TRANSFER_ACTIVE)) //Just in case the tag breaks early (or something wierd happens)!
					mfifo_VIF1chain();
				const VifBridgeStateSnapshot post_chain_bridge = vif1CaptureRuntimeBridgeState();
				//Sanity check! making sure we always have non-zero values
				if (!vif1HasActiveBridgeGifWait(post_chain_bridge)) //If we're waiting on GIF, stop looping, (can be over 1000 loops!)
				{
					const u32 progress_cycles = (g_vif1Cycles == 0 ? 4 : g_vif1Cycles);
					vif1ScheduleBridgeProgress(post_chain_bridge, progress_cycles);
				}

				vif1RefreshMfifoActiveTransferFqc();
				return;
			}
		}
		return;
	}

	vif1FinalizeCompletedMfifoInterrupt();
}
