// SPDX-FileCopyrightText: 2026 EmuCoreX Contributors
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "common/Pcsx2Types.h"
namespace IopThread
{
// The EE owns this adapter. Observe at every event/watermark; reset under a hold
// after state load. One epoch of headroom represents a slightly lagging IOP at wrap.
class CycleClock32
{
public:
    u64 Observe(u32 cycle)
    {
        m_extended += static_cast<u32>(cycle - m_previous);
        m_previous = cycle;
        return m_extended;
    }
    void Reset(u32 cycle) { m_previous = cycle; m_extended = (u64{1} << 32) + cycle; }
private:
    u32 m_previous = 0;
    u64 m_extended = u64{1} << 32;
};
}
