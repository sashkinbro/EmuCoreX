#pragma once

#include "common/Pcsx2Types.h"
#include "common/Pcsx2Defs.h"

/**
 * @file ACRAM.h
 * Additional RAM memory living on the namco board
 * The RAM can be 32 or 64mb sized, or directly, not be there
 * System246 DRIVING : ACRAM is an external PCB, can be 0, 32, 64
 * System246 Rack A : Same than DRIVING unit
 * System246 Rack B : Builtin on the namco board, seems like 64mb version
 * System246 Rack C : same than Rack B
 * System256 : ACRAM is no longer part of the system
 *
 * 64MB = 2 x 32MB banks. Each bank has independent DMA read/write pointers.
 * Ref: ps2sdk/iop/arcade/acram/src/ram.c, ps2sdk/iop/arcade/accore/src/dma.c
 *
 * Address map (base = 0x14000000, bank select = bit 21):
 *   [base + (bank<<21) + 0x60000]  Read pointer register
 *   [base + (bank<<21) + 0x70000]  Write pointer register
 *   [base + (bank<<21) + 0x100000] DMA IO port (written to IOP 0x1F801410)
 *
 * Register value = addr >> 11. Reconstructed: bank_base + (val << 11).
 */

#define ACRAM_ADDR_BASE   0x14000000
#define ACRAM_RANGE       0x1400
#define ACRAM_MAX_SIZE    (_64mb * 2)
#define ACRAM_BANK_SIZE   0x2000000  // 32MB per bank
#define ACRAM_NUM_BANKS   4          // most games use 1 bank; TK4 needs 2, Wangan 4 (its RAM expansion). Use the max.
#define ACRAM_REG_READ    0x60000
#define ACRAM_REG_WRITE   0x70000
#define ACRAM_REG_MASK    0x1FFFFF   // isolate register offset within bank

namespace ACRAM
{
    struct BankState {
        u32 read_addr;
        u32 write_addr;
    };

    void Reset();
    u16 Read16(u32 addr);
    void Write16(u32 addr, u16 val);
    void DmaRead(u32* iop_buf, u32 size_bytes, int bank);
    void DmaWrite(u32* iop_buf, u32 size_bytes, int bank);
    int BankFromDmaTarget(u32 dma_target);
    extern BankState banks[ACRAM_NUM_BANKS];
}
