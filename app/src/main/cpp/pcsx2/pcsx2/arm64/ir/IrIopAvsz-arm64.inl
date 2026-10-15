// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Included once after IrLower-arm64.cpp closes namespace Arm64Ir.
#include "arm64/ir/IrLower-arm64.h"

namespace Arm64Ir
{
	bool PlanIopAvszWindow(const ir::Function& fn, u32 block_index, u32 first_inst,
		IopAvszPlan* plan, std::string* error)
	{
		if (plan) *plan = {};
		const auto fail = [&](const char* message) { if (error) *error = message; return false; };
		if (!plan) return fail("IOP AVSZ window: missing plan output");
		// Generic verification indexes OpInfo; reject unknown opcodes first,
		// including instructions outside the proposed expression window.
		for (const auto& block : fn.blocks)
			for (const auto& inst : block.insts)
				if (static_cast<u32>(inst.op) >= static_cast<u32>(ir::Op::Count))
					return fail("IOP AVSZ window: invalid opcode in function");
		if (!ir::Verify(fn, error)) return false;
		if (block_index >= fn.blocks.size())
			return fail("IOP AVSZ window: block index is out of range");
		const auto& instructions = fn.blocks[block_index].insts;
		constexpr u32 avsz3_count = 29, avsz4_count = 32;
		if (first_inst > instructions.size() || instructions.size() - first_inst < avsz3_count)
			return fail("IOP AVSZ window: incomplete expression");
		const ir::Inst* const i = instructions.data() + first_inst;
		const bool avsz4 = i[6].imm == 16;
		const u32 count = avsz4 ? avsz4_count : avsz3_count;
		if (instructions.size() - first_inst < count)
			return fail("IOP AVSZ window: incomplete four-depth expression");

		// Encode the exact common-IR graph using defining instruction indices,
		// independent of the function's SSA numbering and neighboring blocks.
		struct Expected
		{
			ir::Op op{};
			u64 imm = 0;
			u32 num_args = 0;
			std::array<u32, 3> args{};
		};
		std::array<Expected, avsz4_count> expected{};
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
		const u32 low_mask = append(ir::Op::ConstI32, 0xffff, {});
		const u32 shift16 = append(ir::Op::ConstI32, 16, {});
		const u32 shift12 = append(ir::Op::ConstI32, 12, {});
		const u32 upper_mask = append(ir::Op::ConstI32, 0xffff0000u, {});
		const u32 overflow_flag = append(ir::Op::ConstI32, 0x80040000u, {});
		const u32 first_sz = avsz4 ? 16u : 17u;
		const u32 first_word = append(ir::Op::ReadCp2Data, first_sz, {});
		u32 sum = append(ir::Op::And, 0, {first_word, low_mask});
		for (u32 reg = first_sz + 1; reg <= 19; ++reg)
		{
			const u32 word = append(ir::Op::ReadCp2Data, reg, {});
			const u32 sz = append(ir::Op::And, 0, {word, low_mask});
			sum = append(ir::Op::Add, 0, {sum, sz});
		}
		const u32 raw_scale = append(ir::Op::ReadCp2Control, avsz4 ? 30u : 29u, {});
		const u32 scale_left = append(ir::Op::Shl, 0, {raw_scale, shift16});
		const u32 scale = append(ir::Op::ShrS, 0, {scale_left, shift16});
		const u32 product = append(ir::Op::Mul, 0, {sum, scale});
		const u32 shifted = append(ir::Op::ShrS, 0, {product, shift12});
		append(ir::Op::WriteCp2Data, 24, {shifted});
		const u32 nonnegative = append(ir::Op::MaxS, 0, {shifted, zero});
		const u32 otz = append(ir::Op::MinU, 0, {nonnegative, low_mask});
		const u32 old_d7 = append(ir::Op::ReadCp2Data, 7, {});
		const u32 kept_upper = append(ir::Op::And, 0, {old_d7, upper_mask});
		const u32 merged = append(ir::Op::Or, 0, {kept_upper, otz});
		append(ir::Op::WriteCp2Data, 7, {merged});
		const u32 overflow = append(ir::Op::CmpNe, 0, {shifted, otz});
		const u32 flag = append(ir::Op::Select, 0, {overflow, overflow_flag, zero});
		append(ir::Op::WriteCp2Control, 31, {flag});

		const u32 pc = i[0].guest_pc;
		const u16 delay = i[0].aux & ir::IF_DELAY_SLOT;
		if ((pc & 3u) != 0 || next != count)
			return fail("IOP AVSZ window: invalid guest PC or expression length");
		for (u32 node = 0; node < count; ++node)
		{
			const auto& inst = i[node];
			const auto& item = expected[node];
			const bool writes = item.op == ir::Op::WriteCp2Data || item.op == ir::Op::WriteCp2Control;
			if (inst.op != item.op || inst.type != (writes ? ir::Type::Void : ir::Type::I32) ||
				inst.imm != item.imm || inst.aux != delay || inst.guest_pc != pc ||
				inst.num_args != item.num_args || (writes ? inst.value != 0 : inst.value == 0))
				return fail("IOP AVSZ window: operation, type, state, or metadata mismatch");
			for (u32 arg = 0; arg < 4; ++arg)
			{
				const u32 source = arg < item.num_args ? i[item.args[arg]].value : 0;
				if (inst.args[arg] != source)
					return fail("IOP AVSZ window: operand links do not match the expression");
			}
		}

		// The fused body does not materialize any of these values, including
		// constants and raw reads. Reject all external users and definitions.
		std::vector<bool> window_values(fn.value_types.size(), false);
		for (u32 node = 0; node < count; ++node)
			if (i[node].value != 0) window_values[i[node].value] = true;
		for (size_t block = 0; block < fn.blocks.size(); ++block)
		{
			const auto& outside = fn.blocks[block].insts;
			for (size_t pos = 0; pos < outside.size(); ++pos)
			{
				if (block == block_index && pos >= first_inst && pos - first_inst < count)
					continue;
				const auto& inst = outside[pos];
				if (window_values[inst.value])
					return fail("IOP AVSZ window: value is defined outside the window");
				for (u32 arg = 0; arg < ir::ValueOperandCount(inst); ++arg)
					if (window_values[inst.args[arg]])
						return fail("IOP AVSZ window: value escapes the window");
			}
		}
		*plan = {count, avsz4};
		if (error) error->clear();
		return true;
	}
} // namespace Arm64Ir
