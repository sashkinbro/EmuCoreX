// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Unwired: include once after IrLower-arm64.cpp closes namespace Arm64Ir,
// after EeCop1ComparePlan and PlanEeCop1CompareWindow are declared in its header.
#include "arm64/ir/IrLower-arm64.h"

namespace Arm64Ir
{
	bool PlanEeCop1CompareWindow(const ir::Function& fn, u32 block_index, u32 first_inst,
		EeCop1ComparePlan* plan, std::string* error)
	{
		if (plan) *plan = {};
		const auto fail = [&](const char* message) { if (error) *error = message; return false; };
		if (!plan) return fail("EE COP1 compare window: missing plan output");
		// Verify uses opcode-indexed metadata, so validate every opcode first.
		for (const auto& block : fn.blocks)
			for (const auto& inst : block.insts)
				if (static_cast<u32>(inst.op) >= static_cast<u32>(ir::Op::Count))
					return fail("EE COP1 compare window: invalid opcode in function");
		if (!ir::Verify(fn, error)) return false;
		if (block_index >= fn.blocks.size())
			return fail("EE COP1 compare window: block index is out of range");
		const auto& instructions = fn.blocks[block_index].insts;
		constexpr u32 count = 35;
		if (first_inst > instructions.size() || instructions.size() - first_inst < count)
			return fail("EE COP1 compare window: incomplete expression");
		const ir::Inst* const i = instructions.data() + first_inst;
		const ir::Op predicate = i[29].op;
		if (predicate != ir::Op::CmpEq && predicate != ir::Op::CmpLtU && predicate != ir::Op::CmpLeU)
			return fail("EE COP1 compare window: unsupported key predicate");
		if (i[7].imm >= 32 || i[18].imm >= 32)
			return fail("EE COP1 compare window: source register is out of range");

		// Source links refer to defining instruction positions rather than fixed
		// SSA IDs. Neighboring expressions and blocks may use any valid IDs.
		struct Expected
		{
			ir::Op op{};
			u64 imm = 0;
			u32 num_args = 0;
			std::array<u32, 3> args{};
		};
		std::array<Expected, count> expected{};
		u32 next = 0;
		const auto append = [&](ir::Op op, u64 imm, std::initializer_list<u32> args) {
			const u32 index = next++;
			auto& item = expected[index];
			item.op = op;
			item.imm = imm;
			item.num_args = static_cast<u32>(args.size());
			u32 arg = 0;
			for (const u32 source : args) item.args[arg++] = source;
			return index;
		};
		const u32 zero = append(ir::Op::ConstI32, 0, {});
		const u32 exponent_mask = append(ir::Op::ConstI32, 0x7f800000u, {});
		const u32 sign_mask = append(ir::Op::ConstI32, 0x80000000u, {});
		const u32 max_finite = append(ir::Op::ConstI32, 0x7f7fffffu, {});
		const u32 shift31 = append(ir::Op::ConstI32, 31, {});
		const u32 clear_condition = append(ir::Op::ConstI32, 0xff7fffffu, {});
		const u32 shift23 = append(ir::Op::ConstI32, 23, {});
		const auto append_key = [&](u32 fpr) {
			const u32 raw = append(ir::Op::ReadFprBits, fpr, {});
			const u32 exponent = append(ir::Op::And, 0, {raw, exponent_mask});
			const u32 exponent_zero = append(ir::Op::CmpEq, 0, {exponent, zero});
			const u32 sign = append(ir::Op::And, 0, {raw, sign_mask});
			const u32 clamped = append(ir::Op::Or, 0, {sign, max_finite});
			const u32 exponent_all = append(ir::Op::CmpEq, 0, {exponent, exponent_mask});
			const u32 normalized_inf = append(ir::Op::Select, 0, {exponent_all, clamped, raw});
			const u32 normalized = append(ir::Op::Select, 0, {exponent_zero, zero, normalized_inf});
			const u32 sign_fill = append(ir::Op::ShrS, 0, {normalized, shift31});
			const u32 key_mask = append(ir::Op::Or, 0, {sign_fill, sign_mask});
			return append(ir::Op::Xor, 0, {normalized, key_mask});
		};
		const u32 lhs = append_key(static_cast<u32>(i[7].imm));
		const u32 rhs = append_key(static_cast<u32>(i[18].imm));
		const u32 condition = append(predicate, 0, {lhs, rhs});
		const u32 old_fcr31 = append(ir::Op::ReadFcr31, 0, {});
		const u32 cleared = append(ir::Op::And, 0, {old_fcr31, clear_condition});
		const u32 shifted = append(ir::Op::Shl, 0, {condition, shift23});
		const u32 merged = append(ir::Op::Or, 0, {cleared, shifted});
		append(ir::Op::WriteFcr31, 0, {merged});

		const u32 pc = i[0].guest_pc;
		const u16 delay = i[0].aux & ir::IF_DELAY_SLOT;
		if ((pc & 3u) != 0 || next != count)
			return fail("EE COP1 compare window: invalid guest PC or expression length");
		for (u32 node = 0; node < count; ++node)
		{
			const auto& inst = i[node];
			const auto& item = expected[node];
			const bool writes = node == count - 1;
			if (inst.op != item.op || inst.type != (writes ? ir::Type::Void : ir::Type::I32) ||
				inst.imm != item.imm || inst.aux != delay || inst.guest_pc != pc ||
				inst.num_args != item.num_args || (writes ? inst.value != 0 : inst.value == 0))
				return fail("EE COP1 compare window: operation, type, state, or metadata mismatch");
			for (u32 arg = 0; arg < 4; ++arg)
				if (inst.args[arg] != (arg < item.num_args ? i[item.args[arg]].value : 0))
					return fail("EE COP1 compare window: operand links do not match the expression");
		}

		// No value (including constants, raw reads, and old FCR31) is exported
		// by the prospective fused body. Require private, unique definitions.
		std::vector<bool> window_values(fn.value_types.size(), false);
		for (u32 node = 0; node < count - 1; ++node)
		{
			const u32 value = i[node].value;
			if (value >= window_values.size() || window_values[value])
				return fail("EE COP1 compare window: duplicate or invalid value definition");
			window_values[value] = true;
		}
		for (size_t block = 0; block < fn.blocks.size(); ++block)
		{
			const auto& outside = fn.blocks[block].insts;
			for (size_t pos = 0; pos < outside.size(); ++pos)
			{
				if (block == block_index && pos >= first_inst && pos - first_inst < count)
					continue;
				const auto& inst = outside[pos];
				if (inst.value >= window_values.size() || window_values[inst.value])
					return fail("EE COP1 compare window: invalid or external value definition");
				for (u32 arg = 0; arg < ir::ValueOperandCount(inst); ++arg)
					if (inst.args[arg] >= window_values.size() || window_values[inst.args[arg]])
						return fail("EE COP1 compare window: value escapes the window");
			}
		}
		*plan = {count, static_cast<u8>(i[7].imm), static_cast<u8>(i[18].imm), predicate};
		if (error) error->clear();
		return true;
	}
} // namespace Arm64Ir
