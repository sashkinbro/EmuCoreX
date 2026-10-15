// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+

#include "arm64/ir/IrLower-arm64.h"
#include "common/Assertions.h"

#include "arm64/OaknutHelpers-arm64.h"
#include "arm64/cpuRegistersPack-arm64.h"
#include "Memory.h"
#include "IopMem.h"
#include "IopDma.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <optional>
#include <type_traits>
#include <vector>

namespace Arm64Ir
{
	bool PlanBoundViVfTransfer(const ir::Function& fn, BoundViVfTransferPlan* plan, std::string* error)
	{
		if (plan) *plan = {};
		const auto fail = [&](const char* message) { if (error) *error = message; return false; };
		if (!plan) return fail("bound VI-to-VF plan output is missing");
		if (!ir::Verify(fn, error)) return false;
		if (fn.blocks.size() != 1 || fn.entry != 1 || fn.blocks[0].insts.size() != 4)
			return fail("bound VI-to-VF requires one MFIR transfer slice");
		const auto& i = fn.blocks[0].insts;
		if (i[0].op != ir::Op::ReadVi || i[1].op != ir::Op::Sext16 ||
			i[2].op != ir::Op::WriteVfLaneSplatBits || i[3].op != ir::Op::Return)
			return fail("unsupported bound VI-to-VF operation shape");
		if (i[0].imm >= 16 || i[2].imm == 0 || i[2].imm >= 32 ||
			(i[2].aux & 0x0f) == 0)
			return fail("bound VI-to-VF register index or lane mask is out of range");
		if (i[0].type != ir::Type::I32 || i[1].type != ir::Type::I32 ||
			i[2].type != ir::Type::Void || i[3].type != ir::Type::Void)
			return fail("bound VI-to-VF values require I32");
		const u16 delay = ir::IF_DELAY_SLOT;
		if ((i[0].aux & ~delay) != 0 || (i[1].aux & ~delay) != 0 ||
			(i[2].aux & ~(u16(0x0f) | delay)) != 0 || (i[3].aux & ~delay) != 0)
			return fail("bound VI-to-VF has unsupported instruction flags");
		const bool delay_slot = (i[0].aux & delay) != 0;
		for (const ir::Inst& inst : i)
			if (((inst.aux & delay) != 0) != delay_slot)
				return fail("bound VI-to-VF delay-slot flags are inconsistent");
		if (i[0].value == 0 || i[0].num_args != 0 || i[0].imm >= 16 ||
			i[1].value == 0 || i[1].num_args != 1 || i[1].imm != 0 ||
			i[1].args[0] != i[0].value || i[2].value != 0 || i[2].num_args != 1 ||
			i[2].args[0] != i[1].value || i[3].value != 0 || i[3].num_args != 0 || i[3].imm != 0)
			return fail("bound VI-to-VF SSA links do not match the MFIR slice");
		*plan = {static_cast<u8>(i[0].imm), static_cast<u8>(i[2].imm),
			static_cast<u8>(i[2].aux & 0x0f), delay_slot};
		if (error) error->clear();
		return true;
	}

	bool PlanBoundVfShuffle(const ir::Function& fn, BoundVfShufflePlan* plan, std::string* error)
	{
		if (plan) *plan = {};
		const auto fail = [&](const char* message) { if (error) *error = message; return false; };
		if (!plan) return fail("bound VF shuffle plan output is missing");
		if (!ir::Verify(fn, error)) return false;
		if (fn.blocks.size() != 1 || fn.entry != 1 || fn.blocks[0].insts.size() != 4)
			return fail("bound VF shuffle requires one MR32 transfer slice");
		const auto& i = fn.blocks[0].insts;
		if (i[0].op != ir::Op::ReadVf || i[1].op != ir::Op::VShuffle ||
			i[2].op != ir::Op::WriteVfMasked || i[3].op != ir::Op::Return)
			return fail("unsupported bound VF shuffle operation shape");
		if (i[0].imm >= 32 || i[2].imm == 0 || i[2].imm >= 32 ||
			(i[2].aux & 0x0f) == 0 || i[1].imm != 0x39)
			return fail("bound VF shuffle register index, mask, or selector is out of range");
		if (i[0].type != ir::Type::V4F32 || i[1].type != ir::Type::V4F32 ||
			i[2].type != ir::Type::Void || i[3].type != ir::Type::Void)
			return fail("bound VF shuffle values require V4F32");
		const u16 delay = ir::IF_DELAY_SLOT;
		if ((i[0].aux & ~delay) != 0 || (i[1].aux & ~delay) != 0 ||
			(i[2].aux & ~(u16(0x0f) | delay)) != 0 || (i[3].aux & ~delay) != 0)
			return fail("bound VF shuffle has unsupported flags");
		const bool delay_slot = (i[0].aux & delay) != 0;
		for (const ir::Inst& inst : i)
			if (((inst.aux & delay) != 0) != delay_slot)
				return fail("bound VF shuffle delay-slot flags are inconsistent");
		if (i[0].value == 0 || i[0].num_args != 0 || i[1].value == 0 || i[1].num_args != 1 ||
			i[1].args[0] != i[0].value || i[2].value != 0 || i[2].num_args != 1 ||
			i[2].args[0] != i[1].value || i[3].value != 0 || i[3].num_args != 0 || i[3].imm != 0)
			return fail("bound VF shuffle SSA links do not match MR32");
		*plan = {static_cast<u8>(i[0].imm), static_cast<u8>(i[2].imm),
			static_cast<u8>(i[2].aux & 0x0f), delay_slot};
		if (error) error->clear();
		return true;
	}

	bool PlanBoundVfUnary(const ir::Function& fn, BoundVfUnaryPlan* plan, std::string* error)
	{
		if (plan) *plan = {};
		const auto fail = [&](const char* message) { if (error) *error = message; return false; };
		if (!plan) return fail("bound VF unary plan output is missing");
		if (!ir::Verify(fn, error)) return false;
		if (fn.blocks.size() != 1 || fn.entry != 1 || fn.blocks[0].insts.size() != 4)
			return fail("bound VF unary requires one VABS slice");
		const auto& i = fn.blocks[0].insts;
		if (i[0].op != ir::Op::ReadVf || i[1].op != ir::Op::VAbs ||
			i[2].op != ir::Op::WriteVfMasked || i[3].op != ir::Op::Return)
			return fail("unsupported bound VF unary operation shape");
		if (i[0].imm >= 32 || i[2].imm == 0 || i[2].imm >= 32 ||
			(i[2].aux & 0x0f) == 0 || i[1].imm != 0)
			return fail("bound VF unary register index, mask, or operation immediate is invalid");
		if (i[0].type != ir::Type::V4F32 || i[1].type != ir::Type::V4F32 ||
			i[2].type != ir::Type::Void || i[3].type != ir::Type::Void)
			return fail("bound VF unary values require V4F32");
		const u16 delay = ir::IF_DELAY_SLOT;
		if ((i[0].aux & ~delay) != 0 || (i[1].aux & ~delay) != 0 ||
			(i[2].aux & ~(u16(0x0f) | delay)) != 0 || (i[3].aux & ~delay) != 0)
			return fail("bound VF unary has unsupported instruction flags");
		const bool delay_slot = (i[0].aux & delay) != 0;
		for (const ir::Inst& inst : i)
			if (((inst.aux & delay) != 0) != delay_slot)
				return fail("bound VF unary delay-slot flags are inconsistent");
		if (i[0].value == 0 || i[0].num_args != 0 || i[1].value == 0 || i[1].num_args != 1 ||
			i[1].args[0] != i[0].value || i[2].value != 0 || i[2].num_args != 1 ||
			i[2].args[0] != i[1].value || i[3].value != 0 || i[3].num_args != 0 || i[3].imm != 0)
			return fail("bound VF unary SSA links do not match VABS");
		*plan = {static_cast<u8>(i[0].imm), static_cast<u8>(i[2].imm),
			static_cast<u8>(i[2].aux & 0x0f), delay_slot};
		if (error) error->clear();
		return true;
	}

	bool PlanBoundVfConvert(const ir::Function& fn, BoundVfConvertPlan* plan, std::string* error)
	{
		if (plan) *plan = {};
		const auto fail = [&](const char* message) { if (error) *error = message; return false; };
		if (!plan) return fail("bound VF convert plan output is missing");
		if (!ir::Verify(fn, error)) return false;
		if (fn.blocks.size() != 1 || fn.entry != 1 || fn.blocks[0].insts.size() != 4)
			return fail("bound VF convert requires one integer-to-float slice");
		const auto& i = fn.blocks[0].insts;
		if (i[0].op != ir::Op::ReadVfBits ||
			(i[1].op != ir::Op::VI2F0 && i[1].op != ir::Op::VI2F4 &&
				i[1].op != ir::Op::VI2F12 && i[1].op != ir::Op::VI2F15) ||
			i[2].op != ir::Op::WriteVfMasked || i[3].op != ir::Op::Return)
			return fail("unsupported bound VF convert operation shape");
		if (i[0].imm >= 32 || i[2].imm == 0 || i[2].imm >= 32 ||
			(i[2].aux & 0x0f) == 0 || i[1].imm != 0)
			return fail("bound VF convert register, lane mask, or operation immediate is invalid");
		if (i[0].type != ir::Type::V4I32 || i[1].type != ir::Type::V4F32 ||
			i[2].type != ir::Type::Void || i[3].type != ir::Type::Void)
			return fail("bound VF convert requires V4I32 input and V4F32 result");
		const u16 delay = ir::IF_DELAY_SLOT;
		if ((i[0].aux & ~delay) != 0 || (i[1].aux & ~delay) != 0 ||
			(i[2].aux & ~(u16(0x0f) | delay)) != 0 || (i[3].aux & ~delay) != 0)
			return fail("bound VF convert has unsupported instruction flags");
		const bool delay_slot = (i[0].aux & delay) != 0;
		for (const ir::Inst& inst : i)
			if (((inst.aux & delay) != 0) != delay_slot)
				return fail("bound VF convert delay-slot flags are inconsistent");
		if (i[0].value == 0 || i[0].num_args != 0 || i[1].value == 0 || i[1].num_args != 1 ||
			i[1].args[0] != i[0].value || i[2].value != 0 || i[2].num_args != 1 ||
			i[2].args[0] != i[1].value || i[3].value != 0 || i[3].num_args != 0 || i[3].imm != 0)
			return fail("bound VF convert SSA links do not match the conversion slice");

		u8 fractional_bits = 0;
		switch (i[1].op)
		{
			case ir::Op::VI2F0: fractional_bits = 0; break;
			case ir::Op::VI2F4: fractional_bits = 4; break;
			case ir::Op::VI2F12: fractional_bits = 12; break;
			case ir::Op::VI2F15: fractional_bits = 15; break;
			default: return fail("unsupported bound VF convert operation");
		}
		*plan = {static_cast<u8>(i[0].imm), static_cast<u8>(i[2].imm),
			static_cast<u8>(i[2].aux & 0x0f), fractional_bits, delay_slot};
		if (error) error->clear();
		return true;
	}

	bool LowerBoundVfShufflePlan(const BoundVfShufflePlan& plan, u32 vf_source, u32 vf_dst,
		LowerOutput* out, std::string* error, BoundVfShuffleLayout layout)
	{
		if (out) *out = {};
		if (!out || !oakHasBlock() || plan.src_vf >= 32 || plan.dst_vf == 0 || plan.dst_vf >= 32 ||
			plan.lane_mask == 0 || plan.lane_mask > 0x0f || vf_source >= 31 || vf_dst >= 31 ||
			(layout != BoundVfShuffleLayout::GuestLanes && layout != BoundVfShuffleLayout::MicroVuPartial) ||
			vf_source == static_cast<u32>(OAK_QSCRATCH.index()) ||
			vf_dst == static_cast<u32>(OAK_QSCRATCH.index()))
		{
			if (error) *error = "invalid bound VF shuffle host register contract";
			return false;
		}
		const u8* start = recBeginOaknutEmit();
		const oak::QReg src(vf_source);
		const oak::QReg dst(vf_dst);
		const auto source_lane = [](u32 output_lane) { return (output_lane + 1) & 3; };
		const bool one_lane = (plan.lane_mask & (plan.lane_mask - 1)) == 0;
		if (layout == BoundVfShuffleLayout::MicroVuPartial)
		{
			if (one_lane)
			{
				u32 lane = 0;
				while ((plan.lane_mask & (8u >> lane)) == 0) ++lane;
				oakAsm->DUP(dst.S4(), src.Selem()[source_lane(lane)]);
			}
			else
				oakAsm->EXT(dst.B16(), src.B16(), src.B16(), 4);
		}
		else if (plan.lane_mask == 0x0f)
		{
			oakAsm->EXT(dst.B16(), src.B16(), src.B16(), 4);
		}
		else if (one_lane)
		{
			u32 lane = 0;
			while ((plan.lane_mask & (8u >> lane)) == 0) ++lane;
			oakAsm->INS(dst.Selem()[lane], src.Selem()[source_lane(lane)]);
		}
		else
		{
			const oak::QReg scratch = OAK_QSCRATCH;
			oakAsm->EXT(scratch.B16(), src.B16(), src.B16(), 4);
			for (u32 lane = 0; lane < 4; ++lane)
				if (plan.lane_mask & (8u >> lane))
					oakAsm->INS(dst.Selem()[lane], scratch.Selem()[lane]);
		}
		out->host_size = static_cast<u32>(oakGetCurrentCodePointer() - start);
		recEndOaknutEmit();
		if (error) error->clear();
		return true;
	}

	bool LowerBoundVfUnaryPlan(const BoundVfUnaryPlan& plan, u32 vf_source, u32 vf_dst,
		LowerOutput* out, std::string* error, BoundVfUnaryLayout layout)
	{
		if (out) *out = {};
		if (!out || !oakHasBlock() || plan.src_vf >= 32 || plan.dst_vf == 0 || plan.dst_vf >= 32 ||
			plan.lane_mask == 0 || plan.lane_mask > 0x0f || vf_source >= 31 || vf_dst >= 31 ||
			(layout != BoundVfUnaryLayout::GuestLanes && layout != BoundVfUnaryLayout::MicroVuPartial) ||
			vf_source == static_cast<u32>(OAK_QSCRATCH.index()) ||
			vf_dst == static_cast<u32>(OAK_QSCRATCH.index()))
		{
			if (error) *error = "invalid bound VF unary host register contract";
			return false;
		}
		const u8* start = recBeginOaknutEmit();
		const oak::QReg src(vf_source);
		const oak::QReg dst(vf_dst);
		const bool one_lane = (plan.lane_mask & (plan.lane_mask - 1)) == 0;
		if (layout == BoundVfUnaryLayout::MicroVuPartial)
		{
			if (one_lane)
			{
				u32 lane = 0;
				while ((plan.lane_mask & (8u >> lane)) == 0) ++lane;
				oakAsm->DUP(dst.S4(), src.Selem()[lane]);
			}
			else if (dst.index() != src.index())
				oakAsm->MOV(dst.B16(), src.B16());
			oakAsm->BIC(dst.S4(), 0x80, oak::LslSymbol::LSL, 24);
		}
		else if (plan.lane_mask == 0x0f)
		{
			if (dst.index() != src.index()) oakAsm->MOV(dst.B16(), src.B16());
			oakAsm->BIC(dst.S4(), 0x80, oak::LslSymbol::LSL, 24);
		}
		else
		{
			const oak::QReg scratch = OAK_QSCRATCH;
			oakAsm->MOV(scratch.B16(), src.B16());
			oakAsm->BIC(scratch.S4(), 0x80, oak::LslSymbol::LSL, 24);
			for (u32 lane = 0; lane < 4; ++lane)
				if (plan.lane_mask & (8u >> lane))
					oakAsm->INS(dst.Selem()[lane], scratch.Selem()[lane]);
		}
		out->host_size = static_cast<u32>(oakGetCurrentCodePointer() - start);
		recEndOaknutEmit();
		if (error) error->clear();
		return true;
	}

	bool LowerBoundVfConvertPlan(const BoundVfConvertPlan& plan, u32 vf_source, u32 vf_dst,
		LowerOutput* out, std::string* error, BoundVfConvertLayout layout)
	{
		if (out) *out = {};
		if (!out || !oakHasBlock() || plan.src_vf >= 32 || plan.dst_vf == 0 || plan.dst_vf >= 32 ||
			plan.lane_mask == 0 || plan.lane_mask > 0x0f ||
			(plan.fractional_bits != 0 && plan.fractional_bits != 4 &&
				plan.fractional_bits != 12 && plan.fractional_bits != 15) ||
			vf_source >= 31 || vf_dst >= 31 || vf_source == static_cast<u32>(OAK_QSCRATCH.index()) ||
			vf_dst == static_cast<u32>(OAK_QSCRATCH.index()) ||
			(layout != BoundVfConvertLayout::GuestLanes && layout != BoundVfConvertLayout::MicroVuPartial))
		{
			if (error) *error = "invalid bound VF convert plan, host register contract, or destination layout";
			return false;
		}

		const u8* start = recBeginOaknutEmit();
		const oak::QReg src(vf_source);
		const oak::QReg dst(vf_dst);
		const auto convert = [&](oak::QReg output, oak::QReg input) {
			if (plan.fractional_bits == 0)
				oakAsm->SCVTF(output.S4(), input.S4());
			else
				oakAsm->SCVTF(output.S4(), input.S4(), plan.fractional_bits);
		};

		if (layout == BoundVfConvertLayout::MicroVuPartial)
		{
			if ((plan.lane_mask & (plan.lane_mask - 1)) == 0)
			{
				u32 lane = 0;
				while ((plan.lane_mask & (8u >> lane)) == 0) ++lane;
				if (lane == 0)
					convert(dst, src);
				else
				{
					oakAsm->DUP(dst.S4(), src.Selem()[lane]);
					convert(dst, dst);
				}
			}
			else
				convert(dst, src);
		}
		else if (plan.lane_mask == 0x0f)
		{
			convert(dst, src);
		}
		else
		{
			const oak::QReg scratch = OAK_QSCRATCH;
			convert(scratch, src);
			for (u32 lane = 0; lane < 4; ++lane)
				if (plan.lane_mask & (8u >> lane))
					oakAsm->INS(dst.Selem()[lane], scratch.Selem()[lane]);
		}
		out->host_size = static_cast<u32>(oakGetCurrentCodePointer() - start);
		recEndOaknutEmit();
		if (error) error->clear();
		return true;
	}

	bool PlanBoundVfToInt(const ir::Function& fn, BoundVfToIntPlan* plan, std::string* error)
	{
		if (plan) *plan = {};
		const auto fail = [&](const char* message) { if (error) *error = message; return false; };
		if (!plan) return fail("bound VF-to-integer plan output is missing");
		if (!ir::Verify(fn, error)) return false;
		if (fn.blocks.size() != 1 || fn.entry != 1 || fn.blocks[0].insts.size() != 4)
			return fail("bound VF-to-integer requires one conversion slice");
		const auto& i = fn.blocks[0].insts;
		if (i[0].op != ir::Op::ReadVf ||
			(i[1].op != ir::Op::VF2I0 && i[1].op != ir::Op::VF2I4 &&
				i[1].op != ir::Op::VF2I12 && i[1].op != ir::Op::VF2I15) ||
			i[2].op != ir::Op::WriteVfBitsMasked || i[3].op != ir::Op::Return)
			return fail("unsupported bound VF-to-integer operation shape");
		if (i[0].imm >= 32 || i[2].imm == 0 || i[2].imm >= 32 ||
			(i[2].aux & 0x0f) == 0 || i[1].imm != 0)
			return fail("bound VF-to-integer register, lane mask, or operation immediate is invalid");
		if (i[0].type != ir::Type::V4F32 || i[1].type != ir::Type::V4I32 ||
			i[2].type != ir::Type::Void || i[3].type != ir::Type::Void)
			return fail("bound VF-to-integer requires V4F32 input and V4I32 result");
		const u16 delay = ir::IF_DELAY_SLOT;
		if ((i[0].aux & ~delay) != 0 || (i[1].aux & ~delay) != 0 ||
			(i[2].aux & ~(u16(0x0f) | delay)) != 0 || (i[3].aux & ~delay) != 0)
			return fail("bound VF-to-integer has unsupported instruction flags");
		const bool delay_slot = (i[0].aux & delay) != 0;
		for (const ir::Inst& inst : i)
			if (((inst.aux & delay) != 0) != delay_slot)
				return fail("bound VF-to-integer delay-slot flags are inconsistent");
		if (i[0].value == 0 || i[0].num_args != 0 || i[1].value == 0 || i[1].num_args != 1 ||
			i[1].args[0] != i[0].value || i[2].value != 0 || i[2].num_args != 1 ||
			i[2].args[0] != i[1].value || i[3].value != 0 || i[3].num_args != 0 || i[3].imm != 0)
			return fail("bound VF-to-integer SSA links do not match the conversion slice");

		u8 fractional_bits = 0;
		switch (i[1].op)
		{
			case ir::Op::VF2I0: fractional_bits = 0; break;
			case ir::Op::VF2I4: fractional_bits = 4; break;
			case ir::Op::VF2I12: fractional_bits = 12; break;
			case ir::Op::VF2I15: fractional_bits = 15; break;
			default: return fail("unsupported bound VF-to-integer conversion");
		}
		*plan = {static_cast<u8>(i[0].imm), static_cast<u8>(i[2].imm),
			static_cast<u8>(i[2].aux & 0x0f), fractional_bits, delay_slot};
		if (error) error->clear();
		return true;
	}

	bool LowerBoundVfToIntPlan(const BoundVfToIntPlan& plan, u32 vf_source, u32 vf_dst,
		LowerOutput* out, std::string* error, BoundVfConvertLayout layout)
	{
		if (out) *out = {};
		if (!out || !oakHasBlock() || plan.src_vf >= 32 || plan.dst_vf == 0 || plan.dst_vf >= 32 ||
			plan.lane_mask == 0 || plan.lane_mask > 0x0f ||
			(plan.fractional_bits != 0 && plan.fractional_bits != 4 &&
				plan.fractional_bits != 12 && plan.fractional_bits != 15) ||
			vf_source >= 31 || vf_dst >= 31 || vf_source == static_cast<u32>(OAK_QSCRATCH.index()) ||
			vf_dst == static_cast<u32>(OAK_QSCRATCH.index()) ||
			vf_source == static_cast<u32>(OAK_QSCRATCH2.index()) ||
			vf_dst == static_cast<u32>(OAK_QSCRATCH2.index()) ||
			vf_source == static_cast<u32>(OAK_QSCRATCH3.index()) ||
			vf_dst == static_cast<u32>(OAK_QSCRATCH3.index()) ||
			(layout != BoundVfConvertLayout::GuestLanes && layout != BoundVfConvertLayout::MicroVuPartial))
		{
			if (error) *error = "invalid bound VF-to-integer plan, host register contract, or destination layout";
			return false;
		}

		const u8* start = recBeginOaknutEmit();
		const oak::QReg src(vf_source);
		const oak::QReg dst(vf_dst);
		const oak::QReg scaled = OAK_QSCRATCH;
		const oak::QReg clamp = OAK_QSCRATCH2;
		const oak::QReg scratch = OAK_QSCRATCH3;
		const bool one_lane = (plan.lane_mask & (plan.lane_mask - 1)) == 0;
		if (layout == BoundVfConvertLayout::MicroVuPartial && one_lane)
		{
			u32 lane = 0;
			while ((plan.lane_mask & (8u >> lane)) == 0) ++lane;
			oakAsm->DUP(scaled.S4(), src.Selem()[lane]);
		}
		else
			oakAsm->MOV(scaled.B16(), src.B16());

		if (plan.fractional_bits != 0)
		{
			const u32 scale_bits = 0x3f800000u + (static_cast<u32>(plan.fractional_bits) << 23);
			oakAsm->MOV(OAK_WSCRATCH, scale_bits);
			oakAsm->DUP(clamp.S4(), OAK_WSCRATCH);
			oakAsm->FMUL(scaled.S4(), scaled.S4(), clamp.S4());
		}

		// Replace exponent-255 values with signed infinity before FCVTZS. The
		// legacy helper saturates every infinity/NaN by the scaled raw sign; this
		// also avoids AArch64's different NaN-to-integer result. Scaling happens
		// first, so FPCR.DN canonicalization is reflected in the selected sign.
		oakAsm->USHR(clamp.S4(), scaled.S4(), 23);
		oakAsm->MOVI(scratch.S4(), 0xff);
		oakAsm->AND(clamp.B16(), clamp.B16(), scratch.B16());
		oakAsm->CMEQ(clamp.S4(), clamp.S4(), scratch.S4());
		oakAsm->MOV(OAK_WSCRATCH, 0xff800000u);
		oakAsm->DUP(scratch.S4(), OAK_WSCRATCH);
		oakAsm->AND(scratch.B16(), scaled.B16(), scratch.B16());
		oakAsm->BSL(clamp.B16(), scratch.B16(), scaled.B16());
		// FCVTZS uses round-toward-zero and saturates finite overflow to INT_MIN/MAX.
		// NaNs have already become same-sign infinity, so they follow the legacy clamp.
		oakAsm->FCVTZS(clamp.S4(), clamp.S4());

		if (layout == BoundVfConvertLayout::MicroVuPartial && one_lane)
			oakAsm->INS(dst.Selem()[0], clamp.Selem()[0]);
		else if (plan.lane_mask == 0x0f)
			oakAsm->MOV(dst.B16(), clamp.B16());
		else
		{
			for (u32 lane = 0; lane < 4; ++lane)
				if (plan.lane_mask & (8u >> lane))
					oakAsm->INS(dst.Selem()[lane], clamp.Selem()[lane]);
		}

		out->host_size = static_cast<u32>(oakGetCurrentCodePointer() - start);
		recEndOaknutEmit();
		if (error) error->clear();
		return true;
	}

	bool LowerBoundViVfTransferPlan(const BoundViVfTransferPlan& plan, u32 vi_source, u32 vf_dst,
		LowerOutput* out, std::string* error, BoundViVfDestinationLayout layout)
	{
		if (out) *out = {};
		if (!out || !oakHasBlock() || plan.src_vi >= 16 || plan.dst_vf == 0 || plan.dst_vf >= 32 ||
			plan.lane_mask == 0 || plan.lane_mask > 0x0f ||
			(plan.src_vi == 0 ? vi_source != ~u32(0) :
				(vi_source >= 31 || vi_source == static_cast<u32>(OAK_WSCRATCH.index()))) || vf_dst >= 32 ||
			(layout != BoundViVfDestinationLayout::GuestLanes &&
				layout != BoundViVfDestinationLayout::MicroVuPartial))
		{
			if (error) *error = "invalid bound VI-to-VF host register contract";
			return false;
		}
		const u8* start = recBeginOaknutEmit();
		if (plan.src_vi == 0)
			oakAsm->EOR(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH);
		else
			oakAsm->SXTH(OAK_WSCRATCH, oak::WReg(vi_source));
		const oak::QReg dst(vf_dst);
		if (layout == BoundViVfDestinationLayout::MicroVuPartial)
		{
			// The allocator consumes only lanes selected by lane_mask. For a
			// one-lane write its packed destination is S0; for multiple lanes,
			// preserve the architectural positions and broadcast the raw bits.
			oakAsm->FMOV(oakSRegister(vf_dst), OAK_WSCRATCH);
			if ((plan.lane_mask & (plan.lane_mask - 1)) != 0)
				oakAsm->DUP(dst.S4(), dst.Selem()[0]);
			out->host_size = static_cast<u32>(oakGetCurrentCodePointer() - start);
			recEndOaknutEmit();
			if (error) error->clear();
			return true;
		}
		for (u32 lane = 0; lane < 4; ++lane)
			if (plan.lane_mask & (8u >> lane))
			{
				oakAsm->INS(dst.Selem()[lane], OAK_WSCRATCH);
			}
		out->host_size = static_cast<u32>(oakGetCurrentCodePointer() - start);
		recEndOaknutEmit();
		if (error) error->clear();
		return true;
	}

	extern "C" u32 g_eeir_exit_pc = 0;
	extern "C" u32 g_eeir_exit_valid = 0;

	namespace
	{
		template <typename Reg>
		bool EmitSimpleAddSubImmediate(ir::Op op, ir::Type type, const Reg& dst, const Reg& src, u64 constant)
		{
			if (op != ir::Op::Add && op != ir::Op::Sub) return false;
			const u64 mask = type == ir::Type::I64 ? ~u64(0) : 0xffffffffull;
			constant &= mask;
			bool subtract = op == ir::Op::Sub;
			if (!oak::AddSubImm::is_valid(constant))
			{
				constant = (u64(0) - constant) & mask;
				subtract = !subtract;
			}
			if (!oak::AddSubImm::is_valid(constant)) return false;
			if (subtract) oakAsm->SUB(dst, src, constant);
			else oakAsm->ADD(dst, src, constant);
			return true;
		}

		template <typename Reg>
		bool EmitSimpleLogicalImmediate(ir::Op op, const Reg& dst, const Reg& src, u64 constant)
		{
			if (op != ir::Op::And && op != ir::Op::Or && op != ir::Op::Xor) return false;
			using Word = std::conditional_t<std::is_same_v<Reg, oak::XReg>, std::uint64_t, std::uint32_t>;
			using Immediate = std::conditional_t<std::is_same_v<Reg, oak::XReg>, oak::BitImm64, oak::BitImm32>;
			const Word narrowed = static_cast<Word>(constant);
			// Query the encoder before constructing BitImm, whose invalid-input
			// constructor throws. Zero, all-ones and irregular masks use registers.
			if (!oak::detail::encode_bit_imm(narrowed)) return false;
			const Immediate imm(narrowed);
			switch (op)
			{
				case ir::Op::And: oakAsm->AND(dst, src, imm); break;
				case ir::Op::Or: oakAsm->ORR(dst, src, imm); break;
				case ir::Op::Xor: oakAsm->EOR(dst, src, imm); break;
				default: return false;
			}
			return true;
		}

		void EmitSimpleAlu32(ir::Op op, oak::WReg dst, oak::WReg lhs, oak::WReg rhs)
		{
			switch (op)
			{
				case ir::Op::Add: oakAsm->ADD(dst, lhs, rhs); break;
				case ir::Op::Sub: oakAsm->SUB(dst, lhs, rhs); break;
				case ir::Op::And: oakAsm->AND(dst, lhs, rhs); break;
				case ir::Op::Or: oakAsm->ORR(dst, lhs, rhs); break;
				case ir::Op::Xor: oakAsm->EOR(dst, lhs, rhs); break;
				default: pxFail("invalid simple scalar IR operation"); break;
			}
		}
	}

	bool PlanBoundViBlock(const ir::Function& fn, BoundViPlan* plan, std::string* error)
	{
		if (plan) *plan = {};
		const auto fail = [&](const char* message) { if (error) *error = message; return false; };
		if (!plan) return fail("bound VI plan output is missing");
		if (!ir::Verify(fn, error)) return false;
		if (fn.blocks.size() != 1 || fn.entry != 1 || fn.blocks[0].insts.size() != 6)
			return fail("bound VI requires a single arithmetic slice");
		const auto& i = fn.blocks[0].insts;
		const bool immediate = i[1].op == ir::Op::ConstI32;
		if (i[0].op != ir::Op::ReadVi || (!immediate && i[1].op != ir::Op::ReadVi) ||
			(i[2].op != ir::Op::Add && i[2].op != ir::Op::Sub && i[2].op != ir::Op::And && i[2].op != ir::Op::Or) ||
			i[3].op != ir::Op::Zext16 || i[4].op != ir::Op::WriteVi || i[5].op != ir::Op::Return)
			return fail("unsupported bound VI operation shape");
		if (immediate && (i[1].imm > 0xffffffffull || (i[2].op != ir::Op::Add && i[2].op != ir::Op::Sub)))
			return fail("bound VI immediate requires I32 Add/Sub");
		if (i[0].imm >= 16 || (!immediate && i[1].imm >= 16) || i[4].imm == 0 || i[4].imm >= 16)
			return fail("bound VI register index is out of range or writes VI0");
		for (u32 index = 0; index < 4; ++index)
			if (i[index].type != ir::Type::I32) return fail("bound VI values require I32");
		for (const ir::Inst& inst : i)
			if (inst.aux != 0) return fail("bound VI does not accept auxiliary flags");
		if (i[2].imm != 0 || i[3].imm != 0 || i[5].imm != 0 ||
			i[2].args[0] != i[0].value || i[2].args[1] != i[1].value ||
			i[3].args[0] != i[2].value || i[4].args[0] != i[3].value)
			return fail("bound VI SSA links do not match the arithmetic slice");
		*plan = {i[2].op, static_cast<u8>(i[0].imm), immediate ? u8(0) : static_cast<u8>(i[1].imm),
			static_cast<u8>(i[4].imm), immediate, immediate ? static_cast<u32>(i[1].imm) : 0};
		return true;
	}

	bool LowerBoundViBlock(const ir::Function& fn, u32 lhs_dst, u32 rhs, LowerOutput* out, std::string* error)
	{
		if (out) *out = {};
		BoundViPlan plan;
		if (!PlanBoundViBlock(fn, &plan, error)) return false;
		return LowerBoundViPlan(plan, lhs_dst, rhs, out, error);
	}

	bool LowerBoundViPlan(const BoundViPlan& plan, u32 lhs_dst, u32 rhs, LowerOutput* out, std::string* error)
	{
		if (out) *out = {};
		if (!out || !oakHasBlock() || lhs_dst >= 31 || rhs >= 31 || plan.lhs_vi >= 16 || plan.rhs_vi >= 16 ||
			plan.dst_vi == 0 || plan.dst_vi >= 16 ||
			(plan.alu != ir::Op::Add && plan.alu != ir::Op::Sub && plan.alu != ir::Op::And && plan.alu != ir::Op::Or) ||
			(plan.immediate && (lhs_dst == rhs || (plan.alu != ir::Op::Add && plan.alu != ir::Op::Sub))) ||
			(!plan.immediate && lhs_dst == rhs && plan.lhs_vi != plan.rhs_vi))
		{
			if (error) *error = "invalid bound VI host register contract";
			return false;
		}
		const u8* start = recBeginOaknutEmit();
		const oak::WReg dst = oakWRegister(lhs_dst), right = oakWRegister(rhs);
		// Equal VI operands are architectural identities, independent of host
		// allocation. Folding these uses no additional scratch register.
		if (plan.immediate && plan.lhs_vi == 0)
			oakAsm->MOV(dst, (plan.alu == ir::Op::Add ? plan.constant : 0u - plan.constant) & 0xffffu);
		else if (plan.immediate)
		{
			if (plan.constant != 0 && !EmitSimpleAddSubImmediate(plan.alu, ir::Type::I32, dst, dst, plan.constant))
			{
				oakAsm->MOV(right, plan.constant);
				EmitSimpleAlu32(plan.alu, dst, dst, right);
			}
			oakAsm->UXTH(dst, dst);
		}
		else if (plan.lhs_vi == plan.rhs_vi && plan.alu == ir::Op::Sub)
			oakAsm->MOV(dst, 0);
		else
		{
			if (plan.lhs_vi != plan.rhs_vi || (plan.alu != ir::Op::And && plan.alu != ir::Op::Or))
				EmitSimpleAlu32(plan.alu, dst, dst, right);
			oakAsm->UXTH(dst, dst);
		}
		out->host_size = static_cast<u32>(oakGetCurrentCodePointer() - start);
		recEndOaknutEmit();
		return true;
	}

	bool PlanBoundVfViTransfer(const ir::Function& fn, BoundVfViTransferPlan* plan, std::string* error)
	{
		if (plan) *plan = {};
		const auto fail = [&](const char* message) { if (error) *error = message; return false; };
		if (!plan) return fail("bound VF-to-VI plan output is missing");
		if (!ir::Verify(fn, error)) return false;
		if (fn.blocks.size() != 1 || fn.entry != 1 || fn.blocks[0].insts.size() != 4)
			return fail("bound VF-to-VI requires one lane transfer slice");
		const auto& i = fn.blocks[0].insts;
		if (i[0].op != ir::Op::ReadVfLaneBits || i[1].op != ir::Op::Zext16 ||
			i[2].op != ir::Op::WriteVi || i[3].op != ir::Op::Return)
			return fail("unsupported bound VF-to-VI operation shape");
		if (i[0].imm >= 32 || i[2].imm == 0 || i[2].imm >= 16)
			return fail("bound VF-to-VI register index is out of range or writes VI0");
		if (i[0].type != ir::Type::I32 || i[1].type != ir::Type::I32 ||
			i[2].type != ir::Type::Void || i[3].type != ir::Type::Void)
			return fail("bound VF-to-VI values require I32");
		const u16 allowed_read_aux = 3 | ir::IF_DELAY_SLOT;
		if ((i[0].aux & ~allowed_read_aux) != 0 ||
			(i[1].aux & ~ir::IF_DELAY_SLOT) != 0 ||
			(i[2].aux & ~ir::IF_DELAY_SLOT) != 0 ||
			(i[3].aux & ~ir::IF_DELAY_SLOT) != 0)
			return fail("bound VF-to-VI has unsupported instruction flags");
		const bool delay_slot = (i[0].aux & ir::IF_DELAY_SLOT) != 0;
		for (const ir::Inst& inst : i)
			if (((inst.aux & ir::IF_DELAY_SLOT) != 0) != delay_slot)
				return fail("bound VF-to-VI delay-slot flags are inconsistent");
		if (i[1].imm != 0 || i[2].args[0] != i[1].value ||
			i[1].args[0] != i[0].value || i[3].imm != 0)
			return fail("bound VF-to-VI SSA links do not match the transfer slice");
		*plan = {static_cast<u8>(i[0].imm), static_cast<u8>(i[0].aux & 3),
			static_cast<u8>(i[2].imm), delay_slot};
		if (error) error->clear();
		return true;
	}

	bool LowerBoundVfViTransfer(const ir::Function& fn, u32 vf_source, u32 vi_dst,
		LowerOutput* out, std::string* error)
	{
		if (out) *out = {};
		BoundVfViTransferPlan plan;
		if (!PlanBoundVfViTransfer(fn, &plan, error)) return false;
		return LowerBoundVfViTransferPlan(plan, vf_source, vi_dst, out, error);
	}

	bool LowerBoundVfViTransferPlan(const BoundVfViTransferPlan& plan, u32 vf_source, u32 vi_dst,
		LowerOutput* out, std::string* error)
	{
		if (out) *out = {};
		if (!out || !oakHasBlock() || plan.vf >= 32 || plan.lane >= 4 ||
			plan.dst_vi == 0 || plan.dst_vi >= 16 || vf_source >= 32 || vi_dst >= 31)
		{
			if (error) *error = "invalid bound VF-to-VI host register contract";
			return false;
		}
		const u8* start = recBeginOaknutEmit();
		oakAsm->FMOV(oakWRegister(vi_dst), oakSRegister(vf_source));
		oakAsm->UXTH(oakWRegister(vi_dst), oakWRegister(vi_dst));
		out->host_size = static_cast<u32>(oakGetCurrentCodePointer() - start);
		recEndOaknutEmit();
		if (error) error->clear();
		return true;
	}

	namespace
	{
		// C++ helpers referenced by the lowered code. ABI: integer arguments in
		// W0/W1, results in W0 (or X0 for 64-bit loads).
		extern "C"
		{
			u32 EeIrMemRead8(u32 addr) { return memRead8(addr); }
			u32 EeIrMemRead8S(u32 addr) { return static_cast<u32>(static_cast<s32>(static_cast<s8>(memRead8(addr)))); }
			u32 EeIrMemRead16(u32 addr) { return memRead16(addr); }
			u32 EeIrMemRead16S(u32 addr) { return static_cast<u32>(static_cast<s32>(static_cast<s16>(memRead16(addr)))); }
			u32 EeIrMemRead32(u32 addr) { return memRead32(addr); }
			u64 EeIrMemRead64(u32 addr) { return memRead64(addr); }
			void EeIrMemWrite8(u32 addr, u32 value) { memWrite8(addr, static_cast<u8>(value)); }
			void EeIrMemWrite16(u32 addr, u32 value) { memWrite16(addr, static_cast<u16>(value)); }
			void EeIrMemWrite32(u32 addr, u32 value) { memWrite32(addr, value); }
			void EeIrMemWrite64(u32 addr, u64 value) { memWrite64(addr, value); }
			void EeIrMemRead128(u32 addr, mem128_t* out) { memRead128(addr, out); }
			void EeIrMemWrite128(u32 addr, const mem128_t* value) { memWrite128(addr, value); }

			u32 IopIrMemRead8(u32 addr) { return iopMemRead8(addr); }
			u32 IopIrMemRead8S(u32 addr) { return static_cast<u32>(static_cast<s32>(static_cast<s8>(iopMemRead8(addr)))); }
			u32 IopIrMemRead16(u32 addr) { return iopMemRead16(addr); }
			u32 IopIrMemRead16S(u32 addr) { return static_cast<u32>(static_cast<s32>(static_cast<s16>(iopMemRead16(addr)))); }
			u32 IopIrMemRead32(u32 addr) { return iopMemRead32(addr); }
			void IopIrMemWrite8(u32 addr, u32 value) { iopMemWrite8(addr, static_cast<u8>(value)); }
			void IopIrMemWrite16(u32 addr, u32 value) { iopMemWrite16(addr, static_cast<u16>(value)); }
			void IopIrMemWrite32(u32 addr, u32 value) { iopMemWrite32(addr, value); }

			// DIV/DIVU edge cases match R5900OpcodeImpl exactly.
			u32 EeIrDivSLo(u32 a, u32 b)
			{
				if (a == 0x80000000u && b == 0xffffffffu)
					return 0x80000000u;
				if (b != 0)
					return static_cast<u32>(static_cast<s32>(a) / static_cast<s32>(b));
				return (static_cast<s32>(a) < 0) ? 1u : 0xffffffffu;
			}

			u32 EeIrDivSHi(u32 a, u32 b)
			{
				if (a == 0x80000000u && b == 0xffffffffu)
					return 0;
				if (b != 0)
					return static_cast<u32>(static_cast<s32>(a) % static_cast<s32>(b));
				return a;
			}

			u32 EeIrDivULo(u32 a, u32 b) { return (b != 0) ? a / b : 0xffffffffu; }
			u32 EeIrDivUHi(u32 a, u32 b) { return (b != 0) ? a % b : a; }
		}

		bool IsSupportedOp(ir::Op op)
		{
			switch (op)
			{
				case ir::Op::Nop:
				case ir::Op::ConstI32:
				case ir::Op::ConstF32:
				case ir::Op::ConstI64:
				case ir::Op::ConstVec:
				case ir::Op::Copy:
				case ir::Op::Undef:
				case ir::Op::Select:
				case ir::Op::Not:
				case ir::Op::Neg:
				case ir::Op::Clz:
				case ir::Op::Sext8:
				case ir::Op::Sext16:
				case ir::Op::Zext8:
				case ir::Op::Zext16:
				case ir::Op::Sext32:
				case ir::Op::Zext32:
				case ir::Op::Trunc32:
				case ir::Op::Add:
				case ir::Op::Sub:
				case ir::Op::And:
				case ir::Op::Or:
				case ir::Op::Xor:
				case ir::Op::MinS:
				case ir::Op::MinU:
				case ir::Op::MaxS:
				case ir::Op::MaxU:
				case ir::Op::Mul:
				case ir::Op::MulHiS:
				case ir::Op::MulHiU:
				case ir::Op::Msub:
				case ir::Op::Shl:
				case ir::Op::ShrU:
				case ir::Op::ShrS:
				case ir::Op::VShl:
				case ir::Op::VZipLo:
				case ir::Op::VZipHi:
				case ir::Op::VPackEven:
				case ir::Op::VInterleaveEven:
				case ir::Op::VShuffleBytes:
				case ir::Op::VInterleaveOdd:
				case ir::Op::VMulWidenLoS:
				case ir::Op::VMulWidenHiS:
				case ir::Op::VMulWidenLoU:
				case ir::Op::VMulWidenHiU:
				case ir::Op::VPackSatS:
				case ir::Op::VShlEach:
				case ir::Op::VShrUEach:
				case ir::Op::VShrSEach:
				case ir::Op::VShuffle:
				case ir::Op::VShuffle2:
				case ir::Op::VShrU:
				case ir::Op::VShrS:
				case ir::Op::VAddI:
				case ir::Op::VSubI:
				case ir::Op::VAddSatS:
				case ir::Op::VAddSatU:
				case ir::Op::VSubSatS:
				case ir::Op::VSubSatU:
				case ir::Op::VAbsSatS:
				case ir::Op::VMinS:
				case ir::Op::VMinU:
				case ir::Op::VMaxS:
				case ir::Op::VMaxU:
				case ir::Op::VCmpEq:
				case ir::Op::VCmpNe:
				case ir::Op::VCmpLtS:
				case ir::Op::VCmpLtU:
				case ir::Op::VCmpLeS:
				case ir::Op::VCmpLeU:
				case ir::Op::CmpEq:
				case ir::Op::CmpNe:
				case ir::Op::CmpLtS:
				case ir::Op::CmpLtU:
				case ir::Op::CmpLeS:
				case ir::Op::CmpLeU:
				case ir::Op::CmpGtS:
				case ir::Op::CmpGtU:
				case ir::Op::CmpGeS:
				case ir::Op::CmpGeU:
				case ir::Op::DivS:
				case ir::Op::RemS:
				case ir::Op::DivU:
				case ir::Op::RemU:
				case ir::Op::Load8U:
				case ir::Op::Load8S:
				case ir::Op::Load16U:
				case ir::Op::Load16S:
				case ir::Op::Load32:
				case ir::Op::Load64:
				case ir::Op::Load128:
				case ir::Op::Store8:
				case ir::Op::Store16:
				case ir::Op::Store32:
				case ir::Op::Store64:
				case ir::Op::Store128:
				case ir::Op::ReadCp0:
				case ir::Op::WriteCp0:
				case ir::Op::ReadCp2Data:
				case ir::Op::WriteCp2Data:
				case ir::Op::ReadCp2Control:
				case ir::Op::WriteCp2Control:
				case ir::Op::ReadFprBits:
				case ir::Op::WriteFprBits:
				case ir::Op::ReadFcr31:
				case ir::Op::WriteFcr31:
				case ir::Op::CheckInterrupts:
				case ir::Op::ReadGpr:
				case ir::Op::WriteGpr:
				case ir::Op::ReadHi:
				case ir::Op::ReadLo:
				case ir::Op::WriteHi:
				case ir::Op::WriteLo:
				case ir::Op::AddCycles:
					return true;
				default:
					return false;
			}
		}


		// Forward reads only within one basic block. Keep every architectural
		// write in place: a following memory helper may observe state or fault.
		// Helpers/accesses are barriers even for ordinary RAM because the same
		// operation can dispatch MMIO, events or guest exception handlers.
		void ForwardGuestStateReads(ir::Function& fn, bool inline_division, bool forward_quad_state)
		{
			struct CachedState { u32 narrow = 0, wide = 0; bool sign_extended = false; u32 quad = 0; };
			for (ir::Block& block : fn.blocks)
			{
				std::array<CachedState, 68> cache{}; // GPR[32], HI/LO[2], CP0[32]
				for (ir::Inst& inst : block.insts)
				{
					const auto kind = ir::Info(inst.op).kind;
					if (kind == ir::OpKind::MemLoad || kind == ir::OpKind::MemStore || kind == ir::OpKind::Helper ||
						(!inline_division && (inst.op == ir::Op::DivS || inst.op == ir::Op::DivU ||
							inst.op == ir::Op::RemS || inst.op == ir::Op::RemU)))
					{
						cache = {};
						continue;
					}
					const bool read = inst.op == ir::Op::ReadGpr || inst.op == ir::Op::ReadHi || inst.op == ir::Op::ReadLo || inst.op == ir::Op::ReadCp0;
					const bool write = inst.op == ir::Op::WriteGpr || inst.op == ir::Op::WriteHi || inst.op == ir::Op::WriteLo || inst.op == ir::Op::WriteCp0;
					if (!read && !write)
						continue;
					const u32 reg = (inst.op == ir::Op::ReadCp0 || inst.op == ir::Op::WriteCp0) ? 36u + static_cast<u32>(inst.imm) :
						(inst.op == ir::Op::ReadGpr || inst.op == ir::Op::WriteGpr) ?
						static_cast<u32>(inst.imm) : 32u + static_cast<u32>(inst.imm) * 2u +
						((inst.op == ir::Op::ReadLo || inst.op == ir::Op::WriteLo) ? 1u : 0u);
					if (reg == 0)
						continue;
					CachedState& state = cache[reg];
					const ir::Type type = read ? inst.type : fn.ValueType(inst.args[0]);
					if (reg >= 32 && reg < 36 && type == ir::Type::V4U32)
					{
						// Full writes replace both scalar bank views. Keep full
						// reads explicit so intervening scalar writes are visible.
						if (write) cache[reg] = cache[reg + 2] = {};
						continue;
					}
					if (forward_quad_state && reg < 32 && type == ir::Type::V4U32)
					{
						if (write)
						{
							// A full write invalidates cached scalar views of this GPR.
							state = {};
							state.quad = inst.args[0];
						}
						else
						{
							if (state.quad)
							{
								inst.op = ir::Op::Copy;
								inst.args[0] = state.quad;
								inst.num_args = 1;
								inst.imm = 0;
							}
							state.quad = inst.value;
						}
						continue;
					}
					if (!ir::IsIntegerType(type))
					{
						state = {};
						continue;
					}
					if (write)
					{
						const bool wide = reg < 32 ? (inst.aux & ir::IF_WIDE_WRITE) != 0 : fn.ValueType(inst.args[0]) == ir::Type::I64;
						// A partial write changes the low half, so the old full value
						// is unusable even though the architectural upper half survives.
						state = wide ? CachedState{0, inst.args[0], false} : CachedState{inst.args[0], 0, true};
						continue;
					}
					const bool wide = inst.type == ir::Type::I64;
					const u32 source = wide ? state.wide : state.narrow;
					if (source)
					{
						inst.op = ir::Op::Copy;
						inst.args[0] = source;
						inst.num_args = 1;
					}
					else if ((!wide && state.wide) || (wide && state.narrow && state.sign_extended))
					{
						inst.op = wide ? ir::Op::Sext32 : ir::Op::Trunc32;
						inst.args[0] = wide ? state.narrow : state.wide;
						inst.num_args = 1;
					}
					if (wide)
						state.wide = inst.value;
					else
						state.narrow = inst.value;
					// A narrow READ alone says nothing about bits 32..63. Only a
					// narrow architectural WRITE establishes the sign extension.
					if (inst.op == ir::Op::Copy || inst.op == ir::Op::Sext32 || inst.op == ir::Op::Trunc32)
						inst.imm = 0;
				}
			}
		}

		class Lowerer
		{
		public:
			Lowerer(ir::Function& fn, const LowerOptions& options)
				: m_fn(fn)
				, m_guest_state(options.guest_state)
				, m_inline(options.inline_body)
				, m_hooks(options.hooks)
				, m_capture_exit(options.capture_exit_pc)
				, m_materialize_constants(options.materialize_constants)
				, m_allocate_registers(options.allocate_registers)
				, m_allocate_vectors(options.allocate_registers && options.allocate_vector_registers)
				, m_inline_division(options.optimize_ir && options.inline_division)
				, m_fuse_multiply_pairs(options.optimize_ir)
				, m_fuse_iop_nclips(options.optimize_ir && options.guest_state == GuestState::IOP)
				, m_fuse_ee_cop1_compares(options.optimize_ir && options.guest_state == GuestState::EE)
				, m_direct_quad_memory(options.optimize_ir && options.direct_quad_memory && (!CHECK_CACHE || CHECK_EEREC))
				, m_direct_ee_loads(options.guest_state == GuestState::EE && options.optimize_ir && options.direct_ee_loads && (!CHECK_CACHE || CHECK_EEREC))
				, m_fastmem_ee_loads(options.fastmem_ee_loads && options.inline_body && options.hooks &&
					options.hooks->ee_fastmem_abi && options.hooks->before_memory && options.hooks->after_memory)
				, m_fastmem_ee_cold_context(options.fastmem_ee_cold_context)
				, m_direct_ee_stores(options.guest_state == GuestState::EE && options.optimize_ir && options.direct_ee_stores && (!CHECK_CACHE || CHECK_EEREC))
				, m_direct_quad_vectors(options.direct_quad_vectors)
				, m_direct_iop_loads(options.optimize_ir && options.direct_iop_loads)
				, m_reuse_spill_slots(options.optimize_ir && options.reuse_spill_slots)
			{
			}

			bool Run(u8* code, size_t capacity, LowerOutput* out, std::string* error);

			oak::XReg FrameBase() const { return m_inline ? oak::util::X20 : oak::util::X28; }
			oak::XReg GuestBase() const { return m_inline ? oak::util::X27 : oak::util::X19; }

		private:
			u32 SaveArea() const { return m_inline ? 0u : (m_allocate_registers ? 64u : 32u); }
			static constexpr int kUnallocated = -1;
			void AllocateRegisters();
			void PlanIopNclips();
			void EmitIopNclip();
			void PlanIopAvsz();
			void EmitIopAvsz(bool avsz4);
			void PlanEeCop1Compares();
			void EmitEeCop1Compare(const EeCop1ComparePlan& plan);
			void AllocateVectorRegisters();
			bool IsVectorBarrier(const ir::Inst& inst) const;
			bool UsesDirectVectorMemory(const ir::Inst& inst) const
			{
				const bool scalar = m_direct_ee_loads && (inst.op == ir::Op::Load8U || inst.op == ir::Op::Load8S ||
					inst.op == ir::Op::Load16U || inst.op == ir::Op::Load16S || inst.op == ir::Op::Load32 || inst.op == ir::Op::Load64);
				const bool scalar_store = m_direct_ee_stores && (inst.op == ir::Op::Store8 || inst.op == ir::Op::Store16 ||
					inst.op == ir::Op::Store32 || inst.op == ir::Op::Store64);
				const bool quad = m_direct_quad_memory && m_direct_quad_vectors &&
					(inst.op == ir::Op::Load128 || inst.op == ir::Op::Store128);
				return m_allocate_vectors && (!m_hooks || m_hooks->memory_preserves_vectors) && (scalar || scalar_store || quad);
			}
			void EmitEeDirectAddress(oak::Label& slow)
			{
				// Entries store host_pointer - guest_address. Test the sign only
				// after adding the zero-extended guest address, including high aliases.
				oakLoad64(oak::util::X2, {GuestBase(), static_cast<s64>(offsetof(cpuRegistersPack, vtlbdata.vmap))});
				oakAsm->LSR(oak::util::W3, oak::util::W0, vtlb_private::VTLB_PAGE_BITS);
				oakAsm->LDR(oak::util::X2, oak::util::X2, oak::util::X3, oak::IndexExt::LSL, 3);
				oakAsm->ADD(oak::util::X2, oak::util::X2, oak::util::X0);
				oakAsm->TBNZ(oak::util::X2, 63, slow);
			}
			void SaveVectors(u32 position)
			{
				for (u32 value : m_vector_saves[position])
					oakStore128(oak::QReg(m_vector_registers[value]), {FrameBase(), static_cast<s64>(Slot(value))});
			}
			void RestoreVectors(u32 position)
			{
				for (u32 value : m_vector_restores[position])
					oakLoad128(oak::QReg(m_vector_registers[value]), {FrameBase(), static_cast<s64>(Slot(value))});
			}
			oak::QReg Result128(u32 value) const
			{
				return oak::QReg(!m_at_vector_barrier && m_vector_registers[value] >= 0 ? m_vector_registers[value] : 0);
			}
			oak::QReg Operand128(u32 value, const oak::QReg& scratch)
			{
				if (!m_at_vector_barrier && m_vector_registers[value] >= 0)
					return oak::QReg(m_vector_registers[value]);
				Load128(value, scratch);
				return scratch;
			}
			oak::WReg Result32(u32 value) const { return oak::WReg(m_registers[value] >= 0 ? m_registers[value] : 0); }
			oak::XReg Result64(u32 value) const { return oak::XReg(m_registers[value] >= 0 ? m_registers[value] : 0); }
			oak::WReg Operand32(u32 value, const oak::WReg& scratch)
			{
				if (m_registers[value] >= 0)
					return oak::WReg(m_registers[value]);
				Load32(value, scratch);
				return scratch;
			}
			oak::XReg Operand64(u32 value, const oak::XReg& scratch)
			{
				if (m_registers[value] >= 0)
					return oak::XReg(m_registers[value]);
				Load64(value, scratch);
				return scratch;
			}

			template <typename Reg>
			bool EmitAddSubImmediate(const ir::Inst& inst, const Reg& dst, const Reg& src, u64 constant)
			{
				return EmitSimpleAddSubImmediate(inst.op, inst.type, dst, src, constant);
			}

			u32 Slot(u32 value) const { return m_slots[value]; }

			void Load32(u32 value, const oak::WReg& dst)
			{
				if (m_registers[value] >= 0)
				{
					const oak::WReg src(m_registers[value]);
					// MOV Wn,Wn must execute when truncating a coalesced I64.
					if (dst.index() != src.index() || m_fn.ValueType(value) == ir::Type::I64)
						oakAsm->MOV(dst, src);
				}
				else if (m_constants[value])
					oakAsm->MOV(dst, static_cast<u32>(*m_constants[value]));
				else
					oakLoad32(dst, {FrameBase(), static_cast<s64>(Slot(value))});
			}

			void Load64(u32 value, const oak::XReg& dst)
			{
				if (m_registers[value] >= 0)
				{
					const oak::XReg src(m_registers[value]);
					if (dst.index() != src.index())
						oakAsm->MOV(dst, src);
				}
				else if (m_constants[value])
					oakAsm->MOV(dst, *m_constants[value]);
				else
					oakLoad64(dst, {FrameBase(), static_cast<s64>(Slot(value))});
			}

			void Store32(u32 value, const oak::WReg& src)
			{
				if (m_registers[value] >= 0)
				{
					const oak::WReg dst(m_registers[value]);
					if (dst.index() != src.index())
						oakAsm->MOV(dst, src);
				}
				else
					oakStore32(src, {FrameBase(), static_cast<s64>(Slot(value))});
			}

			void Store64(u32 value, const oak::XReg& src)
			{
				if (m_registers[value] >= 0)
				{
					const oak::XReg dst(m_registers[value]);
					if (dst.index() != src.index())
						oakAsm->MOV(dst, src);
				}
				else
					oakStore64(src, {FrameBase(), static_cast<s64>(Slot(value))});
			}

			void Load128(u32 value, const oak::QReg& dst)
			{
				if (!m_at_vector_barrier && m_vector_registers[value] >= 0)
				{
					const oak::QReg src(m_vector_registers[value]);
					if (dst.index() != src.index())
						oakAsm->MOV(dst.B16(), src.B16());
				}
				else
					oakLoad128(dst, {FrameBase(), static_cast<s64>(Slot(value))});
			}

			void Store128(u32 value, const oak::QReg& src)
			{
				if (!m_at_vector_barrier && m_vector_registers[value] >= 0)
				{
					const oak::QReg dst(m_vector_registers[value]);
					if (dst.index() != src.index())
						oakAsm->MOV(dst.B16(), src.B16());
				}
				else
					oakStore128(src, {FrameBase(), static_cast<s64>(Slot(value))});
			}

			void SlotAddress(u32 value, const oak::XReg& dst)
			{
				const u32 offset = Slot(value);
				if (offset <= 4095)
					oakAsm->ADD(dst, FrameBase(), offset);
				else
				{
					oakAsm->ADD(dst, FrameBase(), offset >> 12, oak::LslSymbol::LSL, 12);
					if (offset & 0xfff)
						oakAsm->ADD(dst, dst, offset & 0xfff);
				}
			}

			s64 GprOffset(u32 reg) const
			{
				if (m_guest_state == GuestState::IOP)
					return static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[0])) + reg * sizeof(u32);
				return static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[0])) +
					static_cast<s64>(reg) * static_cast<s64>(sizeof(GPR_reg));
			}

			s64 FprBitsOffset(u32 reg) const
			{
				return static_cast<s64>(offsetof(cpuRegistersPack, fpuRegs.fpr[0].UL)) +
					static_cast<s64>(reg) * static_cast<s64>(sizeof(FPRreg));
			}

			s64 LoOffset(u64 bank) const { return m_guest_state == GuestState::IOP ? offsetof(cpuRegistersPack, psxRegs.GPR.n.lo) : offsetof(cpuRegistersPack, cpuRegs.LO) + bank * sizeof(u64); }
			s64 HiOffset(u64 bank) const { return m_guest_state == GuestState::IOP ? offsetof(cpuRegistersPack, psxRegs.GPR.n.hi) : offsetof(cpuRegistersPack, cpuRegs.HI) + bank * sizeof(u64); }
			s64 PcOffset() const { return m_guest_state == GuestState::IOP ? offsetof(cpuRegistersPack, psxRegs.pc) : offsetof(cpuRegistersPack, cpuRegs.pc); }

			void EmitFrameAdjust(bool add, u32 amount);
			void EmitPrologue();
			void EmitEntryJump();
			void EmitEpilogue();
			bool EmitBlock(const ir::Block& block, std::string* error);
			bool TryEmitMultiplyPair(const ir::Inst& lo, const ir::Inst& hi);
			bool EmitInst(const ir::Inst& inst, std::string* error);
			bool EmitInstBody(const ir::Inst& inst, std::string* error);
			bool Fail(std::string* error, const char* what);

			ir::Function& m_fn;
			GuestState m_guest_state;
			std::vector<oak::Label> m_labels;
			u32 m_frame = 0;
			bool m_inline = false;
			const LowerHooks* m_hooks = nullptr;
			bool m_capture_exit = false;
			bool m_materialize_constants = true;
			bool m_allocate_registers = true;
			bool m_allocate_vectors = true;
			bool m_at_vector_barrier = false;
			u32 m_emit_position = 0;
			bool m_inline_division = true;
			bool m_fuse_multiply_pairs = true;
			u32 m_fused_multiply_pairs = 0;
			bool m_fuse_iop_nclips = false;
			u32 m_fused_iop_nclips = 0;
			u32 m_fused_iop_avsz = 0;
			bool m_fuse_ee_cop1_compares = false;
			u32 m_fused_ee_cop1_compares = 0;
			std::vector<EeCop1ComparePlan> m_ee_cop1_compare_starts;
			std::vector<IopAvszPlan> m_iop_avsz_starts;
			std::vector<bool> m_iop_nclip_starts, m_fused_values;
			bool m_direct_quad_memory = true;
			bool m_direct_ee_loads = true;
			bool m_fastmem_ee_loads = false;
			bool m_fastmem_ee_cold_context = false;
			struct FastLoadSite { uptr host; u32 guest_pc; bool delay_slot; };
			std::vector<FastLoadSite> m_fast_load_sites;
			bool m_direct_ee_stores = true;
			bool m_direct_quad_vectors = true;
			bool m_direct_iop_loads = true;
			bool m_reuse_spill_slots = true;
			u32 m_iop_memory_operations = 0;
			u32 m_direct_iop_load_operations = 0;
			u32 m_direct_quad_operations = 0;
			u32 m_direct_ee_load_operations = 0;
			u32 m_direct_ee_store_operations = 0;
			u32 m_direct_quad_vector_operations = 0;
			std::vector<std::optional<u64>> m_constants;
			std::vector<u32> m_slots;
			std::vector<int> m_registers;
			std::vector<int> m_vector_registers;
			std::vector<bool> m_vector_homes;
			std::vector<std::vector<u32>> m_vector_saves, m_vector_restores;
		};

		void Lowerer::PlanIopNclips()
		{
			m_fused_values.assign(m_fn.value_types.size(), false);
			size_t instruction_count = 0;
			for (const auto& block : m_fn.blocks) instruction_count += block.insts.size();
			m_iop_nclip_starts.assign(instruction_count, false);
			if (!m_fuse_iop_nclips) return;
			u32 position = 0;
			for (u32 block_index = 0; block_index < m_fn.blocks.size(); ++block_index)
			{
				const auto& insts = m_fn.blocks[block_index].insts;
				for (u32 i = 0; i < insts.size(); ++i, ++position)
				{
					if (insts[i].op != ir::Op::ReadCp2Data || insts[i].imm != 12 ||
						!PlanIopNclipWindow(m_fn, block_index, i, nullptr)) continue;
					m_iop_nclip_starts[position] = true;
					for (u32 node = 0; node < 24; ++node)
						if (const u32 value = insts[i + node].value) m_fused_values[value] = true;
					i += 23;
					position += 23;
				}
			}
		}

		void Lowerer::PlanIopAvsz()
		{
			size_t count = 0;
			for (const auto& block : m_fn.blocks) count += block.insts.size();
			m_iop_avsz_starts.resize(count);
			if (!m_fuse_iop_nclips) return; // Same optimize/IOP domain contract.
			u32 position = 0;
			for (u32 block_index = 0; block_index < m_fn.blocks.size(); ++block_index)
			{
				const auto& insts = m_fn.blocks[block_index].insts;
				for (u32 i = 0; i < insts.size(); ++i, ++position)
				{
					IopAvszPlan plan;
					if (insts[i].op != ir::Op::ConstI32 || insts[i].imm != 0 ||
						!PlanIopAvszWindow(m_fn, block_index, i, &plan, nullptr)) continue;
					m_iop_avsz_starts[position] = plan;
					for (u32 n = 0; n < plan.inst_count; ++n)
						if (const u32 value = insts[i + n].value) m_fused_values[value] = true;
					i += plan.inst_count - 1;
					position += plan.inst_count - 1;
				}
			}
		}

		void Lowerer::PlanEeCop1Compares()
		{
			size_t count = 0;
			for (const auto& block : m_fn.blocks) count += block.insts.size();
			m_ee_cop1_compare_starts.resize(count);
			if (!m_fuse_ee_cop1_compares) return;
			u32 position = 0;
			for (u32 block_index = 0; block_index < m_fn.blocks.size(); ++block_index)
			{
				const auto& insts = m_fn.blocks[block_index].insts;
				for (u32 i = 0; i < insts.size(); ++i, ++position)
				{
					EeCop1ComparePlan plan;
					if (insts.size() - i < 35 || insts[i].op != ir::Op::ConstI32 || insts[i].imm != 0 ||
						insts[i + 1].op != ir::Op::ConstI32 || insts[i + 1].imm != 0x7f800000u ||
						!PlanEeCop1CompareWindow(m_fn, block_index, i, &plan, nullptr)) continue;
					m_ee_cop1_compare_starts[position] = plan;
					for (u32 n = 0; n < plan.inst_count; ++n)
						if (const u32 value = insts[i + n].value) m_fused_values[value] = true;
					i += plan.inst_count - 1;
					position += plan.inst_count - 1;
				}
			}
		}

		void Lowerer::EmitEeCop1Compare(const EeCop1ComparePlan& plan)
		{
			// The strict graph has no escaping values. All live allocator GPRs
			// (X21-X23), stack homes, guest/frame bases and cycle registers survive.
			recBeginOaknutEmit();
			oakLoad32(oak::util::W0, {GuestBase(), FprBitsOffset(plan.lhs_fpr)});
			oakLoad32(oak::util::W1, {GuestBase(), FprBitsOffset(plan.rhs_fpr)});
			oakAsm->MOV(oak::util::W4, 0x7f800000u);
			oakAsm->MOV(oak::util::W3, 0x7f7fffffu);
			const auto normalize_key = [&](oak::WReg value) {
				oakAsm->AND(oak::util::W2, value, oak::util::W4);
				oakAsm->CMP(oak::util::W2, 0);
				oakAsm->CSEL(value, oak::util::WZR, value, oak::Cond::EQ);
				oakAsm->AND(oak::util::W5, value, 0x80000000u);
				oakAsm->ORR(oak::util::W5, oak::util::W5, oak::util::W3);
				oakAsm->CMP(oak::util::W2, oak::util::W4);
				oakAsm->CSEL(value, oak::util::W5, value, oak::Cond::EQ);
				oakAsm->ASR(oak::util::W2, value, 31);
				oakAsm->ORR(oak::util::W2, oak::util::W2, 0x80000000u);
				oakAsm->EOR(value, value, oak::util::W2);
			};
			// All exponent-zero words compare as zero. All exponent-255 words,
			// including NaNs, compare as signed max-finite, matching EE fpuDouble.
			normalize_key(oak::util::W0);
			normalize_key(oak::util::W1);
			oakAsm->CMP(oak::util::W0, oak::util::W1);
			const oak::Cond condition = plan.predicate == ir::Op::CmpEq ? oak::Cond::EQ :
				plan.predicate == ir::Op::CmpLtU ? oak::Cond::LO : oak::Cond::LS;
			oakAsm->CSET(oak::util::W0, condition);
			const s64 fcr31 = static_cast<s64>(offsetof(cpuRegistersPack, fpuRegs.fprc[31]));
			oakLoad32(oak::util::W2, {GuestBase(), fcr31});
			oakAsm->BFI(oak::util::W2, oak::util::W0, 23, 1);
			oakStore32(oak::util::W2, {GuestBase(), fcr31});
			recEndOaknutEmit();
		}

		void Lowerer::EmitIopAvsz(bool avsz4)
		{
			// The exact graph has only private SSA definitions. Use caller-save
			// scratch exclusively; X21-X23 and live spill slots remain untouched.
			recBeginOaknutEmit();
			const auto read_sz = [&](oak::WReg dst, u32 reg) {
				constexpr size_t base = offsetof(cpuRegistersPack, psxRegs.CP2D.r[0]);
				if constexpr ((base & 1u) == 0 && base + 19 * 4 < 8192)
					oakAsm->LDRH(dst, GuestBase(), oak::POffset<13, 1>(base + reg * 4));
				else
				{
					oakLoad32(dst, {GuestBase(), static_cast<s64>(base + reg * 4)});
					oakAsm->UXTH(dst, dst);
				}
			};
			read_sz(oak::util::W0, avsz4 ? 16 : 17);
			for (u32 reg = avsz4 ? 17 : 18; reg <= 19; ++reg)
			{
				read_sz(oak::util::W1, reg);
				oakAsm->ADD(oak::util::W0, oak::util::W0, oak::util::W1);
			}
			constexpr size_t scale_base = offsetof(cpuRegistersPack, psxRegs.CP2C.r[0]);
			const size_t scale = scale_base + (avsz4 ? 30 : 29) * 4;
			if constexpr ((scale_base & 1u) == 0 && scale_base + 30 * 4 < 8192)
				oakAsm->LDRSH(oak::util::W1, GuestBase(), oak::POffset<13, 1>(scale));
			else
			{
				oakLoad32(oak::util::W1, {GuestBase(), static_cast<s64>(scale)});
				oakAsm->SXTH(oak::util::W1, oak::util::W1);
			}
			// W multiplication wraps before the arithmetic shift, as in GTE.
			oakAsm->MUL(oak::util::W0, oak::util::W0, oak::util::W1);
			oakAsm->ASR(oak::util::W0, oak::util::W0, 12);
			oakStore32(oak::util::W0, {GuestBase(), static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[24]))});
			oakAsm->CMP(oak::util::W0, 0);
			oakAsm->CSEL(oak::util::W2, oak::util::W0, oak::util::WZR, oak::Cond::GE);
			oakAsm->MOV(oak::util::W3, 0xffffu);
			oakAsm->CMP(oak::util::W2, oak::util::W3);
			oakAsm->CSEL(oak::util::W2, oak::util::W2, oak::util::W3, oak::Cond::LS);
			// Raw CPU-state halfword write preserves D7's upper 16 bits.
			constexpr size_t otz = offsetof(cpuRegistersPack, psxRegs.CP2D.r[7]);
			if constexpr ((otz & 1u) == 0 && otz < 8192)
				oakAsm->STRH(oak::util::W2, GuestBase(), oak::POffset<13, 1>(otz));
			else
			{
				oakLoad32(oak::util::W1, {GuestBase(), static_cast<s64>(otz)});
				oakAsm->BFI(oak::util::W1, oak::util::W2, 0, 16);
				oakStore32(oak::util::W1, {GuestBase(), static_cast<s64>(otz)});
			}
			oakAsm->MOV(oak::util::W3, 0x80040000u);
			oakAsm->CMP(oak::util::W0, oak::util::W2);
			oakAsm->CSEL(oak::util::W3, oak::util::W3, oak::util::WZR, oak::Cond::NE);
			oakStore32(oak::util::W3, {GuestBase(), static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[31]))});
			recEndOaknutEmit();
		}

		void Lowerer::EmitIopNclip()
		{
			// The strict common-IR graph has no escaping definitions. Its fused
			// body uses only caller-save scratch; live SSA registers X21-X23 and
			// their stack homes remain intact across this expression.
			recBeginOaknutEmit();
			for (u32 vertex = 0; vertex < 3; ++vertex)
			{
				const oak::WReg y(vertex * 2), x(vertex * 2 + 1);
				constexpr size_t first = offsetof(cpuRegistersPack, psxRegs.CP2D.r[12]);
				if constexpr ((first & 1u) == 0 && first + 10 < 8192)
				{
					// The ARM64 register pack stores each signed X/Y halfword in
					// little-endian order. These are raw CPU-state reads, not guest
					// bus accesses, so combine load and signed extraction directly.
					oakAsm->LDRSH(x, GuestBase(), oak::POffset<13, 1>(first + vertex * 4));
					oakAsm->LDRSH(y, GuestBase(), oak::POffset<13, 1>(first + vertex * 4 + 2));
				}
				else
				{
					oakLoad32(y, {GuestBase(), static_cast<s64>(first + vertex * sizeof(u32))});
					oakAsm->SXTH(x, y);
					oakAsm->ASR(y, y, 16);
				}
			}
			oakAsm->SUB(oak::util::W16, oak::util::W2, oak::util::W4);
			oakAsm->MUL(oak::util::W16, oak::util::W1, oak::util::W16);
			oakAsm->SUB(oak::util::W17, oak::util::W4, oak::util::W0);
			oakAsm->MADD(oak::util::W16, oak::util::W3, oak::util::W17, oak::util::W16);
			oakAsm->SUB(oak::util::W17, oak::util::W0, oak::util::W2);
			oakAsm->MADD(oak::util::W16, oak::util::W5, oak::util::W17, oak::util::W16);
			oakStore32(oak::util::W16, {GuestBase(), static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[24]))});
			oakStore32(oak::util::WZR, {GuestBase(), static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[31]))});
			recEndOaknutEmit();
		}

		void Lowerer::AllocateRegisters()
		{
			m_registers.assign(m_fn.value_types.size(), kUnallocated);
			if (!m_allocate_registers)
				return;
			struct Interval { u32 value, first, last; };
			std::vector<Interval> intervals;
			std::vector<u32> last_use(m_fn.value_types.size(), 0);
			u32 position = 0;
			for (const ir::Block& block : m_fn.blocks)
			{
				for (const ir::Inst& inst : block.insts)
				{
					const u32 operands = ir::ValueOperandCount(inst);
					for (u32 i = 0; i < operands; ++i)
						last_use[inst.args[i]] = position;
					if (inst.value && !m_fused_values[inst.value] && !m_constants[inst.value] && ir::IsIntegerType(inst.type))
						intervals.push_back({inst.value, position, position});
					++position;
				}
			}
			// X20 is the inline frame base, X24 the cycle delta, X25 the old
			// fastmem base, X26 the VU clamp base, and X27..X29 dispatcher
			// bases. These three registers
			// are available within an EE block and survive AAPCS64 C helpers.
			constexpr int host_regs[] = {21, 22, 23};
			std::vector<Interval> active;
			for (Interval interval : intervals)
			{
				interval.last = std::max(interval.first, last_use[interval.value]);
				active.erase(std::remove_if(active.begin(), active.end(), [&](const Interval& live) {
					// Every emitter reads all operands before defining its result,
				// so a register can be reused at the operand's final use.
				return live.last <= interval.first;
				}), active.end());
				for (int reg : host_regs)
				{
					const bool busy = std::any_of(active.begin(), active.end(), [&](const Interval& live) {
						return m_registers[live.value] == reg;
					});
					if (!busy)
					{
						m_registers[interval.value] = reg;
						active.push_back(interval);
						break;
					}
				}
			}
		}

		bool Lowerer::IsVectorBarrier(const ir::Inst& inst) const
		{
			const auto kind = ir::Info(inst.op).kind;
			return kind == ir::OpKind::MemLoad || kind == ir::OpKind::MemStore || kind == ir::OpKind::Helper ||
				(!m_inline_division && (inst.op == ir::Op::DivS || inst.op == ir::Op::DivU ||
					inst.op == ir::Op::RemS || inst.op == ir::Op::RemU));
		}

		void Lowerer::AllocateVectorRegisters()
		{
			const u32 count = static_cast<u32>(m_fn.value_types.size());
			m_vector_registers.assign(count, kUnallocated);
			m_vector_homes.assign(count, false);
			if (!m_allocate_vectors)
				return;
			struct Interval { u32 value, first, last; };
			std::vector<Interval> intervals, active;
			std::vector<u32> last_use(count), barriers;
			u32 position = 0;
			for (const ir::Block& block : m_fn.blocks)
				for (const ir::Inst& inst : block.insts)
				{
					for (u32 arg = 0; arg < ir::ValueOperandCount(inst); ++arg)
						last_use[inst.args[arg]] = position;
					if (inst.value && inst.type == ir::Type::V4U32)
						intervals.push_back({inst.value, position, position});
					if (IsVectorBarrier(inst))
						barriers.push_back(position);
					++position;
				}
			m_vector_saves.resize(position);
			m_vector_restores.resize(position);
			// Q0/Q1 are emitter temporaries. Q2..Q7 are caller-save in full;
			// AAPCS64 preserves only the low 64 bits of V8..V15, not full quads.
			constexpr int host_regs[] = {2, 3, 4, 5, 6, 7};
			for (Interval interval : intervals)
			{
				interval.last = std::max(interval.first, last_use[interval.value]);
				active.erase(std::remove_if(active.begin(), active.end(), [&](const Interval& live) {
					return live.last <= interval.first;
				}), active.end());
				for (int reg : host_regs)
				{
					if (std::any_of(active.begin(), active.end(), [&](const Interval& live) {
						return m_vector_registers[live.value] == reg;
					}))
						continue;
					m_vector_registers[interval.value] = reg;
					active.push_back(interval);
					// Homes are needed only at barriers: live inputs are saved
					// before hooks, and live outputs restored after all hooks.
					// Inclusive endpoints cover Store128's pointer argument and
					// Load128's helper result, even at the final/first use.
					for (auto it = std::lower_bound(barriers.begin(), barriers.end(), interval.first);
						it != barriers.end() && *it <= interval.last; ++it)
					{
						m_vector_homes[interval.value] = true;
						if (interval.first < *it)
							m_vector_saves[*it].push_back(interval.value);
						if (*it < interval.last)
							m_vector_restores[*it].push_back(interval.value);
					}
					break;
				}
			}
		}

		bool Lowerer::Fail(std::string* error, const char* what)
		{
			if (error)
			{
				char buffer[96];
				std::snprintf(buffer, sizeof(buffer), "IR lowering: %s", what);
				*error = buffer;
			}
			return false;
		}

		void Lowerer::EmitFrameAdjust(bool add, u32 amount)
		{
			if (amount == 0)
				return;
			// AArch64 ADD/SUB immediates encode 0..4095 or an imm12 shifted by
			// 12, so larger frames are adjusted with two instructions.
			if (amount <= 4095)
			{
				if (add)
					oakAsm->ADD(oak::util::SP, oak::util::SP, amount);
				else
					oakAsm->SUB(oak::util::SP, oak::util::SP, amount);
				return;
			}

			const u32 hi = amount >> 12;
			const u32 lo = amount & 0xfffu;
			if (add)
			{
				oakAsm->ADD(oak::util::SP, oak::util::SP, lo);
				oakAsm->ADD(oak::util::SP, oak::util::SP, hi, oak::LslSymbol::LSL, 12);
			}
			else
			{
				oakAsm->SUB(oak::util::SP, oak::util::SP, hi, oak::LslSymbol::LSL, 12);
				oakAsm->SUB(oak::util::SP, oak::util::SP, lo);
			}
		}

		void Lowerer::EmitPrologue()
		{
			// Inline blocks use the dispatcher's saved registers; only actual
			// spills need a frame. Spill-free blocks need no SP/frame-base work.
			if (m_inline && m_frame == 0)
				return;
			recBeginOaknutEmit();
			EmitFrameAdjust(false, m_frame);
			if (m_inline)
			{
				oakAsm->MOV(oak::util::X20, oak::util::SP);
			}
			else
			{
				oakAsm->STP(oak::util::X19, oak::util::X30, oak::util::SP, oak::SOffset<10, 3>(0));
				oakAsm->STP(oak::util::X28, oak::util::XZR, oak::util::SP, oak::SOffset<10, 3>(16));
				if (m_allocate_registers)
				{
					oakAsm->STP(oak::util::X21, oak::util::X22, oak::util::SP, oak::SOffset<10, 3>(32));
					oakAsm->STP(oak::util::X23, oak::util::XZR, oak::util::SP, oak::SOffset<10, 3>(48));
				}
				oakAsm->MOV(oak::util::X28, oak::util::SP);
				oakMoveAddressToReg(oak::util::X19, &g_cpuRegistersPack);
			}
			recEndOaknutEmit();
		}

		void Lowerer::EmitEntryJump()
		{
			// Block ids follow storage order, but the declared entry can differ.
			// The normal lifter uses entry 1 and pays no runtime instruction here.
			if (m_fn.entry == 1)
				return;
			recBeginOaknutEmit();
			oakAsm->B(m_labels[m_fn.entry - 1]);
			recEndOaknutEmit();
		}

		void Lowerer::EmitEpilogue()
		{
			recBeginOaknutEmit();
			if (m_inline)
			{
				EmitFrameAdjust(true, m_frame);
			}
			else
			{
				oakAsm->LDP(oak::util::X28, oak::util::XZR, oak::util::SP, oak::SOffset<10, 3>(16));
				if (m_allocate_registers)
				{
					oakAsm->LDP(oak::util::X21, oak::util::X22, oak::util::SP, oak::SOffset<10, 3>(32));
					oakAsm->LDP(oak::util::X23, oak::util::XZR, oak::util::SP, oak::SOffset<10, 3>(48));
				}
				oakAsm->LDP(oak::util::X19, oak::util::X30, oak::util::SP, oak::SOffset<10, 3>(0));
				EmitFrameAdjust(true, m_frame);
				oakAsm->RET();
			}
			recEndOaknutEmit();
		}

		bool Lowerer::Run(u8* code, size_t capacity, LowerOutput* out, std::string* error)
		{
			if (!CanLower(m_fn, m_inline, error, m_guest_state))
				return false;

			const u32 value_count = static_cast<u32>(m_fn.value_types.size());
			m_constants.resize(value_count);
			m_slots.resize(value_count);
			if (m_materialize_constants)
			{
				for (const ir::Block& block : m_fn.blocks)
				{
					for (const ir::Inst& inst : block.insts)
					{
						if (inst.op == ir::Op::ConstI32 || inst.op == ir::Op::ConstI64)
							m_constants[inst.value] = inst.imm;
					}
				}
			}
			PlanIopNclips();
			PlanIopAvsz();
			PlanEeCop1Compares();
			AllocateRegisters();
			AllocateVectorRegisters();
			u32 next_slot = SaveArea();
			struct Spill { u32 last, size, slot; };
			std::array<std::vector<u32>, 2> free_slots;
			for (const ir::Block& block : m_fn.blocks)
			{
				std::vector<u32> last_use(value_count);
				u32 position = 0;
				for (const ir::Inst& inst : block.insts)
				{
					for (u32 arg = 0; arg < ir::ValueOperandCount(inst); ++arg)
						last_use[inst.args[arg]] = position;
					++position;
				}
				std::vector<Spill> active;
				position = 0;
				for (const ir::Inst& inst : block.insts)
				{
					if (m_reuse_spill_slots)
					{
						active.erase(std::remove_if(active.begin(), active.end(), [&](const Spill& spill) {
							// Emitters read all operands before writing the result,
							// including addresses passed to memory helpers.
							if (spill.last > position)
								return false;
							free_slots[spill.size == 16].push_back(spill.slot);
							return true;
						}), active.end());
					}
					const u32 value = inst.value;
					const bool allocated = value && (m_registers[value] >= 0 || m_vector_registers[value] >= 0);
					if (allocated)
						++out->register_values;
					if (value && m_vector_registers[value] >= 0)
					{
						++out->vector_register_values;
						if (m_vector_homes[value])
							++out->vector_save_values;
					}
					if (value && !m_fused_values[value] && !m_constants[value] && (!allocated || m_vector_homes[value]))
					{
						++out->spill_values;
						const u32 size = std::max(8u, ir::TypeSize(inst.type));
						auto& free = free_slots[size == 16];
						if (m_reuse_spill_slots && !free.empty())
						{
							m_slots[value] = free.back();
							free.pop_back();
						}
						else
						{
							next_slot = (next_slot + size - 1u) & ~(size - 1u);
							m_slots[value] = next_slot;
							next_slot += size;
							++out->spill_slots;
						}
						active.push_back({std::max(position, last_use[value]), size, m_slots[value]});
					}
					++position;
				}
				// SSA values cannot cross blocks; all typed slots are reusable
				// on every successor, including loops and conditional paths.
				for (const Spill& spill : active)
					free_slots[spill.size == 16].push_back(spill.slot);
			}
			m_frame = (next_slot + 15u) & ~15u;
			if (m_frame > 0x3f00)
				return Fail(error, "stack frame too large");
			out->frame_size = m_frame;

			if (m_inline)
			{
				if (!m_hooks)
					return Fail(error, "inline mode requires exit hooks");
				for (const ir::Block& block : m_fn.blocks)
				{
					for (const ir::Inst& inst : block.insts)
					{
						if ((inst.op == ir::Op::Resume && !m_hooks->guest_exit) ||
							(inst.op == ir::Op::BranchIndirect && !m_hooks->indirect_exit))
							return Fail(error, "missing inline exit hook");
					}
				}

				// Emit into the caller's active Oaknut block. The caller is
				// responsible for the code buffer and for the exit hooks.
				m_labels.resize(m_fn.blocks.size());
				u8* const start = oakGetCurrentCodePointer();
				EmitPrologue();
				EmitEntryJump();
				for (const ir::Block& block : m_fn.blocks)
				{
					oakAsm->l(m_labels[block.id - 1]);
					if (!EmitBlock(block, error))
						return false;
				}
				out->entry = nullptr;
				out->host_size = static_cast<u32>(oakGetCurrentCodePointer() - start);
				out->direct_quad_operations = m_direct_quad_operations;
				out->direct_ee_load_operations = m_direct_ee_load_operations;
				// Publish only after every instruction has lowered successfully.
#if defined(ARCH_ARM64) && defined(__ANDROID__)
				for (const FastLoadSite& site : m_fast_load_sites)
					vtlb_AddLoadStoreInfoIr32(site.host, site.guest_pc, site.delay_slot);
#endif
				out->fastmem_ee_load_operations = static_cast<u32>(m_fast_load_sites.size());
				out->cold_context_ee_load_operations = m_fastmem_ee_cold_context ? out->fastmem_ee_load_operations : 0;
				out->direct_ee_store_operations = m_direct_ee_store_operations;
				out->direct_quad_vector_operations = m_direct_quad_vector_operations;
				out->iop_memory_operations = m_iop_memory_operations;
				out->direct_iop_load_operations = m_direct_iop_load_operations;
				out->fused_multiply_pairs = m_fused_multiply_pairs;
				out->fused_iop_nclips = m_fused_iop_nclips;
				out->fused_iop_avsz = m_fused_iop_avsz;
				out->fused_ee_cop1_compares = m_fused_ee_cop1_compares;
				return true;
			}

			oakSetAsmPtr(code, capacity);
			u8* const start = oakStartBlock();

			m_labels.resize(m_fn.blocks.size());
			EmitPrologue();
			EmitEntryJump();

			for (const ir::Block& block : m_fn.blocks)
			{
				oakAsm->l(m_labels[block.id - 1]);
				if (!EmitBlock(block, error))
					return false;
			}

			const u8* const end = oakEndBlock();
			out->entry = start;
			out->host_size = static_cast<u32>(end - start);
			out->direct_quad_operations = m_direct_quad_operations;
			out->direct_ee_load_operations = m_direct_ee_load_operations;
			out->direct_ee_store_operations = m_direct_ee_store_operations;
			out->direct_quad_vector_operations = m_direct_quad_vector_operations;
			out->iop_memory_operations = m_iop_memory_operations;
			out->direct_iop_load_operations = m_direct_iop_load_operations;
			out->fused_multiply_pairs = m_fused_multiply_pairs;
			out->fused_iop_nclips = m_fused_iop_nclips;
			out->fused_iop_avsz = m_fused_iop_avsz;
			out->fused_ee_cop1_compares = m_fused_ee_cop1_compares;
			return true;
		}

		bool Lowerer::TryEmitMultiplyPair(const ir::Inst& lo, const ir::Inst& hi)
		{
			if (!m_fuse_multiply_pairs || lo.op != ir::Op::Mul || lo.type != ir::Type::I32 ||
				(hi.op != ir::Op::MulHiS && hi.op != ir::Op::MulHiU) || hi.type != ir::Type::I32 ||
				lo.args[0] != hi.args[0] || lo.args[1] != hi.args[1] ||
				m_fn.ValueType(lo.args[0]) != ir::Type::I32 || m_fn.ValueType(lo.args[1]) != ir::Type::I32)
				return false;
			// Read both inputs before defining either result, matching the existing
			// allocator's final-use coalescing contract. X2 is outside its pool.
			recBeginOaknutEmit();
			const auto lhs = Operand32(lo.args[0], oak::util::W0);
			const auto rhs = Operand32(lo.args[1], oak::util::W1);
			if (hi.op == ir::Op::MulHiS)
				oakAsm->SMULL(oak::util::X2, lhs, rhs);
			else
				oakAsm->UMULL(oak::util::X2, lhs, rhs);
			Store32(lo.value, oak::util::W2);
			const auto dst_hi = Result32(hi.value);
			oakAsm->LSR(dst_hi.toX(), oak::util::X2, 32);
			Store32(hi.value, dst_hi);
			recEndOaknutEmit();
			// Vector save/restore schedules use original IR instruction positions.
			m_emit_position += 2;
			++m_fused_multiply_pairs;
			return true;
		}

		bool Lowerer::EmitBlock(const ir::Block& block, std::string* error)
		{
			for (size_t i = 0; i < block.insts.size(); ++i)
			{
				if (m_iop_nclip_starts[m_emit_position])
				{
					EmitIopNclip();
					m_emit_position += 24;
					i += 23;
					++m_fused_iop_nclips;
				}
				else if (const IopAvszPlan& plan = m_iop_avsz_starts[m_emit_position]; plan.inst_count != 0)
				{
					EmitIopAvsz(plan.avsz4);
					m_emit_position += plan.inst_count;
					i += plan.inst_count - 1;
					++m_fused_iop_avsz;
				}
				else if (const EeCop1ComparePlan& plan = m_ee_cop1_compare_starts[m_emit_position]; plan.inst_count != 0)
				{
					EmitEeCop1Compare(plan);
					m_emit_position += plan.inst_count;
					i += plan.inst_count - 1;
					++m_fused_ee_cop1_compares;
				}
				else if (i + 1 < block.insts.size() && TryEmitMultiplyPair(block.insts[i], block.insts[i + 1]))
					++i;
				else if (!EmitInst(block.insts[i], error))
					return false;
			}
			return true;
		}

		bool Lowerer::EmitInst(const ir::Inst& inst, std::string* error)
		{
			const u32 position = m_emit_position++;
			if (!m_allocate_vectors || !IsVectorBarrier(inst) || UsesDirectVectorMemory(inst))
				return EmitInstBody(inst, error);
			m_at_vector_barrier = true;
			recBeginOaknutEmit();
			SaveVectors(position);
			recEndOaknutEmit();
			if (!EmitInstBody(inst, error))
				return false;
			recBeginOaknutEmit();
			RestoreVectors(position);
			recEndOaknutEmit();
			m_at_vector_barrier = false;
			return true;
		}

		bool Lowerer::EmitInstBody(const ir::Inst& inst, std::string* error)
		{
			const u32* a = inst.args;
			if (inst.value && m_constants[inst.value])
				return true;

			if (m_inline && (inst.op == ir::Op::Trap || inst.op == ir::Op::CheckEvents ||
					inst.op == ir::Op::Return))
			{
				return Fail(error, "inline mode does not support this terminator");
			}

			auto load_a = [&]() { Load32(a[0], oak::util::W0); };
			auto load_b = [&]() { Load32(a[1], oak::util::W1); };
			auto store_r = [&]() { Store32(inst.value, oak::util::W0); };
			auto call_helper = [&](const void* fn) {
				if (m_hooks && m_hooks->before_helper)
					m_hooks->before_helper(m_hooks->ctx);
				oakEmitCall(fn);
				if (m_hooks && m_hooks->after_helper)
					m_hooks->after_helper(m_hooks->ctx);
			};

			switch (inst.op)
			{
				case ir::Op::Nop:
					return true;

				case ir::Op::ConstI32:
				case ir::Op::ConstF32:
					recBeginOaknutEmit();
					oakAsm->MOV(oak::util::W0, static_cast<u32>(inst.imm));
					store_r();
					recEndOaknutEmit();
					return true;

				case ir::Op::ConstI64:
					recBeginOaknutEmit();
					oakAsm->MOV(oak::util::X0, inst.imm);
					Store64(inst.value, oak::util::X0);
					recEndOaknutEmit();
					return true;

				case ir::Op::ConstVec:
				{
					const auto& lanes = m_fn.vec_consts[inst.imm];
					recBeginOaknutEmit();
					const auto dst = Result128(inst.value);
					oakAsm->MOV(oak::util::X0, u64(lanes[0]) | (u64(lanes[1]) << 32));
					oakAsm->MOV(oak::util::X1, u64(lanes[2]) | (u64(lanes[3]) << 32));
					if (m_vector_registers[inst.value] >= 0)
					{
						oakAsm->FMOV(dst.toD(), oak::util::X0);
						oakAsm->INS(dst.Delem()[1], oak::util::X1);
					}
					else
					{
						oakStore64(oak::util::X0, {FrameBase(), static_cast<s64>(Slot(inst.value))});
						oakStore64(oak::util::X1, {FrameBase(), static_cast<s64>(Slot(inst.value) + 8)});
					}
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::Copy:
					if (m_reuse_spill_slots && m_registers[inst.value] < 0 && m_registers[a[0]] < 0 &&
						m_vector_registers[inst.value] < 0 && m_vector_registers[a[0]] < 0 &&
						!m_constants[a[0]] && Slot(inst.value) == Slot(a[0]))
						return true;
					if (inst.type == ir::Type::V4U32)
					{
						recBeginOaknutEmit();
						Load128(a[0], Result128(inst.value));
						Store128(inst.value, Result128(inst.value));
						recEndOaknutEmit();
					}
					else if (inst.type == ir::Type::I64)
					{
						recBeginOaknutEmit();
						Load64(a[0], Result64(inst.value));
						Store64(inst.value, Result64(inst.value));
						recEndOaknutEmit();
					}
					else
					{
						recBeginOaknutEmit();
						Load32(a[0], Result32(inst.value));
						Store32(inst.value, Result32(inst.value));
						recEndOaknutEmit();
					}
					return true;

				case ir::Op::Undef:
					recBeginOaknutEmit();
					oakAsm->MOV(oak::util::W0, 0);
					store_r();
					recEndOaknutEmit();
					return true;

				case ir::Op::Select:
					recBeginOaknutEmit();
					Load32(a[0], oak::util::W0);
					if (inst.type == ir::Type::I64)
					{
						Load64(a[1], oak::util::X1);
						Load64(a[2], oak::util::X2);
						oakAsm->CMP(oak::util::W0, 0);
						oakAsm->CSEL(oak::util::X0, oak::util::X1, oak::util::X2, oak::Cond::NE);
						Store64(inst.value, oak::util::X0);
					}
					else
					{
						Load32(a[1], oak::util::W1);
						Load32(a[2], oak::util::W2);
						oakAsm->CMP(oak::util::W0, 0);
						oakAsm->CSEL(oak::util::W0, oak::util::W1, oak::util::W2, oak::Cond::NE);
						store_r();
					}
					recEndOaknutEmit();
					return true;

				case ir::Op::Clz:
					recBeginOaknutEmit();
					if (inst.type == ir::Type::I64)
					{
						const auto src = Operand64(a[0], oak::util::X0);
						const auto dst = Result64(inst.value);
						oakAsm->CLZ(dst, src);
						Store64(inst.value, dst);
					}
					else
					{
						const auto src = Operand32(a[0], oak::util::W0);
						const auto dst = Result32(inst.value);
						oakAsm->CLZ(dst, src);
						Store32(inst.value, dst);
					}
					recEndOaknutEmit();
					return true;

				case ir::Op::Not:
					recBeginOaknutEmit();
					if (inst.type == ir::Type::V4U32)
					{
						const auto src = Operand128(a[0], oak::util::Q0);
						const auto dst = Result128(inst.value);
						oakAsm->NOT(dst.B16(), src.B16());
						Store128(inst.value, dst);
					}
					else if (inst.type == ir::Type::I64)
					{
						const auto src = Operand64(a[0], oak::util::X0);
						const auto dst = Result64(inst.value);
						oakAsm->MVN(dst, src);
						Store64(inst.value, dst);
					}
					else
					{
						const auto src = Operand32(a[0], oak::util::W0);
						const auto dst = Result32(inst.value);
						oakAsm->MVN(dst, src);
						Store32(inst.value, dst);
					}
					recEndOaknutEmit();
					return true;

				case ir::Op::Neg:
				case ir::Op::Sext8:
				case ir::Op::Sext16:
				case ir::Op::Zext8:
				case ir::Op::Zext16:
				{
					recBeginOaknutEmit();
					const auto src = Operand32(a[0], oak::util::W0);
					const auto dst = Result32(inst.value);
					switch (inst.op)
					{
						case ir::Op::Neg: oakAsm->NEG(dst, src); break;
						case ir::Op::Sext8: oakAsm->SXTB(dst, src); break;
						case ir::Op::Sext16: oakAsm->SXTH(dst, src); break;
						case ir::Op::Zext8: oakAsm->UXTB(dst, src); break;
						default: oakAsm->UXTH(dst, src); break;
					}
					Store32(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}
				case ir::Op::Sext32:
				{
					recBeginOaknutEmit();
					const auto src = Operand32(a[0], oak::util::W0);
					const auto dst = Result64(inst.value);
					oakAsm->SXTW(dst, src);
					Store64(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}
				case ir::Op::Zext32:
				{
					recBeginOaknutEmit();
					// A W-register write zeroes the upper word, including in-place.
					Load32(a[0], oak::WReg(Result64(inst.value).index()));
					Store64(inst.value, Result64(inst.value));
					recEndOaknutEmit();
					return true;
				}
				case ir::Op::Trunc32:
					recBeginOaknutEmit();
					Load32(a[0], Result32(inst.value));
					Store32(inst.value, Result32(inst.value));
					recEndOaknutEmit();
					return true;

				case ir::Op::Add:
				case ir::Op::Sub:
				case ir::Op::And:
				case ir::Op::Or:
				case ir::Op::Xor:
				case ir::Op::MinS:
				case ir::Op::MinU:
				case ir::Op::MaxS:
				case ir::Op::MaxU:
				{
					recBeginOaknutEmit();
					if (inst.type == ir::Type::V4U32)
					{
						// All operand registers are read before a coalesced result is written.
						const auto lhs = Operand128(a[0], oak::util::Q0);
						const auto rhs = Operand128(a[1], oak::util::Q1);
						const auto dst = Result128(inst.value);
						switch (inst.op)
						{
							case ir::Op::Add: oakAsm->ADD(dst.S4(), lhs.S4(), rhs.S4()); break;
							case ir::Op::Sub: oakAsm->SUB(dst.S4(), lhs.S4(), rhs.S4()); break;
							case ir::Op::And: oakAsm->AND(dst.B16(), lhs.B16(), rhs.B16()); break;
							case ir::Op::Or: oakAsm->ORR(dst.B16(), lhs.B16(), rhs.B16()); break;
							case ir::Op::Xor: oakAsm->EOR(dst.B16(), lhs.B16(), rhs.B16()); break;
							default: return Fail(error, "unsupported vector arithmetic op");
						}
						Store128(inst.value, dst);
					}
					else if (inst.type == ir::Type::I64)
					{
						const bool commute = (inst.op == ir::Op::And || inst.op == ir::Op::Or || inst.op == ir::Op::Xor) &&
							m_constants[a[0]] && !m_constants[a[1]];
						const u32 left = a[commute ? 1 : 0], right = a[commute ? 0 : 1];
						const auto lhs = Operand64(left, oak::util::X0);
						const auto dst = Result64(inst.value);
						if (m_constants[right] && (EmitAddSubImmediate(inst, dst, lhs, *m_constants[right]) ||
							EmitSimpleLogicalImmediate(inst.op, dst, lhs, *m_constants[right])))
						{
							Store64(inst.value, dst);
							recEndOaknutEmit();
							return true;
						}
						const auto rhs = Operand64(right, oak::util::X1);
						switch (inst.op)
						{
							case ir::Op::Add: oakAsm->ADD(dst, lhs, rhs); break;
							case ir::Op::Sub: oakAsm->SUB(dst, lhs, rhs); break;
							case ir::Op::And: oakAsm->AND(dst, lhs, rhs); break;
							case ir::Op::Or: oakAsm->ORR(dst, lhs, rhs); break;
							case ir::Op::Xor: oakAsm->EOR(dst, lhs, rhs); break;
							default: return Fail(error, "unsupported arithmetic op");
						}
						Store64(inst.value, dst);
					}
					else
					{
						const bool commute = (inst.op == ir::Op::And || inst.op == ir::Op::Or || inst.op == ir::Op::Xor) &&
							m_constants[a[0]] && !m_constants[a[1]];
						const u32 left = a[commute ? 1 : 0], right = a[commute ? 0 : 1];
						const auto lhs = Operand32(left, oak::util::W0);
						const auto dst = Result32(inst.value);
						if (m_constants[right] && (EmitAddSubImmediate(inst, dst, lhs, *m_constants[right]) ||
							EmitSimpleLogicalImmediate(inst.op, dst, lhs, *m_constants[right])))
						{
							Store32(inst.value, dst);
							recEndOaknutEmit();
							return true;
						}
						const auto rhs = Operand32(right, oak::util::W1);
						switch (inst.op)
						{
							case ir::Op::Add:
							case ir::Op::Sub:
							case ir::Op::And:
							case ir::Op::Or:
							case ir::Op::Xor:
								EmitSimpleAlu32(inst.op, dst, lhs, rhs);
								break;
							case ir::Op::MinS:
								oakAsm->CMP(lhs, rhs);
								oakAsm->CSEL(dst, lhs, rhs, oak::Cond::LT);
								break;
							case ir::Op::MinU:
								oakAsm->CMP(lhs, rhs);
								oakAsm->CSEL(dst, lhs, rhs, oak::Cond::LO);
								break;
							case ir::Op::MaxS:
								oakAsm->CMP(lhs, rhs);
								oakAsm->CSEL(dst, lhs, rhs, oak::Cond::GT);
								break;
							case ir::Op::MaxU:
								oakAsm->CMP(lhs, rhs);
								oakAsm->CSEL(dst, lhs, rhs, oak::Cond::HI);
								break;
							default: return Fail(error, "unsupported arithmetic op");
						}
						Store32(inst.value, dst);
					}
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::Mul:
					recBeginOaknutEmit();
					if (inst.type == ir::Type::I64)
					{
						const auto lhs = Operand64(a[0], oak::util::X0);
						const auto rhs = Operand64(a[1], oak::util::X1);
						const auto dst = Result64(inst.value);
						oakAsm->MUL(dst, lhs, rhs);
						Store64(inst.value, dst);
					}
					else
					{
						const auto lhs = Operand32(a[0], oak::util::W0);
						const auto rhs = Operand32(a[1], oak::util::W1);
						const auto dst = Result32(inst.value);
						oakAsm->MUL(dst, lhs, rhs);
						Store32(inst.value, dst);
					}
					recEndOaknutEmit();
					return true;

				case ir::Op::MulHiS:
				case ir::Op::MulHiU:
				{
					recBeginOaknutEmit();
					const auto lhs = Operand32(a[0], oak::util::W0);
					const auto rhs = Operand32(a[1], oak::util::W1);
					if (inst.op == ir::Op::MulHiS)
						oakAsm->SMULL(oak::util::X2, lhs, rhs);
					else
						oakAsm->UMULL(oak::util::X2, lhs, rhs);
					const auto dst = Result32(inst.value);
					oakAsm->LSR(dst.toX(), oak::util::X2, 32);
					Store32(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::VShuffleBytes:
				{
					recBeginOaknutEmit();
					const auto src = Operand128(a[0], oak::util::Q0);
					const auto dst = Result128(inst.value);
					if (inst.imm == 0xfedcba9876543210ull)
					{
						if (src.index() != dst.index()) oakAsm->MOV(dst.B16(), src.B16());
					}
					else if (inst.imm == (inst.imm & 15) * 0x1111111111111111ull)
						oakAsm->DUP(dst.B16(), src.Belem()[inst.imm & 15]);
					else if (inst.imm == 0x98badcfe10325476ull) // PREVH
						oakAsm->REV64(dst.H8(), src.H8());
					else if (inst.imm == 0x9898989810101010ull) // PCPYH
					{
						// Snapshot upper half first: dst may reuse src.
						oakAsm->DUP(oak::util::Q1.H8(), src.Helem()[4]);
						oakAsm->DUP(dst.H8(), src.Helem()[0]);
						oakAsm->INS(dst.Delem()[1], oak::util::Q1.Delem()[0]);
					}
					else
					{
						u64 table[2] = {};
						for (u32 byte = 0; byte < 16; ++byte)
							table[byte / 8] |= ((inst.imm >> (byte * 4)) & 15) << ((byte % 8) * 8);
						oakAsm->MOV(oak::util::X0, table[0]);
						oakAsm->MOV(oak::util::X1, table[1]);
						oakAsm->FMOV(oak::util::Q1.toD(), oak::util::X0);
						oakAsm->INS(oak::util::Q1.Delem()[1], oak::util::X1);
						oakAsm->TBL(dst.B16(), oak::List(src.B16()), oak::util::Q1.B16());
					}
					Store128(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::VZipLo:
				case ir::Op::VZipHi:
				case ir::Op::VPackEven:
				case ir::Op::VInterleaveEven:
				case ir::Op::VInterleaveOdd:
				{
					recBeginOaknutEmit();
					const auto lhs = Operand128(a[0], oak::util::Q0);
					const auto rhs = Operand128(a[1], oak::util::Q1);
					const auto dst = Result128(inst.value);
					const auto emit = [&](auto d, auto s, auto t) {
						switch (inst.op)
						{
							case ir::Op::VZipLo: oakAsm->ZIP1(d, s, t); break;
							case ir::Op::VZipHi: oakAsm->ZIP2(d, s, t); break;
							case ir::Op::VPackEven: oakAsm->UZP1(d, s, t); break;
							case ir::Op::VInterleaveEven: oakAsm->TRN1(d, s, t); break;
							case ir::Op::VInterleaveOdd: oakAsm->TRN2(d, s, t); break;
							default: break;
						}
					};
					if (inst.imm == 8) emit(dst.B16(), lhs.B16(), rhs.B16());
					else if (inst.imm == 16) emit(dst.H8(), lhs.H8(), rhs.H8());
					else emit(dst.S4(), lhs.S4(), rhs.S4());
					Store128(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::VShuffle:
				{
					recBeginOaknutEmit();
					const auto src = Operand128(a[0], oak::util::Q0);
					const auto dst = Result128(inst.value);
					const u32 selectors = static_cast<u32>(inst.imm);
					if (selectors == 0xe4) // identity: 0,1,2,3
					{
						if (src.index() != dst.index())
							oakAsm->MOV(dst.B16(), src.B16());
					}
					else if (selectors == (selectors & 3) * 0x55) // broadcast
						oakAsm->DUP(dst.S4(), src.Selem()[selectors & 3]);
					else if (selectors == 0x4e) // swap 64-bit halves (PINTH source)
						oakAsm->EXT(dst.B16(), src.B16(), src.B16(), 8);
					else if (selectors == 0xc6) // 2,1,0,3 (PEXEW)
					{
						oakAsm->REV64(dst.S4(), src.S4());
						oakAsm->EXT(dst.B16(), dst.B16(), dst.B16(), 12);
					}
					else if (selectors == 0xc9) // 1,2,0,3 (PROT3W)
					{
						oakAsm->REV64(oak::util::Q1.S4(), src.S4());
						oakAsm->EXT(oak::util::Q0.B16(), src.B16(), src.B16(), 8);
						oakAsm->ZIP1(dst.S4(), oak::util::Q1.S4(), oak::util::Q0.S4());
					}
					else if (selectors == 0xd8) // 0,2,1,3 (PEXCW)
					{
						oakAsm->EXT(oak::util::Q1.B16(), src.B16(), src.B16(), 8);
						oakAsm->ZIP1(dst.S4(), src.S4(), oak::util::Q1.S4());
					}
					else
					{
						// Snapshot first: dst may reuse the dying input's register.
						auto input = src;
						if (src.index() == dst.index())
						{
							oakAsm->MOV(oak::util::Q1.B16(), src.B16());
							input = oak::util::Q1;
						}
						oakAsm->DUP(dst.S4(), input.Selem()[selectors & 3]);
						for (u32 lane = 1; lane < 4; ++lane)
							oakAsm->INS(dst.Selem()[lane], input.Selem()[(selectors >> (lane * 2)) & 3]);
					}
					Store128(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::VShuffle2:
				{
					recBeginOaknutEmit();
					const auto lhs = Operand128(a[0], oak::util::Q0);
					const auto rhs = Operand128(a[1], oak::util::Q1);
					const auto dst = Result128(inst.value);
					switch (inst.imm)
					{
						case 0xa60: oakAsm->ZIP1(dst.S4(), lhs.S4(), rhs.S4()); break; // 0,4,1,5
						case 0xef2: oakAsm->ZIP2(dst.S4(), lhs.S4(), rhs.S4()); break; // 2,6,3,7
						case 0xd10: oakAsm->UZP1(dst.S4(), lhs.S4(), rhs.S4()); break; // 0,2,4,6
						case 0xb08: oakAsm->ZIP1(dst.D2(), lhs.D2(), rhs.D2()); break; // 0,1,4,5
						case 0x6be: oakAsm->ZIP2(dst.D2(), rhs.D2(), lhs.D2()); break; // 6,7,2,3
						case 0xf88: // a0,a1,b2,b3 (PADSBH)
							oakAsm->INS(dst.Delem()[0], lhs.Delem()[0]);
							oakAsm->INS(dst.Delem()[1], rhs.Delem()[1]);
							break;
						case 0x688: // 0,1,2,3
							if (dst.index() != lhs.index()) oakAsm->MOV(dst.B16(), lhs.B16());
							break;
						case 0xfac: // 4,5,6,7
							if (dst.index() != rhs.index()) oakAsm->MOV(dst.B16(), rhs.B16());
							break;
						default:
							// Capture every source word before touching a coalesced dst.
							for (u32 lane = 0; lane < 4; ++lane)
							{
								const u32 select = (inst.imm >> (lane * 3)) & 7;
								oakAsm->UMOV(oak::WReg(lane), (select < 4 ? lhs : rhs).Selem()[select & 3]);
							}
							for (u32 lane = 0; lane < 4; ++lane)
								oakAsm->INS(dst.Selem()[lane], oak::WReg(lane));
							break;
					}
					Store128(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::VMulWidenLoS:
				case ir::Op::VMulWidenHiS:
				case ir::Op::VMulWidenLoU:
				case ir::Op::VMulWidenHiU:
				{
					recBeginOaknutEmit();
					const auto lhs = Operand128(a[0], oak::util::Q0);
					const auto rhs = Operand128(a[1], oak::util::Q1);
					const auto dst = Result128(inst.value);
					const bool high = inst.op == ir::Op::VMulWidenHiS || inst.op == ir::Op::VMulWidenHiU;
					const bool sign = inst.op == ir::Op::VMulWidenLoS || inst.op == ir::Op::VMulWidenHiS;
					if (inst.imm == 8)
					{
						if (high)
						{
							if (sign)
								oakAsm->SMULL2(dst.H8(), lhs.B16(), rhs.B16());
							else
								oakAsm->UMULL2(dst.H8(), lhs.B16(), rhs.B16());
						}
						else
						{
							if (sign)
								oakAsm->SMULL(dst.H8(), lhs.toD().B8(), rhs.toD().B8());
							else
								oakAsm->UMULL(dst.H8(), lhs.toD().B8(), rhs.toD().B8());
						}
					}
					else if (inst.imm == 16)
					{
						if (high)
						{
							if (sign)
								oakAsm->SMULL2(dst.S4(), lhs.H8(), rhs.H8());
							else
								oakAsm->UMULL2(dst.S4(), lhs.H8(), rhs.H8());
						}
						else
						{
							if (sign)
								oakAsm->SMULL(dst.S4(), lhs.toD().H4(), rhs.toD().H4());
							else
								oakAsm->UMULL(dst.S4(), lhs.toD().H4(), rhs.toD().H4());
						}
					}
					else
					{
						if (high)
						{
							if (sign)
								oakAsm->SMULL2(dst.D2(), lhs.S4(), rhs.S4());
							else
								oakAsm->UMULL2(dst.D2(), lhs.S4(), rhs.S4());
						}
						else
						{
							if (sign)
								oakAsm->SMULL(dst.D2(), lhs.toD().S2(), rhs.toD().S2());
							else
								oakAsm->UMULL(dst.D2(), lhs.toD().S2(), rhs.toD().S2());
						}
					}
					Store128(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::VPackSatS:
				{
					recBeginOaknutEmit();
					const auto lhs = Operand128(a[0], oak::util::Q0);
					auto rhs = Operand128(a[1], oak::util::Q1);
					const auto dst = Result128(inst.value);
					if (rhs.index() == dst.index())
					{
						oakAsm->MOV(oak::util::Q1.B16(), rhs.B16());
						rhs = oak::util::Q1;
					}
					if (inst.imm == 8)
					{
						oakAsm->SQXTN(dst.toD().B8(), lhs.H8());
						oakAsm->SQXTN2(dst.B16(), rhs.H8());
					}
					else
					{
						oakAsm->SQXTN(dst.toD().H4(), lhs.S4());
						oakAsm->SQXTN2(dst.H8(), rhs.S4());
					}
					Store128(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::VShlEach:
				case ir::Op::VShrUEach:
				case ir::Op::VShrSEach:
				{
					recBeginOaknutEmit();
					const auto lhs = Operand128(a[0], oak::util::Q0);
					const auto rhs = Operand128(a[1], oak::util::Q1);
					const auto dst = Result128(inst.value);
					const auto emit = [&](auto d, auto s, auto t, auto counts, u32 mask_shift)
					{
						oakAsm->SHL(counts, t, mask_shift);
						oakAsm->USHR(counts, counts, mask_shift);
						if (inst.op != ir::Op::VShlEach)
							oakAsm->NEG(counts, counts);
						if (inst.op == ir::Op::VShrSEach)
							oakAsm->SSHL(d, s, counts);
						else
							oakAsm->USHL(d, s, counts);
					};
					if (inst.imm == 8)
						emit(dst.B16(), lhs.B16(), rhs.B16(), oak::util::Q1.B16(), 5);
					else if (inst.imm == 16)
						emit(dst.H8(), lhs.H8(), rhs.H8(), oak::util::Q1.H8(), 12);
					else
						emit(dst.S4(), lhs.S4(), rhs.S4(), oak::util::Q1.S4(), 27);
					Store128(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}


				case ir::Op::VAbsSatS:
				{
					recBeginOaknutEmit();
					const auto source = Operand128(a[0], oak::util::Q0);
					const auto dst = Result128(inst.value);
					if (inst.imm == 8) oakAsm->SQABS(dst.B16(), source.B16());
					else if (inst.imm == 16) oakAsm->SQABS(dst.H8(), source.H8());
					else oakAsm->SQABS(dst.S4(), source.S4());
					Store128(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::VAddI:
				case ir::Op::VSubI:
				case ir::Op::VAddSatS:
				case ir::Op::VAddSatU:
				case ir::Op::VSubSatS:
				case ir::Op::VSubSatU:
				case ir::Op::VMinS:
				case ir::Op::VMinU:
				case ir::Op::VMaxS:
				case ir::Op::VMaxU:
				case ir::Op::VCmpEq:
				case ir::Op::VCmpNe:
				case ir::Op::VCmpLtS:
				case ir::Op::VCmpLtU:
				case ir::Op::VCmpLeS:
				case ir::Op::VCmpLeU:
				{
					recBeginOaknutEmit();
					const auto lhs = Operand128(a[0], oak::util::Q0);
					const auto rhs = Operand128(a[1], oak::util::Q1);
					const auto dst = Result128(inst.value);
					const auto emit = [&](auto d, auto s, auto t) {
						switch (inst.op)
						{
							case ir::Op::VAddI: oakAsm->ADD(d, s, t); break;
							case ir::Op::VSubI: oakAsm->SUB(d, s, t); break;
							case ir::Op::VAddSatS: oakAsm->SQADD(d, s, t); break;
							case ir::Op::VAddSatU: oakAsm->UQADD(d, s, t); break;
							case ir::Op::VSubSatS: oakAsm->SQSUB(d, s, t); break;
							case ir::Op::VSubSatU: oakAsm->UQSUB(d, s, t); break;
							case ir::Op::VMinS: oakAsm->SMIN(d, s, t); break;
							case ir::Op::VMinU: oakAsm->UMIN(d, s, t); break;
							case ir::Op::VMaxS: oakAsm->SMAX(d, s, t); break;
							case ir::Op::VMaxU: oakAsm->UMAX(d, s, t); break;
							case ir::Op::VCmpEq: oakAsm->CMEQ(d, s, t); break;
							case ir::Op::VCmpNe: oakAsm->CMEQ(d, s, t); oakAsm->NOT(dst.B16(), dst.B16()); break;
							case ir::Op::VCmpLtS: oakAsm->CMGT(d, t, s); break;
							case ir::Op::VCmpLtU: oakAsm->CMHI(d, t, s); break;
							case ir::Op::VCmpLeS: oakAsm->CMGE(d, t, s); break;
							case ir::Op::VCmpLeU: oakAsm->CMHS(d, t, s); break;
							default: break;
						}
					};
					if (inst.imm == 8) emit(dst.B16(), lhs.B16(), rhs.B16());
					else if (inst.imm == 16) emit(dst.H8(), lhs.H8(), rhs.H8());
					else emit(dst.S4(), lhs.S4(), rhs.S4());
					Store128(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::VShl:
				case ir::Op::VShrU:
				case ir::Op::VShrS:
				{
					recBeginOaknutEmit();
					const auto source = Operand128(a[0], oak::util::Q0);
					const auto dst = Result128(inst.value);
					const u32 bits = inst.imm ? static_cast<u32>(inst.imm) : 32;
					const auto emit = [&](auto d, auto s, auto count) {
						if (m_constants[a[1]])
						{
							const u32 amount = static_cast<u32>(*m_constants[a[1]]) & (bits - 1);
							// ARM64 immediate right shifts require a nonzero amount.
							if (amount == 0)
							{
								if (dst.index() != source.index()) oakAsm->MOV(dst.B16(), source.B16());
							}
							else if (inst.op == ir::Op::VShl) oakAsm->SHL(d, s, amount);
							else if (inst.op == ir::Op::VShrU) oakAsm->USHR(d, s, amount);
							else oakAsm->SSHR(d, s, amount);
						}
						else
						{
							Load32(a[1], oak::util::W0);
							oakAsm->AND(oak::util::W0, oak::util::W0, bits - 1);
							if (inst.op != ir::Op::VShl) oakAsm->NEG(oak::util::W0, oak::util::W0);
							oakAsm->DUP(count, oak::util::W0);
							if (inst.op == ir::Op::VShrS) oakAsm->SSHL(d, s, count);
							else oakAsm->USHL(d, s, count);
						}
					};
					if (bits == 8) emit(dst.B16(), source.B16(), oak::util::Q1.B16());
					else if (bits == 16) emit(dst.H8(), source.H8(), oak::util::Q1.H8());
					else emit(dst.S4(), source.S4(), oak::util::Q1.S4());
					Store128(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::Shl:
				case ir::Op::ShrU:
				case ir::Op::ShrS:
				{
					recBeginOaknutEmit();
					if (inst.type == ir::Type::I64)
					{
						const auto lhs = Operand64(a[0], oak::util::X0);
						if (m_constants[a[1]])
						{
							const auto dst = Result64(inst.value);
							const u32 amount = static_cast<u32>(*m_constants[a[1]]) & 63u;
							if (inst.op == ir::Op::Shl)
								oakAsm->LSL(dst, lhs, amount);
							else if (inst.op == ir::Op::ShrU)
								oakAsm->LSR(dst, lhs, amount);
							else
								oakAsm->ASR(dst, lhs, amount);
							Store64(inst.value, dst);
							recEndOaknutEmit();
							return true;
						}
						const auto rhs = m_fn.ValueType(a[1]) == ir::Type::I64 ?
							Operand64(a[1], oak::util::X1) : oak::XReg(Operand32(a[1], oak::util::W1).index());
						const auto dst = Result64(inst.value);
						if (inst.op == ir::Op::Shl)
							oakAsm->LSLV(dst, lhs, rhs);
						else if (inst.op == ir::Op::ShrU)
							oakAsm->LSRV(dst, lhs, rhs);
						else
							oakAsm->ASRV(dst, lhs, rhs);
						Store64(inst.value, dst);
					}
					else
					{
						const auto lhs = Operand32(a[0], oak::util::W0);
						if (m_constants[a[1]])
						{
							const auto dst = Result32(inst.value);
							const u32 amount = static_cast<u32>(*m_constants[a[1]]) & 31u;
							if (inst.op == ir::Op::Shl)
								oakAsm->LSL(dst, lhs, amount);
							else if (inst.op == ir::Op::ShrU)
								oakAsm->LSR(dst, lhs, amount);
							else
								oakAsm->ASR(dst, lhs, amount);
							Store32(inst.value, dst);
							recEndOaknutEmit();
							return true;
						}
						const auto rhs = Operand32(a[1], oak::util::W1);
						const auto dst = Result32(inst.value);
						if (inst.op == ir::Op::Shl)
							oakAsm->LSLV(dst, lhs, rhs);
						else if (inst.op == ir::Op::ShrU)
							oakAsm->LSRV(dst, lhs, rhs);
						else
							oakAsm->ASRV(dst, lhs, rhs);
						Store32(inst.value, dst);
					}
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::CmpEq:
				case ir::Op::CmpNe:
				case ir::Op::CmpLtS:
				case ir::Op::CmpLtU:
				case ir::Op::CmpLeS:
				case ir::Op::CmpLeU:
				case ir::Op::CmpGtS:
				case ir::Op::CmpGtU:
				case ir::Op::CmpGeS:
				case ir::Op::CmpGeU:
				{
					oak::Cond cond = oak::Cond::EQ;
					switch (inst.op)
					{
						case ir::Op::CmpEq: cond = oak::Cond::EQ; break;
						case ir::Op::CmpNe: cond = oak::Cond::NE; break;
						case ir::Op::CmpLtS: cond = oak::Cond::LT; break;
						case ir::Op::CmpLtU: cond = oak::Cond::LO; break;
						case ir::Op::CmpLeS: cond = oak::Cond::LE; break;
						case ir::Op::CmpLeU: cond = oak::Cond::LS; break;
						case ir::Op::CmpGtS: cond = oak::Cond::GT; break;
						case ir::Op::CmpGtU: cond = oak::Cond::HI; break;
						case ir::Op::CmpGeS: cond = oak::Cond::GE; break;
						default: cond = oak::Cond::HS; break;
					}
					recBeginOaknutEmit();
					if (m_fn.ValueType(a[0]) == ir::Type::I64)
					{
						const auto lhs = Operand64(a[0], oak::util::X0);
						const auto rhs = Operand64(a[1], oak::util::X1);
						oakAsm->CMP(lhs, rhs);
					}
					else
					{
						const auto lhs = Operand32(a[0], oak::util::W0);
						const auto rhs = Operand32(a[1], oak::util::W1);
						oakAsm->CMP(lhs, rhs);
					}
					const auto dst = Result32(inst.value);
					oakAsm->CSET(dst, cond);
					Store32(inst.value, dst);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::Msub:
				{
					recBeginOaknutEmit();
					if (inst.type == ir::Type::I64)
					{
						const auto lhs = Operand64(a[0], oak::util::X0);
						const auto rhs = Operand64(a[1], oak::util::X1);
						const auto addend = Operand64(a[2], oak::util::X2);
						const auto dst = Result64(inst.value);
						oakAsm->MSUB(dst, lhs, rhs, addend);
						Store64(inst.value, dst);
					}
					else
					{
						const auto lhs = Operand32(a[0], oak::util::W0);
						const auto rhs = Operand32(a[1], oak::util::W1);
						const auto addend = Operand32(a[2], oak::util::W2);
						const auto dst = Result32(inst.value);
						oakAsm->MSUB(dst, lhs, rhs, addend);
						Store32(inst.value, dst);
					}
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::DivS:
				case ir::Op::RemS:
				case ir::Op::DivU:
				case ir::Op::RemU:
				{
					if (m_inline_division)
					{
						recBeginOaknutEmit();
						const auto lhs = Operand32(a[0], oak::util::W0);
						const auto rhs = Operand32(a[1], oak::util::W1);
						const auto dst = Result32(inst.value);
						const bool sign = inst.op == ir::Op::DivS || inst.op == ir::Op::RemS;
						// Keep the quotient in scratch: coalescing may assign dst
						// to an operand that is still needed for the remainder or
						// the R5900 divide-by-zero correction below.
						if (sign)
							oakAsm->SDIV(oak::util::W2, lhs, rhs);
						else
							oakAsm->UDIV(oak::util::W2, lhs, rhs);
						if (inst.op == ir::Op::RemS || inst.op == ir::Op::RemU)
						{
							// ARM64's zero-divisor quotient is zero, so MSUB also
							// returns the dividend in that case. INT_MIN / -1
							// wraps to INT_MIN and produces a zero remainder.
							oakAsm->MSUB(dst, oak::util::W2, rhs, lhs);
						}
						else
						{
							if (sign)
							{
								// R5900 signed divide by zero: negative -> 1,
								// nonnegative -> -1, including a zero dividend.
								oakAsm->ASR(oak::util::W3, lhs, 31);
								oakAsm->LSL(oak::util::W3, oak::util::W3, 1);
								oakAsm->MVN(oak::util::W3, oak::util::W3);
							}
							else
								oakAsm->MOV(oak::util::W3, 0xffffffffu);
							oakAsm->CMP(rhs, 0);
							oakAsm->CSEL(dst, oak::util::W3, oak::util::W2, oak::Cond::EQ);
						}
						Store32(inst.value, dst);
						recEndOaknutEmit();
						return true;
					}
					const void* helper = nullptr;
					switch (inst.op)
					{
						case ir::Op::DivS: helper = reinterpret_cast<const void*>(&EeIrDivSLo); break;
						case ir::Op::RemS: helper = reinterpret_cast<const void*>(&EeIrDivSHi); break;
						case ir::Op::DivU: helper = reinterpret_cast<const void*>(&EeIrDivULo); break;
						default: helper = reinterpret_cast<const void*>(&EeIrDivUHi); break;
					}
					recBeginOaknutEmit();
					load_a();
					load_b();
					call_helper(helper);
					store_r();
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::Load8U:
				case ir::Op::Load8S:
				case ir::Op::Load16U:
				case ir::Op::Load16S:
				case ir::Op::Load32:
				{
					const bool iop = m_guest_state == GuestState::IOP;
					if (iop) ++m_iop_memory_operations;
					const bool direct = (iop && m_direct_iop_loads) || m_direct_ee_loads;
					const bool direct_vectors = UsesDirectVectorMemory(inst);
					const u32 position = m_emit_position - 1;
					const void* helper = nullptr;
					switch (inst.op)
					{
						case ir::Op::Load8U: helper = iop ? reinterpret_cast<const void*>(&IopIrMemRead8) : reinterpret_cast<const void*>(&EeIrMemRead8); break;
						case ir::Op::Load8S: helper = iop ? reinterpret_cast<const void*>(&IopIrMemRead8S) : reinterpret_cast<const void*>(&EeIrMemRead8S); break;
						case ir::Op::Load16U: helper = iop ? reinterpret_cast<const void*>(&IopIrMemRead16) : reinterpret_cast<const void*>(&EeIrMemRead16); break;
						case ir::Op::Load16S: helper = iop ? reinterpret_cast<const void*>(&IopIrMemRead16S) : reinterpret_cast<const void*>(&EeIrMemRead16S); break;
						default: helper = iop ? reinterpret_cast<const void*>(&IopIrMemRead32) : reinterpret_cast<const void*>(&EeIrMemRead32); break;
					}
					recBeginOaknutEmit();
					load_a();
					bool fast_load = false;
#if defined(ARCH_ARM64) && defined(__ANDROID__)
					fast_load = m_fastmem_ee_loads && m_direct_ee_loads && inst.op == ir::Op::Load32 &&
						CHECK_FASTMEM && !vtlb_IsFaultingPC(inst.guest_pc);
#endif
					const bool delay_slot = (inst.aux & ir::IF_DELAY_SLOT) != 0;
					// Only the specialized fault thunk owns context when opted in.
					// Every helper/replayed faulting PC keeps the ordinary pre-hook.
					if (!(fast_load && m_fastmem_ee_cold_context) && m_hooks && m_hooks->before_memory)
						m_hooks->before_memory(m_hooks->ctx, inst.guest_pc, delay_slot);
#if defined(ARCH_ARM64) && defined(__ANDROID__)
					if (fast_load)
					{
						m_fast_load_sites.push_back({reinterpret_cast<uptr>(oakGetCurrentCodePointer()), inst.guest_pc, delay_slot});
						oakAsm->LDR(oak::util::W0, oak::util::X19, oak::util::W0, oak::IndexExt::UXTW);
						// Retain success BD clear: a block may enter with stale BD.
						m_hooks->after_memory(m_hooks->ctx);
						store_r();
						recEndOaknutEmit();
						return true;
					}
#endif
					oak::Label slow, done;
					if (iop && m_direct_iop_loads)
					{
						// Only real RAM pages bypass handlers. Read the live LUT at
						// execution time so remaps and null pages retain their meaning.
						oakAsm->AND(oak::util::W3, oak::util::W0, 0x1fffffff);
						oakAsm->CMP(oak::util::W3, 0x00800000);
						oakAsm->B(oak::Cond::HS, slow);
						oakMoveAddressToReg(oak::util::X2, &psxMemRLUT);
						oakLoad64(oak::util::X2, {oak::util::X2, 0});
						oakAsm->LSR(oak::util::W4, oak::util::W3, 16);
						oakAsm->LDR(oak::util::X2, oak::util::X2, oak::util::X4, oak::IndexExt::LSL, 3);
						oakAsm->CBZ(oak::util::X2, slow);
						oakAsm->AND(oak::util::W3, oak::util::W3, 0xffff);
						oakAsm->ADD(oak::util::X2, oak::util::X2, oak::util::X3);
						switch (inst.op)
						{
							case ir::Op::Load8U: oakAsm->LDRB(oak::util::W0, oak::util::X2); break;
							case ir::Op::Load8S: oakAsm->LDRSB(oak::util::W0, oak::util::X2); break;
							case ir::Op::Load16U: oakAsm->LDRH(oak::util::W0, oak::util::X2); break;
							case ir::Op::Load16S: oakAsm->LDRSH(oak::util::W0, oak::util::X2); break;
							default: oakAsm->LDR(oak::util::W0, oak::util::X2); break;
						}
						oakAsm->B(done);
						oakAsm->l(slow);
						++m_direct_iop_load_operations;
					}
					else if (m_direct_ee_loads)
					{
						EmitEeDirectAddress(slow);
						switch (inst.op)
						{
							case ir::Op::Load8U: oakAsm->LDRB(oak::util::W0, oak::util::X2); break;
							case ir::Op::Load8S: oakAsm->LDRSB(oak::util::W0, oak::util::X2); break;
							case ir::Op::Load16U: oakAsm->LDRH(oak::util::W0, oak::util::X2); break;
							case ir::Op::Load16S: oakAsm->LDRSH(oak::util::W0, oak::util::X2); break;
							default: oakAsm->LDR(oak::util::W0, oak::util::X2); break;
						}
						oakAsm->B(done);
						oakAsm->l(slow);
						++m_direct_ee_load_operations;
						if (direct_vectors) SaveVectors(position);
					}
					call_helper(helper);
					if (direct_vectors) RestoreVectors(position);
					if (direct)
						oakAsm->l(done);
					if (m_hooks && m_hooks->after_memory)
						m_hooks->after_memory(m_hooks->ctx);
					store_r();
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::Load64:
				{
					const bool direct_vectors = UsesDirectVectorMemory(inst);
					const u32 position = m_emit_position - 1;
					recBeginOaknutEmit();
					load_a();
					if (m_hooks && m_hooks->before_memory)
						m_hooks->before_memory(m_hooks->ctx, inst.guest_pc, (inst.aux & ir::IF_DELAY_SLOT) != 0);
					oak::Label slow, done;
					if (m_direct_ee_loads)
					{
						EmitEeDirectAddress(slow);
						oakAsm->LDR(oak::util::X0, oak::util::X2);
						oakAsm->B(done);
						oakAsm->l(slow);
						++m_direct_ee_load_operations;
						if (direct_vectors) SaveVectors(position);
					}
					call_helper(reinterpret_cast<const void*>(&EeIrMemRead64));
					if (direct_vectors) RestoreVectors(position);
					if (m_direct_ee_loads) oakAsm->l(done);
					if (m_hooks && m_hooks->after_memory)
						m_hooks->after_memory(m_hooks->ctx);
					Store64(inst.value, oak::util::X0);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::Load128:
				case ir::Op::Store128:
				{
					const bool direct_vectors = UsesDirectVectorMemory(inst);
					const u32 position = m_emit_position - 1;
					recBeginOaknutEmit();
					load_a();
					if (m_hooks && m_hooks->before_memory)
						m_hooks->before_memory(m_hooks->ctx, inst.guest_pc, (inst.aux & ir::IF_DELAY_SLOT) != 0);
					oak::Label slow, done;
					if (m_direct_quad_memory)
					{
						EmitEeDirectAddress(slow);
						if (inst.op == ir::Op::Load128)
						{
							const auto dst = Result128(inst.value);
							oakAsm->LDR(dst, oak::util::X2);
							Store128(inst.value, dst);
						}
						else
						{
							const auto src = Operand128(a[1], oak::util::Q0);
							oakAsm->STR(src, oak::util::X2);
						}
						oakAsm->B(done);
						oakAsm->l(slow);
						++m_direct_quad_operations;
						if (direct_vectors)
						{
							++m_direct_quad_vector_operations;
							// Only the C-helper fallback needs caller-save homes.
							// Save before before_helper can clobber any full Q value.
							SaveVectors(position);
						}
					}
					SlotAddress(inst.op == ir::Op::Load128 ? inst.value : a[1], oak::util::X1);
					call_helper(inst.op == ir::Op::Load128 ? reinterpret_cast<const void*>(&EeIrMemRead128) :
						reinterpret_cast<const void*>(&EeIrMemWrite128));
					if (direct_vectors)
						RestoreVectors(position);
					if (m_direct_quad_memory)
						oakAsm->l(done);
					if (m_hooks && m_hooks->after_memory)
						m_hooks->after_memory(m_hooks->ctx);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::Store8:
				case ir::Op::Store16:
				case ir::Op::Store32:
				case ir::Op::Store64:
				{
					const bool iop = m_guest_state == GuestState::IOP;
					if (iop) ++m_iop_memory_operations;
					const bool direct_vectors = UsesDirectVectorMemory(inst);
					const u32 position = m_emit_position - 1;
					const void* helper = nullptr;
					switch (inst.op)
					{
						case ir::Op::Store8: helper = iop ? reinterpret_cast<const void*>(&IopIrMemWrite8) : reinterpret_cast<const void*>(&EeIrMemWrite8); break;
						case ir::Op::Store16: helper = iop ? reinterpret_cast<const void*>(&IopIrMemWrite16) : reinterpret_cast<const void*>(&EeIrMemWrite16); break;
						case ir::Op::Store32: helper = iop ? reinterpret_cast<const void*>(&IopIrMemWrite32) : reinterpret_cast<const void*>(&EeIrMemWrite32); break;
						default: helper = reinterpret_cast<const void*>(&EeIrMemWrite64); break;
					}
					recBeginOaknutEmit();
					Load32(a[0], oak::util::W0);
					if (inst.op == ir::Op::Store64)
						Load64(a[1], oak::util::X1);
					else
						Load32(a[1], oak::util::W1);
					if (m_hooks && m_hooks->before_memory)
						m_hooks->before_memory(m_hooks->ctx, inst.guest_pc, (inst.aux & ir::IF_DELAY_SLOT) != 0);
					oak::Label slow, done;
					if (m_direct_ee_stores)
					{
						// Preserve the ordinary VTLB write contract, including live
						// remaps and host write protection for recompiled guest code.
						EmitEeDirectAddress(slow);
						switch (inst.op)
						{
							case ir::Op::Store8: oakAsm->STRB(oak::util::W1, oak::util::X2); break;
							case ir::Op::Store16: oakAsm->STRH(oak::util::W1, oak::util::X2); break;
							case ir::Op::Store32: oakAsm->STR(oak::util::W1, oak::util::X2); break;
							default: oakAsm->STR(oak::util::X1, oak::util::X2); break;
						}
						oakAsm->B(done);
						oakAsm->l(slow);
						++m_direct_ee_store_operations;
						if (direct_vectors) SaveVectors(position);
					}
					call_helper(helper);
					if (direct_vectors) RestoreVectors(position);
					if (m_direct_ee_stores) oakAsm->l(done);
					if (m_hooks && m_hooks->after_memory)
						m_hooks->after_memory(m_hooks->ctx);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::ReadCp0:
					recBeginOaknutEmit();
					oakLoad32(Result32(inst.value), {GuestBase(), static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP0) + inst.imm * sizeof(u32))});
					Store32(inst.value, Result32(inst.value));
					recEndOaknutEmit();
					return true;

				case ir::Op::WriteCp0:
					recBeginOaknutEmit();
					oakStore32(Operand32(a[0], oak::util::W0), {GuestBase(), static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP0) + inst.imm * sizeof(u32))});
					recEndOaknutEmit();
					return true;

				case ir::Op::ReadCp2Data:
					recBeginOaknutEmit();
					oakLoad32(Result32(inst.value), {GuestBase(), static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r) + inst.imm * sizeof(u32))});
					Store32(inst.value, Result32(inst.value));
					recEndOaknutEmit();
					return true;

				case ir::Op::WriteCp2Data:
					recBeginOaknutEmit();
					oakStore32(Operand32(a[0], oak::util::W0), {GuestBase(), static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r) + inst.imm * sizeof(u32))});
					recEndOaknutEmit();
					return true;

				case ir::Op::ReadCp2Control:
					recBeginOaknutEmit();
					oakLoad32(Result32(inst.value), {GuestBase(), static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r) + inst.imm * sizeof(u32))});
					Store32(inst.value, Result32(inst.value));
					recEndOaknutEmit();
					return true;

				case ir::Op::WriteCp2Control:
					recBeginOaknutEmit();
					oakStore32(Operand32(a[0], oak::util::W0), {GuestBase(), static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r) + inst.imm * sizeof(u32))});
					recEndOaknutEmit();
					return true;

				case ir::Op::ReadFprBits:
					recBeginOaknutEmit();
					oakLoad32(Result32(inst.value), {GuestBase(), FprBitsOffset(static_cast<u32>(inst.imm))});
					Store32(inst.value, Result32(inst.value));
					recEndOaknutEmit();
					return true;

				case ir::Op::WriteFprBits:
					recBeginOaknutEmit();
					oakStore32(Operand32(a[0], oak::util::W0), {GuestBase(), FprBitsOffset(static_cast<u32>(inst.imm))});
					recEndOaknutEmit();
					return true;

				case ir::Op::ReadFcr31:
					recBeginOaknutEmit();
					oakLoad32(Result32(inst.value), {GuestBase(), static_cast<s64>(offsetof(cpuRegistersPack, fpuRegs.fprc[31]))});
					Store32(inst.value, Result32(inst.value));
					recEndOaknutEmit();
					return true;

				case ir::Op::WriteFcr31:
					recBeginOaknutEmit();
					oakStore32(Operand32(a[0], oak::util::W0), {GuestBase(), static_cast<s64>(offsetof(cpuRegistersPack, fpuRegs.fprc[31]))});
					recEndOaknutEmit();
					return true;

				case ir::Op::CheckInterrupts:
					recBeginOaknutEmit();
					call_helper(reinterpret_cast<const void*>(&iopTestIntc));
					recEndOaknutEmit();
					return true;

				case ir::Op::ReadGpr:
					recBeginOaknutEmit();
					if (inst.type == ir::Type::V4U32)
					{
						const auto dst = Result128(inst.value);
						if (inst.imm == 0)
							oakAsm->MOVI(dst.B16(), 0);
						else
							oakLoad128(dst, {GuestBase(), GprOffset(static_cast<u32>(inst.imm))});
						Store128(inst.value, dst);
					}
					else if (inst.type == ir::Type::I64)
					{
						oakLoad64(Result64(inst.value), {GuestBase(), GprOffset(static_cast<u32>(inst.imm))});
						Store64(inst.value, Result64(inst.value));
					}
					else
					{
						if (inst.imm == 0)
							oakAsm->MOV(Result32(inst.value), 0);
						else
							oakLoad32(Result32(inst.value), {GuestBase(), GprOffset(static_cast<u32>(inst.imm))});
						Store32(inst.value, Result32(inst.value));
					}
					recEndOaknutEmit();
					return true;

				case ir::Op::WriteGpr:
					if (inst.imm == 0)
						return true; // $0 is hardwired to zero
					recBeginOaknutEmit();
					if (m_fn.ValueType(a[0]) == ir::Type::V4U32)
					{
						const auto src = Operand128(a[0], oak::util::Q0);
						oakStore128(src, {GuestBase(), GprOffset(static_cast<u32>(inst.imm))});
						recEndOaknutEmit();
						return true;
					}
					if (inst.aux & ir::IF_WIDE_WRITE)
					{
						const auto value = Operand64(a[0], oak::util::X0);
						oakStore64(value, {GuestBase(), GprOffset(static_cast<u32>(inst.imm))});
						recEndOaknutEmit();
						return true;
					}
					else
					{
						const auto value = Operand32(a[0], oak::util::W0);
						if (m_guest_state == GuestState::IOP)
						{
							oakStore32(value, {GuestBase(), GprOffset(static_cast<u32>(inst.imm))});
							recEndOaknutEmit();
							return true;
						}
						oakAsm->SXTW(oak::util::X0, value);
					}
					oakStore64(oak::util::X0, {GuestBase(), GprOffset(static_cast<u32>(inst.imm))});
					recEndOaknutEmit();
					return true;

				case ir::Op::ReadHi:
				case ir::Op::ReadLo:
					recBeginOaknutEmit();
					if (inst.type == ir::Type::V4U32)
					{
						oakLoad128(Result128(inst.value), {GuestBase(), (inst.op == ir::Op::ReadHi) ? HiOffset(0) : LoOffset(0)});
						Store128(inst.value, Result128(inst.value));
					}
					else if (inst.type == ir::Type::I64)
					{
						oakLoad64(Result64(inst.value), {GuestBase(), (inst.op == ir::Op::ReadHi) ? HiOffset(inst.imm) : LoOffset(inst.imm)});
						Store64(inst.value, Result64(inst.value));
					}
					else
					{
						oakLoad32(Result32(inst.value), {GuestBase(), (inst.op == ir::Op::ReadHi) ? HiOffset(inst.imm) : LoOffset(inst.imm)});
						Store32(inst.value, Result32(inst.value));
					}
					recEndOaknutEmit();
					return true;

				case ir::Op::WriteHi:
				case ir::Op::WriteLo:
					recBeginOaknutEmit();
					if (m_fn.ValueType(a[0]) == ir::Type::V4U32)
					{
						oakStore128(Operand128(a[0], oak::util::Q0), {GuestBase(), (inst.op == ir::Op::WriteHi) ? HiOffset(0) : LoOffset(0)});
						recEndOaknutEmit();
						return true;
					}
					if (m_fn.ValueType(a[0]) == ir::Type::I64)
					{
						const auto value = Operand64(a[0], oak::util::X0);
						oakStore64(value, {GuestBase(), (inst.op == ir::Op::WriteHi) ? HiOffset(inst.imm) : LoOffset(inst.imm)});
						recEndOaknutEmit();
						return true;
					}
					else
					{
						const auto value = Operand32(a[0], oak::util::W0);
						if (m_guest_state == GuestState::IOP)
						{
							oakStore32(value, {GuestBase(), (inst.op == ir::Op::WriteHi) ? HiOffset(inst.imm) : LoOffset(inst.imm)});
							recEndOaknutEmit();
							return true;
						}
						oakAsm->SXTW(oak::util::X0, value);
					}
					oakStore64(oak::util::X0, {GuestBase(), (inst.op == ir::Op::WriteHi) ? HiOffset(inst.imm) : LoOffset(inst.imm)});
					recEndOaknutEmit();
					return true;

				case ir::Op::AddCycles:
					return true; // M1b does not model cycle accounting yet.

				case ir::Op::Jump:
					recBeginOaknutEmit();
					oakAsm->B(m_labels[a[0] - 1]);
					recEndOaknutEmit();
					return true;

				case ir::Op::Branch:
				{
					const u32 taken = a[1];
					const u32 not_taken = a[2];
					recBeginOaknutEmit();
					const auto condition = Operand32(a[0], oak::util::W0);
					oakAsm->CBNZ(condition, m_labels[taken - 1]);
					oakAsm->B(m_labels[not_taken - 1]);
					recEndOaknutEmit();
					return true;
				}

				case ir::Op::BranchIndirect:
					if (m_inline)
					{
						// Address arrives in W16 for the integration hook.
						recBeginOaknutEmit();
						Load32(a[0], oak::util::W16);
						EmitEpilogue();
						recEndOaknutEmit();
						m_hooks->indirect_exit(m_hooks->ctx);
						return true;
					}
					recBeginOaknutEmit();
					load_a();
					if (m_capture_exit)
					{
						oakMoveAddressToReg(oak::util::X16, &g_eeir_exit_valid);
						oakAsm->MOV(oak::util::W17, 1);
						oakStore32(oak::util::W17, {oak::util::X16, 0});
						oakMoveAddressToReg(oak::util::X16, &g_eeir_exit_pc);
						oakStore32(oak::util::W0, {oak::util::X16, 0});
					}
					oakStore32(oak::util::W0, {GuestBase(), PcOffset()});
					EmitEpilogue();
					recEndOaknutEmit();
					return true;

				case ir::Op::Resume:
					if (m_inline)
					{
						EmitEpilogue();
						m_hooks->guest_exit(m_hooks->ctx, static_cast<u32>(inst.imm), (inst.aux & ir::IF_ANNULLED_DELAY_SLOT) != 0);
						return true;
					}
					if (m_capture_exit)
					{
						recBeginOaknutEmit();
						oakMoveAddressToReg(oak::util::X16, &g_eeir_exit_valid);
						oakAsm->MOV(oak::util::W17, 1);
						oakStore32(oak::util::W17, {oak::util::X16, 0});
						oakMoveAddressToReg(oak::util::X16, &g_eeir_exit_pc);
						oakAsm->MOV(oak::util::W17, static_cast<u32>(inst.imm));
						oakStore32(oak::util::W17, {oak::util::X16, 0});
						recEndOaknutEmit();
					}
					EmitEpilogue();
					return true;

				case ir::Op::Trap:
				case ir::Op::CheckEvents:
				case ir::Op::Return:
					EmitEpilogue();
					return true;

				default:
					return Fail(error, "unsupported IR instruction");
			}
		}
	} // namespace

	bool CanLower(const ir::Function& fn, bool inline_body, std::string* error, GuestState guest_state)
	{
		if (!ir::Verify(fn, error))
			return false;
		// Reject before emission: the caller may fall back to the legacy JIT.
		u64 worst_frame = 64;
		for (u32 value = 1; value < fn.value_types.size(); ++value)
		{
			const u32 size = std::max(8u, ir::TypeSize(fn.ValueType(value)));
			worst_frame = ((worst_frame + size - 1u) & ~u64(size - 1u)) + size;
		}
		if (((worst_frame + 15u) & ~u64(15)) > 0x3f00u)
		{
			if (error)
				*error = "IR stack frame too large";
			return false;
		}
		for (const ir::Block& block : fn.blocks)
		{
			for (const ir::Inst& inst : block.insts)
			{
				if (guest_state != GuestState::IOP &&
					(inst.op == ir::Op::ReadCp0 || inst.op == ir::Op::WriteCp0 || inst.op == ir::Op::CheckInterrupts ||
						inst.op == ir::Op::ReadCp2Data || inst.op == ir::Op::WriteCp2Data ||
						inst.op == ir::Op::ReadCp2Control || inst.op == ir::Op::WriteCp2Control))
				{
					if (error) *error = "COP0/COP2/interrupt lowering is only modelled for IOP";
					return false;
				}
				if (guest_state == GuestState::IOP)
				{
					if ((inst.op == ir::Op::ReadHi || inst.op == ir::Op::ReadLo ||
						inst.op == ir::Op::WriteHi || inst.op == ir::Op::WriteLo) && inst.imm != 0)
					{
						if (error) *error = "IOP has only one HI/LO bank";
						return false;
					}
					// IOP accepts 32-bit integer state, memory and control.
					// EE-specific memory and helpers must never use the IOP layout.
					bool supported = inst.type == ir::Type::Void || inst.type == ir::Type::I32;
					for (u32 arg = 0; arg < ir::ValueOperandCount(inst); ++arg)
						supported &= fn.ValueType(inst.args[arg]) == ir::Type::I32;
					switch (inst.op)
					{
						case ir::Op::Nop: case ir::Op::ConstI32: case ir::Op::Copy:
						case ir::Op::Mul: case ir::Op::MulHiS: case ir::Op::MulHiU:
						case ir::Op::DivS: case ir::Op::DivU: case ir::Op::Msub:
						case ir::Op::Add: case ir::Op::Sub: case ir::Op::And:
						case ir::Op::MinU: case ir::Op::MaxS: case ir::Op::Select:
						case ir::Op::Or: case ir::Op::Xor: case ir::Op::Not: case ir::Op::Clz:
						case ir::Op::Shl: case ir::Op::ShrU: case ir::Op::ShrS:
						case ir::Op::CmpLtS: case ir::Op::CmpLtU:
						case ir::Op::CmpEq: case ir::Op::CmpNe: case ir::Op::CmpLeS:
						case ir::Op::CmpGtS: case ir::Op::CmpGeS:
						case ir::Op::Jump: case ir::Op::Branch: case ir::Op::BranchIndirect:
						case ir::Op::ReadCp0: case ir::Op::WriteCp0: case ir::Op::CheckInterrupts:
						case ir::Op::ReadCp2Data: case ir::Op::WriteCp2Data:
						case ir::Op::ReadCp2Control: case ir::Op::WriteCp2Control:
						case ir::Op::ReadGpr: case ir::Op::WriteGpr:
						case ir::Op::ReadHi: case ir::Op::WriteHi:
						case ir::Op::ReadLo: case ir::Op::WriteLo:
						case ir::Op::Load8S: case ir::Op::Load8U:
						case ir::Op::Load16S: case ir::Op::Load16U: case ir::Op::Load32:
						case ir::Op::Store8: case ir::Op::Store16: case ir::Op::Store32:
						case ir::Op::Resume:
							break;
						default: supported = false; break;
					}
					if (!supported || (inst.aux & ir::IF_WIDE_WRITE))
					{
						if (error) *error = "unsupported IOP IR operation or type";
						return false;
					}
				}
				const bool quad_alu = inst.type == ir::Type::V4U32 &&
					(inst.op == ir::Op::Add || inst.op == ir::Op::Sub || inst.op == ir::Op::And ||
						inst.op == ir::Op::Or || inst.op == ir::Op::Xor || inst.op == ir::Op::Not ||
						ir::IsPackedIntegerOp(inst.op) || ir::IsPackedLanePermutationOp(inst.op) || inst.op == ir::Op::VShuffleBytes ||
						inst.op == ir::Op::VShuffle || inst.op == ir::Op::VShuffle2);
				const bool quad_result = inst.type == ir::Type::V4U32 &&
					(inst.op == ir::Op::ConstVec || inst.op == ir::Op::Copy || inst.op == ir::Op::ReadGpr ||
						inst.op == ir::Op::ReadHi || inst.op == ir::Op::ReadLo || inst.op == ir::Op::Load128 || quad_alu);
				for (u32 arg = 0; arg < ir::ValueOperandCount(inst); ++arg)
				{
					const ir::Type type = fn.ValueType(inst.args[arg]);
					const bool quad_operand = type == ir::Type::V4U32 &&
						(quad_alu || (inst.op == ir::Op::Copy && quad_result) || (inst.op == ir::Op::Store128 && arg == 1) ||
							((inst.op == ir::Op::WriteGpr || inst.op == ir::Op::WriteHi || inst.op == ir::Op::WriteLo) && !(inst.aux & ir::IF_WIDE_WRITE)));
					if ((ir::IsVectorType(type) && !quad_operand) ||
						(inst.op == ir::Op::Store128 && arg == 1 && !quad_operand))
					{
						if (error)
							*error = "unsupported IR vector operand";
						return false;
					}
				}
				if ((inst.op == ir::Op::MulHiS || inst.op == ir::Op::MulHiU) &&
					(inst.type != ir::Type::I32 || fn.ValueType(inst.args[0]) != ir::Type::I32 || fn.ValueType(inst.args[1]) != ir::Type::I32))
				{
					if (error) *error = "high multiply requires I32 operands and result";
					return false;
				}
				if ((ir::IsVectorType(inst.type) && !quad_result) || inst.type == ir::Type::Any ||
					(inst.type == ir::Type::I64 && (inst.op == ir::Op::Undef ||
						inst.op == ir::Op::Neg || inst.op == ir::Op::MinS || inst.op == ir::Op::MinU ||
						inst.op == ir::Op::MaxS || inst.op == ir::Op::MaxU || inst.op == ir::Op::DivS ||
						inst.op == ir::Op::DivU || inst.op == ir::Op::RemS || inst.op == ir::Op::RemU)) ||
					((inst.op == ir::Op::ReadGpr || inst.op == ir::Op::WriteGpr) && inst.imm >= 32))
				{
					if (error)
						*error = "unsupported IR operand type or guest register";
					return false;
				}
				switch (inst.op)
				{
					case ir::Op::Jump:
					case ir::Op::Branch:
					case ir::Op::BranchIndirect:
					case ir::Op::Resume:
						break;
					case ir::Op::Trap:
					case ir::Op::CheckEvents:
					case ir::Op::AddCycles:
						if (error)
							*error = "IR exception/event semantics are not modelled";
						return false;
					case ir::Op::Return:
						if (inline_body)
						{
							if (error)
								*error = "inline mode does not support this terminator";
							return false;
						}
						break;
					default:
						if (!IsSupportedOp(inst.op))
						{
							if (error)
								*error = std::string("unsupported IR op ") + ir::OpName(inst.op);
							return false;
						}
						break;
				}
			}
		}
		return true;
	}

	bool LowerBlock(ir::Function& fn, const LowerOptions& options, u8* code, size_t capacity,
		LowerOutput* out, std::string* error)
	{
		*out = {};
		// Optimize a copy so callers can lower the identical input in A/B modes.
		if (options.optimize_ir)
		{
			if (!CanLower(fn, options.inline_body, error, options.guest_state))
				return false;
			ir::Function optimized = fn;
			ForwardGuestStateReads(optimized, options.inline_division, options.forward_quad_state);
			ir::OptimizeIntegerValues(optimized);
			Lowerer lowerer(optimized, options);
			return lowerer.Run(code, capacity, out, error);
		}
		Lowerer lowerer(fn, options);
		return lowerer.Run(code, capacity, out, error);
	}

	bool LowerBlock(ir::Function& fn, u8* code, size_t capacity, LowerOutput* out, std::string* error)
	{
		return LowerBlock(fn, LowerOptions{}, code, capacity, out, error);
	}
} // namespace Arm64Ir

#include "arm64/ir/IrVfMaxMin-arm64.inl"
#include "arm64/ir/IrIopNclip-arm64.inl"

#include "IrIopAvsz-arm64.inl"

#include "IrVfClip-arm64.inl"

#include "IrEeCop1Compare-arm64.inl"
