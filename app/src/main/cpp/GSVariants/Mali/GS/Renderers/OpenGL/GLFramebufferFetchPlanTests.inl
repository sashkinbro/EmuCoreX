// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Additional native proofs; not included until the intermediate source freeze ends.
namespace GLFetchPlanOracle
{
    inline u32 draws = 0, copies = 0;
    inline PFNGLDRAWELEMENTSBASEVERTEXPROC draw = nullptr;
    inline PFNGLCOPYIMAGESUBDATAPROC copy = nullptr, copyEXT = nullptr, copyOES = nullptr;
    void GLAPIENTRY Draw(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint base)
    { ++draws; draw(mode, count, type, indices, base); }
    void GLAPIENTRY Copy(GLuint s, GLenum st, GLint sl, GLint sx, GLint sy, GLint sz,
        GLuint d, GLenum dt, GLint dl, GLint dx, GLint dy, GLint dz, GLsizei w, GLsizei h, GLsizei z)
    { ++copies; copy(s, st, sl, sx, sy, sz, d, dt, dl, dx, dy, dz, w, h, z); }
    void GLAPIENTRY CopyEXT(GLuint s, GLenum st, GLint sl, GLint sx, GLint sy, GLint sz,
        GLuint d, GLenum dt, GLint dl, GLint dx, GLint dy, GLint dz, GLsizei w, GLsizei h, GLsizei z)
    { ++copies; copyEXT(s, st, sl, sx, sy, sz, d, dt, dl, dx, dy, dz, w, h, z); }
    void GLAPIENTRY CopyOES(GLuint s, GLenum st, GLint sl, GLint sx, GLint sy, GLint sz,
        GLuint d, GLenum dt, GLint dl, GLint dx, GLint dy, GLint dz, GLsizei w, GLsizei h, GLsizei z)
    { ++copies; copyOES(s, st, sl, sx, sy, sz, d, dt, dl, dx, dy, dz, w, h, z); }
}

void GSDeviceOGL::TestFramebufferFetchPlanProof(void (*check)(bool, const char*))
{
    const auto originalCaps = m_fetch_caps;
    const auto originalFeatures = m_features;
    const ScopedGuard restoreFacts([&]() { m_fetch_caps = originalCaps; m_features = originalFeatures; });
    PSSelector color{}, both{}, depth{};
    color.tfx = both.tfx = depth.tfx = 4;
    color.no_color1 = both.no_color1 = depth.no_color1 = true;
    color.color_feedback = both.color_feedback = true;
    both.depth_feedback = depth.depth_feedback = true;
    m_features.depth_feedback = DepthFeedbackSupport::DepthAsRT;
    m_fetch_caps = {GSFramebufferFetchBackend::ARM, true, true, false, false};
    const auto single = SelectFetchPlan(color, true, false);
    const auto unsafeMRT = SelectFetchPlan(both, true, true);
    const auto hdr = SelectFetchPlan(color, false, false);
    bool guards = single.Color() == GSFramebufferFetchBackend::ARM &&
        unsafeMRT.Color() == GSFramebufferFetchBackend::None && unsafeMRT.Depth() == GLDepthFetch::Sampler &&
        hdr.Color() == GSFramebufferFetchBackend::None;
    m_fetch_caps.arm_mrt = true;
    const auto safeMRT = SelectFetchPlan(both, true, true);
    guards &= safeMRT.Color() == GSFramebufferFetchBackend::ARM && safeMRT.Depth() == GLDepthFetch::Sampler;
    m_features.depth_feedback = DepthFeedbackSupport::Depth;
    guards &= SelectFetchPlan(depth, false, false).Depth() == GLDepthFetch::ARM;
    m_fetch_caps.arm_depth = false;
    guards &= SelectFetchPlan(depth, false, false).Depth() == GLDepthFetch::Sampler;
    check(guards, "GL actual per-draw ARM plan guards MRT query HDR/no-colour0 and independent depth capability");

    m_features.depth_feedback = DepthFeedbackSupport::DepthAsRT;
    m_fetch_caps = {GSFramebufferFetchBackend::EXT, true, true, false, true};
    const auto adrenoBoth = SelectFetchPlan(both, true, true);
    const auto adrenoDepth = SelectFetchPlan(depth, false, false);
    bool outputs = adrenoBoth.Color() == GSFramebufferFetchBackend::EXT && adrenoBoth.Depth() == GLDepthFetch::Sampler &&
        adrenoDepth.Color() == GSFramebufferFetchBackend::None && adrenoDepth.Depth() == GLDepthFetch::EXT;
    m_fetch_caps.adreno = false;
    outputs &= SelectFetchPlan(both, true, true).Depth() == GLDepthFetch::EXT;
    check(outputs, "GL Adreno permits one fetch output and generic EXT retains legal MRT fetch");

    ProgramSelector a{}, b{}, padded{};
    a.ps = b.ps = color;
    a.fetch = single; b.fetch = hdr;
    padded = a;
    std::memset(padded.pad, 0xa5, sizeof(padded.pad));
    std::unordered_set<ProgramSelector, ProgramSelectorHash> keys;
    keys.insert(a); keys.insert(b); keys.insert(padded);
    bool cache = keys.size() == 2 && a != b && a == padded &&
        ProgramSelectorHash{}(a) == ProgramSelectorHash{}(padded);
    m_fetch_caps = {};
    cache &= !SupportsFetchPlan(single) && !SupportsFetchPlan({0xff}) && SupportsFetchPlan({0});
    m_fetch_caps = {GSFramebufferFetchBackend::EXT, true, true, false, true};
    // Colour EXT=2; depth EXT=1 occupies bits2..3: two output fetches is6.
    // Key10 is EXT colour plus ARM depth builtin, which has only ONE output.
    cache &= !SupportsFetchPlan({6}) && SupportsFetchPlan({10});
    check(cache, "GL program cache keys include per-draw fetch and reject disabled or malformed learned backends");

    m_fetch_caps = originalCaps;
    m_features = originalFeatures;
    m_features.depth_feedback = DepthFeedbackSupport::DepthAsRT;
    // Actual native SendHWDraw/FeedbackCopyAndBind, using a tiny indexed probe
    // program and real texture objects. No fake capability is submitted.
    check(!g_gs_device && glDrawElementsBaseVertex, "GL copy proof has an isolated real indexed draw entry");
    if (g_gs_device || !glDrawElementsBaseVertex) return;
    g_gs_device = std::make_unique<GSDeviceOGL>();
    auto& target = *static_cast<GSDeviceOGL*>(g_gs_device.get());
    const ScopedGuard destroyDevice([&]() { g_gs_device.reset(); });
    target.m_is_gles = true;
    target.m_features = m_features;
    target.m_features.texture_barrier = false; // require a real copy, never pretend a native barrier exists
    target.m_fetch_caps = originalCaps;
    target.m_shader_tfx_fs = m_shader_tfx_fs;
    target.m_index.start = target.m_vertex.start = 0;
    target.m_index.count = 9;
    target.m_draw_topology = GL_TRIANGLES;
    GLuint vao = 0, index = 0, fbo = 0;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &index); glGenFramebuffers(1, &fbo);
    const ScopedGuard deleteBuffers([&]() {
        glUseProgram(0); glBindVertexArray(0); glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo); glDeleteBuffers(1, &index); glDeleteVertexArrays(1, &vao);
    });
    glBindVertexArray(vao); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index);
    constexpr u16 indices[] = {0,1,2,3,4,5,6,7,8};
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    const auto vertexText = target.GenGlslHeader("main", GL_VERTEX_SHADER, "") + std::string(R"(
void main(){int i=gl_VertexID%3;gl_Position=vec4(i==0?vec2(-1.,-1.):i==1?vec2(3.,-1.):vec2(-1.,3.),0.,1.);}
)");
    const GLuint vertex = GLFetchOracle::Compile(GL_VERTEX_SHADER, vertexText);
    const ScopedGuard deleteVertex([&]() { if (vertex) glDeleteShader(vertex); });
    GSTextureOGL rt(GSTexture::RenderTarget, 4, 4, 1, GSTexture::Format::Color);
    GSTextureOGL rtCopy(GSTexture::Usage::Texture, 4, 4, 1, GSTexture::Format::Color);
    // Logical RT can differ from the actual attachment for ColorClip/HDR.
    GSTextureOGL logicalRT(GSTexture::RenderTarget, 4, 4, 1, GSTexture::Format::Color);
    GSTextureOGL ds(GSTexture::RenderTarget, 4, 4, 1, GSTexture::Format::DepthColor);
    GSTextureOGL dsCopy(GSTexture::Usage::Texture, 4, 4, 1, GSTexture::Format::DepthColor);
    rt.SetState(GSTexture::State::Dirty); rtCopy.SetState(GSTexture::State::Dirty);
    ds.SetState(GSTexture::State::Dirty); dsCopy.SetState(GSTexture::State::Dirty);
    GLFetchPlanOracle::draw = glad_glDrawElementsBaseVertex;
    GLFetchPlanOracle::copy = glad_glCopyImageSubData;
    GLFetchPlanOracle::copyEXT = reinterpret_cast<PFNGLCOPYIMAGESUBDATAPROC>(glad_glCopyImageSubDataEXT);
    GLFetchPlanOracle::copyOES = reinterpret_cast<PFNGLCOPYIMAGESUBDATAPROC>(glad_glCopyImageSubDataOES);
    glad_glDrawElementsBaseVertex = GLFetchPlanOracle::Draw;
    if (GLFetchPlanOracle::copy) glad_glCopyImageSubData = GLFetchPlanOracle::Copy;
    if (GLFetchPlanOracle::copyEXT) glad_glCopyImageSubDataEXT = GLFetchPlanOracle::CopyEXT;
    if (GLFetchPlanOracle::copyOES) glad_glCopyImageSubDataOES = GLFetchPlanOracle::CopyOES;
    const ScopedGuard restoreCommands([&]() {
        glad_glDrawElementsBaseVertex = GLFetchPlanOracle::draw;
        glad_glCopyImageSubData = GLFetchPlanOracle::copy;
        glad_glCopyImageSubDataEXT = reinterpret_cast<PFNGLCOPYIMAGESUBDATAEXTPROC>(GLFetchPlanOracle::copyEXT);
        glad_glCopyImageSubDataOES = reinterpret_cast<PFNGLCOPYIMAGESUBDATAOESPROC>(GLFetchPlanOracle::copyOES);
    });
    bool pixels = true, commands = true, links = vertex != 0, aliases = true;
    u32 executed = 0;
    for (bool enabled : {false, true})
    for (u32 variant = 0; variant < 5; ++variant)
    {
        const bool withDepth = variant == 1 || variant == 3;
        const bool alias = variant >= 2;
        target.m_fetch_caps = enabled ? originalCaps : GLFramebufferFetchCaps{};
        PSSelector selector = withDepth ? both : color;
        if (alias)
        {
            selector.color_feedback = selector.depth_feedback = false;
            selector.tfx = 0; // actual TextureSampler declaration, without shader feedback flags
        }
        const auto plan = target.SelectFetchPlan(selector, true, withDepth);
        const char* main = variant == 3 ?
            "\nlayout(location=1) out highp float alias_depth;void main(){o_col0=vec4(8./255.,16./255.,32./255.,64./255.);alias_depth=texelFetch(TextureSampler,ivec2(gl_FragCoord.xy),0).r+0.125;}\n" : alias ?
            "\nvoid main(){o_col0=texelFetch(TextureSampler,ivec2(gl_FragCoord.xy),0)+vec4(1./255.,0.,0.,0.);}\n" : withDepth ?
            "\nvoid main(){o_col0=sample_from_rt()+vec4(1./255.,0.,0.,0.);o_col1=sample_from_depth()+0.125;}\n" :
            "\nvoid main(){o_col0=sample_from_rt()+vec4(1./255.,0.,0.,0.);}\n";
        const GLuint fragment = GLFetchOracle::Compile(GL_FRAGMENT_SHADER,
            GLFetchOracle::ProbeMain(target.GetPSSource(selector, plan), main));
        const GLuint program = GLFetchOracle::Link(vertex, fragment);
        links &= program != 0;
        if (fragment) glDeleteShader(fragment);
        if (!program) continue;
        const ScopedGuard deleteProgram([&]() { glDeleteProgram(program); });
        std::array<u8, 64> seed{};
        for (u32 i=0;i<seed.size();i+=4) {seed[i]=8;seed[i+1]=16;seed[i+2]=32;seed[i+3]=64;}
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, rt.GetID());
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,seed.data());
        std::array<float,16> z; z.fill(0.25f);
        glBindTexture(GL_TEXTURE_2D, ds.GetID());
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,GL_RED,GL_FLOAT,z.data());
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,rt.GetID(),0);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,withDepth?ds.GetID():0,0);
        constexpr GLenum attachments[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};
        glDrawBuffers(withDepth?2:1,attachments);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {pixels=false;continue;}
        glUseProgram(program); glBindVertexArray(vao); glViewport(0,0,4,4);
        glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_DITHER); glDisable(GL_SCISSOR_TEST);
        GSHWDrawConfig config{};
        config.ps=selector;config.rt=&rt;config.ds_as_rt=withDepth?&ds:nullptr;
        if(alias)config.tex=withDepth?&ds:&rt; // intentionally no TEX_HAZARD bit
        if(variant==4)config.rt=config.tex=&logicalRT;
        config.nindices=9;config.indices_per_prim=3;config.drawarea=config.samplearea=GSVector4i(0,0,4,4);
        GLFetchPlanOracle::draws=GLFetchPlanOracle::copies=0;
        GSTexture* colorCopy=alias?(withDepth?nullptr:&rtCopy):(plan.Color()==GSFramebufferFetchBackend::None?&rtCopy:nullptr);
        GSTexture* depthCopy=withDepth&&plan.Depth()==GLDepthFetch::Sampler?&dsCopy:nullptr;
        if(alias)
        {
            // Model the real adapter's initial clone binding. The submitted
            // shader samples a VALID snapshot, never an undefined attachment
            // feedback loop, even while the no-hazard predicate is still RED.
            GLState::tex_unit[0]=0;
            target.FeedbackCopyAndBind(config,&rt,colorCopy,withDepth?&ds:nullptr,depthCopy,config.drawarea);
            // DoRenderHW initially binds the clone using the logical RT alias.
            // Keep that valid baseline binding even before the refresh fix.
            if(variant==4)target.PSSetShaderResource(0,colorCopy);
            GLFetchPlanOracle::draws=GLFetchPlanOracle::copies=0;
        }
        config.require_full_barrier=true;
        GSApplyPlannedFeedbackBarriers(config,target.m_features,RenderAPI::OpenGL,true,false,false);
        target.SendHWDraw(config,colorCopy,&rt,depthCopy,withDepth?&ds:nullptr,config.require_one_barrier,config.require_full_barrier,selector,plan);
        glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,seed.data());
        bool exact=true;
        for(u32 i=0;i<seed.size();i+=4)exact&=seed[i]==(variant==3?8:11)&&seed[i+1]==16&&seed[i+2]==32&&seed[i+3]==64;
        if(withDepth)
        {
            std::array<float,64> result{};
            glReadBuffer(GL_COLOR_ATTACHMENT1);glReadPixels(0,0,4,4,GL_RGBA,GL_FLOAT,result.data());
            for(u32 i=0;i<result.size();i+=4)exact&=result[i]==0.625f;
        }
        const u32 perPrimitive=static_cast<u32>(colorCopy!=nullptr)+static_cast<u32>(depthCopy!=nullptr);
        const bool command=GLFetchPlanOracle::draws==(perPrimitive?3u:1u)&&GLFetchPlanOracle::copies==perPrimitive*3;
        exact&=glGetError()==GL_NO_ERROR;
        if(alias)aliases&=command&&exact;
        else{commands&=command;pixels&=exact;}
        if(!command||!exact)std::fprintf(stderr,"GL_FETCH_COPY_DIFF enabled=%u variant=%u plan=%u draws=%u copies=%u expectedCopies=%u command=%u pixels=%u\n",
            enabled,variant,plan.key,GLFetchPlanOracle::draws,GLFetchPlanOracle::copies,perPrimitive*3,command,exact);
        ++executed;
    }
    check(links&&executed==10,"GL actual selected single/MRT and disabled TFX sample programs compile and link");
    check(commands,"GL actual SendHWDraw skips ARM copies and orders sampler fallback copies per primitive");
    check(pixels&&executed==10,"GL real colour and R32 MRT copy pixels match across enabled and disabled fetch");
    check(aliases&&executed==10,"GL actual non-null logical/transformed RT and depth-as-RT aliases retain per-primitive copies without hazard hints");
    std::printf("GL_FETCH_PLAN physicalMRT=%u physicalDepthARM=%u pixelCases=%u\n",
        originalCaps.arm_mrt,originalCaps.arm_depth,executed);
}
