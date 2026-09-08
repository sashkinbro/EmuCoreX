// SPDX-License-Identifier: GPL-3.0+
#include "Common.h"
#include "Memory.h"
#include "OpcodeFamilies.h"
#include "VUmicro.h"
#include "cpuinfo.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
constexpr u32 NOP_U = 0x000002ff;
constexpr u32 NOP_L = 0x8000033c;
int failures = 0;
int checks = 0;

void Check(bool pass, const char* name)
{
    ++checks;
    failures += !pass;
    std::printf("%s %s\n", pass ? "PASS" : "FAIL", name);
}

void ClassifierTests()
{
    using namespace OpcodeFamilies;
    for (u32 op : {0x20u, 0x21u, 0x24u, 0x25u, 0x28u, 0x29u, 0x2cu, 0x2du, 0x2eu, 0x2fu})
    {
        char name[64];
        std::snprintf(name, sizeof(name), "lower branch primary=%02x", op);
        Check(VUClassify(op << 25, true) == VU::FAM_LOWER_BRANCH, name);
    }
    Check(VUClassify(NOP_U, false) == VU::FAM_UPPER_MISC, "upper NOP");
    Check(VUClassify(NOP_L, true) == VU::FAM_LOWER_ALU, "lower NOP/MOVE");
    bool roundtrip = true;
    for (u32 status = 0; status < 4096; ++status)
    {
        const u32 packed = VUDenormalizeStatus(status);
        const u32 actual = (bool(packed & 0x0f00) << 0) | (bool(packed & 0xf000) << 1) |
            (bool(packed & 0x000f) << 6) | (bool(packed & 0x00f0) << 7) | ((packed & 0xffff0000) >> 14);
        roundtrip &= actual == status;
    }
    Check(roundtrip, "all 4096 architectural STATUS encodings roundtrip");
    std::array<u32, 1024> micro{};
    auto reset = [&]() {
        for (u32 i = 0; i < micro.size(); i += 2)
        {
            micro[i] = NOP_L;
            micro[i + 1] = NOP_U;
        }
        SetFamilyMask(CORE_VU0, 1ull << VU::FAM_UPPER_MATH);
    };
    auto scan = [&](u32 pc = 0) { return VURegionShouldInterpret(CORE_VU0, pc, 4096, 1023, micro.data()); };
    reset();
    micro[1] |= 0x40000000;
    micro[3] = 0x28;
    Check(scan(), "E delay slot is included");
    reset();
    micro[1] |= 0x40000000;
    micro[5] = 0x28;
    Check(!scan(), "unreachable instruction after E delay excluded");
    reset();
    micro[0] = 0x28;
    micro[1] |= 0xc0000000;
    SetFamilyMask(CORE_VU0, 1ull << VU::FAM_LOWER_LOADSTORE);
    Check(!scan(), "I-bit lower immediate is not an opcode");
    reset();
    micro[0] = (0x20u << 25) | 199;
    micro[401] = 0x40000028;
    Check(scan(), "direct branch target beyond old 128-pair window");
    reset();
    micro[0] = (0x24u << 25);
    micro[901] = 0x28;
    Check(scan(), "indirect target conservatively covers memory");
    reset();
    micro[1023] = NOP_U | 0x40000000;
    micro[1] = 0x28;
    Check(scan(4088), "E delay slot wraps micro memory");
    reset();
    Check(!scan(), "cyclic NOP memory terminates scan");
    SetFamilyMask(CORE_VU0, 0);
}

struct Snapshot
{
    VURegs regs;
    std::vector<u8> memory;
    u32 busy;
    bool fallback;
    std::vector<u8> micro;
};

Snapshot RunVU(u32 index, bool jit, const std::vector<u32>& program, u32 pc, u64 familyMask = 0, u32 budget = 128, bool resumeState = false)
{
    VURegs& vu = index ? VU1 : VU0;
    u8* const micro = vu.Micro;
    u8* const mem = vu.Mem;
    if (!resumeState)
    {
        std::memset(&vu, 0, sizeof(vu));
        vu.Micro = micro;
        vu.Mem = mem;
        vu.idx = index;
        vu.VF[0].f.w = 1.0f;
        for (u32 r = 1; r < 32; ++r)
            for (u32 lane = 0; lane < 4; ++lane)
                vu.VF[r].UL[lane] = 0x3f800000 + (r * 4 + lane) * 0x1000;
        for (u32 r = 1; r < 16; ++r)
            vu.VI[r].UL = r;
    }
    const u32 size = index ? 0x4000 : 0x1000;
    for (u32 i = 0; i < size; i += 8)
    {
        std::memcpy(micro + i, &NOP_L, 4);
        std::memcpy(micro + i + 4, &NOP_U, 4);
    }
    std::memcpy(micro + pc, program.data(), program.size() * sizeof(u32));
    if (!resumeState)
        for (u32 i = 0; i < size; ++i)
            mem[i] = static_cast<u8>(i * 13 + 7);
    VU0.VI[REG_VPU_STAT].UL = index ? 0x100 : 1;
    vu.VI[REG_TPC].UL = pc / 8;
    BaseVUmicroCPU* cpu = index ? (jit ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU1) : &CpuIntVU1)
                               : (jit ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU0) : &CpuIntVU0);
    cpu->Reset();
    OpcodeFamilies::SetFamilyMask(index ? OpcodeFamilies::CORE_VU1 : OpcodeFamilies::CORE_VU0, familyMask);
    cpu->SetStartPC(pc);
    const bool fallback = (vu.flags & VUFLAG_INTERPRETER) != 0;
    for (unsigned call = 0; call < 128 && (VU0.VI[REG_VPU_STAT].UL & (index ? 0x100 : 1)); ++call)
        cpu->Execute(budget);
    return {vu, std::vector<u8>(mem, mem + size), VU0.VI[REG_VPU_STAT].UL, fallback, std::vector<u8>(micro, micro + size)};
}

void Compare(const Snapshot& expected, const Snapshot& actual, const char* name)
{
    auto bytes = [name](const char* field, const void* a, const void* b, size_t size) {
        const auto* x = static_cast<const u8*>(a);
        const auto* y = static_cast<const u8*>(b);
        for (size_t i = 0; i < size; ++i)
            if (x[i] != y[i])
            {
                std::printf("DIFF %s %s byte=%zu interpreter=%02x jit=%02x\n", name, field, i, x[i], y[i]);
                return false;
            }
        return true;
    };
    bool same = bytes("VF", expected.regs.VF, actual.regs.VF, sizeof(expected.regs.VF));
    same &= bytes("VI", expected.regs.VI, actual.regs.VI, sizeof(expected.regs.VI));
    same &= bytes("ACC", &expected.regs.ACC, &actual.regs.ACC, sizeof(expected.regs.ACC));
    same &= bytes("memory", expected.memory.data(), actual.memory.data(), expected.memory.size());
    same &= expected.busy == 0 && actual.busy == 0;
    std::printf("CYCLES %s interpreter=%u jit=%u\n", name, expected.regs.cycle, actual.regs.cycle);
    if (!same)
    {
        auto dump = [](const Snapshot& snapshot, const char* engine) {
            char path[96];
            std::snprintf(path, sizeof(path), "artifacts/case-%04d-%s.bin", checks, engine);
            if (FILE* file = std::fopen(path, "wb"))
            {
                // Canonical architecture layout, no host pointers or struct padding.
                std::fwrite(snapshot.regs.VF, 1, sizeof(snapshot.regs.VF), file);
                std::fwrite(snapshot.regs.VI, 1, sizeof(snapshot.regs.VI), file);
                std::fwrite(&snapshot.regs.ACC, 1, sizeof(snapshot.regs.ACC), file);
                std::fwrite(snapshot.memory.data(), 1, snapshot.memory.size(), file);
                std::fwrite(snapshot.micro.data(), 1, snapshot.micro.size(), file);
                std::fclose(file);
            }
        };
        dump(expected, "interpreter");
        dump(actual, "jit");
    }
    Check(same, name);
}

void VUTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    EmuConfig.Speedhacks.EECycleRate = 0;
    EmuConfig.Speedhacks.EECycleSkip = 0;
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate emulator memory");
        return;
    }
    CpuVU0 = &CpuMicroVU0;
    CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve();
    CpuMicroVU1.Reserve();
    for (u32 vu = 0; vu < 2; ++vu)
    {
        for (u32 op : {0x28u, 0x2cu, 0x2au, 0x2bu, 0x2fu})
        {
            const u32 upper = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | op;
            const std::vector<u32> program = {NOP_L, upper, NOP_L, NOP_U | 0x40000000, NOP_L, NOP_U};
            char name[64];
            std::snprintf(name, sizeof(name), "VU%u upper=%02x pc=0080", vu, op);
            const auto expected = RunVU(vu, false, program, 0x80);
            const auto actual = RunVU(vu, true, program, 0x80);
            Compare(expected, actual, name);
            const auto fallback = RunVU(vu, true, program, 0x80, 1, 1);
            std::snprintf(name, sizeof(name), "VU%u upper=%02x fallback pc=0080 budget=1", vu, op);
            Compare(expected, fallback, name);
            Check(fallback.fallback, "disabled family actually selected interpreter");
            Check(expected.regs.cycle == fallback.regs.cycle, "fallback accounts cycles exactly once");
        }
        struct FamilyCase { u32 family; u32 lower; u32 upper; };
        const FamilyCase cases[] = {
            {1, NOP_L, (15u << 21) | (2u << 16) | (1u << 11) | 0x3cu}, // ADDAx
            {2, NOP_L, (15u << 21) | (3u << 16) | (1u << 11) | 0x13cu}, // ITOF0
            {3, NOP_L, NOP_U},
            {4, (8u << 25) | (3u << 16) | (1u << 11) | 17u, NOP_U}, // IADDIU
            {5, (0x20u << 25) | 1u, NOP_U}, // B + delay
            {6, (15u << 21) | (3u << 16) | (1u << 11), NOP_U}, // LQ
            {7, 0x800003bcu | (2u << 16) | (1u << 11), NOP_U}, // DIV
            {8, 0x8000073cu | (1u << 11), NOP_U}, // ESADD
            {9, 0x800006bcu | (3u << 16), NOP_U}, // XTOP
        };
        for (const auto& c : cases)
        {
            if (!vu && c.family >= 8)
                continue; // EFU/XTOP are VU1 instructions.
            const std::vector<u32> program = {c.lower, c.upper, NOP_L, NOP_U, NOP_L, NOP_U | 0x40000000, NOP_L, NOP_U};
            const auto expected = RunVU(vu, false, program, 0x80);
            const auto fallback = RunVU(vu, true, program, 0x80, 1ull << c.family, 1);
            char name[80];
            std::snprintf(name, sizeof(name), "VU%u family=%u fallback budget=1", vu, c.family);
            Compare(expected, fallback, name);
            Check(fallback.fallback, "family routing confirmed");
            Check(expected.regs.cycle == fallback.regs.cycle, "family fallback cycle equality");
        }
        // Engine transitions must preserve sticky and current flags. A NOP
        // program after SUB observes the flags produced by the previous engine.
        const std::vector<u32> subtract = {NOP_L, (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x2cu,
            NOP_L, NOP_U | 0x40000000, NOP_L, NOP_U};
        const std::vector<u32> nop = {NOP_L, NOP_U | 0x40000000, NOP_L, NOP_U};
        RunVU(vu, false, subtract, 0x80);
        const auto reference = RunVU(vu, false, nop, 0x100, 0, 128, true);
        RunVU(vu, true, subtract, 0x80, 1);
        const auto toJit = RunVU(vu, true, nop, 0x100, 0, 128, true);
        Compare(reference, toJit, vu ? "VU1 interpreter to JIT" : "VU0 interpreter to JIT");
        RunVU(vu, true, subtract, 0x80);
        const auto toInterpreter = RunVU(vu, true, nop, 0x100, 1ull << 3, 128, true);
        Compare(reference, toInterpreter, vu ? "VU1 JIT to interpreter" : "VU0 JIT to interpreter");
        // Reachable code beyond the former scan window and a branch delay
        // slot containing the excluded operation must run entirely on Int.
        std::vector<u32> distant(406, NOP_U);
        for (size_t i = 0; i < distant.size(); i += 2)
            distant[i] = NOP_L;
        distant[0] = (0x20u << 25) | 199u;
        distant[401] = subtract[1];
        distant[403] = NOP_U | 0x40000000;
        const auto branchReference = RunVU(vu, false, distant, 0x80);
        const auto branchFallback = RunVU(vu, true, distant, 0x80, 1, 1);
        Compare(branchReference, branchFallback, vu ? "VU1 distant branch fallback" : "VU0 distant branch fallback");
        Check(branchFallback.fallback, "distant branch selected interpreter at entry");
        if (!vu)
        {
            auto mbit = subtract;
            mbit[1] |= 0x20000000;
            const auto mExpected = RunVU(0, false, mbit, 0x80);
            const auto mActual = RunVU(0, true, mbit, 0x80, 1, 1);
            Compare(mExpected, mActual, "VU0 M-bit interpreter ownership across yields");
        }
    }
}
}

extern "C" __attribute__((visibility("default"))) int EmuCoreXRunJitOracle(const char* suite)
{
    failures = checks = 0;
    if (std::strcmp(suite, "classifier") == 0)
        ClassifierTests();
    else if (std::strcmp(suite, "vu") == 0)
        VUTests();
    else
        return 2;
    std::printf("RESULT checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
