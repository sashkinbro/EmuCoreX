// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after vif_pair_copy. Independent integer model plus actual compiler seams.
extern "C" u32 EmuCoreXOracleCompileVifNarrowLoad(u32,s64,u8*,u32);
namespace VifNarrowLoadOracle
{
    struct GuardedSource
    {
        void* map=MAP_FAILED;size_t size=0;u8* data=nullptr;
        explicit GuardedSource(u32 bytes)
        {
            const size_t page=static_cast<size_t>(sysconf(_SC_PAGESIZE));
            const size_t payload=(bytes+page-1)/page*page;size=payload+2*page;
            map=mmap(nullptr,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
            if(map==MAP_FAILED)return;
            auto* start=static_cast<u8*>(map)+page;
            if(mprotect(start,payload,PROT_READ|PROT_WRITE))return;
            data=start+payload-bytes;
            for(u32 i=0;i<bytes;++i)data[i]=static_cast<u8>((i*71+(i>>3)*29)^0xa5);
        }
        ~GuardedSource(){if(map!=MAP_FAILED)munmap(map,size);}
        bool ReadOnly(){return data&&mprotect(static_cast<u8*>(map)+sysconf(_SC_PAGESIZE),size-2*sysconf(_SC_PAGESIZE),PROT_READ)==0;}
    };
    u32 Scalar(const u8* source,u32 offset,u32 width,bool usn)
    {
        if(width==1)return usn?u32(source[offset]):u32(s32(static_cast<s8>(source[offset])));
        u16 v;std::memcpy(&v,source+offset,2);return usn?u32(v):u32(s32(static_cast<s16>(v)));
    }
    // Destination image and Row/Col are independent host words. Model expands
    // documented narrow fields, cached V2 pairs and V3 boundary W behavior.
    void Model(const nVifBlock& b,const u8* source,std::vector<u8>& dest,u32 address,
        std::array<u32,4>& row,const std::array<u32,4>& col,bool wrap)
    {
        const u32 format=b.upkType&15,components=(format>>2)+1,width=(format&3)==1?2:1;
        const bool usn=b.upkType&32,masked=b.upkType&16;
        const u32 count=b.num?b.num:256,wl=b.wl?b.wl:256;
        const bool fill=b.cl<wl;const u32 cycleSize=fill?b.cl:wl,blockSize=fill?wl:b.cl;
        u32 src=0,cycle=0,iteration=0;std::array<u32,4> cached{};
        for(u32 output=0;output<count;++output)
        {
            const bool consumed=cycle<cycleSize;
            if(consumed&&(components==3))++iteration;
            std::array<u32,4> value{};
            if(components==2)
            {
                if(iteration==0)for(u32 lane=0;lane<4;++lane)cached[lane]=Scalar(source,src+lane*width,width,usn);
                for(u32 lane=0;lane<4;++lane)value[lane]=cached[(iteration?2:0)+(lane&1)];
            }
            else
            {
                for(u32 lane=0;lane<4;++lane)value[lane]=Scalar(source,src+lane*width,width,usn);
                if(components==3)
                {
                    if(width==1&&iteration!=b.aligned)value[3]=0;
                    if(width==2&&(iteration&1)==0&&(((iteration/4)+1+4-b.aligned)&3)==0)value[3]=0;
                }
            }
            for(u32 lane=0;lane<4;++lane)
            {
                const u32 mask=masked?((b.mask>>(std::min(cycle,3u)*8+lane*2))&3):0;
                if(mask==3)continue;
                u32 v=value[lane];
                if(mask==1)v=row[lane];else if(mask==2)v=col[std::min(cycle,3u)];
                else
                {
                    if(b.mode==1||b.mode==2)v+=row[lane];
                    if(b.mode==2||b.mode==3)row[lane]=v;
                }
                const u32 at=wrap?(address&static_cast<u32>(dest.size()-16)):address;
                std::memcpy(dest.data()+at+lane*4,&v,4);
            }
            address+=16;
            if(consumed){src+=components*width;if(components==2)iteration^=1;}
            if(++cycle==blockSize)cycle=0;
            else if(!fill&&cycle==cycleSize){address+=(blockSize-cycleSize)*16;cycle=0;}
        }
    }
    std::array<u32,4> Words(const void* p){std::array<u32,4> v;std::memcpy(v.data(),p,16);return v;}
    void Setup(u32 index,const nVifBlock& b,u32 address)
    {
        auto& vif=index?vif1:vif0;auto& regs=index?vif1Regs:vif0Regs;
        VifPairCopyOracle::Seed(vif);vif.cmd=b.upkType&31;vif.usn=(b.upkType>>5)&1;
        vif.cl=0;vif.tag.addr=address;vif.start_aligned=b.aligned;
        regs.num=b.num?b.num:256;regs.mode=b.mode;regs.mask=b.mask;regs.cycle.cl=b.cl;regs.cycle.wl=b.wl;
    }
    bool DirectShape(const u8* code,u32 bytes,u32 width)
    {
        u32 direct=0,bridge=0;
        for(u32 at=0;at<bytes;at+=4)
        {
            u32 op;std::memcpy(&op,code+at,4);
            const u32 top=op&0xffc00000u;
            direct+=(width==4&&(top==0xbd400000u||top==0xbc400000u))||
                (width==8&&(top==0xfd400000u||top==0xfc400000u));
            bridge+=(op&0xfffffc00u)==0x1e270000u||(op&0xfffffc00u)==0x9e670000u;
        }
        return direct==1&&bridge==0;
    }
}
void RunVifNarrowLoadTests()
{
    using namespace VifNarrowLoadOracle;
    cpuinfo_initialize();const auto config=EmuConfig;EmuConfig=Pcsx2Config();EmuConfig.Speedhacks.vuThread=false;
    if(!SysMemory::Allocate()){EmuConfig=config;Check(false,"allocate narrow VIF memory");return;}
    const auto v0=vif0,v1=vif1;const auto r0=vif0Regs,r1=vif1Regs;
    const ScopedGuard restore([&]{EmuConfig=config;vif0=v0;vif1=v1;vif0Regs=r0;vif1Regs=r1;});
    u8* code=SysMemory::GetEERec()+0x2300000;constexpr u32 capacity=0x10000;
    bool widths=true,upper=true,guards=true;std::array<bool,2> shapes{true,true};
    for(u32 width:{4u,8u})for(s64 offset:{-257ll,-256ll,-1ll,0ll,1ll,255ll,256ll,32760ll,65537ll})
    {
        const u32 bytes=EmuCoreXOracleCompileVifNarrowLoad(width,offset,code,capacity);
        shapes[width==8]&=DirectShape(code,bytes,width);
        // Keep only the architecturally read bytes immediately before PROT_NONE.
        GuardedSource source(width);guards&=source.ReadOnly();if(!source.data)continue;
        std::array<u8,48> output;output.fill(0x63);std::array<u8,16> expected{};
        std::memcpy(expected.data(),source.data,width);
        reinterpret_cast<void(*)(uptr,u8*)>(code)(reinterpret_cast<uptr>(source.data)-offset,output.data()+16);
        widths&=std::memcmp(output.data()+16,expected.data(),width)==0;
        upper&=std::memcmp(output.data()+16+width,expected.data()+width,16-width)==0;
        guards&=std::all_of(output.begin(),output.begin()+16,[](u8 v){return v==0x63;})&&
            std::all_of(output.begin()+32,output.end(),[](u8 v){return v==0x63;});
    }
    Check(widths&&guards,"actual narrow wrappers read exactly4/8 guarded bytes with signed and large address offsets");
    Check(upper,"actual narrow loads clear all dirty upper128-bit lanes while preserving source bits");
    Check(shapes[0],"actual4-byte wrapper emits one scalar SIMD load and no GPR to SIMD bridge");
    Check(shapes[1],"actual8-byte wrapper emits one scalar SIMD load and no GPR to SIMD bridge");

    std::vector<u8> source(8192+128);for(u32 i=0;i<source.size();++i)source[i]=static_cast<u8>((i*71+(i>>3)*29)^0xa5);
    const auto original=source;u64 cases=0;bool data=true,state=true;
    constexpr std::array<std::array<u8,2>,4> cycles{{{1,1},{3,2},{1,3},{0,0}}};
    for(u32 index=0;index<2;++index)for(u32 format:{5u,6u,9u,10u,13u,14u})
    for(bool usn:{false,true})for(bool mask:{false,true})for(u32 mode=0;mode<4;++mode)
    for(u8 num:{u8(3),u8(0)})for(const auto& cycle:cycles)for(u32 align:{0u,1u,7u,15u})
    {
        nVifBlock b{};b.upkType=format|(usn?32:0)|(mask?16:0);b.num=num;b.mode=mode;
        b.cl=cycle[0];b.wl=cycle[1];b.aligned=format==9?(cases&3):(cases&1);b.mask=0xe4936c1b;
        Setup(index,b,64);auto& vif=index?vif1:vif0;
        auto row=Words(&vif.MaskRow);const auto col=Words(&vif.MaskCol);
        std::vector<u8> expected(8192,0x63),actual=expected;
        Model(b,source.data()+64+align,expected,64,row,col,false);
        const u32 emitted=EmuCoreXOracleCompileVifPairCopy(index,&b,code,capacity,true);
        reinterpret_cast<nVifrecCall>(code)(reinterpret_cast<uptr>(actual.data()+64),reinterpret_cast<uptr>(source.data()+64+align));
        if((actual!=expected||Words(&vif.MaskRow)!=row)&&data)
            std::printf("VIFNARROW mismatch idx=%u fmt=%x usn=%u mask=%u mode=%u num=%u cl=%u wl=%u align=%u bytes=%u\n",
                index,format,usn,mask,mode,num,b.cl,b.wl,align,emitted);
        data&=actual==expected;state&=Words(&vif.MaskRow)==row&&Words(&vif.MaskCol)==col;++cases;
    }
    Check(data,"signed unsigned V2/V3/V4-16/8 kernels match independent expansion masking modes fill skip and NUM0 canaries");
    Check(state,"actual narrow kernels preserve exact independent row column updates for every mode");
    Check(source==original,"all actual narrow kernels preserve unaligned packet bytes and source canaries");

    VifUnpackSSE_Init();bool wrapping=true;
    for(u32 index=0;index<2;++index)for(u32 format:{13u,14u})
    for(bool usn:{false,true})for(u32 mode:{0u,2u})for(u32 tail:{16u,32u})
    {
        resetNewVif(index);const u32 size=index?0x4000:0x1000;
        nVifBlock b{};b.upkType=format|(usn?32:0)|16;b.num=9;b.mode=mode;b.cl=3;b.wl=2;b.mask=0xe4936c1b;
        Setup(index,b,size-tail);auto& vif=index?vif1:vif0;auto row=Words(&vif.MaskRow);const auto col=Words(&vif.MaskCol);
        std::vector<u8> expected(size);for(u32 i=0;i<size;++i)expected[i]=static_cast<u8>(i*17+0x41);
        auto& vu=index?VU1:VU0;std::memcpy(vu.Mem,expected.data(),size);
        Model(b,source.data()+64,expected,size-tail,row,col,true);
        if(index)dVifUnpack<1>(source.data()+64,false);else dVifUnpack<0>(source.data()+64,false);
        wrapping&=std::memcmp(vu.Mem,expected.data(),size)==0&&Words(&vif.MaskRow)==row&&Words(&vif.MaskCol)==col;
    }
    Check(wrapping,"production narrow VIF dispatch preserves wrapped VU0/VU1 destination memory row and column state");
    std::printf("VIFNARROW cases=%llu shapesS=%u shapesD=%u\n",static_cast<unsigned long long>(cases),shapes[0],shapes[1]);
}
