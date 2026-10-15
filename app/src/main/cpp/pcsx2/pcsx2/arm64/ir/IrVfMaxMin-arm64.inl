// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Included once after IrLower-arm64.cpp closes namespace Arm64Ir.
#include "arm64/ir/IrLower-arm64.h"
#include "arm64/OaknutHelpers-arm64.h"

namespace Arm64Ir
{
	bool PlanBoundVfMaxMinBlock(const ir::Function& fn, BoundVfMaxMinPlan* plan, std::string* error)
	{
		if (plan) *plan = {};
		const auto fail = [&](const char* message) { if (error) *error = message; return false; };
		if (!plan) return fail("bound VF MAX/MINI plan output is missing");
		if (fn.blocks.size() != 1 || fn.entry != 1 ||
			(fn.blocks[0].insts.size() != 17 && fn.blocks[0].insts.size() != 18))
			return fail("bound VF MAX/MINI requires one 17- or 18-node expression graph");
		const auto& block = fn.blocks[0];
		const auto& i = block.insts;
		const bool broadcast = i.size() == 18;
		// Normalize the original 17-node graph around the optional RHS splat.
		// The two state reads remain raw; only RHS expression uses see the splat.
		const auto base = [&](u32 node) -> const ir::Inst& {
			return i[node + (broadcast && node >= 2 ? 1 : 0)];
		};
		const bool scalar_i = broadcast && base(1).op == ir::Op::ReadVuScalarBits;
		constexpr ir::Op ops[] = {ir::Op::ReadVfBits, ir::Op::ReadVfBits,
			ir::Op::ConstI32, ir::Op::ConstI32, ir::Op::VShrS, ir::Op::VShrU,
			ir::Op::VXor, ir::Op::VShrS, ir::Op::VShrU, ir::Op::VXor,
			ir::Op::VCmpLtS, ir::Op::VAnd, ir::Op::VNot, ir::Op::VAnd,
			ir::Op::VOr, ir::Op::WriteVfBitsMasked, ir::Op::Return};
		// Check opcodes before the generic verifier indexes their OpInfo entries.
		for (u32 node = 0; node < 17; ++node)
			if (base(node).op != (scalar_i && node == 1 ? ir::Op::ReadVuScalarBits : ops[node]))
				return fail("unsupported bound VF MAX/MINI expression operation");
		if (broadcast && i[2].op != (scalar_i ? ir::Op::VSplat32 : ir::Op::VShuffle))
			return fail("bound VF MAX/MINI requires an explicit typed RHS splat");
		if (!ir::Verify(fn, error)) return false;
		if (block.id != 1 || block.flags != 0 || !block.predecessors.empty() || !block.successors.empty() ||
			block.guest_end != block.guest_start + 4 ||
			(fn.guest_entry != 0 && fn.guest_entry != block.guest_start) ||
			!fn.vec_consts.empty() || fn.value_types.size() != 16u + broadcast || fn.next_value != 16u + broadcast)
			return fail("bound VF MAX/MINI has unsupported block or value metadata");
		if (base(0).imm >= 32 ||
			(scalar_i ? base(1).imm != static_cast<u64>(ir::VuScalar::I) : base(1).imm >= 32) ||
			base(15).imm == 0 || base(15).imm >= 32 ||
			(base(15).aux & 0x0f) == 0 || base(2).imm != 31 || base(3).imm != 1)
			return fail("bound VF MAX/MINI has invalid register, lane mask, or shift constant");
		const u16 delay = base(0).aux & ir::IF_DELAY_SLOT;
		u8 rhs_lane = 4;
		if (broadcast)
		{
			const auto& splat = i[2];
			if ((scalar_i ? splat.imm != 0 :
				(splat.imm != 0x00 && splat.imm != 0x55 && splat.imm != 0xaa && splat.imm != 0xff)) ||
				splat.type != ir::Type::V4I32 || splat.value == 0 ||
				splat.num_args != 1 || splat.args[0] != base(1).value ||
				splat.args[1] != 0 || splat.args[2] != 0 || splat.args[3] != 0 ||
				splat.aux != delay || splat.guest_pc != block.guest_start)
				return fail("bound VF MAX/MINI has an invalid raw RHS splat");
			if (!scalar_i)
				rhs_lane = static_cast<u8>(splat.imm / 0x55);
		}
		for (u32 node = 0; node < 17; ++node)
		{
			const auto& inst = base(node);
			const ir::Type type = node >= 15 ? ir::Type::Void :
				(node == 2 || node == 3 || (scalar_i && node == 1)) ? ir::Type::I32 : ir::Type::V4I32;
			const u16 aux = delay | (node == 15 ? base(15).aux & 0x0f : 0);
			if (inst.type != type || inst.aux != aux || inst.guest_pc != block.guest_start ||
				(node >= 4 && node != 15 && inst.imm != 0))
				return fail("bound VF MAX/MINI has invalid types, flags, PC, or immediates");
			if ((node < 15 && inst.value == 0) || (node >= 15 && inst.value != 0))
				return fail("bound VF MAX/MINI has invalid result definitions");
			for (u32 arg = inst.num_args; arg < 4; ++arg)
				if (inst.args[arg] != 0)
					return fail("bound VF MAX/MINI has unused nonzero operands");
		}
		const auto expression_value = [&](u32 node) {
			return broadcast && node == 1 ? i[2].value : base(node).value;
		};
		const auto args = [&](u32 node, u32 count, u32 first = 0, u32 second = 0) {
			return base(node).num_args == count &&
				(count < 1 || base(node).args[0] == expression_value(first)) &&
				(count < 2 || base(node).args[1] == expression_value(second));
		};
		const bool maximum = args(10, 2, 9, 6);
		if (!args(0, 0) || !args(1, 0) || !args(2, 0) || !args(3, 0) ||
			!args(4, 2, 0, 2) || !args(5, 2, 4, 3) || !args(6, 2, 0, 5) ||
			!args(7, 2, 1, 2) || !args(8, 2, 7, 3) || !args(9, 2, 1, 8) ||
			(!maximum && !args(10, 2, 6, 9)) || !args(11, 2, 0, 10) ||
			!args(12, 1, 10) || !args(13, 2, 1, 12) || !args(14, 2, 11, 13) ||
			!args(15, 1, 14) || !args(16, 0))
			return fail("bound VF MAX/MINI SSA links do not match the raw ordering expression");
		*plan = {static_cast<u8>(base(0).imm), static_cast<u8>(scalar_i ? 0 : base(1).imm),
			static_cast<u8>(base(15).imm), static_cast<u8>(base(15).aux & 0x0f), maximum, delay != 0, rhs_lane,
			scalar_i ? BoundVfMaxMinRhs::ScalarI : BoundVfMaxMinRhs::Vector};
		if (error) error->clear();
		return true;
	}

	bool LowerBoundVfMaxMinPlan(const BoundVfMaxMinPlan& plan, u32 lhs_host, u32 rhs_host, u32 dst_host,
		LowerOutput* out, std::string* error, BoundVfMaxMinLayout layout)
	{
		if (out) *out = {};
		const bool scalar_i = plan.rhs_kind == BoundVfMaxMinRhs::ScalarI;
		if (!out || !oakHasBlock() || plan.lhs_vf >= 32 || plan.rhs_vf >= 32 ||
			plan.dst_vf == 0 || plan.dst_vf >= 32 || plan.lane_mask == 0 || plan.lane_mask > 15 || plan.rhs_lane > 4 ||
			(plan.rhs_kind != BoundVfMaxMinRhs::Vector && !scalar_i) ||
			(scalar_i && (plan.rhs_vf != 0 || plan.rhs_lane != 4 || dst_host == rhs_host)) ||
			lhs_host >= 29 || rhs_host >= 29 || dst_host >= 29 ||
			(lhs_host == rhs_host && (scalar_i || plan.lhs_vf != plan.rhs_vf)) ||
			(layout != BoundVfMaxMinLayout::GuestLanes && layout != BoundVfMaxMinLayout::MicroVuPartial))
		{
			if (error) *error = "invalid bound VF MAX/MINI plan, host bindings, or destination layout";
			return false;
		}

		const u8* start = recBeginOaknutEmit();
		const bool broadcast = scalar_i || plan.rhs_lane < 4;
		const u32 effective_rhs_host = broadcast ? 29 : rhs_host;
		const oak::QReg lhs(lhs_host), rhs(effective_rhs_host), dst(dst_host);
		// ScalarI binds only the raw word in S0. Its other lanes are unspecified,
		// and the complete caller-owned I binding remains read-only.
		if (broadcast)
			oakAsm->DUP(rhs.S4(), oak::QReg(rhs_host).Selem()[scalar_i ? 0 : plan.rhs_lane]);
		const oak::QReg key_a = OAK_QSCRATCH, key_b = OAK_QSCRATCH2;
		// Fuse the two sign-key graphs into six integer SIMD instructions.
		// The signed comparison orders all raw words, including NaN payloads
		// and signed zeros, without reading or changing FPCR/FPSR.
		oakAsm->SSHR(key_a.S4(), lhs.S4(), 31);
		oakAsm->USHR(key_a.S4(), key_a.S4(), 1);
		oakAsm->EOR(key_a.B16(), key_a.B16(), lhs.B16());
		oakAsm->SSHR(key_b.S4(), rhs.S4(), 31);
		oakAsm->USHR(key_b.S4(), key_b.S4(), 1);
		oakAsm->EOR(key_b.B16(), key_b.B16(), rhs.B16());
		const bool one_lane = (plan.lane_mask & (plan.lane_mask - 1)) == 0;
		const bool whole_result = plan.lane_mask == 15 ||
			(layout == BoundVfMaxMinLayout::MicroVuPartial && !one_lane);
		// Aliasing is against the effective RHS. For a broadcast, the raw RHS
		// destination no longer contains operand B; its splat is now in Q29.
		const bool independent_dst = dst_host != lhs_host && dst_host != effective_rhs_host;
		const oak::QReg mask = whole_result && independent_dst ? dst : key_a;
		if (plan.maximum)
			oakAsm->CMGT(mask.S4(), key_a.S4(), key_b.S4());
		else
			oakAsm->CMGT(mask.S4(), key_b.S4(), key_a.S4());
		if (whole_result)
		{
			// Fuse (A & mask) | (B & ~mask), retaining either aliased input.
			if (dst_host == lhs_host)
				oakAsm->BIF(dst.B16(), rhs.B16(), mask.B16());
			else if (dst_host == effective_rhs_host)
				oakAsm->BIT(dst.B16(), lhs.B16(), mask.B16());
			else
				oakAsm->BSL(dst.B16(), lhs.B16(), rhs.B16());
		}
		else
		{
			// Finish in reserved scratch before committing any aliased lane.
			oakAsm->BSL(mask.B16(), lhs.B16(), rhs.B16());
			if (layout == BoundVfMaxMinLayout::MicroVuPartial)
			{
				u32 lane = 0;
				while ((plan.lane_mask & (8u >> lane)) == 0) ++lane;
				oakAsm->DUP(dst.S4(), mask.Selem()[lane]);
			}
			else
				for (u32 lane = 0; lane < 4; ++lane)
					if (plan.lane_mask & (8u >> lane))
						oakAsm->INS(dst.Selem()[lane], mask.Selem()[lane]);
		}
		out->host_size = static_cast<u32>(oakGetCurrentCodePointer() - start);
		recEndOaknutEmit();
		if (error) error->clear();
		return true;
	}
} // namespace Arm64Ir
