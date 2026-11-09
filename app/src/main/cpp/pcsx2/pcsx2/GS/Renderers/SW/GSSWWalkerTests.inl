// Actual rasterizer + C++/generated scanline output, using GS address tables.
#include "Memory.h"
#include "common/ScopedGuard.h"
#include "cpuinfo.h"
void GSDrawScanline::TestWalkers(void(*check)(bool,const char*))
{
    cpuinfo_initialize();
    const bool existing_code=SysMemory::GetCodePtr(0)!=nullptr;
    if(!existing_code&&!SysMemory::Allocate()){check(false,"GS walker fixture allocates the actual JIT code arena");return;}
    ScopedGuard release_code=[&](){if(!existing_code)SysMemory::Release();};
    std::fprintf(stderr,"SW_WALK_BOOTSTRAP code=%p existing=%d\n",SysMemory::GetCodePtr(0),existing_code);
    auto mem=std::make_unique<GSLocalMemory>();
    std::fprintf(stderr,"SW_WALK_BOOTSTRAP local_memory_ready\n");
    GSDrawScanline ds;
    std::fprintf(stderr,"SW_WALK_BOOTSTRAP scanline_ready\n");
    GIFRegFRAME frame{};frame.FBW=1;frame.PSM=PSMCT32;
    GIFRegZBUF zbuf{};zbuf.PSM=PSMZ32;zbuf.ZMSK=1;
    const GSPixelOffset4* off=mem->GetPixelOffset4(frame,zbuf);
    alignas(32) u32 texture[32*8]{};
    for(int y=0;y<8;y++)for(int x=0;x<32;x++)texture[y*32+x]=0xff000000u|u32(x+1);
    GSVertexSW vertex[3]={GSVertexSW::zero(),GSVertexSW::zero(),GSVertexSW::zero()};
    const u16 index[3]={0,1,2};
    const int vx[3]={0,32,0},vy[3]={0,0,16};
    bool jit_c=true,colour=true,clipped=true,saturation=true,negative=true,reciprocal=true;
    bool jit_ready=true;
    auto draw=[&](bool generated,int kind,int clip) {
        std::memset(mem->m_vm8,0,GSLocalMemory::m_vmsize);
        GSRasterizerData data;
        data.setup_prim=nullptr;data.draw_scanline=nullptr;data.draw_edge=nullptr;
        data.primclass=GS_TRIANGLE_CLASS;data.vertex=vertex;data.vertex_count=3;
        data.index=const_cast<u16*>(index);data.index_count=3;
        data.scissor=GSVector4i(clip,0,32,16);data.bbox=GSVector4i(0,0,32,16);
        auto& g=data.global;g={};g.vm=mem->m_vm8;g.fzbr=off->row;g.fzbc=off->col;
        g.sel.key=0;g.sel.fpsm=0;g.sel.fwrite=1;g.sel.iip=1;
        g.sel.atst=ATST_ALWAYS;g.sel.tfx=TFX_NONE;g.sel.prim=GS_TRIANGLE_CLASS;
        g.sel.colclamp=1;
        g.fm=GSVector4i::zero();g.zm=GSVector4i::xffffffff();
        for(int i=0;i<3;i++) {
            vertex[i]=GSVertexSW::zero();vertex[i].p.x=float(vx[i]);vertex[i].p.y=float(vy[i]);
            vertex[i].c=GSVector4(float((10+(i==1?5:0))<<7),float(20<<7),float(30<<7),float(128<<7));
        }
        if(kind) {
            g.sel.iip=0;g.sel.tfx=TFX_DECAL;g.sel.tcc=1;g.sel.fst=(kind!=3);
            g.sel.tw=2;g.tex[0]=texture;
            g.t.min=GSVector4i(31,31,7,7).xxxxlh();g.t.max=GSVector4i::zero();
            g.t.mask=GSVector4i::xffffffff();g.t.invmask=GSVector4i::zero();
            for(auto& v:vertex)v.t=GSVector4(kind==1?3000.0f*65536.0f:kind==2?-0.03125f*65536.0f:6.0f*65536.0f-0.09375f,0.0f,kind==3?3.0f:1.0f,0.0f);
        }
        if(kind==0&&clip==0)std::fprintf(stderr,"SW_WALK_BOOTSTRAP draw generated=%d\n",generated);
        if(generated)jit_ready&=ds.SetupDraw(data);
        else {data.setup_prim=&CSetupPrim;data.draw_scanline=static_cast<DrawScanlinePtr>(&CDrawScanline);data.draw_edge=nullptr;}
        if(!data.setup_prim||!data.draw_scanline)return std::array<u32,32>{};
        GSRasterizer r(&ds,0,1);r.Draw(data);
        std::array<u32,32> out{};for(int x=0;x<32;x++)out[x]=mem->ReadPixel32(x,0,0,1);return out;
    };
    for(int kind=0;kind<4;kind++)for(int clip:{0,3,7}) {
        const auto c=draw(false,kind,clip),j=draw(true,kind,clip);jit_c&=c==j;
        for(int x=0;x<32;x++) {
            u32 expected=0;
            if(x>=clip) {
                if(kind==0)expected=0x801e1400u|u32((1280+40+(x-2)*16+32*(x/8))>>7);
                else if(kind==1)expected=0xff000020u; // saturate to2047, then repeat in32texels
                else if(kind==2)expected=0xff000001u; // negative half-sixteenth truncates towardzero
                else expected=0xff000003u; // coarse reciprocal of3samples2, exactdivide samples1
            }
            if(kind==0){colour&=c[x]==expected&&j[x]==expected;if(clip)clipped&=c[x]==expected&&j[x]==expected;}
            else if(kind==1)saturation&=c[x]==expected&&j[x]==expected;
            else if(kind==2)negative&=c[x]==expected&&j[x]==expected;
            else reciprocal&=c[x]==expected&&j[x]==expected;
        }
    }
    check(jit_ready,"GS actual software setup/draw generators compile all walker fixtures");
    check(colour,"GS actual C++ and Oaknut rasterizers apply eight-pixel hardware colour blocks");
    check(clipped,"GS actual C++ and Oaknut colour block phase is pinned to absolute X through clipping");
    check(saturation,"GS actual C++ and Oaknut sampler saturates signed12.4coordinate before repeat addressing");
    check(negative,"GS actual C++ and Oaknut sampler truncates negative sub-sixteenth coordinate toward zero");
    check(reciprocal,"GS actual C++ and Oaknut perspective sampler uses console reciprocal grid");
    check(jit_c,"GS actual C++ and Oaknut generated software pixels agree for every walker fixture");
}
extern "C" void EmuCoreXOracleSWWalkers(void(*check)(bool,const char*)) { GSDrawScanline::TestWalkers(check); }
