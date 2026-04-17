// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#ifndef PCSX2_CORE_STATE_EXPOSURE_H
#define PCSX2_CORE_STATE_EXPOSURE_H

#include "../../arm64/cpuRegistersPack.h"

// Transitional seam for canonical state exposure. New direct field grabs from
// g_cpuRegistersPack should not be added outside this header.
#define CORE_STATE_RUNTIME_PACK (g_cpuRegistersPack)
#define CORE_STATE_CPU_PACK_VIEW (reinterpret_cast<cpuRegistersPack&>(CORE_STATE_RUNTIME_PACK))
#define CORE_STATE_CPU_REGS (CORE_STATE_CPU_PACK_VIEW.cpuRegs)
#define CORE_STATE_FPU_REGS (CORE_STATE_CPU_PACK_VIEW.fpuRegs)
#define CORE_STATE_PSX_REGS (CORE_STATE_RUNTIME_PACK.psxRegs)
#define CORE_STATE_VU_REGS (CORE_STATE_RUNTIME_PACK.vuRegs)
#define CORE_STATE_VTLB_DATA (CORE_STATE_RUNTIME_PACK.vtlbdata)

inline Arm64CpuRuntimePack* GetCoreStateRuntimePackPtr()
{
	return &CORE_STATE_RUNTIME_PACK;
}

#endif // PCSX2_CORE_STATE_EXPOSURE_H
