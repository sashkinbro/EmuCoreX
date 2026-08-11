// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

static const u32 BIAS = 2;				// Bus is half of the actual ps2 speed
static const u32 PS2CLK_DEFAULT = 294912000; // PS2 console / System 246
static const u32 PS2CLK_S256 = 393216000; // System 256 (294.912 * 4/3)
static const u32 PS2CLK_SS256 = 442368000; // Super System 256 (294.912 * 3/2)
extern u32 PS2CLK;
extern u32 PSXCLK;


#include "Memory.h"
#include "R5900.h"
#include "Hw.h"
#include "Dmac.h"

#include "SaveState.h"
#include "DebugTools/Debug.h"

#include <string>

extern std::string ShiftJIS_ConvertString( const char* src );
extern std::string ShiftJIS_ConvertString( const char* src, int maxlen );
