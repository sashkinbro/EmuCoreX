// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
//
// Architecture-neutral recompiler IR shared by the EE, IOP and VU front ends.
//
// The IR is a linear SSA form per basic block: every instruction defines at
// most one value, values are block-local, and a block ends in exactly one
// terminator. Guest architectural state (GPRs, FPRs, VF/VI, HI/LO, flags) is
// modelled with explicit read/write instructions instead of phi nodes, so a
// block can be lowered independently and block linking only needs to flush
// the values that stay live across the edge.
//
// Value id 0 is reserved for "no value". Block id 0 is reserved for "no
// block". Both allocators start at 1.

#pragma once

#include "common/Pcsx2Defs.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace ir
{
	// ------------------------------------------------------------------
	// Types
	// ------------------------------------------------------------------
	enum class Type : u8
	{
		Void = 0,
		I32,
		I64,
		F32,
		V4F32, // four IEEE single lanes (VU/MMI vector register)
		V4I32, // four signed 32-bit lanes
		V4U32, // four unsigned 32-bit lanes
		Flags, // packed emulator flag word (status/mac/clip or EE FPU flags)
		Any,   // wildcard used by polymorphic ops (Copy, Add, Select, ...)
	};

	const char* TypeName(Type type);
	bool IsVectorType(Type type);
	bool IsIntegerType(Type type);
	u32 TypeSize(Type type);

	// Raw VU scalar state selectors, independent of physical VI/cache indexes.
	// I is the current architectural 32-bit word; reading it applies no clamp.
	enum class VuScalar : u8
	{
		I = 0,
	};

	// ------------------------------------------------------------------
	// Opcodes
	// ------------------------------------------------------------------
	// IR_OP_LIST entries: name, kind, min_args, max_args, result, terminator.
	// The result type is what the instruction defines; Any means the result
	// type must equal the first operand type (polymorphic ops).
#define IR_OP_LIST(X) \
	/* Pseudo */ \
	X(Nop,          Pseudo,      0, 0, Void, false) \
	X(ConstI32,     Pseudo,      0, 0, I32,  false) \
	X(ConstI64,     Pseudo,      0, 0, I64,  false) \
	X(ConstF32,     Pseudo,      0, 0, F32,  false) \
	X(ConstVec,     Pseudo,      0, 0, V4U32, false) \
	X(Copy,         Pseudo,      1, 1, Any,  false) \
	X(Undef,        Pseudo,      0, 0, Any,  false) \
	X(Select,       Pseudo,      3, 3, Any,  false) \
	/* Integer */ \
	X(Not,          Unary,       1, 1, Any,  false) \
	X(Neg,          Unary,       1, 1, Any,  false) \
	X(Clz,          Unary,       1, 1, Any,  false) \
	X(Ctz,          Unary,       1, 1, Any,  false) \
	X(Sext8,        Unary,       1, 1, Any,  false) \
	X(Sext16,       Unary,       1, 1, Any,  false) \
	X(Zext8,        Unary,       1, 1, Any,  false) \
	X(Zext16,       Unary,       1, 1, Any,  false) \
	X(Sext32,       Unary,       1, 1, I64,  false) \
	X(Zext32,       Unary,       1, 1, I64,  false) \
	X(Trunc32,      Unary,       1, 1, I32,  false) \
	X(Bswap,        Unary,       1, 1, Any,  false) \
	X(Add,          Binary,      2, 2, Any,  false) \
	X(Sub,          Binary,      2, 2, Any,  false) \
	X(Mul,          Binary,      2, 2, Any,  false) \
	/* Msub(a,b,c) = c - a*b, with modulo integer arithmetic. */ \
	X(Msub,         Ternary,     3, 3, Any,  false) \
	X(MulHiS,       Binary,      2, 2, Any,  false) \
	X(MulHiU,       Binary,      2, 2, Any,  false) \
	X(DivS,         Binary,      2, 2, Any,  false) \
	X(DivU,         Binary,      2, 2, Any,  false) \
	X(RemS,         Binary,      2, 2, Any,  false) \
	X(RemU,         Binary,      2, 2, Any,  false) \
	X(And,          Binary,      2, 2, Any,  false) \
	X(Or,           Binary,      2, 2, Any,  false) \
	X(Xor,          Binary,      2, 2, Any,  false) \
	X(Shl,          Shift,       2, 2, Any,  false) \
	X(ShrU,         Shift,       2, 2, Any,  false) \
	X(ShrS,         Shift,       2, 2, Any,  false) \
	X(Rotl,         Shift,       2, 2, Any,  false) \
	X(Rotr,         Shift,       2, 2, Any,  false) \
	X(MinS,         Binary,      2, 2, Any,  false) \
	X(MinU,         Binary,      2, 2, Any,  false) \
	X(MaxS,         Binary,      2, 2, Any,  false) \
	X(MaxU,         Binary,      2, 2, Any,  false) \
	X(CmpEq,        Compare,     2, 2, I32,  false) \
	X(CmpNe,        Compare,     2, 2, I32,  false) \
	X(CmpLtS,       Compare,     2, 2, I32,  false) \
	X(CmpLtU,       Compare,     2, 2, I32,  false) \
	X(CmpLeS,       Compare,     2, 2, I32,  false) \
	X(CmpLeU,       Compare,     2, 2, I32,  false) \
	X(CmpGtS,       Compare,     2, 2, I32,  false) \
	X(CmpGtU,       Compare,     2, 2, I32,  false) \
	X(CmpGeS,       Compare,     2, 2, I32,  false) \
	X(CmpGeU,       Compare,     2, 2, I32,  false) \
	/* Guest memory */ \
	X(Load8U,       MemLoad,     1, 1, I32,  false) \
	X(Load8S,       MemLoad,     1, 1, I32,  false) \
	X(Load16U,      MemLoad,     1, 1, I32,  false) \
	X(Load16S,      MemLoad,     1, 1, I32,  false) \
	X(Load32,       MemLoad,     1, 1, I32,  false) \
	X(Load64,       MemLoad,     1, 1, I64,  false) \
	X(Load128,      MemLoad,     1, 1, V4U32, false) \
	X(Store8,       MemStore,    2, 2, Void, false) \
	X(Store16,      MemStore,    2, 2, Void, false) \
	X(Store32,      MemStore,    2, 2, Void, false) \
	X(Store64,      MemStore,    2, 2, Void, false) \
	X(Store128,     MemStore,    2, 2, Void, false) \
	/* Guest architectural state */ \
	X(ReadGpr,      StateRead,   0, 0, Any,  false) \
	X(WriteGpr,     StateWrite,  1, 1, Void, false) \
	X(ReadCp0,      StateRead,   0, 0, I32,  false) \
	X(WriteCp0,     StateWrite,  1, 1, Void, false) \
	/* Raw IOP GTE register access; transfer side effects are composed in the lifter. */ \
	X(ReadCp2Data,  StateRead,   0, 0, I32,  false) \
	X(WriteCp2Data, StateWrite,  1, 1, Void, false) \
	X(ReadCp2Control, StateRead, 0, 0, I32,  false) \
	X(WriteCp2Control, StateWrite, 1, 1, Void, false) \
	X(ReadFpr,      StateRead,   0, 0, F32,  false) \
	X(WriteFpr,     StateWrite,  1, 1, Void, false) \
	/* Raw scalar COP1 transfer words. FCR31 is the only architecturally writable FCR here. */ \
	X(ReadFprBits,  StateRead,   0, 0, I32,  false) \
	X(WriteFprBits, StateWrite,  1, 1, Void, false) \
	X(ReadFcr31,    StateRead,   0, 0, I32,  false) \
	X(WriteFcr31,   StateWrite,  1, 1, Void, false) \
	X(ReadVf,       StateRead,   0, 0, V4F32, false) \
	/* Read all four VF words as signed raw integer lanes without FP reinterpretation. */ \
	X(ReadVfBits,   StateRead,   0, 0, V4I32, false) \
	/* Logical scheduled CLIP history; bounded VU callers bind the ring slots. */ \
	X(ReadVuClip,   StateRead,   0, 0, I32, false) \
	X(WriteVuClip,  StateWrite,  1, 1, Void, false) \
	/* Raw 32-bit VU scalar state; imm is a VuScalar selector. */ \
	X(ReadVuScalarBits, StateRead, 0, 0, I32, false) \
	X(WriteVf,      StateWrite,  1, 1, Void, false) \
	/* Write selected raw VU VF lanes from a V4F32 bit vector; aux bits 0..3 are x/y/z/w. */ \
	X(WriteVfMasked, StateWrite, 1, 1, Void, false) \
	X(WriteVfBitsMasked, StateWrite, 1, 1, Void, false) \
	/* Read raw bits from a single VU VF lane; aux selects lane 0..3. */ \
	X(ReadVfLaneBits, StateRead, 0, 0, I32, false) \
	/* Write signed-16 VI bits, extended and splatted to selected raw VF lanes. */ \
	X(WriteVfLaneSplatBits, StateWrite, 1, 1, Void, false) \
	X(ReadVi,       StateRead,   0, 0, I32,  false) \
	X(WriteVi,      StateWrite,  1, 1, Void, false) \
	X(ReadAcc,      StateRead,   0, 0, V4F32, false) \
	X(WriteAcc,     StateWrite,  1, 1, Void, false) \
	/* HI/LO imm selects EE bank 0/1 for scalars; V4U32 accesses full 128 bits at imm=0. IOP is scalar bank 0. */ \
	X(ReadHi,       StateRead,   0, 0, Any,  false) \
	X(WriteHi,      StateWrite,  1, 1, Void, false) \
	X(ReadLo,       StateRead,   0, 0, Any,  false) \
	X(WriteLo,      StateWrite,  1, 1, Void, false) \
	X(ReadFlags,    StateRead,   0, 0, Flags, false) \
	X(WriteFlags,   StateWrite,  1, 1, Void, false) \
	/* Control */ \
	X(Jump,         Control,     1, 1, Void, true) \
	X(Branch,       Control,     3, 3, Void, true) \
	X(BranchIndirect, Control,   1, 1, Void, true) \
	X(CheckEvents,  Control,     1, 1, Void, true) \
	X(Resume,       Control,     0, 0, Void, true) \
	X(Return,       Control,     0, 0, Void, true) \
	X(Trap,         Control,     0, 0, Void, true) \
	X(AddCycles,    Cycle,       0, 0, Void, false) \
	X(CheckInterrupts, Helper,  0, 0, Void, false) \
	X(CallHelper,   Helper,      0, 4, Any,  false) \
	/* EE FPU */ \
	X(FAdd,         FpuBinary,   2, 2, F32,  false) \
	X(FSub,         FpuBinary,   2, 2, F32,  false) \
	X(FMul,         FpuBinary,   2, 2, F32,  false) \
	X(FDiv,         FpuBinary,   2, 2, F32,  false) \
	X(FMin,         FpuBinary,   2, 2, F32,  false) \
	X(FMax,         FpuBinary,   2, 2, F32,  false) \
	X(FMadd,        FpuTernary,  3, 3, F32,  false) \
	X(FMsub,        FpuTernary,  3, 3, F32,  false) \
	X(FSqrt,        FpuUnary,    1, 1, F32,  false) \
	X(FAbs,         FpuUnary,    1, 1, F32,  false) \
	X(FNeg,         FpuUnary,    1, 1, F32,  false) \
	X(FCmpEq,       FpuCompare,  2, 2, I32,  false) \
	X(FCmpLt,       FpuCompare,  2, 2, I32,  false) \
	X(FCmpLe,       FpuCompare,  2, 2, I32,  false) \
	X(F2I,          FpuConvert,  1, 1, I32,  false) \
	X(I2F,          FpuConvert,  1, 1, F32,  false) \
	/* VU / vector */ \
	X(VAdd,         VecBinary,   2, 2, Any,  false) \
	X(VSub,         VecBinary,   2, 2, Any,  false) \
	X(VMul,         VecBinary,   2, 2, Any,  false) \
	X(VMad,         VecTernary,  3, 3, Any,  false) \
	X(VMSub,        VecTernary,  3, 3, Any,  false) \
	X(VAnd,         VecBinary,   2, 2, Any,  false) \
	X(VOr,          VecBinary,   2, 2, Any,  false) \
	X(VXor,         VecBinary,   2, 2, Any,  false) \
	X(VNot,         VecUnary,    1, 1, Any,  false) \
	X(VNeg,         VecUnary,    1, 1, Any,  false) \
	X(VAbs,         VecUnary,    1, 1, Any,  false) \
	/* Packed integer lanes: imm is 8/16/32 bits (0 means 32); 128-bit storage. */ \
	X(VAddI,        VecBinary,   2, 2, Any,  false) \
	X(VSubI,        VecBinary,   2, 2, Any,  false) \
	X(VAddSatS,     VecBinary,   2, 2, Any,  false) \
	X(VAddSatU,     VecBinary,   2, 2, Any,  false) \
	X(VSubSatS,     VecBinary,   2, 2, Any,  false) \
	X(VSubSatU,     VecBinary,   2, 2, Any,  false) \
	X(VAbsSatS,     VecUnary,    1, 1, Any,  false) \
	/* Widening products: low/high source half, signed/unsigned input lanes. */ \
	X(VMulWidenLoS, VecBinary,   2, 2, Any,  false) \
	X(VMulWidenHiS, VecBinary,   2, 2, Any,  false) \
	X(VMulWidenLoU, VecBinary,   2, 2, Any,  false) \
	X(VMulWidenHiU, VecBinary,   2, 2, Any,  false) \
	/* Signed saturating pack: two inputs, output lane width 8/16 (0=16). */ \
	X(VPackSatS,    VecBinary,   2, 2, Any,  false) \
	/* Per-lane shifts: each count is masked by lane_bits-1. */ \
	X(VShlEach,     VecBinary,   2, 2, Any,  false) \
	X(VShrUEach,    VecBinary,   2, 2, Any,  false) \
	X(VShrSEach,    VecBinary,   2, 2, Any,  false) \
	/* Packed shifts mask scalar I32/I64 count with lane_bits - 1. */ \
	X(VShl,         Shift,       2, 2, Any,  false) \
	X(VShrU,        Shift,       2, 2, Any,  false) \
	X(VShrS,        Shift,       2, 2, Any,  false) \
	/* Packed min/max and compares use the same lane width; compares return all ones/zero per lane. */ \
	X(VMinS,        VecBinary,   2, 2, Any,  false) \
	X(VMinU,        VecBinary,   2, 2, Any,  false) \
	X(VMaxS,        VecBinary,   2, 2, Any,  false) \
	X(VMaxU,        VecBinary,   2, 2, Any,  false) \
	X(VCmpEq,       VecCompare,  2, 2, Any,  false) \
	X(VCmpNe,       VecCompare,  2, 2, Any,  false) \
	X(VCmpLtS,      VecCompare,  2, 2, Any,  false) \
	X(VCmpLtU,      VecCompare,  2, 2, Any,  false) \
	X(VCmpLeS,      VecCompare,  2, 2, Any,  false) \
	X(VCmpLeU,      VecCompare,  2, 2, Any,  false) \
	X(VBroadcast,   VecLane,     1, 1, Any,  false) \
	/* Repeat one I32 word in all four V4I32 lanes without FP conversion. */ \
	X(VSplat32,     VecSplat,    1, 1, V4I32, false) \
	/* Bit-preserving packed permutations. imm is lane width 8/16/32 (0=32). */ \
	X(VZipLo,       VecBinary,   2, 2, Any,  false) \
	X(VZipHi,       VecBinary,   2, 2, Any,  false) \
	X(VPackEven,    VecBinary,   2, 2, Any,  false) \
	X(VInterleaveEven, VecBinary, 2, 2, Any, false) \
	X(VInterleaveOdd, VecBinary, 2, 2, Any, false) \
	/* VShuffleBytes: sixteen 4-bit byte selectors packed in imm, low byte first. */ \
	X(VShuffleBytes, VecLane,    1, 1, Any,  false) \
	/* Bit-preserving 32-bit lanes. imm packs four selectors, low lane first: */ \
	/* VShuffle uses 2 bits/lane (0..3); VShuffle2 uses 3 (a:0..3,b:4..7). */ \
	X(VShuffle,     VecLane,     1, 1, Any,  false) \
	X(VShuffle2,    VecLane2,    2, 2, Any,  false) \
	X(VMerge,       VecLane2,    2, 2, Any,  false) \
	X(VPack16,      VecUnary,    1, 1, Any,  false) \
	X(VPack8,       VecUnary,    1, 1, Any,  false) \
	X(VUnpack8,     VecUnary,    1, 1, Any,  false) \
	X(VUnpack16,    VecUnary,    1, 1, Any,  false) \
	/* Raw signed-word CLIP comparisons: x+/x-/y+/y-/z+/z- in low six bits. */ \
	X(VClip,        VecClip,     2, 2, I32, false) \
	X(VMr32,        VecUnary,    1, 1, Any,  false) \
	X(VMove,        VecLane2,    2, 2, Any,  false) \
	X(VF2I0,        VecConvert,  1, 1, V4I32, false) \
	X(VF2I4,        VecConvert,  1, 1, V4I32, false) \
	X(VF2I12,       VecConvert,  1, 1, V4I32, false) \
	X(VF2I15,       VecConvert,  1, 1, V4I32, false) \
	X(VI2F0,        VecConvert,  1, 1, V4F32, false) \
	X(VI2F4,        VecConvert,  1, 1, V4F32, false) \
	X(VI2F12,       VecConvert,  1, 1, V4F32, false) \
	X(VI2F15,       VecConvert,  1, 1, V4F32, false) \
	X(VDiv,         VecBinary,   2, 2, Any,  false) \
	X(VSqrt,        VecUnary,    1, 1, Any,  false) \
	X(VRSqrt,       VecUnary,    1, 1, Any,  false) \
	X(VWaitQ,       Cycle,       0, 0, Void, false) \
	X(VOpMula,      VecBinary,   2, 2, Any,  false)

	enum class OpKind : u8
	{
		Pseudo,
		Unary,
		Binary,
		Ternary,
		Compare,
		Shift,
		MemLoad,
		MemStore,
		StateRead,
		StateWrite,
		Control,
		Cycle,
		Helper,
		FpuUnary,
		FpuBinary,
		FpuTernary,
		FpuCompare,
		FpuConvert,
		VecUnary,
		VecBinary,
		VecTernary,
		VecCompare,
		VecClip,
		VecConvert,
		VecSplat,
		VecLane,
		VecLane2,
	};

	enum class Op : u16
	{
#define IR_OP_ENUM(name, kind, min_args, max_args, result, term) name,
		IR_OP_LIST(IR_OP_ENUM)
#undef IR_OP_ENUM
		Count,
	};

	struct OpInfo
	{
		OpKind kind;
		u8 min_args;
		u8 max_args;
		Type result;
		bool terminator;
	};

	inline constexpr OpInfo kOpInfo[] = {
#define IR_OP_INFO(name, kind, min_args, max_args, result, term) \
	{OpKind::kind, min_args, max_args, Type::result, term},
		IR_OP_LIST(IR_OP_INFO)
#undef IR_OP_INFO
	};

	static_assert(std::size(kOpInfo) == static_cast<size_t>(Op::Count));

	const char* OpName(Op op);
	bool IsPackedIntegerOp(Op op);
	bool IsPackedLanePermutationOp(Op op);
	inline const OpInfo& Info(Op op) { return kOpInfo[static_cast<size_t>(op)]; }
	inline bool IsTerminator(Op op) { return Info(op).terminator; }

	// Memory spaces carried in Inst::aux for Load/Store ops.
	enum class MemSpace : u8
	{
		Guest = 0, // main RAM / BIOS / scratchpad mapped through the VTLB
		Scratchpad,
		VuMem,
		Mmio,
	};

	// Extra instruction flags. Bits 0-3 are reserved for the memory space.
	enum InstFlags : u16
	{
		IF_MEM_SPACE_MASK = 0x000f,
		IF_FASTMEM = 0x0010, // lowering may use the fastmem path
		IF_SIGN_EXTEND = 0x0020,
		IF_EXACT = 0x0040, // cannot be replaced by a value-equivalent op
		IF_DELAY_SLOT = 0x0080, // instruction occupies a branch delay slot
		IF_WIDE_WRITE = 0x0100, // WriteGpr stores a full 64-bit value as-is
		IF_ANNULLED_DELAY_SLOT = 0x0200, // Resume skips a branch-likely delay slot
	};

	// ------------------------------------------------------------------
	// Instructions, blocks, functions
	// ------------------------------------------------------------------
	struct Inst
	{
		Op op = Op::Nop;
		Type type = Type::Void; // result type
		u32 value = 0;          // defined SSA value (0 = none)
		u32 args[4] = {};       // SSA value ids (Control: block ids in args)
		u64 imm = 0;            // constants, guest addresses, register indexes
		u32 guest_pc = 0;
		u16 aux = 0;
		u8 num_args = 0;

		u32 arg_count() const { return num_args; }
	};

	struct Block
	{
		u32 id = 0;
		u32 guest_start = 0; // guest byte address of the first instruction
		u32 guest_end = 0;   // guest byte address one past the last instruction
		u32 flags = 0;
		std::vector<Inst> insts;
		std::vector<u32> successors;
		std::vector<u32> predecessors;
	};

	struct Function
	{
		std::vector<Block> blocks;          // index = block id - 1
		std::vector<Type> value_types = {Type::Void}; // index = value id, [0] = Void
		std::vector<std::array<u32, 4>> vec_consts;
		u32 entry = 0;
		u32 guest_entry = 0;
		u32 next_value = 1;

		Type ValueType(u32 id) const { return (id < value_types.size()) ? value_types[id] : Type::Void; }
		Block* GetBlock(u32 id) { return (id > 0 && id <= blocks.size()) ? &blocks[id - 1] : nullptr; }
		const Block* GetBlock(u32 id) const { return (id > 0 && id <= blocks.size()) ? &blocks[id - 1] : nullptr; }
	};

	// ------------------------------------------------------------------
	// Builder
	// ------------------------------------------------------------------
	class Builder
	{
	public:
		explicit Builder(Function& fn) : m_fn(&fn) {}

		Function& Fn() { return *m_fn; }
		const Function& Fn() const { return *m_fn; }

		u32 CreateBlock(u32 guest_start);
		Block& BlockAt(u32 id) { return m_fn->blocks[id - 1]; }
		void SetBlock(u32 id) { m_block = id; }
		u32 CurrentBlock() const { return m_block; }

		// Guest PC attached to subsequent instructions until changed. When 0,
		// instructions inherit the current block's start address.
		void SetGuestPc(u32 pc) { m_pc = pc; }
		void SetDelaySlot(bool delay_slot) { m_delay_slot = delay_slot; }
		void SetBlockEnd(u32 guest_end);

		u32 Emit(Op op, Type type, std::initializer_list<u32> args, u64 imm = 0, u16 aux = 0, u32 guest_pc = 0);
		u32 Emit0(Op op, Type type = Type::Void);
		u32 Emit1(Op op, Type type, u32 a, u64 imm = 0, u16 aux = 0);
		u32 Emit2(Op op, Type type, u32 a, u32 b, u64 imm = 0, u16 aux = 0);
		u32 Emit3(Op op, Type type, u32 a, u32 b, u32 c, u64 imm = 0, u16 aux = 0);
		u32 Emit4(Op op, Type type, u32 a, u32 b, u32 c, u32 d, u64 imm = 0, u16 aux = 0);

		u32 ConstI32(u32 v);
		u32 ConstI64(u64 v);
		u32 ConstF32(float v);
		u32 ConstVec(const std::array<u32, 4>& lanes);
		u32 Undef(Type type);

		// Terminators. Targets are block ids; guest target addresses go to imm.
		u32 Jump(u32 target, u32 guest_target = 0);
		u32 Branch(u32 cond, u32 taken, u32 not_taken, u32 guest_target = 0);
		u32 BranchIndirect(u32 address);
		u32 CheckEvents(u32 fallthrough_block);
		u32 Resume(u32 guest_pc);
		u32 Return();
		u32 Trap();

	private:
		u32 AddValue(Type type);
		void AttachSuccessors(Block& block, const Inst& term);
		void AddPredecessor(u32 block_id, u32 pred_id);

		Function* m_fn;
		u32 m_block = 0;
		u32 m_pc = 0;
		bool m_delay_slot = false;
	};

	// ------------------------------------------------------------------
	// Verification and printing
	// ------------------------------------------------------------------
	// Value operands exclude control-flow block ids. Shared by optimizers and
	// liveness analysis; the function must already have passed Verify.
	u32 ValueOperandCount(const Inst& inst);
	// Block-local copy propagation, integer constant folding and dead-value
	// removal. Memory accesses, helpers and architectural writes stay ordered.
	void OptimizeIntegerValues(Function& fn);
	bool Verify(const Function& fn, std::string* error = nullptr);
	void Dump(const Function& fn, std::string& out);
	std::string Dump(const Function& fn);

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	bool RunSelfTests();
#endif
} // namespace ir
