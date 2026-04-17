// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "GS.h"
#include "Gif_Unit.h"
#include "Vif_Dma.h"

// A three-way toggle used to determine if the GIF is stalling (transferring) or done (finished).
// Should be a gifstate_t rather then int, but I don't feel like possibly interfering with savestates right now.


alignas(16) GIF_Fifo gif_fifo;
alignas(16) gifStruct gif;

static __fi void GifDMAInt(int cycles);
static __fi void CalculateFIFOCSR();
bool CheckPaths();
void GIFdma();
void mfifoGIFtransfer();

static void gifLogPath3Sync(const char* event)
{
	static u32 s_gif_path3_logs = 0;
	if (s_gif_path3_logs++ >= 64)
		return;

	Console.WriteLn(
		"GIFPath3[%s] cycle=%u apath=%u p3state=%u masked=%d canPath3=%d gifqwc=%x fifo=%d vifVGW=%d vifqwc=%x vifstat=%08x",
		event, cpuRegs.cycle, gifRegs.stat.APATH, static_cast<u32>(gifUnit.gifPath[GIF_PATH_3].state),
		gifUnit.Path3Masked() ? 1 : 0, gifUnit.CanDoPath3() ? 1 : 0, gifch.qwc, gif_fifo.fifoSize,
		vif1HasGifWait() ? 1 : 0, vif1ch.qwc, vif1Regs.stat._u32);
}

static bool gifShouldStallActivePath3Dma()
{
	return gifUnit.Path3Masked() || !gifUnit.CanDoPath3();
}

static void gifApplyPath3DmaStallState(EE_EventType dma_event)
{
	CPU_SET_DMASTALL(dma_event, gifShouldStallActivePath3Dma());
}

static bool gifShouldKickDmaLoopAfterPath3Resume(bool require_pending_work)
{
	if (gifUnit.Path3Masked() && gifch.qwc != 0)
		return false;

	return !require_pending_work || gifch.chcr.STR || gif_fifo.fifoSize;
}

static bool gifTryResumeVifFromPath3Idle(const char* event, bool require_pending_work)
{
	if (gifUnit.gifPath[GIF_PATH_3].state != GIF_PATH_IDLE)
		return false;

	if (!vif1RequestResumeFromGifPathIdle(1))
		return false;

	gifLogPath3Sync(event);
	if (gifShouldKickDmaLoopAfterPath3Resume(require_pending_work))
		GifDMAInt(16);

	return true;
}

static bool gifHasPendingPath3Transfer()
{
	return (gifch.qwc > 0) || !gif.gspath3done;
}

static void gifRefreshActiveDmaFifoStatus()
{
	gifRegs.stat.FQC = std::min((u32)0x10, gifch.qwc);
	CalculateFIFOCSR();
}

static bool gifTryPauseDmaForPse(EE_EventType dma_event)
{
	if (!gifRegs.ctrl.PSE)
		return false;

	GifDMAInt(16);
	CPU_SET_DMASTALL(dma_event, true);
	return true;
}

static bool gifTryRunPendingPath3Transfer(EE_EventType dma_event, void (*transfer_fn)())
{
	if (!gifHasPendingPath3Transfer())
		return false;

	transfer_fn();
	gifApplyPath3DmaStallState(dma_event);
	return true;
}

static void gifFinalizeCompletedDmaInterrupt()
{
	gif.gscycles = 0;
	gifch.chcr.STR = false;
	gifRegs.stat.FQC = gif_fifo.fifoSize;
	CalculateFIFOCSR();
	hwDmacIrq(DMAC_GIF);

	if (gif_fifo.fifoSize)
		GifDMAInt(8 * BIAS);
}

static bool gifTryHandleCompletedMfifoDrain()
{
	if (!(spr0ch.madr == gifch.tadr || (gif.gifstate & GIF_STATE_EMPTY)))
		return false;

	gif.gifstate = GIF_STATE_EMPTY;
	FireMFIFOEmpty();

	if (gifHasPendingPath3Transfer())
	{
		CPU_SET_DMASTALL(DMAC_MFIFO_GIF, true);
		return true;
	}

	return false;
}

static void gifFinalizeCompletedMfifoInterrupt()
{
	gif.gscycles = 0;
	gifch.chcr.STR = false;
	gif.gifstate = GIF_STATE_READY;
	gifRegs.stat.FQC = gif_fifo.fifoSize;
	CalculateFIFOCSR();
	hwDmacIrq(DMAC_GIF);
	CPU_SET_DMASTALL(DMAC_MFIFO_GIF, false);

	if (gif_fifo.fifoSize)
		GifDMAInt(8 * BIAS);
}

static void gifRefreshPath3DoneFromChainTag(const tDMA_TAG* ptag)
{
	gif.gspath3done = hwDmacSrcChainWithStack(gifch, ptag->ID);
}

static void gifApplyTieBitPath3Completion(const tDMA_TAG* ptag)
{
	if (gifch.chcr.TIE && ptag->IRQ)
		gif.gspath3done = true;
}

static void gifApplyChainTagToActiveTransfer(const tDMA_TAG* ptag, u32& cycle_counter)
{
	gifch.madr = ptag[1]._u32; // MADR = ADDR field + SPR
	gifRefreshActiveDmaFifoStatus();
	cycle_counter += 2; // Add 1 cycles from the QW read for the tag.
	gifRefreshPath3DoneFromChainTag(ptag);
}

static bool gifComputeInitialPath3DoneForDmaStart()
{
	if (gifch.chcr.MOD == NORMAL_MODE)
		return true;

	if (gifch.chcr.MOD == CHAIN_MODE && gifch.qwc > 0)
	{
		const auto tag = gifch.chcr.tag();
		return (tag.ID == TAG_REFE) || (tag.ID == TAG_END) || (tag.IRQ && gifch.chcr.TIE);
	}

	return false;
}

static bool gifHandleQueuedSignalForDma(EE_EventType dma_event, bool allow_fifo_drain_when_not_full)
{
	if (!gifUnit.gsSIGNAL.queued)
		return false;

	GifDMAInt(128);
	CPU_SET_DMASTALL(dma_event, true);
	return !allow_fifo_drain_when_not_full || gif_fifo.fifoSize == 16;
}

static bool gifDrainFifoAndApplyPath3Stall(EE_EventType dma_event)
{
	if (gif_fifo.fifoSize == 0)
		return false;

	const int readSize = gif_fifo.read_fifo();
	if (readSize)
		GifDMAInt(readSize * BIAS);

	// If the DMA is masked/blocked and the fifo is full, no need to run the DMA.
	// If we just read from the fifo, we want to loop and not read more DMA.
	// If there is no DMA data waiting and the DMA is active, let the DMA progress until there is.
	if ((!CheckPaths() && gif_fifo.fifoSize == 16) || readSize)
	{
		gifApplyPath3DmaStallState(dma_event);
		return true;
	}

	return false;
}

static __fi void GifDMAInt(int cycles)
{
	if (dmacRegs.ctrl.MFD == MFD_GIF)
	{
		if (!(cpuRegs.interrupt & (1 << DMAC_MFIFO_GIF)) || cpuRegs.eCycle[DMAC_MFIFO_GIF] < (u32)cycles)
		{
			CPU_INT(DMAC_MFIFO_GIF, cycles);
		}
	}
	else if (!(cpuRegs.interrupt & (1 << DMAC_GIF)) || cpuRegs.eCycle[DMAC_GIF] < (u32)cycles)
	{
		CPU_INT(DMAC_GIF, cycles);
	}
}
__fi void clearFIFOstuff(bool full)
{
	CSRreg.FIFO = full ? CSR_FIFO_FULL : CSR_FIFO_EMPTY;
}

//I suspect this is GS side which should really be handled by GS which also doesn't current have a fifo, but we can guess from our fifo
static __fi void CalculateFIFOCSR()
{
	if (gifRegs.stat.FQC >= 15)
	{
		CSRreg.FIFO = CSR_FIFO_FULL;
	}
	else if (gifRegs.stat.FQC == 0)
	{
		CSRreg.FIFO = CSR_FIFO_EMPTY;
	}
	else
	{
		CSRreg.FIFO = CSR_FIFO_NORMAL;
	}
}


bool CheckPaths()
{
	// Can't do Path 3, so try dma again later...
	if (!gifUnit.CanDoPath3())
	{
		if (!gifUnit.Path3Masked())
		{
			//DevCon.Warning("Path3 stalled APATH %x PSE %x DIR %x Signal %x", gifRegs.stat.APATH, gifRegs.stat.PSE, gifRegs.stat.DIR, gifUnit.gsSIGNAL.queued);
			GifDMAInt(128);
		}
		return false;
	}
	return true;
}

void GIF_Fifo::init()
{
	std::memset(data, 0, sizeof(data));
	fifoSize = 0;
	gifRegs.stat.FQC = 0;

	gif.gifstate = GIF_STATE_READY;
	gif.gspath3done = true;

	gif.gscycles = 0;
	gif.prevcycles = 0;
	gif.mfifocycles = 0;
}


int GIF_Fifo::write_fifo(u32* pMem, int size)
{
	if (fifoSize == 16)
	{
		//GIF_LOG("GIF FIFO Full");
		return 0;
	}

	const int transferSize = std::min(size, 16 - (int)fifoSize);

	int writePos = fifoSize * 4;

	memcpy(&data[writePos], pMem, transferSize * 16);

	fifoSize += transferSize;

	gifRegs.stat.FQC = fifoSize;
	CalculateFIFOCSR();

	return transferSize;
}

int GIF_Fifo::read_fifo()
{
	if (!fifoSize || !gifUnit.CanDoPath3())
	{
		gifRegs.stat.FQC = fifoSize;
		CalculateFIFOCSR();
		if (fifoSize)
		{
			GifDMAInt(128);
		}
		return 0;
	}

	const int sizeRead = gifUnit.TransferGSPacketData(GIF_TRANS_DMA, (u8*)&data, fifoSize * 16) / 16; //returns the size actually read

	if (sizeRead < (int)fifoSize)
	{
		if (sizeRead > 0)
		{
			const int copyAmount = fifoSize - sizeRead;
			const int readpos = sizeRead * 4;

			for (int i = 0; i < copyAmount; i++)
				CopyQWC(&data[i * 4], &data[readpos + (i * 4)]);

			fifoSize = copyAmount;

		}
	}
	else
	{
		fifoSize = 0;
	}

	gifRegs.stat.FQC = fifoSize;
	CalculateFIFOCSR();

	return sizeRead;
}

void incGifChAddr(u32 qwc)
{
	if (gifch.chcr.STR)
	{
		gifch.madr += qwc * 16;
		gifch.qwc -= qwc;
		hwDmacSrcTadrInc(gifch);
	}
}

__fi void gifCheckPathStatus(bool calledFromGIF)
{
	// If GIF is running on it's own, let it handle its own timing.
	if (calledFromGIF && gifch.chcr.STR)
	{
		if (gif_fifo.fifoSize == 16)
			GifDMAInt(16);
		return;
	}

	// Required for Path3 Masking timing!
	if (gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_WAIT)
		gifUnit.gifPath[GIF_PATH_3].state = GIF_PATH_IDLE;

	if (gifRegs.stat.APATH == 3)
	{
		gifRegs.stat.APATH = 0;
		gifRegs.stat.OPH = 0;

		if (!calledFromGIF && (gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_IDLE || gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_WAIT))
		{
			if (gifUnit.checkPaths(1, 1, 0))
				gifUnit.Execute(false, true);
		}
	}

	// GIF DMA isn't running but VIF might be waiting on PATH3 so resume it here
	if (calledFromGIF)
	{
		gifTryResumeVifFromPath3Idle("ResumeVIF", true);
	}
}

__fi void gifInterrupt()
{
	gifCheckPathStatus(false);

	if (gifTryResumeVifFromPath3Idle("InterruptResumeVIF", false))
	{
		gifApplyPath3DmaStallState(DMAC_GIF);
		return;
	}

	if (dmacRegs.ctrl.MFD == MFD_GIF)
	{ // GIF MFIFO
		//Console.WriteLn("GIF MFIFO");
		gifMFIFOInterrupt();
		return;
	}

	if (gifHandleQueuedSignalForDma(DMAC_GIF, true))
		return;

	if (gifDrainFifoAndApplyPath3Stall(DMAC_GIF))
		return;

	if (!(gifch.chcr.STR))
		return;

	if (gifHasPendingPath3Transfer())
	{
		if (!dmacRegs.ctrl.DMAE)
		{
			// Re-raise the int shortly in the future
			GifDMAInt(64);
			CPU_SET_DMASTALL(DMAC_GIF, true);
			return;
		}
		gifTryRunPendingPath3Transfer(DMAC_GIF, GIFdma);
		return;
	}

	gifFinalizeCompletedDmaInterrupt();
}

static u32 WRITERING_DMA(u32* pMem, u32 qwc)
{
	const u32 originalQwc = qwc;

	if (gifRegs.stat.IMT)
	{
		// Splitting by 8qw can be really slow, so on bigger packets be less picky.
		// Games seem to be more concerned with other channels finishing before PATH 3 finishes
		// so we can get away with transferring "most" of it when it's a big packet.
		// Use Wallace and Gromit Project Zoo or The Suffering for testing
		if (qwc > 64)
			qwc = qwc * 0.5f;
		else
			qwc = std::min(qwc, 8u);
	}
	// If the packet is larger than 8qw, try to time the packet somewhat so any "finish" signals don't fire way too early and GIF syncs with other units.
	// (Mana Khemia exhibits flickering characters without).
	else if (qwc > 8)
		qwc -= 8;

	uint size;

	if (CheckPaths() == false || ((qwc < 8 || gif_fifo.fifoSize > 0) && CHECK_GIFFIFOHACK))
	{
		if (gif_fifo.fifoSize < 16)
		{
			size = gif_fifo.write_fifo((u32*)pMem, originalQwc); // Use original QWC here, the intermediate mode is for the GIF unit, not DMA
			incGifChAddr(size);
			return size;
		}
		return 4; // Arbitrary value, probably won't schedule a DMA anwyay since the FIFO is full and GIF is paused
	}

	size = gifUnit.TransferGSPacketData(GIF_TRANS_DMA, (u8*)pMem, qwc * 16) / 16;
	incGifChAddr(size);
	return size;
}

static __fi void GIFchain()
{
	tDMA_TAG* pMem;

	pMem = dmaGetAddr(gifch.madr, false);
	if (pMem == NULL)
	{
		// Must increment madr and clear qwc, else it loops
		gifch.madr += gifch.qwc * 16;
		gifch.qwc = 0;
		return;
	}

	const int transferred = WRITERING_DMA((u32*)pMem, gifch.qwc);
	gif.gscycles += transferred * BIAS;

	if (!gifUnit.Path3Masked() || (gif_fifo.fifoSize < 16))
		GifDMAInt(gif.gscycles);
}

static __fi tDMA_TAG* ReadTag()
{
	tDMA_TAG* ptag = dmaGetAddr(gifch.tadr, false); // Set memory pointer to TADR

	if (!(gifch.transfer("Gif", ptag)))
		return NULL;

	gifApplyChainTagToActiveTransfer(ptag, gif.gscycles);
	return ptag;
}

static bool gifTryHandleStdDrainStall()
{
	if (!((dmacRegs.ctrl.STD == STD_GIF) && (gif.prevcycles != 0)))
		return false;

	if ((gifch.madr + (gifch.qwc * 16)) > dmacRegs.stadr.ADDR)
	{
		GifDMAInt(4);
		CPU_SET_DMASTALL(DMAC_GIF, true);
		gif.gscycles = 0;
		return true;
	}

	gif.prevcycles = 0;
	gifch.qwc = 0;
	return false;
}

static tDMA_TAG* gifTryReadPendingChainTag()
{
	if ((gifch.chcr.MOD != CHAIN_MODE) || gif.gspath3done || gifch.qwc != 0)
		return nullptr;

	tDMA_TAG* ptag = ReadTag();
	if (ptag == nullptr)
		return nullptr;

	gifRefreshActiveDmaFifoStatus();
	return ptag;
}

static bool gifTryHandleStdStallOnChainTag(const tDMA_TAG* ptag)
{
	if (dmacRegs.ctrl.STD != STD_GIF)
		return false;

	if ((ptag->ID != TAG_REFS) || ((gifch.madr + (gifch.qwc * 16)) <= dmacRegs.stadr.ADDR))
		return false;

	gif.prevcycles = gif.gscycles;
	gifch.tadr -= 16;
	gifch.qwc = 0;
	hwDmacIrq(DMAC_STALL_SIS);
	GifDMAInt(128);
	gif.gscycles = 0;
	CPU_SET_DMASTALL(DMAC_GIF, true);
	return true;
}

void GIFdma()
{
	while (gifHasPendingPath3Transfer())
	{
		gif.gscycles = gif.prevcycles;

		if (gifTryPauseDmaForPse(DMAC_GIF))
			return;

		if (gifTryHandleStdDrainStall())
			return;

		if (tDMA_TAG* ptag = gifTryReadPendingChainTag())
		{
			if (gifTryHandleStdStallOnChainTag(ptag))
				return;

			gifApplyTieBitPath3Completion(ptag);
		}
		else if (dmacRegs.ctrl.STD == STD_GIF && gifch.chcr.MOD == NORMAL_MODE)
		{
		}

		// Transfer Dn_QWC from Dn_MADR to GIF
		if (gifch.qwc > 0) // Normal Mode
		{
			GIFchain(); // Transfers the data set by the switch
			gifApplyPath3DmaStallState(DMAC_GIF);
			return;
		}
	}

	gif.prevcycles = 0;
	GifDMAInt(16);
}

void dmaGIF()
{
	// DevCon.Warning("dmaGIFstart chcr = %lx, madr = %lx, qwc  = %lx\n tadr = %lx, asr0 = %lx, asr1 = %lx", gifch.chcr._u32, gifch.madr, gifch.qwc, gifch.tadr, gifch.asr0, gifch.asr1);
	gif.gspath3done = gifComputeInitialPath3DoneForDmaStart();
	CPU_SET_DMASTALL(DMAC_GIF, false);

	gifInterrupt();
}

static u32 QWCinGIFMFIFO(u32 DrainADDR)
{
	u32 ret;

	// Calculate what we have in the fifo.
	if (DrainADDR <= spr0ch.madr)
	{
		// Drain is below the write position, calculate the difference between them
		ret = (spr0ch.madr - DrainADDR) >> 4;
	}
	else
	{
		u32 limit = dmacRegs.rbor.ADDR + dmacRegs.rbsr.RMSK + 16;
		// Drain is higher than SPR so it has looped round,
		// calculate from base to the SPR tag addr and what is left in the top of the ring
		ret = ((spr0ch.madr - dmacRegs.rbor.ADDR) + (limit - DrainADDR)) >> 4;
	}
	if (ret == 0)
		gif.gifstate = GIF_STATE_EMPTY;

	return ret;
}

static __fi bool mfifoGIFrbTransfer()
{
	const u32 qwc = std::min(QWCinGIFMFIFO(gifch.madr), gifch.qwc);

	if (qwc == 0) // Either gifch.qwc is 0 (shouldn't get here) or the FIFO is empty.
		return true;

	u8* src = (u8*)PSM(gifch.madr);
	if (src == NULL)
		return false;

	const u32 MFIFOUntilEnd = ((dmacRegs.rbor.ADDR + dmacRegs.rbsr.RMSK + 16) - gifch.madr) >> 4;
	const bool needWrap = MFIFOUntilEnd < qwc;
	const u32 firstTransQWC = needWrap ? MFIFOUntilEnd : qwc;
	const u32 transferred = WRITERING_DMA((u32*)src, firstTransQWC); // First part

	gifch.madr = dmacRegs.rbor.ADDR + (gifch.madr & dmacRegs.rbsr.RMSK);
	gifch.tadr = dmacRegs.rbor.ADDR + (gifch.tadr & dmacRegs.rbsr.RMSK);

	if (needWrap && transferred == MFIFOUntilEnd)
	{
		src = (u8*)PSM(dmacRegs.rbor.ADDR);
		if (src == NULL)
			return false;

		// Need to do second transfer to wrap around
		const uint secondTransQWC = qwc - MFIFOUntilEnd;
		const u32 transferred2 = WRITERING_DMA((u32*)src, secondTransQWC); // Second part

		gif.mfifocycles += (transferred2 + transferred) * 2;
	}
	else
		gif.mfifocycles += transferred * 2;

	return true;
}

static __fi void mfifoGIFchain()
{
	// Is QWC = 0? if so there is nothing to transfer
	if (gifch.qwc == 0)
	{
		gif.mfifocycles += 4;
		return;
	}

	if ((gifch.madr & ~dmacRegs.rbsr.RMSK) == dmacRegs.rbor.ADDR)
	{
		if (QWCinGIFMFIFO(gifch.madr) == 0)
		{
			gif.gifstate = GIF_STATE_EMPTY;
			gif.mfifocycles += 4;
			return;
		}

		if (!mfifoGIFrbTransfer())
		{
			gif.mfifocycles += 4;
			gifch.qwc = 0;
			gif.gspath3done = true;
			return;
		}

		// This ends up being done more often but it's safer :P
		// Make sure we wrap the addresses, dont want it being stuck outside the ring when reading from the ring!
		gifch.madr = dmacRegs.rbor.ADDR + (gifch.madr & dmacRegs.rbsr.RMSK);
		gifch.tadr = gifch.madr;
	}
	else
	{
		tDMA_TAG* pMem = dmaGetAddr(gifch.madr, false);
		if (pMem == NULL)
		{
			gif.mfifocycles += 4;
			gifch.qwc = 0;
			gif.gspath3done = true;
			return;
		}

		gif.mfifocycles += WRITERING_DMA((u32*)pMem, gifch.qwc) * 2;
	}

	return;
}

static u32 qwctag(u32 mask)
{
	return (dmacRegs.rbor.ADDR + (mask & dmacRegs.rbsr.RMSK));
}

void mfifoGifMaskMem(int id)
{
	switch (id)
	{
		// These five transfer data following the tag, need to check its within the buffer (Front Mission 4)
		case TAG_CNT:
		case TAG_NEXT:
		case TAG_CALL:
		case TAG_RET:
		case TAG_END:
			if (gifch.madr < dmacRegs.rbor.ADDR) // Probably not needed but we will check anyway.
			{
				gifch.madr = qwctag(gifch.madr);
			}
			else if (gifch.madr > (dmacRegs.rbor.ADDR + (u32)dmacRegs.rbsr.RMSK)) // Usual scenario is the tag is near the end (Front Mission 4)
			{
				gifch.madr = qwctag(gifch.madr);
			}
			break;
		default:
			// Do nothing as the MADR could be outside
			break;
	}
}

void mfifoGIFtransfer()
{
	gif.mfifocycles = 0;

	if (gifTryPauseDmaForPse(DMAC_MFIFO_GIF))
		return;

	if (gifch.qwc == 0)
	{
		gifch.tadr = qwctag(gifch.tadr);

		if (QWCinGIFMFIFO(gifch.tadr) == 0)
		{
			gif.gifstate = GIF_STATE_EMPTY;
			GifDMAInt(4);
			CPU_SET_DMASTALL(DMAC_MFIFO_GIF, true);
			return;
		}

		tDMA_TAG* ptag = dmaGetAddr(gifch.tadr, false);
		gifch.unsafeTransfer(ptag);
		gifApplyChainTagToActiveTransfer(ptag, gif.mfifocycles);

		if (dmacRegs.ctrl.STD == STD_GIF && (ptag->ID == TAG_REFS))
		{
		}
		mfifoGifMaskMem(ptag->ID);

		gifch.tadr = qwctag(gifch.tadr);

		gifApplyTieBitPath3Completion(ptag);
	}

	mfifoGIFchain();

	GifDMAInt(std::max(gif.mfifocycles, (u32)4));

}

void gifMFIFOInterrupt()
{
	//DevCon.Warning("gifMFIFOInterrupt");
	gif.mfifocycles = 0;

	if (dmacRegs.ctrl.MFD != MFD_GIF)
	{ // GIF not in MFIFO anymore, come out.
		gifInterrupt();
		CPU_SET_DMASTALL(DMAC_MFIFO_GIF, true);
		return;
	}

	gifCheckPathStatus(false);

	if (gifTryResumeVifFromPath3Idle("MFIFOResumeVIF", false))
	{
		gifApplyPath3DmaStallState(DMAC_MFIFO_GIF);
		return;
	}

	if (gifHandleQueuedSignalForDma(DMAC_MFIFO_GIF, false))
		return;

	if (gifDrainFifoAndApplyPath3Stall(DMAC_MFIFO_GIF))
		return;

	if (!gifch.chcr.STR)
		return;

	if (gifTryHandleCompletedMfifoDrain())
		return;

	if (gifTryRunPendingPath3Transfer(DMAC_MFIFO_GIF, mfifoGIFtransfer))
		return;

	gifFinalizeCompletedMfifoInterrupt();
}

bool SaveStateBase::gifDmaFreeze()
{
	// Note: mfifocycles is not a persistent var, so no need to save it here.
	if (!FreezeTag("GIFdma"))
		return false;

	Freeze(gif);
	Freeze(gif_fifo);

	return IsOkay();
}
