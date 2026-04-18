// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "GS.h"
#include "Gif_Unit.h"
#include "MTVU.h"
#include "VUmicro.h"
#include "Vif_Dma.h"
#include "Vif_Dynarec.h"

u32 g_vif1Cycles = 0;

static __fi EE_EventType vif1GetActiveDmaEvent()
{
	return dmacRegs.ctrl.MFD == MFD_VIF1 ? DMAC_MFIFO_VIF : DMAC_VIF1;
}

void vif1RequestDmaRetry(u32 cycles)
{
	const EE_EventType irq = vif1GetActiveDmaEvent();
	if (!(cpuRegs.interrupt & (1u << irq)) || cpuRegs.eCycle[irq] > cycles)
		CPU_INT(irq, cycles);
}

void vif1RequestDmaProgress(u32 cycles)
{
	vif1RequestDmaRetry(cycles);
}

void vif1RequestDmaProgressWithVuBusy(u32 cycles)
{
	vif1RequestDmaRetry(std::max<int>(cycles, cpuGetCycles(VU_MTVU_BUSY)));
}

static __fi void vif1SetActiveTransferFqcFromQwc()
{
	vif1Regs.stat.FQC = std::min(vif1ch.qwc, static_cast<u32>(16));
}

static __fi void vif1ClearActiveTransferFqc()
{
	vif1Regs.stat.FQC = 0;
}

static void vif1ScheduleVuFinishCheck(u32 cycles)
{
	CPU_INT(VIF_VU1_FINISH, cycles);
	CPU_SET_DMASTALL(VIF_VU1_FINISH, true);
}

void vif1SetActiveDmaStall(bool stalled)
{
	CPU_SET_DMASTALL(vif1GetActiveDmaEvent(), stalled);
}

static __fi void vif1BeginActiveDmaRetryStall(u32 cycles)
{
	vif1RequestDmaRetry(cycles);
	vif1SetActiveDmaStall(true);
}

static __fi bool vif1HasActiveDmaInterruptScheduled()
{
	const EE_EventType irq = vif1GetActiveDmaEvent();
	return (cpuRegs.interrupt & (1u << irq)) != 0;
}

static __fi void vif1DispatchActiveDmaInterrupt()
{
	if (dmacRegs.ctrl.MFD == MFD_VIF1)
		vifMFIFOInterrupt();
	else
		vif1Interrupt();
}

static __fi void vif1SetGifPathWaitState(bool waiting_for_path3)
{
	vif1Regs.stat.VGW = waiting_for_path3;
}

__fi void vif1SetupTransfer();

bool vif1RequestResumeFromGifPathIdle(u32 resumeCycles)
{
	if (!vif1HasGifWait())
		return false;

	vif1RequestDmaRetry(resumeCycles);
	return true;
}

bool vif1ResumeDmaFromGifPathSignal(u32 resumeCycles)
{
	if (!vif1HasGifWait())
		return false;

	vif1ClearGifWait();
	vif1RequestDmaRetry(resumeCycles);
	return true;
}

__fi void vif1FLUSH()
{
	if (VU0.VI[REG_VPU_STAT].UL & 0x500) // T bit stop or Busy
	{
		vif1.BeginVuWait(vif1ch.chcr.STR);
		vif1Regs.stat.VEW = true;
	}
}

void vif1TransferToMemory()
{
	u128* pMem = (u128*)dmaGetAddr(vif1ch.madr, false);

	// VIF from gsMemory
	if (pMem == nullptr)
	{ // Is vif0ptag empty?
		Console.WriteLn("Vif1 Tag BUSERR");
		dmacRegs.stat.BEIS = true; // Bus Error
		vif1ClearActiveTransferFqc();

		vif1ch.qwc = 0;
		vif1.done = true;
		vif1RequestDmaRetry(0);
		return; // An error has occurred.
	}

	// MTGS concerns:  The MTGS is inherently disagreeable with the idea of downloading
	// stuff from the GS.  The *only* way to handle this case safely is to flush the GS
	// completely and execute the transfer there-after.
	//Console.Warning("Real QWC %x", vif1ch.qwc);
	const u32 size = std::min(vif1.GSLastDownloadSize, (u32)vif1ch.qwc);
	//const u128* pMemEnd  = vif1.GSLastDownloadSize + pMem;

#ifdef PCSX2_DEVBUILD
	if (size)
	{
		// Checking if any crazy game does a partial
		// gs primitive and then does a gs download...
		Gif_Path& p1 = gifUnit.gifPath[GIF_PATH_1];
		Gif_Path& p2 = gifUnit.gifPath[GIF_PATH_2];
		Gif_Path& p3 = gifUnit.gifPath[GIF_PATH_3];
		pxAssert(p1.isDone() || !p1.gifTag.isValid);
		pxAssert(p2.isDone() || !p2.gifTag.isValid);
		pxAssert(p3.isDone() || !p3.gifTag.isValid);
	}
#endif

	MTGS::InitAndReadFIFO(reinterpret_cast<u8*>(pMem), size);
	//	pMem += size;

	//Some games such as Alex Ferguson's Player Manager 2001 reads less than GSLastDownloadSize by VIF then reads the remainder by FIFO
	//Clearing the memory is clearing memory it shouldn't be and kills it.
	//The only scenario where this could be used is the transfer size really is less than QWC, not the other way around as it was doing
	//That said, I think this is pointless and a waste of cycles and could cause more problems than good. We will alert this situation below anyway.
	/*if (vif1.GSLastDownloadSize < vif1ch.qwc) {
		if (pMem < pMemEnd) {
			DevCon.Warning("GS Transfer < VIF QWC, Clearing end of space GST %x QWC %x", vif1.GSLastDownloadSize, (u32)vif1ch.qwc);

			__m128 zeroreg = _mm_setzero_ps();
			do {
				_mm_store_ps((float*)pMem, zeroreg);
			} while (++pMem < pMemEnd);
		}
	}*/

	g_vif1Cycles += size * 2;
	vif1ch.madr += size * 16; // mgs3 scene changes
	if (vif1.GSLastDownloadSize >= vif1ch.qwc)
	{
		vif1.GSLastDownloadSize -= vif1ch.qwc;
		vif1Regs.stat.FQC = std::min(static_cast<u32>(16), vif1.GSLastDownloadSize);
		vif1ch.qwc = 0;
	}
	else
	{
		vif1ClearActiveTransferFqc();
		vif1ch.qwc -= vif1.GSLastDownloadSize;
		vif1.GSLastDownloadSize = 0;
		//This could be potentially bad and cause hangs. I guess we will find out.
		DevCon.Warning("QWC left on VIF FIFO Reverse");
	}
}

bool _VIF1chain()
{
	u32* pMem;

	if (vif1ch.qwc == 0)
	{
		vif1.SetTransferActive(false);
		vif1.ClearResumeOffset();
		return true;
	}

	// Clarification - this is TO memory mode, for some reason i used the other way round >.<
	if (vif1.dmamode == VIF_NORMAL_TO_MEM_MODE)
	{
		vif1TransferToMemory();
		vif1.SetTransferActive(false);
		return true;
	}

	pMem = (u32*)dmaGetAddr(vif1ch.madr, !vif1ch.chcr.DIR);
	if (pMem == nullptr)
	{
		vif1.cmd = 0;
		vif1.tag.size = 0;
		vif1ch.qwc = 0;
		return true;
	}

	VIF_LOG("VIF1chain size=%d, madr=%lx, tadr=%lx",
		vif1ch.qwc, vif1ch.madr, vif1ch.tadr);

	if (vif1.HasResumeOffset())
		return VIF1transfer(pMem + vif1.GetResumeOffset(), vif1ch.qwc * 4 - vif1.GetResumeOffset(), false);
	else
		return VIF1transfer(pMem, vif1ch.qwc * 4, false);
}

static bool vif1TryStallStdTransferOnRefsTag(const tDMA_TAG* ptag)
{
	if (vif1.done || dmacRegs.ctrl.STD != STD_VIF1 || ptag->ID != TAG_REFS)
		return false;

	// There are still bugs here, need to also check if gif->madr +16*qwc >= stadr, if not, stall.
	if ((vif1ch.madr + vif1ch.qwc * 16) <= dmacRegs.stadr.ADDR)
		return false;

	hwDmacIrq(DMAC_STALL_SIS);
	vif1SetActiveDmaStall(true);
	return true;
}

static bool vif1TransferChainTagPayload(const tDMA_TAG* ptag)
{
	if (!vif1ch.chcr.TTE)
		return true;

	bool ret;
	alignas(16) static u128 masked_tag;

	masked_tag._u64[0] = 0;
	masked_tag._u64[1] = *((u64*)ptag + 1);

	VIF_LOG("\tVIF1 SrcChain TTE=1, data = 0x%08x.%08x", masked_tag._u32[3], masked_tag._u32[2]);

	if (vif1.HasResumeOffset())
	{
		ret = VIF1transfer((u32*)&masked_tag + vif1.GetResumeOffset(), 4 - vif1.GetResumeOffset(), true);
	}
	else
	{
		// Some games (like killzone) do Tags mid unpack, the nops will just write blank data
		// to the VU's, which breaks stuff, this is where the 128bit packet will fail, so we ignore the first 2 words.
		vif1.SetResumeOffset(2);
		ret = VIF1transfer((u32*)&masked_tag + 2, 2, true);
	}

	if (ret || !vif1.HasResumeOffset())
		return true;

	vif1.SetTransferActive(false); // Better clear this so it has to do it again (Jak 1)
	vif1ch.qwc = 0; // Gumball 3000 pauses the DMA when the tag stalls so we need to reset the QWC, it'll be gotten again later
	return false;
}

static void vif1FinalizeTransferSetupFromTag(const tDMA_TAG* ptag)
{
	vif1.ClearResumeOffset();

	vif1.done |= hwDmacSrcChainWithStack(vif1ch, ptag->ID);

	if (vif1ch.qwc > 0)
		vif1.SetTransferActive(true);

	// Check TIE bit of CHCR and IRQ bit of tag.
	if (vif1ch.chcr.TIE && ptag->IRQ)
	{
		VIF_LOG("dmaIrq Set");
		vif1.done = true;
	}
}

static void vif1ScheduleDmaProgressFromCurrentState()
{
	if (vif1Regs.stat.VGW && gifUnit.gifPath[GIF_PATH_3].state != GIF_PATH_IDLE)
		return;

	if (vif1.waitforvu)
		vif1RequestDmaProgress(std::max(static_cast<int>(g_vif1Cycles), cpuGetCycles(VU_MTVU_BUSY)));
	else
		vif1RequestDmaProgress(g_vif1Cycles);
}

static void vif1ClearCompletedPath2DownloadWait()
{
	if (gifRegs.stat.APATH != 2 || !gifUnit.gifPath[GIF_PATH_2].isDone())
		return;

	gifRegs.stat.APATH = 0;
	gifRegs.stat.OPH = 0;
	vif1SetGifPathWaitState(false); // Let vif continue if it's stuck on a flush.

	if (gifUnit.checkPaths(1, 0, 1))
		gifUnit.Execute(false, true);
}

static bool vif1TryHandleMfifoInterrupt()
{
	// Some games start VIF before MFIFO mode fully settles, so handle that path early.
	if (dmacRegs.ctrl.MFD != MFD_VIF1)
		return false;

	vif1SetActiveTransferFqcFromQwc();
	vifMFIFOInterrupt();
	return true;
}

static bool vif1TryHandleGsDownloadPathWait()
{
	if (!vif1ch.chcr.DIR)
		return false;

	const bool isDirect = (vif1.cmd & 0x7f) == 0x50;
	const bool isDirectHL = (vif1.cmd & 0x7f) == 0x51;
	if ((isDirect && !gifUnit.CanDoPath2()) || (isDirectHL && !gifUnit.CanDoPath2HL()))
	{
		GUNIT_WARN("vif1Interrupt() - Waiting for Path 2 to be ready");
		vif1SetGifPathWaitState(gifRegs.stat.APATH == 3); // Gunslinger II waits on Path 3.
		vif1BeginActiveDmaRetryStall(128);
		return true;
	}

	vif1SetGifPathWaitState(false); // Path 3 isn't busy so we don't need to wait for it.
	vif1SetActiveTransferFqcFromQwc();
	return false;
}

static bool vif1TryHandleVuExecutionWait()
{
	if (!vif1.IsWaitingForVu())
		return false;

	vif1ScheduleVuFinishCheck(std::max(16, cpuGetCycles(VU_MTVU_BUSY)));
	vif1SetActiveDmaStall(true);
	return true;
}

static bool vif1TryRescheduleActiveVuThreadWait()
{
	if (!(VU0.VI[REG_VPU_STAT].UL & 0x500))
		return false;

	vu1Thread.Get_MTVUChanges();

	if (THREAD_VU1 && !INSTANT_VU1 && (VU0.VI[REG_VPU_STAT].UL & 0x100))
		vif1ScheduleVuFinishCheck(cpuGetCycles(VU_MTVU_BUSY));
	else
		vif1ScheduleVuFinishCheck(128);

	return true;
}

static bool vif1TryFinishActiveVuExecution()
{
	if (!(VU0.VI[REG_VPU_STAT].UL & 0x100))
		return false;

	const u64 cycle_start = VU1.cycle;
	vu1Finish(false);

	if (THREAD_VU1 && !INSTANT_VU1 && (VU0.VI[REG_VPU_STAT].UL & 0x100))
		vif1ScheduleVuFinishCheck(cpuGetCycles(VU_MTVU_BUSY));
	else
		vif1ScheduleVuFinishCheck(VU1.cycle - cycle_start);

	return true;
}

static void vif1ResumeDmaAfterVuCompletion()
{
	if (!vif1.IsWaitingForVu())
		return;

	vif1.EndVuWait();

	// Check if VIF is already scheduled to interrupt, if it's waiting, kick it.
	if (!vif1HasActiveDmaInterruptScheduled() && vif1ch.chcr.STR &&
		!vif1Regs.stat.test(VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS))
	{
		vif1DispatchActiveDmaInterrupt();
	}
}

static void vif1RefreshActiveTransferFqc()
{
	if (vif1ch.chcr.DIR)
		vif1SetActiveTransferFqcFromQwc();
}

static void vif1UpdateVpsAfterInterruptProgress()
{
	// Mirroring change to VIF0.
	if (vif1.cmd)
	{
		if (vif1.done && (vif1ch.qwc == 0))
			vif1Regs.stat.VPS = VPS_WAITING;
	}
	else
	{
		vif1Regs.stat.VPS = VPS_IDLE;
	}
}

static bool vif1ShouldHoldCompletedDmaOnStall()
{
	return vif1.IsWaitingForStallState() && vif1.done;
}

static bool vif1TryHandleIrqStall()
{
	if (!(vif1.irq && vif1.IsIrqStalled()))
		return false;

	VIF_LOG("VIF IRQ Firing");
	if (!vif1Regs.stat.ER1)
		vif1Regs.stat.INT = true;

	// Yakuza watches VIF_STAT so lets do this here.
	if (((vif1Regs.code >> 24) & 0x7f) != 0x7)
	{
		vif1Regs.stat.VIS = true;
	}

	hwIntcIrq(VIF1intc);
	--vif1.irq;

	if (!vif1Regs.stat.test(VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS))
		return false;

	// NFSHPS stalls when the whole packet has gone across (it stalls in the last 32bit cmd).
	// In this case VIF will end.
	vif1RefreshActiveTransferFqc();
	if ((vif1ch.qwc > 0 || !vif1.done) && !CHECK_VIF1STALLHACK)
	{
		vif1Regs.stat.VPS = VPS_DECODING; // Onimusha - Blade Warriors
		VIF_LOG("VIF1 Stalled");
		vif1SetActiveDmaStall(true);
		return true;
	}

	return false;
}

static void vif1ContinueActiveTransferProgress()
{
	_VIF1chain();

	// VIF_NORMAL_FROM_MEM_MODE is a very slow operation.
	// Timesplitters 2 depends on this being a bit higher than 128.
	vif1RefreshActiveTransferFqc();
	vif1ScheduleDmaProgressFromCurrentState();
}

static bool vif1TryAdvanceTransferSetup()
{
	if (vif1.done)
		return false;

	if (!(dmacRegs.ctrl.DMAE) || vif1Regs.stat.VSS) // Stopped or DMA Disabled
		return true;

	if (!vif1.IsTransferActive())
		vif1SetupTransfer();

	vif1RefreshActiveTransferFqc();
	vif1ScheduleDmaProgressFromCurrentState();
	return true;
}

static void vif1FinalizeCompletedDmaInterrupt()
{
	if ((vif1ch.chcr.DIR == VIF_NORMAL_TO_MEM_MODE) && vif1.GSLastDownloadSize <= 16)
	{
		// Reverse fifo has finished and nothing is left, so lets clear the outputting flag.
		gifRegs.stat.OPH = false;
	}

	vif1RefreshActiveTransferFqc();

	vif1ch.chcr.STR = false;
	vif1.CompleteDmaTransfer();
	if (vif1.HasQueuedProgram())
		vifExecQueue(1);
	g_vif1Cycles = 0;
	VIF_LOG("VIF1 DMA End");
	hwDmacIrq(DMAC_VIF1);
	vif1SetActiveDmaStall(false);
}

__fi void vif1SetupTransfer()
{
	tDMA_TAG* ptag;

	ptag = dmaGetAddr(vif1ch.tadr, false); //Set memory pointer to TADR

	if (!(vif1ch.transfer("Vif1 Tag", ptag)))
		return;

	vif1ch.madr = ptag[1]._u32; //MADR = ADDR field + SPR
	g_vif1Cycles += 1; // Add 1 g_vifCycles from the QW read for the tag
	vif1.SetTransferActive(false);

	VIF_LOG("VIF1 Tag %8.8x_%8.8x size=%d, id=%d, madr=%lx, tadr=%lx",
		ptag[1]._u32, ptag[0]._u32, vif1ch.qwc, ptag->ID, vif1ch.madr, vif1ch.tadr);

	if (vif1TryStallStdTransferOnRefsTag(ptag))
		return;

	if (!vif1TransferChainTagPayload(ptag))
		return;

	vif1FinalizeTransferSetupFromTag(ptag);
}

__fi void vif1VUFinish()
{
	// Sync up VU1 so we don't errantly wait.
	while (!THREAD_VU1 && (VU0.VI[REG_VPU_STAT].UL & 0x100))
	{
		const s64 cycle_diff = static_cast<int>(cpuRegs.cycle - VU1.cycle);

		if ((EmuConfig.Gamefixes.VUSyncHack && cycle_diff < VU1.nextBlockCycles) || cycle_diff <= 0)
			break;

		CpuVU1->ExecuteBlock();
	}

	if (vif1TryRescheduleActiveVuThreadWait())
		return;

	if (vif1TryFinishActiveVuExecution())
		return;

	vif1Regs.stat.VEW = false;
	VIF_LOG("VU1 finished");
	vif1ResumeDmaAfterVuCompletion();

	//DevCon.Warning("VU1 state cleared");
}

__fi void vif1Interrupt()
{
	VIF_LOG("vif1Interrupt: %8.8llx chcr %x, done %x, qwc %x", cpuRegs.cycle, vif1ch.chcr._u32, vif1.done, vif1ch.qwc);

	g_vif1Cycles = 0;

	vif1ClearCompletedPath2DownloadWait();

	if (vif1TryHandleMfifoInterrupt())
		return;

	if (vif1TryHandleGsDownloadPathWait())
		return;

	if (vif1TryHandleVuExecutionWait())
		return;

	if (vif1Regs.stat.VGW)
	{
		vif1SetActiveDmaStall(true);
		return;
	}

	if (!vif1ch.chcr.STR)
		return;

	if (vif1TryHandleIrqStall())
		return;

	vif1.ClearStallState();

	vif1UpdateVpsAfterInterruptProgress();

	if (vif1.IsTransferActive())
	{
		vif1ContinueActiveTransferProgress();
		return;
	}

	if (vif1TryAdvanceTransferSetup())
		return;

	if (vif1ShouldHoldCompletedDmaOnStall())
	{
		vif1BeginActiveDmaRetryStall(0);
		return; //Dont want to end if vif is stalled.
	}

	vif1FinalizeCompletedDmaInterrupt();
}

void dmaVIF1()
{
	VIF_LOG("dmaVIF1 chcr = %lx, madr = %lx, qwc  = %lx\n"
			"        tadr = %lx, asr0 = %lx, asr1 = %lx",
		vif1ch.chcr._u32, vif1ch.madr, vif1ch.qwc,
		vif1ch.tadr, vif1ch.asr0, vif1ch.asr1);

	g_vif1Cycles = 0;
	vif1.PrepareForDmaStart();
	vif1SetActiveDmaStall(false);

	if (vif1ch.qwc > 0) // Normal Mode
	{

		// ignore tag if it's a GS download (Def Jam Fight for NY)
		if (vif1ch.chcr.MOD == CHAIN_MODE && vif1ch.chcr.DIR)
		{
			vif1.dmamode = VIF_CHAIN_MODE;
			//DevCon.Warning(L"VIF1 QWC on Chain CHCR " + vif1ch.chcr.desc());

			if ((vif1ch.chcr.tag().ID == TAG_REFE) || (vif1ch.chcr.tag().ID == TAG_END) || (vif1ch.chcr.tag().IRQ && vif1ch.chcr.TIE))
			{
				vif1.done = true;
			}
			else
			{
				vif1.done = false;
			}
		}
		else //Assume normal mode for reverse FIFO and Normal.
		{
			if (dmacRegs.ctrl.STD == STD_VIF1)
				Console.WriteLn("DMA Stall Control on VIF1 normal not implemented - Report which game to PCSX2 Team");

			if (vif1ch.chcr.DIR) // from Memory
				vif1.dmamode = VIF_NORMAL_FROM_MEM_MODE;
			else
				vif1.dmamode = VIF_NORMAL_TO_MEM_MODE;

			if (vif1.HasResumeOffset() && !vif1.done)
				DevCon.Warning("Warning! VIF1 starting a Normal transfer with vif offset set (Possible force stop?)");
			vif1.done = true;
		}

		vif1.SetTransferActive(true);
	}
	else
	{
		vif1.SetTransferActive(false);
		vif1.dmamode = VIF_CHAIN_MODE;
		vif1.done = false;
	}

	vif1RefreshActiveTransferFqc();

	// Check VIF isn't stalled before starting the loop.
	// Batman Vengence does something stupid and instead of cancelling a stall it tries to restart VIF, THEN check the stall
	// However if VIF FIFO is reversed, it can continue
	if (!vif1ch.chcr.DIR || !vif1Regs.stat.test(VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS))
		vif1RequestDmaProgress(4);
}
