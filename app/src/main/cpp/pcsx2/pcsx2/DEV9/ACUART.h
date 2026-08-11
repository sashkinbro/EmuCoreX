#pragma once
#include "MemoryTypes.h"

#define ACUART_BASE 0x12418000 // everything, both reg set and I/O is done in that little 0xFFF range
#define IS_ACUART_RANGE(a) ((a & 0xFFFFF000) == ACUART_BASE)

namespace ACUART {
    u16 Read16(u32 addr);
    void Write16(u32 addr, u16 val);
    void StreamV257(u32 cycles); // V257 drive-board status responder, ticked from DEV9async (RRV)
    void ResetBg3State();        // clear BG3 acuart HANDLE-handshake state on game boot (fixes HANDLE-ERROR-on-reset)
}
