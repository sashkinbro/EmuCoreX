//
// Created by k2154 on 2025-08-18.
//

#ifndef PCSX2_VTLBDEF_H
#define PCSX2_VTLBDEF_H

#include "MemoryTypes.h"
#include "common/SingleRegisterTypes.h"

// Specialized function pointers for each read type
typedef  mem8_t vtlbMemR8FP(u32 addr);
typedef  mem16_t vtlbMemR16FP(u32 addr);
typedef  mem32_t vtlbMemR32FP(u32 addr);
typedef  mem64_t vtlbMemR64FP(u32 addr);
typedef  RETURNS_R128 vtlbMemR128FP(u32 addr);

// Specialized function pointers for each write type
typedef  void vtlbMemW8FP(u32 addr,mem8_t data);
typedef  void vtlbMemW16FP(u32 addr,mem16_t data);
typedef  void vtlbMemW32FP(u32 addr,mem32_t data);
typedef  void vtlbMemW64FP(u32 addr,mem64_t data);
typedef  void TAKES_R128 vtlbMemW128FP(u32 addr,r128 data);

template <size_t Width, bool Write> struct vtlbMemFP;

template<> struct vtlbMemFP<  8, false> { typedef vtlbMemR8FP   fn; static const uptr Index = 0; };
template<> struct vtlbMemFP< 16, false> { typedef vtlbMemR16FP  fn; static const uptr Index = 1; };
template<> struct vtlbMemFP< 32, false> { typedef vtlbMemR32FP  fn; static const uptr Index = 2; };
template<> struct vtlbMemFP< 64, false> { typedef vtlbMemR64FP  fn; static const uptr Index = 3; };
template<> struct vtlbMemFP<128, false> { typedef vtlbMemR128FP fn; static const uptr Index = 4; };
template<> struct vtlbMemFP<  8,  true> { typedef vtlbMemW8FP   fn; static const uptr Index = 0; };
template<> struct vtlbMemFP< 16,  true> { typedef vtlbMemW16FP  fn; static const uptr Index = 1; };
template<> struct vtlbMemFP< 32,  true> { typedef vtlbMemW32FP  fn; static const uptr Index = 2; };
template<> struct vtlbMemFP< 64,  true> { typedef vtlbMemW64FP  fn; static const uptr Index = 3; };
template<> struct vtlbMemFP<128,  true> { typedef vtlbMemW128FP fn; static const uptr Index = 4; };

typedef u32 vtlbHandler;

namespace vtlb_private
{
    static const uint VTLB_PAGE_BITS = 12;
    static const uint VTLB_PAGE_MASK = 4095;
    static const uint VTLB_PAGE_SIZE = 4096;

    static const uint VTLB_PMAP_SZ		= _1mb * 512;
    static const uint VTLB_PMAP_ITEMS	= VTLB_PMAP_SZ / VTLB_PAGE_SIZE;
    static const uint VTLB_VMAP_ITEMS	= _4gb / VTLB_PAGE_SIZE;

    static const uint VTLB_HANDLER_ITEMS = 128;

    static const uptr POINTER_SIGN_BIT = 1ULL << (sizeof(uptr) * 8 - 1);

    struct VTLBPhysical
    {
    private:
        sptr value;
        explicit VTLBPhysical(sptr value): value(value) { }
    public:
        VTLBPhysical(): value(0) {}
        /// Create from a pointer to raw memory
        static VTLBPhysical fromPointer(void *ptr) { return fromPointer((sptr)ptr); }
        /// Create from an integer representing a pointer to raw memory
        static VTLBPhysical fromPointer(sptr ptr);
        /// Create from a handler and address
        static VTLBPhysical fromHandler(vtlbHandler handler);

        /// Get the raw value held by the entry
        uptr raw() const { return value; }
        /// Returns whether or not this entry is a handler
        bool isHandler() const { return value < 0; }
        /// Assumes the entry is a pointer, giving back its value
        uptr assumePtr() const { return value; }
        /// Assumes the entry is a handler, and gets the raw handler ID
        u8 assumeHandler() const { return value; }
    };

    struct VTLBVirtual
    {
    private:
        uptr value;
        explicit VTLBVirtual(uptr value): value(value) { }
    public:
        VTLBVirtual(): value(0) {}
        VTLBVirtual(VTLBPhysical phys, u32 paddr, u32 vaddr);
        static VTLBVirtual fromPointer(uptr ptr, u32 vaddr) {
            return VTLBVirtual(VTLBPhysical::fromPointer(ptr), 0, vaddr);
        }

        /// Get the raw value held by the entry
        uptr raw() const { return value; }
        /// Returns whether or not this entry is a handler
        bool isHandler(u32 vaddr) const { return (sptr)(value + vaddr) < 0; }
        /// Assumes the entry is a pointer, giving back its value
        uptr assumePtr(u32 vaddr) const { return value + vaddr; }
        /// Assumes the entry is a handler, and gets the raw handler ID
        u8 assumeHandlerGetID() const { return value; }
        /// Assumes the entry is a handler, and gets the physical address
        u32 assumeHandlerGetPAddr(u32 vaddr) const { return (value + vaddr - assumeHandlerGetID()) & ~POINTER_SIGN_BIT; }
        /// Assumes the entry is a handler, returning it as a void*
        void *assumeHandlerGetRaw(int index, bool write) const;
        /// Assumes the entry is a handler, returning it
        template <size_t Width, bool Write>
        typename vtlbMemFP<Width, Write>::fn *assumeHandler() const;
    };

    struct MapData
    {
        // first indexer -- 8/16/32/64/128 bit tables [values 0-4]
        // second indexer -- read/write  [0 or 1]
        // third indexer -- 128 possible handlers!
        void* RWFT[5][2][VTLB_HANDLER_ITEMS];

        VTLBPhysical pmap[VTLB_PMAP_ITEMS]; //512KB // PS2 physical to x86 physical

        VTLBVirtual* vmap;                //4MB (allocated by vtlb_init) // PS2 virtual to x86 physical

        u32* ppmap;               //4MB (allocated by vtlb_init) // PS2 virtual to PS2 physical

        uptr fastmem_base;

        MapData()
        {
            vmap = NULL;
            ppmap = NULL;
            fastmem_base = 0;
        }
    };

    extern MapData& vtlbdata;

    inline void *VTLBVirtual::assumeHandlerGetRaw(int index, bool write) const
    {
        return vtlbdata.RWFT[index][write][assumeHandlerGetID()];
    }

    template <size_t Width, bool Write>
    typename vtlbMemFP<Width, Write>::fn *VTLBVirtual::assumeHandler() const
    {
        using FP = vtlbMemFP<Width, Write>;
        return (typename FP::fn *)assumeHandlerGetRaw(FP::Index, Write);
    }
}

#endif //PCSX2_VTLBDEF_H
