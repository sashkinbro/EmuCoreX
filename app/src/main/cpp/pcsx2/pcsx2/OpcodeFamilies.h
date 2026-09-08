// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
//
// EmuCoreX per-opcode-family interpreter fallback.
//
// Lets the user force individual opcode families (or single opcodes) out of the
// ARM64 recompilers and onto the interpreters, which is useful for bisecting
// JIT bugs in specific games. Everything is stored as plain integers:
//
//   - One u64 bitmask per core where bit N set means "family N is disabled"
//     (family N == index inside the canonical family list below).
//   - One unordered_set<u32> per core holding individual opcode keys.
//
// The Kotlin settings UI mirrors the exact same family indices / opcode keys
// (see ui/settings/OpcodeFamilies.kt), so the two sides only ever exchange
// numbers over the generic setSetting() bridge.

#pragma once

#include "common/Pcsx2Defs.h"

#include <unordered_set>

namespace OpcodeFamilies
{
	enum Core
	{
		CORE_EE = 0,
		CORE_IOP = 1,
		CORE_VU0 = 2,
		CORE_VU1 = 3,
		CORE_COUNT = 4
	};

	// Canonical family lists. The index inside each list == the bitmask bit.
	// KEEP IN SYNC with OpcodeFamilies.kt on the Kotlin side.
	namespace EE
	{
		enum Family
		{
			FAM_SPECIAL = 0, // SPECIAL table (SLL..DSRA32, SYSCALL, traps, ...)
			FAM_REGIMM = 1, // REGIMM table (BLTZ/BGEZ/traps/MTSAB)
			FAM_JUMP = 2, // J, JAL
			FAM_BRANCH = 3, // BEQ..BGTZ + likely variants
			FAM_ALU_IMM = 4, // ADDI..LUI, DADDI/DADDIU
			FAM_LOADSTORE = 5, // LB..SWR, LQ/SQ, LDL/LDR/SDL/SDR, LWC1/SWC1
			FAM_COP0 = 6, // System Control Coprocessor
			FAM_COP1 = 7, // Floating Point Unit
			FAM_COP2 = 8, // VU0 macro mode incl. LQC2/SQC2
			FAM_MMI = 9, // Multimedia instructions base table
			FAM_MMI0 = 10, // MMI0 parallel add/sub
			FAM_MMI1 = 11, // MMI1 parallel compare/min/max
			FAM_MMI2 = 12, // MMI2 parallel multiply/divide
			FAM_MMI3 = 13, // MMI3
			FAM_MISC = 14, // CACHE, PREF and everything unclassified
			FAM_COUNT
		};
	} // namespace EE

	namespace IOP
	{
		enum Family
		{
			FAM_SPECIAL = 0,
			FAM_REGIMM = 1,
			FAM_JUMP = 2,
			FAM_BRANCH = 3,
			FAM_ALU_IMM = 4,
			FAM_LOADSTORE = 5,
			FAM_COP0 = 6,
			FAM_GTE = 7, // Geometry Transformation Engine (COP2) incl. LWC2/SWC2
			FAM_COUNT
		};
	} // namespace IOP

	namespace VU
	{
		enum Family
		{
			FAM_UPPER_MATH = 0, // ADD/SUB/MUL/MADD/MSUB/MAX/MINI/OPMSUB (+q/i)
			FAM_UPPER_ACC = 1, // ADDA/SUBA/MULA/MADDA/MSUBA/OPMULA
			FAM_UPPER_CONVERT = 2, // ITOF*/FTOI*/ABS
			FAM_UPPER_MISC = 3, // CLIP, NOP
			FAM_LOWER_ALU = 4, // Integer ALU, flag ops, MOVE/MR32, MFP/MTIR/MFIR, R-reg
			FAM_LOWER_BRANCH = 5, // B/BAL/JR/JALR/IBxx
			FAM_LOWER_LOADSTORE = 6, // LQ/SQ(+I/D), ILW/ISW(+R)
			FAM_LOWER_DIVSQRT = 7, // DIV/SQRT/RSQRT/WAITQ
			FAM_LOWER_EFU = 8, // Elementary function unit (ESADD..EEXP, WAITP)
			FAM_LOWER_GIF = 9, // XGKICK/XTOP/XITOP
			FAM_COUNT
		};
	} // namespace VU

	// ---------------------------------------------------------------------------
	// Global blocklist state. Zero/empty by default => everything recompiled.
	// ---------------------------------------------------------------------------
	extern u64 g_familyMask[CORE_COUNT];
	extern std::unordered_set<u32> g_disabledOpcodes[CORE_COUNT];

	__forceinline_odr bool FamilyDisabled(u32 core, u32 family)
	{
		return (g_familyMask[core] >> family) & 1ull;
	}

	__forceinline_odr bool OpcodeDisabled(u32 core, u32 key)
	{
		return !g_disabledOpcodes[core].empty() && g_disabledOpcodes[core].count(key) != 0;
	}

	// ---------------------------------------------------------------------------
	// EE classification. Returns the family of the instruction in `code` and
	// (optionally) its individual-opcode key.
	//
	// Key layout (matches Kotlin): (primary << 11) | sub, where sub is:
	//   SPECIAL  -> funct (6 bits)
	//   REGIMM   -> rt    (5 bits)
	//   COP0/COP1-> rs    (5 bits)
	//   COP1 S/W -> 0x20 | funct (6 bits)
	//   MMI/MMI0..3 -> ((code >> 6) & 0x1f) << 6 | funct (11 bits)
	//   standard -> 0
	// ---------------------------------------------------------------------------
	u32 EEClassify(u32 code, u32* key);

	// ---------------------------------------------------------------------------
	// IOP classification (same key layout).
	// ---------------------------------------------------------------------------
	u32 IOPClassify(u32 code, u32* key);

	// ---------------------------------------------------------------------------
	// VU classification. `code` is the raw 32-bit instruction word (upper or
	// lower slot); `isLower` selects the decoder. Individual keys are not
	// tracked for VU (families only).
	// ---------------------------------------------------------------------------
	u32 VUClassify(u32 code, bool isLower);

	// Should the EE recompiler delegate this instruction to the interpreter?
	__forceinline_odr bool EEShouldInterpret(u32 code)
	{
		if (g_familyMask[CORE_EE] == 0 && g_disabledOpcodes[CORE_EE].empty())
			return false;
		u32 key = 0;
		const u32 family = EEClassify(code, &key);
		if (FamilyDisabled(CORE_EE, family))
			return true;
		return key != 0 && OpcodeDisabled(CORE_EE, key);
	}

	// Should the IOP recompiler delegate this instruction to the interpreter?
	__forceinline_odr bool IOPShouldInterpret(u32 code)
	{
		if (g_familyMask[CORE_IOP] == 0 && g_disabledOpcodes[CORE_IOP].empty())
			return false;
		u32 key = 0;
		const u32 family = IOPClassify(code, &key);
		if (FamilyDisabled(CORE_IOP, family))
			return true;
		return key != 0 && OpcodeDisabled(CORE_IOP, key);
	}

	// Does the VU microprogram region starting at startPC (byte address,
	// 8-aligned) contain a disabled family? Covers reachable direct branches
	// and delay slots, all memory for indirect control flow. Call at program
	// start and retain the decision until completion. Empty masks are a no-op.
	bool VURegionShouldInterpret(u32 core, u32 startPC, u32 microMemSize, u32 progMemMask, const u32* micro);

	// Parses "mask;id,id,id" style values produced by the Android settings UI.
	// familyMask: decimal u64. disabledIds: comma separated decimal u32 keys.
	void SetFamilyMask(u32 core, u64 mask);
	void SetDisabledOpcodes(u32 core, const std::unordered_set<u32>& ids);

} // namespace OpcodeFamilies
