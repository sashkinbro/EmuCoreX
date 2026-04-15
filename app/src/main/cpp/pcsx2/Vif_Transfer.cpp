// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "Vif_Dma.h"
#include "Vif_Dynarec.h"

//------------------------------------------------------------------
// VifCode Transfer Interpreter (Vif0/Vif1)
//------------------------------------------------------------------

// Interprets packet
_vifT void vifTransferLoop(u32* &data) {
	vifStruct& vifX = GetVifX;

	u32& pSize = vifX.vifpacketsize;

	int ret = 0;

	vifXRegs.stat.VPS |= VPS_TRANSFERRING;
	vifXRegs.stat.ER1  = false;
	//VIF_LOG("Starting VIF%d loop, pSize = %x, stalled = %x", idx, pSize, vifX.vifstalled.enabled );
	while (pSize > 0 && !vifX.IsWaitingForStallState()) {

		if(!vifX.cmd) { // Get new VifCode

			if(!vifXRegs.err.MII)
			{
				if(vifX.irq && !CHECK_VIF1STALLHACK)
					break;

				vifX.irq      |= data[0] >> 31;
			}

			vifXRegs.code = data[0];
			vifX.cmd	  = data[0] >> 24;


			VIF_LOG("New VifCMD %x tagsize %x irq %d", vifX.cmd, vifX.tag.size, vifX.irq);
			if (IsDevBuild && TraceLogging.EE.VIFcode.IsActive()) {
				// Pass 2 means "log it"
				vifCmdHandler[idx][vifX.cmd & 0x7f](2, data);
			}
		}

		ret = vifCmdHandler[idx][vifX.cmd & 0x7f](vifX.pass, data);
		data   += ret;
		pSize  -= ret;
		if (vifX.IsWaitingForStallState())
		{
			int current_STR = idx ? vif1ch.chcr.STR : vif0ch.chcr.STR;
			if (!current_STR)
				DevCon.Warning("Warning! VIF%d stalled during FIFO transfer!", idx);
		}
	}
}

_vifT static __fi bool vifTransfer(u32 *data, int size, bool TTE) {
	vifStruct& vifX = GetVifX;

	// irqoffset necessary to add up the right qws, or else will spin (spiderman)
	int transferred = vifX.HasResumeOffset() ? vifX.GetResumeOffset() : 0;

	vifX.vifpacketsize = size;
	vifTransferLoop<idx>(data);

	transferred += size - vifX.vifpacketsize;

	//Make this a minimum of 1 cycle so if it's the end of the packet it doesnt just fall through.
	//Metal Saga can do this, just to be safe :)
	if (!idx) g_vif0Cycles += std::max(1, (int)((transferred * BIAS) >> 2));
	else	  g_vif1Cycles += std::max(1, (int)((transferred * BIAS) >> 2));

	const u32 resume_offset = transferred % 4;
	vifX.SetResumeOffset(resume_offset); // cannot lose the offset

	if (vifX.irq && vifX.cmd == 0) {
		VIF_LOG("Vif%d IRQ Triggering", idx);
		//Always needs to be set to return to the correct offset if there is data left.
		vifX.SetIrqStall(!!vifXch.chcr.STR);
	}

	if (!TTE) // *WARNING* - Tags CAN have interrupts! so lets just ignore the dma modifying stuffs (GT4)
	{
		transferred  = transferred >> 2;
		transferred = std::min((int)vifXch.qwc, transferred);
		vifXch.madr +=(transferred << 4);
		vifXch.qwc  -= transferred;

		hwDmacSrcTadrInc(vifXch);

		if(!vifXch.qwc)
		{
			vifX.SetTransferActive(false);
			vifX.ClearResumeOffset();
		}
		else if (resume_offset != 0)
		{
			vifX.SetResumeOffset(resume_offset);
		}
		else
		{
			vifX.ClearResumeOffset();
		}
	}
	else
	{
		if (resume_offset != 0)
			vifX.SetResumeOffset(resume_offset);
		else
			vifX.ClearResumeOffset();
	}

	vifExecQueue(idx);

	return !vifX.IsWaitingForStallState();
}

// When TTE is set to 1, MADR and QWC are not updated as part of the transfer.
bool VIF0transfer(u32 *data, int size, bool TTE) {
	return vifTransfer<0>(data, size, TTE);
}
bool VIF1transfer(u32 *data, int size, bool TTE) {
	return vifTransfer<1>(data, size, TTE);
}
