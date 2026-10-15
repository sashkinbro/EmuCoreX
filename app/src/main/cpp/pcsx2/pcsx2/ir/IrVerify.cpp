// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+

#include "ir/Ir.h"

#include <algorithm>
#include <string>

namespace ir
{
	namespace
	{
		bool Fail(std::string* error, std::string_view message)
		{
			if (error)
				*error = std::string(message);
			return false;
		}

		bool CheckOperandType(const Function& fn, const Inst& inst, u32 arg_index, Type expected, std::string* error)
		{
			if (arg_index >= inst.arg_count())
				return Fail(error, std::string(OpName(inst.op)) + ": missing operand");

			const u32 id = inst.args[arg_index];
			if (id == 0 || id >= fn.value_types.size())
				return Fail(error, std::string(OpName(inst.op)) + ": operand is not an SSA value");

			const Type actual = fn.ValueType(id);
			if (expected != Type::Any && actual != expected)
				return Fail(error, std::string(OpName(inst.op)) + ": operand type mismatch");

			return true;
		}

		bool VerifyInstruction(const Function& fn, const Block& block, const Inst& inst, u32 index,
			const std::vector<bool>& defined, std::string* error)
		{
			const OpInfo& info = Info(inst.op);
			const bool is_last = (index + 1 == block.insts.size());

			if (inst.num_args < info.min_args || inst.num_args > info.max_args)
				return Fail(error, std::string(OpName(inst.op)) + ": bad operand count");

			if ((inst.op == Op::ReadCp0 || inst.op == Op::WriteCp0) && inst.imm >= 32)
				return Fail(error, "COP0: register index is out of range");
			if ((inst.op == Op::ReadCp2Data || inst.op == Op::WriteCp2Data ||
				inst.op == Op::ReadCp2Control || inst.op == Op::WriteCp2Control) &&
				(inst.imm >= 32 || (inst.aux & ~IF_DELAY_SLOT) != 0))
				return Fail(error, "COP2: register index or flags are out of range");
			if ((inst.op == Op::ReadFprBits || inst.op == Op::WriteFprBits) &&
				(inst.imm >= 32 || (inst.aux & ~IF_DELAY_SLOT) != 0))
				return Fail(error, "FPR bits: register index or flags are out of range");
			if ((inst.op == Op::ReadFcr31 || inst.op == Op::WriteFcr31) &&
				(inst.imm != 0 || (inst.aux & ~IF_DELAY_SLOT) != 0))
				return Fail(error, "FCR31: fixed register or flags are out of range");
			if ((inst.op == Op::ReadHi || inst.op == Op::ReadLo || inst.op == Op::WriteHi || inst.op == Op::WriteLo) && inst.imm > 1)
				return Fail(error, "HI/LO: bank index is out of range");
			if (inst.op == Op::ConstVec && inst.imm >= fn.vec_consts.size())
				return Fail(error, "ConstVec: constant pool index is out of range");
			if (inst.op == Op::ReadVfLaneBits &&
				(inst.imm >= 32 || (inst.aux & ~(u16(3) | IF_DELAY_SLOT)) != 0))
				return Fail(error, "ReadVfLaneBits: VF index or lane flags are out of range");
			if (inst.op == Op::ReadVf &&
				(inst.imm >= 32 || (inst.aux & ~IF_DELAY_SLOT) != 0))
				return Fail(error, "ReadVf: VF index or flags are out of range");
			if (inst.op == Op::ReadVfBits &&
				(inst.imm >= 32 || (inst.aux & ~IF_DELAY_SLOT) != 0))
				return Fail(error, "ReadVfBits: VF index or flags are out of range");
			if ((inst.op == Op::ReadVuClip || inst.op == Op::WriteVuClip || inst.op == Op::VClip) &&
				(inst.imm != 0 || (inst.aux & ~IF_DELAY_SLOT) != 0))
				return Fail(error, "VU CLIP: invalid selector or flags");
			if (inst.op == Op::ReadVuScalarBits &&
				(inst.imm != static_cast<u64>(VuScalar::I) || (inst.aux & ~IF_DELAY_SLOT) != 0))
				return Fail(error, "ReadVuScalarBits: scalar selector or flags are out of range");
			if (inst.op == Op::WriteVfLaneSplatBits &&
				(inst.imm >= 32 || !(inst.aux & 0x0f) || (inst.aux & ~(u16(0x0f) | IF_DELAY_SLOT)) != 0))
				return Fail(error, "WriteVfLaneSplatBits: VF index, lane mask, or flags are out of range");
			if (inst.op == Op::WriteVfMasked &&
				(inst.imm >= 32 || !(inst.aux & 0x0f) || (inst.aux & ~(u16(0x0f) | IF_DELAY_SLOT)) != 0))
				return Fail(error, "WriteVfMasked: VF index, lane mask, or flags are out of range");
			if (inst.op == Op::WriteVfBitsMasked &&
				(inst.imm >= 32 || !(inst.aux & 0x0f) || (inst.aux & ~(u16(0x0f) | IF_DELAY_SLOT)) != 0))
				return Fail(error, "WriteVfBitsMasked: VF index, lane mask, or flags are out of range");
			if ((inst.op == Op::VShuffle && inst.imm > 0xff) || (inst.op == Op::VShuffle2 && inst.imm > 0xfff))
				return Fail(error, "vector shuffle: lane selectors are out of range");

			if (info.terminator != is_last)
				return Fail(error, std::string(OpName(inst.op)) + ": terminator placement is wrong");

			// Every value operand must be defined earlier in this block. Block
			// targets of control instructions are checked separately.
			if (info.kind != OpKind::Control)
			{
				for (u32 i = 0; i < inst.arg_count(); ++i)
				{
					const u32 id = inst.args[i];
					if (id == 0 || id >= fn.value_types.size())
						return Fail(error, std::string(OpName(inst.op)) + ": operand is not an SSA value");
					if (id >= defined.size() || !defined[id])
						return Fail(error, std::string(OpName(inst.op)) + ": operand used before definition");
				}
			}

			if (inst.op == Op::Clz && !IsIntegerType(inst.type))
				return Fail(error, "CLZ requires an integer result");
			if (inst.op == Op::ReadHi || inst.op == Op::ReadLo || inst.op == Op::WriteHi || inst.op == Op::WriteLo)
			{
				const Type type = info.kind == OpKind::StateRead ? inst.type : fn.ValueType(inst.args[0]);
				if (!IsIntegerType(type) && (type != Type::V4U32 || inst.imm != 0))
					return Fail(error, "HI/LO requires scalar integer or full V4U32 at bank zero");
			}

			if (IsPackedIntegerOp(inst.op) || IsPackedLanePermutationOp(inst.op))
			{
				if (inst.imm != 0 && inst.imm != 8 && inst.imm != 16 && (inst.imm != 32 || inst.op == Op::VPackSatS))
					return Fail(error, "packed integer: lane width must be 8, 16 or 32 bits");
				const Type operand = fn.ValueType(inst.args[0]);
				if (IsPackedIntegerOp(inst.op) && operand != Type::V4U32 && operand != Type::V4I32)
					return Fail(error, "packed integer: integer vector operand required");
			}

			// Result/type rules.
			if (info.result == Type::Void)
			{
				if (inst.value != 0)
					return Fail(error, std::string(OpName(inst.op)) + ": unexpected result value");
			}
			else
			{
				if (inst.value == 0 || inst.value >= fn.value_types.size())
					return Fail(error, std::string(OpName(inst.op)) + ": missing result value");

				const Type declared = fn.ValueType(inst.value);
				if (declared != inst.type)
					return Fail(error, std::string(OpName(inst.op)) + ": result type does not match the instruction");

				if (info.result != Type::Any && inst.type != info.result)
					return Fail(error, std::string(OpName(inst.op)) + ": wrong result type");

				if (info.result == Type::Any)
				{
					if (inst.op == Op::Undef)
					{
						if (inst.type == Type::Void || inst.type == Type::Any)
							return Fail(error, "Undef: bad type");
					}
					else if (inst.op == Op::ConstVec)
					{
						if (inst.type != Type::V4U32)
							return Fail(error, "ConstVec: bad type");
					}
					else if (inst.op == Op::Select)
					{
						if (!CheckOperandType(fn, inst, 0, Type::I32, error) ||
							!CheckOperandType(fn, inst, 1, inst.type, error) ||
							!CheckOperandType(fn, inst, 2, inst.type, error))
							return false;
					}
					else if (inst.arg_count() > 0)
					{
						const Type operand_type = fn.ValueType(inst.args[0]);
						if (operand_type != inst.type)
							return Fail(error, std::string(OpName(inst.op)) + ": result type must match the first operand");
					}
				}
			}

			// Kind-specific operand checks.
			switch (info.kind)
			{
				case OpKind::Binary:
				{
					const Type t0 = fn.ValueType(inst.args[0]);
					const Type t1 = fn.ValueType(inst.args[1]);
					if (t0 != t1)
						return Fail(error, std::string(OpName(inst.op)) + ": operands have different types");
					break;
				}
				case OpKind::Ternary:
				{
					const Type t = fn.ValueType(inst.args[0]);
					if (!IsIntegerType(t) || fn.ValueType(inst.args[1]) != t || fn.ValueType(inst.args[2]) != t)
						return Fail(error, std::string(OpName(inst.op)) + ": matching integer operands required");
					break;
				}
				case OpKind::Compare:
				{
					const Type t0 = fn.ValueType(inst.args[0]);
					const Type t1 = fn.ValueType(inst.args[1]);
					if (t0 != t1 || !IsIntegerType(t0))
						return Fail(error, std::string(OpName(inst.op)) + ": integer operands required");
					if (inst.type != Type::I32)
						return Fail(error, std::string(OpName(inst.op)) + ": compare result must be i32");
					break;
				}
				case OpKind::Shift:
				{
					const Type t0 = fn.ValueType(inst.args[0]);
					const Type t1 = fn.ValueType(inst.args[1]);
					if (!IsIntegerType(t1))
						return Fail(error, std::string(OpName(inst.op)) + ": shift amount must be integer");
					if ((inst.op == Op::VShl || inst.op == Op::VShrU || inst.op == Op::VShrS) &&
						t0 != Type::V4U32 && t0 != Type::V4I32)
						return Fail(error, std::string(OpName(inst.op)) + ": integer vector operand required");
					if (inst.type != t0)
						return Fail(error, std::string(OpName(inst.op)) + ": shift result must match the value type");
					break;
				}
				case OpKind::MemLoad:
				{
					const Type addr = fn.ValueType(inst.args[0]);
					if (!IsIntegerType(addr))
						return Fail(error, std::string(OpName(inst.op)) + ": address must be integer");
					break;
				}
				case OpKind::MemStore:
				{
					const Type addr = fn.ValueType(inst.args[0]);
					if (!IsIntegerType(addr))
						return Fail(error, std::string(OpName(inst.op)) + ": address must be integer");
					if (inst.args[1] == 0)
						return Fail(error, std::string(OpName(inst.op)) + ": missing store value");
					break;
				}
				case OpKind::StateWrite:
				{
					if (inst.op == Op::WriteVuClip && (inst.type != Type::Void || fn.ValueType(inst.args[0]) != Type::I32))
						return Fail(error, "VU CLIP history: I32 write required");
					if (inst.op == Op::WriteCp0 && fn.ValueType(inst.args[0]) != Type::I32)
						return Fail(error, "COP0: 32-bit write required");
					if ((inst.op == Op::WriteCp2Data || inst.op == Op::WriteCp2Control) &&
						fn.ValueType(inst.args[0]) != Type::I32)
						return Fail(error, "COP2: 32-bit write required");
					if ((inst.op == Op::WriteFprBits || inst.op == Op::WriteFcr31) &&
						fn.ValueType(inst.args[0]) != Type::I32)
						return Fail(error, "COP1 raw state: 32-bit write required");
					if (inst.op == Op::WriteVfLaneSplatBits && fn.ValueType(inst.args[0]) != Type::I32)
						return Fail(error, "VU raw lane write: 32-bit write required");
					if (inst.op == Op::WriteVfMasked && fn.ValueType(inst.args[0]) != Type::V4F32)
						return Fail(error, "VU masked VF write: V4F32 value required");
					if (inst.op == Op::WriteVfBitsMasked && fn.ValueType(inst.args[0]) != Type::V4I32)
						return Fail(error, "VU masked VF bits write: V4I32 value required");
					if (inst.args[0] == 0)
						return Fail(error, std::string(OpName(inst.op)) + ": missing value");
					break;
				}
				case OpKind::FpuUnary:
					if (!CheckOperandType(fn, inst, 0, Type::F32, error))
						return false;
					break;
				case OpKind::FpuBinary:
				case OpKind::FpuCompare:
					if (!CheckOperandType(fn, inst, 0, Type::F32, error) ||
						!CheckOperandType(fn, inst, 1, Type::F32, error))
						return false;
					break;
				case OpKind::FpuTernary:
					if (!CheckOperandType(fn, inst, 0, Type::F32, error) ||
						!CheckOperandType(fn, inst, 1, Type::F32, error) ||
						!CheckOperandType(fn, inst, 2, Type::F32, error))
						return false;
					break;
				case OpKind::FpuConvert:
					if (inst.op == Op::F2I)
					{
						if (!CheckOperandType(fn, inst, 0, Type::F32, error))
							return false;
					}
					else if (!CheckOperandType(fn, inst, 0, Type::I32, error))
					{
						return false;
					}
					break;
				case OpKind::VecClip:
					if (fn.ValueType(inst.args[0]) != Type::V4I32 || fn.ValueType(inst.args[1]) != Type::V4I32)
						return Fail(error, "VClip: raw V4I32 sources required");
					break;
				case OpKind::VecUnary:
				{
					const Type t0 = fn.ValueType(inst.args[0]);
					if (!IsVectorType(t0) || inst.type != t0)
						return Fail(error, std::string(OpName(inst.op)) + ": vector operand required");
					break;
				}
				case OpKind::VecBinary:
				case OpKind::VecCompare:
				{
					const Type t0 = fn.ValueType(inst.args[0]);
					const Type t1 = fn.ValueType(inst.args[1]);
					if (!IsVectorType(t0) || t0 != t1)
						return Fail(error, std::string(OpName(inst.op)) + ": matching vector operands required");
					if (info.kind == OpKind::VecCompare && !IsVectorType(inst.type))
						return Fail(error, std::string(OpName(inst.op)) + ": vector compare result must be a vector");
					break;
				}
				case OpKind::VecTernary:
				{
					const Type t0 = fn.ValueType(inst.args[0]);
					if (!IsVectorType(t0) || fn.ValueType(inst.args[1]) != t0 || fn.ValueType(inst.args[2]) != t0)
						return Fail(error, std::string(OpName(inst.op)) + ": matching vector operands required");
					break;
				}
				case OpKind::VecConvert:
				{
					const Type t0 = fn.ValueType(inst.args[0]);
					if (!IsVectorType(t0))
						return Fail(error, std::string(OpName(inst.op)) + ": vector operand required");
					if (inst.op == Op::VI2F0 || inst.op == Op::VI2F4 ||
						inst.op == Op::VI2F12 || inst.op == Op::VI2F15)
					{
						if (t0 != Type::V4I32 || inst.type != Type::V4F32 || inst.imm != 0)
							return Fail(error, std::string(OpName(inst.op)) + ": V4I32 input, V4F32 result, and zero immediate required");
					}
					else if (inst.op == Op::VF2I0 || inst.op == Op::VF2I4 || inst.op == Op::VF2I12 || inst.op == Op::VF2I15)
					{
						if (t0 != Type::V4F32 || inst.type != Type::V4I32 || inst.imm != 0)
							return Fail(error, std::string(OpName(inst.op)) + ": V4F32 input, V4I32 result, and zero immediate required");
					}
					break;
				}
				case OpKind::VecSplat:
					if (fn.ValueType(inst.args[0]) != Type::I32 || inst.type != Type::V4I32 || inst.imm != 0)
						return Fail(error, "VSplat32: I32 input, V4I32 result, and zero immediate required");
					break;
				case OpKind::VecLane:
				{
					const Type t0 = fn.ValueType(inst.args[0]);
					if (!IsVectorType(t0) || inst.type != t0)
						return Fail(error, std::string(OpName(inst.op)) + ": vector operand required");
					break;
				}
				case OpKind::VecLane2:
				{
					const Type t0 = fn.ValueType(inst.args[0]);
					if (!IsVectorType(t0) || fn.ValueType(inst.args[1]) != t0 || inst.type != t0)
						return Fail(error, std::string(OpName(inst.op)) + ": matching vector operands required");
					break;
				}
				case OpKind::Helper:
				{
					for (u32 i = 0; i < inst.arg_count(); ++i)
					{
						if (inst.args[i] == 0)
							return Fail(error, "CallHelper: missing operand");
					}
					break;
				}
				default:
					break;
			}

			return true;
		}
	} // namespace

	bool Verify(const Function& fn, std::string* error)
	{
		if (error)
			error->clear();

		if (fn.blocks.empty())
			return Fail(error, "function has no blocks");
		if (fn.entry == 0 || fn.entry > fn.blocks.size())
			return Fail(error, "invalid entry block");
		if (fn.value_types.empty() || fn.value_types[0] != Type::Void)
			return Fail(error, "value type table must start with Void");

		// Value ids are allocated densely and uniquely.
		std::vector<bool> seen_value(fn.value_types.size(), false);
		for (size_t i = 1; i < fn.value_types.size(); ++i)
		{
			if (fn.value_types[i] == Type::Void || fn.value_types[i] == Type::Any)
				return Fail(error, "value with void/any type");
		}
		(void)seen_value;

		for (const Block& block : fn.blocks)
		{
			if (block.id == 0)
				return Fail(error, "block with id 0");
			if (block.insts.empty())
				return Fail(error, "empty block");
			if (!IsTerminator(block.insts.back().op))
				return Fail(error, "block does not end in a terminator");

			// Def-before-use scan. Values are block local.
			std::vector<bool> defined(fn.value_types.size(), false);
			for (u32 i = 0; i < block.insts.size(); ++i)
			{
				const Inst& inst = block.insts[i];
				if (!VerifyInstruction(fn, block, inst, i, defined, error))
					return false;

				if (inst.value != 0)
				{
					if (inst.value >= defined.size())
						return Fail(error, "value id out of range");
					if (defined[inst.value])
						return Fail(error, "value defined twice");
					defined[inst.value] = true;
				}

				if (Info(inst.op).kind == OpKind::Control && i + 1 != block.insts.size())
					return Fail(error, "instruction after terminator");
			}

			// CFG consistency.
			std::vector<u32> expected;
			const Inst& term = block.insts.back();
			switch (term.op)
			{
				case Op::Jump:
					expected.push_back(term.args[0]);
					break;
				case Op::Branch:
					expected.push_back(term.args[1]);
					expected.push_back(term.args[2]);
					break;
				case Op::CheckEvents:
					expected.push_back(term.args[0]);
					break;
				default:
					break;
			}

			for (const u32 target : expected)
			{
				if (target == 0 || target > fn.blocks.size())
					return Fail(error, "terminator target out of range");
			}

			if (expected.size() != block.successors.size())
				return Fail(error, "successor list does not match the terminator");

			for (const u32 target : expected)
			{
				if (std::find(block.successors.begin(), block.successors.end(), target) == block.successors.end())
					return Fail(error, "missing successor");

				const Block* succ = fn.GetBlock(target);
				if (!succ)
					return Fail(error, "missing successor block");

				if (std::find(succ->predecessors.begin(), succ->predecessors.end(), block.id) == succ->predecessors.end())
					return Fail(error, "missing reciprocal predecessor");
			}
		}

		return true;
	}

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	bool RunSelfTests()
	{
		Function fn;
		fn.guest_entry = 0x00100000;
		Builder builder(fn);

		const u32 b1 = builder.CreateBlock(0x00100000);
		const u32 five = builder.ConstI32(5);
		const u32 gpr = builder.Emit0(Op::ReadGpr, Type::I32);
		const u32 sum = builder.Emit2(Op::Add, Type::I32, five, gpr);
		builder.Emit1(Op::WriteGpr, Type::Void, sum, 4);
		const u32 cond = builder.Emit2(Op::CmpLtS, Type::I32, sum, five);

		const u32 b2 = builder.CreateBlock(0x00100004);
		const u32 b3 = builder.CreateBlock(0x00100008);

		builder.SetBlock(b1);
		builder.Branch(cond, b3, b2, 0x00100008);
		builder.SetBlock(b2);
		builder.Jump(b1, 0x00100000);
		builder.SetBlock(b3);
		builder.Return();

		std::string error;
		if (!Verify(fn, &error))
			return false;

		const std::string dump = Dump(fn);
		if (dump.find("ConstI32") == std::string::npos || dump.find("CmpLtS") == std::string::npos ||
			dump.find("Branch") == std::string::npos)
			return false;

		// A use-before-def must be rejected.
		{
			Function broken = fn;
			broken.blocks[0].insts[2].args[0] = 9999;
			if (Verify(broken, &error))
				return false;
		}

		// An instruction after the terminator must be rejected.
		{
			Function broken = fn;
			Inst extra;
			extra.op = Op::Nop;
			broken.blocks[0].insts.push_back(extra);
			if (Verify(broken, &error))
				return false;
		}

		// A result type that disagrees with the operand type must be rejected.
		{
			Function broken = fn;
			Inst& add = broken.blocks[0].insts[2];
			add.type = Type::F32;
			broken.value_types[add.value] = Type::F32;
			if (Verify(broken, &error))
				return false;
		}

		return true;
	}
#endif
} // namespace ir
