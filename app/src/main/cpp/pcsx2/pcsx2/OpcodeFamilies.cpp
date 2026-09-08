// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
//
// Implementation of the EmuCoreX per-opcode-family interpreter fallback
// classification tables. See OpcodeFamilies.h for the design notes.

#include "PrecompiledHeader.h"
#include "OpcodeFamilies.h"
#include <array>

namespace OpcodeFamilies
{
	u64 g_familyMask[CORE_COUNT] = {0, 0, 0, 0};
	std::unordered_set<u32> g_disabledOpcodes[CORE_COUNT];

	void SetFamilyMask(u32 core, u64 mask)
	{
		g_familyMask[core] = mask;
	}

	void SetDisabledOpcodes(u32 core, const std::unordered_set<u32>& ids)
	{
		g_disabledOpcodes[core] = ids;
	}

	// -------------------------------------------------------------------------
	// EE
	// -------------------------------------------------------------------------
	u32 EEClassify(u32 code, u32* key)
	{
		const u32 primary = code >> 26;
		if (key)
			*key = 0;

		switch (primary)
		{
			case 0: // SPECIAL
			{
				const u32 funct = code & 0x3F;
				if (key)
					*key = (primary << 11) | funct;
				return EE::FAM_SPECIAL;
			}

			case 1: // REGIMM
			{
				const u32 rt = (code >> 16) & 0x1F;
				if (key)
					*key = (primary << 11) | rt;
				return EE::FAM_REGIMM;
			}

			case 2:
			case 3:
				return EE::FAM_JUMP;

			case 4:
			case 5:
			case 6:
			case 7:
			case 20:
			case 21:
			case 22:
			case 23:
				return EE::FAM_BRANCH;

			case 8:
			case 9:
			case 10:
			case 11:
			case 12:
			case 13:
			case 14:
			case 15:
			case 24: // DADDI
			case 25: // DADDIU
				return EE::FAM_ALU_IMM;

			case 26: // LDL
			case 27: // LDR
			case 30: // LQ
			case 31: // SQ
			case 32: // LB
			case 33: // LH
			case 34: // LWL
			case 35: // LW
			case 36: // LBU
			case 37: // LHU
			case 38: // LWR
			case 39: // LWU
			case 40: // SB
			case 41: // SH
			case 42: // SWL
			case 43: // SW
			case 44: // SDL
			case 45: // SDR
			case 46: // SWR
			case 49: // LWC1
			case 55: // LD
			case 57: // SWC1
			case 63: // SD
				return EE::FAM_LOADSTORE;

			case 16: // COP0
			{
				if (key)
					*key = (primary << 11) | ((code >> 21) & 0x1F);
				return EE::FAM_COP0;
			}

			case 17: // COP1 (FPU)
			{
				const u32 rs = (code >> 21) & 0x1F;
				if (key)
				{
					if (rs == 16 || rs == 20) // S.FMT / W.FMT
						*key = (primary << 11) | 0x40 | (code & 0x3F);
					else
						*key = (primary << 11) | rs;
				}
				return EE::FAM_COP1;
			}

			case 18: // COP2 (VU0 macro)
			case 54: // LQC2
			case 62: // SQC2
				return EE::FAM_COP2;

			case 28: // MMI
			{
				const u32 funct = code & 0x3F;
				if (key)
					*key = (primary << 11) | (((code >> 6) & 0x1F) << 6) | funct;
				switch (funct)
				{
					case 8: // MMI0
						return EE::FAM_MMI0;
					case 9: // MMI2
						return EE::FAM_MMI2;
					case 40: // MMI1
						return EE::FAM_MMI1;
					case 41: // MMI3
						return EE::FAM_MMI3;
					default:
						return EE::FAM_MMI;
				}
			}

			case 47: // CACHE
			case 51: // PREF
				return EE::FAM_MISC;

			default:
				return EE::FAM_MISC;
		}
	}

	// -------------------------------------------------------------------------
	// IOP
	// -------------------------------------------------------------------------
	u32 IOPClassify(u32 code, u32* key)
	{
		const u32 primary = code >> 26;
		if (key)
			*key = 0;

		switch (primary)
		{
			case 0: // SPECIAL
			{
				if (key)
					*key = (primary << 11) | (code & 0x3F);
				return IOP::FAM_SPECIAL;
			}

			case 1: // REGIMM
			{
				if (key)
					*key = (primary << 11) | ((code >> 16) & 0x1F);
				return IOP::FAM_REGIMM;
			}

			case 2:
			case 3:
				return IOP::FAM_JUMP;

			case 4:
			case 5:
			case 6:
			case 7:
				return IOP::FAM_BRANCH;

			case 8:
			case 9:
			case 10:
			case 11:
			case 12:
			case 13:
			case 14:
			case 15:
				return IOP::FAM_ALU_IMM;

			case 16: // COP0
			{
				if (key)
					*key = (primary << 11) | ((code >> 21) & 0x1F);
				return IOP::FAM_COP0;
			}

			case 18: // COP2 (GTE)
			{
				if (key)
					*key = (primary << 11) | (code & 0x3F);
				return IOP::FAM_GTE;
			}

			case 32: // LB
			case 33: // LH
			case 34: // LWL
			case 35: // LW
			case 36: // LBU
			case 37: // LHU
			case 38: // LWR
			case 40: // SB
			case 41: // SH
			case 42: // SWL
			case 43: // SW
			case 46: // SWR
			case 50: // LWC2
			case 58: // SWC2
				return IOP::FAM_LOADSTORE;

			default:
				return IOP::FAM_LOADSTORE;
		}
	}

	// -------------------------------------------------------------------------
	// VU
	// -------------------------------------------------------------------------
	namespace
	{
		// Upper FD_00..FD_11 tables: family per 5-bit sub index.
		// A = UPPER_ACC, C = UPPER_CONVERT, M = UPPER_MISC, X = unknown/MISC
		u32 VUUpperFdFamily(u32 table, u32 sub)
		{
			if (table == 3 && (sub == 10 || sub == 11))
				return VU::FAM_UPPER_MISC; // reserved / NOP
			switch (sub)
			{
				case 0:
				case 1:
				case 2:
				case 3: // ADDA/SUBA/MADDA/MSUBA (x/y/z/w)
				case 6: // MULA (x/y/z/w)
				case 7: // MULAq / ABS / MULAi / MULAw
				case 8: // ADDAq / MADDAq / ADDAi / MADDAi
				case 9: // SUBAq / MSUBAq / SUBAi / MSUBAi
				case 10: // ADDA / MADDA / MULA / (unknown)
				case 11: // SUBA / MSUBA / OPMULA / (unknown)
					// FD_01 sub 7 is ABS, FD_11 sub 7 is CLIP, FD_10 sub 7 is MULAi.
					if (table == 1 && sub == 7)
						return VU::FAM_UPPER_CONVERT; // ABS
					if (table == 3 && sub == 7)
						return VU::FAM_UPPER_MISC; // CLIP
					return VU::FAM_UPPER_ACC;

				case 4:
				case 5: // ITOF*/FTOI*
					return VU::FAM_UPPER_CONVERT;

				default:
					return VU::FAM_UPPER_MISC;
			}
		}

		u32 VULowerSubtableFamily(u32 t3, u32 sub)
		{
			// t3 = which T3_xx table (0..3), sub = (code >> 6) & 0x1f
			switch (sub)
			{
				case 12: // MOVE (T3_00) / MR32 (T3_01)
					return VU::FAM_LOWER_ALU;
				case 13: // LQI/SQI/LQD/SQD
					return VU::FAM_LOWER_LOADSTORE;
				case 14: // DIV/SQRT/RSQRT/WAITQ
					return VU::FAM_LOWER_DIVSQRT;
				case 15: // MTIR/MFIR/ILWR/ISWR
					return (t3 == 0 || t3 == 1) ? VU::FAM_LOWER_ALU : VU::FAM_LOWER_LOADSTORE;
				case 16: // RNEXT/RGET/RINIT/RXOR
					return VU::FAM_LOWER_ALU;
				case 25: // MFP (T3_00)
					return VU::FAM_LOWER_ALU;
				case 26: // XTOP (T3_00) / XITOP (T3_01)
					return VU::FAM_LOWER_GIF;
				case 27: // XGKICK (T3_00)
					return VU::FAM_LOWER_GIF;
				case 28: // ESADD/ERSADD/ELENG/ERLENG
				case 29: // EATANxy/EATANxz/ESUM
				case 30: // ESQRT/ERSQRT/ERCPR/WAITP
				case 31: // ESIN/EATAN/EEXP
					return VU::FAM_LOWER_EFU;
				default:
					return VU::FAM_LOWER_ALU;
			}
		}
	} // namespace

	u32 VUClassify(u32 code, bool isLower)
	{
		if (!isLower)
		{
			// Upper opcode: 6-bit primary in the low bits.
			const u32 primary = code & 0x3F;
			if (primary <= 0x2F)
				return VU::FAM_UPPER_MATH; // ADDx..MINI incl. q/i/broadcast forms
			if (primary < 0x3C)
				return VU::FAM_UPPER_MISC; // reserved, not an FD subtable
			return VUUpperFdFamily(primary & 3, (code >> 6) & 0x1F);
		}

		// Lower opcode: 7-bit primary in the high bits.
		const u32 primary = code >> 25;
		if (primary < 0x40)
		{
			switch (primary)
			{
				case 0: // LQ
				case 1: // SQ
				case 4: // ILW
				case 5: // ISW
					return VU::FAM_LOWER_LOADSTORE;
				case 8: // IADDIU
				case 9: // ISUBIU
					return VU::FAM_LOWER_ALU;
				case 16: // FCEQ
				case 17: // FCSET
				case 18: // FCAND
				case 19: // FCOR
				case 20: // FSEQ
				case 21: // FSSET
				case 22: // FSAND
				case 23: // FSOR
				case 24: // FMEQ
				case 26: // FMAND
				case 27: // FMOR
				case 28: // FCGET
					return VU::FAM_LOWER_ALU;
				case 32: // B
				case 33: // BAL
				case 36: // JR
				case 37: // JALR
				case 40: // IBEQ
				case 41: // IBNE
				case 44: // IBLTZ
				case 45: // IBGTZ
				case 46: // IBLEZ
				case 47: // IBGEZ
					return VU::FAM_LOWER_BRANCH;
				default:
					return VU::FAM_LOWER_ALU;
			}
		}

		if (primary == 0x40) // mVULowerOP
		{
			const u32 sub6 = code & 0x3F;
			if (sub6 == 48 || sub6 == 49 || sub6 == 50 || sub6 == 52 || sub6 == 53)
				return VU::FAM_LOWER_ALU; // IADD, ISUB, IADDI, IAND, IOR
			if (sub6 >= 60)
				return VULowerSubtableFamily(sub6 - 60, (code >> 6) & 0x1F);
			return VU::FAM_LOWER_ALU;
		}

		return VU::FAM_LOWER_ALU;
	}

	bool VURegionShouldInterpret(u32 core, u32 startPC, u32 microMemSize, u32 progMemMask, const u32* micro)
	{
		if (g_familyMask[core] == 0 || !micro || microMemSize == 0)
			return false;
		// Select an engine before entering a microprogram. Follow both arms of
		// direct branches and include E/branch delay slots. Unknown indirect
		// targets (or branches in delay slots) conservatively cover all memory.
		// Never change engines at a block boundary with a live pipeline.
		const u32 pairs = microMemSize / 8;
		if (pairs == 0 || pairs > 2048 || (pairs & (pairs - 1)) || progMemMask != microMemSize / 4 - 1)
			return true;
		const u32 mask = pairs - 1;
		auto disabled = [&](u32 pc) {
			const u32 upper = micro[pc * 2 + 1];
			return FamilyDisabled(core, VUClassify(upper, false)) ||
				(!(upper & 0x80000000u) && FamilyDisabled(core, VUClassify(micro[pc * 2], true)));
		};
		auto all_memory = [&]() {
			for (u32 pc = 0; pc < pairs; ++pc)
				if (disabled(pc))
					return true;
			return false;
		};
		std::array<bool, 2048> visited{};
		std::array<u32, 2048> work{};
		u32 count = 0;
		auto enqueue = [&](u32 pc) {
			pc &= mask;
			if (!visited[pc])
			{
				visited[pc] = true;
				work[count++] = pc;
			}
		};
		enqueue(startPC / 8);
		while (count)
		{
			const u32 pc = work[--count];
			const u32 lower = micro[pc * 2];
			const u32 upper = micro[pc * 2 + 1];
			if (disabled(pc))
				return true;
			const u32 delay = (pc + 1) & mask;
			if (upper & 0x40000000u)
			{
				if (disabled(delay))
					return true;
				continue;
			}
			if (!(upper & 0x80000000u) && VUClassify(lower, true) == VU::FAM_LOWER_BRANCH)
			{
				if (disabled(delay))
					return true;
				const u32 delayUpper = micro[delay * 2 + 1];
				const u32 op = lower >> 25;
				if (op == 0x24 || op == 0x25 || (delayUpper & 0x40000000u) ||
					(!(delayUpper & 0x80000000u) && VUClassify(micro[delay * 2], true) == VU::FAM_LOWER_BRANCH))
					return all_memory();
				const s32 offset = static_cast<s32>(lower << 21) >> 21;
				enqueue(pc + 1 + offset);
				if (op != 0x20 && op != 0x21)
					enqueue(pc + 2);
			}
			else
				enqueue(pc + 1);
		}
		return false;
	}

} // namespace OpcodeFamilies
