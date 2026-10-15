// SPDX-License-Identifier: GPL-3.0+
// Common ARM64 scalar-immediate lowering oracle. Included from
// jit_oracle.cpp after the IR and EE test helpers are declared.

namespace
{
static bool IrTestIsLogicalImmediate(u64 value, u32 width)
{
	const u64 width_mask = width == 32 ? 0xffffffffull : ~0ull;
	value &= width_mask;
	for (u32 element = 2; element <= width; element <<= 1)
	{
		const u64 element_mask = element == 64 ? ~0ull : ((1ull << element) - 1);
		const u64 pattern = value & element_mask;
		u64 repeated = 0;
		for (u32 shift = 0; shift < width; shift += element)
			repeated |= pattern << shift;
		if ((repeated & width_mask) != value || pattern == 0 || pattern == element_mask)
			continue;
		for (u32 ones = 1; ones < element; ++ones)
		{
			const u64 run = (1ull << ones) - 1;
			for (u32 rotation = 0; rotation < element; ++rotation)
			{
				const u64 rotated = rotation == 0 ? run :
					((run >> rotation) | (run << (element - rotation))) & element_mask;
				if (rotated == pattern)
					return true;
			}
		}
	}
	return false;
}

static bool IrTestIsAddSubImmediate(u64 value, u32 width)
{
	const u64 width_mask = width == 32 ? 0xffffffffull : ~0ull;
	value &= width_mask;
	const auto encodable = [](u64 imm) {
		return imm <= 4095 || ((imm & 0xfffull) == 0 && (imm >> 12) <= 4095);
	};
	return encodable(value) || encodable((0ull - value) & width_mask);
}

static u32 IrTestCountLogicalImmediates(const Arm64Ir::LowerOutput& out, ir::Op op, u32 width)
{
	const u32 expected_opc = op == ir::Op::And ? 0u : op == ir::Op::Or ? 1u : 2u;
	u32 count = 0;
	for (u32 offset = 0; offset + sizeof(u32) <= out.host_size; offset += sizeof(u32))
	{
		u32 instruction = 0;
		std::memcpy(&instruction, static_cast<const u8*>(out.entry) + offset, sizeof(instruction));
		if ((instruction & 0x1f800000u) == 0x12000000u &&
			((instruction >> 29) & 3u) == expected_opc && (instruction >> 31) == (width == 64))
			++count;
	}
	return count;
}

static u32 IrTestCountAddSubImmediates(const Arm64Ir::LowerOutput& out, u32 width)
{
	u32 count = 0;
	for (u32 offset = 0; offset + sizeof(u32) <= out.host_size; offset += sizeof(u32))
	{
		u32 instruction = 0;
		std::memcpy(&instruction, static_cast<const u8*>(out.entry) + offset, sizeof(instruction));
		const u32 rn = (instruction >> 5) & 31u;
		const u32 rd = instruction & 31u;
		if ((instruction & 0x1f000000u) == 0x11000000u &&
			((instruction >> 29) & 1u) == 0 && (instruction >> 31) == (width == 64) &&
			rn != 31 && rd != 31 && !(rn == 19 && rd == 19))
			++count;
	}
	return count;
}

static ir::Function IrTestScalarImmediateFixture(ir::Op op, ir::Type type, u64 constant,
	bool constant_left, bool alias_destination)
{
	ir::Function fn;
	ir::Builder b(fn);
	b.CreateBlock(EE_TEST_PC);
	const u32 source = b.Emit(ir::Op::ReadGpr, type, {}, 8);
	const u32 imm = type == ir::Type::I32 ? b.ConstI32(static_cast<u32>(constant)) : b.ConstI64(constant);
	const u32 value = constant_left ? b.Emit2(op, type, imm, source) : b.Emit2(op, type, source, imm);
	b.Emit1(ir::Op::WriteGpr, ir::Type::Void, value, alias_destination ? 8 : 9,
		type == ir::Type::I64 ? ir::IF_WIDE_WRITE : 0);
	b.Return();
	return fn;
}

struct IrScalarImmediateResult
{
	bool lowered = false;
	bool encoding_ok = false;
	bool result_ok = false;
	u32 immediate_count = 0;
	u32 host_bytes = 0;
	u64 expected = 0;
	u64 actual = 0;
};

static IrScalarImmediateResult IrTestRunScalarImmediate(ir::Op op, ir::Type type, u64 constant,
	bool constant_left, bool alias_destination, bool expect_immediate)
{
	IrScalarImmediateResult result;
	const u32 width = type == ir::Type::I32 ? 32 : 64;
	const u64 width_mask = width == 32 ? 0xffffffffull : ~0ull;
	const u64 lhs = width == 32 ? 0xf0e1d2c3u : 0xf0e1d2c3b4a59687ull;
	const u64 rhs = constant & width_mask;
	const u64 left = constant_left ? rhs : lhs;
	const u64 right = constant_left ? lhs : rhs;
	switch (op)
	{
		case ir::Op::And: result.expected = left & right; break;
		case ir::Op::Or: result.expected = left | right; break;
		case ir::Op::Xor: result.expected = left ^ right; break;
		case ir::Op::Add: result.expected = left + right; break;
		case ir::Op::Sub: result.expected = left - right; break;
		default: return result;
	}
	result.expected &= width_mask;
	const u32 destination = alias_destination ? 8 : 9;
	const u64 initial_destination = 0xdeadbeef89abcdefull;
	ir::Function fn = IrTestScalarImmediateFixture(op, type, constant, constant_left, alias_destination);
	Arm64Ir::LowerOptions options;
	options.optimize_ir = true;
	options.materialize_constants = true;
	options.allocate_registers = true;
	options.reuse_spill_slots = true;
	Arm64Ir::LowerOutput out{};
	std::string error;
	const bool lowered = Arm64Ir::LowerBlock(fn, options, SysMemory::GetEERec() + 0x2000000,
		0x10000, &out, &error);
	if (!lowered || !out.entry || !out.host_size)
		return result;
	result.lowered = true;
	result.host_bytes = out.host_size;
	result.immediate_count = (op == ir::Op::And || op == ir::Op::Or || op == ir::Op::Xor) ?
		IrTestCountLogicalImmediates(out, op, width) : IrTestCountAddSubImmediates(out, width);
	result.encoding_ok = result.immediate_count == (expect_immediate ? 1u : 0u);
	std::memset(&cpuRegs, 0, sizeof(cpuRegs));
	cpuRegs.GPR.r[8].UD[0] = lhs;
	if (destination != 8)
		cpuRegs.GPR.r[destination].UD[0] = initial_destination;
	cpuRegs.pc = EE_TEST_PC;
	reinterpret_cast<void (*)()>(out.entry)();
	result.expected = width == 32 ?
		static_cast<u64>(static_cast<s64>(static_cast<s32>(static_cast<u32>(result.expected)))) : result.expected;
	result.actual = cpuRegs.GPR.r[destination].UD[0];
	result.result_ok = result.actual == result.expected &&
		(alias_destination || cpuRegs.GPR.r[8].UD[0] == lhs);
	return result;
}

static bool IrTestRunLogicalSpillKernel()
{
	constexpr u32 kInputCount = 30;
	constexpr u64 kMask = 0x00ff00ff00ff00ffull;
	ir::Function fn;
	ir::Builder b(fn);
	b.CreateBlock(EE_TEST_PC);
	std::array<u32, kInputCount> results{};
	for (u32 index = 0; index < kInputCount; ++index)
	{
		const u32 source = b.Emit(ir::Op::ReadGpr, ir::Type::I64, {}, index + 1);
		const u32 constant = b.ConstI64(kMask);
		results[index] = b.Emit2(ir::Op::Xor, ir::Type::I64,
			(index & 1) ? source : constant, (index & 1) ? constant : source);
	}
	u32 combined = results[0];
	for (u32 index = 1; index < kInputCount; ++index)
		combined = b.Emit2(ir::Op::Xor, ir::Type::I64, combined, results[index]);
	b.Emit1(ir::Op::WriteGpr, ir::Type::Void, combined, 31, ir::IF_WIDE_WRITE);
	b.Return();
	Arm64Ir::LowerOptions options;
	options.optimize_ir = true;
	options.materialize_constants = true;
	options.allocate_registers = true;
	options.reuse_spill_slots = true;
	Arm64Ir::LowerOutput out{};
	std::string error;
	if (!Arm64Ir::LowerBlock(fn, options, SysMemory::GetEERec() + 0x2000000, 0x10000, &out, &error) ||
		!out.entry || !out.host_size)
		return false;
	std::memset(&cpuRegs, 0, sizeof(cpuRegs));
	u64 expected = 0;
	for (u32 index = 0; index < kInputCount; ++index)
	{
		const u64 input = 0xfedcba9876543210ull ^ (0x0102040810204080ull * (index + 1));
		cpuRegs.GPR.r[index + 1].UD[0] = input;
		expected ^= input ^ kMask;
	}
	reinterpret_cast<void (*)()>(out.entry)();
	return out.spill_values > 0 && IrTestCountLogicalImmediates(out, ir::Op::Xor, 64) == kInputCount &&
		cpuRegs.GPR.r[31].UD[0] == expected;
}
} // namespace

void CommonLogicalImmediateTests()
{
	struct MaskCase { const char* name; u64 value; };
	constexpr std::array<MaskCase, 9> masks = {{
		{"single-bit", 0x8000000000000000ull},
		{"low-run", 0x00000000000001feull},
		{"rotated-run", 0xf800000000000007ull},
		{"repeated-byte-run", 0x00ff00ff00ff00ffull},
		{"alternating-nibbles", 0xf0f0f0f0f0f0f0f0ull},
		{"zero", 0x0000000000000000ull},
		{"all-ones", 0xffffffffffffffffull},
		{"irregular-32", 0x0000000012345678ull},
		{"irregular-64", 0x123456789abcdef0ull}}};
	constexpr ir::Op logical_ops[] = {ir::Op::And, ir::Op::Or, ir::Op::Xor};
	constexpr const char* logical_names[] = {"AND", "ORR", "EOR"};
	for (u32 op_index = 0; op_index < std::size(logical_ops); ++op_index)
	{
		const ir::Op op = logical_ops[op_index];
		const char* const op_name = logical_names[op_index];
		for (ir::Type type : {ir::Type::I32, ir::Type::I64})
		{
			const u32 width = type == ir::Type::I32 ? 32 : 64;
			for (const MaskCase& mask : masks)
			for (bool constant_left : {false, true})
			for (bool alias_destination : {false, true})
			{
				const u64 value = width == 32 ? static_cast<u32>(mask.value) : mask.value;
				const bool encodable = IrTestIsLogicalImmediate(value, width);
				const IrScalarImmediateResult result = IrTestRunScalarImmediate(op, type, value, constant_left,
					alias_destination, encodable);
				char label[192];
				std::snprintf(label, sizeof(label), "common %s I%u mask=%s const_left=%u alias=%u semantic expected=%016llx actual=%016llx",
					op_name, width, mask.name, constant_left, alias_destination,
					static_cast<unsigned long long>(result.expected), static_cast<unsigned long long>(result.actual));
				Check(result.lowered && result.result_ok, label);
				std::snprintf(label, sizeof(label), "common %s I%u mask=%s const_left=%u alias=%u immediate count expected=%u actual=%u",
					op_name, width, mask.name, constant_left, alias_destination,
					encodable ? 1u : 0u, result.immediate_count);
				Check(result.lowered && result.encoding_ok, label);
				if (!constant_left && !alias_destination)
					std::printf("IRIMMEDIATE_NATIVE op=%s width=%u mask=%s bytes=%u\n",
						op_name, width, mask.name, result.host_bytes);
			}
		}
	}

	// Include the shifted-12 A64 add/sub immediate form alongside the existing
	// low-12 path, plus values immediately beyond both encodable regions.
	constexpr std::array<u64, 12> arithmetic_constants = {
		0ull, 1ull, 4095ull, 4096ull, 0x123000ull, 0xfff000ull,
		4097ull, 0x1001ull, ~0ull, 0ull - 4095ull, 0ull - 4096ull, 0ull - 0x123000ull};
	for (ir::Op op : {ir::Op::Add, ir::Op::Sub})
	for (ir::Type type : {ir::Type::I32, ir::Type::I64})
	{
		const u32 width = type == ir::Type::I32 ? 32 : 64;
		const u64 width_mask = width == 32 ? 0xffffffffull : ~0ull;
		for (u64 constant : arithmetic_constants)
		{
			const u64 narrowed = constant & width_mask;
			for (bool constant_left : {false, true})
			{
				const bool immediate_expected = !constant_left &&
					IrTestIsAddSubImmediate(narrowed, width);
				const IrScalarImmediateResult result = IrTestRunScalarImmediate(op, type, narrowed,
					constant_left, false, immediate_expected);
				char label[192];
				std::snprintf(label, sizeof(label), "common %s I%u immediate=%016llx const_left=%u semantic expected=%016llx actual=%016llx",
					op == ir::Op::Add ? "ADD" : "SUB", width,
					static_cast<unsigned long long>(narrowed), constant_left,
					static_cast<unsigned long long>(result.expected), static_cast<unsigned long long>(result.actual));
				Check(result.lowered && result.result_ok, label);
				std::snprintf(label, sizeof(label), "common %s I%u immediate=%016llx const_left=%u encoding expected=%u actual=%u",
					op == ir::Op::Add ? "ADD" : "SUB", width,
					static_cast<unsigned long long>(narrowed), constant_left,
					immediate_expected ? 1u : 0u, result.immediate_count);
				Check(result.lowered && result.encoding_ok, label);
			}
		}
	}
	Check(IrTestRunLogicalSpillKernel(),
		"common I64 logical immediates retain results across register-allocation spills");
}
