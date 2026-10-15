// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "arm64/iop/IopIrLifter-arm64.h"
#include <algorithm>

namespace IopIr
{
    namespace
    {
        u32 ReadCp2Data(ir::Builder& b, u32 reg)
        {
            return b.Emit(ir::Op::ReadCp2Data, ir::Type::I32, {}, reg);
        }

        void WriteCp2Data(ir::Builder& b, u32 reg, u32 value)
        {
            b.Emit1(ir::Op::WriteCp2Data, ir::Type::Void, value, reg);
        }

        u32 EmitMfc2Value(ir::Builder& b, u32 reg)
        {
            if (reg != 29)
                return ReadCp2Data(b, reg);

            // MFC2 $29 computes ORGB and also writes the computed value back
            // to CP2D[29], including when used as the source of SWC2.
            const u32 ir1 = b.Emit2(ir::Op::And, ir::Type::I32,
                b.Emit2(ir::Op::ShrU, ir::Type::I32, ReadCp2Data(b, 9), b.ConstI32(7)), b.ConstI32(31));
            const u32 ir2 = b.Emit2(ir::Op::Shl, ir::Type::I32,
                b.Emit2(ir::Op::And, ir::Type::I32,
                    b.Emit2(ir::Op::ShrU, ir::Type::I32, ReadCp2Data(b, 10), b.ConstI32(7)), b.ConstI32(31)), b.ConstI32(5));
            const u32 ir3 = b.Emit2(ir::Op::Shl, ir::Type::I32,
                b.Emit2(ir::Op::And, ir::Type::I32,
                    b.Emit2(ir::Op::ShrU, ir::Type::I32, ReadCp2Data(b, 11), b.ConstI32(7)), b.ConstI32(31)), b.ConstI32(10));
            const u32 value = b.Emit2(ir::Op::Or, ir::Type::I32,
                b.Emit2(ir::Op::Or, ir::Type::I32, ir1, ir2), ir3);
            WriteCp2Data(b, 29, value);
            return value;
        }

        void EmitMtc2Value(ir::Builder& b, u32 reg, u32 value)
        {
            if (reg >= 8 && reg <= 11)
            {
                const u32 extended = b.Emit2(ir::Op::ShrS, ir::Type::I32,
                    b.Emit2(ir::Op::Shl, ir::Type::I32, value, b.ConstI32(16)), b.ConstI32(16));
                WriteCp2Data(b, reg, extended);
            }
            else if (reg == 15)
            {
                const u32 sxy1 = ReadCp2Data(b, 13), sxy2 = ReadCp2Data(b, 14);
                WriteCp2Data(b, 12, sxy1);
                WriteCp2Data(b, 13, sxy2);
                WriteCp2Data(b, 14, value);
                WriteCp2Data(b, 15, value);
            }
            else if (reg >= 16 && reg <= 19)
                WriteCp2Data(b, reg, b.Emit2(ir::Op::And, ir::Type::I32, value, b.ConstI32(0xffff)));
            else if (reg == 28)
            {
                WriteCp2Data(b, 28, value);
                WriteCp2Data(b, 9, b.Emit2(ir::Op::Shl, ir::Type::I32,
                    b.Emit2(ir::Op::And, ir::Type::I32, value, b.ConstI32(31)), b.ConstI32(7)));
                WriteCp2Data(b, 10, b.Emit2(ir::Op::Shl, ir::Type::I32,
                    b.Emit2(ir::Op::And, ir::Type::I32,
                        b.Emit2(ir::Op::ShrU, ir::Type::I32, value, b.ConstI32(5)), b.ConstI32(31)), b.ConstI32(7)));
                WriteCp2Data(b, 11, b.Emit2(ir::Op::Shl, ir::Type::I32,
                    b.Emit2(ir::Op::And, ir::Type::I32,
                        b.Emit2(ir::Op::ShrU, ir::Type::I32, value, b.ConstI32(10)), b.ConstI32(31)), b.ConstI32(7)));
            }
            else if (reg == 30)
            {
                WriteCp2Data(b, 30, value);
                const u32 sign = b.Emit2(ir::Op::ShrS, ir::Type::I32, value, b.ConstI32(31));
                WriteCp2Data(b, 31, b.Emit1(ir::Op::Clz, ir::Type::I32,
                    b.Emit2(ir::Op::Xor, ir::Type::I32, value, sign)));
            }
            else
                WriteCp2Data(b, reg, value);
        }

        bool LiftMemory(ir::Builder& b, u32 word)
        {
            const u32 op = word >> 26, rs = (word >> 21) & 31, rt = (word >> 16) & 31;
            const auto read = [&](u32 r) { return r == 0 ? b.ConstI32(0) : b.Emit(ir::Op::ReadGpr, ir::Type::I32, {}, r); };
            const auto bin = [&](ir::Op operation, u32 a, u32 c) { return b.Emit2(operation, ir::Type::I32, a, c); };
            const auto write = [&](u32 v) { b.Emit1(ir::Op::WriteGpr, ir::Type::Void, v, rt); };
            const u32 address = bin(ir::Op::Add, read(rs), b.ConstI32(static_cast<u32>(static_cast<s16>(word))));
            if (op == 50)
            {
                const u32 value = b.Emit1(ir::Op::Load32, ir::Type::I32, address);
                EmitMtc2Value(b, rt, value);
                return true;
            }
            if (op == 58)
            {
                b.Emit2(ir::Op::Store32, ir::Type::Void, address, EmitMfc2Value(b, rt));
                return true;
            }
            if (op == 32 || op == 33 || op == 35 || op == 36 || op == 37)
            {
                const ir::Op load = op == 32 ? ir::Op::Load8S : op == 33 ? ir::Op::Load16S :
                    op == 36 ? ir::Op::Load8U : op == 37 ? ir::Op::Load16U : ir::Op::Load32;
                write(b.Emit1(load, ir::Type::I32, address)); // keep loads into $0 for MMIO side effects
                return true;
            }
            if (op == 40 || op == 41 || op == 43)
            {
                b.Emit2(op == 40 ? ir::Op::Store8 : op == 41 ? ir::Op::Store16 : ir::Op::Store32,
                    ir::Type::Void, address, read(rt));
                return true;
            }
            // R3000A little-endian word merges. Capture the original address
            // before the read, including base==target. Each shift is 0..24.
            const u32 shift = bin(ir::Op::Shl, bin(ir::Op::And, address, b.ConstI32(3)), b.ConstI32(3));
            const u32 complement = bin(ir::Op::Sub, b.ConstI32(24), shift);
            const u32 aligned = bin(ir::Op::And, address, b.ConstI32(0xfffffffcu));
            const u32 memory = b.Emit1(ir::Op::Load32, ir::Type::I32, aligned);
            if ((op == 34 || op == 38) && rt == 0) return true;
            const u32 reg = read(rt);
            u32 merged;
            if (op == 34) // LWL
                merged = bin(ir::Op::Or, bin(ir::Op::And, reg, bin(ir::Op::ShrU, b.ConstI32(0x00ffffff), shift)),
                    bin(ir::Op::Shl, memory, complement));
            else if (op == 38) // LWR
                merged = bin(ir::Op::Or, bin(ir::Op::And, reg, bin(ir::Op::Shl, b.ConstI32(0xffffff00), complement)),
                    bin(ir::Op::ShrU, memory, shift));
            else if (op == 42) // SWL
                merged = bin(ir::Op::Or, bin(ir::Op::ShrU, reg, complement),
                    bin(ir::Op::And, memory, bin(ir::Op::Shl, b.ConstI32(0xffffff00), shift)));
            else // SWR
                merged = bin(ir::Op::Or, bin(ir::Op::Shl, reg, shift),
                    bin(ir::Op::And, memory, bin(ir::Op::ShrU, b.ConstI32(0x00ffffff), complement)));
            if (op == 34 || op == 38) write(merged);
            else b.Emit2(ir::Op::Store32, ir::Type::Void, aligned, merged);
            return true;
        }

        bool LiftInstruction(ir::Builder& b, u32 word)
        {
            // ADDIU $0,$0,index is an IRX import marker, with possible host HLE
            // side effects in the legacy decoder even though its GPR write dies.
            if (word >> 16 == 0x2400) return false;
            const u32 rs = (word >> 21) & 31, rt = (word >> 16) & 31;
            const u32 rd = (word >> 11) & 31, sa = (word >> 6) & 31;
            const auto read = [&](u32 r) { return r == 0 ? b.ConstI32(0) : b.Emit(ir::Op::ReadGpr, ir::Type::I32, {}, r); };
            const auto write = [&](u32 r, u32 v) { b.Emit1(ir::Op::WriteGpr, ir::Type::Void, v, r); };
            const u32 opcode = word >> 26;
            if ((opcode >= 32 && opcode <= 38) || (opcode >= 40 && opcode <= 43) || opcode == 46 || opcode == 50 || opcode == 58)
                return LiftMemory(b, word);
            ir::Op op;
            switch (opcode)
            {
                case 8: case 9: op = ir::Op::Add; break; // ADDI/ADDIU: current core wraps to 32 bits
                case 10: op = ir::Op::CmpLtS; break;
                case 11: op = ir::Op::CmpLtU; break;
                case 12: op = ir::Op::And; break;
                case 13: op = ir::Op::Or; break;
                case 14: op = ir::Op::Xor; break;
                case 15: write(rt, b.ConstI32(word << 16)); return true;
                case 16:
                    switch (rs)
                    {
                        case 0: case 2: // MFC0/CFC0: no read side effect for $0
                            if (rt) write(rt, b.Emit(ir::Op::ReadCp0, ir::Type::I32, {}, rd));
                            return true;
                        case 4: case 6: // MTC0/CTC0: all fields are writable in the current core
                            b.Emit1(ir::Op::WriteCp0, ir::Type::Void, read(rt), rd);
                            return true;
                        case 16: // Legacy COP0 dispatch selects RFE by Rs, including low-bit aliases.
                        {
                            const u32 status = b.Emit(ir::Op::ReadCp0, ir::Type::I32, {}, 12);
                            const u32 low = b.Emit2(ir::Op::And, ir::Type::I32, status, b.ConstI32(0x3c));
                            const u32 restored = b.Emit2(ir::Op::ShrU, ir::Type::I32, low, b.ConstI32(2));
                            const u32 upper = b.Emit2(ir::Op::And, ir::Type::I32, status, b.ConstI32(0xfffffff0u));
                            b.Emit1(ir::Op::WriteCp0, ir::Type::Void, b.Emit2(ir::Op::Or, ir::Type::I32, upper, restored), 12);
                            b.Emit0(ir::Op::CheckInterrupts, ir::Type::Void);
                            return true;
                        }
                        default: return false;
                    }
                case 18: // COP2 transfers and bounded canonical GTE commands.
                {
                    const u32 gte_funct = word & 63u;
                    if (rs == 16 && (gte_funct == 0x2du || gte_funct == 0x2eu))
                    {
                        // AVSZ3/4 use unsigned SZ low halves and a signed ZSF
                        // low half. The product is an I32 wrap before ASR 12;
                        // MAC0 receives that shifted result, while OTZ clamps
                        // it to [0, 65535] and preserves D7's upper half.
                        const bool avsz4 = gte_funct == 0x2eu;
                        const u32 first_sz = avsz4 ? 16u : 17u;
                        const u32 zero = b.ConstI32(0);
                        const u32 low16_mask = b.ConstI32(0xffff);
                        const u32 shift16 = b.ConstI32(16);
                        const u32 shift12 = b.ConstI32(12);
                        const u32 upper16_mask = b.ConstI32(0xffff0000u);
                        const u32 overflow_flag = b.ConstI32(0x80040000u);

                        const u32 first_word = ReadCp2Data(b, first_sz);
                        u32 sum = b.Emit2(ir::Op::And, ir::Type::I32,
                            first_word, low16_mask);
                        for (u32 reg = first_sz + 1; reg <= 19; ++reg)
                        {
                            const u32 word_value = ReadCp2Data(b, reg);
                            const u32 sz = b.Emit2(ir::Op::And, ir::Type::I32,
                                word_value, low16_mask);
                            sum = b.Emit2(ir::Op::Add, ir::Type::I32, sum, sz);
                        }
                        const u32 scale_reg = avsz4 ? 30u : 29u;
                        const u32 raw_scale = b.Emit(ir::Op::ReadCp2Control,
                            ir::Type::I32, {}, scale_reg);
                        const u32 scale_left = b.Emit2(ir::Op::Shl, ir::Type::I32,
                            raw_scale, shift16);
                        const u32 scale = b.Emit2(ir::Op::ShrS, ir::Type::I32,
                            scale_left, shift16);
                        const u32 product = b.Emit2(ir::Op::Mul, ir::Type::I32, sum, scale);
                        const u32 shifted = b.Emit2(ir::Op::ShrS, ir::Type::I32,
                            product, shift12);
                        WriteCp2Data(b, 24, shifted);

                        const u32 nonnegative = b.Emit2(ir::Op::MaxS, ir::Type::I32,
                            shifted, zero);
                        const u32 otz = b.Emit2(ir::Op::MinU, ir::Type::I32,
                            nonnegative, low16_mask);
                        const u32 old_d7 = ReadCp2Data(b, 7);
                        const u32 kept_upper = b.Emit2(ir::Op::And, ir::Type::I32,
                            old_d7, upper16_mask);
                        const u32 merged_d7 = b.Emit2(ir::Op::Or, ir::Type::I32,
                            kept_upper, otz);
                        WriteCp2Data(b, 7, merged_d7);

                        const u32 overflow = b.Emit2(ir::Op::CmpNe, ir::Type::I32,
                            shifted, otz);
                        const u32 flag = b.Emit3(ir::Op::Select, ir::Type::I32,
                            overflow, overflow_flag, zero);
                        b.Emit1(ir::Op::WriteCp2Control, ir::Type::Void, flag, 31);
                        return true;
                    }
                    if (rs == 16 && (word & 63u) == 6)
                    {
                        // Match the current core's raw signed-coordinate NCLIP:
                        // products and sums wrap to I32, MAC0 receives the
                        // determinant, and FLAG is cleared. SF/LM are ignored.
                        // Capture all source words before committing either write.
                        const u32 xy0 = ReadCp2Data(b, 12);
                        const u32 xy1 = ReadCp2Data(b, 13);
                        const u32 xy2 = ReadCp2Data(b, 14);
                        const u32 shift16 = b.ConstI32(16);
                        const auto x = [&](u32 xy) {
                            return b.Emit2(ir::Op::ShrS, ir::Type::I32,
                                b.Emit2(ir::Op::Shl, ir::Type::I32, xy, shift16), shift16);
                        };
                        const auto y = [&](u32 xy) {
                            return b.Emit2(ir::Op::ShrS, ir::Type::I32, xy, shift16);
                        };
                        const u32 x0 = x(xy0), y0 = y(xy0);
                        const u32 x1 = x(xy1), y1 = y(xy1);
                        const u32 x2 = x(xy2), y2 = y(xy2);
                        const auto product = [&](u32 a, u32 first, u32 second) {
                            return b.Emit2(ir::Op::Mul, ir::Type::I32, a,
                                b.Emit2(ir::Op::Sub, ir::Type::I32, first, second));
                        };
                        const u32 a = product(x0, y1, y2);
                        const u32 c = product(x1, y2, y0);
                        const u32 d = product(x2, y0, y1);
                        const u32 determinant = b.Emit2(ir::Op::Add, ir::Type::I32,
                            b.Emit2(ir::Op::Add, ir::Type::I32, a, c), d);
                        WriteCp2Data(b, 24, determinant);
                        b.Emit1(ir::Op::WriteCp2Control, ir::Type::Void, b.ConstI32(0), 31);
                        return true;
                    }
                    // Other nonzero funct encodings retain legacy GTE dispatch,
                    // even when their Rs field resembles a register transfer.
                    if (word & 63u) return false;
                    switch (rs)
                    {
                        case 0: // MFC2; $0 suppresses both the GPR write and rd29's ORGB side effect.
                            if (rt != 0)
                            {
                                write(rt, EmitMfc2Value(b, rd));
                            }
                            return true;
                        case 2: // CFC2; no read or write for $0.
                            if (rt != 0)
                                write(rt, b.Emit(ir::Op::ReadCp2Control, ir::Type::I32, {}, rd));
                            return true;
                        case 4: // MTC2: compose the R3000A register-specific transforms from raw state ops.
                        {
                            EmitMtc2Value(b, rd, read(rt));
                            return true;
                        }
                        case 6: // CTC2 is an unmodified 32-bit control-register write.
                            b.Emit1(ir::Op::WriteCp2Control, ir::Type::Void, read(rt), rd);
                            return true;
                        default:
                            return false;
                    }
                }
                case 0:
                {
                    switch (word & 63)
                    {
                        case 0: op = ir::Op::Shl; break;
                        case 2: op = ir::Op::ShrU; break;
                        case 3: op = ir::Op::ShrS; break;
                        case 4: op = ir::Op::Shl; break;
                        case 6: op = ir::Op::ShrU; break;
                        case 7: op = ir::Op::ShrS; break;
                        case 0x10: write(rd, b.Emit0(ir::Op::ReadHi, ir::Type::I32)); return true;
                        case 0x12: write(rd, b.Emit0(ir::Op::ReadLo, ir::Type::I32)); return true;
                        case 0x11: b.Emit1(ir::Op::WriteHi, ir::Type::Void, read(rs)); return true;
                        case 0x13: b.Emit1(ir::Op::WriteLo, ir::Type::Void, read(rs)); return true;
                        case 0x18: case 0x19: // MULT/MULTU: IOP ignores Rd
                        case 0x1a: case 0x1b: // DIV/DIVU
                        {
                            const u32 a = read(rs), c = read(rt);
                            const bool multiply = (word & 63) <= 0x19;
                            const bool sign = ((word & 63) & 1) == 0;
                            const u32 lo = b.Emit2(multiply ? ir::Op::Mul : sign ? ir::Op::DivS : ir::Op::DivU,
                                ir::Type::I32, a, c);
                            const u32 hi = multiply ? b.Emit2(sign ? ir::Op::MulHiS : ir::Op::MulHiU, ir::Type::I32, a, c) :
                                b.Emit3(ir::Op::Msub, ir::Type::I32, lo, c, a);
                            b.Emit1(ir::Op::WriteLo, ir::Type::Void, lo);
                            b.Emit1(ir::Op::WriteHi, ir::Type::Void, hi);
                            return true;
                        }
                        case 0x20: case 0x21: op = ir::Op::Add; break; // ADD/ADDU: legacy wrap semantics
                        case 0x22: case 0x23: op = ir::Op::Sub; break; // SUB/SUBU: legacy wrap semantics
                        case 0x24: op = ir::Op::And; break;
                        case 0x25: op = ir::Op::Or; break;
                        case 0x26: case 0x27: op = ir::Op::Xor; break;
                        case 0x2a: op = ir::Op::CmpLtS; break;
                        case 0x2b: op = ir::Op::CmpLtU; break;
                        default: return false;
                    }
                    const u32 funct = word & 63;
                    u32 value;
                    if (funct <= 7)
                    {
                        const u32 amount = funct <= 3 ? b.ConstI32(sa) : read(rs);
                        value = b.Emit2(op, ir::Type::I32, read(rt), amount);
                    }
                    else if (funct == 0x27)
                    {
                        value = b.Emit2(ir::Op::Or, ir::Type::I32, read(rs), read(rt));
                        value = b.Emit1(ir::Op::Not, ir::Type::I32, value);
                    }
                    else
                        value = b.Emit2(op, ir::Type::I32, read(rs), read(rt));
                    write(rd, value);
                    return true;
                }
                default: return false;
            }
            const u32 immediate = (word >> 26) <= 11 ? static_cast<u32>(static_cast<s16>(word)) : word & 0xffff;
            write(rt, b.Emit2(op, ir::Type::I32, read(rs), b.ConstI32(immediate)));
            return true;
        }
        bool IsControl(u32 word)
        {
            const u32 op = word >> 26, rt = (word >> 16) & 31, funct = word & 63;
            return (op >= 2 && op <= 7) || (op == 0 && (funct == 8 || funct == 9)) ||
                (op == 1 && (rt == 0 || rt == 1 || rt == 16 || rt == 17));
        }

        bool CanLiftDelay(u32 word)
        {
            ir::Function probe;
            ir::Builder b(probe);
            b.CreateBlock(4);
            return LiftInstruction(b, word);
        }

        void LiftControl(ir::Builder& b, u32 word, u32 delay, u32 pc)
        {
            const u32 op = word >> 26, rs = (word >> 21) & 31, rt = (word >> 16) & 31, rd = (word >> 11) & 31;
            const auto read = [&](u32 r) { return r == 0 ? b.ConstI32(0) : b.Emit(ir::Op::ReadGpr, ir::Type::I32, {}, r); };
            const bool indirect = op == 0, jump = op == 2 || op == 3;
            const u32 link = op == 3 || (op == 1 && rt >= 16) ? 31 : indirect && (word & 63) == 9 ? rd : 0;
            // Existing R3000A interpreter and recompiler read Rs after the
            // link write, including JALR Rd==Rs and REGIMM-AL Rs==31 aliases.
            if (link) b.Emit1(ir::Op::WriteGpr, ir::Type::Void, b.ConstI32(pc + 8), link);
            u32 condition = 0, address = 0;
            if (indirect) address = read(rs);
            else if (!jump)
            {
                const u32 a = read(rs);
                const u32 c = op == 4 || op == 5 ? read(rt) : b.ConstI32(0);
                const ir::Op compare = op == 4 ? ir::Op::CmpEq : op == 5 ? ir::Op::CmpNe :
                    op == 6 ? ir::Op::CmpLeS : op == 7 ? ir::Op::CmpGtS :
                    (rt & 1) ? ir::Op::CmpGeS : ir::Op::CmpLtS;
                condition = b.Emit2(compare, ir::Type::I32, a, c);
            }
            b.SetGuestPc(pc + 4);
            b.SetDelaySlot(true);
            LiftInstruction(b, delay); // prevalidated before any control effects
            b.SetDelaySlot(false);
            b.SetGuestPc(pc);
            b.SetBlockEnd(pc + 8);
            if (indirect) b.BranchIndirect(address);
            else
            {
                const u32 target = jump ? ((pc + 4) & 0xf0000000u) | ((word & 0x03ffffffu) << 2) :
                    pc + 4 + static_cast<u32>(static_cast<s32>(static_cast<s16>(word)) * 4);
                if (jump) b.Resume(target);
                else
                {
                    const u32 original = b.CurrentBlock();
                    const u32 taken = b.CreateBlock(target), fallthrough = b.CreateBlock(pc + 8);
                    b.SetBlock(original);
                    b.SetGuestPc(pc);
                    b.Branch(condition, taken, fallthrough, target);
                    b.SetBlock(taken);
                    b.Resume(target);
                    b.SetBlockEnd(target);
                    b.SetBlock(fallthrough);
                    b.Resume(pc + 8);
                    b.SetBlockEnd(pc + 8);
                }
            }
        }
    }

    bool LiftSequence(const u32* code, u32 start_pc, u32 max_insts,
        ir::Function& out, u32* accepted, std::string* error, bool* ends_in_branch)
    {
        out = {};
        *accepted = 0;
        if (ends_in_branch) *ends_in_branch = false;
        if (!code || (start_pc & 3) || max_insts == 0)
        {
            if (error) *error = "empty IOP sequence or misaligned PC";
            return false;
        }
        ir::Builder b(out);
        b.CreateBlock(start_pc);
        const u32 limit = std::min(max_insts, (0x1000u - (start_pc & 0xfff)) / 4);
        for (u32 i = 0; i < limit; ++i)
        {
            b.SetGuestPc(start_pc + i * 4);
            if (IsControl(code[i]))
            {
                if (i + 1 >= limit || !CanLiftDelay(code[i + 1])) break;
                LiftControl(b, code[i], code[i + 1], start_pc + i * 4);
                *accepted += 2;
                if (ends_in_branch) *ends_in_branch = true;
                return ir::Verify(out, error);
            }
            if (!LiftInstruction(b, code[i])) break;
            ++*accepted;
        }
        if (*accepted == 0)
        {
            out = {};
            if (error) *error = "unsupported IOP instruction at sequence start";
            return false;
        }
        const u32 end = start_pc + *accepted * 4;
        b.SetBlockEnd(end);
        b.Resume(end);
        return ir::Verify(out, error);
    }
}
