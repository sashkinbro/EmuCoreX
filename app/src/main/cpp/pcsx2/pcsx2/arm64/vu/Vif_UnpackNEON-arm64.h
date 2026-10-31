// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-FileCopyrightText: 2026 isztld <https://isztld.com/>
// SPDX-License-Identifier: GPL-3.0

#pragma once

#include "Common.h"
#include "Vif_Dma.h"
#include "Vif_Dynarec.h"
#include "arm64/OaknutHelpers-arm64.h"

#include <utility>
#include <vector>

static constexpr oak::QReg VIF_Q_DEST{0};
static constexpr oak::QReg VIF_Q_WORK{1};
static constexpr oak::QReg VIF_Q_COL0{2};
static constexpr oak::QReg VIF_Q_COL1{3};
static constexpr oak::QReg VIF_Q_COL2{4};
static constexpr oak::QReg VIF_Q_COL3{5};
static constexpr oak::QReg VIF_Q_ROW{6};
static constexpr oak::QReg VIF_Q_TEMP{7};
static constexpr oak::WReg VIF_W_WORK{4};

// --------------------------------------------------------------------------------------
//  VifUnpackSSE_Base
// --------------------------------------------------------------------------------------
class VifUnpackNEON_Base
{
public:
	bool usn; // unsigned flag
	bool doMask; // masking write enable flag
	int UnpkLoopIteration;
	int UnpkNoOfIterations;
	int IsAligned;


protected:
	OakMemOperand dstIndirect;
	OakMemOperand srcIndirect;
	oak::QReg workReg;
	oak::QReg destReg;
	oak::WReg workGprW;

public:
	VifUnpackNEON_Base();
	virtual ~VifUnpackNEON_Base() = default;

	virtual void xUnpack(int upktype) const;
	virtual bool IsWriteProtectedOp() const = 0;
	virtual bool IsInputMasked() const = 0;
	virtual bool IsUnmaskedOp() const = 0;
	virtual void xMovDest() const;

protected:
	virtual void doMaskWrite(oak::QReg regX) const = 0;

	virtual void xShiftR(oak::QReg regX, int n) const;
	virtual void xPMOVXX8(oak::QReg regX) const;
	virtual void xPMOVXX16(oak::QReg regX) const;

	virtual void xUPK_S_32() const;
	virtual void xUPK_S_16() const;
	virtual void xUPK_S_8() const;

	virtual void xUPK_V2_32() const;
	virtual void xUPK_V2_16() const;
	virtual void xUPK_V2_8() const;

	virtual void xUPK_V3_32() const;
	virtual void xUPK_V3_16() const;
	virtual void xUPK_V3_8() const;

	virtual void xUPK_V4_32() const;
	virtual void xUPK_V4_16() const;
	virtual void xUPK_V4_8() const;
	virtual void xUPK_V4_5() const;
};

// --------------------------------------------------------------------------------------
//  VifUnpackSSE_Simple
// --------------------------------------------------------------------------------------
class VifUnpackNEON_Simple : public VifUnpackNEON_Base
{
	typedef VifUnpackNEON_Base _parent;

public:
	int curCycle;

public:
	VifUnpackNEON_Simple(bool usn_, bool domask_, int curCycle_);
	virtual ~VifUnpackNEON_Simple() = default;

	virtual bool IsWriteProtectedOp() const { return false; }
	virtual bool IsInputMasked() const { return false; }
	virtual bool IsUnmaskedOp() const { return !doMask; }

protected:
	virtual void doMaskWrite(oak::QReg regX) const;
};

// --------------------------------------------------------------------------------------
//  VifUnpackSSE_Dynarec
// --------------------------------------------------------------------------------------
class VifUnpackNEON_Dynarec : public VifUnpackNEON_Base
{
	typedef VifUnpackNEON_Base _parent;

public:
	bool isFill;
	int doMode; // two bit value representing difference mode
	bool skipProcessing;
	bool inputMasked;

protected:
	const nVifStruct& v; // vif0 or vif1
	const nVifBlock& vB; // some pre-collected data from VifStruct
	int vCL; // internal copy of vif->cl

public:
	VifUnpackNEON_Dynarec(const nVifStruct& vif_, const nVifBlock& vifBlock_);
	VifUnpackNEON_Dynarec(const VifUnpackNEON_Dynarec& src) // copy constructor
		: _parent(src)
		, v(src.v)
		, vB(src.vB)
	{
		isFill = src.isFill;
		vCL = src.vCL;
	}

	virtual ~VifUnpackNEON_Dynarec() = default;

	virtual bool IsWriteProtectedOp() const { return skipProcessing; }
	virtual bool IsInputMasked() const { return inputMasked; }
	virtual bool IsUnmaskedOp() const { return !doMode && !doMask; }

	void ModUnpack(int upknum, bool PostOp);
	void ProcessMasks();
	void CompileRoutine();

protected:
	using CopyRow = std::pair<s64, s64>; // (destination offset, source offset)
	void EmitCopyRows(const std::vector<CopyRow>& rows) const;

	virtual void doMaskWrite(oak::QReg regX) const;
	void SetMasks(int cS) const;
	void writeBackRow() const;

	static VifUnpackNEON_Dynarec FillingWrite(const VifUnpackNEON_Dynarec& src)
	{
		VifUnpackNEON_Dynarec fillingWrite(src);
		fillingWrite.doMask = true;
		fillingWrite.doMode = 0;
		return fillingWrite;
	}
};
