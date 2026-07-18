// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "R3000A.h"
#include "VU.h"
#include "Vif.h"
#include "arm64/iop/iR3000A-arm64.h"
#include "arm64/ee/iR5900-arm64.h"
#include "arm64/OaknutHelpers-arm64.h"

#include "common/Console.h"
#include "common/emitter/x86emitter.h"

#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

// yay sloppy crap needed until we can remove dependency on this hippopotamic
// landmass of shared code. (air)
extern u32 g_psxConstRegs[32];

// ARM64 host GPR caching
static uint g_x86checknext;

// use ARM64 host register allocation

void _initX86regs()
{
	std::memset(x86regs, 0, sizeof(x86regs));
	g_x86AllocCounter = 0;
	g_x86checknext = 0;
}

int _getFreeX86reg(int mode)
{
    int i, tempi = -1;
    u32 bestcount = 0x10000;

    for (i = 0; i < iREGCNT_GPR; ++i)
    {
        const int reg = (g_x86checknext + i) % iREGCNT_GPR;
        if (x86regs[reg].inuse || !_isAllocatableX86reg(reg))
            continue;

        if (mode & MODE_CALLEESAVED) {
            if(!oakIsCalleeSavedRegister(reg)) {
                continue;
            }
        } else {
            if(!oakIsCallerSaved(reg)) {
                continue;
            }
        }

        if ((mode & MODE_COP2) && mVUIsReservedCOP2(reg))
            continue;

        if (x86regs[reg].inuse == 0)
        {
            g_x86checknext = (reg + 1) % iREGCNT_GPR;
            return reg;
        }
    }

    for (i = 0; i < iREGCNT_GPR; ++i)
    {
        if (!_isAllocatableX86reg(i))
            continue;

        if (mode & MODE_CALLEESAVED) {
            if(!oakIsCalleeSavedRegister(i)) {
                continue;
            }
        } else {
            if(!oakIsCallerSaved(i)) {
                continue;
            }
        }

        if ((mode & MODE_COP2) && mVUIsReservedCOP2(i))
            continue;

        // should have checked inuse in the previous loop.
        pxAssert(x86regs[i].inuse);

        if (x86regs[i].needed)
            continue;

        if (x86regs[i].type != X86TYPE_TEMP)
        {
            if (x86regs[i].counter < bestcount)
            {
                tempi = static_cast<int>(i);
                bestcount = x86regs[i].counter;
            }
            continue;
        }

        _freeX86reg(i);
        return i;
    }

    if (tempi != -1)
    {
        _freeX86reg(tempi);
        return tempi;
    }

	pxFailRel("ARM64 register allocation error");
	return -1;
}

static void eeCoreMoveHostXImm_emit_oaknut(int hostreg, u64 imm)
{
	recBeginOaknutEmit();
	oakAsm->MOV(oakXRegister(hostreg), imm);
	recEndOaknutEmit();
}

static void eeCoreZeroHostX_emit_oaknut(int hostreg)
{
	const oak::XReg host = oakXRegister(hostreg);
	recBeginOaknutEmit();
	oakAsm->EOR(host, host, host);
	recEndOaknutEmit();
}

static void eeCoreZeroHostW_emit_oaknut(int hostreg)
{
	const oak::WReg host = oakWRegister(hostreg);
	recBeginOaknutEmit();
	oakAsm->EOR(host, host, host);
	recEndOaknutEmit();
}

static void eeCoreNotHostX_emit_oaknut(int hostreg)
{
	const oak::XReg host = oakXRegister(hostreg);
	recBeginOaknutEmit();
	oakAsm->MVN(host, host);
	recEndOaknutEmit();
}

static void eeCoreMoveHostWImm_emit_oaknut(int hostreg, u32 imm)
{
	recBeginOaknutEmit();
	oakAsm->MOV(oakWRegister(hostreg), imm);
	recEndOaknutEmit();
}

static void eeCoreMoveHostXmm64ToHost_emit_oaknut(int hostreg, int xmmreg)
{
	recBeginOaknutEmit();
	oakAsm->FMOV(oakXRegister(hostreg), oakDRegister(xmmreg));
	recEndOaknutEmit();
}

static void eeCoreLoadGpr64ToHost_emit_oaknut(int hostreg, int guestreg)
{
	recBeginOaknutEmit();
	oakLoad64(oakXRegister(hostreg), {oak::util::X27, eeGpr128Offset(guestreg)});
	recEndOaknutEmit();
}

static void eeCoreLoadFprc32ToHost_emit_oaknut(int hostreg, int guestreg)
{
	recBeginOaknutEmit();
	oakLoad32(oakWRegister(hostreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, fpuRegs.fprc[guestreg]))});
	recEndOaknutEmit();
}

static void eeCoreLoadPsx32ToHost_emit_oaknut(int hostreg, int guestreg)
{
	recBeginOaknutEmit();
	oakLoad32(oakWRegister(hostreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[guestreg]))});
	recEndOaknutEmit();
}

static void eeCoreLoadVu0Vi16ToHost_emit_oaknut(int hostreg, int guestreg)
{
	recBeginOaknutEmit();
	oakLoad16(oakWRegister(hostreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[guestreg].US[0]))});
	recEndOaknutEmit();
}

static void eeCoreStoreGpr64FromHost_emit_oaknut(int guestreg, int hostreg)
{
	recBeginOaknutEmit();
	oakStore64(oakXRegister(hostreg), {oak::util::X27, eeGpr128Offset(guestreg)});
	recEndOaknutEmit();
}

static void eeCoreStoreGpr64Imm_emit_oaknut(int guestreg, u64 imm)
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_XSCRATCH, imm);
	oakStore64(OAK_XSCRATCH, {oak::util::X27, eeGpr128Offset(guestreg)});
	recEndOaknutEmit();
}

static void eeCoreStoreFprc32FromHost_emit_oaknut(int guestreg, int hostreg)
{
	recBeginOaknutEmit();
	oakStore32(oakWRegister(hostreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, fpuRegs.fprc[guestreg]))});
	recEndOaknutEmit();
}

static void eeCoreStoreVu0Vi16FromHost_emit_oaknut(int guestreg, int hostreg)
{
	recBeginOaknutEmit();
	oakStore16(oakWRegister(hostreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[guestreg].UL))});
	recEndOaknutEmit();
}

static void eeCoreStorePcWriteback32FromHost_emit_oaknut(int hostreg)
{
	recBeginOaknutEmit();
	oakStore32(oakWRegister(hostreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pcWriteback))});
	recEndOaknutEmit();
}

static void eeCoreStorePsx32FromHost_emit_oaknut(int guestreg, int hostreg)
{
	recBeginOaknutEmit();
	oakStore32(oakWRegister(hostreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[guestreg]))});
	recEndOaknutEmit();
}

static void eeCoreStorePsxPcWriteback32FromHost_emit_oaknut(int hostreg)
{
	recBeginOaknutEmit();
	oakStore32(oakWRegister(hostreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.pcWriteback))});
	recEndOaknutEmit();
}

void _flushConstReg(int reg)
{
	if (GPR_IS_CONST1(reg) && !(g_cpuFlushedConstReg & (1 << reg)))
	{
		eeCoreStoreGpr64Imm_emit_oaknut(reg, g_cpuConstRegs[reg].UD[0]);
		g_cpuFlushedConstReg |= (1 << reg);
		if (reg == 0)
			DevCon.Warning("Flushing r0!");
	}
}

void _flushConstRegs(bool delete_const)
{
	int zero_reg_count = 0;
	int minusone_reg_count = 0;
    u32 i;
	for (i = 0; i < 32; ++i)
	{
		if (!GPR_IS_CONST1(i) || g_cpuFlushedConstReg & (1u << i))
			continue;

		if (g_cpuConstRegs[i].SD[0] == 0)
			zero_reg_count++;
		else if (g_cpuConstRegs[i].SD[0] == -1)
			minusone_reg_count++;
	}

	// if we have more than one of zero/minus-one, precompute
	bool rax_is_zero = false;
	if (zero_reg_count > 1)
	{
		eeCoreZeroHostX_emit_oaknut(EE_HOST_RAX);
		for (i = 0; i < 32; ++i)
		{
			if (!GPR_IS_CONST1(i) || g_cpuFlushedConstReg & (1u << i))
				continue;

			if (g_cpuConstRegs[i].SD[0] == 0)
			{
				eeCoreStoreGpr64FromHost_emit_oaknut(i, EE_HOST_RAX);
				g_cpuFlushedConstReg |= 1u << i;
				if (delete_const)
					g_cpuHasConstReg &= ~(1u << i);
			}
		}
		rax_is_zero = true;
	}
	if (minusone_reg_count > 1)
	{
		if (!rax_is_zero) {
			eeCoreMoveHostXImm_emit_oaknut(EE_HOST_RAX, UINT64_MAX);
        }
		else {
			eeCoreNotHostX_emit_oaknut(EE_HOST_RAX);
        }

		for (i = 0; i < 32; ++i)
		{
			if (!GPR_IS_CONST1(i) || g_cpuFlushedConstReg & (1u << i))
				continue;

			if (g_cpuConstRegs[i].SD[0] == -1)
			{
				eeCoreStoreGpr64FromHost_emit_oaknut(i, EE_HOST_RAX);
				g_cpuFlushedConstReg |= 1u << i;
				if (delete_const)
					g_cpuHasConstReg &= ~(1u << i);
			}
		}
	}

	// and whatever's left over..
	for (i = 0; i < 32; ++i)
	{
		if (!GPR_IS_CONST1(i) || g_cpuFlushedConstReg & (1u << i))
			continue;

		eeCoreStoreGpr64Imm_emit_oaknut(i, g_cpuConstRegs[i].UD[0]);
		g_cpuFlushedConstReg |= 1u << i;
		if (delete_const)
			g_cpuHasConstReg &= ~(1u << i);
	}
}

void _validateRegs()
{
#ifdef PCSX2_DEVBUILD
#define MODE_STRING(x) ((((x) & MODE_READ)) ? (((x)&MODE_WRITE) ? "readwrite" : "read") : "write")
	// check that no two registers are in write mode in both fprs and gprs
	for (s8 guestreg = 0; guestreg < 32; guestreg++)
	{
        if(guestreg == 3) continue;

		u32 gprreg = 0, gprmode = 0;
		u32 fprreg = 0, fprmode = 0;
		for (u32 hostreg = 0; hostreg < iREGCNT_GPR; hostreg++)
		{
			if (x86regs[hostreg].inuse && x86regs[hostreg].type == X86TYPE_GPR && x86regs[hostreg].reg == guestreg)
			{
				pxAssertMsg(gprreg == 0 && gprmode == 0, "register is not already allocated in a GPR");
				gprreg = hostreg;
				gprmode = x86regs[hostreg].mode;
			}
		}
		for (u32 hostreg = 0; hostreg < iREGCNT_XMM; hostreg++)
		{
			if (xmmregs[hostreg].inuse && xmmregs[hostreg].type == XMMTYPE_GPRREG && xmmregs[hostreg].reg == guestreg)
			{
				pxAssertMsg(fprreg == 0 && fprmode == 0, "register is not already allocated in a XMM");
				fprreg = hostreg;
				fprmode = xmmregs[hostreg].mode;
			}
		}

		if ((gprmode | fprmode) & MODE_WRITE)
			pxAssertMsg((gprmode & MODE_WRITE) != (fprmode & MODE_WRITE), "only one of gpr or fps is in write state");

		if (gprmode & MODE_WRITE)
			pxAssertMsg(fprmode == 0, "when writing to the gpr, fpr is invalid");
		if (fprmode & MODE_WRITE)
			pxAssertMsg(gprmode == 0, "when writing to the fpr, gpr is invalid");
	}
#undef MODE_STRING
#endif
}

int _allocX86reg(int type, int reg, int mode)
{
	if (type == X86TYPE_GPR || type == X86TYPE_PSX)
	{
		pxAssertMsg(reg >= 0 && reg < 34, "Register index out of bounds.");
	}

	int hostXMMreg = (type == X86TYPE_GPR) ? _checkXMMreg(XMMTYPE_GPRREG, reg, 0) : -1;
	if (type != X86TYPE_TEMP)
	{
        int i, e = static_cast<int>(iREGCNT_GPR);
		for (i = 0; i < e; ++i)
		{
			if (!x86regs[i].inuse || x86regs[i].type != type || x86regs[i].reg != reg)
				continue;

			pxAssert(type != X86TYPE_GPR || !GPR_IS_CONST1(reg) || (GPR_IS_CONST1(reg) && g_cpuFlushedConstReg & (1u << reg)));

			// can't go from write to read
			pxAssert(!((x86regs[i].mode & (MODE_READ | MODE_WRITE)) == MODE_WRITE && (mode & (MODE_READ | MODE_WRITE)) == MODE_READ));
			// if (type != X86TYPE_TEMP && !(x86regs[i].mode & MODE_READ) && (mode & MODE_READ))

			if (type == X86TYPE_GPR)
			{

				if (mode & MODE_WRITE)
				{
					if (GPR_IS_CONST1(reg))
					{
						GPR_DEL_CONST(reg);
					}

					if (hostXMMreg >= 0)
					{
						// ensure upper bits get written
						pxAssert(!(xmmregs[hostXMMreg].mode & MODE_WRITE));
						_freeXMMreg(hostXMMreg);
					}
				}
			}
			else if (type == X86TYPE_PSX)
			{

				if (mode & MODE_WRITE)
				{
					if (PSX_IS_CONST1(reg))
					{
						PSX_DEL_CONST(reg);
					}
				}
			}
			else if (type == X86TYPE_VIREG)
			{
				// keep VI temporaries separate
				if (reg < 0)
					continue;
			}

			x86regs[i].counter = g_x86AllocCounter++;
			x86regs[i].mode |= mode & ~MODE_CALLEESAVED;
			x86regs[i].needed = true;
			return i;
		}
	}

	const int regnum = _getFreeX86reg(mode);
	x86regs[regnum].type = type;
	x86regs[regnum].reg = reg;
	x86regs[regnum].mode = mode & ~MODE_CALLEESAVED;
	x86regs[regnum].counter = g_x86AllocCounter++;
	x86regs[regnum].needed = true;
	x86regs[regnum].inuse = true;

	if (type == X86TYPE_GPR)
	{
	}

	if (mode & MODE_READ)
	{
		switch (type)
		{
			case X86TYPE_GPR:
			{
				if (reg == 0)
				{
					eeCoreZeroHostX_emit_oaknut(regnum);
				}
				else
				{
					if (hostXMMreg >= 0)
					{
						// is in a XMM. we don't need to free the XMM since we're not writing, and it's still valid
						eeCoreMoveHostXmm64ToHost_emit_oaknut(regnum, hostXMMreg);

						// if the XMM was dirty, just get rid of it, we don't want to try to sync the values up...
						if (xmmregs[hostXMMreg].mode & MODE_WRITE)
						{
							_freeXMMreg(hostXMMreg);
						}
					}
					else if (GPR_IS_CONST1(reg))
					{
						eeCoreMoveHostXImm_emit_oaknut(regnum, g_cpuConstRegs[reg].UD[0]);
						g_cpuFlushedConstReg |= (1u << reg);
						x86regs[regnum].mode |= MODE_WRITE; // reg is dirty

					}
					else
					{
						// not loaded
						eeCoreLoadGpr64ToHost_emit_oaknut(regnum, reg);
					}
				}
			}
			break;

			case X86TYPE_FPRC:
				eeCoreLoadFprc32ToHost_emit_oaknut(regnum, reg);
				break;

			case X86TYPE_PSX:
			{
				if (reg == 0)
				{
					eeCoreZeroHostW_emit_oaknut(regnum);
				}
				else
				{
					if (PSX_IS_CONST1(reg))
					{
						eeCoreMoveHostWImm_emit_oaknut(regnum, g_psxConstRegs[reg]);
						g_psxFlushedConstReg |= (1u << reg);
						x86regs[regnum].mode |= MODE_WRITE; // reg is dirty

					}
					else
					{
						eeCoreLoadPsx32ToHost_emit_oaknut(regnum, reg);
					}
				}
			}
			break;

			case X86TYPE_VIREG:
			{
				eeCoreLoadVu0Vi16ToHost_emit_oaknut(regnum, reg);
			}
			break;

			default:
				abort();
				break;
		}
	}

	if (type == X86TYPE_GPR && (mode & MODE_WRITE))
	{
		if (reg < 32 && GPR_IS_CONST1(reg))
		{
			GPR_DEL_CONST(reg);
		}
		if (hostXMMreg >= 0)
		{
			// writing, so kill the xmm allocation. gotta ensure the upper bits gets stored first.
			_freeXMMreg(hostXMMreg);
		}
	}
	else if (type == X86TYPE_PSX && (mode & MODE_WRITE))
	{
		if (reg < 32 && PSX_IS_CONST1(reg))
		{
			PSX_DEL_CONST(reg);
		}
	}

	return regnum;
}

void _writebackX86Reg(int x86reg)
{
	switch (x86regs[x86reg].type)
	{
		case X86TYPE_GPR:
			eeCoreStoreGpr64FromHost_emit_oaknut(x86regs[x86reg].reg, x86reg);
			break;

		case X86TYPE_FPRC:
			eeCoreStoreFprc32FromHost_emit_oaknut(x86regs[x86reg].reg, x86reg);
			break;

		case X86TYPE_VIREG:
			eeCoreStoreVu0Vi16FromHost_emit_oaknut(x86regs[x86reg].reg, x86reg);
			break;

		case X86TYPE_PCWRITEBACK:
			eeCoreStorePcWriteback32FromHost_emit_oaknut(x86reg);
			break;

		case X86TYPE_PSX:
			eeCoreStorePsx32FromHost_emit_oaknut(x86regs[x86reg].reg, x86reg);
			break;

		case X86TYPE_PSX_PCWRITEBACK:
			eeCoreStorePsxPcWriteback32FromHost_emit_oaknut(x86reg);
			break;

		default:
			abort();
			break;
	}
}

int _checkX86reg(int type, int reg, int mode)
{
    u32 i;
	for (i = 0; i < iREGCNT_GPR; ++i)
	{
		if (x86regs[i].inuse && x86regs[i].reg == reg && x86regs[i].type == type)
		{
			// shouldn't have dirty constants...
			pxAssert((type != X86TYPE_GPR || !GPR_IS_DIRTY_CONST(reg)) &&
					 (type != X86TYPE_PSX || !PSX_IS_DIRTY_CONST(reg)));

			if ((type == X86TYPE_GPR || type == X86TYPE_PSX) && !(x86regs[i].mode & MODE_READ) && (mode & MODE_READ))
				pxFailRel("Somehow ended up with an allocated ARM64 register without mode");

			// ensure constants get deleted once we alloc as write
			if (mode & MODE_WRITE)
			{
				if (type == X86TYPE_GPR)
				{
					// go through the alloc path instead, because we might need to invalidate an xmm.
					return _allocX86reg(X86TYPE_GPR, reg, mode);
				}
				else if (type == X86TYPE_PSX)
				{
					pxAssert(!PSX_IS_DIRTY_CONST(reg));
					PSX_DEL_CONST(reg);
				}
			}

			x86regs[i].mode |= mode;
			x86regs[i].counter = g_x86AllocCounter++;
			x86regs[i].needed = 1;
			return i;
		}
	}

	return -1;
}

void _addNeededX86reg(int type, int reg)
{
    u32 i;
	for (i = 0; i < iREGCNT_GPR; ++i)
	{
		if (!x86regs[i].inuse || x86regs[i].reg != reg || x86regs[i].type != type)
			continue;

		x86regs[i].counter = g_x86AllocCounter++;
		x86regs[i].needed = 1;
	}
}

void _clearNeededX86regs()
{
    u32 i;
	for (i = 0; i < iREGCNT_GPR; ++i)
	{
		if (x86regs[i].needed)
		{
			if (x86regs[i].inuse && (x86regs[i].mode & MODE_WRITE))
				x86regs[i].mode |= MODE_READ;
		}
		x86regs[i].needed = 0;
	}
}

void _freeX86reg(int x86reg)
{
	pxAssert(x86reg >= 0 && x86reg < (int)iREGCNT_GPR);

	if (x86regs[x86reg].inuse && (x86regs[x86reg].mode & MODE_WRITE))
	{
		_writebackX86Reg(x86reg);
		x86regs[x86reg].mode &= ~MODE_WRITE;
	}

	_freeX86regWithoutWriteback(x86reg);
}

void _freeX86regWithoutWriteback(int x86reg)
{
	pxAssert(x86reg >= 0 && x86reg < (int)iREGCNT_GPR);

	x86regs[x86reg].inuse = 0;

	if (x86regs[x86reg].type == X86TYPE_VIREG)
	{
		mVUFreeCOP2GPR(x86reg);
	}
}

void _freeX86regs()
{
    u32 i;
	for (i = 0; i < iREGCNT_GPR; ++i)
    {
        _freeX86reg(i);
    }
}

void _flushX86regs()
{
    u32 i;
	for (i = 0; i < iREGCNT_GPR; ++i)
	{
		if (x86regs[i].inuse && x86regs[i].mode & MODE_WRITE)
		{
			// shouldn't be const, because if we got to write mode, we should've flushed then
//			pxAssert(x86regs[i].type != X86TYPE_GPR || !GPR_IS_DIRTY_CONST(x86regs[i].reg));

			_writebackX86Reg(i);
			x86regs[i].mode = (x86regs[i].mode & ~MODE_WRITE) | MODE_READ;
		}
	}
}

