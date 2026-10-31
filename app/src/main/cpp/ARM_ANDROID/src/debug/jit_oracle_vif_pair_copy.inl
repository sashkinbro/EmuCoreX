// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include inside jit_oracle.cpp's anonymous namespace, with Vif_Dynarec.h at TU scope.
extern "C" u32 EmuCoreXOracleCompileVifPairCopy(u32, const nVifBlock*, u8*, u32, bool);

namespace VifPairCopyOracle
{
    using Row = std::pair<u32,u32>;
    std::vector<Row> Rows(const nVifBlock& block)
    {
        const u32 count=block.num?block.num:256;
        const u32 wl=block.wl?block.wl:256;
        const bool fill=block.cl<wl;
        u32 source=0,dest=0,cycle=0;
        std::vector<Row> rows;
        for(u32 row=0;row<count;++row)
        {
            rows.emplace_back(dest,source);
            dest+=16;
            if(!fill || cycle<block.cl) source+=16;
            if(++cycle==wl)
            {
                cycle=0;
                if(!fill && row+1<count) dest+=(block.cl-wl)*16;
            }
        }
        return rows;
    }
    u32 PairCount(const std::vector<Row>& rows)
    {
        u32 count=0;
        for(size_t i=0;i<rows.size();)
            if(i+1<rows.size() && rows[i+1].first==rows[i].first+16 &&
                rows[i+1].second==rows[i].second+16) {++count;i+=2;} else ++i;
        return count;
    }
    bool PairInstructions(const u8* code,u32 bytes,u32 expected)
    {
        u32 loads=0,stores=0;
        bool operands=true;
        for(u32 at=0;at<bytes;at+=4)
        {
            u32 word;std::memcpy(&word,code+at,4);
            const bool load=(word&0xffc00000u)==0xad400000u;
            const bool store=(word&0xffc00000u)==0xad000000u;
            if(!load&&!store)continue;
            loads+=load;stores+=store;
            operands&=(word&31)==0 && ((word>>10)&31)==1 &&
                ((word>>5)&31)==(load?1u:0u) && ((word>>15)&127)<=63;
        }
        return operands&&loads==expected&&stores==expected;
    }
    void Seed(vifStruct& vif)
    {
        for(u32 i=0;i<16;++i)
        {
            reinterpret_cast<u8*>(&vif.MaskRow)[i]=static_cast<u8>(0x31+i*7);
            reinterpret_cast<u8*>(&vif.MaskCol)[i]=static_cast<u8>(0xb3-i*3);
        }
    }
    std::array<u8,32> RowCol(const vifStruct& vif)
    {
        std::array<u8,32> state{};
        std::memcpy(state.data(),&vif.MaskRow,16);
        std::memcpy(state.data()+16,&vif.MaskCol,16);
        return state;
    }
}

void RunVifPairCopyTests()
{
    using namespace VifPairCopyOracle;
    cpuinfo_initialize();
    const auto savedConfig=EmuConfig;
    EmuConfig=Pcsx2Config();EmuConfig.Speedhacks.vuThread=false;
    if(!SysMemory::Allocate()) {EmuConfig=savedConfig;Check(false,"allocate native VIF pair-copy memory");return;}
    const auto savedVif0=vif0,savedVif1=vif1;
    const auto savedRegs0=vif0Regs,savedRegs1=vif1Regs;
    struct Restore
    {
        const Pcsx2Config& config;const vifStruct& v0;const vifStruct& v1;
        const VIFregisters& r0;const VIFregisters& r1;
        ~Restore(){EmuConfig=config;vif0=v0;vif1=v1;vif0Regs=r0;vif1Regs=r1;}
    } restore{savedConfig,savedVif0,savedVif1,savedRegs0,savedRegs1};
    u8* const legacyCode=SysMemory::GetEERec()+0x2100000;
    u8* const candidateCode=SysMemory::GetEERec()+0x2200000;
    constexpr u32 capacity=0x10000,padding=64;
    std::vector<u8> source(8192+128);
    for(size_t i=0;i<source.size();++i)source[i]=static_cast<u8>((i*71+(i>>3)*29)^0xa5);
    const auto originalSource=source;
    u64 cases=0,legacyBytes=0,candidateBytes=0,expectedPairs=0;
    bool allData=true,allState=true,allPairs=true,allSavings=true;
    constexpr std::array<u8,10> counts{1,2,3,63,64,65,127,128,255,0};
    constexpr std::array<std::array<u8,2>,10> cycles{{{1,1},{2,2},{0,0},{3,1},{3,2},
        {1,3},{2,3},{0,3},{255,2},{255,3}}};
    for(u32 index=0;index<2;++index)
    for(u8 num:counts)for(const auto& cycle:cycles)for(bool usn:{false,true})
    {
        nVifBlock block{};block.num=num;block.upkType=0xc|(usn?0x20:0);
        block.cl=cycle[0];block.wl=cycle[1];block.mode=0;block.mask=0x91f07ce3;
        block.aligned=static_cast<u8>(cases&1);
        const auto rows=Rows(block);
        const u32 pairs=PairCount(rows);
        const u32 span=rows.back().first+16;
        const u32 before=EmuCoreXOracleCompileVifPairCopy(index,&block,legacyCode,capacity,false);
        const u32 after=EmuCoreXOracleCompileVifPairCopy(index,&block,candidateCode,capacity,true);
        allPairs&=PairInstructions(candidateCode,after,pairs);
        if(block.cl==block.wl&&pairs) allSavings&=after<before;
        legacyBytes+=before;candidateBytes+=after;expectedPairs+=pairs;
        for(u32 alignment:{0u,1u,4u,15u})
        {
            std::vector<u8> expected(span+padding*2,0x5b),legacy=expected,candidate=expected;
            for(const auto& row:rows)
                std::memcpy(expected.data()+padding+row.first,source.data()+padding+alignment+row.second,16);
            vifStruct& vif=index?vif1:vif0;
            Seed(vif);const auto state=RowCol(vif);
            reinterpret_cast<nVifrecCall>(legacyCode)(reinterpret_cast<uptr>(legacy.data()+padding),
                reinterpret_cast<uptr>(source.data()+padding+alignment));
            const auto oldState=RowCol(vif);
            Seed(vif);
            reinterpret_cast<nVifrecCall>(candidateCode)(reinterpret_cast<uptr>(candidate.data()+padding),
                reinterpret_cast<uptr>(source.data()+padding+alignment));
            if((legacy!=expected||candidate!=expected)&&allData)
                std::printf("VIFPAIR mismatch idx=%u num=%u cl=%u wl=%u align=%u\n",index,num,block.cl,block.wl,alignment);
            allData&=legacy==expected&&candidate==expected;
            allState&=oldState==state&&RowCol(vif)==state;
            ++cases;
        }
    }
    Check(allData,"V4-32 actual paired and legacy routines match independent rows and full canaries");
    Check(allState,"unmasked mode0 pair copies preserve every row and column byte");
    Check(source==originalSource,"paired copies never modify packet data or source canaries");
    Check(allPairs&&expectedPairs>0,"V4-32 emits exactly the adjacent source/destination Q0 Q1 pairs");
    Check(allSavings&&candidateBytes<legacyBytes,"contiguous V4-32 kernels use fewer actual instructions");
    std::printf("VIFPAIR cases=%llu pairs=%llu legacyBytes=%llu candidateBytes=%llu\n",
        static_cast<unsigned long long>(cases),static_cast<unsigned long long>(expectedPairs),
        static_cast<unsigned long long>(legacyBytes),static_cast<unsigned long long>(candidateBytes));

    bool fallbackData=true,fallbackState=true,fallbackShape=true;
    for(u32 index=0;index<2;++index)for(u32 mode=0;mode<4;++mode)
    for(bool masked:{false,true})for(u32 format:{0xcu,0xdu,0xeu,0x4u})
    {
        if(!mode&&!masked&&format==0xc)continue;
        nVifBlock block{};block.num=9;block.cl=3;block.wl=2;block.upkType=format|(masked?0x10:0);
        block.mode=mode;block.mask=0xe4936c1b;
        const u32 before=EmuCoreXOracleCompileVifPairCopy(index,&block,legacyCode,capacity,false);
        const u32 after=EmuCoreXOracleCompileVifPairCopy(index,&block,candidateCode,capacity,true);
        std::vector<u8> legacy(512,0x67),candidate=legacy;
        vifStruct& vif=index?vif1:vif0;
        Seed(vif);
        reinterpret_cast<nVifrecCall>(legacyCode)(reinterpret_cast<uptr>(legacy.data()+padding),reinterpret_cast<uptr>(source.data()+padding));
        const auto oldState=RowCol(vif);Seed(vif);
        reinterpret_cast<nVifrecCall>(candidateCode)(reinterpret_cast<uptr>(candidate.data()+padding),reinterpret_cast<uptr>(source.data()+padding));
        fallbackData&=legacy==candidate;
        fallbackState&=oldState==RowCol(vif);
        // Address helpers can emit PC-relative operands. Compare the legacy
        // variant at the identical placement after executing the candidate.
        const std::vector<u8> emittedCandidate(candidateCode,candidateCode+after);
        const u32 sameAddressBytes=EmuCoreXOracleCompileVifPairCopy(index,&block,candidateCode,capacity,false);
        fallbackShape&=after==sameAddressBytes&&
            std::memcmp(emittedCandidate.data(),candidateCode,after)==0;
    }
    Check(fallbackData&&fallbackState,"mask mode and non-V4-32 fallbacks retain exact output and row column state");
    Check(fallbackShape,"ineligible VIF routines retain the original emitted instructions");

    // Exercise the production length/cache/wrap selector, not only the raw routine.
    VifUnpackSSE_Init();
    bool wrappedData=true,cacheLength=true;
    for(u32 index=0;index<2;++index)
    {
        resetNewVif(index);
        const u32 size=index?0x4000:0x1000;
        for(u32 tail:{0u,16u,32u,64u})for(u8 num:{u8(2),u8(3),u8(0)})
        for(const auto& cycle:std::array<std::array<u8,2>,3>{{{1,1},{3,2},{1,3}}})
        {
            nVifBlock key{};key.num=num;key.upkType=0xc;key.cl=cycle[0];key.wl=cycle[1];
            const auto rows=Rows(key);const u32 span=rows.back().first+16;
            const u32 addr=tail?size-tail:0;
            std::vector<u8> expected(size);
            for(u32 i=0;i<size;++i) expected[i]=static_cast<u8>(i*17+0x41);
            VURegs& vu=index?VU1:VU0;
            std::memcpy(vu.Mem,expected.data(),size);
            for(const auto& row:rows)
                std::memcpy(expected.data()+((addr+row.first)&(size-16)),source.data()+padding+row.second,16);
            vifStruct& vif=index?vif1:vif0;VIFregisters& regs=index?vif1Regs:vif0Regs;
            Seed(vif);vif.cmd=0xc;vif.usn=0;vif.cl=0;vif.tag.addr=addr;vif.start_aligned=0;
            regs.num=num?num:256;regs.mode=0;regs.mask=0;regs.cycle.cl=key.cl;regs.cycle.wl=key.wl;
            const bool fill=key.cl<key.wl;
            if(index)dVifUnpack<1>(source.data()+padding,fill);else dVifUnpack<0>(source.data()+padding,fill);
            wrappedData&=std::memcmp(vu.Mem,expected.data(),size)==0;
            key.key0=0;key.key1=(u32(key.wl)<<24)|(u32(key.cl)<<16);
            const nVifBlock* cached=nVif[index].vifBlocks.find(key);
            cacheLength&=cached&&cached->length==std::min(span,0xffffu);
        }
    }
    Check(wrappedData,"production VIF dispatch handles exact ends and VU0 VU1 wrap with paired copy cache entries");
    Check(cacheLength,"cached VIF lengths preserve NUM0 decoding and skip fill destination bounds");
}
