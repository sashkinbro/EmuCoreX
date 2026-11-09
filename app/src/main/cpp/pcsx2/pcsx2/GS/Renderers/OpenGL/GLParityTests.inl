// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Included inside TestFramebufferFetch after the real context is initialized.
{
    // CheckFeatures must resolve real advertised aliases before index-0 fallback.
    const auto oldMask=glad_glColorMaski;const auto oldOES=glad_glColorMaskiOES;const auto oldEXT=glad_glColorMaskiEXT;
    const auto oldRange=glad_glDrawRangeElementsBaseVertex;
    const auto oldRangeOES=glad_glDrawRangeElementsBaseVertexOES;const auto oldRangeEXT=glad_glDrawRangeElementsBaseVertexEXT;
    const int oldOESFlag=GLAD_GL_OES_draw_buffers_indexed,oldEXTFlag=GLAD_GL_EXT_draw_buffers_indexed;
    const int oldRangeOESFlag=GLAD_GL_OES_draw_elements_base_vertex,oldRangeEXTFlag=GLAD_GL_EXT_draw_elements_base_vertex;
    const ScopedGuard restore([&](){glad_glColorMaski=oldMask;glad_glColorMaskiOES=oldOES;glad_glColorMaskiEXT=oldEXT;
        glad_glDrawRangeElementsBaseVertex=oldRange;glad_glDrawRangeElementsBaseVertexOES=oldRangeOES;glad_glDrawRangeElementsBaseVertexEXT=oldRangeEXT;
        GLAD_GL_OES_draw_buffers_indexed=oldOESFlag;GLAD_GL_EXT_draw_buffers_indexed=oldEXTFlag;
        GLAD_GL_OES_draw_elements_base_vertex=oldRangeOESFlag;GLAD_GL_EXT_draw_elements_base_vertex=oldRangeEXTFlag;
        vendor=renderer=version=nullptr;glad_glGetString=realGetString;device.CheckFeatures();});
    bool aliases=true,ranges=true;
    for(bool core:{false,true})for(bool oes:{false,true})for(bool ext:{false,true})
    {
        // These non-forwarded sentinels are never submitted; CheckFeatures only
        // selects pointers. Distinct symbols expose incorrect preference order.
        const auto oesMask=reinterpret_cast<PFNGLCOLORMASKIOESPROC>(GetString);
        const auto extMask=reinterpret_cast<PFNGLCOLORMASKIEXTPROC>(DrawPlain);
        glad_glColorMaski=core?ReplaceGL::ColorMaski:nullptr;glad_glColorMaskiOES=oesMask;glad_glColorMaskiEXT=extMask;
        GLAD_GL_OES_draw_buffers_indexed=oes;GLAD_GL_EXT_draw_buffers_indexed=ext;
        glad_glDrawRangeElementsBaseVertex=core?DrawRange:nullptr;
        glad_glDrawRangeElementsBaseVertexOES=reinterpret_cast<PFNGLDRAWRANGEELEMENTSBASEVERTEXOESPROC>(DrawPlain);
        glad_glDrawRangeElementsBaseVertexEXT=reinterpret_cast<PFNGLDRAWRANGEELEMENTSBASEVERTEXEXTPROC>(GetString);
        GLAD_GL_OES_draw_elements_base_vertex=oes;GLAD_GL_EXT_draw_elements_base_vertex=ext;
        device.CheckFeatures();
        const auto expectedMask=core?ReplaceGL::ColorMaski:oes?reinterpret_cast<PFNGLCOLORMASKIPROC>(oesMask):
            ext?reinterpret_cast<PFNGLCOLORMASKIPROC>(extMask):ReplaceGL::ColorMaski;
        const auto expectedRange=core?DrawRange:oes?reinterpret_cast<PFNGLDRAWRANGEELEMENTSBASEVERTEXPROC>(DrawPlain):
            ext?reinterpret_cast<PFNGLDRAWRANGEELEMENTSBASEVERTEXPROC>(GetString):nullptr;
        aliases&=glad_glColorMaski==expectedMask;ranges&=glad_glDrawRangeElementsBaseVertex==expectedRange;
    }
    check(aliases,"GL actual feature initialization prefers core OES EXT indexed mask before plain fallback");
    check(ranges,"GL actual feature initialization resolves core OES EXT ranged draw with absent-alias fallback");
    struct CompilerIdentity{const char* vendor;const char* renderer;bool rewrite;};
    constexpr CompilerIdentity identities[]={
        {"Qualcomm","Adreno (TM) 740",true},{"Qualcomm","Adreno (TM) 840",true},
        {"Google Inc. (Qualcomm)","ANGLE (Qualcomm, Adreno (TM) 740, OpenGL ES 3.2)",false},
        {"Mesa","freedreno Adreno 740",false},{"Qualcomm","Turnip Adreno 740",false},
        {"ARM","Mali-G57",false},{"Imagination Technologies","PowerVR Rogue",false},
        {"NVIDIA Corporation","GeForce",false},{"Intel","Intel UHD",false}};
    bool rules=true;
    for(const auto& id:identities)
    {
        vendor=id.vendor;renderer=id.renderer;glad_glGetString=GetString;device.CheckFeatures();
        const std::string header=device.GenGlslHeader("main",GL_FRAGMENT_SHADER,"");
        rules&=header.find(id.rewrite?"#define DRIVER_REWRITE_BOOLEAN_NEGATION 1\n":"#define DRIVER_REWRITE_BOOLEAN_NEGATION 0\n")!=std::string::npos;
    }
    struct FetchIdentity{const char* vendor;const char* renderer;const char* version;bool restricted;};
    constexpr FetchIdentity fetchIdentities[]={
        {"Qualcomm","Adreno (TM) 840","OpenGL ES 3.2 V@0842.52 (GIT@test)",true},
        {"Qualcomm","Adreno (TM) 740","OpenGL ES 3.2 V@0842.52 (GIT@test)",false},
        {"Qualcomm","Adreno (TM) 840","OpenGL ES 3.2 V@0842.53 (GIT@test)",false},
        {"Qualcomm","Adreno (TM) 840","OpenGL ES 3.2 V@0842.520 (GIT@test)",false},
        {"Google Inc. (Qualcomm)","ANGLE (Qualcomm, Adreno (TM) 840)","OpenGL ES 3.2 V@0842.52 (GIT@test)",false},
        {"Mesa","freedreno Adreno 840","OpenGL ES 3.2 Mesa 26.3",false}};
    bool restricted=true;
    for(const auto& id:fetchIdentities)
    {
        vendor=id.vendor;renderer=id.renderer;version=id.version;glad_glGetString=GetString;device.CheckFeatures();
        device.m_features.depth_feedback=DepthFeedbackSupport::DepthAsRT;
        device.m_fetch_caps.preferred=GSFramebufferFetchBackend::EXT;device.m_fetch_caps.ext=true;
        PSSelector ps{};ps.tfx=4;ps.no_color1=true;ps.color_feedback=true;ps.depth_feedback=true;
        const auto mrt=device.SelectFetchPlan(ps,true,true,true);
        const auto norm16=device.SelectFetchPlan(ps,true,false,false);
        const auto rgba8=device.SelectFetchPlan(ps,true,false,true);
        restricted&=device.m_fetch_caps.ext_single_rgba8==id.restricted&&
            (mrt.Color()==GSFramebufferFetchBackend::None)==id.restricted&&
            (norm16.Color()==GSFramebufferFetchBackend::None)==id.restricted&&rgba8.Color()==GSFramebufferFetchBackend::EXT;
        if(id.restricted)restricted&=mrt.Depth()==GLDepthFetch::Sampler;
    }
    version=nullptr;
    check(restricted,"GL physical EXT MRT and wide-format fallback is limited to tested native Adreno840 V0842.52 while RGBA8 stays enabled");
    check(rules,"GL boolean compiler rewrite follows Qualcomm proprietary identity and excludes ANGLE Mesa and other vendors");
}
{
    const int oldCore=GLAD_GL_VERSION_4_2,oldARB=GLAD_GL_ARB_conservative_depth,oldAMD=GLAD_GL_AMD_conservative_depth;
    const bool oldES=device.m_is_gles;
    const ScopedGuard restore([&](){GLAD_GL_VERSION_4_2=oldCore;GLAD_GL_ARB_conservative_depth=oldARB;
        GLAD_GL_AMD_conservative_depth=oldAMD;device.m_is_gles=oldES;});
    bool headers=true;
    for(bool es:{false,true})for(u32 provider=0;provider<4;provider++)
    {
        device.m_is_gles=es;GLAD_GL_VERSION_4_2=provider==1;GLAD_GL_ARB_conservative_depth=provider==2;
        GLAD_GL_AMD_conservative_depth=provider==3;
        const auto header=device.GenGlslHeader("main",GL_FRAGMENT_SHADER,"");
        headers&=header.find(provider?"#define PS_HAS_CONSERVATIVE_DEPTH 1\n":"#define PS_HAS_CONSERVATIVE_DEPTH 0\n")!=std::string::npos;
        const auto ext=header.find(provider==3?"#extension GL_AMD_conservative_depth":"#extension GL_ARB_conservative_depth");
        if(provider==2||provider==3)
        {
            const auto helper=header.find("uvec2 gpu_bitwise_and");const auto precision=header.find("precision highp");
            headers&=ext!=std::string::npos&&ext<helper&&(precision==std::string::npos||ext<precision);
        }
        else headers&=ext==std::string::npos;
    }
    check(headers,"GL conservative depth core ARB AMD headers enable correct capability before declarations and helpers");
}
