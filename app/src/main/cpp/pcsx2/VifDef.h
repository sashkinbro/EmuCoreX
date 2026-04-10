//
// Created by k2154 on 2025-07-30.
//

#ifndef PCSX2_VIFDEF_H
#define PCSX2_VIFDEF_H

#include "common/Pcsx2Defs.h"
#include "common/StringUtil.h"
#include "MemoryTypes.h"

//
// Bitfield Structure
//
union tVIF_STAT {
    struct {
        u32 VPS : 2; // Vif(0/1) status; 00 - idle, 01 - waiting for data following vifcode, 10 - decoding vifcode, 11 - decompressing/trasferring data follwing vifcode.
        u32 VEW : 1; // E-bit wait (1 - wait, 0 - don't wait)
        u32 VGW : 1; // Status waiting for the end of gif transfer (Vif1 only)
        u32 _reserved : 2;
        u32 MRK : 1; // Mark Detect
        u32 DBF : 1; // Double Buffer Flag
        u32 VSS : 1; // Stopped by STOP
        u32 VFS : 1; // Stopped by ForceBreak
        u32 VIS : 1; // Vif Interrupt Stall
        u32 INT : 1; // Intereupt by the i bit.
        u32 ER0 : 1; // DmaTag Mismatch error.
        u32 ER1 : 1; // VifCode error
        u32 _reserved2 : 9;
        u32 FDR : 1; // VIF/FIFO transfer direction. (false - memory -> Vif, true - Vif -> memory)
        u32 FQC : 5; // Amount of data. Up to 8 qwords on Vif0, 16 on Vif1.
    };
    u32 _u32;

    tVIF_STAT() = default;
    tVIF_STAT(u32 val)			{ _u32 = val; }
    bool test(u32 flags) const	{ return !!(_u32 & flags); }
    void set_flags	(u32 flags)	{ _u32 |=  flags; }
    void clear_flags(u32 flags) { _u32 &= ~flags; }
    void reset()				{ _u32 = 0; }
    std::string desc() const		{ return StringUtil::StdStringFromFormat("Stat: 0x%x", _u32); }
};

union tVIF_ERR {
    struct {
        u32 MII : 1; // Masks Stat INT.
        u32 ME0 : 1; // Masks Stat Err0.
        u32 ME1 : 1; // Masks Stat Err1.
        u32 _reserved : 29;
    };
    u32 _u32;

    tVIF_ERR() = default;
    tVIF_ERR  (u32 val)					{ _u32 = val; }
    void write(u32 val)					{ _u32 = val; }
    bool test		(u32 flags) const	{ return !!(_u32 & flags); }
    void set_flags	(u32 flags)			{ _u32 |=  flags; }
    void clear_flags(u32 flags)			{ _u32 &= ~flags; }
    void reset()						{ _u32 = 0; }
    std::string desc() const				{ return StringUtil::StdStringFromFormat("Err: 0x%x", _u32); }
};

struct vifCycle
{
    u8 cl, wl;
    u8 pad[2];
};

struct VIFregisters {
    tVIF_STAT stat;
    u32 _pad0[3];
    u32 fbrst;
    u32 _pad1[3];
    tVIF_ERR err;
    u32 _pad2[3];
    u32 mark;
    u32 _pad3[3];
    vifCycle cycle; //data write cycle
    u32 _pad4[3];
    u32 mode;
    u32 _pad5[3];
    u32 num;
    u32 _pad6[3];
    u32 mask;
    u32 _pad7[3];
    u32 code;
    u32 _pad8[3];
    u32 itops;
    u32 _pad9[3];
    u32 base;      // Not used in VIF0
    u32 _pad10[3];
    u32 ofst;      // Not used in VIF0
    u32 _pad11[3];
    u32 tops;      // Not used in VIF0
    u32 _pad12[3];
    u32 itop;
    u32 _pad13[3];
    u32 top;       // Not used in VIF0
    u32 _pad14[3];
    u32 mskpath3;
    u32 _pad15[3];
    u32 r0;        // row0 register
    u32 _pad16[3];
    u32 r1;        // row1 register
    u32 _pad17[3];
    u32 r2;        // row2 register
    u32 _pad18[3];
    u32 r3;        // row3 register
    u32 _pad19[3];
    u32 c0;        // col0 register
    u32 _pad20[3];
    u32 c1;        // col1 register
    u32 _pad21[3];
    u32 c2;        // col2 register
    u32 _pad22[3];
    u32 c3;        // col3 register
    u32 _pad23[3];
    u32 offset;    // internal UNPACK offset
    u32 addr;
};

static VIFregisters& vif0Regs = (VIFregisters&)eeHw[0x3800];
static VIFregisters& vif1Regs = (VIFregisters&)eeHw[0x3C00];

#endif //PCSX2_VIFDEF_H
