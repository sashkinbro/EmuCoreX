// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Config.h"
#include "R3000A.h"
#include "Vif.h"
#include "VU.h"
#include "arm64/OaknutHelpers-arm64.h"
#include "arm64/ee/iCore-arm64.h"
#include "arm64/ee/iR5900-arm64.h"

#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

u16 g_x86AllocCounter = 0;
u16 g_xmmAllocCounter = 0;

EEINST* g_pCurInstInfo = NULL;

_xmmregs xmmregs[iREGCNT_XMM], s_saveXMMregs[iREGCNT_XMM];

// ARM64 host GPR caching
_x86regs x86regs[iREGCNT_GPR], s_saveX86regs[iREGCNT_GPR];

static void eeSharedLoadFpu32ToXmm_emit_oaknut(int xmmreg, int fpreg)
{
	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, fpuRegs.fpr[fpreg].f))});
	oakAsm->FMOV(oakSRegister(xmmreg), OAK_WSCRATCH);
	recEndOaknutEmit();
}

static void eeSharedLoadFpuAcc32ToXmm_emit_oaknut(int xmmreg)
{
	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, fpuRegs.ACC.f))});
	oakAsm->FMOV(oakSRegister(xmmreg), OAK_WSCRATCH);
	recEndOaknutEmit();
}

static void eeSharedZeroXmm128_emit_oaknut(int xmmreg)
{
	const oak::QReg reg = oakQRegister(xmmreg);
	recBeginOaknutEmit();
	oakAsm->EOR(reg.B16(), reg.B16(), reg.B16());
	recEndOaknutEmit();
}

static void eeSharedLoadGpr128ToXmm_emit_oaknut(int xmmreg, int gprreg)
{
	recBeginOaknutEmit();
	oakLoad128(oakQRegister(xmmreg), {oak::util::X27, eeGpr128Offset(gprreg)});
	recEndOaknutEmit();
}

static void eeSharedLoadGpr128ConstLoToXmm_emit_oaknut(int xmmreg, int gprreg, u64 value)
{
	recBeginOaknutEmit();
	oakLoad128(oakQRegister(xmmreg), {oak::util::X27, eeGpr128Offset(gprreg)});
	oakAsm->MOV(OAK_XSCRATCH, value);
	oakAsm->INS(oakQRegister(xmmreg).Delem()[0], OAK_XSCRATCH);
	recEndOaknutEmit();
}

static void eeSharedLoadGpr128HostLoToXmm_emit_oaknut(int xmmreg, int gprreg, int hostreg)
{
	recBeginOaknutEmit();
	oakLoad128(oakQRegister(xmmreg), {oak::util::X27, eeGpr128Offset(gprreg)});
	oakAsm->INS(oakQRegister(xmmreg).Delem()[0], oakXRegister(hostreg));
	recEndOaknutEmit();
}

static void eeSharedStoreGpr64FromHost_emit_oaknut(int guestreg, int hostreg)
{
	recBeginOaknutEmit();
	oakStore64(oakXRegister(hostreg), {oak::util::X27, eeGpr128Offset(guestreg)});
	recEndOaknutEmit();
}

static void eeSharedStorePsx32FromHost_emit_oaknut(int guestreg, int hostreg)
{
	recBeginOaknutEmit();
	oakStore32(oakWRegister(hostreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[guestreg]))});
	recEndOaknutEmit();
}

static void eeSharedStoreGpr128FromXmm_emit_oaknut(int guestreg, int xmmreg)
{
	recBeginOaknutEmit();
	oakStore128(oakQRegister(xmmreg), {oak::util::X27, eeGpr128Offset(guestreg)});
	recEndOaknutEmit();
}

static void eeSharedStoreFpu32FromXmm_emit_oaknut(int fpreg, int xmmreg)
{
	recBeginOaknutEmit();
	oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(xmmreg));
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, fpuRegs.fpr[fpreg]))});
	recEndOaknutEmit();
}

static void eeSharedStoreFpuAcc32FromXmm_emit_oaknut(int xmmreg)
{
	recBeginOaknutEmit();
	oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(xmmreg));
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, fpuRegs.ACC.f))});
	recEndOaknutEmit();
}

static void eeSharedStoreVu0I32FromXmm_emit_oaknut(int xmmreg)
{
	recBeginOaknutEmit();
	oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(xmmreg));
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_I].F))});
	recEndOaknutEmit();
}

static void eeSharedStoreVu0Acc128FromXmm_emit_oaknut(int xmmreg)
{
	recBeginOaknutEmit();
	oakStore128(oakQRegister(xmmreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].ACC.F))});
	recEndOaknutEmit();
}

static void eeSharedStoreVu0Vf128FromXmm_emit_oaknut(int vfreg, int xmmreg)
{
	recBeginOaknutEmit();
	oakStore128(oakQRegister(xmmreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VF[vfreg].F))});
	recEndOaknutEmit();
}

static void eeSharedLoadVu0I32ToXmm_emit_oaknut(int xmmreg)
{
	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_I].F))});
	oakAsm->FMOV(oakSRegister(xmmreg), OAK_WSCRATCH);
	recEndOaknutEmit();
}

static void eeSharedLoadVu0Acc128ToXmm_emit_oaknut(int xmmreg)
{
	recBeginOaknutEmit();
	oakLoad128(oakQRegister(xmmreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].ACC.F))});
	recEndOaknutEmit();
}

static void eeSharedLoadVu0Vf128ToXmm_emit_oaknut(int vfreg, int xmmreg)
{
	recBeginOaknutEmit();
	oakLoad128(oakQRegister(xmmreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VF[vfreg].F))});
	recEndOaknutEmit();
}

// Clear current register mapping structure
// Clear allocation counter
void _initXMMregs()
{
	std::memset(xmmregs, 0, sizeof(xmmregs));
	g_xmmAllocCounter = 0;
}

bool _isAllocatableX86reg(int x86reg)
{
	// we use rax, rcx and rdx as scratch (they have special purposes...)
	if (x86reg <= 2)
		return false;

	// We keep the first two argument registers free.
	// On windows, this is ecx/edx, and it's taken care of above, but on Linux, it uses rsi/rdi.
	// The issue is when we do a load/store, the address register overlaps a cached register.
	// TODO(Stenzek): Rework loadstores to handle this and allow caching.
//	if (x86reg == arg1reg.GetId() || x86reg == arg2reg.GetId())
//		return false;

//	// arg3reg is also used for dispatching without fastmem
//		return false;

//	// rbp is used as the fastmem base
////    if (CHECK_FASTMEM && x86reg == 20)
//		return false;

#ifdef ENABLE_VTUNE
	// vtune needs ebp...
	if (!CHECK_FASTMEM && x86reg == 5)
		return false;
#endif

	// rsp is never allocatable..
	if (x86reg == 4)
		return false;

	// Pinned EE JIT registers: X19=fastmem base, X24=RECCYCLE, X25=old fastmem base,
	// X26=mVU clamp base, X27=cpuRegistersPack, X28=psxRegs, X29=recLUT.
	// X26 must stay reserved: real-game COP2 testing showed that allocating it lets
	// a guest value replace the clamp base used by subsequently generated VU code.
	if (x86reg == 19 || x86reg == 24 || x86reg == 25 || x86reg == 26 || x86reg == 27 || x86reg == 28 || x86reg == 29)
		return false;

	return true;
}

bool _hasX86reg(int type, int reg, int required_mode /*= 0*/)
{
	for (auto & x86reg : x86regs)
	{
		if (x86reg.inuse && x86reg.type == type && x86reg.reg == reg)
		{
			return ((x86reg.mode & required_mode) == required_mode);
		}
	}

	return false;
}

// Get the index of a free register
// Step1: check any available register (inuse == 0)
// Step2: check registers that are not live (both EEINST_LIVE* are cleared)
// Step3: check registers that won't use SSE in the future (likely broken as EEINST_XMM isn't set properly)
// Step4: take a randome register
//
// Note: I don't understand why we don't check register that aren't useful anymore
// (i.e EEINST_USED is cleared)
int _getFreeXMMreg(u32 maxreg)
{
    int i, tempi;
	u32 bestcount = 0x10000, e = maxreg;

	// check for free registers
	for (i = 0; i < e; ++i)
	{
#if defined(__ANDROID__)
		// xmm8/xmm9 (s8/s9) are reserved as pinned FPU clamp constants
		// (+FLT_MAX / -FLT_MAX) loaded once at JIT entry. Skip them to
		// prevent the allocator from clobbering the pinned values.
		if (i == 8 || i == 9)
			continue;
#endif
		if (!xmmregs[i].inuse)
			return i;
	}

	// check for dead regs
	tempi = -1;
	bestcount = 0xffff;
    for (i = 0; i < e; ++i)
	{
#if defined(__ANDROID__)
		if (i == 8 || i == 9)
			continue;
#endif
		pxAssert(xmmregs[i].inuse);
		if (xmmregs[i].needed)
			continue;

		// temps should be needed
		pxAssert(xmmregs[i].type != XMMTYPE_TEMP);

		if (xmmregs[i].counter < bestcount)
		{
			switch (xmmregs[i].type)
			{
				case XMMTYPE_GPRREG:
				{
					if (EEINST_USEDTEST(xmmregs[i].reg))
						continue;
				}
				break;

				case XMMTYPE_FPREG:
				{
					if (FPUINST_USEDTEST(xmmregs[i].reg))
						continue;
				}
				break;

				case XMMTYPE_VFREG:
				{
					if (EEINST_VFUSEDTEST(xmmregs[i].reg))
						continue;
				}
				break;
			}

			tempi = i;
			bestcount = xmmregs[i].counter;
		}
	}
	if (tempi != -1)
	{
		_freeXMMreg(tempi);
		return tempi;
	}

	// lastly, try without the used check
	bestcount = 0xffff;
    for (i = 0; i < e; ++i)
	{
#if defined(__ANDROID__)
		if (i == 8 || i == 9)
			continue;
#endif
		pxAssert(xmmregs[i].inuse);
		if (xmmregs[i].needed)
			continue;

		if (xmmregs[i].counter < bestcount)
		{
			tempi = i;
			bestcount = xmmregs[i].counter;
		}
	}

	if (tempi != -1)
	{
		_freeXMMreg(tempi);
		return tempi;
	}

	pxFailRel("*PCSX2*: XMM Reg Allocation Error in _getFreeXMMreg()!");
	return -1;
}

// Reserve a ARM64 vector register for temporary operation.
int _allocTempXMMreg(XMMSSEType type)
{
	const int xmmreg = _getFreeXMMreg();
	xmmregs[xmmreg].inuse = 1;
	xmmregs[xmmreg].type = XMMTYPE_TEMP;
	xmmregs[xmmreg].needed = 1;
	xmmregs[xmmreg].counter = g_xmmAllocCounter++;
	g_xmmtypes[xmmreg] = type;
	return xmmreg;
}

// Search register "reg" of type "type" which is inuse
// If register doesn't have the read flag but mode is read
// then populate the register from the memory
// Note: There is a special HALF mode (to handle low 64 bits copy) but it seems to be unused
//
// So basically it is mostly used to set the mode of the register, and load value if we need to read it
int _checkXMMreg(int type, int reg, int mode)
{
    u32 i;
	for (i = 0; i < iREGCNT_XMM; ++i)
	{
		if (xmmregs[i].inuse && (xmmregs[i].type == (type & 0xff)) && (xmmregs[i].reg == reg))
		{
			// shouldn't have dirty constants...
			pxAssert(type != XMMTYPE_GPRREG || !GPR_IS_DIRTY_CONST(reg));

			if (type == XMMTYPE_GPRREG && !(xmmregs[i].mode & (MODE_READ | MODE_WRITE)) && (mode & MODE_READ))
				pxFailRel("Somehow ended up with an allocated xmm without mode");

			if (type == XMMTYPE_GPRREG && (mode & MODE_WRITE))
			{
				// go through the alloc path instead, because we might need to invalidate a gpr.
				return _allocGPRtoXMMreg(reg, mode);
			}

			xmmregs[i].mode |= mode;
			xmmregs[i].counter = g_xmmAllocCounter++; // update counter
			xmmregs[i].needed = 1;
			return i;
		}
	}

	return -1;
}

bool _hasXMMreg(int type, int reg, int required_mode /*= 0*/)
{
	for (auto & xmmreg : xmmregs)
	{
		if (xmmreg.inuse && xmmreg.type == type && xmmreg.reg == reg)
		{
			return ((xmmreg.mode & required_mode) == required_mode);
		}
	}

	return false;
}

// Fully allocate a FPU register
// first trial:
//     search an already reserved reg then populate it if we read it
// Second trial:
//     reserve a new reg, then populate it if we read it
//
// Note: FPU are always in ARM64 vector register
int _allocFPtoXMMreg(int fpreg, int mode)
{
    u32 i;
	for (i = 0; i < iREGCNT_XMM; ++i)
	{
		if (xmmregs[i].inuse == 0)
			continue;
		if (xmmregs[i].type != XMMTYPE_FPREG)
			continue;
		if (xmmregs[i].reg != fpreg)
			continue;

		if (!(xmmregs[i].mode & MODE_READ) && (mode & MODE_READ))
		{
			eeSharedLoadFpu32ToXmm_emit_oaknut(i, fpreg);
			xmmregs[i].mode |= MODE_READ;
		}

		g_xmmtypes[i] = XMMT_FPS;
		xmmregs[i].counter = g_xmmAllocCounter++; // update counter
		xmmregs[i].needed = 1;
		xmmregs[i].mode |= mode;
		return i;
	}

	const int xmmreg = _getFreeXMMreg();

	g_xmmtypes[xmmreg] = XMMT_FPS;
	xmmregs[xmmreg].inuse = 1;
	xmmregs[xmmreg].type = XMMTYPE_FPREG;
	xmmregs[xmmreg].reg = fpreg;
	xmmregs[xmmreg].mode = mode;
	xmmregs[xmmreg].needed = 1;
	xmmregs[xmmreg].counter = g_xmmAllocCounter++;

	if (mode & MODE_READ) {
		eeSharedLoadFpu32ToXmm_emit_oaknut(xmmreg, fpreg);
    }

	return xmmreg;
}

int _allocGPRtoXMMreg(int gprreg, int mode)
{
#define MODE_STRING(x) ((((x) & MODE_READ)) ? (((x)&MODE_WRITE) ? "readwrite" : "read") : "write")

	// is this already in a gpr?
	const int hostx86reg = _checkX86reg(X86TYPE_GPR, gprreg, MODE_READ);

    u32 i;
	for (i = 0; i < iREGCNT_XMM; ++i)
	{
		if (!xmmregs[i].inuse || xmmregs[i].type != XMMTYPE_GPRREG || xmmregs[i].reg != gprreg)
			continue;

		if (!(xmmregs[i].mode & (MODE_READ | MODE_WRITE)) && (mode & MODE_READ))
			pxFailRel("Somehow ended up with an allocated register without mode");

		if (mode & MODE_WRITE && hostx86reg >= 0)
		{
			x86regs[hostx86reg].inuse = 0;
		}

		if (mode & MODE_WRITE)
		{
			if (GPR_IS_CONST1(gprreg))
			{
				GPR_DEL_CONST(gprreg);
			}
			if (hostx86reg >= 0)
			{
				// ARM64 host register should be up to date, because if it was written, it should've been invalidated
				pxAssert(!(x86regs[hostx86reg].mode & MODE_WRITE));
				_freeX86regWithoutWriteback(hostx86reg);
			}
		}

		xmmregs[i].counter = g_xmmAllocCounter++; // update counter
		xmmregs[i].needed = true;
		xmmregs[i].mode |= mode;
		return i;
	}

	const int xmmreg = _getFreeXMMreg();

	xmmregs[xmmreg].inuse = 1;
	xmmregs[xmmreg].type = XMMTYPE_GPRREG;
	xmmregs[xmmreg].reg = gprreg;
	xmmregs[xmmreg].mode = mode;
	xmmregs[xmmreg].needed = 1;
	xmmregs[xmmreg].counter = g_xmmAllocCounter++;

	if (mode & MODE_READ)
	{
		if (gprreg == 0)
		{
			eeSharedZeroXmm128_emit_oaknut(xmmreg);
		}
		else
		{
			if (GPR_IS_CONST1(gprreg))
			{

				// load lower+upper, replace lower
				eeSharedLoadGpr128ConstLoToXmm_emit_oaknut(xmmreg, gprreg, g_cpuConstRegs[gprreg].UD[0]);
				xmmregs[xmmreg].mode |= MODE_WRITE; // reg is dirty
				g_cpuFlushedConstReg |= (1u << gprreg);

				// kill any gpr allocation which is dirty, since it's a constant value
				if (hostx86reg >= 0)
				{
					x86regs[hostx86reg].inuse = 0;
				}
			}
			else if (hostx86reg >= 0)
			{

				// load lower+upper, replace lower if dirty
				if (x86regs[hostx86reg].mode & MODE_WRITE)
				{
					eeSharedLoadGpr128HostLoToXmm_emit_oaknut(xmmreg, gprreg, hostx86reg);
					_freeX86regWithoutWriteback(hostx86reg);
					xmmregs[xmmreg].mode |= MODE_WRITE;
				}
				else
				{
					eeSharedLoadGpr128ToXmm_emit_oaknut(xmmreg, gprreg);
				}

				// if the gpr was written to (dirty), we need to invalidate it
			}
			else
			{
				// not loaded
				eeSharedLoadGpr128ToXmm_emit_oaknut(xmmreg, gprreg);
			}
		}
	}

	if (mode & MODE_WRITE && gprreg < 32 && GPR_IS_CONST1(gprreg))
	{
		GPR_DEL_CONST(gprreg);
	}
	if (mode & MODE_WRITE && hostx86reg >= 0)
	{
		_freeX86regWithoutWriteback(hostx86reg);
	}

	return xmmreg;
#undef MODE_STRING
}

// Same code as _allocFPtoXMMreg but for the FPU ACC register
// (seriously boy you could have factorized it)
int _allocFPACCtoXMMreg(int mode)
{
    u32 i;
	for (i = 0; i < iREGCNT_XMM; ++i)
	{
		if (xmmregs[i].inuse == 0)
			continue;
		if (xmmregs[i].type != XMMTYPE_FPACC)
			continue;

		if (!(xmmregs[i].mode & MODE_READ) && (mode & MODE_READ))
		{
			eeSharedLoadFpuAcc32ToXmm_emit_oaknut(i);
			xmmregs[i].mode |= MODE_READ;
		}

		g_xmmtypes[i] = XMMT_FPS;
		xmmregs[i].counter = g_xmmAllocCounter++; // update counter
		xmmregs[i].needed = 1;
		xmmregs[i].mode |= mode;
		return i;
	}

	const int xmmreg = _getFreeXMMreg();

	g_xmmtypes[xmmreg] = XMMT_FPS;
	xmmregs[xmmreg].inuse = 1;
	xmmregs[xmmreg].type = XMMTYPE_FPACC;
	xmmregs[xmmreg].mode = mode;
	xmmregs[xmmreg].needed = 1;
	xmmregs[xmmreg].reg = 0;
	xmmregs[xmmreg].counter = g_xmmAllocCounter++;

	if (mode & MODE_READ)
	{
		eeSharedLoadFpuAcc32ToXmm_emit_oaknut(xmmreg);
	}

	return xmmreg;
}

void _reallocateXMMreg(int xmmreg, int newtype, int newreg, int newmode, bool writeback /*= true*/)
{
	pxAssert(xmmreg >= 0 && xmmreg <= static_cast<int>(iREGCNT_XMM));
	_xmmregs& xr = xmmregs[xmmreg];
	if (writeback)
		_freeXMMreg(xmmreg);

	xr.inuse = true;
	xr.type = newtype;
	xr.reg = newreg;
	xr.mode = newmode;
	xr.needed = true;
}

// Mark reserved GPR reg as needed. It won't be evicted anymore.
// You must use _clearNeededXMMregs to clear the flag
void _addNeededGPRtoX86reg(int gprreg)
{
	for (auto & x86reg : x86regs)
	{
		if (x86reg.inuse == 0)
			continue;
		if (x86reg.type != X86TYPE_GPR)
			continue;
		if (x86reg.reg != gprreg)
			continue;

		x86reg.counter = g_x86AllocCounter++; // update counter
		x86reg.needed = 1;
		break;
	}
}

void _addNeededPSXtoX86reg(int gprreg)
{
	for (auto & x86reg : x86regs)
	{
		if (x86reg.inuse == 0)
			continue;
		if (x86reg.type != X86TYPE_PSX)
			continue;
		if (x86reg.reg != gprreg)
			continue;

		x86reg.counter = g_x86AllocCounter++; // update counter
		x86reg.needed = 1;
		break;
	}
}

void _addNeededGPRtoXMMreg(int gprreg)
{
	for (auto & xmmreg : xmmregs)
	{
		if (xmmreg.inuse == 0)
			continue;
		if (xmmreg.type != XMMTYPE_GPRREG)
			continue;
		if (xmmreg.reg != gprreg)
			continue;

		xmmreg.counter = g_xmmAllocCounter++; // update counter
		xmmreg.needed = 1;
		break;
	}
}

// Mark reserved FPU reg as needed. It won't be evicted anymore.
// You must use _clearNeededXMMregs to clear the flag
void _addNeededFPtoXMMreg(int fpreg)
{
	for (auto & xmmreg : xmmregs)
	{
		if (xmmreg.inuse == 0)
			continue;
		if (xmmreg.type != XMMTYPE_FPREG)
			continue;
		if (xmmreg.reg != fpreg)
			continue;

		xmmreg.counter = g_xmmAllocCounter++; // update counter
		xmmreg.needed = 1;
		break;
	}
}

// Mark reserved FPU ACC reg as needed. It won't be evicted anymore.
// You must use _clearNeededXMMregs to clear the flag
void _addNeededFPACCtoXMMreg()
{
	for (auto & xmmreg : xmmregs)
	{
		if (xmmreg.inuse == 0)
			continue;
		if (xmmreg.type != XMMTYPE_FPACC)
			continue;

		xmmreg.counter = g_xmmAllocCounter++; // update counter
		xmmreg.needed = 1;
		break;
	}
}

// Clear needed flags of all registers
// Written register will set MODE_READ (aka data is valid, no need to load it)
void _clearNeededXMMregs()
{
    u32 i;
    for (i = 0; i < iREGCNT_XMM; ++i)
    {
        if (xmmregs[i].needed)
        {
            // setup read to any just written regs
            if (xmmregs[i].inuse && (xmmregs[i].mode & MODE_WRITE))
                xmmregs[i].mode |= MODE_READ;
            xmmregs[i].needed = 0;
        }

        if (xmmregs[i].inuse)
        {
            pxAssert(xmmregs[i].type != XMMTYPE_TEMP);
        }
    }
}

// Flush is 0: _freeXMMreg. Flush in memory if MODE_WRITE. Clear inuse
// Flush is 1: Flush in memory. But register is still valid
// Flush is 2: like 0 ...
// Flush is 3: drop register content
void _deleteGPRtoX86reg(int reg, int flush)
{
    u32 i;
	for (i = 0; i < iREGCNT_GPR; ++i)
	{
		if (x86regs[i].inuse && x86regs[i].type == X86TYPE_GPR && x86regs[i].reg == reg)
		{
			switch (flush)
			{
				case DELETE_REG_FREE:
					_freeX86reg(i);
					break;
				case DELETE_REG_FLUSH:
				case DELETE_REG_FLUSH_AND_FREE:
					if (x86regs[i].mode & MODE_WRITE)
					{
						pxAssert(reg != 0);
						eeSharedStoreGpr64FromHost_emit_oaknut(reg, i);

						// get rid of MODE_WRITE since don't want to flush again
						x86regs[i].mode &= ~MODE_WRITE;
						x86regs[i].mode |= MODE_READ;
					}

					if (flush == DELETE_REG_FLUSH_AND_FREE)
						x86regs[i].inuse = 0;
					break;

				case DELETE_REG_FREE_NO_WRITEBACK:
					x86regs[i].inuse = 0;
					break;
			}

			return;
		}
	}
}

void _deletePSXtoX86reg(int reg, int flush)
{
    u32 i;
	for (i = 0; i < iREGCNT_GPR; ++i)
	{
		if (x86regs[i].inuse && x86regs[i].type == X86TYPE_PSX && x86regs[i].reg == reg)
		{
			switch (flush)
			{
				case DELETE_REG_FREE:
					_freeX86reg(i);
					break;
				case DELETE_REG_FLUSH:
				case DELETE_REG_FLUSH_AND_FREE:
					if (x86regs[i].mode & MODE_WRITE)
					{
						pxAssert(reg != 0);
						eeSharedStorePsx32FromHost_emit_oaknut(reg, i);

						// get rid of MODE_WRITE since don't want to flush again
						x86regs[i].mode &= ~MODE_WRITE;
						x86regs[i].mode |= MODE_READ;

					}

					if (flush == 2)
						x86regs[i].inuse = 0;
					break;

				case DELETE_REG_FREE_NO_WRITEBACK:
					x86regs[i].inuse = 0;
					break;
			}

			return;
		}
	}
}

void _deleteGPRtoXMMreg(int reg, int flush)
{
    u32 i;
	for (i = 0; i < iREGCNT_XMM; ++i)
	{
		if (xmmregs[i].inuse && xmmregs[i].type == XMMTYPE_GPRREG && xmmregs[i].reg == reg)
		{
			switch (flush)
			{
				case DELETE_REG_FREE:
					_freeXMMreg(i);
					break;
				case DELETE_REG_FLUSH:
				case DELETE_REG_FLUSH_AND_FREE:
					if (xmmregs[i].mode & MODE_WRITE)
					{
						pxAssert(reg != 0);

						//pxAssert( g_xmmtypes[i] == XMMT_INT );
						eeSharedStoreGpr128FromXmm_emit_oaknut(reg, i);

						// get rid of MODE_WRITE since don't want to flush again
						xmmregs[i].mode &= ~MODE_WRITE;
						xmmregs[i].mode |= MODE_READ;
					}

					if (flush == DELETE_REG_FLUSH_AND_FREE)
						xmmregs[i].inuse = 0;
					break;

				case DELETE_REG_FREE_NO_WRITEBACK:
					xmmregs[i].inuse = 0;
					break;
			}

			return;
		}
	}
}

// Flush is 0: _freeXMMreg. Flush in memory if MODE_WRITE. Clear inuse
// Flush is 1: Flush in memory. But register is still valid
// Flush is 2: drop register content
void _deleteFPtoXMMreg(int reg, int flush)
{
    u32 i;
	for (i = 0; i < iREGCNT_XMM; ++i)
	{
		if (xmmregs[i].inuse && xmmregs[i].type == XMMTYPE_FPREG && xmmregs[i].reg == reg)
		{
			switch (flush)
			{
				case DELETE_REG_FREE:
				case DELETE_REG_FLUSH_AND_FREE:
					_freeXMMreg(i);
					return;

				case DELETE_REG_FLUSH:
					if (xmmregs[i].mode & MODE_WRITE)
					{
						eeSharedStoreFpu32FromXmm_emit_oaknut(reg, i);
						// get rid of MODE_WRITE since don't want to flush again
						xmmregs[i].mode &= ~MODE_WRITE;
						xmmregs[i].mode |= MODE_READ;
					}
					return;

				case DELETE_REG_FREE_NO_WRITEBACK:
					xmmregs[i].inuse = 0;
					return;
			}
		}
	}
}

void _writebackXMMreg(int xmmreg)
{
	switch (xmmregs[xmmreg].type)
	{
		case XMMTYPE_VFREG:
		{
			if (xmmregs[xmmreg].reg == 33) {
				eeSharedStoreVu0I32FromXmm_emit_oaknut(xmmreg);
            }
			else if (xmmregs[xmmreg].reg == 32) {
				eeSharedStoreVu0Acc128FromXmm_emit_oaknut(xmmreg);
            }
			else if (xmmregs[xmmreg].reg > 0) {
				eeSharedStoreVu0Vf128FromXmm_emit_oaknut(xmmregs[xmmreg].reg, xmmreg);
            }
		}
		break;

		case XMMTYPE_GPRREG:
			pxAssert(xmmregs[xmmreg].reg != 0);
			eeSharedStoreGpr128FromXmm_emit_oaknut(xmmregs[xmmreg].reg, xmmreg);
			break;

		case XMMTYPE_FPREG:
			eeSharedStoreFpu32FromXmm_emit_oaknut(xmmregs[xmmreg].reg, xmmreg);
			break;

		case XMMTYPE_FPACC:
			eeSharedStoreFpuAcc32FromXmm_emit_oaknut(xmmreg);
			break;

		default:
			break;
	}
}

// Free cached register
// Step 1: flush content in memory if MODE_WRITE
// Step 2: clear 'inuse' field
void _freeXMMreg(int xmmreg)
{
	pxAssert(static_cast<uint>(xmmreg) < iREGCNT_XMM);
	if (!xmmregs[xmmreg].inuse)
		return;

	if (xmmregs[xmmreg].mode & MODE_WRITE)
		_writebackXMMreg(xmmreg);

	xmmregs[xmmreg].mode = 0;
	xmmregs[xmmreg].inuse = 0;

	if (xmmregs[xmmreg].type == XMMTYPE_VFREG)
		mVUFreeCOP2XmmReg(xmmreg);
}

void _freeXMMregWithoutWriteback(int xmmreg)
{
	pxAssert(static_cast<uint>(xmmreg) < iREGCNT_XMM);
	if (!xmmregs[xmmreg].inuse)
		return;

	xmmregs[xmmreg].mode = 0;
	xmmregs[xmmreg].inuse = 0;

	if (xmmregs[xmmreg].type == XMMTYPE_VFREG)
		mVUFreeCOP2XmmReg(xmmreg);
}

int _allocVFtoXMMreg(int vfreg, int mode)
{
	// mode == 0 is called by the microvu side, and we don't want to clash with its temps...
	if (mode != 0)
	{
        u32 i;
		for (i = 0; i < iREGCNT_XMM; ++i)
		{
			if (xmmregs[i].inuse && xmmregs[i].type == XMMTYPE_VFREG && xmmregs[i].reg == vfreg)
			{
				pxAssert(mode == 0 || xmmregs[i].mode != 0);
				xmmregs[i].counter = g_xmmAllocCounter++;
				xmmregs[i].mode |= mode;
				return i;
			}
		}
	}

	// -1 here because we don't want to allocate PQ.
	const int xmmreg = _getFreeXMMreg(iREGCNT_XMM - 1);
	xmmregs[xmmreg].inuse = true;
	xmmregs[xmmreg].type = XMMTYPE_VFREG;
	xmmregs[xmmreg].counter = g_xmmAllocCounter++;
	xmmregs[xmmreg].needed = true;
	xmmregs[xmmreg].reg = vfreg;
	xmmregs[xmmreg].mode = mode;

	if (mode & MODE_READ)
	{
		if (vfreg == 33) {
			eeSharedLoadVu0I32ToXmm_emit_oaknut(xmmreg);
        }
		else if (vfreg == 32) {
			eeSharedLoadVu0Acc128ToXmm_emit_oaknut(xmmreg);
        }
		else {
			eeSharedLoadVu0Vf128ToXmm_emit_oaknut(xmmregs[xmmreg].reg, xmmreg);
        }
	}

	return xmmreg;
}

void _flushCOP2regs()
{
    u32 i;
	for (i = 0; i < iREGCNT_XMM; ++i)
	{
		if (xmmregs[i].inuse && xmmregs[i].type == XMMTYPE_VFREG)
		{
			_freeXMMreg(i);
		}
	}
}

void _flushXMMreg(int xmmreg)
{
	if (xmmregs[xmmreg].inuse && xmmregs[xmmreg].mode & MODE_WRITE)
	{
		_writebackXMMreg(xmmreg);
		xmmregs[xmmreg].mode = (xmmregs[xmmreg].mode & ~MODE_WRITE) | MODE_READ;
	}
}

// Flush in memory all inuse registers but registers are still valid
void _flushXMMregs()
{
    u32 i;
	for (i = 0; i < iREGCNT_XMM; ++i)
    {
        _flushXMMreg(i);
    }
}

int _allocIfUsedGPRtoX86(int gprreg, int mode)
{
	const int x86reg = _checkX86reg(X86TYPE_GPR, gprreg, mode);
	if (x86reg >= 0)
		return x86reg;

	return EEINST_USEDTEST(gprreg) ? _allocX86reg(X86TYPE_GPR, gprreg, mode) : -1;
}

int _allocIfUsedVItoX86(int vireg, int mode)
{
	const int x86reg = _checkX86reg(X86TYPE_VIREG, vireg, mode);
	if (x86reg >= 0)
		return x86reg;

	// Prefer not to stop on COP2 reserved registers here.
	return EEINST_VIUSEDTEST(vireg) ? _allocX86reg(X86TYPE_VIREG, vireg, mode | MODE_COP2) : -1;
}

int _allocIfUsedGPRtoXmmReg(int gprreg, int mode)
{
	const int mmreg = _checkXMMreg(XMMTYPE_GPRREG, gprreg, mode);
	if (mmreg >= 0)
		return mmreg;

	return EEINST_XMM_USEDTEST(gprreg) ? _allocGPRtoXMMreg(gprreg, mode) : -1;
}

int _allocIfUsedFPUtoXmmReg(int fpureg, int mode)
{
	const int mmreg = _checkXMMreg(XMMTYPE_FPREG, fpureg, mode);
	if (mmreg >= 0)
		return mmreg;

	return FPUINST_USEDTEST(fpureg) ? _allocFPtoXMMreg(fpureg, mode) : -1;
}

void _recClearInst(EEINST* pinst)
{
	// we set everything as being live to begin with, since it needs to be written at the end of the block
	std::memset(pinst, 0, sizeof(EEINST));
	std::memset(pinst->regs, EEINST_LIVE, sizeof(pinst->regs));
	std::memset(pinst->fpuregs, EEINST_LIVE, sizeof(pinst->fpuregs));
	std::memset(pinst->vfregs, EEINST_LIVE, sizeof(pinst->vfregs));
	std::memset(pinst->viregs, EEINST_LIVE, sizeof(pinst->viregs));
}

// returns nonzero value if reg has been written between [startpc, endpc-4]
u32 _recIsRegReadOrWritten(EEINST* pinst, int size, u8 xmmtype, u8 reg)
{
	u32 i, inst = 1;

	while (size-- > 0)
	{
		for (i = 0; i < std::size(pinst->writeType); ++i)
		{
			if ((pinst->writeType[i] == xmmtype) && (pinst->writeReg[i] == reg))
				return inst;
		}

		for (i = 0; i < std::size(pinst->readType); ++i)
		{
			if ((pinst->readType[i] == xmmtype) && (pinst->readReg[i] == reg))
				return inst;
		}

		++inst;
		pinst++;
	}

	return 0;
}

void _recFillRegister(EEINST& pinst, int type, int reg, int write)
{
    u32 i;

	if (write)
	{
		for (i = 0; i < std::size(pinst.writeType); ++i)
		{
			if (pinst.writeType[i] == XMMTYPE_TEMP)
			{
				pinst.writeType[i] = type;
				pinst.writeReg[i] = reg;
				return;
			}
		}
		pxAssume(false);
	}
	else
	{
		for (i = 0; i < std::size(pinst.readType); ++i)
		{
			if (pinst.readType[i] == XMMTYPE_TEMP)
			{
				pinst.readType[i] = type;
				pinst.readReg[i] = reg;
				return;
			}
		}
		pxAssume(false);
	}
}
