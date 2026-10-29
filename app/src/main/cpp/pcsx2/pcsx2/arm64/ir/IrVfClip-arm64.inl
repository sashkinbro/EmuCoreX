// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Included once after IrLower-arm64.cpp closes namespace Arm64Ir.
#include "arm64/ir/IrLower-arm64.h"
#include "arm64/OaknutHelpers-arm64.h"

namespace Arm64Ir
{
	bool PlanBoundVfClipBlock(const ir::Function& fn, BoundVfClipPlan* plan, std::string* error)
	{
		if (plan) *plan = {};
		const auto fail = [&](const char* why) { if (error) *error = why; return false; };
		if (!plan || fn.blocks.size() != 1 || fn.entry != 1 || fn.blocks[0].insts.size() != 11)
			return fail("bound CLIP requires one 11-node expression and an output plan");
		const auto& block = fn.blocks[0];
		const auto& i = block.insts;
		constexpr ir::Op ops[] = {ir::Op::ReadVfBits, ir::Op::ReadVfBits, ir::Op::VClip,
			ir::Op::ReadVuClip, ir::Op::ConstI32, ir::Op::Shl, ir::Op::Or,
			ir::Op::ConstI32, ir::Op::And, ir::Op::WriteVuClip, ir::Op::Return};
		for (u32 n = 0; n < 11; ++n)
			if (i[n].op != ops[n]) return fail("bound CLIP operation mismatch");
		if (!ir::Verify(fn, error)) return false;
		if (block.id != 1 || block.flags || !block.predecessors.empty() || !block.successors.empty() ||
			(block.guest_start & 3u) || block.guest_end != block.guest_start + 4 ||
			(fn.guest_entry && fn.guest_entry != block.guest_start) || !fn.vec_consts.empty() ||
			fn.next_value != 10 || fn.value_types.size() != 10)
			return fail("bound CLIP block or value metadata mismatch");
		const u16 delay = i[0].aux & ir::IF_DELAY_SLOT;
		constexpr u8 counts[] = {0, 0, 2, 0, 0, 2, 2, 0, 2, 1, 0};
		constexpr u8 sources[11][2] = {{}, {}, {0, 1}, {}, {}, {3, 4}, {5, 2}, {}, {6, 7}, {8}, {}};
		for (u32 n = 0; n < 11; ++n)
		{
			const auto& inst = i[n];
			const ir::Type type = n < 2 ? ir::Type::V4I32 : n < 9 ? ir::Type::I32 : ir::Type::Void;
			const u64 imm = n < 2 ? inst.imm : n == 4 ? 6 : n == 7 ? 0xffffff : 0;
			if ((n < 2 && inst.imm >= 32) || inst.type != type || inst.imm != imm ||
				inst.guest_pc != block.guest_start || inst.aux != delay ||
				inst.value != (n < 9 ? n + 1 : 0) || inst.num_args != counts[n])
				return fail("bound CLIP type, definition, state, or instruction metadata mismatch");
			for (u32 a = 0; a < 4; ++a)
				if (inst.args[a] != (a < counts[n] ? i[sources[n][a]].value : 0))
					return fail("bound CLIP SSA operand mismatch");
		}
		*plan = {static_cast<u8>(i[0].imm), static_cast<u8>(i[1].imm), delay != 0};
		if (error) error->clear();
		return true;
	}

	bool LowerBoundVfClipPlan(const BoundVfClipPlan& plan, u32 lhs_host, u32 rhs_host,
		u32 clip_host, LowerOutput* out, std::string* error)
	{
		if (out) *out = {};
		if (!out || !oakHasBlock() || plan.lhs_vf >= 32 || plan.rhs_vf >= 32 || lhs_host >= 29 || rhs_host >= 29 ||
			clip_host >= 16 || (lhs_host == rhs_host && plan.lhs_vf != plan.rhs_vf))
		{
			if (error) *error = "invalid bound CLIP plan or caller bindings";
			return false;
		}
		const u8* start = recBeginOaknutEmit();
		const oak::QReg fs(lhs_host), ft(rhs_host), threshold(29), positive(30), negative(31);
		const oak::WReg history(clip_host);
		// Raw integer threshold and comparisons preserve FPCR/FPSR and inputs.
		oakAsm->UMOV(oak::util::W16, ft.Selem()[3]);
		oakAsm->AND(oak::util::W17, oak::util::W16, 0x7fffffffu);
		oakAsm->TST(oak::util::W16, 0x7f800000u);
		oakAsm->MOV(oak::util::W16, 0x007fffffu);
		oakAsm->CSEL(oak::util::W16, oak::util::W17, oak::util::W16, oak::Cond::NE);
		oakAsm->DUP(threshold.S4(), oak::util::W16);
		oakAsm->CMGT(positive.S4(), fs.S4(), threshold.S4());
		oakAsm->MOV(oak::util::W16, 0x80000000u);
		oakAsm->DUP(negative.S4(), oak::util::W16);
		oakAsm->EOR(negative.B16(), negative.B16(), fs.B16());
		oakAsm->CMGT(negative.S4(), negative.S4(), threshold.S4());
		// These immutable host constants are independent of VU state/ring slots.
		alignas(16) static constexpr u32 weights[4] = {1, 4, 16, 0};
		oakMoveAddressToReg(oak::util::X17, weights);
		oakAsm->LDR(threshold, oak::util::X17);
		oakAsm->AND(positive.B16(), positive.B16(), threshold.B16());
		oakAsm->SHL(threshold.S4(), threshold.S4(), 1);
		oakAsm->AND(negative.B16(), negative.B16(), threshold.B16());
		oakAsm->ADDV(oak::SReg(30), positive.S4());
		oakAsm->ADDV(oak::SReg(31), negative.S4());
		oakAsm->FMOV(oak::util::W16, oak::SReg(30));
		oakAsm->FMOV(oak::util::W17, oak::SReg(31));
		oakAsm->LSL(history, history, 6);
		oakAsm->AND(history, history, 0xffffffu);
		oakAsm->ORR(history, history, oak::util::W16);
		oakAsm->ORR(history, history, oak::util::W17);
		out->host_size = static_cast<u32>(oakGetCurrentCodePointer() - start);
		recEndOaknutEmit();
		if (error) error->clear();
		return true;
	}
} // namespace Arm64Ir
