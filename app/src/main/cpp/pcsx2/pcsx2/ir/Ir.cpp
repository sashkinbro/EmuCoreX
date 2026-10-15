// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+

#include "ir/Ir.h"

#include <cstring>
#include <optional>
#include <algorithm>

namespace ir
{
	const char* TypeName(Type type)
	{
		switch (type)
		{
			case Type::Void: return "void";
			case Type::I32: return "i32";
			case Type::I64: return "i64";
			case Type::F32: return "f32";
			case Type::V4F32: return "v4f32";
			case Type::V4I32: return "v4i32";
			case Type::V4U32: return "v4u32";
			case Type::Flags: return "flags";
			case Type::Any: return "any";
		}
		return "?";
	}

	bool IsVectorType(Type type)
	{
		return type == Type::V4F32 || type == Type::V4I32 || type == Type::V4U32;
	}

	bool IsIntegerType(Type type)
	{
		return type == Type::I32 || type == Type::I64;
	}

	u32 TypeSize(Type type)
	{
		switch (type)
		{
			case Type::Void: return 0;
			case Type::I32: return 4;
			case Type::I64: return 8;
			case Type::F32: return 4;
			case Type::Flags: return 4;
			case Type::V4F32:
			case Type::V4I32:
			case Type::V4U32: return 16;
			case Type::Any: return 0;
		}
		return 0;
	}

	const char* OpName(Op op)
	{
		static constexpr const char* names[] = {
#define IR_OP_NAME(name, kind, min_args, max_args, result, term) #name,
			IR_OP_LIST(IR_OP_NAME)
#undef IR_OP_NAME
		};
		const size_t index = static_cast<size_t>(op);
		return (index < std::size(names)) ? names[index] : "?";
	}

	// ------------------------------------------------------------------
	// Builder
	// ------------------------------------------------------------------
	u32 Builder::AddValue(Type type)
	{
		const u32 id = m_fn->next_value++;
		m_fn->value_types.push_back(type);
		return id;
	}

	void Builder::AddPredecessor(u32 block_id, u32 pred_id)
	{
		Block* block = m_fn->GetBlock(block_id);
		if (!block)
			return;

		for (const u32 existing : block->predecessors)
		{
			if (existing == pred_id)
				return;
		}
		block->predecessors.push_back(pred_id);
	}

	void Builder::AttachSuccessors(Block& block, const Inst& term)
	{
		auto add_successor = [&](u32 target) {
			if (target == 0)
				return;

			for (const u32 existing : block.successors)
			{
				if (existing == target)
					return;
			}
			block.successors.push_back(target);
			AddPredecessor(target, block.id);
		};

		switch (term.op)
		{
			case Op::Jump:
				add_successor(term.args[0]);
				break;
			case Op::Branch:
				add_successor(term.args[1]);
				add_successor(term.args[2]);
				break;
			case Op::CheckEvents:
				add_successor(term.args[0]);
				break;
			default:
				break;
		}
	}

	u32 Builder::CreateBlock(u32 guest_start)
	{
		Block block;
		block.id = static_cast<u32>(m_fn->blocks.size()) + 1;
		block.guest_start = guest_start;
		block.guest_end = guest_start;
		m_fn->blocks.push_back(std::move(block));
		if (m_fn->entry == 0)
			m_fn->entry = static_cast<u32>(m_fn->blocks.size());
		m_block = static_cast<u32>(m_fn->blocks.size());
		m_pc = 0;
		return m_block;
	}

	void Builder::SetBlockEnd(u32 guest_end)
	{
		if (m_block == 0)
			return;
		m_fn->blocks[m_block - 1].guest_end = guest_end;
	}

	u32 Builder::Emit(Op op, Type type, std::initializer_list<u32> args, u64 imm, u16 aux, u32 guest_pc)
	{
		const OpInfo& info = Info(op);
		if (m_block == 0 || args.size() > 4)
		{
			// Malformed construction; the verifier reports it on the caller's
			// behalf, but keep the builder safe.
			return 0;
		}

		Block& block = m_fn->blocks[m_block - 1];
		if (!block.insts.empty() && IsTerminator(block.insts.back().op))
		{
			// Nothing may follow a terminator.
			return 0;
		}

		Inst inst;
		inst.op = op;
		inst.type = type;
		inst.imm = imm;
		inst.aux = aux;
		if (m_delay_slot)
			inst.aux |= IF_DELAY_SLOT;
		inst.guest_pc = guest_pc ? guest_pc : (m_pc ? m_pc : block.guest_start);
		inst.num_args = static_cast<u8>(args.size());
		u32 index = 0;
		for (const u32 arg : args)
			inst.args[index++] = arg;

		if (info.result != Type::Void && !info.terminator)
			inst.value = AddValue(type);

		if (info.terminator)
			AttachSuccessors(block, inst);

		block.insts.push_back(inst);
		if (info.terminator)
			block.guest_end = inst.guest_pc + 4;

		return inst.value;
	}

	u32 Builder::Emit0(Op op, Type type)
	{
		return Emit(op, type, {});
	}

	u32 Builder::Emit1(Op op, Type type, u32 a, u64 imm, u16 aux)
	{
		return Emit(op, type, {a}, imm, aux);
	}

	u32 Builder::Emit2(Op op, Type type, u32 a, u32 b, u64 imm, u16 aux)
	{
		return Emit(op, type, {a, b}, imm, aux);
	}

	u32 Builder::Emit3(Op op, Type type, u32 a, u32 b, u32 c, u64 imm, u16 aux)
	{
		return Emit(op, type, {a, b, c}, imm, aux);
	}

	u32 Builder::Emit4(Op op, Type type, u32 a, u32 b, u32 c, u32 d, u64 imm, u16 aux)
	{
		return Emit(op, type, {a, b, c, d}, imm, aux);
	}

	u32 Builder::ConstI32(u32 v)
	{
		return Emit(Op::ConstI32, Type::I32, {}, v);
	}

	u32 Builder::ConstI64(u64 v)
	{
		return Emit(Op::ConstI64, Type::I64, {}, v);
	}

	u32 Builder::ConstF32(float v)
	{
		u32 bits;
		std::memcpy(&bits, &v, sizeof(bits));
		return Emit(Op::ConstF32, Type::F32, {}, bits);
	}

	u32 Builder::ConstVec(const std::array<u32, 4>& lanes)
	{
		m_fn->vec_consts.push_back(lanes);
		return Emit(Op::ConstVec, Type::V4U32, {}, m_fn->vec_consts.size() - 1);
	}

	u32 Builder::Undef(Type type)
	{
		return Emit(Op::Undef, type, {});
	}

	u32 Builder::Jump(u32 target, u32 guest_target)
	{
		return Emit(Op::Jump, Type::Void, {target}, guest_target);
	}

	u32 Builder::Branch(u32 cond, u32 taken, u32 not_taken, u32 guest_target)
	{
		return Emit(Op::Branch, Type::Void, {cond, taken, not_taken}, guest_target);
	}

	u32 Builder::BranchIndirect(u32 address)
	{
		return Emit(Op::BranchIndirect, Type::Void, {address});
	}

	u32 Builder::CheckEvents(u32 fallthrough_block)
	{
		return Emit(Op::CheckEvents, Type::Void, {fallthrough_block});
	}

	u32 Builder::Resume(u32 guest_pc)
	{
		return Emit(Op::Resume, Type::Void, {}, guest_pc);
	}

	u32 Builder::Return()
	{
		return Emit(Op::Return, Type::Void, {});
	}

	u32 Builder::Trap()
	{
		return Emit(Op::Trap, Type::Void, {});
	}

	bool IsPackedLanePermutationOp(Op op)
	{
		return op == Op::VZipLo || op == Op::VZipHi || op == Op::VPackEven || op == Op::VInterleaveEven || op == Op::VInterleaveOdd;
	}

	bool IsPackedIntegerOp(Op op)
	{
		switch (op)
		{
			case Op::VAddI: case Op::VSubI: case Op::VAbsSatS:
			case Op::VAddSatS: case Op::VAddSatU: case Op::VSubSatS: case Op::VSubSatU:
			case Op::VShl: case Op::VShrU: case Op::VShrS:
			case Op::VMulWidenLoS: case Op::VMulWidenHiS: case Op::VMulWidenLoU: case Op::VMulWidenHiU:
			case Op::VShlEach: case Op::VShrUEach: case Op::VShrSEach: case Op::VPackSatS:
			case Op::VMinS: case Op::VMinU: case Op::VMaxS: case Op::VMaxU:
			case Op::VCmpEq: case Op::VCmpNe: case Op::VCmpLtS: case Op::VCmpLtU:
			case Op::VCmpLeS: case Op::VCmpLeU:
				return true;
			default: return false;
		}
	}

	u32 ValueOperandCount(const Inst& inst)
	{
		if (Info(inst.op).kind == OpKind::Control)
			return (inst.op == Op::Branch || inst.op == Op::BranchIndirect) ? 1u : 0u;
		return inst.arg_count();
	}

	namespace
	{
		bool RemovableValue(const Inst& inst)
		{
			if (!inst.value)
				return false;
			if (IsVectorType(inst.type))
				return inst.op == Op::ConstVec || inst.op == Op::Copy || inst.op == Op::ReadGpr ||
					inst.op == Op::ReadHi || inst.op == Op::ReadLo ||
					(inst.op == Op::VSplat32 && inst.type == Type::V4I32) ||
					(inst.type == Type::V4U32 && (inst.op == Op::Add || inst.op == Op::Sub ||
						inst.op == Op::And || inst.op == Op::Or || inst.op == Op::Xor || inst.op == Op::Not ||
						IsPackedIntegerOp(inst.op) || IsPackedLanePermutationOp(inst.op) || inst.op == Op::VShuffleBytes ||
						inst.op == Op::VShuffle || inst.op == Op::VShuffle2));
			if (!IsIntegerType(inst.type))
				return false;
			switch (inst.op)
			{
				case Op::ConstI32: case Op::ConstI64: case Op::Copy:
				case Op::Add: case Op::Sub: case Op::Mul: case Op::Msub:
				case Op::MulHiS: case Op::MulHiU:
				case Op::And: case Op::Or: case Op::Xor: case Op::Not: case Op::Neg: case Op::Clz:
				case Op::Shl: case Op::ShrU: case Op::ShrS:
				case Op::Sext8: case Op::Sext16: case Op::Zext8: case Op::Zext16:
				case Op::Sext32: case Op::Zext32: case Op::Trunc32:
				case Op::CmpEq: case Op::CmpNe: case Op::CmpLtS: case Op::CmpLtU:
				case Op::CmpLeS: case Op::CmpLeU: case Op::CmpGtS: case Op::CmpGtU:
				case Op::CmpGeS: case Op::CmpGeU: case Op::Select:
				case Op::ReadGpr: case Op::ReadHi: case Op::ReadLo: case Op::ReadCp0:
				case Op::ReadFprBits: case Op::ReadFcr31: case Op::ReadVuScalarBits:
					return true;
				default: return false;
			}
		}

		std::optional<u64> FoldInteger(const Function& fn, const Inst& inst,
			const std::vector<std::optional<u64>>& constants)
		{
			if (!inst.value || !IsIntegerType(inst.type))
				return std::nullopt;
			if (inst.op == Op::ConstI32 || inst.op == Op::ConstI64)
				return inst.imm;
			for (u32 i = 0; i < inst.arg_count(); ++i)
				if (!constants[inst.args[i]])
					return std::nullopt;
			if (!inst.arg_count())
				return std::nullopt;
			const u64 a = *constants[inst.args[0]];
			const u64 b = inst.arg_count() > 1 ? *constants[inst.args[1]] : 0;
			const u32 bits = fn.ValueType(inst.args[0]) == Type::I64 ? 64 : 32;
			const u64 mask = bits == 64 ? ~u64(0) : 0xffffffffull;
			const u32 shift = static_cast<u32>(b) & (bits - 1);
			const auto signed_value = [&](u64 value) {
				return bits == 64 ? static_cast<s64>(value) : static_cast<s64>(static_cast<s32>(value));
			};
			switch (inst.op)
			{
				case Op::Copy: return a;
				case Op::Add: return a + b;
				case Op::Sub: return a - b;
				case Op::Mul: return a * b;
				case Op::MulHiS:
					if (bits != 32 || inst.type != Type::I32) return std::nullopt;
					return static_cast<u64>(static_cast<s64>(static_cast<s32>(a)) * static_cast<s64>(static_cast<s32>(b))) >> 32;
				case Op::MulHiU:
					if (bits != 32 || inst.type != Type::I32) return std::nullopt;
					return (static_cast<u64>(static_cast<u32>(a)) * static_cast<u32>(b)) >> 32;
				case Op::Msub: return *constants[inst.args[2]] - a * b;
				case Op::And: return a & b;
				case Op::Or: return a | b;
				case Op::Xor: return a ^ b;
				case Op::Not: return ~a;
				case Op::Neg: return u64(0) - a;
				case Op::Clz:
				{
					u32 count = 0;
					for (u64 bit = u64(1) << (bits - 1); bit && !(a & bit); bit >>= 1)
						++count;
					return count;
				}
				case Op::Shl: return a << shift;
				case Op::ShrU: return (a & mask) >> shift;
				case Op::ShrS: return static_cast<u64>(signed_value(a) >> shift);
				case Op::Sext8: return static_cast<u64>(static_cast<s64>(static_cast<s8>(a)));
				case Op::Sext16: return static_cast<u64>(static_cast<s64>(static_cast<s16>(a)));
				case Op::Sext32: return static_cast<u64>(static_cast<s64>(static_cast<s32>(a)));
				case Op::Zext8: return a & 0xff;
				case Op::Zext16: return a & 0xffff;
				case Op::Zext32: case Op::Trunc32: return a & 0xffffffffull;
				case Op::CmpEq: return a == b;
				case Op::CmpNe: return a != b;
				case Op::CmpLtS: return signed_value(a) < signed_value(b);
				case Op::CmpLtU: return a < b;
				case Op::CmpLeS: return signed_value(a) <= signed_value(b);
				case Op::CmpLeU: return a <= b;
				case Op::CmpGtS: return signed_value(a) > signed_value(b);
				case Op::CmpGtU: return a > b;
				case Op::CmpGeS: return signed_value(a) >= signed_value(b);
				case Op::CmpGeU: return a >= b;
				case Op::Select: return a ? b : *constants[inst.args[2]];
				default: return std::nullopt;
			}
		}
	}

	void OptimizeIntegerValues(Function& fn)
	{
		for (Block& block : fn.blocks)
		{
			std::vector<u32> aliases(fn.value_types.size(), 0);
			std::vector<std::optional<u64>> constants(fn.value_types.size());
			for (Inst& inst : block.insts)
			{
				for (u32 i = 0; i < ValueOperandCount(inst); ++i)
					while (aliases[inst.args[i]])
						inst.args[i] = aliases[inst.args[i]];
				if (inst.op == Op::Copy)
					aliases[inst.value] = inst.args[0];
				if (auto folded = FoldInteger(fn, inst, constants))
				{
					const u64 value = inst.type == Type::I32 ? static_cast<u32>(*folded) : *folded;
					constants[inst.value] = value;
					inst.op = inst.type == Type::I32 ? Op::ConstI32 : Op::ConstI64;
					inst.imm = value;
					inst.num_args = 0;
				}
			}
			std::vector<bool> live(fn.value_types.size(), false);
			for (auto it = block.insts.rbegin(); it != block.insts.rend(); ++it)
			{
				if (RemovableValue(*it) && !live[it->value])
				{
					it->op = Op::Nop;
					it->type = Type::Void;
					it->value = 0;
					it->num_args = 0;
					continue;
				}
				for (u32 i = 0; i < ValueOperandCount(*it); ++i)
					live[it->args[i]] = true;
			}
			block.insts.erase(std::remove_if(block.insts.begin(), block.insts.end(), [](const Inst& inst) {
				return inst.op == Op::Nop;
			}), block.insts.end());
		}
	}
} // namespace ir
