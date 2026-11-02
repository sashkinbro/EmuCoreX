// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Native CPU-only activation proof for the existing fallback. Device capability
// comes from the actual driver finalizer; no backend submission occurs here.
void GSRendererHW::TestProvokingFallback(void (*check)(bool,const char*))
{
    if(!g_gs_device||g_gs_renderer||g_texture_cache)
    {check(false,"GS provoking fallback owns an isolated CPU renderer with finalized device facts");return;}
    const auto settings=GSConfig;GSConfig=Pcsx2Config::GSOptions();
    g_gs_renderer=std::make_unique<GSRendererHW>();auto& r=*static_cast<GSRendererHW*>(g_gs_renderer.get());
    ScopedGuard restore=[&](){g_gs_renderer.reset();GSConfig=settings;};
    const bool last=g_gs_device->Features().provoking_vertex_last;
    bool lines=true,triangles=true,smooth=true;
    for(GS_PRIM_CLASS primitive:{GS_LINE_CLASS,GS_TRIANGLE_CLASS})for(bool iip:{false,true})
    {
        r.ResetStates();r.PRIM=&r.m_env.PRIM;r.m_env.PRIM.AA1=false;r.m_conf.vs.iip=iip;r.m_vt.m_primclass=primitive;
        alignas(32) GSVertex original[7]{};
        for(u32 vertex=0;vertex<7;++vertex)
        {
            u32 words[8];for(u32 lane=0;lane<8;++lane)words[lane]=0x3f000000u+vertex*0x10101u+lane*0x113355u;
            std::memcpy(&original[vertex],words,sizeof(words));original[vertex].RGBAQ.U32[0]=0x80000000u+vertex*0x113355u;
            r.m_vertex.buff[vertex]=original[vertex];
        }
        const u16 lineIndices[6]={4,5,0,1,2,3},triangleIndices[6]={4,0,6,2,4,1};
        const u16* source=primitive==GS_LINE_CLASS?lineIndices:triangleIndices;
        std::memcpy(r.m_index.buff,source,12);r.m_index.tail=6;r.m_vertex.head=0;r.m_vertex.next=r.m_vertex.tail=7;
        r.HandleFlatShadedVertices();const bool fixed=!last&&!iip;
        bool exact=r.m_index.tail==6;
        if(fixed)
        {
            const u32 count=primitive==GS_LINE_CLASS?2:3;
            exact&=r.m_vertex.head==6&&r.m_vertex.next==6&&r.m_vertex.tail==6;
            for(u32 at=0;at<6;++at)
            {
                auto expected=original[source[at]];expected.RGBAQ.U32[0]=original[source[(at/count)*count+count-1]].RGBAQ.U32[0];
                exact&=r.m_index.buff[at]==at&&std::memcmp(&r.m_vertex.buff[at],&expected,sizeof(expected))==0;
            }
        }
        else
        {
            exact&=r.m_vertex.head==0&&r.m_vertex.next==7&&r.m_vertex.tail==7&&std::memcmp(r.m_index.buff,source,12)==0;
            exact&=std::memcmp(r.m_vertex.buff,original,sizeof(original))==0;
        }
        if(iip)smooth&=exact;else if(primitive==GS_LINE_CLASS)lines&=exact;else triangles&=exact;
    }
    check(lines,"GS actual finalized LAST capability keeps native lines or deindexes pairs with the exact true last color and unchanged order");
    check(triangles,"GS actual finalized LAST capability keeps native triangles or deindexes shared vertices with exact last color and all other32byte payload intact");
    check(smooth,"GS existing smooth shading bypasses the flat-color fallback under both finalized LAST capability outcomes");
    std::printf("GS_DRIVER_FLAT_FALLBACK last=%u\n",unsigned(last));
}
extern "C" void EmuCoreXOracleProvokingFallback(void (*check)(bool,const char*))
{
    GSRendererHW::TestProvokingFallback(check);
}
