// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+

#include "arm64/ee/EeIrLifter-arm64.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace EeIr
{
	namespace
	{
		inline u32 Op(u32 w) { return w >> 26; }
		inline u32 Rs(u32 w) { return (w >> 21) & 0x1f; }
		inline u32 Rt(u32 w) { return (w >> 16) & 0x1f; }
		inline u32 Rd(u32 w) { return (w >> 11) & 0x1f; }
		inline u32 Sa(u32 w) { return (w >> 6) & 0x1f; }
		inline u32 Funct(u32 w) { return w & 0x3f; }
		inline s32 Simm(u32 w) { return static_cast<s16>(w); }
		inline u32 Uimm(u32 w) { return w & 0xffff; }

		enum class Kind
		{
			Plain,
			Branch,       // conditional, delay slot executes on both paths
			BranchLikely, // conditional, delay slot only executes when taken
			Jump,         // j / jal
			JumpIndirect, // jr / jalr
			Trap,         // syscall / break
			Unsupported,
		};

		class Lifter
		{
		public:
			Lifter(const u32* code, const LiftOptions& options, ir::Function& fn)
				: m_code(code)
				, m_opts(options)
				, m_b(fn)
			{
			}

			bool Run(u32* end_pc, std::string* error);

		private:
			u32 ReadGpr(u32 index) { return index == 0 ? m_b.ConstI32(0) : m_b.Emit(ir::Op::ReadGpr, ir::Type::I32, {}, index); }
			u32 ReadGprWide(u32 index) { return index == 0 ? m_b.ConstI64(0) : m_b.Emit(ir::Op::ReadGpr, ir::Type::I64, {}, index); }
			void WriteGpr(u32 index, u32 value) { m_b.Emit(ir::Op::WriteGpr, ir::Type::Void, {value}, index); }
			// Full 64-bit write, no sign extension: jal/jalr link registers are
			// zero-extended on the R5900 (the interpreter stores a u32).
			void WriteGprWide(u32 index, u32 value) { m_b.Emit(ir::Op::WriteGpr, ir::Type::Void, {value}, index, ir::IF_WIDE_WRITE); }
			u32 ReadGprQuad(u32 index) { return index == 0 ? m_b.ConstVec({0, 0, 0, 0}) : m_b.Emit(ir::Op::ReadGpr, ir::Type::V4U32, {}, index); }
			u32 Const(u32 value) { return m_b.ConstI32(value); }
			u32 Bin(ir::Op op, u32 a, u32 b) { return m_b.Emit2(op, ir::Type::I32, a, b); }
			u32 Un(ir::Op op, u32 a) { return m_b.Emit1(op, ir::Type::I32, a); }
			u32 BinWide(ir::Op op, u32 a, u32 b) { return m_b.Emit2(op, ir::Type::I64, a, b); }
			void LiftMultiply(u32 rs, u32 rt, u32 rd, bool sign, u32 bank, bool accumulate);
			void LiftDivide(u32 rs, u32 rt, bool sign, u32 bank);

			bool Fail(std::string* error, const char* what, u32 pc)
			{
				if (error)
				{
					char buffer[96];
					std::snprintf(buffer, sizeof(buffer), "%s at 0x%08x", what, pc);
					*error = buffer;
				}
				return false;
			}

			Kind Classify(u32 word) const;
			u32 Addr(u32 rs, s32 offset);
			bool LiftPlain(u32 word, u32 pc, std::string* error);
			bool LiftSpecial(u32 word, u32 pc, std::string* error);
			bool LiftMergeMemory(u32 word);
			bool LiftPackedHiLo(u32 word);
			bool LiftRegimmBranch(u32 word, u32 pc, u32* cond, bool* link, std::string* error);

			// Lifts the delay slot into the current block. Fails when the slot
			// is itself a control instruction (not modelled yet).
			bool LiftDelaySlot(u32 delay_pc, std::string* error);

			const u32* m_code;
			LiftOptions m_opts;
			ir::Builder m_b;
		};

		Kind Lifter::Classify(u32 word) const
		{
			const u32 op = Op(word);
			switch (op)
			{
				case 0x00:
				{
					switch (Funct(word))
					{
						case 0x08:
						case 0x09:
							return Kind::JumpIndirect;
						case 0x0C:
						case 0x0D:
							return Kind::Trap;
						default:
							return Kind::Plain;
					}
				}
				case 0x01:
				{
					switch (Rt(word))
					{
						case 0x00: // bltz
						case 0x01: // bgez
						case 0x10: // bltzal
						case 0x11: // bgezal
							return Kind::Branch;
						case 0x02: // bltzl
						case 0x03: // bgezl
						case 0x12: // bltzall
						case 0x13: // bgezall
							return Kind::BranchLikely;
						default:
							return Kind::Unsupported;
					}
				}
				case 0x02:
				case 0x03:
					return Kind::Jump;
				case 0x04:
				case 0x05:
				case 0x06:
				case 0x07:
					return Kind::Branch;
				case 0x11: // COP1 condition branches
					if (Rs(word) == 0x08)
					{
						switch (Rt(word))
						{
							case 0: // bc1f
							case 1: // bc1t
							return Kind::Branch;
							case 2: // bc1fl
							case 3: // bc1tl
							return Kind::BranchLikely;
							default:
								return Kind::Unsupported;
						}
					}
					return Kind::Plain;
				case 0x14:
				case 0x15:
				case 0x16:
				case 0x17:
					return Kind::BranchLikely;
				default:
					return Kind::Plain;
			}
		}

		void Lifter::LiftMultiply(u32 rs, u32 rt, u32 rd, bool sign, u32 bank, bool accumulate)
		{
			const u32 a = ReadGpr(rs), b = ReadGpr(rt);
			u32 lo = Bin(ir::Op::Mul, a, b);
			u32 hi = Bin(sign ? ir::Op::MulHiS : ir::Op::MulHiU, a, b);
			if (accumulate)
			{
				const auto join = [&](u32 low, u32 high) {
					return BinWide(ir::Op::Or, m_b.Emit1(ir::Op::Zext32, ir::Type::I64, low),
						BinWide(ir::Op::Shl, m_b.Emit1(ir::Op::Zext32, ir::Type::I64, high), m_b.ConstI64(32)));
				};
				// Only the low word of each 64-bit HI/LO bank participates. Add
				// modulo 64 bits, then sign-extend both resulting words separately.
				const u32 old_lo = m_b.Emit(ir::Op::ReadLo, ir::Type::I32, {}, bank);
				const u32 old_hi = m_b.Emit(ir::Op::ReadHi, ir::Type::I32, {}, bank);
				const u32 sum = BinWide(ir::Op::Add, join(lo, hi), join(old_lo, old_hi));
				lo = m_b.Emit1(ir::Op::Trunc32, ir::Type::I32, sum);
				hi = m_b.Emit1(ir::Op::Trunc32, ir::Type::I32, BinWide(ir::Op::ShrU, sum, m_b.ConstI64(32)));
			}
			m_b.Emit1(ir::Op::WriteLo, ir::Type::Void, lo, bank);
			m_b.Emit1(ir::Op::WriteHi, ir::Type::Void, hi, bank);
			if (rd != 0)
				WriteGpr(rd, lo);
		}

		void Lifter::LiftDivide(u32 rs, u32 rt, bool sign, u32 bank)
		{
			const u32 a = ReadGpr(rs), b = ReadGpr(rt);
			const u32 quotient = Bin(sign ? ir::Op::DivS : ir::Op::DivU, a, b);
			m_b.Emit1(ir::Op::WriteLo, ir::Type::Void, quotient, bank);
			// Wrapping a-q*b also gives a for /0 and zero for INT_MIN/-1.
			const u32 remainder = m_b.Emit3(ir::Op::Msub, ir::Type::I32, quotient, b, a);
			m_b.Emit1(ir::Op::WriteHi, ir::Type::Void, remainder, bank);
		}

		bool Lifter::LiftSpecial(u32 word, u32 pc, std::string* error)
		{
			const u32 rs = Rs(word);
			const u32 rt = Rt(word);
			const u32 rd = Rd(word);
			const u32 sa = Sa(word);

			switch (Funct(word))
			{
				case 0x00: // sll
					WriteGpr(rd, Bin(ir::Op::Shl, ReadGpr(rt), Const(sa)));
					return true;
				case 0x02: // srl
					WriteGpr(rd, Bin(ir::Op::ShrU, ReadGpr(rt), Const(sa)));
					return true;
				case 0x03: // sra
					WriteGpr(rd, Bin(ir::Op::ShrS, ReadGpr(rt), Const(sa)));
					return true;
				case 0x04: // sllv
					WriteGpr(rd, Bin(ir::Op::Shl, ReadGpr(rt), ReadGpr(rs)));
					return true;
				case 0x06: // srlv
					WriteGpr(rd, Bin(ir::Op::ShrU, ReadGpr(rt), ReadGpr(rs)));
					return true;
				case 0x07: // srav
					WriteGpr(rd, Bin(ir::Op::ShrS, ReadGpr(rt), ReadGpr(rs)));
					return true;
				case 0x14: // 64-bit variable shift
					WriteGprWide(rd, BinWide(ir::Op::Shl, ReadGprWide(rt), ReadGprWide(rs)));
					return true;
				case 0x16: // 64-bit variable shift
					WriteGprWide(rd, BinWide(ir::Op::ShrU, ReadGprWide(rt), ReadGprWide(rs)));
					return true;
				case 0x17: // 64-bit variable shift
					WriteGprWide(rd, BinWide(ir::Op::ShrS, ReadGprWide(rt), ReadGprWide(rs)));
					return true;
				case 0x2D: // daddu
					WriteGprWide(rd, BinWide(ir::Op::Add, ReadGprWide(rs), ReadGprWide(rt)));
					return true;
				case 0x2F: // dsubu
					WriteGprWide(rd, BinWide(ir::Op::Sub, ReadGprWide(rs), ReadGprWide(rt)));
					return true;
				case 0x38: // 64-bit immediate shift
					WriteGprWide(rd, BinWide(ir::Op::Shl, ReadGprWide(rt), m_b.ConstI64(sa + 0)));
					return true;
				case 0x3A: // 64-bit immediate shift
					WriteGprWide(rd, BinWide(ir::Op::ShrU, ReadGprWide(rt), m_b.ConstI64(sa + 0)));
					return true;
				case 0x3B: // 64-bit immediate shift
					WriteGprWide(rd, BinWide(ir::Op::ShrS, ReadGprWide(rt), m_b.ConstI64(sa + 0)));
					return true;
				case 0x3C: // 64-bit immediate shift
					WriteGprWide(rd, BinWide(ir::Op::Shl, ReadGprWide(rt), m_b.ConstI64(sa + 32)));
					return true;
				case 0x3E: // 64-bit immediate shift
					WriteGprWide(rd, BinWide(ir::Op::ShrU, ReadGprWide(rt), m_b.ConstI64(sa + 32)));
					return true;
				case 0x3F: // 64-bit immediate shift
					WriteGprWide(rd, BinWide(ir::Op::ShrS, ReadGprWide(rt), m_b.ConstI64(sa + 32)));
					return true;
				case 0x0A: // movz
				{
					const u32 value = ReadGprWide(rs);
					const u32 old = ReadGprWide(rd);
					const u32 cond = Bin(ir::Op::CmpEq, ReadGprWide(rt), m_b.ConstI64(0));
					WriteGprWide(rd, m_b.Emit3(ir::Op::Select, ir::Type::I64, cond, value, old));
					return true;
				}
				case 0x0B: // movn
				{
					const u32 value = ReadGprWide(rs);
					const u32 old = ReadGprWide(rd);
					const u32 cond = Bin(ir::Op::CmpNe, ReadGprWide(rt), m_b.ConstI64(0));
					WriteGprWide(rd, m_b.Emit3(ir::Op::Select, ir::Type::I64, cond, value, old));
					return true;
				}
				case 0x0F: // sync
					m_b.Emit0(ir::Op::Nop);
					return true;
				case 0x10: // mfhi
					WriteGprWide(rd, m_b.Emit0(ir::Op::ReadHi, ir::Type::I64));
					return true;
				case 0x11: // mthi
					m_b.Emit1(ir::Op::WriteHi, ir::Type::Void, ReadGprWide(rs));
					return true;
				case 0x12: // mflo
					WriteGprWide(rd, m_b.Emit0(ir::Op::ReadLo, ir::Type::I64));
					return true;
				case 0x13: // mtlo
					m_b.Emit1(ir::Op::WriteLo, ir::Type::Void, ReadGprWide(rs));
					return true;
				case 0x18: // mult
				case 0x19: // multu
					LiftMultiply(rs, rt, rd, Funct(word) == 0x18, 0, false);
					return true;
				case 0x1A: // div
				case 0x1B: // divu
					LiftDivide(rs, rt, Funct(word) == 0x1a, 0);
					return true;
				case 0x20: // add (traps on overflow)
				case 0x22: // sub (traps on overflow)
					return Fail(error, "trapping arithmetic is not modelled yet", pc);
				case 0x21: // addu
					WriteGpr(rd, Bin(ir::Op::Add, ReadGpr(rs), ReadGpr(rt)));
					return true;
				case 0x23: // subu
					WriteGpr(rd, Bin(ir::Op::Sub, ReadGpr(rs), ReadGpr(rt)));
					return true;
				case 0x24: // and
					WriteGprWide(rd, BinWide(ir::Op::And, ReadGprWide(rs), ReadGprWide(rt)));
					return true;
				case 0x25: // or
					WriteGprWide(rd, BinWide(ir::Op::Or, ReadGprWide(rs), ReadGprWide(rt)));
					return true;
				case 0x26: // xor
					WriteGprWide(rd, BinWide(ir::Op::Xor, ReadGprWide(rs), ReadGprWide(rt)));
					return true;
				case 0x27: // nor
					WriteGprWide(rd, m_b.Emit1(ir::Op::Not, ir::Type::I64, BinWide(ir::Op::Or, ReadGprWide(rs), ReadGprWide(rt))));
					return true;
				case 0x2A: // slt
					WriteGpr(rd, Bin(ir::Op::CmpLtS, ReadGprWide(rs), ReadGprWide(rt)));
					return true;
				case 0x2B: // sltu
					WriteGpr(rd, Bin(ir::Op::CmpLtU, ReadGprWide(rs), ReadGprWide(rt)));
					return true;
				default:
					return Fail(error, "unsupported SPECIAL instruction", pc);
			}
		}

		bool Lifter::LiftRegimmBranch(u32 word, u32 pc, u32* cond, bool* link, std::string* error)
		{
			const u32 rs = Rs(word);
			const u32 value = ReadGprWide(rs);
			switch (Rt(word))
			{
				case 0x00: // bltz
				case 0x02: // bltzl
				case 0x10: // bltzal
				case 0x12: // bltzall
					*cond = Bin(ir::Op::CmpLtS, value, m_b.ConstI64(0));
					*link = (Rt(word) >= 0x10);
					return true;
				case 0x01: // bgez
				case 0x03: // bgezl
				case 0x11: // bgezal
				case 0x13: // bgezall
					*cond = Bin(ir::Op::CmpGeS, value, m_b.ConstI64(0));
					*link = (Rt(word) >= 0x10);
					return true;
				default:
					return Fail(error, "unsupported REGIMM instruction", pc);
			}
		}

		bool Lifter::LiftMergeMemory(u32 word)
		{
			const u32 op = Op(word), rt = Rt(word);
			const bool wide = op == 0x1a || op == 0x1b || op == 0x2c || op == 0x2d;
			const bool load = op == 0x1a || op == 0x1b || op == 0x22 || op == 0x26;
			const bool left = op == 0x1a || op == 0x22 || op == 0x2a || op == 0x2c;
			const u32 width = wide ? 8 : 4;
			const u32 address = Addr(Rs(word), Simm(word));
			const u32 byte = Bin(ir::Op::And, address, Const(width - 1));
			const u32 shift = Bin(ir::Op::Shl, byte, Const(3));
			const u32 complement = Bin(ir::Op::Sub, Const((width - 1) * 8), shift);
			const u32 aligned = Bin(ir::Op::And, address, Const(~(width - 1)));
			// Keep reads even for $0 and capture the address before base==target
			// merges. Stores are ordered read/modify/write through existing hooks.
			const u32 memory = m_b.Emit1(wide ? ir::Op::Load64 : ir::Op::Load32, wide ? ir::Type::I64 : ir::Type::I32, aligned);
			if (load && rt == 0) return true;
			const auto constant = [&](u64 value) { return wide ? m_b.ConstI64(value) : Const(static_cast<u32>(value)); };
			const auto bin = [&](ir::Op operation, u32 a, u32 b) { return wide ? BinWide(operation, a, b) : Bin(operation, a, b); };
			const u32 reg = wide ? ReadGprWide(rt) : ReadGpr(rt);
			const u64 low_mask = wide ? 0x00ffffffffffffffull : 0x00ffffff;
			const u64 high_mask = wide ? 0xffffffffffffff00ull : 0xffffff00;
			u32 value;
			if (load)
				value = left ? bin(ir::Op::Or, bin(ir::Op::And, reg, bin(ir::Op::ShrU, constant(low_mask), shift)),
					bin(ir::Op::Shl, memory, complement)) :
					bin(ir::Op::Or, bin(ir::Op::And, reg, bin(ir::Op::Shl, constant(high_mask), complement)),
						bin(ir::Op::ShrU, memory, shift));
			else
				value = left ? bin(ir::Op::Or, bin(ir::Op::ShrU, reg, complement),
					bin(ir::Op::And, memory, bin(ir::Op::Shl, constant(high_mask), shift))) :
					bin(ir::Op::Or, bin(ir::Op::Shl, reg, shift),
						bin(ir::Op::And, memory, bin(ir::Op::ShrU, constant(low_mask), complement)));
			if (!load)
				m_b.Emit2(wide ? ir::Op::Store64 : ir::Op::Store32, ir::Type::Void, aligned, value);
			else if (wide)
				WriteGprWide(rt, value);
			else if (left)
				WriteGpr(rt, value);
			else
			{
				// EE LWR sign extends only at alignment zero. Other alignments
				// overwrite the low word while preserving bits 63..32 (and 127..64).
				const u32 sign = m_b.Emit1(ir::Op::Sext32, ir::Type::I64, value);
				const u32 kept = BinWide(ir::Op::Or,
					BinWide(ir::Op::And, ReadGprWide(rt), m_b.ConstI64(0xffffffff00000000ull)),
					m_b.Emit1(ir::Op::Zext32, ir::Type::I64, value));
				const u32 zero = Bin(ir::Op::CmpEq, byte, Const(0));
				WriteGprWide(rt, m_b.Emit3(ir::Op::Select, ir::Type::I64, zero, sign, kept));
			}
			return true;
		}

		bool Lifter::LiftPackedHiLo(u32 word)
		{
			const u32 group = Funct(word), sub = Sa(word), rs = Rs(word), rt = Rt(word), rd = Rd(word);
			const auto read = [&](ir::Op op) { return m_b.Emit0(op, ir::Type::V4U32); };
			const auto write = [&](ir::Op op, u32 value) { m_b.Emit1(op, ir::Type::Void, value); };
			const auto binary = [&](ir::Op op, u32 a, u32 b, u32 bits = 0)
			{ return m_b.Emit2(op, ir::Type::V4U32, a, b, bits); };
			const auto shuffle = [&](u32 value, u32 lanes)
			{ return m_b.Emit1(ir::Op::VShuffle, ir::Type::V4U32, value, lanes); };
			const auto sign_extend_even = [&](u32 value, bool odd = false)
			{
				const u32 pair = shuffle(value, odd ? 0xdd : 0x88);
				return binary(ir::Op::VZipLo, pair, m_b.Emit2(ir::Op::VShrS, ir::Type::V4U32, pair, Const(31)), 32);
			};
			if ((group == 9 || group == 41) && (sub == 8 || sub == 9))
			{
				const ir::Op state = sub == 8 ? ir::Op::ReadHi : ir::Op::ReadLo;
				if (group == 9)
				{
					if (rd)
						m_b.Emit1(ir::Op::WriteGpr, ir::Type::Void, read(state), rd);
				}
				else
					write(sub == 8 ? ir::Op::WriteHi : ir::Op::WriteLo, ReadGprQuad(rs));
				return true;
			}
			if (group == 0x30) // PMFHL variants; reserved modes have no effect.
			{
				if (!rd || sub > 4)
					return true;
				const u32 lo = read(ir::Op::ReadLo), hi = read(ir::Op::ReadHi);
				u32 value;
				if (sub <= 1)
					value = binary(sub == 0 ? ir::Op::VInterleaveEven : ir::Op::VInterleaveOdd, lo, hi, 32);
				else if (sub == 2)
				{
					// A reconstructed signed 64-bit value fits I32 exactly when
					// its HI word equals the sign of its LO word.
					const u32 lo_sign = m_b.Emit2(ir::Op::VShrS, ir::Type::V4U32, lo, Const(31));
					const u32 hi_sign = m_b.Emit2(ir::Op::VShrS, ir::Type::V4U32, hi, Const(31));
					const u32 fits = binary(ir::Op::VCmpEq, hi, lo_sign, 32);
					const u32 limits =
						binary(ir::Op::Xor, hi_sign, m_b.ConstVec({0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff}));
					const u32 selected = binary(ir::Op::Or, binary(ir::Op::And, lo, fits),
												binary(ir::Op::And, limits, m_b.Emit1(ir::Op::Not, ir::Type::V4U32, fits)));
					value = sign_extend_even(selected);
				}
				else
					value = shuffle(binary(sub == 3 ? ir::Op::VPackEven : ir::Op::VPackSatS, lo, hi, 16), 0xd8);
				m_b.Emit1(ir::Op::WriteGpr, ir::Type::Void, value, rd);
				return true;
			}
			if (group == 0x31) // PMTHL.LW replaces only the even words.
			{
				if (sub != 0)
					return true;
				const u32 source = ReadGprQuad(rs), lo = read(ir::Op::ReadLo), hi = read(ir::Op::ReadHi);
				const u32 even = m_b.ConstVec({~u32(0), 0, ~u32(0), 0});
				const u32 odd = m_b.ConstVec({0, ~u32(0), 0, ~u32(0)});
				write(ir::Op::WriteLo, binary(ir::Op::Or, binary(ir::Op::And, lo, odd), binary(ir::Op::And, source, even)));
				write(ir::Op::WriteHi,
					  binary(ir::Op::Or, binary(ir::Op::And, hi, odd), binary(ir::Op::And, shuffle(source, 0xb1), even)));
				return true;
			}
			if ((group == 9 && (sub == 2 || sub == 3)) || (group == 41 && sub == 3))
			{
				if (rd)
				{
					const ir::Op op = group == 41 ? ir::Op::VShrSEach : sub == 2 ? ir::Op::VShlEach : ir::Op::VShrUEach;
					const u32 value = binary(op, ReadGprQuad(rt), ReadGprQuad(rs), 32);
					m_b.Emit1(ir::Op::WriteGpr, ir::Type::Void, sign_extend_even(value), rd);
				}
				return true;
			}
			if ((group == 9 || group == 41) && sub == 12)
			{
				const u32 lhs = shuffle(ReadGprQuad(rs), 0x88), rhs = shuffle(ReadGprQuad(rt), 0x88);
				const u32 value = binary(group == 9 ? ir::Op::VMulWidenLoS : ir::Op::VMulWidenLoU, lhs, rhs, 32);
				write(ir::Op::WriteLo, sign_extend_even(value));
				write(ir::Op::WriteHi, sign_extend_even(value, true));
				if (rd)
					m_b.Emit1(ir::Op::WriteGpr, ir::Type::Void, value, rd);
				return true;
			}
			if (group == 9 && (sub == 16 || sub == 17 || sub == 20 || sub == 21 || sub == 28))
			{
				const u32 lhs = ReadGprQuad(rs), rhs = ReadGprQuad(rt);
				const u32 lower = binary(ir::Op::VMulWidenLoS, lhs, rhs, 16);
				const u32 upper = binary(ir::Op::VMulWidenHiS, lhs, rhs, 16);
				u32 lo = m_b.Emit2(ir::Op::VShuffle2, ir::Type::V4U32, lower, upper, 0xb08);
				u32 hi = m_b.Emit2(ir::Op::VShuffle2, ir::Type::V4U32, lower, upper, 0xf9a);
				if (sub == 16 || sub == 20)
				{
					const ir::Op operation = sub == 16 ? ir::Op::VAddI : ir::Op::VSubI;
					lo = binary(operation, read(ir::Op::ReadLo), lo, 32);
					hi = binary(operation, read(ir::Op::ReadHi), hi, 32);
				}
				else if (sub == 17 || sub == 21)
				{
					const auto horizontal = [&](u32 product)
					{
						const u32 even = shuffle(product, 0xa0), odd = shuffle(product, 0xf5);
						const u32 result =
							sub == 17 ? binary(ir::Op::VAddI, odd, even, 32) : binary(ir::Op::VSubI, odd, even, 32);
						const u32 kept = sub == 17 ? odd : m_b.Emit1(ir::Op::Not, ir::Type::V4U32, odd);
						return m_b.Emit2(ir::Op::VShuffle2, ir::Type::V4U32, result, kept, 0xea8);
					};
					lo = horizontal(lo);
					hi = horizontal(hi);
				}
				write(ir::Op::WriteLo, lo);
				write(ir::Op::WriteHi, hi);
				if (rd)
					m_b.Emit1(ir::Op::WriteGpr, ir::Type::Void, binary(ir::Op::VInterleaveEven, lo, hi, 32), rd);
				return true;
			}
			return false;
		}


		bool Lifter::LiftPlain(u32 word, u32 pc, std::string* error)
		{
			if (word == 0)
			{
				m_b.Emit0(ir::Op::Nop);
				return true;
			}

			const u32 op = Op(word);
			if (op == 0x00)
				return LiftSpecial(word, pc, error);

			const u32 rs = Rs(word);
			const u32 rt = Rt(word);
			const s32 simm = Simm(word);
			const u32 uimm = Uimm(word);

			switch (op)
			{
				case 0x1a: case 0x1b: // LDL/LDR
				case 0x22: case 0x26: // LWL/LWR
				case 0x2a: case 0x2e: // SWL/SWR
				case 0x2c: case 0x2d: // SDL/SDR
					return LiftMergeMemory(word);
				case 0x09: // addiu
					WriteGpr(rt, Bin(ir::Op::Add, ReadGpr(rs), Const(static_cast<u32>(simm))));
					return true;
				case 0x0A: // slti
					WriteGpr(rt, Bin(ir::Op::CmpLtS, ReadGprWide(rs), m_b.ConstI64(static_cast<u64>(static_cast<s64>(simm)))));
					return true;
				case 0x0B: // sltiu
					WriteGpr(rt, Bin(ir::Op::CmpLtU, ReadGprWide(rs), m_b.ConstI64(static_cast<u64>(static_cast<s64>(simm)))));
					return true;
				case 0x0C: // andi
					WriteGprWide(rt, BinWide(ir::Op::And, ReadGprWide(rs), m_b.ConstI64(uimm)));
					return true;
				case 0x0D: // ori
					WriteGprWide(rt, BinWide(ir::Op::Or, ReadGprWide(rs), m_b.ConstI64(uimm)));
					return true;
				case 0x0E: // xori
					WriteGprWide(rt, BinWide(ir::Op::Xor, ReadGprWide(rs), m_b.ConstI64(uimm)));
					return true;
				case 0x0F: // lui
					WriteGpr(rt, Const(uimm << 16));
					return true;
				case 0x11: // COP1 raw transfers and selected raw S-format operations
				{
					const u32 format = rs;
					const u32 fs = Rd(word);
					if (format == 0x10)
					{
						const u32 funct = Funct(word);
						if (funct == 0x06) // mov.s: raw copy, flags are untouched
						{
							const u32 bits = m_b.Emit(ir::Op::ReadFprBits, ir::Type::I32, {}, fs);
							m_b.Emit1(ir::Op::WriteFprBits, ir::Type::Void, bits, Sa(word));
							return true;
						}
						if (funct == 0x05 || funct == 0x07) // abs.s / neg.s
						{
							const u32 source = m_b.Emit(ir::Op::ReadFprBits, ir::Type::I32, {}, fs);
							const u32 result = funct == 0x05 ?
								Bin(ir::Op::And, source, Const(0x7fffffffu)) :
								Bin(ir::Op::Xor, source, Const(0x80000000u));
							m_b.Emit1(ir::Op::WriteFprBits, ir::Type::Void, result, Sa(word));

							// The interpreter clears only the current O/U cause bits for
							// ABS.S and NEG.S. Sticky flags and all other FCR31 bits survive.
							const u32 fcr31 = m_b.Emit0(ir::Op::ReadFcr31, ir::Type::I32);
							m_b.Emit1(ir::Op::WriteFcr31, ir::Type::Void,
								Bin(ir::Op::And, fcr31, Const(~0x0000c000u)));
							return true;
						}
						if (funct == 0x28 || funct == 0x29) // max.s / min.s
						{
							// The EE orders these raw words as signed integers. When both
							// operands are negative, that order is reversed for MAX/MIN.
							const u32 lhs = m_b.Emit(ir::Op::ReadFprBits, ir::Type::I32, {}, fs);
							const u32 rhs = m_b.Emit(ir::Op::ReadFprBits, ir::Type::I32, {}, Rt(word));
							const u32 both_negative = Bin(ir::Op::CmpLtS,
								Bin(ir::Op::And, lhs, rhs), Const(0));
							const u32 lhs_less_raw = Bin(ir::Op::CmpLtS, lhs, rhs);
							const u32 lhs_less_real = Bin(ir::Op::Xor, lhs_less_raw, both_negative);
							const bool is_max = funct == 0x28;
							// Equality can toggle the predicate for negative words, but either
							// selected operand is then the same raw bit pattern.
							const u32 result = m_b.Emit3(ir::Op::Select, ir::Type::I32,
								lhs_less_real, is_max ? rhs : lhs, is_max ? lhs : rhs);
							m_b.Emit1(ir::Op::WriteFprBits, ir::Type::Void, result, Sa(word));

							// MAX.S and MIN.S clear current O/U causes only.
							const u32 fcr31 = m_b.Emit0(ir::Op::ReadFcr31, ir::Type::I32);
							m_b.Emit1(ir::Op::WriteFcr31, ir::Type::Void,
								Bin(ir::Op::And, fcr31, Const(~0x0000c000u)));
							return true;
						}
						return Fail(error, "unsupported COP1 S-format operation", pc);
					}
					if ((format != 0x00 && format != 0x02 && format != 0x04 && format != 0x06) ||
						(word & 0x7ffu) != 0)
						return Fail(error, "unsupported or malformed COP1 transfer", pc);

					switch (format)
					{
						case 0x00: // mfc1: raw word, signed extension to the EE's 64-bit GPR
							if (rt != 0)
							{
								const u32 bits = m_b.Emit(ir::Op::ReadFprBits, ir::Type::I32, {}, fs);
								WriteGprWide(rt, m_b.Emit1(ir::Op::Sext32, ir::Type::I64, bits));
							}
							return true;
						case 0x02: // cfc1: FCR0 is an architectural constant; only FCR31 is readable
							if (rt != 0)
							{
								u32 value;
								if (fs == 0)
									value = m_b.ConstI64(0x2e00);
								else if (fs == 31)
									value = m_b.Emit1(ir::Op::Sext32, ir::Type::I64,
										m_b.Emit0(ir::Op::ReadFcr31, ir::Type::I32));
								else
									value = m_b.ConstI64(0);
								WriteGprWide(rt, value);
							}
							return true;
						case 0x04: // mtc1: preserve the exact low 32 GPR bits
							m_b.Emit(ir::Op::WriteFprBits, ir::Type::Void, {ReadGpr(rt)}, fs);
							return true;
						case 0x06: // ctc1: only FCR31 is writable in this interpreter
							if (fs == 31)
								m_b.Emit1(ir::Op::WriteFcr31, ir::Type::Void, ReadGpr(rt));
							return true;
					}
					return false;
				}
				case 0x19: // daddiu
					WriteGprWide(rt, BinWide(ir::Op::Add, ReadGprWide(rs), m_b.ConstI64(static_cast<u64>(static_cast<s64>(simm)))));
					return true;
				case 0x1C: // MMI: packed arithmetic, shifts, permutations and bitwise operations
				{
					if (LiftPackedHiLo(word)) return true;
					const u32 group = word & 0x3f;
					const u32 sub = Sa(word);
					const u32 rd = Rd(word);
					switch (group)
					{
						case 0x00: case 0x01: // MADD/MADDU
						case 0x20: case 0x21: // MADD1/MADDU1
							LiftMultiply(rs, rt, rd, (group & 1) == 0, group >> 5, true);
							return true;
						case 0x18: case 0x19: // MULT1/MULTU1
							LiftMultiply(rs, rt, rd, group == 0x18, 1, false);
							return true;
						case 0x1a: case 0x1b: // DIV1/DIVU1
							LiftDivide(rs, rt, group == 0x1a, 1);
							return true;
						case 0x10: case 0x12: // MFHI1/MFLO1
							WriteGprWide(rd, m_b.Emit(group == 0x10 ? ir::Op::ReadHi : ir::Op::ReadLo, ir::Type::I64, {}, 1));
							return true;
						case 0x11: case 0x13: // MTHI1/MTLO1
							m_b.Emit1(group == 0x11 ? ir::Op::WriteHi : ir::Op::WriteLo, ir::Type::Void, ReadGprWide(rs), 1);
							return true;
						case 0x04: // PLZCW
						{
							if (rd == 0) return true;
							const u32 source = ReadGprWide(rs);
							const auto count = [&](u32 word) {
								const u32 normalized = Bin(ir::Op::Xor, word, Bin(ir::Op::ShrS, word, Const(31)));
								return m_b.Emit1(ir::Op::Zext32, ir::Type::I64, Bin(ir::Op::Sub, Un(ir::Op::Clz, normalized), Const(1)));
							};
							const u32 low = count(m_b.Emit1(ir::Op::Trunc32, ir::Type::I32, source));
							const u32 high = count(m_b.Emit1(ir::Op::Trunc32, ir::Type::I32,
								BinWide(ir::Op::ShrU, source, m_b.ConstI64(32))));
							WriteGprWide(rd, BinWide(ir::Op::Or, low, BinWide(ir::Op::Shl, high, m_b.ConstI64(32))));
							return true;
						}
					}
					if (group == 0x08 && (sub == 30 || sub == 31)) // pext5 / ppac5
					{
						if (rd != 0)
						{
							const u32 source = ReadGprQuad(rt);
							constexpr u32 masks[] = {0x1f, 0x3e0, 0x7c00, 0x8000};
							constexpr u32 shifts[] = {3, 6, 9, 16};
							u32 result = 0;
							for (u32 field = 0; field < 4; ++field)
							{
								const u32 mask = m_b.ConstVec({masks[field], masks[field], masks[field], masks[field]});
								u32 value = source;
								if (sub == 30) value = m_b.Emit2(ir::Op::And, ir::Type::V4U32, value, mask);
								value = m_b.Emit2(sub == 30 ? ir::Op::VShl : ir::Op::VShrU, ir::Type::V4U32, value, Const(shifts[field]));
								if (sub == 31) value = m_b.Emit2(ir::Op::And, ir::Type::V4U32, value, mask);
								result = result ? m_b.Emit2(ir::Op::Or, ir::Type::V4U32, result, value) : value;
							}
							m_b.Emit1(ir::Op::WriteGpr, ir::Type::Void, result, rd);
						}
						return true;
					}
					if (group == 0x28 && sub == 4) // padsbh: lower subtract, upper add
					{
						if (rd != 0)
						{
							const u32 lhs = ReadGprQuad(rs), rhs = ReadGprQuad(rt);
							const u32 low = m_b.Emit2(ir::Op::VSubI, ir::Type::V4U32, lhs, rhs, 16);
							const u32 high = m_b.Emit2(ir::Op::VAddI, ir::Type::V4U32, lhs, rhs, 16);
							const u32 value = m_b.Emit2(ir::Op::VShuffle2, ir::Type::V4U32, low, high, 0xf88);
							m_b.Emit1(ir::Op::WriteGpr, ir::Type::Void, value, rd);
						}
						return true;
					}
					u64 byte_selectors = 0;
					if (group == 0x09 && sub == 26) byte_selectors = 0xfe98badc76103254ull; // pexeh
					else if (group == 0x09 && sub == 27) byte_selectors = 0x98badcfe10325476ull; // prevh
					else if (group == 0x29 && sub == 26) byte_selectors = 0xfebadc9876325410ull; // pexch
					else if (group == 0x29 && sub == 27) byte_selectors = 0x9898989810101010ull; // pcpyh
					if (byte_selectors)
					{
						if (rd != 0)
							m_b.Emit1(ir::Op::WriteGpr, ir::Type::Void,
								m_b.Emit1(ir::Op::VShuffleBytes, ir::Type::V4U32, ReadGprQuad(rt), byte_selectors), rd);
						return true;
					}
					ir::Op permutation = ir::Op::Nop;
					u32 permutation_bits = 16;
					if (group == 0x08 && (sub == 22 || sub == 26)) permutation = ir::Op::VZipLo;
					else if (group == 0x08 && (sub == 23 || sub == 27)) permutation = ir::Op::VPackEven;
					else if (group == 0x28 && (sub == 22 || sub == 26)) permutation = ir::Op::VZipHi;
					else if (group == 0x09 && sub == 10) permutation = ir::Op::VZipLo; // pinth
					else if (group == 0x29 && sub == 10) permutation = ir::Op::VInterleaveEven; // pinteh
					if (permutation != ir::Op::Nop)
					{
						if (rd != 0)
						{
							if (sub == 26 || sub == 27) permutation_bits = 8;
							u32 second = ReadGprQuad(rs);
							if (group == 0x09 && sub == 10)
								second = m_b.Emit1(ir::Op::VShuffle, ir::Type::V4U32, second, 0x4e);
							const u32 value = m_b.Emit2(permutation, ir::Type::V4U32, ReadGprQuad(rt), second, permutation_bits);
							m_b.Emit1(ir::Op::WriteGpr, ir::Type::Void, value, rd);
						}
						return true;
					}
					if (group == 0x34 || group == 0x36 || group == 0x37 || group == 0x3c || group == 0x3e || group == 0x3f)
					{
						if (rd != 0)
						{
							const ir::Op shift = (group & 3) == 0 ? ir::Op::VShl : (group & 3) == 2 ? ir::Op::VShrU : ir::Op::VShrS;
							const u32 value = m_b.Emit2(shift, ir::Type::V4U32, ReadGprQuad(rt), Const(sub), group < 0x38 ? 16 : 32);
							m_b.Emit1(ir::Op::WriteGpr, ir::Type::Void, value, rd);
						}
						return true;
					}
					if (group == 0x28 && (sub == 1 || sub == 5)) // pabsw / pabsh
					{
						if (rd != 0)
						{
							const u32 value = m_b.Emit1(ir::Op::VAbsSatS, ir::Type::V4U32, ReadGprQuad(rt), sub == 1 ? 32 : 16);
							m_b.Emit1(ir::Op::WriteGpr, ir::Type::Void, value, rd);
						}
						return true;
					}
					u32 selectors = ~0u;
					ir::Op shuffle = ir::Op::VShuffle2;
					if (group == 0x08 && sub == 0x12) selectors = 0xa60; // pextlw: rt0,rs0,rt1,rs1
					else if (group == 0x28 && sub == 0x12) selectors = 0xef2; // pextuw: rt2,rs2,rt3,rs3
					else if (group == 0x08 && sub == 0x13) selectors = 0xd10; // ppacw: rt0,rt2,rs0,rs2
					else if (group == 0x09 && sub == 0x0e) selectors = 0xb08; // pcpyld: rt0,rt1,rs0,rs1
					else if (group == 0x29 && sub == 0x0e) selectors = 0x6be; // pcpyud: rs2,rs3,rt2,rt3
					else if (group == 0x09 && (sub == 0x1e || sub == 0x1f))
					{
						shuffle = ir::Op::VShuffle;
						selectors = sub == 0x1e ? 0xc6 : 0xc9; // pexew: 2,1,0,3 / prot3w: 1,2,0,3
					}
					else if (group == 0x29 && sub == 0x1e)
					{
						shuffle = ir::Op::VShuffle;
						selectors = 0xd8; // pexcw: 0,2,1,3
					}
					if (selectors != ~0u)
					{
						if (rd != 0)
						{
							const u32 rt_value = ReadGprQuad(rt);
							const u32 value = shuffle == ir::Op::VShuffle ?
								m_b.Emit1(shuffle, ir::Type::V4U32, rt_value, selectors) :
								m_b.Emit2(shuffle, ir::Type::V4U32, rt_value, ReadGprQuad(rs), selectors);
							m_b.Emit1(ir::Op::WriteGpr, ir::Type::Void, value, rd);
						}
						return true;
					}
					ir::Op operation;
					u32 lane_bits = 0;
					bool invert = false, swap = false;
					if (group == 0x08 && sub <= 3)
					{
						constexpr ir::Op word_ops[] = {ir::Op::Add, ir::Op::Sub, ir::Op::VCmpLtS, ir::Op::VMaxS};
						operation = word_ops[sub]; // paddw / psubw / pcgtw / pmaxw
						swap = sub == 2; // rs > rt is rt < rs
					}
					else if (group == 0x08 && sub >= 4 && sub <= 10)
					{
						constexpr ir::Op narrow_ops[] = {ir::Op::VAddI, ir::Op::VSubI, ir::Op::VCmpLtS, ir::Op::VMaxS};
						operation = narrow_ops[sub & 3];
						lane_bits = sub < 8 ? 16 : 8;
						swap = (sub & 3) == 2;
					}
					else if (group == 0x28 && (sub == 2 || sub == 3 || sub == 6 || sub == 7 || sub == 10))
					{
						operation = (sub & 3) == 2 ? ir::Op::VCmpEq : ir::Op::VMinS;
						lane_bits = sub < 4 ? 32 : sub < 8 ? 16 : 8;
					}
					else if ((group == 0x08 || group == 0x28) &&
						(sub == 16 || sub == 17 || sub == 20 || sub == 21 || sub == 24 || sub == 25))
					{
						operation = group == 0x08 ? (sub & 1 ? ir::Op::VSubSatS : ir::Op::VAddSatS) :
							(sub & 1 ? ir::Op::VSubSatU : ir::Op::VAddSatU);
						lane_bits = sub < 20 ? 32 : sub < 24 ? 16 : 8;
					}
					else if (group == 0x09 && (sub == 0x12 || sub == 0x13))
						operation = sub == 0x12 ? ir::Op::And : ir::Op::Xor; // pand / pxor
					else if (group == 0x29 && (sub == 0x12 || sub == 0x13))
					{
						operation = ir::Op::Or; // por / pnor
						invert = sub == 0x13;
					}
					else
						return Fail(error, "unsupported MMI instruction", pc);
					if (rd != 0)
					{
						u32 value = m_b.Emit2(operation, ir::Type::V4U32, ReadGprQuad(swap ? rt : rs), ReadGprQuad(swap ? rs : rt), lane_bits);
						if (invert)
							value = m_b.Emit1(ir::Op::Not, ir::Type::V4U32, value);
						m_b.Emit1(ir::Op::WriteGpr, ir::Type::Void, value, rd);
					}
					return true;
				}
				case 0x1E: // lq: EE silently aligns down, including loads to $0
					if (!m_opts.quad_memory)
						return Fail(error, "quad memory disabled", pc);
					m_b.Emit(ir::Op::WriteGpr, ir::Type::Void,
						{m_b.Emit1(ir::Op::Load128, ir::Type::V4U32, Bin(ir::Op::And, Addr(rs, simm), Const(~u32(15))))}, rt);
					return true;
				case 0x1F: // sq
					if (!m_opts.quad_memory)
						return Fail(error, "quad memory disabled", pc);
					m_b.Emit2(ir::Op::Store128, ir::Type::Void,
						Bin(ir::Op::And, Addr(rs, simm), Const(~u32(15))), ReadGprQuad(rt));
					return true;
				case 0x27: // lwu
					WriteGprWide(rt, m_b.Emit1(ir::Op::Zext32, ir::Type::I64, m_b.Emit1(ir::Op::Load32, ir::Type::I32, Addr(rs, simm))));
					return true;
				case 0x37: // ld
					WriteGprWide(rt, m_b.Emit1(ir::Op::Load64, ir::Type::I64, Addr(rs, simm)));
					return true;
				case 0x3F: // sd
					m_b.Emit2(ir::Op::Store64, ir::Type::Void, Addr(rs, simm), ReadGprWide(rt));
					return true;
				case 0x20: // lb
					WriteGpr(rt, m_b.Emit1(ir::Op::Load8S, ir::Type::I32, Addr(rs, simm)));
					return true;
				case 0x21: // lh
					WriteGpr(rt, m_b.Emit1(ir::Op::Load16S, ir::Type::I32, Addr(rs, simm)));
					return true;
				case 0x23: // lw
					WriteGpr(rt, m_b.Emit1(ir::Op::Load32, ir::Type::I32, Addr(rs, simm)));
					return true;
				case 0x24: // lbu
					WriteGpr(rt, m_b.Emit1(ir::Op::Load8U, ir::Type::I32, Addr(rs, simm)));
					return true;
				case 0x25: // lhu
					WriteGpr(rt, m_b.Emit1(ir::Op::Load16U, ir::Type::I32, Addr(rs, simm)));
					return true;
				case 0x28: // sb
					m_b.Emit2(ir::Op::Store8, ir::Type::Void, Addr(rs, simm), ReadGpr(rt));
					return true;
				case 0x29: // sh
					m_b.Emit2(ir::Op::Store16, ir::Type::Void, Addr(rs, simm), ReadGpr(rt));
					return true;
				case 0x2B: // sw
					m_b.Emit2(ir::Op::Store32, ir::Type::Void, Addr(rs, simm), ReadGpr(rt));
					return true;
				default:
					return Fail(error, "unsupported instruction", pc);
			}
		}

		u32 Lifter::Addr(u32 rs, s32 offset)
		{
			if (offset == 0)
				return ReadGpr(rs);
			return Bin(ir::Op::Add, ReadGpr(rs), Const(static_cast<u32>(offset)));
		}

		bool Lifter::LiftDelaySlot(u32 delay_pc, std::string* error)
		{
			const u32 word = m_code[(delay_pc - m_opts.start_pc) >> 2];
			if (Classify(word) != Kind::Plain)
				return Fail(error, "control instruction in a delay slot", delay_pc);

			m_b.SetGuestPc(delay_pc);
			m_b.SetDelaySlot(true);
			const bool result = LiftPlain(word, delay_pc, error);
			m_b.SetDelaySlot(false);
			return result;
		}

		bool Lifter::Run(u32* end_pc, std::string* error)
		{
			const u32 start = m_opts.start_pc;
			const u32 page_end = (start & ~0xfffu) + 0x1000u;
			const u32 limit = std::min(start + m_opts.max_insts * 4u, page_end);

			m_b.CreateBlock(start);
			m_b.SetGuestPc(start);

			u32 pc = start;
			while (pc < limit)
			{
				const u32 word = m_code[(pc - start) >> 2];
				const Kind kind = Classify(word);

				if (kind == Kind::Plain)
				{
					m_b.SetGuestPc(pc);
					if (!LiftPlain(word, pc, error))
						return false;
					pc += 4;
					continue;
				}

				if (kind == Kind::Unsupported)
					return Fail(error, "unsupported instruction", pc);

				if (kind == Kind::Trap)
				{
					m_b.SetGuestPc(pc);
					m_b.Trap();
					m_b.SetBlockEnd(pc + 4);
					*end_pc = pc + 4;
					return true;
				}

				// Control flow: every path needs its delay slot.
				const u32 delay_pc = pc + 4;
				if (delay_pc >= limit)
					return Fail(error, "delay slot crosses the block limit", pc);

				const u32 branch_pc = pc;
				const u32 fallthrough_pc = delay_pc + 4;
				u32 cond = 0;
				bool link = false;
				u32 guest_target = 0;

				switch (kind)
				{
					case Kind::Branch:
					case Kind::BranchLikely:
					{
						switch (Op(word))
						{
							case 0x01:
								if (!LiftRegimmBranch(word, pc, &cond, &link, error))
									return false;
								break;
							case 0x04: // beq
							case 0x14: // beql
								cond = Bin(ir::Op::CmpEq, ReadGprWide(Rs(word)), ReadGprWide(Rt(word)));
								break;
							case 0x05: // bne
							case 0x15: // bnel
								cond = Bin(ir::Op::CmpNe, ReadGprWide(Rs(word)), ReadGprWide(Rt(word)));
								break;
							case 0x06: // blez
							case 0x16: // blezl
								cond = Bin(ir::Op::CmpLeS, ReadGprWide(Rs(word)), m_b.ConstI64(0));
								break;
							case 0x07: // bgtz
							case 0x17: // bgtzl
								cond = Bin(ir::Op::CmpGtS, ReadGprWide(Rs(word)), m_b.ConstI64(0));
								break;
							case 0x11: // bc1f/bc1t/bc1fl/bc1tl
							{
								const u32 fcr_condition = Bin(ir::Op::And,
									m_b.Emit0(ir::Op::ReadFcr31, ir::Type::I32), Const(0x00800000u));
								cond = Bin((Rt(word) & 1) ? ir::Op::CmpNe : ir::Op::CmpEq,
									fcr_condition, Const(0));
								break;
							}
							default:
								return Fail(error, "unsupported branch", pc);
						}
						guest_target = static_cast<u32>(static_cast<s32>(delay_pc) + Simm(word) * 4);
						break;
					}
					case Kind::Jump:
						guest_target = ((delay_pc & 0xf0000000u) | ((word & 0x03ffffffu) << 2));
						link = (Op(word) == 0x03);
						break;
					case Kind::JumpIndirect:
					{
						const u32 target = ReadGpr(Rs(word));
						link = (Funct(word) == 0x09);
						if (link)
						{
							// jalr with rd == 0 discards the link; it does not write $31.
							const u32 rd = Rd(word);
							WriteGprWide(rd, m_b.ConstI64(branch_pc + 8));
						}
						if (!LiftDelaySlot(delay_pc, error))
							return false;
						m_b.SetGuestPc(fallthrough_pc);
						m_b.BranchIndirect(target);
						m_b.SetBlockEnd(fallthrough_pc);
						*end_pc = fallthrough_pc;
						return true;
					}
					default:
						return Fail(error, "unsupported control flow", pc);
				}

				if (link)
					WriteGprWide(31, m_b.ConstI64(branch_pc + 8));

				const u32 original_block = m_b.CurrentBlock();

				if (kind == Kind::BranchLikely)
				{
					// The delay slot only runs on the taken path, so both
					// outcomes become separate continuation blocks.
					const u32 taken = m_b.CreateBlock(guest_target);
					const u32 skipped = m_b.CreateBlock(fallthrough_pc);

					m_b.SetBlock(original_block);
					m_b.SetGuestPc(branch_pc);
					m_b.Branch(cond, taken, skipped, guest_target);

					m_b.SetBlock(taken);
					m_b.SetGuestPc(delay_pc);
					if (!LiftDelaySlot(delay_pc, error))
						return false;
					m_b.SetGuestPc(fallthrough_pc);
					m_b.Resume(guest_target);
					m_b.SetBlockEnd(fallthrough_pc);

					m_b.SetBlock(skipped);
					m_b.SetGuestPc(fallthrough_pc);
					m_b.Emit(ir::Op::Resume, ir::Type::Void, {}, fallthrough_pc, ir::IF_ANNULLED_DELAY_SLOT);
					m_b.SetBlockEnd(fallthrough_pc);

					*end_pc = fallthrough_pc;
					return true;
				}

				// Normal branches and jumps execute the delay slot on every path.
				if (!LiftDelaySlot(delay_pc, error))
					return false;

				if (kind == Kind::Branch)
				{
					const u32 taken = m_b.CreateBlock(guest_target);
					const u32 fallthrough_block = m_b.CreateBlock(fallthrough_pc);

					// The branch itself belongs to the original block.
					m_b.SetBlock(original_block);
					m_b.SetGuestPc(branch_pc);
					m_b.Branch(cond, taken, fallthrough_block, guest_target);
					m_b.SetBlockEnd(fallthrough_pc);

					m_b.SetBlock(taken);
					m_b.SetGuestPc(guest_target);
					m_b.Resume(guest_target);
					m_b.SetBlockEnd(guest_target);

					m_b.SetBlock(fallthrough_block);
					m_b.SetGuestPc(fallthrough_pc);
					m_b.Resume(fallthrough_pc);
					m_b.SetBlockEnd(fallthrough_pc);
				}
				else // Kind::Jump
				{
					m_b.SetGuestPc(fallthrough_pc);
					m_b.Resume(guest_target);
					m_b.SetBlockEnd(fallthrough_pc);
				}

				*end_pc = fallthrough_pc;
				return true;
			}

			// Fell off the decode limit: this is a fall-through block.
			m_b.SetGuestPc(pc);
			m_b.Resume(pc);
			m_b.SetBlockEnd(pc);
			*end_pc = pc;
			return true;
		}
	} // namespace

	bool LiftBlock(const u32* code, const LiftOptions& options, ir::Function& out, u32* end_pc, std::string* error)
	{
		out = ir::Function();
		out.guest_entry = options.start_pc;

		Lifter lifter(code, options, out);
		return lifter.Run(end_pc, error);
	}

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	bool RunSelfTests()
	{
		// Fall-through, conditional branch and delay slot.
		{
			alignas(4) static const u32 code[] = {
				0x24080005, // addiu t0, zero, 5
				0x11000002, // beq t0, zero, +2
				0x25080001, // addiu t0, t0, 1 (delay slot)
				0x00000000, // nop
			};
			ir::Function fn;
			u32 end = 0;
			std::string error;
			if (!LiftBlock(code, {0x00100000, 4}, fn, &end, &error))
				return false;
			if (end != 0x0010000c || !ir::Verify(fn, &error))
				return false;
			const std::string dump = ir::Dump(fn);
			if (dump.find("CmpEq") == std::string::npos || dump.find("Branch") == std::string::npos ||
				dump.find("Resume") == std::string::npos || dump.find("Add") == std::string::npos)
				return false;
		}

		// jal writes the link register and exits at the target.
		{
			alignas(4) static const u32 code[] = {
				0x0c040800, // jal 0x00102000
				0x00000000, // nop
			};
			ir::Function fn;
			u32 end = 0;
			std::string error;
			if (!LiftBlock(code, {0x00100000, 2}, fn, &end, &error))
				return false;
			if (end != 0x00100008 || !ir::Verify(fn, &error))
				return false;
			const std::string dump = ir::Dump(fn);
			if (dump.find("WriteGpr") == std::string::npos || dump.find("0x00100008") == std::string::npos ||
				dump.find("0x00102000") == std::string::npos)
				return false;
		}

		// jr becomes an indirect terminator.
		{
			alignas(4) static const u32 code[] = {
				0x03e00008, // jr $31
				0x00000000, // nop
			};
			ir::Function fn;
			u32 end = 0;
			std::string error;
			if (!LiftBlock(code, {0x00100000, 2}, fn, &end, &error))
				return false;
			if (end != 0x00100008 || !ir::Verify(fn, &error))
				return false;
			if (ir::Dump(fn).find("BranchIndirect") == std::string::npos)
				return false;
		}

		// mult/div produce HI/LO state updates.
		{
			alignas(4) static const u32 code[] = {
				0x01090018, // mult t0, t1
				0x00005012, // mflo t2
			};
			ir::Function fn;
			u32 end = 0;
			std::string error;
			if (!LiftBlock(code, {0x00100000, 2}, fn, &end, &error))
				return false;
			if (!ir::Verify(fn, &error))
				return false;
			const std::string dump = ir::Dump(fn);
			if (dump.find("MulHiS") == std::string::npos || dump.find("WriteHi") == std::string::npos ||
				dump.find("WriteLo") == std::string::npos || dump.find("ReadLo") == std::string::npos)
				return false;
		}

		// Likely branch: delay slot only on the taken path.
		{
			alignas(4) static const u32 code[] = {
				0x51000001, // beql t0, zero, +1
				0x25080001, // addiu t0, t0, 1 (conditional delay slot)
				0x00000000, // nop
			};
			ir::Function fn;
			u32 end = 0;
			std::string error;
			if (!LiftBlock(code, {0x00100000, 3}, fn, &end, &error))
				return false;
			if (end != 0x00100008 || !ir::Verify(fn, &error))
				return false;
			if (fn.blocks.size() != 3)
				return false;
		}

		// Unsupported instructions must fail so the caller can fall back.
		{
			alignas(4) static const u32 code[] = {
				0xc5280000, // lwc1 f8, 0(t1)
			};
			ir::Function fn;
			u32 end = 0;
			std::string error;
			if (LiftBlock(code, {0x00100000, 1}, fn, &end, &error))
				return false;
		}

		return true;
	}
#endif
} // namespace EeIr
