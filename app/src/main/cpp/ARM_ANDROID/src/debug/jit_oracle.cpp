// SPDX-License-Identifier: GPL-3.0+
#include "Common.h"
#include "Memory.h"
#include "OpcodeFamilies.h"
#include "VUmicro.h"
#include "cpuinfo.h"
#include "Gif_Unit.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>
#include <android/log.h>
#include <cerrno>

namespace
{
constexpr u32 NOP_U = 0x000002ff;
constexpr u32 NOP_L = 0x8000033c;
int failures = 0;
int checks = 0;
thread_local std::vector<u8>* gifOutput = nullptr;
thread_local bool gifOverflow = false;

// Little-endian, fixed-width fields only. Version this layout whenever the
// meaning/order changes. No host pointers, ABI-dependent bools or VU pipelines.
struct VU1Capture
{
    u32 magic = 0x31554f56; // VOU1
    u32 version = 1;
    u32 byteSize = sizeof(VU1Capture);
    u32 pc = 0, eeCycle = 0, vuCycle = 0, fbrst = 0, top = 0, itop = 0;
    u32 fpcrLo = 0, fpcrHi = 0, clamp = 0, fixes = 0, speed = 0;
    s32 cycleRate = 0;
    u32 cycleSkip = 0;
    u32 vf[128]{}, vi[128]{}, acc[4]{};
    u8 memory[0x4000]{}, micro[0x4000]{};
};

void ApplyCapture(const VU1Capture& input)
{
    u8* mem = VU1.Mem;
    u8* micro = VU1.Micro;
    std::memset(&VU1, 0, sizeof(VU1));
    VU1.Mem = mem;
    VU1.Micro = micro;
    VU1.idx = 1;
    std::memcpy(VU1.VF, input.vf, sizeof(input.vf));
    std::memcpy(VU1.VI, input.vi, sizeof(input.vi));
    std::memcpy(&VU1.ACC, input.acc, sizeof(input.acc));
    std::memcpy(mem, input.memory, sizeof(input.memory));
    std::memcpy(micro, input.micro, sizeof(input.micro));
    VU1.VI[REG_TPC].UL = input.pc / 8;
    VU1.cycle = input.vuCycle;
    cpuRegs.cycle = input.eeCycle;
    VU0.VI[REG_VPU_STAT].UL = 0x100;
    VU0.VI[REG_FBRST].UL = input.fbrst;
    vif1Regs.top = input.top;
    vif1Regs.itop = input.itop;
    VU1.macflag = VU1.VI[REG_MAC_FLAG].UL;
    VU1.statusflag = VU1.VI[REG_STATUS_FLAG].UL;
    VU1.clipflag = VU1.VI[REG_CLIP_FLAG].UL;
    VU1.q.UL = VU1.pending_q = VU1.VI[REG_Q].UL;
    VU1.p.UL = VU1.pending_p = VU1.VI[REG_P].UL;
    std::fill_n(VU1.micro_macflags, 4, VU1.macflag);
    std::fill_n(VU1.micro_clipflags, 4, VU1.clipflag);
    std::fill_n(VU1.micro_statusflags, 4, VUDenormalizeStatus(VU1.statusflag));
}

void Check(bool pass, const char* name)
{
    ++checks;
    failures += !pass;
    std::printf("%s %s\n", pass ? "PASS" : "FAIL", name);
}

void ClassifierTests()
{
    // A two-tag packet exercises the actual GIF parser used by XGKICK.
    // Interpreter fallback must retain EOP even with the JIT configured on.
    alignas(16) std::array<u8, 0x4000> gifMemory{};
    Gif_Tag::HW_Gif_Tag tag{};
    tag.NLOOP = 1;
    tag.NREG = 1;
    std::memcpy(gifMemory.data(), &tag, sizeof(tag));
    tag.EOP = 1;
    std::memcpy(gifMemory.data() + 32, &tag, sizeof(tag));
    const auto savedConfig = EmuConfig;
    EmuConfig.Cpu.Recompiler.EnableVU1 = true;
    EmuConfig.Gamefixes.XgKickHack = false;
    Check(gifUnit.GetGSPacketSize(GIF_PATH_1, gifMemory.data(), 0, ~0u, false, true) == 32,
        "fallback XGKICK stops at first tag with JIT enabled");
    Check(gifUnit.GetGSPacketSize(GIF_PATH_1, gifMemory.data(), 32, ~0u, false, true) == (0x80000000u | 32),
        "fallback XGKICK preserves EOP with JIT enabled");
    Check(gifUnit.GetGSPacketSize(GIF_PATH_1, gifMemory.data(), 0, ~0u, true, true) == (0x80000000u | 64),
        "fallback XGKICK flush preserves packet size and EOP");
    Check(gifUnit.GetGSPacketSize(GIF_PATH_1, gifMemory.data(), 0, ~0u, true) == 64,
        "fast JIT XGKICK retains plain packet size");
    EmuConfig = savedConfig;
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
    std::vector<u8> gif;
    bool gifOverflowed;
};

Snapshot RunVU(u32 index, bool jit, const std::vector<u32>& program, u32 pc, u64 familyMask = 0, u32 budget = 128, bool resumeState = false, const std::vector<u8>* initialMemory = nullptr, const u32* initialVF = nullptr)
{
    // The generated dispatcher may elide an FPCR write when VU and EE
    // configurations match. Reproduce its EE caller, not the shell FPCR.
    const FPControlRegisterBackup callerFPCR(EmuConfig.Cpu.FPUFPCR);
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
    if (initialVF)
        std::memcpy(vu.VF, initialVF, sizeof(vu.VF));
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
    if (initialMemory)
        std::memcpy(mem, initialMemory->data(), size);
    VU0.VI[REG_VPU_STAT].UL = index ? 0x100 : 1;
    vu.VI[REG_TPC].UL = pc / 8;
    BaseVUmicroCPU* cpu = index ? (jit ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU1) : &CpuIntVU1)
                               : (jit ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU0) : &CpuIntVU0);
    cpu->Reset();
    OpcodeFamilies::SetFamilyMask(index ? OpcodeFamilies::CORE_VU1 : OpcodeFamilies::CORE_VU0, familyMask);
    cpu->SetStartPC(pc);
    const bool fallback = (vu.flags & VUFLAG_INTERPRETER) != 0;
    std::vector<u8> gif;
    gifOutput = &gif;
    gifOverflow = false;
    for (unsigned call = 0; call < 128 && (VU0.VI[REG_VPU_STAT].UL & (index ? 0x100 : 1)); ++call)
        cpu->Execute(budget);
    gifOutput = nullptr;
    return {vu, std::vector<u8>(mem, mem + size), VU0.VI[REG_VPU_STAT].UL, fallback, std::vector<u8>(micro, micro + size), std::move(gif), gifOverflow};
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
    if (expected.gif.size() != actual.gif.size())
        std::printf("DIFF %s GIF length interpreter=%zu jit=%zu\n", name, expected.gif.size(), actual.gif.size());
    same &= expected.gif.size() == actual.gif.size() &&
        bytes("GIF", expected.gif.data(), actual.gif.data(), expected.gif.size());
    same &= !expected.gifOverflowed && !actual.gifOverflowed;
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
            std::snprintf(path, sizeof(path), "artifacts/case-%04d-%s.gif.bin", checks, engine);
            if (FILE* file = std::fopen(path, "wb"))
            {
                std::fwrite(snapshot.gif.data(), 1, snapshot.gif.size(), file);
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
    // VU1 must use its own operand clamp policy, independent of VU0.
    const std::vector<u32> clampProbe = {0x7f800000, NOP_U | 0x80000000,
        NOP_L, (15u << 21) | (3u << 6) | 0x22,
        NOP_L, NOP_U | 0x40000000, NOP_L, NOP_U};
    const auto savedClampOptions = EmuConfig.Cpu.Recompiler;
    for (bool clampVU1 : {false, true})
    {
        EmuConfig.Cpu.Recompiler.vu1Overflow = clampVU1;
        EmuConfig.Cpu.Recompiler.vu0Overflow = false;
        const auto withVU0Off = RunVU(1, false, clampProbe, 0x80);
        EmuConfig.Cpu.Recompiler.vu0Overflow = true;
        const auto withVU0On = RunVU(1, false, clampProbe, 0x80);
        Compare(withVU0Off, withVU0On, clampVU1 ? "VU1 clamp on is independent of VU0" : "VU1 clamp off is independent of VU0");
        Check(withVU0Off.regs.VF[3].UL[3] == (clampVU1 ? 0x7f7fffFFu : 0x7f800000u),
            "VU1 operand uses selected overflow policy");
    }
    EmuConfig.Cpu.Recompiler = savedClampOptions;
    for (u32 vu = 0; vu < 2; ++vu)
    {
        EmuConfig.Cpu.Recompiler.vu0Overflow = false;
        EmuConfig.Cpu.Recompiler.vu1Overflow = false;
        for (u32 operand : {0x7f800000u, 0xff800000u, 0x7fc00000u})
        {
            auto program = clampProbe;
            program[0] = operand;
            char name[80];
            std::snprintf(name, sizeof(name), "VU%u unclamped ADDi flags operand=%08x", vu, operand);
            Compare(RunVU(vu, false, program, 0x80), RunVU(vu, true, program, 0x80), name);
        }
    }
    EmuConfig.Cpu.Recompiler = savedClampOptions;
    for (u32 vu = 0; vu < 2; ++vu)
    {
        EmuConfig.Cpu.Recompiler.vu0Overflow = true;
        EmuConfig.Cpu.Recompiler.vu1Overflow = true;
        for (u32 mask = 1; mask < 16; ++mask)
        {
            std::array<u32, 128> registers{};
            registers[3] = 0x3f800000;
            for (u32 lane = 0; lane < 4; ++lane)
            {
                registers[4 + lane] = std::array<u32, 4>{0x7f800000, 0xff800000, 0x7fc00000, 0x7f7fffff}[lane];
                registers[8 + lane] = 0x40000000;
            }
            const std::vector<u32> program = {NOP_L, (mask << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u,
                NOP_L, NOP_U | 0x40000000, NOP_L, NOP_U};
            char name[80];
            std::snprintf(name, sizeof(name), "VU%u normal ADD clamp mask=%x", vu, mask);
            Compare(RunVU(vu, false, program, 0x80, 0, 128, false, nullptr, registers.data()),
                RunVU(vu, true, program, 0x80, 0, 128, false, nullptr, registers.data()), name);
        }
    }
    EmuConfig.Cpu.Recompiler = savedClampOptions;
    // Real XGKICK opcodes with a two-tag packet, without creating an MTGS
    // thread. Only the final submission is intercepted; VU transfer timing
    // and reads of guest packet memory still execute in each engine.
    std::vector<u8> packetMemory(0x4000);
    Gif_Tag::HW_Gif_Tag tag{};
    tag.NLOOP = 1;
    tag.NREG = 1;
    std::memcpy(packetMemory.data() + 16, &tag, sizeof(tag));
    tag.EOP = 1;
    std::memcpy(packetMemory.data() + 48, &tag, sizeof(tag));
    const std::vector<u32> kick = {0x800006fcu | (1u << 11), NOP_U,
        NOP_L, NOP_U, NOP_L, NOP_U | 0x40000000, NOP_L, NOP_U};
    const auto kickInt = RunVU(1, false, kick, 0x80, 0, 128, false, &packetMemory);
    Check(kickInt.gif.size() == 64 && !kickInt.gifOverflowed, "interpreter XGKICK emits exactly two tags");
    Compare(kickInt, RunVU(1, true, kick, 0x80, 0, 128, false, &packetMemory), "VU1 XGKICK JIT packet bytes");
    Compare(kickInt, RunVU(1, true, kick, 0x80, 1ull << 9, 1, false, &packetMemory), "VU1 XGKICK fallback packet bytes");
    std::vector<u32> flaggedKick = {NOP_L, (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x2cu,
        NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U};
    flaggedKick.insert(flaggedKick.end(), kick.begin(), kick.end());
    Compare(RunVU(1, false, flaggedKick, 0x80, 0, 128, false, &packetMemory),
        RunVU(1, true, flaggedKick, 0x80, 0, 128, false, &packetMemory), "VU1 STATUS survives XGKICK host call");
    for (u32 vu = 0; vu < 2; ++vu)
    {
        for (u32 padding = 0; padding <= 8; ++padding)
        {
            std::vector<u32> branchFlags = {NOP_L,
                (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x2cu};
            for (u32 i = 0; i < padding; ++i)
                branchFlags.insert(branchFlags.end(), {NOP_L, NOP_U});
            // B skips two pairs after its delay slot, landing on an E-bit
            // NOP. The exit must publish flags produced in the prior block.
            branchFlags.insert(branchFlags.end(), {(0x20u << 25) | 3u, NOP_U,
                NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U,
                NOP_L, NOP_U | 0x40000000, NOP_L, NOP_U});
            char name[80];
            std::snprintf(name, sizeof(name), "VU%u flags across branch to E with %u NOPs", vu, padding);
            Compare(RunVU(vu, false, branchFlags, 0x80), RunVU(vu, true, branchFlags, 0x80), name);
        }
        for (u32 repetitions : {1u, 2u, 4u, 8u})
        {
            std::vector<u32> flagsProgram;
            for (u32 i = 0; i < repetitions; ++i)
            {
                for (u32 upper : {
                    (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x2cu,
                    (15u << 21) | (1u << 16) | (1u << 11) | (3u << 6) | 0x2cu,
                    (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u})
                {
                    flagsProgram.push_back(NOP_L);
                    flagsProgram.push_back(upper);
                }
            }
            flagsProgram.insert(flagsProgram.end(), {NOP_L, NOP_U | 0x40000000, NOP_L, NOP_U});
            const auto expectedFlags = RunVU(vu, false, flagsProgram, 0x80);
            char flagName[80];
            std::snprintf(flagName, sizeof(flagName), "VU%u sticky flags across %u FMAC writes", vu, repetitions * 3);
            Compare(expectedFlags, RunVU(vu, true, flagsProgram, 0x80), flagName);
        }
        // 1 + 3*2^-25 rounds up under nearest, but stays exactly 1 under
        // the configured chop mode. The old shell environment hid this bug
        // because the previous normal-valued corpus used exact sums.
        const std::vector<u32> roundingProbe = {0x33c00000, NOP_U | 0x80000000,
            NOP_L, (15u << 21) | (3u << 6) | 0x22,
            NOP_L, NOP_U | 0x40000000, NOP_L, NOP_U};
        const auto roundingInt = RunVU(vu, false, roundingProbe, 0x80);
        const auto roundingJit = RunVU(vu, true, roundingProbe, 0x80);
        Check(roundingInt.regs.VF[3].UL[3] == 0x3f800000, "ADDi chop rounding has known result");
        Compare(roundingInt, roundingJit, vu ? "VU1 inherited EE FPCR" : "VU0 inherited EE FPCR");
        const auto savedEEFPCR = EmuConfig.Cpu.FPUFPCR;
        EmuConfig.Cpu.FPUFPCR.SetRoundMode(FPRoundMode::Nearest);
        Compare(roundingInt, RunVU(vu, true, roundingProbe, 0x80),
            vu ? "VU1 distinct EE and VU FPCR" : "VU0 distinct EE and VU FPCR");
        EmuConfig.Cpu.FPUFPCR = savedEEFPCR;
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

bool EmuCoreXOracleRecordGif(const u8* data, u32 size)
{
    if (!gifOutput)
        return false;
    constexpr size_t limit = 16 * 1024 * 1024;
    if (size > limit - gifOutput->size())
        gifOverflow = true;
    else
        gifOutput->insert(gifOutput->end(), data, data + size);
    return true;
}

void EmuCoreXOracleCaptureVU1(u32 startPC)
{
    // Called on the EE thread after the previous program finished. MTVU
    // dispatch bypasses this hook; no cross-thread register snapshots.
    static thread_local u32 poll = 0, remaining = 0, stride = 32, position = 0, sequence = 0;
    static thread_local std::string directory;
    if (!remaining)
    {
        if ((++poll & 1023) || EmuFolders::Logs.empty())
            return;
        const std::string request = EmuFolders::Logs + "/vu-oracle.request";
        FILE* file = std::fopen(request.c_str(), "rb");
        if (!file)
            return;
        u32 count = 0, interval = 32;
        const int fields = std::fscanf(file, "%u %u", &count, &interval);
        std::fclose(file);
        std::remove(request.c_str());
        if (fields < 1 || !count || count > 256 || !interval || interval > 65536)
        {
            __android_log_print(ANDROID_LOG_ERROR, "EmuCoreX", "VU oracle: invalid request fields=%d count=%u stride=%u", fields, count, interval);
            return;
        }
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = EmuFolders::Logs + "/vu-oracle-" + std::to_string(getpid()) + "-" + std::to_string(stamp);
        // Android external app storage grants ADB access through ext_data_rw.
        // Owner-only directories make adb pull report an empty capture.
        if (mkdir(directory.c_str(), 0770) != 0)
        {
            __android_log_print(ANDROID_LOG_ERROR, "EmuCoreX", "VU oracle: mkdir %s: %s", directory.c_str(), std::strerror(errno));
            return;
        }
        remaining = count;
        stride = interval;
        position = sequence = 0;
        __android_log_print(ANDROID_LOG_INFO, "EmuCoreX", "VU oracle: capturing %u inputs to %s", count, directory.c_str());
    }
    if (++position % stride)
        return;
    // Canonical replay currently starts with drained pipelines. Never label a
    // suspended interpreter program or pending transfer as a supported input.
    if (VU1.xgkickenable || (VU1.flags & VUFLAG_INTERPRETER))
    {
        __android_log_print(ANDROID_LOG_WARN, "EmuCoreX", "VU oracle: skipped input with pending VU work");
        if (!--remaining)
            __android_log_print(ANDROID_LOG_INFO, "EmuCoreX", "VU oracle: capture finished with skipped inputs");
        return;
    }
    VU1Capture input;
    input.pc = startPC;
    input.eeCycle = cpuRegs.cycle;
    input.vuCycle = VU1.cycle;
    input.fbrst = VU0.VI[REG_FBRST].UL;
    input.top = vif1Regs.top;
    input.itop = vif1Regs.itop;
    input.fpcrLo = static_cast<u32>(EmuConfig.Cpu.VU1FPCR.bitmask);
    input.fpcrHi = static_cast<u32>(EmuConfig.Cpu.VU1FPCR.bitmask >> 32);
    const auto& rec = EmuConfig.Cpu.Recompiler;
    input.clamp = rec.vu1Overflow | (rec.vu1ExtraOverflow << 1) | (rec.vu1SignOverflow << 2) | (rec.vu1Underflow << 3);
    input.fixes = EmuConfig.Gamefixes.XgKickHack | (EmuConfig.Gamefixes.IbitHack << 1) |
        (EmuConfig.Gamefixes.VUSyncHack << 2) | (EmuConfig.Gamefixes.FullVU0SyncHack << 3);
    input.speed = EmuConfig.Speedhacks.vuFlagHack | (EmuConfig.Speedhacks.vu1Instant << 1);
    input.cycleRate = EmuConfig.Speedhacks.EECycleRate;
    input.cycleSkip = EmuConfig.Speedhacks.EECycleSkip;
    std::memcpy(input.vf, VU1.VF, sizeof(input.vf));
    std::memcpy(input.vi, VU1.VI, sizeof(input.vi));
    std::memcpy(input.acc, &VU1.ACC, sizeof(input.acc));
    std::memcpy(input.memory, VU1.Mem, sizeof(input.memory));
    std::memcpy(input.micro, VU1.Micro, sizeof(input.micro));
    const std::string path = directory + "/vu1-" + std::to_string(sequence++) + ".vuo";
    FILE* file = std::fopen(path.c_str(), "wb");
    bool written = file && std::fwrite(&input, sizeof(input), 1, file) == 1;
    if (file && std::fclose(file) != 0)
        written = false;
    if (!written)
    {
        __android_log_print(ANDROID_LOG_ERROR, "EmuCoreX", "VU oracle: failed writing %s: %s", path.c_str(), std::strerror(errno));
        remaining = 0;
        return;
    }
    if (!--remaining)
        __android_log_print(ANDROID_LOG_INFO, "EmuCoreX", "VU oracle: capture finished (%u inputs)", sequence);
}

static int ReplayVU1(const char* path, bool fallback)
{
    failures = checks = 0;
    VU1Capture input;
    FILE* file = std::fopen(path, "rb");
    bool valid = file && std::fread(&input, sizeof(input), 1, file) == 1;
    if (file)
    {
        valid &= std::fgetc(file) == EOF;
        std::fclose(file);
    }
    valid &= input.magic == 0x31554f56 && input.version == 1 && input.byteSize == sizeof(input) &&
        input.pc < 0x4000 && !(input.pc & 7) && input.top < 1024 && input.itop < 1024 &&
        input.clamp < 16 && input.fixes < 16 && input.speed < 4 &&
        input.cycleRate >= -3 && input.cycleRate <= 3 && input.cycleSkip <= 3;
    if (!valid)
    {
        std::fprintf(stderr, "Invalid or unsupported VU1 capture: %s\n", path);
        return 2;
    }
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = input.speed & 1;
    EmuConfig.Speedhacks.vu1Instant = input.speed & 2;
    EmuConfig.Speedhacks.EECycleRate = input.cycleRate;
    EmuConfig.Speedhacks.EECycleSkip = input.cycleSkip;
    EmuConfig.Cpu.VU1FPCR.bitmask = input.fpcrLo | (static_cast<u64>(input.fpcrHi) << 32);
    auto& rec = EmuConfig.Cpu.Recompiler;
    rec.vu1Overflow = input.clamp & 1;
    rec.vu1ExtraOverflow = input.clamp & 2;
    rec.vu1SignOverflow = input.clamp & 4;
    rec.vu1Underflow = input.clamp & 8;
    EmuConfig.Gamefixes.XgKickHack = input.fixes & 1;
    EmuConfig.Gamefixes.IbitHack = input.fixes & 2;
    EmuConfig.Gamefixes.VUSyncHack = input.fixes & 4;
    EmuConfig.Gamefixes.FullVU0SyncHack = input.fixes & 8;
    if (!SysMemory::Allocate())
        return 2;
    CpuVU0 = &CpuMicroVU0;
    CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve();
    CpuMicroVU1.Reserve();
    auto run = [&](bool jit) {
        const FPControlRegisterBackup callerFPCR(EmuConfig.Cpu.FPUFPCR);
        ApplyCapture(input);
        BaseVUmicroCPU* cpu = jit ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU1) : &CpuIntVU1;
        cpu->Reset();
        OpcodeFamilies::SetFamilyMask(OpcodeFamilies::CORE_VU1, jit && fallback ? 1023 : 0);
        cpu->SetStartPC(input.pc);
        const bool routed = (VU1.flags & VUFLAG_INTERPRETER) != 0;
        std::vector<u8> gif;
        gifOutput = &gif;
        gifOverflow = false;
        for (u32 call = 0; call < 1024 && (VU0.VI[REG_VPU_STAT].UL & 0x100); ++call)
            cpu->Execute(128);
        gifOutput = nullptr;
        return Snapshot{VU1, std::vector<u8>(VU1.Mem, VU1.Mem + 0x4000),
            VU0.VI[REG_VPU_STAT].UL, routed, std::vector<u8>(VU1.Micro, VU1.Micro + 0x4000), std::move(gif), gifOverflow};
    };
    const auto expected = run(false);
    const auto actual = run(true);
    std::printf("INPUT pc=%04x clamp=%u fixes=%u speed=%u GIF interpreter=%zu jit=%zu\n",
        input.pc, input.clamp, input.fixes, input.speed, expected.gif.size(), actual.gif.size());
    Compare(expected, actual, "captured VU1 program");
    if (fallback)
        Check(actual.fallback, "captured input used interpreter fallback");
    std::printf("RESULT checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}

extern "C" __attribute__((visibility("default"))) int EmuCoreXReplayVU1(const char* path)
{
    return ReplayVU1(path, false);
}

extern "C" __attribute__((visibility("default"))) int EmuCoreXReplayVU1Fallback(const char* path)
{
    return ReplayVU1(path, true);
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
