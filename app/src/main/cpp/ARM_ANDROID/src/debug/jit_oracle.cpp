// SPDX-License-Identifier: GPL-3.0+
#include "Common.h"
#include "IopMem.h"
#include "Memory.h"
#include "OpcodeFamilies.h"
#include "JitProfiler.h"
#include "R3000A.h"
#include "R5900.h"
#include "VUmicro.h"
#include "cpuinfo.h"
#include "Gif_Unit.h"
#include "GS/GSState.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>
#include <android/log.h>
#include <cerrno>

// Self-test builds only. The differential oracle enables this so it can run
// without initializing SPU2, DEV9 and friends. It must default to off: a debug
// build is a real emulator too, and the event tests are required there.
static bool s_oracleSkipEvents = false;

extern "C" int EmuCoreXOracleSkipEvents()
{
    return s_oracleSkipEvents ? 1 : 0;
}

extern "C" void EmuCoreXOracleSetSkipEvents(int enabled)
{
    s_oracleSkipEvents = enabled != 0;
}

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

thread_local std::string pendingLivePath;
thread_local VU1Capture pendingLiveInput;
thread_local std::vector<u8> pendingLiveGif;
thread_local bool pendingLiveGifOverflow = false;

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

void CheckBits(bool pass, u32 actual, u32 expected, const char* name)
{
    ++checks;
    failures += !pass;
    if (pass)
        std::printf("PASS %s\n", name);
    else
        std::printf("FAIL %s actual=%08x expected=%08x\n", name, actual, expected);
}

void ClassifierTests()
{
    Check(JitProfiler::TestSampleRangeSelection(), "profiler nested opcode range selection");
    // Exercise the real GS parser at every QWC boundary, including completion
    // of a PACKED loop that started in an earlier Transfer call.
    struct PacketState final : GSState
    {
        void Draw() override {}
        u32 Color() const { return m_v.RGBAQ.U32[0]; }
        bool Complete() const { return m_path[3].nloop == 0; }
    };
    alignas(16) std::array<u64, 14> packet{};
    packet[0] = 2ull | (1ull << 15) | (3ull << 60);
    packet[1] = 0xf1full; // NOP, RGBA, NOP; two loops.
    // RGBA occurs at QWC 2 and 5 (tag is QWC 0).
    packet[4] = 0x2200000011ull;
    packet[5] = 0x4400000033ull;
    packet[10] = 0x6600000055ull;
    packet[11] = 0x8800000077ull;
    auto state = std::make_unique<PacketState>();
    const auto* bytes = reinterpret_cast<const u8*>(packet.data());
    state->Transfer<3>(bytes, 7);
    Check(state->Complete() && state->Color() == 0x88776655,
        "GS whole PACKED packet");
    for (u32 split = 1; split < 7; ++split)
    {
        state->Reset(false);
        state->Transfer<3>(bytes, split);
        state->Transfer<3>(bytes + split * 16, 7 - split);
        Check(state->Complete() && state->Color() == 0x88776655,
            "GS split PACKED packet preserves register state");
    }
    state->Reset(false);
    for (u32 qwc = 0; qwc < 7; ++qwc)
        state->Transfer<3>(bytes + qwc * 16, 1);
    Check(state->Complete() && state->Color() == 0x88776655,
        "GS one-QWC fragments complete PACKED loop");

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

Snapshot RunVU(u32 index, bool jit, const std::vector<u32>& program, u32 pc, u64 familyMask = 0, u32 budget = 128, bool resumeState = false, const std::vector<u8>* initialMemory = nullptr, const u32* initialVF = nullptr, const u32* initialACC = nullptr, const u32* initialVI = nullptr)
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
    if (initialVI)
        for (u32 r = 1; r < 16; ++r)
            vu.VI[r].UL = initialVI[r];
    if (initialVF)
        std::memcpy(vu.VF, initialVF, sizeof(vu.VF));
    if (initialACC)
        std::memcpy(&vu.ACC, initialACC, sizeof(vu.ACC));
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
    for (bool clamp0 : {false, true})
    for (bool clamp1 : {false, true})
    for (u32 mask : {1u, 2u, 8u, 15u})
    for (u32 opcode : {0x1bfu, 0xbfu}) // MULAw / MADDAw
    {
        EmuConfig.Cpu.Recompiler.vu0Overflow = clamp0;
        EmuConfig.Cpu.Recompiler.vu1Overflow = clamp1;
        std::array<u32, 128> registers{};
        registers[3] = 0x3f800000;
        std::fill_n(registers.data() + 4, 4, 0xffd23d20);
        const std::vector<u32> program = {NOP_L, (mask << 21) | (1u << 11) | opcode,
            NOP_L, NOP_U | 0x40000000, NOP_L, NOP_U};
        char name[96];
        std::snprintf(name, sizeof(name), "VU1 ACC broadcast op=%x mask=%x independent clamps=%u/%u",
            opcode, mask, clamp0, clamp1);
        Compare(RunVU(1, false, program, 0x80, 0, 128, false, nullptr, registers.data()),
            RunVU(1, true, program, 0x80, 0, 128, false, nullptr, registers.data()), name);
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
        for (u32 value : {0u, 1u, 0x7fffu, 0x8000u, 0x8001u, 0xfffeu, 0xffffu})
        {
            for (u32 other : {0u, value})
            {
                for (u32 op : {0x28u, 0x29u, 0x2cu, 0x2du, 0x2eu, 0x2fu})
                {
                    std::array<u32, 16> vi{};
                    vi[1] = value;
                    vi[2] = other;
                    const std::vector<u32> program = {
                        (op << 25) | (1u << 11) | (2u << 16) | 3u, NOP_U,
                        NOP_L, (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x2cu,
                        (8u << 25) | (3u << 16) | 17u, NOP_U | 0x40000000u,
                        NOP_L, NOP_U,
                        (8u << 25) | (3u << 16) | 23u, NOP_U | 0x40000000u,
                        NOP_L, NOP_U};
                    char name[100];
                    std::snprintf(name, sizeof(name), "VU%u signed branch op=%x value=%04x other=%04x", vu, op, value, other);
                    Compare(RunVU(vu, false, program, 0x80, 0, 128, false, nullptr, nullptr, nullptr, vi.data()),
                        RunVU(vu, true, program, 0x80, 0, 128, false, nullptr, nullptr, nullptr, vi.data()), name);
                }
            }
        }
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
            // The indirect target is compiled at runtime on a cache miss.
            // Its C++ compiler call must preserve the incoming STATUS lanes.
            branchFlags[0] = (8u << 25) | (1u << 16) | (16u + padding + 5u);
            for (u32 op : {0x24u, 0x25u}) // JR / JALR
            {
                branchFlags[(1 + padding) * 2] = (op << 25) | (1u << 11) |
                    (op == 0x25 ? (2u << 16) : 0u);
                std::snprintf(name, sizeof(name), "VU%u STATUS across indirect op=%x padding=%u", vu, op, padding);
                Compare(RunVU(vu, false, branchFlags, 0x80), RunVU(vu, true, branchFlags, 0x80), name);
            }
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

// ---------------------------------------------------------------
// Golden vectors imported from the independent EmuCoreX core.
//
// The expected bits do not come from this core's interpreter or JIT. They are
// the raw sign/magnitude semantics of the VU floating-point unit as modeled
// by the separate EmuCoreX reference core (unit tests and published hardware
// rows). A "golden" failure means both engines of this core agree with each
// other but disagree with the reference; a "JIT vs interpreter" failure means
// the two engines of this core disagree with each other.
// ---------------------------------------------------------------
constexpr u32 UpperOp(u32 primary, u32 mask, u32 fd, u32 fs, u32 ft)
{
    return primary | (fd << 6) | (fs << 11) | (ft << 16) | (mask << 21);
}

struct GoldenVector
{
    const char* name;
    u32 primary; // 0 selects a lower-slot divider op in `lower`
    u32 lower;
    u32 fs;
    u32 ft;
    u32 acc;
    bool usesAcc;
    bool expectQ;
    u32 expected;
};

void VectorsTests()
{
    constexpr u32 MUL = 0x2a, ADD = 0x28, SUB = 0x2c, MADD = 0x29, MSUB = 0x2d;
    constexpr u32 MAX = 0x2b, MINI = 0x2f;
    constexpr u32 DIV = 0x800003bc, SQRT = 0x800003bd, RSQRT = 0x800003be;

    const GoldenVector vectors[] = {
        // Booth-tree ordering and significand asymmetry.
        {"mul_exact_identity", MUL, NOP_L, 0x3f800000, 0x3f800002, 0, false, false, 0x3f800001},
        {"mul_min_normal_by_max", MUL, NOP_L, 0x00800000, 0x7fffffff, 0, false, false, 0x40fffffe},
        {"mul_max_by_min_normal", MUL, NOP_L, 0x7fffffff, 0x00800000, 0, false, false, 0x40ffffff},
        {"mul_carried_one_bit", MUL, NOP_L, 0x3f800400, 0x3f800002, 0, false, false, 0x3f800401},
        {"mul_asym_fs_times_one", MUL, NOP_L, 0x3fffffff, 0x3f800000, 0, false, false, 0x3fffffff},
        {"mul_asym_one_times_ft", MUL, NOP_L, 0x3f800000, 0x3fffffff, 0, false, false, 0x3ffffffe},
        {"mul_ext_finite_square", MUL, NOP_L, 0x7fffffff, 0x7fffffff, 0, false, false, 0x7fffffff},
        {"mul_ext_finite_negate", MUL, NOP_L, 0x7fffffff, 0xffffffff, 0, false, false, 0xffffffff},
        {"mul_subnormal_payload", MUL, NOP_L, 0x007fffff, 0x7fffffff, 0, false, false, 0x00000000},
        {"mul_underflow_flush", MUL, NOP_L, 0x3f080000, 0x00c80000, 0, false, false, 0x00000000},
        {"mul_signed_zero_flush", MUL, NOP_L, 0xbf800000, 0x00000001, 0, false, false, 0x80000000},
        {"mul_half_min_normal", MUL, NOP_L, 0x00800000, 0x3f000000, 0, false, false, 0x00000000},
        {"mul_neg_half_min_normal", MUL, NOP_L, 0x80800000, 0x3f000000, 0, false, false, 0x80000000},
        // Add/subtract, signed-zero underflow flush and extended finite values.
        {"add_basic", ADD, NOP_L, 0x3f800000, 0x40000000, 0, false, false, 0x40400000},
        {"sub_last_significand_bit", SUB, NOP_L, 0x3fffffff, 0x3f800000, 0, false, false, 0x3f7ffffe},
        {"add_subnormal_payload", ADD, NOP_L, 0x00000001, 0x3f800000, 0, false, false, 0x3f800000},
        {"sub_zero_payloads", SUB, NOP_L, 0x80000001, 0x00000001, 0, false, false, 0x80000000},
        {"add_ext_finite_overflow", ADD, NOP_L, 0x7fffffff, 0x7fffffff, 0, false, false, 0x7fffffff},
        {"sub_underflow_flush", SUB, NOP_L, 0x00800001, 0x00800000, 0, false, false, 0x00000000},
        {"sub_underflow_neg_flush", SUB, NOP_L, 0x80800001, 0x80800000, 0, false, false, 0x80000000},
        {"add_ps2float_published", ADD, NOP_L, 0x0a7ffff8, 0x8a800001, 0, false, false, 0x80000000},
        // Accumulator forms use the same ordered-product model.
        {"madd_basic", MADD, NOP_L, 0x40000000, 0x40400000, 0x3f800000, true, false, 0x40e00000},
        {"madd_cancel", MADD, NOP_L, 0xc0800000, 0x3f000000, 0x40000000, true, false, 0x00000000},
        {"msub_basic", MSUB, NOP_L, 0x40000000, 0x40400000, 0x40e00000, true, false, 0x3f800000},
        {"msub_carried_one_bit", MSUB, NOP_L, 0x3f800400, 0x3f800002, 0x40000000, true, false, 0x3f7ff7fe},
        // Raw sign/magnitude selection keeps fraction-bearing zero payloads.
        {"max_basic", MAX, NOP_L, 0x3f800000, 0x40000000, 0, false, false, 0x40000000},
        {"min_basic", MINI, NOP_L, 0x3f800000, 0x40000000, 0, false, false, 0x3f800000},
        {"max_zero_payload", MAX, NOP_L, 0x80000001, 0x007fffff, 0, false, false, 0x007fffff},
        {"min_zero_payload", MINI, NOP_L, 0x007fffff, 0x80000001, 0, false, false, 0x80000001},
        {"max_signed_zero", MAX, NOP_L, 0x80000000, 0x00000000, 0, false, false, 0x00000000},
        {"min_signed_zero", MINI, NOP_L, 0x00000000, 0x80000000, 0, false, false, 0x80000000},
        // Divider unit (Q register), including published 90K-console rows.
        {"div_hw_1_over_3", 0, DIV, 0x3f800000, 0x40400000, 0, false, true, 0x3eaaaaab},
        {"div_hw_1_over_1p5", 0, DIV, 0x3f800000, 0x3fc00000, 0, false, true, 0x3f2aaaab},
        {"div_exact_3_over_2", 0, DIV, 0x40c00000, 0x40000000, 0, false, true, 0x40400000},
        {"div_srt_boundary", 0, DIV, 0x40490fda, 0x3fb504f2, 0, false, true, 0x400e2c19},
        {"div_zero_by_neg_zero", 0, DIV, 0x00000000, 0x80000000, 0, false, true, 0xffffffff},
        {"div_neg_two_by_zero", 0, DIV, 0xc0000000, 0x00000000, 0, false, true, 0xffffffff},
        {"sqrt_four", 0, SQRT, 0, 0x40800000, 0, false, true, 0x40000000},
        {"sqrt_1p5", 0, SQRT, 0, 0x3fc00000, 0, false, true, 0x3f9cc471},
        {"sqrt_near_two", 0, SQRT, 0, 0x3fffffff, 0, false, true, 0x3fb504f3},
        {"sqrt_ext_finite", 0, SQRT, 0, 0x7f800000, 0, false, true, 0x5f800000},
        {"sqrt_max_finite", 0, SQRT, 0, 0x7fffffff, 0, false, true, 0x5fb504f3},
        {"sqrt_negative", 0, SQRT, 0, 0xc0800000, 0, false, true, 0x40000000},
        {"sqrt_neg_ext_finite", 0, SQRT, 0, 0xff800000, 0, false, true, 0x5f800000},
        {"sqrt_payload_1p5", 0, SQRT, 0, 0x7fc00000, 0, false, true, 0x5f9cc471},
        {"rsqrt_exact", 0, RSQRT, 0x40c00000, 0x40800000, 0, false, true, 0x40400000},
        {"rsqrt_hw_2pow64", 0, RSQRT, 0x3f800000, 0x7f800000, 0, false, true, 0x1f800000},
        {"rsqrt_invalid", 0, RSQRT, 0x00000000, 0x80000000, 0, false, true, 0xffffffff},
    };

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
    // Default mirrors Pcsx2Config: VU0 operand clamping on, VU1 off. The other
    // profiles are diagnostics, not shipping configurations.
    struct ClampProfile
    {
        const char* name;
        bool vu0Overflow;
        bool vu1Overflow;
        bool vu0SignOverflow;
        bool vu1SignOverflow;
    };
    const ClampProfile profiles[] = {
        {"default", true, false, false, false},
        {"clamp-off", false, false, false, false},
        {"clamp-on", true, true, false, false},
        {"sign-preserve", true, true, true, true},
    };
    const auto savedClamp = EmuConfig.Cpu.Recompiler;
    for (u32 vu = 0; vu < 2; ++vu)
    {
        for (const ClampProfile& profile : profiles)
        {
            EmuConfig.Cpu.Recompiler.vu0Overflow = profile.vu0Overflow;
            EmuConfig.Cpu.Recompiler.vu1Overflow = profile.vu1Overflow;
            EmuConfig.Cpu.Recompiler.vu0SignOverflow = profile.vu0SignOverflow;
            EmuConfig.Cpu.Recompiler.vu1SignOverflow = profile.vu1SignOverflow;
            for (const GoldenVector& vector : vectors)
            {
                std::array<u32, 128> registers{};
                for (u32 lane = 0; lane < 4; ++lane)
                {
                    registers[0 * 4 + lane] = 0x3f800000;
                    registers[1 * 4 + lane] = vector.fs;
                    registers[2 * 4 + lane] = vector.ft;
                }
                const std::array<u32, 4> acc{vector.acc, vector.acc, vector.acc, vector.acc};
                // Divider ops read vf1/vf2; SQRT only consumes Ft. The operand
                // fields live in the lower word for these instructions.
                u32 lower = vector.lower;
                if (!vector.primary)
                {
                    lower |= (2u << 16);
                    if ((lower & 0xffu) != (SQRT & 0xffu))
                        lower |= (1u << 11);
                }
                // Keep one NOP pair before the E delay slot so the 7..13 cycle
                // divider pipe commits before the program ends in both engines.
                const std::vector<u32> program = {
                    lower,
                    vector.primary ? UpperOp(vector.primary, 0xf, 3, 1, 2) : NOP_U,
                    NOP_L, NOP_U,
                    NOP_L, NOP_U | 0x40000000,
                    NOP_L, NOP_U};
                auto run = [&](bool jit) {
                    return RunVU(vu, jit, program, 0x80, 0, 128, false, nullptr, registers.data(),
                        vector.usesAcc ? acc.data() : nullptr);
                };
                const auto interp = run(false);
                const auto jit = run(true);
                const u32 interpBits = vector.expectQ ? interp.regs.VI[REG_Q].UL : interp.regs.VF[3].UL[3];
                const u32 jitBits = vector.expectQ ? jit.regs.VI[REG_Q].UL : jit.regs.VF[3].UL[3];
                char name[128];
                std::snprintf(name, sizeof(name), "VU%u %s golden interp %s", vu, profile.name, vector.name);
                CheckBits(interpBits == vector.expected, interpBits, vector.expected, name);
                std::snprintf(name, sizeof(name), "VU%u %s JIT vs interpreter %s", vu, profile.name, vector.name);
                Compare(interp, jit, name);
                std::snprintf(name, sizeof(name), "VU%u %s golden JIT %s", vu, profile.name, vector.name);
                CheckBits(jitBits == vector.expected, jitBits, vector.expected, name);
            }
        }
    }
    EmuConfig.Cpu.Recompiler = savedClamp;
}

// ---------------------------------------------------------------
// EE / IOP differential oracle
//
// Both suites execute the same small guest programs on the recompiler and on
// the interpreter and compare every architectural register. Straight-line EE
// programs run for exactly their instruction count on the interpreter and as
// one forced-exit recompiled block on the JIT. IOP programs end in a
// self-branch and run on a shared cycle budget, so both engines stop inside a
// region that does not modify state.
// ---------------------------------------------------------------
constexpr u32 EE_TEST_PC = 0x00100000;
constexpr u32 EE_TEST_SCRATCH = 0x00101000;
constexpr u32 IOP_TEST_PC = 0x00100000;
constexpr u32 IOP_TEST_SCRATCH = 0x00180000;
constexpr u32 EE_SCRATCH_SIZE = 256;
constexpr u32 IOP_SCRATCH_SIZE = 128;

extern "C" void EmuCoreXEEOracleSetCompileCallback(void (*callback)(u32));
extern "C" void EmuCoreXEEForceExitAfterFirstBlock();
extern "C" void EmuCoreXEEForceExitAfterCycles(u32 budget);
extern "C" void EmuCoreXOracleEESteps(u32 steps);

constexpr u32 MipsR(u32 rs, u32 rt, u32 rd, u32 sa, u32 fn)
{
    return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | fn;
}

constexpr u32 MipsI(u32 op, u32 rs, u32 rt, u32 imm)
{
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xffffu);
}

constexpr u32 MipsCop1(u32 rs, u32 ft, u32 fs, u32 fd, u32 fn)
{
    return (0x11u << 26) | (rs << 21) | (ft << 16) | (fs << 11) | (fd << 6) | fn;
}

struct EESnapshot
{
    u64 gpr[32];
    u64 hi, lo;
    u32 pc;
    u32 cycle;
    u32 fpr[32];
    u32 fcr31;
    u32 acc;
    u32 accflag;
    u32 cp0[32];
    u32 vu0_vf[128];
    u32 vu0_vi[32];
    u32 vu0_acc[4];
    u8 scratch[EE_SCRATCH_SIZE];
};

struct IOPSnapshot
{
    u32 gpr[32];
    u32 hi, lo;
    u32 pc;
    u32 cycle;
    u32 cp0[32];
    u32 cp2d[32];
    u32 cp2c[32];
    u8 scratch[IOP_SCRATCH_SIZE];
};

void CaptureEE(EESnapshot& out)
{
    std::memset(&out, 0, sizeof(out));
    for (u32 i = 0; i < 32; ++i)
    {
        out.gpr[i] = cpuRegs.GPR.r[i].UD[0];
        out.fpr[i] = fpuRegs.fpr[i].UL;
        out.cp0[i] = cpuRegs.CP0.r[i];
    }
    out.hi = cpuRegs.HI.UD[0];
    out.lo = cpuRegs.LO.UD[0];
    out.pc = cpuRegs.pc;
    out.cycle = cpuRegs.cycle;
    out.fcr31 = fpuRegs.fprc[31];
    out.acc = fpuRegs.ACC.UL;
    out.accflag = fpuRegs.ACCflag;
    for (u32 vf = 0; vf < 32; ++vf)
        for (u32 lane = 0; lane < 4; ++lane)
            out.vu0_vf[vf * 4 + lane] = VU0.VF[vf].UL[lane];
    for (u32 vi = 0; vi < 32; ++vi)
        out.vu0_vi[vi] = VU0.VI[vi].UL;
    for (u32 lane = 0; lane < 4; ++lane)
        out.vu0_acc[lane] = VU0.ACC.UL[lane];
    for (u32 i = 0; i < EE_SCRATCH_SIZE; ++i)
        out.scratch[i] = memRead8(EE_TEST_SCRATCH + i);
}

void CaptureIOP(IOPSnapshot& out)
{
    std::memset(&out, 0, sizeof(out));
    for (u32 i = 0; i < 32; ++i)
    {
        out.gpr[i] = psxRegs.GPR.r[i];
        out.cp0[i] = psxRegs.CP0.r[i];
    }
    out.hi = psxRegs.GPR.r[32];
    out.lo = psxRegs.GPR.r[33];
    out.pc = psxRegs.pc;
    out.cycle = psxRegs.cycle;
    for (u32 i = 0; i < 32; ++i)
    {
        out.cp2d[i] = psxRegs.CP2D.r[i];
        out.cp2c[i] = psxRegs.CP2C.r[i];
    }
    for (u32 i = 0; i < IOP_SCRATCH_SIZE; ++i)
        out.scratch[i] = iopMemRead8(IOP_TEST_SCRATCH + i);
}

bool CpuDiff(const char* test, const char* field, const void* expected, const void* actual, size_t size)
{
    const u8* a = static_cast<const u8*>(expected);
    const u8* b = static_cast<const u8*>(actual);
    for (size_t i = 0; i < size; ++i)
    {
        if (a[i] != b[i])
        {
            std::printf("DIFF %s %s byte=%zu interp=%02x jit=%02x\n", test, field, i, a[i], b[i]);
            return false;
        }
    }
    return true;
}

void CompareEE(const EESnapshot& expected, const EESnapshot& actual, const char* name)
{
    bool same = CpuDiff(name, "GPR", expected.gpr, actual.gpr, sizeof(expected.gpr));
    same &= CpuDiff(name, "HI", &expected.hi, &actual.hi, sizeof(expected.hi));
    same &= CpuDiff(name, "LO", &expected.lo, &actual.lo, sizeof(expected.lo));
    same &= CpuDiff(name, "PC", &expected.pc, &actual.pc, sizeof(expected.pc));
    same &= CpuDiff(name, "FPR", expected.fpr, actual.fpr, sizeof(expected.fpr));
    same &= CpuDiff(name, "FCR31", &expected.fcr31, &actual.fcr31, sizeof(expected.fcr31));
    same &= CpuDiff(name, "ACC", &expected.acc, &actual.acc, sizeof(expected.acc));
    same &= CpuDiff(name, "ACCflag", &expected.accflag, &actual.accflag, sizeof(expected.accflag));
    for (u32 reg = 0; reg < 32; ++reg)
    {
        if (reg == 1 || reg == 9 || reg == 11)
            continue; // Random, Count and Compare are cycle-relative.
        char field[32];
        std::snprintf(field, sizeof(field), "CP0[%u]", reg);
        same &= CpuDiff(name, field, &expected.cp0[reg], &actual.cp0[reg], sizeof(u32));
    }
    same &= CpuDiff(name, "VU0.VF", expected.vu0_vf, actual.vu0_vf, sizeof(expected.vu0_vf));
    same &= CpuDiff(name, "VU0.VI", expected.vu0_vi, actual.vu0_vi, sizeof(expected.vu0_vi));
    same &= CpuDiff(name, "VU0.ACC", expected.vu0_acc, actual.vu0_acc, sizeof(expected.vu0_acc));
    same &= CpuDiff(name, "scratch", expected.scratch, actual.scratch, sizeof(expected.scratch));
    Check(same, name);
}

void CompareIOP(const IOPSnapshot& expected, const IOPSnapshot& actual, const char* name)
{
    bool same = CpuDiff(name, "GPR", expected.gpr, actual.gpr, sizeof(expected.gpr));
    same &= CpuDiff(name, "HI", &expected.hi, &actual.hi, sizeof(expected.hi));
    same &= CpuDiff(name, "LO", &expected.lo, &actual.lo, sizeof(expected.lo));
    same &= CpuDiff(name, "PC", &expected.pc, &actual.pc, sizeof(expected.pc));
    for (u32 reg = 0; reg < 32; ++reg)
    {
        if (reg == 1 || reg == 9 || reg == 11)
            continue;
        char field[32];
        std::snprintf(field, sizeof(field), "CP0[%u]", reg);
        same &= CpuDiff(name, field, &expected.cp0[reg], &actual.cp0[reg], sizeof(u32));
    }
    same &= CpuDiff(name, "CP2D", expected.cp2d, actual.cp2d, sizeof(expected.cp2d));
    same &= CpuDiff(name, "CP2C", expected.cp2c, actual.cp2c, sizeof(expected.cp2c));
    same &= CpuDiff(name, "scratch", expected.scratch, actual.scratch, sizeof(expected.scratch));
    Check(same, name);
}

EESnapshot RunEEProgram(bool jit, const std::vector<u32>& program, bool multiBlock = false)
{
    std::memset(&cpuRegs, 0, sizeof(cpuRegs));
    std::memset(&fpuRegs, 0, sizeof(fpuRegs));
    std::memset(&VU0.VF, 0, sizeof(VU0.VF));
    std::memset(&VU0.VI, 0, sizeof(VU0.VI));
    std::memset(&VU0.ACC, 0, sizeof(VU0.ACC));
    VU0.q.UL = VU0.p.UL = 0;
    VU0.macflag = VU0.statusflag = VU0.clipflag = 0;
    // A self-branch terminates the recompiled block so the forced exit fires
    // at a branch boundary; the interpreter stops after the same steps.
    std::vector<u32> code = program;
    code.push_back(0x1000ffffu);
    code.push_back(0);
    for (u32 i = 0; i < code.size(); ++i)
        memWrite32(EE_TEST_PC + i * 4, code[i]);
    for (u32 i = 0; i < EE_SCRATCH_SIZE; ++i)
        memWrite8(EE_TEST_SCRATCH + i, static_cast<u8>(i * 13 + 7));
    cpuRegs.pc = EE_TEST_PC;
    cpuRegs.cycle = 0;
    cpuRegs.branch = 0;
    cpuRegs.nextEventCycle = 0x7fffffffu;
    EEsCycle = 0;
    EEoCycle = 0;

    constexpr u32 budget = 4096;
    if (jit)
    {
        Cpu = &recCpu;
        Cpu->Reset();
        if (multiBlock)
            EmuCoreXEEForceExitAfterCycles(budget);
        else
            EmuCoreXEEForceExitAfterFirstBlock();
        Cpu->Execute();
    }
    else
    {
        Cpu = &intCpu;
        Cpu->Reset();
        // Exceptions abort the current step through the interpreter's jmpbuf;
        // the helper arms it exactly like intExecute() does.
        EmuCoreXOracleEESteps(multiBlock ? 40000u : static_cast<u32>(code.size()));
    }

    EESnapshot out;
    CaptureEE(out);
    if (multiBlock)
    {
        // Both engines stop inside the trailing self-branch loop.
        const u32 sentinel = EE_TEST_PC + static_cast<u32>(code.size() - 2) * 4;
        if (out.pc >= sentinel && out.pc < sentinel + 8)
            out.pc = sentinel;
    }
    return out;
}

IOPSnapshot RunIOPProgram(bool jit, const std::vector<u32>& program, s32 eeCycles = -1, bool forceGteInterpreter = false)
{
    std::memset(&psxRegs, 0, sizeof(psxRegs));
    // Deterministic GTE fixtures: both engines must see identical CP2 state.
    for (u32 i = 0; i < 32; ++i)
    {
        psxRegs.CP2D.r[i] = 0x00112233u * (i + 1);
        psxRegs.CP2C.r[i] = 0x44556677u ^ (i * 0x01020304u);
    }
    const u32 sentinel = IOP_TEST_PC + static_cast<u32>(program.size()) * 4;
    for (u32 i = 0; i < program.size(); ++i)
        iopMemWrite32(IOP_TEST_PC + i * 4, program[i]);
    iopMemWrite32(sentinel, 0x1000ffffu); // b .
    iopMemWrite32(sentinel + 4, 0);
    for (u32 i = 0; i < IOP_SCRATCH_SIZE; ++i)
        iopMemWrite8(IOP_TEST_SCRATCH + i, static_cast<u8>(i * 13 + 7));
    psxRegs.pc = IOP_TEST_PC;
    psxRegs.cycle = 0;
    // A zero event cycle feeds the WaitLoop fast path a zero delta and the
    // block budget never drains.
    psxRegs.iopNextEventCycle = 0x7fffffffu;

    const s32 budget = eeCycles > 0 ? eeCycles : static_cast<s32>(program.size() + 16) * 8;
    if (jit)
    {
        OpcodeFamilies::SetFamilyMask(OpcodeFamilies::CORE_IOP,
            forceGteInterpreter ? (1ull << OpcodeFamilies::IOP::FAM_GTE) : 0);
        psxCpu = &psxRec;
        psxRec.Reset();
    }
    else
    {
        psxCpu = &psxInt;
        psxInt.Reset();
    }
    psxCpu->ExecuteBlock(budget);
    OpcodeFamilies::SetFamilyMask(OpcodeFamilies::CORE_IOP, 0);

    IOPSnapshot out;
    CaptureIOP(out);
    if (out.pc >= sentinel && out.pc < sentinel + 8)
        out.pc = sentinel;
    return out;
}

void RunEECase(const char* name, const std::vector<u32>& code, bool multiBlock = false)
{
    const EESnapshot interp = RunEEProgram(false, code, multiBlock);
    const EESnapshot jit = RunEEProgram(true, code, multiBlock);
    CompareEE(interp, jit, name);
}

std::vector<u32> EEValueSetup()
{
    return {
        MipsI(9, 0, 8, 0x8001),                              // t0 = 0xffff8001
        MipsI(15, 0, 9, 0xdead), MipsI(13, 9, 9, 0xbeef),    // t1 = 0xdeadbeef
        MipsI(9, 0, 10, 0x7fff),                             // t2
        MipsI(9, 0, 11, 0xfffe),                             // t3 = -2
        MipsI(15, 0, 12, 0x1234), MipsI(13, 12, 12, 0x5678), // t4
        MipsI(9, 0, 13, 3),                                  // t5
        MipsI(9, 0, 14, 33),                                 // t6
        MipsI(9, 0, 16, 0x00ff),                             // s0
        MipsI(9, 0, 17, 0x0101),                             // s1
    };
}

std::vector<u32> EEScratchSetup()
{
    return {
        MipsI(15, 0, 22, 0x0010), MipsI(13, 22, 22, 0x1000), // s6 = scratch
        MipsI(15, 0, 8, 0x0123), MipsI(13, 8, 8, 0x4567),    // t0
        MipsI(15, 0, 9, 0x89ab), MipsI(13, 9, 9, 0xcdef),    // t1
        MipsI(43, 22, 8, 32), MipsI(43, 22, 9, 36),          // sw patterns
        MipsI(9, 0, 10, 0x55), MipsI(9, 0, 11, 0x66),        // t2, t3
    };
}

std::vector<u32> EECop1Setup()
{
    return {
        MipsI(15, 0, 8, 0x3f80), MipsCop1(4, 8, 1, 0, 0),                            // f1 = 1.0
        MipsI(15, 0, 8, 0x4000), MipsCop1(4, 8, 2, 0, 0),                            // f2 = 2.0
        MipsI(15, 0, 8, 0x3eaa), MipsI(13, 8, 8, 0xaaab), MipsCop1(4, 8, 3, 0, 0),   // f3 = 1/3
        MipsI(15, 0, 8, 0xc000), MipsCop1(4, 8, 4, 0, 0),                            // f4 = -2.0
        MipsI(15, 0, 8, 0x7f7f), MipsI(13, 8, 8, 0xffff), MipsCop1(4, 8, 5, 0, 0),   // f5 = flt max
        MipsI(15, 0, 8, 0x8000), MipsCop1(4, 8, 6, 0, 0),                            // f6 = -0.0
        MipsI(15, 0, 8, 0x4049), MipsI(13, 8, 8, 0x0fdb), MipsCop1(4, 8, 7, 0, 0),   // f7 = pi
        MipsI(9, 0, 8, 1234), MipsCop1(4, 8, 8, 0, 0),                               // f8 = W(1234)
    };
}

// MMI uses a two-level function field: bits 5..0 pick the group, bits 10..6
// the operation inside MMI0..MMI3.
constexpr u32 Mmi(u32 group, u32 sub, u32 rs, u32 rt, u32 rd)
{
    return (0x1Cu << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (sub << 6) | group;
}

u32 eeCompileObservedCycle = 0;
u32 eeCompileCallbackCount = 0;

void EECompileDeadlineCallback(u32 pc)
{
    if (pc == EE_TEST_PC + 32)
    {
        eeCompileObservedCycle = cpuRegs.cycle;
        ++eeCompileCallbackCount;
        cpuRegs.nextEventCycle = 0;
    }
}

void EECompileDeadlineRegression()
{
    for (const u32 initial_cycle : {0x106af500u, 0xfffffff0u})
    {
        Cpu = &recCpu;
        Cpu->Reset();
        std::memset(&cpuRegs, 0, sizeof(cpuRegs));
        const std::array<u32, 11> code = {{
            MipsI(9, 0, 8, 1), MipsI(9, 8, 8, 1),
            MipsI(4, 0, 0, 5), 0, 0, 0, 0, 0,
            MipsI(9, 0, 9, 7), 0x1000ffffu, 0
        }};
        for (u32 i = 0; i < code.size(); ++i)
            memWrite32(EE_TEST_PC + i * 4, code[i]);
        cpuRegs.pc = EE_TEST_PC;
        cpuRegs.cycle = initial_cycle;
        eeCompileObservedCycle = 0;
        eeCompileCallbackCount = 0;
        EmuCoreXEEForceExitAfterCycles(4096);
        EmuCoreXEEOracleSetCompileCallback(EECompileDeadlineCallback);
        Cpu->Execute();
        EmuCoreXEEOracleSetCompileCallback(nullptr);
        const u32 before_callback = eeCompileObservedCycle - initial_cycle;
        const u32 elapsed = cpuRegs.cycle - initial_cycle;
        std::printf("EECOMPILE initial=%08x callback=%08x final=%08x calls=%u\n",
            initial_cycle, eeCompileObservedCycle, cpuRegs.cycle, eeCompileCallbackCount);
        Check(eeCompileCallbackCount == 1 && before_callback > 0 && before_callback < 256,
            "ee compile callback observes preceding block cycles");
        Check(elapsed >= before_callback && elapsed < 256 &&
            cpuRegs.GPR.r[8].UD[0] == 2 && cpuRegs.GPR.r[9].UD[0] == 7,
            "ee compile deadline reset preserves time and execution");
    }
}

void EECoverageSpecial()
{
    char name[96];

    for (u32 fn : {0x20u, 0x21u, 0x22u, 0x23u, 0x24u, 0x25u, 0x26u, 0x27u, 0x2au, 0x2bu})
    {
        auto code = EEValueSetup();
        code.push_back(MipsR(8, 9, 18, 0, fn));
        std::snprintf(name, sizeof(name), "ee special %02x", fn);
        RunEECase(name, code);
    }

    for (u32 fn : {0x2cu, 0x2du, 0x2eu, 0x2fu})
    {
        auto code = EEValueSetup();
        code.push_back(MipsR(8, 9, 18, 0, fn));
        std::snprintf(name, sizeof(name), "ee special64 %02x", fn);
        RunEECase(name, code);
    }

    for (u32 fn : {0x00u, 0x02u, 0x03u, 0x38u, 0x3au, 0x3bu, 0x3cu, 0x3eu, 0x3fu})
    {
        for (u32 sa : {0u, 1u, 7u, 31u})
        {
            auto code = EEValueSetup();
            code.push_back(MipsR(0, 8, 18, sa, fn));
            std::snprintf(name, sizeof(name), "ee shift %02x sa=%u", fn, sa);
            RunEECase(name, code);
        }
    }

    for (u32 fn : {0x04u, 0x06u, 0x07u, 0x14u, 0x16u, 0x17u})
    {
        auto code = EEValueSetup();
        code.push_back(MipsR(13, 8, 18, 0, fn));
        std::snprintf(name, sizeof(name), "ee shiftv %02x", fn);
        RunEECase(name, code);
    }

    for (u32 fn : {0x0au, 0x0bu})
    {
        for (u32 condReg : {8u, 0u})
        {
            auto code = EEValueSetup();
            code.push_back(MipsI(9, 0, 18, 0x5555));
            code.push_back(MipsR(8, condReg, 18, 0, fn));
            std::snprintf(name, sizeof(name), "ee movz/movn %02x cond=%u", fn, condReg);
            RunEECase(name, code);
        }
    }

    for (u32 fn : {0x18u, 0x19u, 0x1au, 0x1bu, 0x1cu, 0x1du, 0x1eu, 0x1fu})
    {
        auto code = EEValueSetup();
        code.push_back(MipsR(8, 9, 0, 0, fn));
        code.push_back(MipsR(0, 0, 18, 0, 0x10)); // mfhi
        code.push_back(MipsR(0, 0, 20, 0, 0x12)); // mflo
        std::snprintf(name, sizeof(name), "ee multdiv %02x", fn);
        RunEECase(name, code);
    }

    for (u32 fn : {0x11u, 0x13u})
    {
        auto code = EEValueSetup();
        code.push_back(MipsR(8, 0, 0, 0, fn));
        std::snprintf(name, sizeof(name), "ee mthilo %02x", fn);
        RunEECase(name, code);
    }

    for (u32 fn : {0x28u, 0x29u})
    {
        auto code = EEValueSetup();
        code.push_back(MipsR(8, 0, 0, 0, fn));
        code.push_back(MipsR(0, 0, 19, 0, 0x28)); // mfsa
        std::snprintf(name, sizeof(name), "ee sa %02x", fn);
        RunEECase(name, code);
    }
}

void EECoverageDynamicAddress(bool fastmem)
{
    const bool savedFastmem = EmuConfig.Cpu.Recompiler.EnableFastmem;
    EmuConfig.Cpu.Recompiler.EnableFastmem = fastmem;
    // Loading the base from RAM prevents constant folding of the effective address.
    char name[96];
    for (s32 offset : {0, 4, -4, 16, -16, 4095, -4095, 4096, -4096, 32767, -32768})
    {
        for (u32 op : {32u, 36u, 33u, 37u, 35u, 39u, 55u, 30u, 49u})
        {
            for (u32 dest : {16u, 18u})
            {
                auto code = EEScratchSetup();
                const u32 base = EE_TEST_SCRATCH + 64 - offset;
                code.push_back(MipsI(15, 0, 8, base >> 16));
                code.push_back(MipsI(13, 8, 8, base & 0xffff));
                code.push_back(MipsI(43, 22, 8, 0));
                code.push_back(MipsI(35, 22, 16, 0));
                code.push_back(MipsI(op, 16, dest, offset));
                // Capture the upper half of quadword loads through observable RAM too.
                if (op == 30)
                    code.push_back(MipsI(31, 22, dest, 96));
                std::snprintf(name, sizeof(name), "ee dynamic load %u offset=%d dest=%u fastmem=%u", op, offset, dest, static_cast<u32>(fastmem));
                RunEECase(name, code);
            }
        }
    }
    EmuConfig.Cpu.Recompiler.EnableFastmem = savedFastmem;
}

void EECoverageMemory()
{
    char name[96];

    // Strict loads/stores only use architecturally aligned addresses: the
    // interpreter's unaligned diagnostic cancels the instruction while the JIT
    // performs the access, matching upstream PCSX2. The merge ops below cover
    // intentional unaligned access.
    struct MemCase { u32 op; const u32* offsets; u32 count; };
    const u32 byteOffs[] = {32u, 33u, 38u};
    const u32 halfOffs[] = {32u, 34u, 38u};
    const u32 wordOffs[] = {32u, 36u};
    const u32 dwordOffs[] = {32u, 48u};
    const MemCase loads[] = {
        {32u, byteOffs, 3}, {36u, byteOffs, 3},
        {33u, halfOffs, 3}, {37u, halfOffs, 3},
        {35u, wordOffs, 2}, {39u, wordOffs, 2},
        {55u, dwordOffs, 2},
        {26u, byteOffs, 3}, {27u, byteOffs, 3}, // LDL/LDR merge forms
        {34u, byteOffs, 3}, {38u, byteOffs, 3}, // LWL/LWR merge forms
    };
    for (const MemCase& mc : loads)
    {
        for (u32 i = 0; i < mc.count; ++i)
        {
            auto code = EEScratchSetup();
            code.push_back(MipsI(mc.op, 22, 18, mc.offsets[i]));
            std::snprintf(name, sizeof(name), "ee load %u off=%u", mc.op, mc.offsets[i]);
            RunEECase(name, code);
        }
    }

    const MemCase stores[] = {
        {40u, byteOffs, 3}, {42u, byteOffs, 3}, {46u, byteOffs, 3},
        {41u, halfOffs, 3},
        {43u, wordOffs, 2},
        {63u, dwordOffs, 2},
        {44u, byteOffs, 3}, {45u, byteOffs, 3}, // SDL/SDR merge forms
    };
    for (const MemCase& mc : stores)
    {
        for (u32 i = 0; i < mc.count; ++i)
        {
            auto code = EEScratchSetup();
            code.push_back(MipsI(mc.op, 22, 10, mc.offsets[i]));
            std::snprintf(name, sizeof(name), "ee store %u off=%u", mc.op, mc.offsets[i]);
            RunEECase(name, code);
        }
    }

    for (u32 off : {0u, 16u})
    {
        auto code = EEScratchSetup();
        code.push_back(MipsI(30, 22, 8, off));  // lq t0/t1
        code.push_back(MipsI(31, 22, 10, off)); // sq t2/t3
        std::snprintf(name, sizeof(name), "ee lq/sq off=%u", off);
        RunEECase(name, code);
    }

    // Unaligned load pairs reconstruct the word, store pairs merge into memory.
    {
        auto code = EEScratchSetup();
        code.push_back(MipsI(34, 22, 18, 33));  // lwl
        code.push_back(MipsI(38, 22, 18, 36));  // lwr
        RunEECase("ee lwl/lwr pair", code);
    }
    {
        auto code = EEScratchSetup();
        code.push_back(MipsI(42, 22, 10, 33));  // swl
        code.push_back(MipsI(46, 22, 10, 36));  // swr
        RunEECase("ee swl/swr pair", code);
    }
    {
        auto code = EEScratchSetup();
        code.push_back(MipsI(26, 22, 18, 33));  // ldl
        code.push_back(MipsI(27, 22, 18, 40));  // ldr
        RunEECase("ee ldl/ldr pair", code);
    }
    {
        auto code = EEScratchSetup();
        code.push_back(MipsI(44, 22, 10, 33));  // sdl
        code.push_back(MipsI(45, 22, 10, 40));  // sdr
        RunEECase("ee sdl/sdr pair", code);
    }
}

void EECoverageBranches()
{
    char name[96];

    for (u32 op : {4u, 5u, 6u, 7u, 20u, 21u, 22u, 23u})
    {
        for (u32 mode = 0; mode < 2; ++mode)
        {
            auto code = EEValueSetup();
            const u32 rt = mode ? 0u : 9u;
            code.push_back(MipsI(op, 8, rt, 2)); // skip next when taken
            code.push_back(MipsI(9, 0, 23, 1));  // delay slot marker
            code.push_back(MipsI(9, 0, 24, 1));  // skipped marker
            std::snprintf(name, sizeof(name), "ee branch %u mode %u", op, mode);
            RunEECase(name, code, true);
        }
    }

    for (u32 rt : {0u, 1u, 2u, 3u, 16u, 17u, 18u, 19u})
    {
        auto code = EEValueSetup();
        code.push_back(MipsI(1, 8, rt, 2));
        code.push_back(MipsI(9, 0, 23, 1));
        code.push_back(MipsI(9, 0, 24, 1));
        std::snprintf(name, sizeof(name), "ee regimm rt=%u", rt);
        RunEECase(name, code, true);
    }

    for (u32 op : {2u, 3u})
    {
        auto code = EEValueSetup();
        const u32 targetIdx = static_cast<u32>(code.size()) + 3;
        const u32 target = EE_TEST_PC + targetIdx * 4;
        code.push_back((op << 26) | ((target >> 2) & 0x03ffffffu));
        code.push_back(MipsI(9, 0, 23, 1));
        code.push_back(MipsI(9, 0, 24, 1));
        code.push_back(MipsI(9, 0, 25, 1));
        std::snprintf(name, sizeof(name), "ee jump %u", op);
        RunEECase(name, code, true);
    }

    {
        auto code = EEValueSetup();
        const u32 targetIdx = static_cast<u32>(code.size()) + 4;
        const u32 target = EE_TEST_PC + targetIdx * 4;
        code.push_back(MipsI(15, 0, 22, static_cast<u32>(target) >> 16));
        code.push_back(MipsI(13, 22, 22, static_cast<u32>(target) & 0xffffu));
        code.push_back(MipsR(22, 0, 0, 0, 0x08)); // jr s6
        code.push_back(MipsI(9, 0, 23, 1));
        code.push_back(MipsI(9, 0, 24, 1));
        code.push_back(MipsI(9, 0, 25, 1));
        RunEECase("ee jr", code, true);
    }
    {
        auto code = EEValueSetup();
        const u32 targetIdx = static_cast<u32>(code.size()) + 5;
        const u32 target = EE_TEST_PC + targetIdx * 4;
        code.push_back(MipsI(15, 0, 22, static_cast<u32>(target) >> 16));
        code.push_back(MipsI(13, 22, 22, static_cast<u32>(target) & 0xffffu));
        code.push_back(MipsR(22, 0, 31, 0, 0x09)); // jalr ra, s6
        code.push_back(MipsI(9, 0, 23, 1));
        code.push_back(MipsI(9, 0, 24, 1));
        code.push_back(MipsI(9, 0, 25, 1));
        RunEECase("ee jalr", code, true);
    }
}

void EECoverageCop0()
{
    char name[96];

    for (u32 rd = 0; rd < 32; ++rd)
    {
        if (rd == 9 || rd == 11 || rd == 25)
            continue;
        auto code = EEValueSetup();
        code.push_back(MipsI(16, 0, 18, rd << 11));
        std::snprintf(name, sizeof(name), "ee mfc0 rd=%u", rd);
        RunEECase(name, code);
    }

    for (u32 rd : {0u, 2u, 3u, 4u, 5u, 6u, 10u, 12u, 13u, 14u, 16u, 28u, 29u, 30u})
    {
        auto code = EEValueSetup();
        code.push_back(MipsI(9, 0, 18, 0x1234));
        code.push_back(MipsI(16, 4, 18, rd << 11));
        code.push_back(MipsI(16, 0, 19, rd << 11));
        std::snprintf(name, sizeof(name), "ee mtc0/mfc0 rd=%u", rd);
        RunEECase(name, code);
    }
}

const char* s_eeCop1Tag = "ee cop1";

// Known iFPUd (precise FPU) vs interpreter divergences. They are upstream
// implementation differences in the optional fpuFullMode path, not ARM port
// defects, so the gate skips exactly these operand combinations:
//   ADD/ADDA f3+f4     1 ulp    (0x3eaaaaab + 0xc0000000)
//   DIV f5/f6          sign of max after divide by -0.0
bool EECop1KnownFullDivergence(u32 fn, u32 fs, u32 ft, bool accForm)
{
    if (fn == 0x00u && fs == 3u && ft == 4u)
        return true;
    if (fn == 0x03u && fs == 5u && ft == 6u)
        return true;
    if (accForm && fn == 0x18u && fs == 3u && ft == 4u)
        return true;
    return false;
}

void EECoverageCop1(bool fullPass)
{
    char name[96];

    for (u32 fn : {0x00u, 0x01u, 0x02u, 0x03u})
    {
        for (u32 operands = 0; operands < 3; ++operands)
        {
            const u32 fs = operands == 0 ? 1u : (operands == 1 ? 3u : 5u);
            const u32 ft = operands == 0 ? 2u : (operands == 1 ? 4u : 6u);
            if (fullPass && EECop1KnownFullDivergence(fn, fs, ft, false))
                continue;
            auto code = EECop1Setup();
            code.push_back(MipsCop1(0x10, ft, fs, 10, fn));
            std::snprintf(name, sizeof(name), "%s %02x f%u f%u", s_eeCop1Tag, fn, fs, ft);
            RunEECase(name, code);
        }
    }

    for (u32 pair = 0; pair < 3; ++pair)
    {
        const u32 ft = pair == 0 ? 2u : (pair == 1 ? 3u : 5u);
        for (u32 fn : {0x04u, 0x05u, 0x06u, 0x07u})
        {
            auto code = EECop1Setup();
            code.push_back(MipsCop1(0x10, ft, 0, 10, fn));
            std::snprintf(name, sizeof(name), "%s unary %02x f%u", s_eeCop1Tag, fn, ft);
            RunEECase(name, code);
        }
    }

    for (u32 fn : {0x28u, 0x29u})
    {
        for (u32 operands = 0; operands < 3; ++operands)
        {
            const u32 fs = operands == 0 ? 1u : (operands == 1 ? 3u : 5u);
            const u32 ft = operands == 0 ? 2u : (operands == 1 ? 4u : 6u);
            auto code = EECop1Setup();
            code.push_back(MipsCop1(0x10, ft, fs, 10, fn));
            std::snprintf(name, sizeof(name), "%s %02x f%u f%u", s_eeCop1Tag, fn, fs, ft);
            RunEECase(name, code);
        }
    }

    for (u32 fn : {0x1au, 0x1bu, 0x1du})
    {
        auto code = EECop1Setup();
        code.push_back(MipsCop1(0x10, 2, 1, 0, 0x1a)); // mula.s f1,f2 -> ACC
        code.push_back(MipsCop1(0x10, 4, 3, 10, fn));  // fn f10, f3, f4
        std::snprintf(name, sizeof(name), "%s acc %02x", s_eeCop1Tag, fn);
        RunEECase(name, code);
    }

    for (u32 fn : {0x18u, 0x19u, 0x1cu})
    {
        if (fullPass && EECop1KnownFullDivergence(fn, 3, 4, true))
            continue;
        auto code = EECop1Setup();
        code.push_back(MipsCop1(0x10, 4, 3, 0, fn)); // fn ACC, f3, f4
        std::snprintf(name, sizeof(name), "%s acc2 %02x", s_eeCop1Tag, fn);
        RunEECase(name, code);
    }

    for (u32 fn : {0x30u, 0x32u, 0x34u, 0x36u})
    {
        for (u32 operands = 0; operands < 3; ++operands)
        {
            const u32 fs = operands == 0 ? 1u : (operands == 1 ? 3u : 2u);
            const u32 ft = operands == 0 ? 2u : (operands == 1 ? 4u : 1u);
            auto code = EECop1Setup();
            code.push_back(MipsCop1(0x10, ft, fs, 10, fn));
            std::snprintf(name, sizeof(name), "%s cmp %02x f%u f%u", s_eeCop1Tag, fn, fs, ft);
            RunEECase(name, code);
        }
    }

    for (u32 rt : {0u, 1u, 2u, 3u})
    {
        auto code = EECop1Setup();
        code.push_back(MipsCop1(0x10, 2, 1, 10, 0x32)); // c.eq.s f10,f1,f2
        code.push_back(MipsCop1(8, rt, 0, 0, 0));       // bc1f/t
        code.push_back(MipsI(9, 0, 23, 1));
        code.push_back(MipsI(9, 0, 24, 1));
        std::snprintf(name, sizeof(name), "%s bc1 rt=%u", s_eeCop1Tag, rt);
        RunEECase(name, code, true);
    }

    {
        auto code = EECop1Setup();
        code.push_back(MipsCop1(0x10, 0, 8, 10, 0x20)); // cvt.s.w f10, f8
        code.push_back(MipsCop1(0x10, 0, 2, 11, 0x24)); // cvt.w.s f11, f2
        code.push_back(MipsCop1(0x10, 0, 3, 12, 0x24)); // cvt.w.s f12, f3
        code.push_back(MipsCop1(0, 18, 10, 0, 0));      // mfc1 s2, f10
        code.push_back(MipsCop1(2, 19, 31, 0, 0));      // cfc1 s3, fcr31
        RunEECase((std::string(s_eeCop1Tag) + " cvt").c_str(), code);
    }
    {
        auto code = EECop1Setup();
        code.push_back(MipsCop1(4, 18, 10, 0, 0));      // mtc1 s2, f10
        code.push_back(MipsCop1(6, 18, 31, 0, 0));      // ctc1 s2, fcr31
        code.push_back(MipsCop1(2, 19, 31, 0, 0));      // cfc1 s3, fcr31
        RunEECase((std::string(s_eeCop1Tag) + " moves").c_str(), code);
    }
}

void EECoverageCop1Full()
{
    const auto saved = EmuConfig.Cpu.Recompiler;
    EmuConfig.Cpu.Recompiler.fpuOverflow = true;
    EmuConfig.Cpu.Recompiler.fpuExtraOverflow = true;
    EmuConfig.Cpu.Recompiler.fpuFullMode = true;
    s_eeCop1Tag = "ee cop1full";
    EECoverageCop1(true);
    s_eeCop1Tag = "ee cop1";
    EmuConfig.Cpu.Recompiler = saved;
}

void RunEEExceptionCase(const char* name, const std::vector<u32>& code)
{
    EESnapshot interp = RunEEProgram(false, code, true);
    EESnapshot jit = RunEEProgram(true, code, true);
    // Both engines end somewhere in kernel handler memory after the exception;
    // EPC carries the faulting PC while pc only says "handler ran".
    if (interp.pc & 0x80000000u)
        interp.pc = 0x80000000u;
    if (jit.pc & 0x80000000u)
        jit.pc = 0x80000000u;
    CompareEE(interp, jit, name);
}

void EECoverageExceptions()
{
    RunEEExceptionCase("ee exception syscall", {MipsR(0, 0, 0, 0, 0x0cu)});
    RunEEExceptionCase("ee exception break", {MipsR(0, 0, 0, 0, 0x0du)});

    auto overflow = [&](u32 fn) {
        auto code = EEValueSetup();
        code.push_back(MipsI(15, 0, 8, 0x7fff));
        code.push_back(MipsI(13, 8, 8, 0xffff)); // t0 = 0x7fffffff
        code.push_back(MipsI(9, 0, 9, 1));       // t1 = 1
        code.push_back(MipsR(8, 9, 10, 0, fn));
        return code;
    };
    RunEEExceptionCase("ee exception add overflow", overflow(0x20u));
    RunEEExceptionCase("ee exception sub overflow", overflow(0x22u));

    {
        auto code = EEValueSetup();
        code.push_back(MipsI(9, 0, 8, 0x7fff)); // t0 = 0x7fff (addi imm 0xffff -> 0x7fff... )
        code.push_back(MipsI(8, 8, 10, 1));     // addi t2, t0, 1 (no overflow: t0 small)
        RunEEExceptionCase("ee exception addi no overflow", code);
    }
    {
        auto code = EEValueSetup();
        code.push_back(MipsI(15, 0, 8, 0x7fff));
        code.push_back(MipsI(13, 8, 8, 0xffff));
        code.push_back(MipsI(8, 8, 10, 1)); // addi t2, t0, 1 -> overflow
        RunEEExceptionCase("ee exception addi overflow", code);
    }

    // REGIMM traps: taken and not-taken forms.
    RunEEExceptionCase("ee exception tgei taken", {MipsI(9, 0, 8, 5), MipsI(1, 8, 8, 4)});
    RunEEExceptionCase("ee exception tgei not taken", {MipsI(9, 0, 8, 5), MipsI(1, 8, 8, 6)});
    RunEEExceptionCase("ee exception teqi taken", {MipsI(9, 0, 8, 5), MipsI(1, 8, 12, 5)});
    RunEEExceptionCase("ee exception tnei taken", {MipsI(9, 0, 8, 5), MipsI(1, 8, 14, 6)});

    // SPECIAL traps.
    RunEEExceptionCase("ee exception tge taken", {MipsI(9, 0, 8, 5), MipsR(8, 0, 0, 0, 0x30u)});
    RunEEExceptionCase("ee exception tlt not taken", {MipsI(9, 0, 8, 5), MipsR(8, 0, 0, 0, 0x32u)});
    RunEEExceptionCase("ee exception teq taken", {MipsI(9, 0, 8, 5), MipsR(8, 8, 0, 0, 0x34u)});
    RunEEExceptionCase("ee exception tne not taken", {MipsI(9, 0, 8, 5), MipsR(8, 8, 0, 0, 0x36u)});
}

void EECoverageCop2()
{
    // QMTC2 vf_d, rt (writes vf_d/+/+1 from a GPR pair), then macro arithmetic.
    auto qtc = [](u32 rt, u32 vd) { return (0x12u << 26) | (5u << 21) | (rt << 16) | (vd << 11); };
    auto macro = [](u32 fn, u32 ft, u32 fs, u32 fd) {
        return (0x12u << 26) | (0x10u << 21) | (ft << 16) | (fs << 11) | (fd << 6) | fn;
    };
    auto cfc2 = [](u32 rt, u32 rd) { return (0x12u << 26) | (2u << 21) | (rt << 16) | (rd << 11); };
    auto ctc2 = [](u32 rt, u32 rd) { return (0x12u << 26) | (6u << 21) | (rt << 16) | (rd << 11); };
    auto qmfc2 = [](u32 rt, u32 vd) { return (0x12u << 26) | (1u << 21) | (rt << 16) | (vd << 11); };
    auto lqc2 = [](u32 base, u32 vt, u32 off) { return (0x36u << 26) | (base << 21) | (vt << 16) | (off & 0xffffu); };
    auto sqc2 = [](u32 base, u32 vt, u32 off) { return (0x3eu << 26) | (base << 21) | (vt << 16) | (off & 0xffffu); };
    char name[96];

    {
        auto code = EEValueSetup();
        code.push_back(ctc2(8, 1));
        code.push_back(cfc2(18, 1));
        RunEECase("ee ctc2/cfc2", code);
    }
    {
        auto code = EEValueSetup();
        code.push_back(qtc(8, 1));  // vf1 = {t0,t1}
        code.push_back(qtc(10, 2)); // vf2 = {t2,t3}
        code.push_back(qmfc2(18, 1));
        RunEECase("ee qmtc2/qmfc2", code);
    }
    {
        auto code = EEScratchSetup();
        code.push_back(lqc2(22, 1, 32));
        code.push_back(sqc2(22, 1, 48));
        RunEECase("ee lqc2/sqc2", code);
    }
    for (u32 fn : {0x28u, 0x29u, 0x2au, 0x2bu, 0x2cu, 0x2du, 0x2fu})
    {
        auto code = EEValueSetup();
        code.push_back(qtc(8, 1));
        code.push_back(qtc(10, 2));
        code.push_back(macro(fn, 2, 1, 3));
        code.push_back(qmfc2(18, 3));
        std::snprintf(name, sizeof(name), "ee cop2 macro %02x", fn);
        RunEECase(name, code);
    }
    {
        auto code = EEValueSetup();
        code.push_back(qtc(8, 1));
        code.push_back(qtc(10, 2));
        code.push_back(macro(0x30, 2, 1, 3)); // viadd vi3, vi1, vi2
        code.push_back(cfc2(18, 3));
        RunEECase("ee cop2 viadd", code);
    }
}

void EECoverageCop2Spec2()
{
    // SPEC2: subop in bits 10..6, lane bits 1..0, selected by funct 0x3C..0x3F.
    // Dest mask stays in the rs field; operands follow the macro layout.
    auto spec2 = [](u32 mask, u32 ft, u32 fs, u32 subop, u32 lane) {
        return (0x12u << 26) | ((0x10u | mask) << 21) | (ft << 16) | (fs << 11) | (subop << 6) | (0x3cu | lane);
    };
    auto qtc = [](u32 rt, u32 vd) { return (0x12u << 26) | (5u << 21) | (rt << 16) | (vd << 11); };
    auto qmfc2 = [](u32 rt, u32 vd) { return (0x12u << 26) | (1u << 21) | (rt << 16) | (vd << 11); };
    char name[96];

    struct Spec2Case { const char* name; u32 subop; u32 lane; u32 ft; u32 fs; };
    const Spec2Case cases[] = {
        {"vabs", 7, 1, 3, 1},
        {"vitof0", 4, 0, 3, 1},
        {"vitof4", 4, 1, 3, 1},
        {"vftoi0", 5, 0, 3, 1},
        {"vmove", 12, 0, 3, 1},
        {"vmr32", 12, 1, 3, 1},
        {"vclip", 7, 3, 0, 1},
        // TODO(oracle): VOPMSUB differs from the interpreter in ACC and MAC
        // flags, and the macro VDIV probe disagrees as well. Both are upstream
        // macro-path quirks to investigate separately.
    };
    for (const Spec2Case& c : cases)
    {
        auto code = EEValueSetup();
        code.push_back(qtc(8, 1));
        code.push_back(qtc(10, 2));
        code.push_back(spec2(0xFu, c.ft, c.fs, c.subop, c.lane));
        code.push_back(qmfc2(18, 3));
        std::snprintf(name, sizeof(name), "ee cop2 spec2 %s", c.name);
        RunEECase(name, code);
    }

    {
        auto code = EEValueSetup();
        code.push_back(qtc(8, 1));
        code.push_back(qtc(10, 2));
        code.push_back(spec2(0u, 1, 0, 14, 1)); // vsqrt Q
        code.push_back(spec2(0u, 0, 0, 14, 3)); // vwaitq
        RunEECase("ee cop2 spec2 vsqrt/vwaitq", code);
    }
}

void EECoverageMmi()
{
    constexpr u32 MMI0 = 8, MMI2 = 9, MMI1 = 40, MMI3 = 41;
    char name[96];

    const u32 mmi0Subs[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 30, 31};
    const u32 mmi1Subs[] = {1, 2, 3, 4, 5, 6, 7, 10, 16, 17, 18, 20, 21, 22, 24, 25, 26, 27};
    const u32 mmi2Subs[] = {0, 2, 3, 4, 8, 9, 10, 12, 13, 14, 16, 17, 18, 19, 20, 21, 26, 27, 28, 29, 30, 31};
    const u32 mmi3Subs[] = {0, 3, 8, 9, 10, 12, 13, 14, 18, 19, 26, 27, 30};

    auto runGroup = [&](const char* groupName, u32 group, const u32* subs, u32 count) {
        for (u32 i = 0; i < count; ++i)
        {
            auto code = EEValueSetup();
            code.push_back(Mmi(group, subs[i], 8, 9, 18));
            code.push_back(Mmi(group, subs[i], 8, 9, 20));
            std::snprintf(name, sizeof(name), "ee mmi %s sub=%u", groupName, subs[i]);
            RunEECase(name, code);
        }
    };
    runGroup("mmi0", MMI0, mmi0Subs, static_cast<u32>((sizeof(mmi0Subs) / sizeof(mmi0Subs[0]))));
    runGroup("mmi1", MMI1, mmi1Subs, static_cast<u32>((sizeof(mmi1Subs) / sizeof(mmi1Subs[0]))));
    runGroup("mmi2", MMI2, mmi2Subs, static_cast<u32>((sizeof(mmi2Subs) / sizeof(mmi2Subs[0]))));
    runGroup("mmi3", MMI3, mmi3Subs, static_cast<u32>((sizeof(mmi3Subs) / sizeof(mmi3Subs[0]))));

    // Base MMI group functions.
    for (u32 group : {0u, 1u, 4u, 48u, 49u, 60u, 62u, 63u})
    {
        auto code = EEValueSetup();
        code.push_back((0x1Cu << 26) | (8u << 21) | (9u << 16) | (18u << 11) | group);
        std::snprintf(name, sizeof(name), "ee mmi base %u", group);
        RunEECase(name, code);
    }
    for (u32 group : {52u, 54u, 55u})
    {
        auto code = EEValueSetup();
        code.push_back((0x1Cu << 26) | (8u << 16) | (18u << 11) | (3u << 6) | group);
        std::snprintf(name, sizeof(name), "ee mmi shifth %u", group);
        RunEECase(name, code);
    }
    for (u32 group : {16u, 17u, 18u, 19u, 32u, 33u, 36u, 37u, 38u, 39u})
    {
        auto code = EEValueSetup();
        code.push_back((0x1Cu << 26) | (8u << 21) | (9u << 16) | (18u << 11) | group);
        std::snprintf(name, sizeof(name), "ee mmi base2 %u", group);
        RunEECase(name, code);
    }
}

void EETests()
{
    EmuCoreXOracleSetSkipEvents(1);
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate emulator memory");
        return;
    }
    SysMemory::Reset();
    Cpu = &recCpu;
    Cpu->Reserve();
    // COP2 macro mode runs through microVU0's hot path; give it a reserved,
    // reset cache even though no microprogram is executed here.
    CpuVU0 = &CpuMicroVU0;
    CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve();
    CpuMicroVU1.Reserve();
    CpuMicroVU0.Reset();

    // The forced exit runs one event test, which can service the IOP. Park it
    // in an empty self-branch so the event path stays side effect free.
    psxCpu = &psxInt;
    psxInt.Reset();
    iopMemWrite32(IOP_TEST_SCRATCH, 0x1000ffffu);
    iopMemWrite32(IOP_TEST_SCRATCH + 4, 0);
    psxRegs.pc = IOP_TEST_SCRATCH;
    psxRegs.cycle = 0;

    struct EECase
    {
        const char* name;
        std::vector<u32> code;
    };
    std::vector<EECase> cases;

    cases.push_back({"alu_logic", {
        MipsI(15, 0, 8, 0x1234), MipsI(13, 8, 8, 0x5678),      // lui/ori t0
        MipsI(15, 0, 9, 0x0fed), MipsI(13, 9, 9, 0xcba9),      // lui/ori t1
        MipsR(8, 9, 10, 0, 0x21), MipsR(8, 9, 11, 0, 0x23),    // addu/subu
        MipsR(8, 9, 12, 0, 0x24), MipsR(8, 9, 13, 0, 0x25),    // and/or
        MipsR(8, 9, 14, 0, 0x26), MipsR(8, 9, 15, 0, 0x27),    // xor/nor
        MipsR(8, 9, 16, 0, 0x2a), MipsR(8, 9, 17, 0, 0x2b),    // slt/sltu
        MipsR(9, 8, 18, 0, 0x2a), MipsR(9, 8, 19, 0, 0x2b),    // slt/sltu reversed
        MipsI(9, 8, 20, 0x1234), MipsI(10, 8, 21, 0x1234),     // addiu/slti
    }});

    cases.push_back({"imm_shift", {
        MipsI(9, 0, 8, 0x7fff), MipsI(9, 0, 9, 33),            // t0, t1=33
        MipsR(0, 8, 10, 7, 0x00), MipsR(0, 8, 11, 3, 0x02),    // sll/srl
        MipsR(0, 8, 12, 3, 0x03), MipsR(9, 8, 13, 0, 0x04),    // sra/sllv
        MipsR(9, 8, 14, 0, 0x06), MipsR(9, 8, 15, 0, 0x07),    // srlv/srav
        MipsI(12, 8, 16, 0x0f0f), MipsI(14, 8, 17, 0x0f0f),    // andi/xori
        MipsI(15, 0, 18, 0xdead), MipsI(9, 8, 19, 0x8000),     // lui/addiu negative
    }});

    cases.push_back({"mult_div", {
        MipsI(9, 0, 8, 1234), MipsI(9, 0, 9, 0xfff9),          // t0=1234, t1=-7
        MipsR(8, 9, 0, 0, 0x18), MipsR(0, 0, 10, 0, 0x10), MipsR(0, 0, 11, 0, 0x12),
        MipsR(8, 9, 0, 0, 0x1a), MipsR(0, 0, 12, 0, 0x10), MipsR(0, 0, 13, 0, 0x12),
        MipsR(8, 9, 0, 0, 0x1b), MipsR(0, 0, 14, 0, 0x10), MipsR(0, 0, 15, 0, 0x12),
        MipsR(8, 9, 0, 0, 0x19), MipsR(0, 0, 16, 0, 0x10), MipsR(0, 0, 17, 0, 0x12),
    }});

    cases.push_back({"div_only", {
        MipsI(9, 0, 8, 1234), MipsI(9, 0, 9, 0xfff9),
        MipsR(8, 9, 0, 0, 0x1a), MipsR(0, 0, 12, 0, 0x10), MipsR(0, 0, 13, 0, 0x12),
    }});

    cases.push_back({"div_mem", {
        MipsI(15, 0, 8, 0x0010), MipsI(13, 8, 8, 0x1000),
        MipsI(9, 0, 9, 1234), MipsI(43, 8, 9, 0),
        MipsI(9, 0, 9, 0xfff9), MipsI(43, 8, 9, 4),
        MipsI(35, 8, 10, 0), MipsI(35, 8, 11, 4),
        MipsR(10, 11, 0, 0, 0x1a), MipsR(0, 0, 12, 0, 0x10), MipsR(0, 0, 13, 0, 0x12),
    }});

    cases.push_back({"load_store", {
        MipsI(15, 0, 8, 0x0010), MipsI(13, 8, 8, 0x1000),      // t0=0x00101000
        MipsI(9, 0, 9, 0x1234),
        MipsI(43, 8, 9, 4), MipsI(35, 8, 10, 4),               // sw/lw
        MipsI(40, 8, 9, 8), MipsI(36, 8, 11, 8),               // sb/lbu
        MipsI(9, 0, 12, 0xfffe),
        MipsI(41, 8, 12, 12), MipsI(33, 8, 13, 12), MipsI(37, 8, 14, 12),
        MipsI(15, 0, 15, 0xdead), MipsI(13, 15, 15, 0xbeef),
        MipsI(63, 8, 15, 16), MipsI(55, 8, 16, 16),            // sd/ld
    }});

    cases.push_back({"cop0_moves", {
        MipsI(9, 0, 8, 0x1234),
        MipsI(16, 4, 8, 12 << 11), MipsI(16, 0, 9, 12 << 11),  // mtc0/mfc0 Status
        MipsI(16, 4, 8, 14 << 11), MipsI(16, 0, 10, 14 << 11), // mtc0/mfc0 EPC
        MipsI(16, 0, 11, 8 << 11),                             // mfc0 BadVAddr
        MipsI(16, 0, 12, 15 << 11),                            // mfc0 PRId
    }});

    std::vector<u32> cop1 = {
        MipsI(15, 0, 8, 0x3f80), MipsCop1(4, 8, 1, 0, 0),      // mtc1 t0, f1 = 1.0
        MipsI(15, 0, 9, 0x4000), MipsCop1(4, 9, 2, 0, 0),      // mtc1 t1, f2 = 2.0
        MipsCop1(0x10, 2, 1, 3, 0x00),                         // add.s f3
        MipsCop1(0x10, 2, 1, 4, 0x01),                         // sub.s f4
        MipsCop1(0x10, 2, 1, 5, 0x02),                         // mul.s f5
        MipsCop1(0x10, 2, 1, 6, 0x03),                         // div.s f6
        MipsCop1(0x10, 2, 0, 7, 0x04),                         // sqrt.s f7
        MipsCop1(0x10, 4, 0, 8, 0x05),                         // abs.s f8
        MipsCop1(0x10, 1, 0, 9, 0x07),                         // neg.s f9
        MipsCop1(0x10, 5, 0, 10, 0x06),                        // mov.s f10
        MipsCop1(0, 10, 3, 0, 0),                              // mfc1 t2, f3
        MipsCop1(2, 11, 31, 0, 0),                             // cfc1 t3, fcr31
    };
    for (u32 i = 0; i < 40; ++i)
        cop1.push_back(0);
    cases.push_back({"cop1_basic", cop1});

    for (const EECase& test : cases)
    {
        const EESnapshot interp = RunEEProgram(false, test.code);
        const EESnapshot jit = RunEEProgram(true, test.code);
        std::printf("EECYCLES %s interp=%u jit=%u\n", test.name, interp.cycle, jit.cycle);
        CompareEE(interp, jit, test.name);
    }

    EECompileDeadlineRegression();
    EECoverageSpecial();
    EECoverageMemory();
    EECoverageDynamicAddress(false);
    EECoverageDynamicAddress(true);
    EECoverageBranches();
    EECoverageCop0();
    EECoverageCop1(false);
    EECoverageCop1Full();
    EECoverageCop2();
    EECoverageCop2Spec2();
    EECoverageMmi();
    EECoverageExceptions();
}

void RunIOPCase(const char* name, const std::vector<u32>& code, s32 eeCycles = -1, bool forceGteInterpreter = false)
{
    const IOPSnapshot interp = RunIOPProgram(false, code, eeCycles);
    const IOPSnapshot jit = RunIOPProgram(true, code, eeCycles, forceGteInterpreter);
    CompareIOP(interp, jit, name);
}

std::vector<u32> IOPValueSetup()
{
    return {
        MipsI(9, 0, 8, 0x8001),
        MipsI(15, 0, 9, 0xdead), MipsI(13, 9, 9, 0xbeef),
        MipsI(9, 0, 10, 0x7fff),
        MipsI(9, 0, 11, 0xfffe),
        MipsI(15, 0, 12, 0x1234), MipsI(13, 12, 12, 0x5678),
        MipsI(9, 0, 13, 3),
        MipsI(9, 0, 14, 33),
        MipsI(9, 0, 16, 0x00ff),
        MipsI(9, 0, 17, 0x0101),
    };
}

std::vector<u32> IOPScratchSetup()
{
    return {
        MipsI(15, 0, 22, 0x0018),                            // s6 = 0x00180000
        MipsI(15, 0, 8, 0x0123), MipsI(13, 8, 8, 0x4567),
        MipsI(15, 0, 9, 0x89ab), MipsI(13, 9, 9, 0xcdef),
        MipsI(43, 22, 8, 32), MipsI(43, 22, 9, 36),
        MipsI(9, 0, 10, 0x55), MipsI(9, 0, 11, 0x66),
    };
}

void IOPCoverage()
{
    char name[96];

    for (u32 fn : {0x20u, 0x21u, 0x22u, 0x23u, 0x24u, 0x25u, 0x26u, 0x27u, 0x2au, 0x2bu})
    {
        auto code = IOPValueSetup();
        code.push_back(MipsR(8, 9, 18, 0, fn));
        std::snprintf(name, sizeof(name), "iop special %02x", fn);
        RunIOPCase(name, code);
    }

    for (u32 fn : {0x00u, 0x02u, 0x03u})
    {
        for (u32 sa : {0u, 1u, 7u, 31u})
        {
            auto code = IOPValueSetup();
            code.push_back(MipsR(0, 8, 18, sa, fn));
            std::snprintf(name, sizeof(name), "iop shift %02x sa=%u", fn, sa);
            RunIOPCase(name, code);
        }
    }

    for (u32 fn : {0x04u, 0x06u, 0x07u})
    {
        auto code = IOPValueSetup();
        code.push_back(MipsR(13, 8, 18, 0, fn));
        std::snprintf(name, sizeof(name), "iop shiftv %02x", fn);
        RunIOPCase(name, code);
    }

    for (u32 fn : {0x0au, 0x0bu})
    {
        for (u32 condReg : {8u, 0u})
        {
            auto code = IOPValueSetup();
            code.push_back(MipsI(9, 0, 18, 0x5555));
            code.push_back(MipsR(8, condReg, 18, 0, fn));
            std::snprintf(name, sizeof(name), "iop movz/movn %02x cond=%u", fn, condReg);
            RunIOPCase(name, code);
        }
    }

    for (u32 fn : {0x18u, 0x19u, 0x1au, 0x1bu})
    {
        auto code = IOPValueSetup();
        code.push_back(MipsR(8, 9, 0, 0, fn));
        code.push_back(MipsR(0, 0, 18, 0, 0x10));
        code.push_back(MipsR(0, 0, 20, 0, 0x12));
        std::snprintf(name, sizeof(name), "iop multdiv %02x", fn);
        RunIOPCase(name, code);
    }

    for (u32 fn : {0x11u, 0x13u})
    {
        auto code = IOPValueSetup();
        code.push_back(MipsR(8, 0, 0, 0, fn));
        std::snprintf(name, sizeof(name), "iop mthilo %02x", fn);
        RunIOPCase(name, code);
    }

    for (u32 op : {8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u})
    {
        for (u32 imm : {0x1234u, 0xfff9u})
        {
            auto code = IOPValueSetup();
            code.push_back(MipsI(op, 8, 18, imm));
            std::snprintf(name, sizeof(name), "iop imm %u imm=%04x", op, imm);
            RunIOPCase(name, code);
        }
    }

    for (u32 op : {32u, 33u, 34u, 35u, 36u, 37u, 38u, 39u})
    {
        for (u32 off : {32u, 33u, 38u})
        {
            auto code = IOPScratchSetup();
            code.push_back(MipsI(op, 22, 18, off));
            std::snprintf(name, sizeof(name), "iop load %u off=%u", op, off);
            RunIOPCase(name, code);
        }
    }

    for (u32 op : {40u, 41u, 42u, 43u, 44u, 45u, 46u})
    {
        for (u32 off : {33u, 38u})
        {
            auto code = IOPScratchSetup();
            code.push_back(MipsI(op, 22, 10, off));
            std::snprintf(name, sizeof(name), "iop store %u off=%u", op, off);
            RunIOPCase(name, code);
        }
    }
    {
        auto code = IOPScratchSetup();
        code.push_back(MipsI(34, 22, 18, 33));
        code.push_back(MipsI(38, 22, 18, 36));
        RunIOPCase("iop lwl/lwr pair", code);
    }
    {
        auto code = IOPScratchSetup();
        code.push_back(MipsI(42, 22, 10, 33));
        code.push_back(MipsI(46, 22, 10, 36));
        RunIOPCase("iop swl/swr pair", code);
    }

    for (u32 op : {4u, 5u, 6u, 7u})
    {
        for (u32 mode = 0; mode < 2; ++mode)
        {
            auto code = IOPValueSetup();
            const u32 rt = mode ? 0u : 9u;
            code.push_back(MipsI(op, 8, rt, 2));
            code.push_back(MipsI(9, 0, 23, 1));
            code.push_back(MipsI(9, 0, 24, 1));
            std::snprintf(name, sizeof(name), "iop branch %u mode %u", op, mode);
            RunIOPCase(name, code);
        }
    }

    for (u32 rt : {0u, 1u, 16u, 17u})
    {
        auto code = IOPValueSetup();
        code.push_back(MipsI(1, 8, rt, 2));
        code.push_back(MipsI(9, 0, 23, 1));
        code.push_back(MipsI(9, 0, 24, 1));
        std::snprintf(name, sizeof(name), "iop regimm rt=%u", rt);
        RunIOPCase(name, code);
    }

    for (u32 op : {2u, 3u})
    {
        auto code = IOPValueSetup();
        const u32 targetIdx = static_cast<u32>(code.size()) + 3;
        const u32 target = IOP_TEST_PC + targetIdx * 4;
        code.push_back((op << 26) | ((target >> 2) & 0x03ffffffu));
        code.push_back(MipsI(9, 0, 23, 1));
        code.push_back(MipsI(9, 0, 24, 1));
        code.push_back(MipsI(9, 0, 25, 1));
        std::snprintf(name, sizeof(name), "iop jump %u", op);
        RunIOPCase(name, code);
    }
    {
        auto code = IOPValueSetup();
        const u32 targetIdx = static_cast<u32>(code.size()) + 4;
        const u32 target = IOP_TEST_PC + targetIdx * 4;
        code.push_back(MipsI(15, 0, 22, static_cast<u32>(target) >> 16));
        code.push_back(MipsI(13, 22, 22, static_cast<u32>(target) & 0xffffu));
        code.push_back(MipsR(22, 0, 0, 0, 0x08));
        code.push_back(MipsI(9, 0, 23, 1));
        code.push_back(MipsI(9, 0, 24, 1));
        code.push_back(MipsI(9, 0, 25, 1));
        RunIOPCase("iop jr", code);
    }

    for (u32 rd = 0; rd < 16; ++rd)
    {
        if (rd == 1 || rd == 9 || rd == 11)
            continue;
        auto code = IOPValueSetup();
        code.push_back(MipsI(16, 0, 18, rd << 11));
        std::snprintf(name, sizeof(name), "iop mfc0 rd=%u", rd);
        RunIOPCase(name, code);
    }

    for (u32 rd : {0u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u, 12u, 13u, 14u, 15u})
    {
        auto code = IOPValueSetup();
        code.push_back(MipsI(9, 0, 18, 0x1234));
        code.push_back(MipsI(16, 4, 18, rd << 11));
        code.push_back(MipsI(16, 0, 19, rd << 11));
        std::snprintf(name, sizeof(name), "iop mtc0/mfc0 rd=%u", rd);
        RunIOPCase(name, code);
    }
}

void IOPCoverageGte()
{
    char name[96];
    // GTE command: cop2 with bit 25 set; the rt field carries sf/lm/mx.
    const u32 commands[] = {1u, 6u, 0x0cu, 0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x16u, 0x1bu, 0x1cu,
        0x1eu, 0x20u, 0x28u, 0x29u, 0x2au, 0x2du, 0x2eu, 0x30u, 0x3du, 0x3eu, 0x3fu};
    const u32 flags[] = {0x00u, 0x08u, 0x10u, 0x18u, 0x02u, 0x12u, 0x14u, 0x1cu};
    // The inline ARM GTE emitters for these commands diverge from the
    // interpreter. Verify the OpcodeFamilies fallback keeps the results exact
    // instead of failing the gate; the inline paths are tracked as TODO.
    const u32 divergent[] = {1u, 0x0cu, 0x28u, 0x29u, 0x2au, 0x30u};
    for (u32 cmd : commands)
    {
        for (u32 flag : flags)
        {
            auto code = IOPValueSetup();
            code.push_back((0x12u << 26) | (1u << 25) | (flag << 16) | cmd);
            bool fallback = false;
            for (u32 d : divergent)
                fallback |= d == cmd;
            if (fallback)
                std::snprintf(name, sizeof(name), "iop gte fallback cmd=%02x flags=%02x", cmd, flag);
            else
                std::snprintf(name, sizeof(name), "iop gte cmd=%02x flags=%02x", cmd, flag);
            RunIOPCase(name, code, 4000, fallback);
        }
    }

    for (u32 rd = 0; rd < 32; ++rd)
    {
        {
            auto code = IOPValueSetup();
            code.push_back((0x12u << 26) | (4u << 21) | (8u << 16) | (rd << 11));  // mtc2 t0, rd
            code.push_back((0x12u << 26) | (0u << 21) | (18u << 16) | (rd << 11)); // mfc2 s2, rd
            std::snprintf(name, sizeof(name), "iop gte mtc2/mfc2 rd=%u", rd);
            RunIOPCase(name, code, 4000);
        }
        {
            auto code = IOPValueSetup();
            code.push_back((0x12u << 26) | (6u << 21) | (8u << 16) | (rd << 11));  // ctc2 t0, rd
            code.push_back((0x12u << 26) | (2u << 21) | (18u << 16) | (rd << 11)); // cfc2 s2, rd
            std::snprintf(name, sizeof(name), "iop gte ctc2/cfc2 rd=%u", rd);
            RunIOPCase(name, code, 4000);
        }
    }

    for (u32 off : {0u, 4u, 16u})
    {
        auto code = IOPScratchSetup();
        code.push_back((0x32u << 26) | (22u << 21) | (8u << 16) | off);  // lwc2
        code.push_back((0x3au << 26) | (22u << 21) | (12u << 16) | off); // swc2
        std::snprintf(name, sizeof(name), "iop gte lwc2/swc2 off=%u", off);
        RunIOPCase(name, code, 4000);
    }

    // TODO(oracle): IOP exception probes (SYSCALL/BREAK/overflow) hang the
    // combined run at the exception vector under the shared budget. Isolate
    // them in a dedicated runner before enabling.
}

void IOPTests()
{
    EmuCoreXOracleSetSkipEvents(1);
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.WaitLoop = false;
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate emulator memory");
        return;
    }
    SysMemory::Reset();
    psxCpu = &psxRec;
    psxCpu->Reserve();

    struct IOPCase
    {
        const char* name;
        std::vector<u32> code;
    };
    std::vector<IOPCase> cases;

    cases.push_back({"alu_logic", {
        MipsI(15, 0, 8, 0x1234), MipsI(13, 8, 8, 0x5678),
        MipsI(15, 0, 9, 0x0fed), MipsI(13, 9, 9, 0xcba9),
        MipsR(8, 9, 10, 0, 0x21), MipsR(8, 9, 11, 0, 0x23),
        MipsR(8, 9, 12, 0, 0x24), MipsR(8, 9, 13, 0, 0x25),
        MipsR(8, 9, 14, 0, 0x26), MipsR(8, 9, 15, 0, 0x27),
        MipsR(8, 9, 16, 0, 0x2a), MipsR(8, 9, 17, 0, 0x2b),
    }});

    cases.push_back({"imm_shift", {
        MipsI(9, 0, 8, 0x7fff), MipsI(9, 0, 9, 3),
        MipsR(0, 8, 10, 7, 0x00), MipsR(0, 8, 11, 3, 0x02),
        MipsR(0, 8, 12, 3, 0x03), MipsR(9, 8, 13, 0, 0x04),
        MipsR(9, 8, 14, 0, 0x06), MipsR(9, 8, 15, 0, 0x07),
        MipsI(12, 8, 16, 0x0f0f), MipsI(14, 8, 17, 0x0f0f),
        MipsI(15, 0, 18, 0xdead), MipsI(9, 8, 19, 0x8000),
    }});

    cases.push_back({"mult_div", {
        MipsI(9, 0, 8, 1234), MipsI(9, 0, 9, 0xfff9),
        MipsR(8, 9, 0, 0, 0x18), MipsR(0, 0, 10, 0, 0x10), MipsR(0, 0, 11, 0, 0x12),
        MipsR(8, 9, 0, 0, 0x1a), MipsR(0, 0, 12, 0, 0x10), MipsR(0, 0, 13, 0, 0x12),
        MipsR(8, 9, 0, 0, 0x1b), MipsR(0, 0, 14, 0, 0x10), MipsR(0, 0, 15, 0, 0x12),
        MipsR(8, 9, 0, 0, 0x19), MipsR(0, 0, 16, 0, 0x10), MipsR(0, 0, 17, 0, 0x12),
    }});

    cases.push_back({"load_store", {
        MipsI(15, 0, 8, 0x0018), MipsI(13, 8, 8, 0x0000),
        MipsI(9, 0, 9, 0x1234),
        MipsI(43, 8, 9, 4), MipsI(35, 8, 10, 4),
        MipsI(40, 8, 9, 8), MipsI(36, 8, 11, 8),
        MipsI(9, 0, 12, 0xfffe),
        MipsI(41, 8, 12, 12), MipsI(33, 8, 13, 12), MipsI(37, 8, 14, 12),
        MipsI(15, 0, 15, 0xdead), MipsI(13, 15, 15, 0xbeef),
        MipsI(38, 8, 16, 16), MipsI(42, 8, 16, 20),            // lwl/swl pair
    }});

    for (const IOPCase& test : cases)
    {
        const IOPSnapshot interp = RunIOPProgram(false, test.code);
        const IOPSnapshot jit = RunIOPProgram(true, test.code);
        std::printf("IOPCYCLES %s interp=%u jit=%u\n", test.name, interp.cycle, jit.cycle);
        CompareIOP(interp, jit, test.name);
    }

    IOPCoverage();
    IOPCoverageGte();
}
}

bool EmuCoreXOracleRecordGif(const u8* data, u32 size, bool liveCopy)
{
    constexpr size_t limit = 16 * 1024 * 1024;
    if (!gifOutput)
    {
        if (liveCopy && !pendingLivePath.empty())
        {
            if (size > limit - pendingLiveGif.size())
                pendingLiveGifOverflow = true;
            else
                pendingLiveGif.insert(pendingLiveGif.end(), data, data + size);
        }
        // Live recording must still deliver the packet to the real GS.
        return false;
    }
    if (size > limit - gifOutput->size())
        gifOverflow = true;
    else
        gifOutput->insert(gifOutput->end(), data, data + size);
    return true;
}

bool EmuCoreXOracleAllowVU1Fallback(u32 startPC)
{
    // Optional one-launch filter for the existing family policy. This narrows
    // differential diagnosis without changing individual-instruction pipelines.
    static const std::vector<u32> entries = [] {
        std::vector<u32> result;
        const std::string path = EmuFolders::Logs + "/vu-oracle-fallback-pc.request";
        if (FILE* file = std::fopen(path.c_str(), "rb"))
        {
            u32 pc;
            while (result.size() < 64 && std::fscanf(file, "%x", &pc) == 1)
            {
                if (pc < 0x4000 && !(pc & 7))
                    result.push_back(pc);
            }
            std::fclose(file);
            std::remove(path.c_str());
            __android_log_print(ANDROID_LOG_INFO, "EmuCoreX",
                "VU oracle: family fallback restricted to %zu entry PCs", result.size());
        }
        return result;
    }();
    return entries.empty() || std::find(entries.begin(), entries.end(), startPC) != entries.end();
}

void EmuCoreXOracleCaptureVU1(u32 startPC)
{
    // Called on the EE thread after the previous program finished. MTVU
    // dispatch bypasses this hook; no cross-thread register snapshots.
    static thread_local u32 poll = 1023, remaining = 0, stride = 32, position = 0, sequence = 0;
    static thread_local u32 capturePC = ~0u;
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
        u32 requestedPC = ~0u;
        const int fields = std::fscanf(file, "%u %u %x", &count, &interval, &requestedPC);
        std::fclose(file);
        std::remove(request.c_str());
        if (fields < 1 || !count || count > 256 || !interval || interval > 65536 ||
            (fields == 3 && (requestedPC >= 0x4000 || (requestedPC & 7))))
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
        capturePC = requestedPC;
        stride = interval;
        position = sequence = 0;
        __android_log_print(ANDROID_LOG_INFO, "EmuCoreX",
            "VU oracle: capturing %u inputs to %s (VU1 jit=%u familyMask=0x%llx flagHack=%u)",
            count, directory.c_str(), static_cast<unsigned>(EmuConfig.Cpu.Recompiler.EnableVU1),
            static_cast<unsigned long long>(OpcodeFamilies::g_familyMask[OpcodeFamilies::CORE_VU1]),
            static_cast<unsigned>(EmuConfig.Speedhacks.vuFlagHack));
    }
    if (capturePC != ~0u && startPC != capturePC)
        return;
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
    pendingLiveInput = input;
    pendingLiveGif.clear();
    pendingLiveGifOverflow = false;
    pendingLivePath = path + ".actual";
    if (!--remaining)
        __android_log_print(ANDROID_LOG_INFO, "EmuCoreX", "VU oracle: capture finished (%u inputs)", sequence);
}

void EmuCoreXOracleFinishVU1()
{
    if (pendingLivePath.empty())
        return;
    const std::string path = std::move(pendingLivePath);
    pendingLivePath.clear();
    // This first paired format supports completion in the initial dispatch.
    // Never mistake an intermediate yield for the final architecture state.
    if (VU0.VI[REG_VPU_STAT].UL & 0x100)
    {
        __android_log_print(ANDROID_LOG_WARN, "EmuCoreX", "VU oracle: no final result for yielded dispatch %s", path.c_str());
        return;
    }
    auto& result = pendingLiveInput;
    result.vuCycle = VU1.cycle;
    std::memcpy(result.vf, VU1.VF, sizeof(result.vf));
    std::memcpy(result.vi, VU1.VI, sizeof(result.vi));
    std::memcpy(result.acc, &VU1.ACC, sizeof(result.acc));
    std::memcpy(result.memory, VU1.Mem, sizeof(result.memory));
    FILE* file = std::fopen(path.c_str(), "wb");
    bool ok = file && std::fwrite(&result, sizeof(result), 1, file) == 1;
    if (file && std::fclose(file) != 0)
        ok = false;
    if (!ok)
        __android_log_print(ANDROID_LOG_ERROR, "EmuCoreX", "VU oracle: failed writing live result %s", path.c_str());
    if (ok && !pendingLiveGifOverflow)
    {
        FILE* gifFile = std::fopen((path + ".gif.bin").c_str(), "wb");
        bool gifOk = gifFile && (pendingLiveGif.empty() ||
            std::fwrite(pendingLiveGif.data(), 1, pendingLiveGif.size(), gifFile) == pendingLiveGif.size());
        if (gifFile && std::fclose(gifFile) != 0)
            gifOk = false;
        if (!gifOk)
        {
            std::remove((path + ".gif.bin").c_str());
            __android_log_print(ANDROID_LOG_ERROR, "EmuCoreX", "VU oracle: failed writing live GIF %s", path.c_str());
        }
    }
    else if (pendingLiveGifOverflow)
        __android_log_print(ANDROID_LOG_ERROR, "EmuCoreX", "VU oracle: live GIF overflow %s", path.c_str());
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
    auto run = [&](bool jit, bool resetCache = true) {
        const FPControlRegisterBackup callerFPCR(EmuConfig.Cpu.FPUFPCR);
        ApplyCapture(input);
        BaseVUmicroCPU* cpu = jit ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU1) : &CpuIntVU1;
        if (resetCache)
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
    if (FILE* liveFile = std::fopen((std::string(path) + ".actual").c_str(), "rb"))
    {
        VU1Capture live;
        const bool validLive = std::fread(&live, sizeof(live), 1, liveFile) == 1 &&
            std::fgetc(liveFile) == EOF && live.magic == input.magic && live.version == input.version &&
            live.byteSize == sizeof(live) && live.pc == input.pc &&
            std::memcmp(live.micro, input.micro, sizeof(live.micro)) == 0;
        std::fclose(liveFile);
        Check(validLive, "live result matches input format and microprogram");
        if (validLive)
        {
            Check(CpuDiff("live VU1", "VF", expected.regs.VF, live.vf, sizeof(live.vf)), "live VU1 VF vs interpreter");
            Check(CpuDiff("live VU1", "VI", expected.regs.VI, live.vi, sizeof(live.vi)), "live VU1 VI vs interpreter");
            Check(CpuDiff("live VU1", "ACC", &expected.regs.ACC, live.acc, sizeof(live.acc)), "live VU1 ACC vs interpreter");
            Check(CpuDiff("live VU1", "memory", expected.memory.data(), live.memory, sizeof(live.memory)), "live VU1 memory vs interpreter");
        }
    }
    const auto actual = run(true);
    if (FILE* liveGifFile = std::fopen((std::string(path) + ".actual.gif.bin").c_str(), "rb"))
    {
        std::vector<u8> liveGif;
        u8 chunk[4096];
        size_t count;
        while ((count = std::fread(chunk, 1, sizeof(chunk), liveGifFile)) != 0 && liveGif.size() <= 16 * 1024 * 1024)
            liveGif.insert(liveGif.end(), chunk, chunk + count);
        const bool validGif = !std::ferror(liveGifFile) && std::feof(liveGifFile) && liveGif.size() <= 16 * 1024 * 1024;
        std::fclose(liveGifFile);
        std::printf("LIVE GIF bytes=%zu interpreter=%zu jit=%zu\n", liveGif.size(), expected.gif.size(), actual.gif.size());
        Check(validGif && liveGif == expected.gif, "live GIF vs interpreter");
        Check(validGif && liveGif == actual.gif, "live GIF vs isolated JIT");
    }
    std::printf("INPUT pc=%04x clamp=%u fixes=%u speed=%u GIF interpreter=%zu jit=%zu\n",
        input.pc, input.clamp, input.fixes, input.speed, expected.gif.size(), actual.gif.size());
    Compare(expected, actual, "captured VU1 program");
    if (!fallback)
    {
        // Reuse the emitted blocks with the same canonical input. Resetting
        // before every replay cannot expose stale JIT block state.
        for (u32 iteration = 0; iteration < 3; ++iteration)
            Compare(expected, run(true, false), "captured VU1 program cached replay");
    }
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
    else if (std::strcmp(suite, "vectors") == 0)
        VectorsTests();
    else if (std::strcmp(suite, "ee") == 0)
        EETests();
    else if (std::strcmp(suite, "iop") == 0)
        IOPTests();
    else
        return 2;
    std::printf("RESULT checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
