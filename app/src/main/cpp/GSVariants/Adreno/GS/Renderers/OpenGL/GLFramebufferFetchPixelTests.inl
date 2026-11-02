// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Native-only continuation: real depth, normalized16, float and alpha-pass pixels.
void GSDeviceOGL::TestFramebufferFetchPixelProof(void (*check)(bool, const char*))
{
    check(!g_gs_device, "GL precision proof starts with no other graphics device");
    if(g_gs_device)return;
    g_gs_device=std::make_unique<GSDeviceOGL>();
    auto& target=*static_cast<GSDeviceOGL*>(g_gs_device.get());
    const ScopedGuard destroy([&](){g_gs_device.reset();});
    target.m_is_gles=true;target.m_features=m_features;target.m_fetch_caps=m_fetch_caps;
    target.m_features.texture_barrier=false;
    target.m_shader_tfx_fs=m_shader_tfx_fs;
    const auto caps=m_fetch_caps;
    GLFramebufferFetchCaps noFetchCaps=caps;
    noFetchCaps.preferred=GSFramebufferFetchBackend::None;
    noFetchCaps.ext=noFetchCaps.arm_depth=noFetchCaps.arm_mrt=false;
    GLuint vao=0,index=0,fbo=0;
    glGenVertexArrays(1,&vao);glGenBuffers(1,&index);glGenFramebuffers(1,&fbo);
    const ScopedGuard buffers([&](){glUseProgram(0);glBindVertexArray(0);glBindFramebuffer(GL_FRAMEBUFFER,0);
        glDeleteFramebuffers(1,&fbo);glDeleteBuffers(1,&index);glDeleteVertexArrays(1,&vao);});
    glBindVertexArray(vao);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,index);
    constexpr u16 ids[]={0,1,2,3,4,5,6,7,8};glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(ids),ids,GL_STATIC_DRAW);
    target.m_index.start=target.m_vertex.start=0;target.m_index.count=9;target.m_draw_topology=GL_TRIANGLES;
    const GLuint vertex=GLFetchOracle::Compile(GL_VERTEX_SHADER,GLFetchOracle::FullscreenVertexSource(target));
    const ScopedGuard freeVertex([&](){if(vertex)glDeleteShader(vertex);});
    u32 programCase=0;
    const auto program=[&](const PSSelector& ps,GLFramebufferFetchPlan plan,const char* main){
        const u32 caseId=programCase++;
        const GLuint fragment=GLFetchOracle::Compile(GL_FRAGMENT_SHADER,GLFetchOracle::ProbeMain(target.GetPSSource(ps,plan),main));
        const GLuint p=GLFetchOracle::Link(vertex,fragment);
        if(!p)std::fprintf(stderr,"GL_FETCH_PIXEL_LINK failed case=%u tfx=%u iip=%u no_color1=%u color_feedback=%u depth_feedback=%u depth_mode=%u plan=%u fragment=%u\n",
            caseId,ps.tfx,unsigned(ps.iip),unsigned(ps.no_color1),unsigned(ps.color_feedback),unsigned(ps.depth_feedback),
            unsigned(target.m_features.depth_feedback),plan.key,unsigned(fragment!=0));
        if(fragment)glDeleteShader(fragment);return p;};
    GLFetchPlanOracle::draw=glad_glDrawElementsBaseVertex;
    GLFetchPlanOracle::copy=glad_glCopyImageSubData;
    GLFetchPlanOracle::copyEXT=reinterpret_cast<PFNGLCOPYIMAGESUBDATAPROC>(glad_glCopyImageSubDataEXT);
    GLFetchPlanOracle::copyOES=reinterpret_cast<PFNGLCOPYIMAGESUBDATAPROC>(glad_glCopyImageSubDataOES);
    glad_glDrawElementsBaseVertex=GLFetchPlanOracle::Draw;
    if(GLFetchPlanOracle::copy)glad_glCopyImageSubData=GLFetchPlanOracle::Copy;
    if(GLFetchPlanOracle::copyEXT)glad_glCopyImageSubDataEXT=GLFetchPlanOracle::CopyEXT;
    if(GLFetchPlanOracle::copyOES)glad_glCopyImageSubDataOES=GLFetchPlanOracle::CopyOES;
    const ScopedGuard restoreCommands([&](){glad_glDrawElementsBaseVertex=GLFetchPlanOracle::draw;
        glad_glCopyImageSubData=GLFetchPlanOracle::copy;
        glad_glCopyImageSubDataEXT=reinterpret_cast<PFNGLCOPYIMAGESUBDATAEXTPROC>(GLFetchPlanOracle::copyEXT);
        glad_glCopyImageSubDataOES=reinterpret_cast<PFNGLCOPYIMAGESUBDATAOESPROC>(GLFetchPlanOracle::copyOES);});
    GSTextureOGL rt(GSTexture::RenderTarget,4,4,1,GSTexture::Format::Color);
    GSTextureOGL rtCopy(GSTexture::Usage::Texture,4,4,1,GSTexture::Format::Color);
    GSTextureOGL ds(GSTexture::DepthStencil,4,4,1,GSTexture::Format::DepthStencil);
    GSTextureOGL dsCopy(GSTexture::Usage::Texture,4,4,1,GSTexture::Format::DepthStencil);
    PSSelector color{},depth{},plain{};
    color.tfx=depth.tfx=plain.tfx=4;color.no_color1=depth.no_color1=plain.no_color1=true;
    color.color_feedback=true;depth.depth_feedback=true;
    const auto attach=[&](GLuint image,GLuint z){glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,image,0);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,0,0);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,z,0);
        const GLenum output=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&output);glReadBuffer(output);
        glViewport(0,0,4,4);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_DITHER);
        glBindVertexArray(vao);return glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;};
    GSHWDrawConfig config{};config.rt=&rt;config.ds=&ds;
    config.nindices=9;config.indices_per_prim=3;config.drawarea=config.samplearea=GSVector4i(0,0,4,4);
    bool direct=true;u32 directCases=0;
    target.m_features.depth_feedback=DepthFeedbackSupport::Depth;
    for(bool enabled:{false,true})
    {
        target.m_fetch_caps=enabled?caps:noFetchCaps;
        const auto plan=target.SelectFetchPlan(depth,true,false);
        const GLuint p=program(depth,plan,"\nvoid main(){float z=sample_from_depth()+0.125;gl_FragDepth=z;o_col0=vec4(z,0.,0.,1.);}\n");
        if(!p){direct=false;continue;}const ScopedGuard deleteProgram([&](){glDeleteProgram(p);});
        direct&=attach(rt.GetID(),ds.GetID());
        glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_ALWAYS);
        glClearBufferfi(GL_DEPTH_STENCIL,0,0.25f,0);
        glUseProgram(p);config.ps=depth;GLState::tex_unit[4]=0;
        GLFetchPlanOracle::draws=GLFetchPlanOracle::copies=0;
        target.SendHWDraw(config,nullptr,&rt,plan.Depth()==GLDepthFetch::Sampler?&dsCopy:nullptr,&ds,
            false,false,depth,plan);
        std::array<u8,64> image{};glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
        bool exact=true;for(u32 i=0;i<image.size();i+=4)exact&=image[i]==159&&image[i+1]==0&&image[i+2]==0&&image[i+3]==255;
        const bool copy=plan.Depth()==GLDepthFetch::Sampler;
        const GLenum depthError=glGetError();
        direct&=exact&&GLFetchPlanOracle::draws==(copy?3u:1u)&&GLFetchPlanOracle::copies==(copy?3u:0u)&&depthError==GL_NO_ERROR;
        if(!exact||depthError)std::fprintf(stderr,"GL_FETCH_DEPTH_DIFF enabled=%u plan=%u rgba=%u/%u/%u/%u copies=%u draws=%u error=%x\n",
            enabled,plan.key,image[0],image[1],image[2],image[3],GLFetchPlanOracle::copies,GLFetchPlanOracle::draws,depthError);
        ++directCases;
    }
    check(direct&&directCases==2,"GL real direct-depth ARM and disabled-copy paths preserve depth evolution and command counts");
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);target.m_features.depth_feedback=DepthFeedbackSupport::DepthAsRT;

    // Masking a real capability DOWN is safe: this must select copies, not
    // submit ARM MRT fetch on a driver which does not promise that feature.
    {
        GSTextureOGL depthRT(GSTexture::RenderTarget,4,4,1,GSTexture::Format::DepthColor);
        GSTextureOGL depthRTCopy(GSTexture::Usage::Texture,4,4,1,GSTexture::Format::DepthColor);
        target.m_fetch_caps=caps;target.m_fetch_caps.arm_mrt=false;
        PSSelector both=color;both.depth_feedback=true;
        const auto plan=target.SelectFetchPlan(both,true,true);
        bool exact=caps.preferred!=GSFramebufferFetchBackend::ARM||plan.Color()==GSFramebufferFetchBackend::None;
        const GLuint p=program(both,plan,"\nvoid main(){o_col0=sample_from_rt()+vec4(1./255.,0.,0.,0.);o_col1=sample_from_depth()+0.125;}\n");
        const ScopedGuard freeProgram([&](){if(p)glDeleteProgram(p);});
        exact&=p!=0;
        if(p)
        {
            std::array<u8,64> seed{};for(u32 i=0;i<seed.size();i+=4){seed[i]=8;seed[i+1]=16;seed[i+2]=32;seed[i+3]=64;}
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,rt.GetID());
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,seed.data());
            std::array<float,16> z;z.fill(0.25f);glBindTexture(GL_TEXTURE_2D,depthRT.GetID());
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,GL_RED,GL_FLOAT,z.data());
            attach(rt.GetID(),0);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,depthRT.GetID(),0);
            constexpr GLenum outputs[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};glDrawBuffers(2,outputs);
            exact&=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
            glUseProgram(p);config.ps=both;config.rt=&rt;config.ds=nullptr;config.ds_as_rt=&depthRT;
            GLState::tex_unit[2]=GLState::tex_unit[4]=0;GLFetchPlanOracle::copies=GLFetchPlanOracle::draws=0;
            const bool copyRT=plan.Color()==GSFramebufferFetchBackend::None;
            const bool copyZ=plan.Depth()==GLDepthFetch::Sampler;
            target.SendHWDraw(config,copyRT?&rtCopy:nullptr,&rt,copyZ?&depthRTCopy:nullptr,&depthRT,false,false,both,plan);
            glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,seed.data());
            for(u32 i=0;i<seed.size();i+=4)exact&=seed[i]==11&&seed[i+1]==16&&seed[i+2]==32&&seed[i+3]==64;
            std::array<float,64> image{};glReadBuffer(GL_COLOR_ATTACHMENT1);glReadPixels(0,0,4,4,GL_RGBA,GL_FLOAT,image.data());
            for(u32 i=0;i<image.size();i+=4)exact&=image[i]==0.625f;
            const u32 copies=static_cast<u32>(copyRT)+static_cast<u32>(copyZ);
            exact&=GLFetchPlanOracle::copies==copies*3&&GLFetchPlanOracle::draws==(copies?3u:1u)&&glGetError()==GL_NO_ERROR;
        }
        check(exact,"GL MRT capability false executes real safe copy fallback with identical colour and depth pixels");
        config.ds_as_rt=nullptr;
    }

    bool normalized=true;u32 normalizedCases=0,fp32Cases=0;bool loggedColorClipFailure=false;
    {
        GSTextureOGL clip(GSTexture::RenderTarget,4,4,1,GSTexture::Format::ColorClip);
        GSTextureOGL clipCopy(GSTexture::Usage::Texture,4,4,1,GSTexture::Format::ColorClip);
        const bool unorm=target.m_features.rgba16_unorm;
        if(!unorm)
        {
            const int floatSupport=GLAD_GL_EXT_color_buffer_float;
            GLAD_GL_EXT_color_buffer_float=0; // mask DOWN, never submit an unsupported allocation
            GSTexture* unavailable=target.CreateSurface(GSTexture::RenderTarget,4,4,1,GSTexture::Format::ColorClip);
            GLAD_GL_EXT_color_buffer_float=floatSupport;
            check(unavailable==nullptr,"GL ColorClip factory rejects absent UNORM16 and FP32 attachment support before allocation");
            delete unavailable;
        }
        normalized&=clip.GetGLFormat()==ResolveOGLColorClipStorage(unorm).format&&clipCopy.GetGLFormat()==clip.GetGLFormat();
        if(!unorm)std::fprintf(stderr,"SKIP GL physical UNORM16 precision: EXT_texture_norm16 absent; executing real FP32 ColorClip fallback instead\n");
        constexpr u16 values[]={0,1,127,128,255,256,257,511,512,0x1234,0x7fff,0xffff};
        for(bool enabled:{false,true})
        {
            target.m_fetch_caps=enabled?caps:noFetchCaps;
            const auto plan=target.SelectFetchPlan(color,OGLStorageSupportsARMColorFetch(clip.GetGLFormat()),false,
                clip.GetGLFormat()==GL_RGBA16);
            normalized&=unorm||caps.preferred!=GSFramebufferFetchBackend::ARM||plan.Color()==GSFramebufferFetchBackend::None;
            const GLuint p=program(color,plan,"\nvoid main(){uint v=uint(round(sample_from_rt().r*65535.));o_col0=vec4(float(v&255u)/255.,float(v>>8)/255.,0.,1.);}\n");
            if(!p){normalized=false;continue;}const ScopedGuard deleteProgram([&](){glDeleteProgram(p);});
            if(!attach(clip.GetID(),0)){normalized=false;continue;}glUseProgram(p);
            target.m_index.count=3;config.nindices=3;config.ps=color;config.rt=&clip;config.ds=nullptr;
            for(u16 value:values)
            {
                std::array<u16,64> seed{};for(u32 i=0;i<seed.size();i+=4){seed[i]=value;seed[i+3]=0xffff;}
                std::array<float,64> floatSeed{};for(u32 i=0;i<floatSeed.size();i+=4){floatSeed[i]=static_cast<float>(value)/65535.f;floatSeed[i+3]=1.f;}
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,clip.GetID());
                glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,unorm?GL_UNSIGNED_SHORT:GL_FLOAT,
                    unorm?static_cast<const void*>(seed.data()):static_cast<const void*>(floatSeed.data()));
                GLState::tex_unit[2]=0;GLFetchPlanOracle::draws=GLFetchPlanOracle::copies=0;
                const bool copy=plan.Color()==GSFramebufferFetchBackend::None;
                target.SendHWDraw(config,copy?&clipCopy:nullptr,&clip,nullptr,nullptr,false,false,color,plan);
                if(unorm)
                {
                    std::array<u8,64> image{};glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
                    bool caseExact=true;u32 firstBadPixel=0xffffffffu;
                    for(u32 i=0;i<image.size();i+=4)
                    {
                        const bool pixelExact=image[i]==(value&255)&&image[i+1]==(value>>8)&&image[i+2]==0&&image[i+3]==255;
                        caseExact&=pixelExact;
                        if(!pixelExact&&firstBadPixel==0xffffffffu)firstBadPixel=i/4;
                    }
                    normalized&=caseExact;
                    ++normalizedCases;
                    const GLenum error=glGetError();
                    normalized&=error==GL_NO_ERROR;
                    if((!caseExact||error!=GL_NO_ERROR)&&!loggedColorClipFailure)
                    {
                        std::array<u16,64> raw{};GLenum rawError=GL_NO_ERROR;
                        if(clip.GetIntType()==GL_UNSIGNED_SHORT)
                        {
                            glReadPixels(0,0,4,4,clip.GetIntFormat(),clip.GetIntType(),raw.data());
                            rawError=glGetError();
                        }
                        const u32 pixel=firstBadPixel==0xffffffffu?10:firstBadPixel;
                        const u32 i=pixel*4;
                        std::fprintf(stderr,"GL_COLORCLIP_FIRST_FAIL enabled=%u plan=%u seed_rgba16=%u,0,0,65535 expected_rgba8=%u,%u,0,255 actual_rgba8=%u,%u,%u,%u raw_read_format=%x raw_read_type=%x raw_rgba16=%u,%u,%u,%u raw_error=%x error=%x storage_gl_format=%x storage_read_format=%x storage_read_type=%x copy=%u draws=%u copies=%u pixel=%u\n",
                            unsigned(enabled),plan.key,unsigned(value),unsigned(value&255),unsigned(value>>8),unsigned(image[i]),unsigned(image[i+1]),unsigned(image[i+2]),unsigned(image[i+3]),
                            unsigned(clip.GetIntFormat()),unsigned(clip.GetIntType()),unsigned(raw[i]),unsigned(raw[i+1]),unsigned(raw[i+2]),unsigned(raw[i+3]),unsigned(rawError),unsigned(error),
                            unsigned(clip.GetGLFormat()),unsigned(clip.GetIntFormat()),unsigned(clip.GetIntType()),unsigned(copy),GLFetchPlanOracle::draws,GLFetchPlanOracle::copies,pixel);
                        normalized&=rawError==GL_NO_ERROR;
                        if(enabled&&plan.Color()==GSFramebufferFetchBackend::EXT&&clip.GetIntType()==GL_UNSIGNED_SHORT)
                        {
                            const auto runRawFetchProbe=[&](bool explicitHighp,const char* qualifierLabel)
                            {
                                std::string source=target.GetPSSource(color,plan);
                                constexpr std::string_view qualifier="#define TARGET_0_QUALIFIER inout";
                                const size_t qualifierAt=source.find(qualifier);
                                const bool qualifierFound=qualifierAt!=std::string::npos;
                                if(explicitHighp&&qualifierFound)
                                    source.replace(qualifierAt,qualifier.size(),"#define TARGET_0_QUALIFIER inout highp");
                                const GLuint probeFragment=GLFetchOracle::Compile(GL_FRAGMENT_SHADER,
                                    GLFetchOracle::ProbeMain(std::move(source),"\nvoid main(){o_col0=sample_from_rt();}\n"));
                                const GLuint probeProgram=GLFetchOracle::Link(vertex,probeFragment);
                                if(probeFragment)glDeleteShader(probeFragment);
                                if(!probeProgram)
                                {
                                    std::fprintf(stderr,"GL_COLORCLIP_FETCH_PROBE compile_link=failed qualifier=%s marker=%u\n",
                                        qualifierLabel,unsigned(qualifierFound));
                                    return;
                                }
                                const ScopedGuard deleteProbe([&](){glDeleteProgram(probeProgram);});
                                for(u16 probeValue:{u16{1},u16{257},u16{32768}})
                                for(u32 mode=0;mode<3;++mode)
                                {
                                    attach(clip.GetID(),0);
                                    std::array<u16,64> probeSeed{};
                                    for(u32 n=0;n<probeSeed.size();n+=4){probeSeed[n]=probeValue;probeSeed[n+3]=0xffff;}
                                    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,clip.GetID());
                                    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_SHORT,probeSeed.data());
                                    if(mode==1)
                                    {
                                        glBindFramebuffer(GL_FRAMEBUFFER,fbo);
                                        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,0,0);
                                        attach(clip.GetID(),0); // explicit post-upload detach/rebind control
                                    }
                                    else if(mode==2)
                                        glFinish(); // post-upload completion control, without a pre-draw readback
                                    const GLenum setupError=glGetError();
                                    glUseProgram(probeProgram);config.ps=color;config.rt=&clip;config.ds=nullptr;
                                    GLState::tex_unit[2]=0;GLFetchPlanOracle::draws=GLFetchPlanOracle::copies=0;
                                    target.SendHWDraw(config,nullptr,&clip,nullptr,nullptr,false,false,color,plan);
                                    std::array<u16,64> probeResult{};
                                    glReadPixels(0,0,4,4,clip.GetIntFormat(),clip.GetIntType(),probeResult.data());
                                    const GLenum probeError=glGetError();
                                    std::fprintf(stderr,"GL_COLORCLIP_FETCH_PROBE qualifier=%s mode=%s enabled=1 plan=%u seed_rgba16=%u,0,0,65535 actual_raw_rgba16=%u,%u,%u,%u read_format=%x read_type=%x setup_error=%x probe_error=%x draws=%u copies=%u center_pixel=10\n",
                                        qualifierLabel,mode==0?"attached":mode==1?"detach-rebind":"glFinish",plan.key,unsigned(probeValue),
                                        unsigned(probeResult[40]),unsigned(probeResult[41]),unsigned(probeResult[42]),unsigned(probeResult[43]),
                                        unsigned(clip.GetIntFormat()),unsigned(clip.GetIntType()),unsigned(setupError),unsigned(probeError),
                                        GLFetchPlanOracle::draws,GLFetchPlanOracle::copies);
                                }
                            };
                            runRawFetchProbe(false,"inherited-precision");
                            runRawFetchProbe(true,"explicit-highp");

                            // A pre-read is intentionally isolated here because it may flush
                            // tile state. Validate the exact seed first, then compare fetch
                            // and the known-good sampler-copy route on the same attachment.
                            const auto runSeedAndCopyControl=[&](bool useFetch)
                            {
                                target.m_fetch_caps=useFetch?caps:noFetchCaps;
                                const auto controlPlan=target.SelectFetchPlan(color,
                                    OGLStorageSupportsARMColorFetch(clip.GetGLFormat()),false,
                                    clip.GetGLFormat()==GL_RGBA16);
                                const GLuint controlFragment=GLFetchOracle::Compile(GL_FRAGMENT_SHADER,
                                    GLFetchOracle::ProbeMain(target.GetPSSource(color,controlPlan),
                                        "\nvoid main(){o_col0=sample_from_rt();}\n"));
                                const GLuint controlProgram=GLFetchOracle::Link(vertex,controlFragment);
                                if(controlFragment)glDeleteShader(controlFragment);
                                if(!controlProgram)
                                {
                                    std::fprintf(stderr,"GL_COLORCLIP_SEED_CONTROL compile_link=failed route=%s plan=%u\n",
                                        useFetch?"fetch":"copy",controlPlan.key);
                                    return;
                                }
                                const ScopedGuard deleteControl([&](){glDeleteProgram(controlProgram);});
                                attach(clip.GetID(),0);
                                std::array<u16,64> controlSeed{};
                                for(u32 n=0;n<controlSeed.size();n+=4){controlSeed[n]=32768;controlSeed[n+3]=0xffff;}
                                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,clip.GetID());
                                glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_SHORT,controlSeed.data());
                                std::array<u16,64> before{};
                                glReadPixels(0,0,4,4,clip.GetIntFormat(),clip.GetIntType(),before.data());
                                const GLenum beforeError=glGetError();
                                config.ps=color;config.rt=&clip;config.ds=nullptr;config.ds_as_rt=nullptr;
                                glUseProgram(controlProgram);GLState::tex_unit[2]=0;
                                GLFetchPlanOracle::draws=GLFetchPlanOracle::copies=0;
                                target.SendHWDraw(config,useFetch?nullptr:&clipCopy,&clip,nullptr,nullptr,false,false,color,controlPlan);
                                std::array<u16,64> after{};
                                glReadPixels(0,0,4,4,clip.GetIntFormat(),clip.GetIntType(),after.data());
                                const GLenum afterError=glGetError();
                                std::fprintf(stderr,"GL_COLORCLIP_SEED_CONTROL route=%s plan=%u expected_seed_rgba16=32768,0,0,65535 pre_draw_rgba16=%u,%u,%u,%u after_draw_rgba16=%u,%u,%u,%u read_format=%x read_type=%x before_error=%x after_error=%x draws=%u copies=%u\n",
                                    useFetch?"fetch":"copy",controlPlan.key,unsigned(before[40]),unsigned(before[41]),unsigned(before[42]),unsigned(before[43]),
                                    unsigned(after[40]),unsigned(after[41]),unsigned(after[42]),unsigned(after[43]),unsigned(clip.GetIntFormat()),unsigned(clip.GetIntType()),
                                    unsigned(beforeError),unsigned(afterError),GLFetchPlanOracle::draws,GLFetchPlanOracle::copies);
                            };
                            runSeedAndCopyControl(true);
                            runSeedAndCopyControl(false);
                            target.m_fetch_caps=caps;
                        }
                        loggedColorClipFailure=true;
                    }
                }
                else
                {
                    std::array<float,64> image{};glReadPixels(0,0,4,4,GL_RGBA,GL_FLOAT,image.data());
                    for(u32 i=0;i<image.size();i+=4)normalized&=std::lround(image[i]*255.f)==(value&255)&&
                        std::lround(image[i+1]*255.f)==(value>>8)&&image[i+2]==0.f&&image[i+3]==1.f;
                    ++fp32Cases;
                }
                const GLenum error=unorm?GL_NO_ERROR:glGetError();
                normalized&=GLFetchPlanOracle::draws==1&&GLFetchPlanOracle::copies==static_cast<u32>(copy)&&error==GL_NO_ERROR;
                if(error)std::fprintf(stderr,"GL_COLORCLIP_ERROR storage=%x value=%u error=%x\n",clip.GetGLFormat(),value,error);
            }
        }
    }
    check(normalized&&normalizedCases+fp32Cases==24,"GL actual supported ColorClip storage preserves sixteen-bit encoding and legal fetch/copy precision");

// Draft block: legal RGBA8 software-colclip proof, not norm16 hardware evidence.
    bool wrapped=true;u32 wrappedCases=0;
    for(bool enabled:{false,true})
    {
        target.m_fetch_caps=enabled?caps:noFetchCaps;
        PSSelector wrap=color;wrap.colclip=1;wrap.blend_a=2;
        const auto plan=target.SelectFetchPlan(wrap,true,false);
        const std::string source=target.GetPSSource(wrap,plan);
        wrapped&=source.find("#define PS_COLCLIP 1")!=std::string::npos&&
            source.find("#define PS_COLCLIP_HW 0")!=std::string::npos;
        const GLuint p=program(wrap,plan,"\nuniform highp int WrapDelta;void main(){vec3 c=round(sample_from_rt().rgb*255.)+vec3(float(WrapDelta));ps_color_clamp_wrap(c);o_col0=vec4(c/255.,1.);}\n");
        if(!p){wrapped=false;continue;}const ScopedGuard deleteProgram([&](){glDeleteProgram(p);});
        wrapped&=attach(rt.GetID(),0);glUseProgram(p);
        target.m_index.count=3;config.nindices=3;config.ps=wrap;config.rt=&rt;config.ds=nullptr;config.ds_as_rt=nullptr;
        for(s32 delta:{-257,-256,-255,-1,0,1,255,256,257,511})
        for(u8 value:{u8{0},u8{1},u8{127},u8{128},u8{254},u8{255}})
        {
            std::array<u8,64> seed{};for(u32 i=0;i<seed.size();i+=4){seed[i]=value;seed[i+1]=16;seed[i+2]=32;seed[i+3]=255;}
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,rt.GetID());
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,seed.data());
            glUniform1i(glGetUniformLocation(p,"WrapDelta"),delta);
            GLState::tex_unit[2]=0;GLFetchPlanOracle::draws=GLFetchPlanOracle::copies=0;
            const bool copy=plan.Color()==GSFramebufferFetchBackend::None;
            target.SendHWDraw(config,copy?&rtCopy:nullptr,&rt,nullptr,nullptr,false,false,wrap,plan);
            std::array<u8,64> image{};glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
            for(u32 i=0;i<image.size();i+=4)wrapped&=image[i]==static_cast<u8>(static_cast<s32>(value)+delta)&&
                image[i+1]==static_cast<u8>(16+delta)&&image[i+2]==static_cast<u8>(32+delta)&&image[i+3]==255;
            const GLenum error=glGetError();wrapped&=error==GL_NO_ERROR&&GLFetchPlanOracle::draws==1&&GLFetchPlanOracle::copies==static_cast<u32>(copy);
            ++wrappedCases;
        }
    }
    check(wrapped&&wrappedCases==120,"GL real RGBA8 software-colclip wrap preserves signed and nine-bit boundaries under ARM fetch and copies");

// Draft native member-function block: physical FP32 ColorClip transfer proof.
    if(!target.m_features.rgba16_unorm)
    {
        bool transfers=true;u32 transferCases=0;
        GLuint readFbo=0;glGenFramebuffers(1,&readFbo);target.m_fbo_read=readFbo;
        const ScopedGuard deleteReadFbo([&](){target.m_texture_upload_buffer.reset();target.m_fbo_read=0;glDeleteFramebuffers(1,&readFbo);});
        GSTextureOGL physical(GSTexture::RenderTarget,5,3,1,GSTexture::Format::ColorClip);
        transfers&=physical.GetGLFormat()==GL_RGBA32F&&physical.GetIntType()==GL_FLOAT&&physical.GetIntShift()==4&&
            physical.CalcUploadPitch(5)==80&&physical.GetMemUsage()==240;
        std::array<float,6*3*4> input{};
        for(u32 y=0;y<3;++y)for(u32 x=0;x<6;++x)for(u32 c=0;c<4;++c)
            input[(y*6+x)*4+c]=static_cast<float>(y*113+x*19+c*7)/65535.f;
        const auto download=[&](bool pbo){
            target.m_disable_download_pbo=!pbo;
            auto output=GSDownloadTextureOGL::Create(7,5,GSTexture::Format::ColorClip);
            if(!output)return false;
            output->CopyFromTexture(GSVector4i(1,1,6,4),&physical,GSVector4i(0,0,5,3),0,false);
            std::array<u8,16+96*3+16> image{},expected{};image.fill(0xa5);expected=image;
            for(u32 y=0;y<3;++y)std::memcpy(expected.data()+16+y*96,input.data()+y*6*4,80);
            const bool read=output->ReadTexels(GSVector4i(1,1,6,4),image.data()+16,96);
            const bool valid=read&&output->GetMapPitch()==128&&image==expected&&glGetError()==GL_NO_ERROR;
            if(!valid)std::fprintf(stderr,"GL_FP32_TRANSFER pbo=%u read=%u pitch=%u pixels=%u\n",pbo,read,output->GetMapPitch(),image==expected);
            return valid;
        };
        // Actual client upload with a padded source row, then CPU/PBO readback.
        transfers&=physical.Update(GSVector4i(0,0,5,3),input.data(),96);
        for(bool pbo:{false,true}){transfers&=download(pbo);++transferCases;}
        target.m_texture_upload_buffer=GLStreamBuffer::Create(GL_PIXEL_UNPACK_BUFFER,65536);
        transfers&=target.m_texture_upload_buffer!=nullptr;
        if(target.m_texture_upload_buffer)
        {
            // Actual PBO upload and odd row stride.
            transfers&=physical.Update(GSVector4i(0,0,5,3),input.data(),96);
            for(bool pbo:{false,true}){transfers&=download(pbo);++transferCases;}
            // Map/Unmap uses a different aligned pitch and must skip padding.
            GSTexture::GSMap map{};
            const bool mapped=physical.Map(map);
            transfers&=mapped;
            if(mapped)
            {
                transfers&=map.pitch==128;
                for(u32 y=0;y<3;++y)std::memcpy(map.bits+y*map.pitch,input.data()+y*6*4,80);
                physical.Unmap();
                for(bool pbo:{false,true}){transfers&=download(pbo);++transferCases;}
            }
        }
        target.m_disable_download_pbo=false;
        check(transfers&&transferCases==6,"GL actual FP32 ColorClip client/PBO/map uploads and CPU/PBO downloads preserve odd rows rectangles and canaries");
    }

    target.m_fetch_caps=caps;const auto hdrPlan=target.SelectFetchPlan(color,false,false);
    bool hdr=caps.preferred!=GSFramebufferFetchBackend::ARM||hdrPlan.Color()==GSFramebufferFetchBackend::None;
    GLuint floats[2]{};glGenTextures(2,floats);
    const ScopedGuard deleteFloats([&](){glDeleteTextures(2,floats);});
    std::array<float,64> floatSeed{};for(u32 i=0;i<floatSeed.size();i+=4){floatSeed[i]=0.25f;floatSeed[i+1]=0.5f;floatSeed[i+2]=0.75f;floatSeed[i+3]=1;}
    for(GLuint texture:floats){glBindTexture(GL_TEXTURE_2D,texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,4,4,0,GL_RGBA,GL_FLOAT,floatSeed.data());}
    hdr&=attach(floats[0],0);target.m_index.count=9;
    const GLuint fp=program(color,hdrPlan,"\nvoid main(){o_col0=sample_from_rt()+vec4(0.125,0.,0.,0.);}\n");
    const ScopedGuard deleteFloatProgram([&](){if(fp)glDeleteProgram(fp);});
    hdr&=fp!=0;
    if(fp)
    {
        glUseProgram(fp);glBindTextureUnit(2,floats[1]);
        GLFetchPlanOracle::draws=GLFetchPlanOracle::copies=0;
        config.nindices=9;config.rt=&rt;config.ds=nullptr;config.ps=color;
        for(u32 i=0;i<9;i+=3)
        {
            // True float attachment, rather than the legacy OGL ColorHDR
            // object's RGBA8 allocation. Execute actual copy API and Draw.
            if(GLAD_GL_ES_VERSION_3_2&&glCopyImageSubData)glCopyImageSubData(floats[0],GL_TEXTURE_2D,0,0,0,0,floats[1],GL_TEXTURE_2D,0,0,0,0,4,4,1);
            else if(GLAD_GL_EXT_copy_image&&glCopyImageSubDataEXT)glCopyImageSubDataEXT(floats[0],GL_TEXTURE_2D,0,0,0,0,floats[1],GL_TEXTURE_2D,0,0,0,0,4,4,1);
            else if(GLAD_GL_OES_copy_image&&glCopyImageSubDataOES)glCopyImageSubDataOES(floats[0],GL_TEXTURE_2D,0,0,0,0,floats[1],GL_TEXTURE_2D,0,0,0,0,4,4,1);
            else{hdr=false;break;}
            target.Draw(config,i,3);
        }
        std::array<float,64> image{};glReadPixels(0,0,4,4,GL_RGBA,GL_FLOAT,image.data());
        for(u32 i=0;i<image.size();i+=4)hdr&=image[i]==0.625f&&image[i+1]==0.5f&&image[i+2]==0.75f&&image[i+3]==1;
        hdr&=GLFetchPlanOracle::draws==3&&GLFetchPlanOracle::copies==3&&glGetError()==GL_NO_ERROR;
    }
    check(hdr,"GL trueRGBA16F attachments use legal selected fallback and preserve float copy pixels");

    bool alpha=true;u32 alphaCases=0;
    target.m_index.count=3;config.nindices=3;config.rt=&rt;config.ds=nullptr;
    config.alpha_second_pass.enable=true;config.alpha_second_pass.ps=color;
    for(bool enabled:{false,true})
    {
        target.m_fetch_caps=enabled?caps:noFetchCaps;
        config.ps=plain;
        const auto first=target.SelectFetchPlan(config.ps,true,false);
        const auto second=target.SelectFetchPlan(config.alpha_second_pass.ps,true,false);
        const std::string firstSource=target.GetPSSource(config.ps,first);
        const std::string secondSource=target.GetPSSource(config.alpha_second_pass.ps,second);
        ProgramSelector keyA{},keyB{};keyA.ps=keyB.ps=color;keyA.fetch=first;keyB.fetch=second;
        alpha&=first.Color()==GSFramebufferFetchBackend::None&&firstSource!=secondSource&&
            (second.Color()==GSFramebufferFetchBackend::None||keyA!=keyB);
        const GLuint p0=program(plain,first,"\nvoid main(){o_col0=vec4(8./255.,16./255.,32./255.,64./255.);}\n");
        const GLuint p1=program(color,second,"\nvoid main(){o_col0=sample_from_rt()+vec4(1./255.,0.,0.,0.);}\n");
        const ScopedGuard programs([&](){if(p0)glDeleteProgram(p0);if(p1)glDeleteProgram(p1);});
        if(!p0||!p1){alpha=false;continue;}
        alpha&=attach(rt.GetID(),0);GLState::tex_unit[2]=0;
        glUseProgram(p0);target.SendHWDraw(config,nullptr,&rt,nullptr,nullptr,false,false,config.ps,first);
        glUseProgram(p1);target.SendHWDraw(config,second.Color()==GSFramebufferFetchBackend::None?&rtCopy:nullptr,&rt,nullptr,nullptr,
            false,false,config.alpha_second_pass.ps,second);
        std::array<u8,64> image{};glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
        for(u32 i=0;i<image.size();i+=4)alpha&=image[i]==9&&image[i+1]==16&&image[i+2]==32&&image[i+3]==64;
        alpha&=glGetError()==GL_NO_ERROR;++alphaCases;
    }
    check(alpha&&alphaCases==2,"GL second alpha pass uses its own selected source key and prior pass pixels under fetch and copy");
    std::printf("GL_FETCH_PRECISION directCases=%u norm16Cases=%u fp32Cases=%u alphaCases=%u\n",directCases,normalizedCases,fp32Cases,alphaCases);
}
