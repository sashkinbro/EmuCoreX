//
// Created by k2154 on 2025-07-30.
//

#ifndef PCSX2_CPUREGISTERSPACK_H
#define PCSX2_CPUREGISTERSPACK_H

#include "Config.h"
#include "R5900Def.h"
#include "R3000ADef.h"
#include "VUDef.h"
#include "vtlbDef.h"
#include "ShuffleLanes.h"

struct cpuRegistersPack
{
    // This packed state is a local ARM64 runtime contract used by the fork's
    // recompiler path. It intentionally keeps multiple EE/IOP/VU/runtime pieces
    // co-located so ARM64 helper code can address them through stable offsets.
    alignas(16) cpuRegisters cpuRegs{};
    alignas(16) fpuRegisters fpuRegs{};
    alignas(16) psxRegisters psxRegs{};
    alignas(16) VIFregisters vifRegs[2]{};
    alignas(16) VURegs vuRegs[2];
    alignas(16) ShuffleLanes shuffle;
    alignas(16) Pcsx2Config::CpuOptions Cpu;
    alignas(16) mVU_SSE4 mVUss4;
    alignas(32) mVU_Globals mVUglob;
    // Local ARM64 divergence: vtlb map state is stored inside the packed CPU
    // runtime block instead of as a standalone global. Do not separate this
    // without auditing recVTLB, fastmem, save-state, and helper offset users.
    alignas(64) vtlb_private::MapData vtlbdata;
};
alignas(64) extern cpuRegistersPack g_cpuRegistersPack;
////
static cpuRegisters& cpuRegs = g_cpuRegistersPack.cpuRegs;
static fpuRegisters& fpuRegs = g_cpuRegistersPack.fpuRegs;
static psxRegisters& psxRegs = g_cpuRegistersPack.psxRegs;
static VURegs& VU0 = g_cpuRegistersPack.vuRegs[0];
static VURegs& VU1 = g_cpuRegistersPack.vuRegs[1];
static VURegs (&vuRegs)[2] = g_cpuRegistersPack.vuRegs;

#endif //PCSX2_CPUREGISTERSPACK_H
