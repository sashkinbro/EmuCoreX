// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <cstddef>

struct tlbs;

extern void WriteCP0Status(u32 value);
extern void WriteCP0Config(u32 value);
extern void cpuUpdateOperationMode();
extern void WriteTLB(int i);
extern void UnmapTLB(const tlbs& t, int i);
extern void MapTLB(const tlbs& t, int i);
extern void RefreshTLBEntryMapping(const tlbs& previous, int i);
extern void RestoreTLBMappingsFromSnapshot(const tlbs* previous, size_t count, bool only_changed_entries, bool apply_goemon_fix);
extern void ClearTLBMappingsFromSnapshot(const tlbs* entries, size_t count);

extern void COP0_UpdatePCCR();
extern void COP0_DiagnosticPCCR();
