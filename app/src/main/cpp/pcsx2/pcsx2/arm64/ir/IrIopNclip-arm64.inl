// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Included once after IrLower-arm64.cpp closes namespace Arm64Ir.
#include "arm64/ir/IrLower-arm64.h"

namespace Arm64Ir
{
	bool PlanIopNclipWindow(const ir::Function& fn, u32 block_index, u32 first_inst, std::string* error)
	{
		const auto fail = [&](const char* message) { if (error) *error = message; return false; };
		// Verify currently indexes OpInfo before checking the opcode range.
		// Validate that range throughout the function before calling it, including
		// unrelated instructions and terminators outside the candidate window.
		for (const auto& block : fn.blocks)
			for (const auto& inst : block.insts)
				if (static_cast<u32>(inst.op) >= static_cast<u32>(ir::Op::Count))
					return fail("IOP determinant window: invalid opcode in function");
		if (!ir::Verify(fn, error)) return false;
		constexpr u32 count = 24;
		if (block_index >= fn.blocks.size())
			return fail("IOP determinant window: block index is out of range");
		const auto& instructions = fn.blocks[block_index].insts;
		if (first_inst > instructions.size() || instructions.size() - first_inst < count)
			return fail("IOP determinant window: incomplete expression");
		const ir::Inst* const i = instructions.data() + first_inst;
		constexpr ir::Op operations[count] = {
			ir::Op::ReadCp2Data, ir::Op::ReadCp2Data, ir::Op::ReadCp2Data, ir::Op::ConstI32,
			ir::Op::Shl, ir::Op::ShrS, ir::Op::ShrS,
			ir::Op::Shl, ir::Op::ShrS, ir::Op::ShrS,
			ir::Op::Shl, ir::Op::ShrS, ir::Op::ShrS,
			ir::Op::Sub, ir::Op::Mul, ir::Op::Sub, ir::Op::Mul, ir::Op::Sub, ir::Op::Mul,
			ir::Op::Add, ir::Op::Add, ir::Op::WriteCp2Data, ir::Op::ConstI32, ir::Op::WriteCp2Control};
		constexpr u8 operand_counts[count] = {0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2,
			2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 1};
		// Entries are defining instruction indices, not fixed SSA value numbers.
		constexpr u8 operands[count][2] = {
			{0, 0}, {0, 0}, {0, 0}, {0, 0},
			{0, 3}, {4, 3}, {0, 3}, {1, 3}, {7, 3}, {1, 3}, {2, 3}, {10, 3}, {2, 3},
			{9, 12}, {5, 13}, {12, 6}, {8, 15}, {6, 9}, {11, 17}, {14, 16}, {19, 18},
			{20, 0}, {0, 0}, {22, 0}};
		const u16 delay = i[0].aux & ir::IF_DELAY_SLOT;
		const u32 pc = i[0].guest_pc;
		if (pc & 3u)
			return fail("IOP determinant window: misaligned guest PC");
		for (u32 node = 0; node < count; ++node)
		{
			const auto& inst = i[node];
			const bool writes = node == 21 || node == 23;
			const ir::Type type = writes ? ir::Type::Void : ir::Type::I32;
			const u64 immediate = node < 3 ? 12u + node : node == 3 ? 16u :
				node == 21 ? 24u : node == 23 ? 31u : 0u;
			if (inst.op != operations[node] || inst.type != type || inst.imm != immediate ||
				inst.aux != delay || inst.guest_pc != pc || inst.num_args != operand_counts[node] ||
				(writes ? inst.value != 0 : inst.value == 0))
				return fail("IOP determinant window: operation, type, state, or metadata mismatch");
			for (u32 arg = 0; arg < 4; ++arg)
			{
				const u32 expected = arg < operand_counts[node] ? i[operands[node][arg]].value : 0;
				if (inst.args[arg] != expected)
					return fail("IOP determinant window: operand links do not match the expression");
			}
		}

		// Every value defined by the window is private to it. Skipping allocation
		// or materialization is unsafe even for an extra user of its constants or
		// raw input words. Also reject duplicate definitions in another block:
		// generic verification currently enforces uniqueness only within a block.
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
					return fail("IOP determinant window: value is defined outside the window");
				const u32 uses = ir::ValueOperandCount(inst);
				for (u32 arg = 0; arg < uses; ++arg)
					if (window_values[inst.args[arg]])
						return fail("IOP determinant window: value escapes the window");
			}
		}
		if (error) error->clear();
		return true;
	}
} // namespace Arm64Ir
