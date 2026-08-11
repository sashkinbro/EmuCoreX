#pragma once
#include "MemoryTypes.h"

/**
 * @brief source related to ACCORE.IRX MMIO and stuff
 *
 */


namespace ACCORE {
	void Reset();
    u16 Read16(u32 addr);
    void Write16(u32 addr, u16 val);
	void intr(int INTRN);
	void Interrupt(u32 mem, u16 val);
	bool hasPendingInterrupt();
	namespace DMA {
        enum TT {
            NONE = 0,
            ATA,
            ATAPI,
            ATA_WRITE,
        };
		extern enum TT PendTrasnfType;
	}
	enum {
		INTRN_ATA = 0x0,
		INTRN_JV = 0x1,
		INTRN_UART = 0x2,
		INTRN_LAST = 0x2,
	};
	enum INTC_CAUS {
		CAUS_ATA = 0x8000,
		CAUS_UART = 0x4000 // UART receive interrupt (drive-board serial link)
	};
}

#define ACCORE_INTR_ATA  			0x13000000
#define ACCORE_INTR_UART 			0x13100000
#define ACCORE_FPGA_BEGIN_PROGRAM 	0x12416008
#define ACCPRE_FPGA_FINISH_PROGRAM 	0x12416012
