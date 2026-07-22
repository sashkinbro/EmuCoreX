// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0

#include "arm64/vu/Vif_UnpackNEON-arm64.h"
#include "MTVU.h"

#include "common/Assertions.h"
#include "common/Perf.h"
#include "common/StringUtil.h"

static void vifStoreLane32(oak::QReg reg, int lane, OakMemOperand addr)
{
	oakAsm->UMOV(OAK_WSCRATCH2, reg.Selem()[lane]);
	oakStore32(OAK_WSCRATCH2, addr);
}

static void vifStoreLow32(oak::QReg reg, OakMemOperand addr)
{
	oakAsm->FMOV(OAK_WSCRATCH2, reg.toS());
	oakStore32(OAK_WSCRATCH2, addr);
}

static void vifStoreLow64(oak::QReg reg, OakMemOperand addr)
{
	oakAsm->FMOV(OAK_XSCRATCH2, reg.toD());
	oakStore64(OAK_XSCRATCH2, addr);
}

static void vifStoreHigh64(oak::QReg reg, OakMemOperand addr)
{
	oakAsm->UMOV(OAK_XSCRATCH2, reg.Delem()[1]);
	oakStore64(OAK_XSCRATCH2, addr);
}

static void vifLoadQFromAddress(oak::QReg dst, const void* addr)
{
	oakMoveAddressToReg(OAK_XSCRATCH2, addr);
	oakAsm->LDR(dst, OAK_XSCRATCH2);
}

static void vifStoreQToAddress(oak::QReg src, const void* addr)
{
	oakMoveAddressToReg(OAK_XSCRATCH2, addr);
	oakAsm->STR(src, OAK_XSCRATCH2);
}

static void mVUmergeRegs(oak::QReg dest, oak::QReg src, int xyzw, bool modXYZW = false, bool canModifySrc = false)
{
	xyzw &= 0xf;
	if ((dest.index() != src.index()) && (xyzw != 0))
	{
		if (xyzw == 0x8)
			oakAsm->MOV(dest.Selem()[0], src.Selem()[0]);
		else if (xyzw == 0xf)
			oakAsm->MOV(dest.B16(), src.B16());
		else
		{
			if (modXYZW)
			{
				if (xyzw == 1)
				{
					oakAsm->MOV(dest.Selem()[3], src.Selem()[0]);
					return;
				}
				else if (xyzw == 2)
				{
					oakAsm->MOV(dest.Selem()[2], src.Selem()[0]);
					return;
				}
				else if (xyzw == 4)
				{
					oakAsm->MOV(dest.Selem()[1], src.Selem()[0]);
					return;
				}
			}

			if (xyzw == 0)
				return;
			if (xyzw == 15)
			{
				oakAsm->MOV(dest.B16(), src.B16());
				return;
			}
			if (xyzw == 14 && canModifySrc)
			{
				// xyz - we can get rid of the mov if we swap the RA around
				oakAsm->MOV(src.Selem()[3], dest.Selem()[3]);
				oakAsm->MOV(dest.B16(), src.B16());
				return;
			}

			// reverse
			xyzw = ((xyzw & 1) << 3) | ((xyzw & 2) << 1) | ((xyzw & 4) >> 1) | ((xyzw & 8) >> 3);

			if ((xyzw & 3) == 3)
			{
				// xy
				oakAsm->MOV(dest.Delem()[0], src.Delem()[0]);
				xyzw &= ~3;
			}
			else if ((xyzw & 12) == 12)
			{
				// zw
				oakAsm->MOV(dest.Delem()[1], src.Delem()[1]);
				xyzw &= ~12;
			}

			// xyzw
			for (u32 i = 0; i < 4; i++)
			{
				if (xyzw & (1u << i))
					oakAsm->MOV(dest.Selem()[i], src.Selem()[i]);
			}
		}
	}
}

static void maskedVecWrite(oak::QReg reg, OakMemOperand addr, int xyzw)
{
	switch (xyzw)
	{
		case 5: // YW
			vifStoreLane32(reg, 1, {addr.base, addr.offset + 4}); // Y
			vifStoreLane32(reg, 3, {addr.base, addr.offset + 12}); // W
			break;

		case 9: // XW
			vifStoreLow32(reg, addr); // X
			vifStoreLane32(reg, 3, {addr.base, addr.offset + 12}); // W
			break;

		case 10: //XZ
			vifStoreLow32(reg, addr); // X
			vifStoreLane32(reg, 2, {addr.base, addr.offset + 8}); // Z
			break;

		case 3: // ZW
			vifStoreHigh64(reg, {addr.base, addr.offset + 8});
			break;

		case 11: //XZW
			vifStoreLow32(reg, addr); // X
			vifStoreHigh64(reg, {addr.base, addr.offset + 8}); // ZW
			break;

		case 13: // XYW
			vifStoreLow64(reg, addr);
			vifStoreLane32(reg, 3, {addr.base, addr.offset + 12});
			break;

		case 6: // YZ
			vifStoreLane32(reg, 1, {addr.base, addr.offset + 4});
			vifStoreLane32(reg, 2, {addr.base, addr.offset + 8});
			break;

		case 7: // YZW
			vifStoreLane32(reg, 1, {addr.base, addr.offset + 4});
			vifStoreHigh64(reg, {addr.base, addr.offset + 8});
			break;

		case 12: // XY
			vifStoreLow64(reg, addr);
			break;

		case 14: // XYZ
			vifStoreLow64(reg, addr);
			vifStoreLane32(reg, 2, {addr.base, addr.offset + 8}); // Z
			break;

		case 4:
			vifStoreLane32(reg, 1, {addr.base, addr.offset + 4});
			break; // Y
		case 2:
			vifStoreLane32(reg, 2, {addr.base, addr.offset + 8});
			break; // Z
		case 1:
			vifStoreLane32(reg, 3, {addr.base, addr.offset + 12});
			break; // W
		case 8:
			vifStoreLow32(reg, addr);
			break; // X

		case 0:
			Console.Error("maskedVecWrite case 0!");
			break;

		default:
			oakStore128(reg, addr);
			break; // XYZW
	}
}

void dVifReset(int idx)
{
	u8* const old_high_water = nVif[idx].recWritePtr;
	nVif[idx].vifBlocks.reset();

	const size_t offset = idx ? HostMemoryMap::VIF1recOffset : HostMemoryMap::VIF0recOffset;
	const size_t size = idx ? HostMemoryMap::VIF1recSize : HostMemoryMap::VIF0recSize;
	nVif[idx].recWritePtr = SysMemory::GetCodePtr(offset);
	nVif[idx].recEndPtr = nVif[idx].recWritePtr + (size - _256kb);
	SysMemory::DiscardCodeCachePages(nVif[idx].recWritePtr, old_high_water);
}

void dVifRelease(int idx)
{
	nVif[idx].vifBlocks.clear();
}

VifUnpackNEON_Dynarec::VifUnpackNEON_Dynarec(const nVifStruct& vif_, const nVifBlock& vifBlock_)
	: v(vif_)
	, vB(vifBlock_)
{
	const int wl = vB.wl ? vB.wl : 256; //0 is taken as 256 (KH2)
	isFill = (vB.cl < wl);
	usn = (vB.upkType >> 5) & 1;
	doMask = (vB.upkType >> 4) & 1;
	doMode = vB.mode & 3;
	IsAligned = vB.aligned;
	vCL = 0;
}

__fi void makeMergeMask(u32& x)
{
	x = ((x & 0x40) >> 6) | ((x & 0x10) >> 3) | (x & 4) | ((x & 1) << 3);
}

__fi void VifUnpackNEON_Dynarec::SetMasks(int cS) const
{
	const int idx = v.idx;
	const vifStruct& vif = MTVU_VifX;

	//This could have ended up copying the row when there was no row to write.1810080
	u32 m0 = vB.mask; //The actual mask example 0x03020100
	u32 m3 = ((m0 & 0xaaaaaaaa) >> 1) & ~m0; //all the upper bits, so our example 0x01010000 & 0xFCFDFEFF = 0x00010000 just the cols (shifted right for maskmerge)
	u32 m2 = (m0 & 0x55555555) & (~m0 >> 1); // 0x1000100 & 0xFE7EFF7F = 0x00000100 Just the row

	if ((doMask && m2) || doMode)
	{
		vifLoadQFromAddress(VIF_Q_ROW, &vif.MaskRow);
	}
	if (doMask && m3)
	{
		vifLoadQFromAddress(VIF_Q_COL0, &vif.MaskCol);
		if ((cS >= 2) && (m3 & 0x0000ff00))
			oakAsm->DUP(VIF_Q_COL1.S4(), VIF_Q_COL0.Selem()[1]);
		if ((cS >= 3) && (m3 & 0x00ff0000))
			oakAsm->DUP(VIF_Q_COL2.S4(), VIF_Q_COL0.Selem()[2]);
		if ((cS >= 4) && (m3 & 0xff000000))
			oakAsm->DUP(VIF_Q_COL3.S4(), VIF_Q_COL0.Selem()[3]);
		if ((cS >= 1) && (m3 & 0x000000ff))
			oakAsm->DUP(VIF_Q_COL0.S4(), VIF_Q_COL0.Selem()[0]);
	}
	//if (doMask||doMode) loadRowCol((nVifStruct&)v);
}

void VifUnpackNEON_Dynarec::doMaskWrite(oak::QReg regX) const
{
	pxAssertMsg(regX.index() <= 1, "Reg Overflow! XMM2 thru XMM6 are reserved for masking.");

	const int cc = std::min(vCL, 3);
	u32 m0 = (vB.mask >> (cc * 8)) & 0xff; //The actual mask example 0xE4 (protect, col, row, clear)
	u32 m3 = ((m0 & 0xaa) >> 1) & ~m0; //all the upper bits (cols shifted right) cancelling out any write protects 0x10
	u32 m2 = (m0 & 0x55) & (~m0 >> 1); // all the lower bits (rows)cancelling out any write protects 0x04
	u32 m4 = (m0 & ~((m3 << 1) | m2)) & 0x55; //  = 0xC0 & 0x55 = 0x40 (for merge mask)

	makeMergeMask(m2);
	makeMergeMask(m3);
	makeMergeMask(m4);

	if (doMask && m2) // Merge MaskRow
	{
		mVUmergeRegs(regX, VIF_Q_ROW, m2);
	}

	if (doMask && m3) // Merge MaskCol
	{
		mVUmergeRegs(regX, oakQRegister(VIF_Q_COL0.index() + cc), m3);
	}

	if (doMode)
	{
		u32 m5 = ~(m2 | m3 | m4) & 0xf;

		if (!doMask)
			m5 = 0xf;

		if (m5 < 0xf)
		{
			oakAsm->MOVI(VIF_Q_TEMP.S4(), 0);
			if (doMode == 3)
			{
				mVUmergeRegs(VIF_Q_ROW, regX, m5, false, false);
			}
			else
			{
				mVUmergeRegs(VIF_Q_TEMP, VIF_Q_ROW, m5, false, false);
				oakAsm->ADD(regX.S4(), regX.S4(), VIF_Q_TEMP.S4());
				if (doMode == 2)
					mVUmergeRegs(VIF_Q_ROW, regX, m5, false, false);
			}
		}
		else
		{
			if (doMode == 3)
			{
				oakAsm->MOV(VIF_Q_ROW.B16(), regX.B16());
			}
			else
			{
				oakAsm->ADD(regX.S4(), regX.S4(), VIF_Q_ROW.S4());
				if (doMode == 2)
				{
					oakAsm->MOV(VIF_Q_ROW.B16(), regX.B16());
				}
			}
		}
	}

	if (doMask && m4)
		maskedVecWrite(regX, dstIndirect, m4 ^ 0xf);
	else
		oakStore128(regX, dstIndirect);
}

void VifUnpackNEON_Dynarec::writeBackRow() const
{
	const int idx = v.idx;
	vifStoreQToAddress(VIF_Q_ROW, &(MTVU_VifX.MaskRow));

}

void VifUnpackNEON_Dynarec::ModUnpack(int upknum, bool PostOp)
{
	switch (upknum)
	{
		case 0:
		case 1:
		case 2:
			if (PostOp)
			{
				UnpkLoopIteration++;
				UnpkLoopIteration = UnpkLoopIteration & 0x3;
			}
			break;

		case 4:
		case 5:
		case 6:
			if (PostOp)
			{
				UnpkLoopIteration++;
				UnpkLoopIteration = UnpkLoopIteration & 0x1;
			}
			break;

		case 8:
			if (PostOp)
			{
				UnpkLoopIteration++;
				UnpkLoopIteration = UnpkLoopIteration & 0x1;
			}
			break;
		case 9:
			if (!PostOp)
			{
				UnpkLoopIteration++;
			}
			break;
		case 10:
			if (!PostOp)
			{
				UnpkLoopIteration++;
			}
			break;

		case 12:
			break;
		case 13:
			break;
		case 14:
			break;
		case 15:
			break;

		case 3:
		case 7:
		case 11:
			// TODO: Needs hardware testing.
			// Dynasty Warriors 5: Empire  - Player 2 chose a character menu.
			Console.Warning("Vpu/Vif: Invalid Unpack %d", upknum);
			break;
	}
}

void VifUnpackNEON_Dynarec::ProcessMasks()
{
	skipProcessing = false;
	inputMasked = false;

	if (!doMask)
		return;

	const int cc = std::min(vCL, 3);
	const u32 full_mask = (vB.mask >> (cc * 8)) & 0xff;
	const u32 rowcol_mask = ((full_mask >> 1) | full_mask) & 0x55; // Rows or Cols being written instead of data, or protected.

	// Every channel is write protected for this cycle, no need to process anything.
	skipProcessing = full_mask == 0xff;

	// All channels are masked, no reason to process anything here.
	inputMasked = rowcol_mask == 0x55;
}

void VifUnpackNEON_Dynarec::CompileRoutine()
{
	const int wl = vB.wl ? vB.wl : 256; //0 is taken as 256 (KH2)
	const int upkNum = vB.upkType & 0xf;
	const u8& vift = nVifT[upkNum];
	const int cycleSize = isFill ? vB.cl : wl;
	const int blockSize = isFill ? wl : vB.cl;
	const int skipSize = blockSize - cycleSize;

	uint vNum = vB.num ? vB.num : 256;
	doMode = (upkNum == 0xf) ? 0 : doMode; // V4_5 has no mode feature.
	UnpkNoOfIterations = 0;

	pxAssume(vCL == 0);

	// Value passed determines # of col regs we need to load
	SetMasks(isFill ? blockSize : cycleSize);

	while (vNum)
	{
		// Determine if reads/processing can be skipped.
		ProcessMasks();

		if (vCL < cycleSize)
		{
			ModUnpack(upkNum, false);
			xUnpack(upkNum);
			xMovDest();
			ModUnpack(upkNum, true);

			dstIndirect.offset += 16;
			srcIndirect.offset += vift;

			vNum--;
			if (++vCL == blockSize)
				vCL = 0;
		}
		else if (isFill)
		{
			xUnpack(upkNum);
			xMovDest();

			// dstIndirect += 16;
			dstIndirect.offset += 16;

			vNum--;
			if (++vCL == blockSize)
				vCL = 0;
		}
		else
		{
			// dstIndirect += (16 * skipSize);
			dstIndirect.offset += 16 * skipSize;
			vCL = 0;
		}
	}

	if (doMode >= 2)
		writeBackRow();

	oakAsm->RET();
}

static u16 dVifComputeLength(uint cl, uint wl, u8 num, bool isFill)
{
	uint length = (num > 0) ? (num * 16) : 4096; // 0 = 256

	if (!isFill)
	{
		uint skipSize = (cl - wl) * 16;
		uint blocks = (num + (wl - 1)) / wl; //Need to round up num's to calculate skip size correctly.
		length += (blocks - 1) * skipSize;
	}

	return std::min(length, 0xFFFFu);
}

_vifT __fi nVifBlock* dVifCompile(nVifBlock& block, bool isFill)
{
	nVifStruct& v = nVif[idx];

	// Check size before the compilation
	if (v.recWritePtr >= v.recEndPtr)
		dVifReset(idx);

	// recEndPtr is the reset threshold. The reserved 256 KiB remains available
	// to the block already being compiled, but Oaknut must never cross into the
	// neighboring code-cache region.
	const size_t offset = idx ? HostMemoryMap::VIF1recOffset : HostMemoryMap::VIF0recOffset;
	const size_t size = idx ? HostMemoryMap::VIF1recSize : HostMemoryMap::VIF0recSize;
	u8* const physical_end = SysMemory::GetCodePtr(offset + size);
	pxAssert(v.recWritePtr < physical_end);
	oakSetAsmPtr(v.recWritePtr, physical_end - v.recWritePtr);

	block.startPtr = (uptr)oakStartBlock();
	block.length = dVifComputeLength(block.cl, block.wl, block.num, isFill);
	v.vifBlocks.add(block);

	VifUnpackNEON_Dynarec(v, block).CompileRoutine();

	Perf::vif.RegisterPC(v.recWritePtr, oakGetCurrentCodePointer() - v.recWritePtr, block.upkType /* FIXME ideally a key*/);
	v.recWritePtr = oakEndBlock();

	return &block;
}

_vifT __fi void dVifUnpack(const u8* data, bool isFill)
{
	nVifStruct& v = nVif[idx];
	vifStruct& vif = MTVU_VifX;
	VIFregisters& vifRegs = MTVU_VifXRegs;

	const u8 upkType = (vif.cmd & 0x1f) | (vif.usn << 5);
	const int doMask = isFill ? 1 : (vif.cmd & 0x10);

	nVifBlock block;

	// Performance note: initial code was using u8/u16 field of the struct
	// directly. However reading back the data (as u32) in HashBucket.find
	// leads to various memory stalls. So it is way faster to manually build the data
	// in u32 (aka ARM64 host register).
	//
	// Warning the order of data in hash_key/key0/key1 depends on the nVifBlock struct
	u32 hash_key = (u32)(upkType & 0xFF) << 8 | (vifRegs.num & 0xFF);

	u32 key1 = ((u32)vifRegs.cycle.wl << 24) | ((u32)vifRegs.cycle.cl << 16) | ((u32)(vif.start_aligned & 0xFF) << 8) | ((u32)vifRegs.mode & 0xFF);
	if ((upkType & 0xf) != 9)
		key1 &= 0xFFFF01FF;

	// Zero out the mask parameter if it's unused -- games leave random junk
	// values here which cause false recblock cache misses.
	u32 key0 = doMask ? vifRegs.mask : 0;

	block.hash_key = hash_key;
	block.key0 = key0;
	block.key1 = key1;

	//	block.num, block.upkType, block.scl, block.cl, block.wl, block.mode,
	//	doMask >> 4, doMask ? wxsFormat( L"0x%08x", block.mask ).c_str() : L"ignored"
	//);

	// Seach in cache before trying to compile the block
	nVifBlock* b = v.vifBlocks.find(block);
	if (!b) [[unlikely]]
	{
		b = dVifCompile<idx>(block, isFill);
	}

	{ // Execute the block
		const VURegs& VU = g_cpuRegistersPack.vuRegs[idx];
		const uint vuMemLimit = idx ? 0x4000 : 0x1000;

		u8* startmem = VU.Mem + (vif.tag.addr & (vuMemLimit - 0x10));
		u8* endmem = VU.Mem + vuMemLimit;

		if ((startmem + b->length) <= endmem) [[likely]]
		{
#if 1
			// No wrapping, you can run the fast dynarec
			((nVifrecCall)b->startPtr)((uptr)startmem, (uptr)data);
#else
			// comparison mode
			static u8 tmpbuf[512 * 1024];
			((nVifrecCall)b->startPtr)((uptr)tmpbuf, (uptr)data);

			_nVifUnpack(idx, data, vifRegs.mode, isFill);

			const u32 words = b->length / 4;
			for (u32 i = 0; i < words; i++)
			{
				if (*((u32*)tmpbuf + i) != *((u32*)startmem + i))
				{
					pauseCCC(*((u32*)tmpbuf + i), *((u32*)startmem + i), i);
					((nVifrecCall)b->startPtr)((uptr)tmpbuf, (uptr)data);
					break;
				}
			}
#endif
		}
		else
		{
			_nVifUnpack(idx, data, vifRegs.mode, isFill);
		}
	}
}

template void dVifUnpack<0>(const u8* data, bool isFill);
template void dVifUnpack<1>(const u8* data, bool isFill);
