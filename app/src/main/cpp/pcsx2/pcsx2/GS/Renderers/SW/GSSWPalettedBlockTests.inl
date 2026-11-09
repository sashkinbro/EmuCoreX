// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "GS/GSBlock.h"
#include "cpuinfo.h"
extern "C" void EmuCoreXOracleSWPalettedBlockRead(void(*check)(bool,const char*)) {
    cpuinfo_initialize();auto mem=std::make_unique<GSLocalMemory>();
    alignas(32) u8 source[256];alignas(32) u32 palette[256];
    bool read8=true,read4=true,pad8=true,pad4=true,expand8=true,expand4=true,guard8=true,guard4=true;
    alignas(32) u8 output[4096];
    for(int pattern=0;pattern<3;pattern++) {
    for(int i=0;i<256;i++){source[i]=pattern==0?u8(i):pattern==1?u8(i*37+(i>>3)*19):u8(255-i);palette[i]=0x80000000u|u32(i*0x010203u);}
    std::memcpy(mem->m_vm8,source,sizeof(source));
    for(int pitch:{16,32,64}) {
        std::memset(output,0xa5,sizeof(output));GSBlock::ReadBlock8(source,output,pitch);
        for(int y=0;y<16;y++)for(int x=0;x<16;x++)read8&=output[y*pitch+x]==mem->ReadPixel8(x,y,0,1);
        for(int y=0;y<16;y++)for(int x=16;x<pitch;x++)pad8&=output[y*pitch+x]==0xa5;
        pad8&=std::all_of(output+pitch*16,output+sizeof(output),[](u8 v){return v==0xa5;});
        std::memset(output,0xa5,sizeof(output));GSBlock::ReadBlock4(source,output,pitch);
        for(int y=0;y<16;y++)for(int x=0;x<32;x++)read4&=((output[y*pitch+x/2]>>((x&1)*4))&15)==mem->ReadPixel4(x,y,0,1);
        for(int y=0;y<16;y++)for(int x=16;x<pitch;x++)pad4&=output[y*pitch+x]==0xa5;
        pad4&=std::all_of(output+pitch*16,output+sizeof(output),[](u8 v){return v==0xa5;});
    }
    for(int pitch:{128,160,192}) {
        std::memset(output,0xa5,sizeof(output));GSBlock::ReadAndExpandBlock8_32(source,output,pitch,palette);
        for(int y=0;y<16;y++)for(int x=0;x<16;x++)expand8&=reinterpret_cast<const u32*>(output+y*pitch)[x]==palette[mem->ReadPixel8(x,y,0,1)];
        for(int y=0;y<16;y++)for(int x=64;x<pitch;x++)guard8&=output[y*pitch+x]==0xa5;
        guard8&=std::all_of(output+pitch*16,output+sizeof(output),[](u8 v){return v==0xa5;});
        std::memset(output,0xa5,sizeof(output));GSBlock::ReadAndExpandBlock4_32(source,output,pitch,palette);
        for(int y=0;y<16;y++)for(int x=0;x<32;x++)expand4&=reinterpret_cast<const u32*>(output+y*pitch)[x]==palette[mem->ReadPixel4(x,y,0,1)];
        for(int y=0;y<16;y++)for(int x=128;x<pitch;x++)guard4&=output[y*pitch+x]==0xa5;
        guard4&=std::all_of(output+pitch*16,output+sizeof(output),[](u8 v){return v==0xa5;});
    }
    }
    check(read8,"GS actual8bit block reader matches literal scalar GS swizzled pixel addresses at three pitches");
    check(read4,"GS actual4bit block reader preserves both nibble lanes against scalar GS pixel addresses");
    check(pad8&&pad4,"GS actual compact8bit and4bit block readers preserve row padding and tail guards");
    check(expand8,"GS actual8bit paletted block expansion matches literal palette lookup for all256pixels");
    check(expand4,"GS actual4bit paletted block expansion matches literal palette lookup for all512pixels");
    check(guard8&&guard4,"GS actual paletted block expansion preserves all row and tail guards");
}
