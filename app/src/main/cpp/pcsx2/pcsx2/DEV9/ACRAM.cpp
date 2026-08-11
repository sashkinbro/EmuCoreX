
#include "common/Console.h"
#include "ACRAM.h"
#include "MemoryTypes.h"
#include "IopMem.h"
#include <cstring>

#include "Config.h"
#define ACRAM_LOG(fmt, ...) if (EmuConfig.Arcade.RAMVerboseReads) Console.WriteLn(Color_Gray, "ACRAM:" fmt __VA_OPT__(,) __VA_ARGS__)

#define OOB_REPORT(T) Console.Error("%s: out of bound index: %08X", __FUNCTION__, T)
#define GET_RAM_OFF(addr) (((addr) - ACRAM_ADDR_BASE) / 2) // u8 buffer on u16 MMIO, halve the address to get real offset

ACRAM::BankState ACRAM::banks[ACRAM_NUM_BANKS] = {};

void ACRAM::Reset()
{
	std::memset(banks, 0, sizeof(banks));
}

u16 ACRAM::Read16(u32 addr) {
    u32 offset = addr - ACRAM_ADDR_BASE;
    u32 reg = offset & ACRAM_REG_MASK;
    // FPGA status registers: TK5DR polls reg 0x00-0x1F during boot, expecting 0x50 (ready).
    if (reg < 0x20)
        return 0x50;
    u32 off = GET_RAM_OFF(addr);
    if (off < ACRAM_MAX_SIZE)
        return iopMem->ACRAM[off];
    OOB_REPORT(addr);
    return 0;
}

// Track DMA pointers per bank (ACRAM_NUM_BANKS): without this, one bank's streaming writes clobber
// another bank's data/pointers and the game hangs at load. Ref: ps2sdk acram/src/ram.c
void ACRAM::Write16(u32 addr, u16 val) {
    u32 offset = addr - ACRAM_ADDR_BASE;
    int bank = (offset >> 21) & (ACRAM_NUM_BANKS - 1); // address bits 21+ pick the bank
    u32 reg = offset & ACRAM_REG_MASK;
    u32 bank_base = bank * ACRAM_BANK_SIZE;

    if (reg >= ACRAM_REG_READ && reg < ACRAM_REG_WRITE) {
        // One register write carries a full address, split in two:
        //   address = (value << 11) + (register offset & 0x7FC)
        // The value picks the 2KB page, the register offset picks the spot
        // inside that page (that's how the driver works — ps2sdk acram ram.c).
        banks[bank].read_addr = bank_base + ((u32)val << 11) + (reg & 0x7FC);
        return;
    } else if (reg >= ACRAM_REG_WRITE && reg < 0x80000) {
        banks[bank].write_addr = bank_base + ((u32)val << 11) + (reg & 0x7FC);
        return;
    } else if (reg >= 0x20000 && reg < ACRAM_REG_READ) {
        // Size/config registers: control only, nothing to store — the actual
        // transfer size comes from the DMA8 BCR.
        return;
    }

    u32 off = GET_RAM_OFF(addr);
    if (off < ACRAM_MAX_SIZE)
        iopMem->ACRAM[off] = (u8)(val & 0xFF);
    else
        OOB_REPORT(addr);
}

int ACRAM::BankFromDmaTarget(u32 dma_target) { // same bank select as Write16
    return ((dma_target - ACRAM_ADDR_BASE) >> 21) & (ACRAM_NUM_BANKS - 1);
}

void ACRAM::DmaRead(u32* iop_buf, u32 size_bytes, int bank) {
    u32& addr = banks[bank].read_addr;
    addr &= (ACRAM_MAX_SIZE - 1);
    ACRAM_LOG("DMARead  addr:%8X size:%8X bank:%d", addr, size_bytes, bank);
    if (addr + size_bytes <= ACRAM_MAX_SIZE) {
        std::memcpy(iop_buf, &iopMem->ACRAM[addr], size_bytes);
    } else {
        u32 first = ACRAM_MAX_SIZE - addr;
        std::memcpy(iop_buf, &iopMem->ACRAM[addr], first);
        std::memcpy((u8*)iop_buf + first, &iopMem->ACRAM[0], size_bytes - first);
    }
    addr = (addr + size_bytes) & (ACRAM_MAX_SIZE - 1);
}

void ACRAM::DmaWrite(u32* iop_buf, u32 size_bytes, int bank) {
    u32& addr = banks[bank].write_addr;
    addr &= (ACRAM_MAX_SIZE - 1);
    ACRAM_LOG("DMAWrite addr:%8X size:%8X bank:%d", addr, size_bytes, bank);
    if (addr + size_bytes <= ACRAM_MAX_SIZE) {
        std::memcpy(&iopMem->ACRAM[addr], iop_buf, size_bytes);
    } else {
        u32 first = ACRAM_MAX_SIZE - addr;
        std::memcpy(&iopMem->ACRAM[addr], iop_buf, first);
        std::memcpy(&iopMem->ACRAM[0], (u8*)iop_buf + first, size_bytes - first);
    }
    addr = (addr + size_bytes) & (ACRAM_MAX_SIZE - 1);
}
