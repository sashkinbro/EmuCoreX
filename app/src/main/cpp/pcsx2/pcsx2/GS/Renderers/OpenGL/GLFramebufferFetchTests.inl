// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Unreferenced RED scaffold. Include in GSDeviceOGL.cpp ONLY under native
// selftests and declare the static entry in the class under the same guard.
// No release hooks; synthetic caps are structural, never physical GPU evidence.
#include "GS/Renderers/Common/GSFramebufferFetchPolicy.h"
#include "GLTextureStorageTests.inl"
#include "GS/Renderers/Common/GSFeedbackBarrierPlan.h"

extern "C" void EmuCoreXOracleFeedbackDrawlistTargets(void (*check)(bool,const char*));
extern "C" void EmuCoreXOracleFeedbackBlendStages(void (*check)(bool,const char*));

namespace GLFetchOracle
{
    static PFNGLGETSTRINGPROC realGetString = nullptr;
    static PFNGLGETBOOLEANVPROC realGetBooleanv = nullptr;
    static const char* vendor = nullptr;
    static const char* renderer = nullptr;
    static const char* version = nullptr;
    static GLContext* activeContext = nullptr;

    const GLubyte* GLAPIENTRY GetString(GLenum name)
    {
        if (vendor && name == GL_VENDOR) return reinterpret_cast<const GLubyte*>(vendor);
        if (renderer && name == GL_RENDERER) return reinterpret_cast<const GLubyte*>(renderer);
        if (version && name == GL_VERSION) return reinterpret_cast<const GLubyte*>(version);
        return realGetString(name);
    }

    void GLAPIENTRY GetBooleanv(GLenum name,GLboolean* value)
    {
        // Synthetic ARM availability is CPU control-flow evidence. Its
        // extension enum must never be queried on a physical non-ARM driver.
        if(vendor&&name==GL_FRAGMENT_SHADER_FRAMEBUFFER_FETCH_MRT_ARM){*value=GL_FALSE;return;}
        realGetBooleanv(name,value);
    }

    GLuint Compile(GLenum type, const std::string& source)
    {
        const GLuint shader = glCreateShader(type);
        const char* text = source.c_str();
        glShaderSource(shader, 1, &text, nullptr);
        glCompileShader(shader);
        GLint success = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (success) return shader;
        std::array<char, 8192> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::fprintf(stderr, "GL_FETCH_COMPILE type=%u %s\n", type, log.data());
        glDeleteShader(shader);
        return 0;
    }

    GLuint Link(GLuint vertex, GLuint fragment)
    {
        if (!vertex || !fragment) return 0;
        const GLuint program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        GLint success = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (success) return program;
        std::array<char, 8192> log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::fprintf(stderr, "GL_FETCH_LINK %s\n", log.data());
        glDeleteProgram(program);
        return 0;
    }

    struct IndexedCall {GLenum mode=0,type=0;GLuint first=0,last=0;GLsizei count=0;const void* indices=nullptr;GLint base=0;bool ranged=false;};
    inline IndexedCall indexedCall;
    void GLAPIENTRY DrawRange(GLenum mode,GLuint first,GLuint last,GLsizei count,GLenum type,const void* indices,GLint base)
    {indexedCall={mode,type,first,last,count,indices,base,true};}
    void GLAPIENTRY DrawPlain(GLenum mode,GLsizei count,GLenum type,const void* indices,GLint base)
    {indexedCall={mode,type,0,0,count,indices,base,false};}

    // Canonical TFX interface; even a renamed production main can keep its
    // inputs active during driver linkage. Match interpolation explicitly.
    std::string VertexBody(bool iip)
    {
        return std::string("out SHADER{vec4 t_float;vec4 t_int;") +
            (iip ? "vec4 c;" : "flat vec4 c;") + R"(
float inv_cov;flat uint interior;} VSout;
void main(){int i=gl_VertexID%3;vec2 p=i==0?vec2(-1.,-1.):i==1?vec2(3.,-1.):vec2(-1.,3.);
gl_Position=vec4(p,0.,1.);VSout.t_float=vec4(0.);VSout.t_int=vec4(0.);VSout.c=vec4(1.);VSout.inv_cov=0.;VSout.interior=1u;}
)";
    }

    // Retain the production declarations, directives and sample helpers; only
    // replace main to isolate their contract from unrelated TFX arithmetic.
    std::string ProbeMain(std::string source, const char* main)
    {
        const std::string entry = "#define ps_main main";
        const size_t at = source.find(entry);
        if (at == std::string::npos) return {};
        source.replace(at, entry.size(), "#define ps_main emucorex_original_ps_main");
        source += main;
        return source;
    }
}

void GSDeviceOGL::TestFramebufferFetch(void (*check)(bool, const char*))
{
    GLFetchStorageOracle::Run(check);
    EmuCoreXOracleFeedbackDrawlistTargets(check);
    EmuCoreXOracleFeedbackBlendStages(check);
    using namespace GLFetchOracle;
    check(!g_gs_device, "GL fetch oracle starts without another graphics device");
    if (g_gs_device) return;
    WindowInfo window;
    window.type = WindowInfo::Type::Surfaceless;
    const std::array<GLContext::Version, 3> versions = {{{GLContext::Profile::ES, 3, 2},
        {GLContext::Profile::ES, 3, 1}, {GLContext::Profile::ES, 3, 0}}};
    Error error;
    auto context = GLContext::Create(window, versions, &error);
    check(context && context->IsGLES() && context->MakeCurrent(), "GL fetch real surfaceless EGL ES context");
    if (!context) { std::fprintf(stderr, "GL_FETCH_EGL %s\n", error.GetDescription().c_str()); return; }
    if (!context->IsGLES() || !context->IsCurrent()) return;
    activeContext = context.get();
    realGetString = glad_glGetString;
    realGetBooleanv = glad_glGetBooleanv;
    glad_glGetBooleanv = GetBooleanv;
    const bool physicalARM = GLAD_GL_ARM_shader_framebuffer_fetch;
    const bool physicalEXT = GLAD_GL_EXT_shader_framebuffer_fetch;
    const auto savedConfig = GSConfig;
    const auto savedResources = EmuFolders::Resources;
    const ScopedGuard restore([&]() {
        vendor = renderer = version = nullptr;
        glad_glGetString = realGetString;
        glad_glGetBooleanv = realGetBooleanv;
        // CheckFeatures installs compatibility entry points. Reload actual
        // functions/caps before dropping this context, never retain fake caps.
        gladLoadGLES2(+[](const char* name) {
            return reinterpret_cast<GLADapiproc>(activeContext->GetProcAddress(name));
        });
        GSConfig = savedConfig;
        EmuFolders::Resources = savedResources;
        activeContext = nullptr;
        context->DoneCurrent();
    });
    GSDeviceOGL device;
    device.m_is_gles = true;
    EmuFolders::Resources = "resources";
    const auto source = ReadShaderSource("shaders/opengl/tfx_fs.glsl");
    check(source && !source->empty(), "GL fetch reads canonical production TFX fragment source");
    if (!source || source->empty()) return;
    device.m_shader_tfx_fs = *source;

    // Capture production submission arguments without forwarding fake indices.
    // Omitting range hints, changing byte offsets/base vertex, or dropping the
    // no-range/empty-vertex fallback breaks these independently derived literals.
    {
        const auto range=glad_glDrawRangeElementsBaseVertex;const auto plain=glad_glDrawElementsBaseVertex;
        const ScopedGuard restoreDraws([&](){glad_glDrawRangeElementsBaseVertex=range;glad_glDrawElementsBaseVertex=plain;});
        glad_glDrawRangeElementsBaseVertex=DrawRange;glad_glDrawElementsBaseVertex=DrawPlain;
        device.m_draw_topology=GL_TRIANGLES;device.m_vertex.start=11;device.m_vertex.count=7;
        device.m_index.start=13;device.m_index.count=9;
        device.DrawIndexedPrimitive();
        check(indexedCall.ranged&&indexedCall.first==0&&indexedCall.last==6&&indexedCall.count==9&&
            indexedCall.indices==reinterpret_cast<void*>(26)&&indexedCall.base==11&&indexedCall.type==GL_UNSIGNED_SHORT,
            "GL full indexed production draw forwards uploaded range byte offset and base vertex");
        device.DrawIndexedPrimitive(3,6);
        check(indexedCall.ranged&&indexedCall.last==6&&indexedCall.count==6&&indexedCall.indices==reinterpret_cast<void*>(32)&&indexedCall.base==11,
            "GL partial indexed production draw retains full vertex range and relative index offset");
        device.m_vertex.count=0;device.DrawIndexedPrimitive(3,6);
        check(!indexedCall.ranged&&indexedCall.count==6&&indexedCall.indices==reinterpret_cast<void*>(32)&&indexedCall.base==11,
            "GL zero vertex range preserves unranged fallback without unsigned underflow");
        glad_glDrawRangeElementsBaseVertex=nullptr;device.m_vertex.count=7;device.DrawIndexedPrimitive();
        check(!indexedCall.ranged&&indexedCall.count==9&&indexedCall.indices==reinterpret_cast<void*>(26),
            "GL missing ranged entry preserves actual unranged submission");
    }

    bool featureAgreement = true, directives = true, ordering = true, veto = true;
    u32 structuralCases = 0;
    struct Identity { const char* vendor; const char* renderer; bool mobile; };
    constexpr Identity identities[] = {{"ARM", "Mali-G57", true},
        {"Imagination Technologies", "PowerVR Rogue", true},
        {"Qualcomm", "Adreno (TM) 740", false}, {"Mesa", "llvmpipe", false}};
    for (const auto& identity : identities)
    for (bool arm : {false, true})
    for (bool ext : {false, true})
    for (bool disabled : {false, true})
    for (auto depth : {GSDepthFeedbackMode::None, GSDepthFeedbackMode::Depth,
        GSDepthFeedbackMode::DepthAsRT, GSDepthFeedbackMode::Auto})
    {
        vendor = identity.vendor;
        renderer = identity.renderer;
        glad_glGetString = GetString;
        GLAD_GL_ARM_shader_framebuffer_fetch = arm;
        GLAD_GL_EXT_shader_framebuffer_fetch = ext;
        GSConfig = savedConfig;
        GSConfig.DisableFramebufferFetch = disabled;
        GSConfig.OverrideTextureBarriers = -1;
        GSConfig.DepthFeedbackMode = depth;
        device.m_features = {};
        const bool initialized = device.CheckFeatures();
        const auto expected = DecideGLFramebufferFetch(arm, ext, false, false, disabled, identity.mobile);
        const std::string header = device.GenGlslHeader("main", GL_FRAGMENT_SHADER, "");
        featureAgreement &= initialized && device.m_features.framebuffer_fetch == expected.enabled;
        ordering &= device.m_features.framebuffer_fetch_orders_overlap == FbFetchOrdersOverlappingPrims(expected.backend);
        if (expected.backend == GSFramebufferFetchBackend::ARM)
            directives &= header.find("#extension GL_ARM_shader_framebuffer_fetch : require") != std::string::npos;
        else if (expected.backend == GSFramebufferFetchBackend::EXT)
            directives &= header.find("#extension GL_EXT_shader_framebuffer_fetch : require") != std::string::npos;
        else
            veto &= !device.m_features.framebuffer_fetch &&
                header.find("#define HAS_FRAMEBUFFER_FETCH 0") != std::string::npos;
        ++structuralCases;
    }
    check(featureAgreement, "GL runtime ARM-only dual-extension and user-veto decisions match selected backend policy");
    check(directives, "GL shader directives enable the selected ARM or EXT backend");
    check(ordering, "GL selected ARM publishes overlap ordering without promoting other backends");
    check(veto, "GL disabled or absent fetch never resurrects in the shader header");
    std::printf("GL_FETCH_STRUCTURAL cases=%u physicalARM=%u physicalEXT=%u\n", structuralCases, physicalARM, physicalEXT);

    // Physical driver only from here: no synthetic extension is compiled or
    // submitted, and unsupported MRT/float configurations are not GPU-run.
    vendor = renderer = version = nullptr;
    glad_glGetString = realGetString;
    glad_glGetBooleanv = realGetBooleanv;
    GLAD_GL_ARM_shader_framebuffer_fetch = physicalARM;
    GLAD_GL_EXT_shader_framebuffer_fetch = physicalEXT;
    GSConfig = savedConfig;
    GSConfig.DisableFramebufferFetch = false;
    device.m_features = {};
    if (!device.CheckFeatures()) { check(false, "GL fetch physical feature initialization"); return; }
    std::fprintf(stderr,"GL_FETCH_RAW_DRIVER vendor=%s renderer=%s version=%s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
    #include "GLParityTests.inl"
    if(device.m_fetch_caps.adreno&&device.m_features.framebuffer_fetch&&GSConfig.DepthFeedbackMode==GSDepthFeedbackMode::Auto)
        check(device.m_features.depth_feedback==DepthFeedbackSupport::Depth,
            "GL Adreno Auto uses direct depth attachment instead of incompatible depth-as-colour output");
    const std::string vertexSource = device.GenGlslHeader("main", GL_VERTEX_SHADER, "") + VertexBody(true);
    GLuint vertex = Compile(GL_VERTEX_SHADER, vertexSource);
    const ScopedGuard deleteVertex([&]() { if (vertex) glDeleteShader(vertex); });
    bool links = vertex != 0;
    GLuint colorProgram = 0;
    for (u32 variant = 0; variant < 5; ++variant)
    {
        PSSelector selector{};
        selector.tfx = 4;
        selector.iip = true;
        selector.no_color1 = variant != 4;
        selector.color_feedback = variant != 1;
        selector.no_color = variant == 1;
        selector.depth_feedback = variant == 2 || variant == 3;
        device.m_features.depth_feedback = variant == 3 ? DepthFeedbackSupport::Depth : DepthFeedbackSupport::DepthAsRT;
        const char* probe = variant == 1 ? "\nvoid main(){gl_FragDepth=0.5;}\n" :
            "\nvoid main(){o_col0=sample_from_rt()+vec4(1.0/255.0,0.,0.,0.);}\n";
        std::string fragmentSource = ProbeMain(device.GetPSSource(selector), probe);
        const GLuint fragment = fragmentSource.empty() ? 0 : Compile(GL_FRAGMENT_SHADER, fragmentSource);
        const GLuint program = Link(vertex, fragment);
        links &= program != 0;
        if (fragment) glDeleteShader(fragment);
        if (variant == 0) colorProgram = program;
        else if (program) glDeleteProgram(program);
    }
    // Isolate the reported location-1 failure from fetch and actual dual-source
    // output use. The only diagnostic mutation is the extension directive.
    PSSelector mrt{};mrt.tfx=4;mrt.iip=true;mrt.no_color1=true;mrt.depth_feedback=true;
    device.m_features.depth_feedback=DepthFeedbackSupport::DepthAsRT;
    for (bool blendExtension : {true,false})
    for (bool fetchEnabled : {false,true})
    {
        const auto plan=fetchEnabled?device.SelectFetchPlan(mrt,true,true):GLFramebufferFetchPlan{};
        std::string text=ProbeMain(device.GetPSSource(mrt,plan),
            "\nvoid main(){o_col0=vec4(0.25);o_col1=sample_from_depth();}\n");
        if(!blendExtension)
        {
            const std::string directive="#extension GL_EXT_blend_func_extended : require\n";
            const auto at=text.find(directive);if(at!=std::string::npos)text.erase(at,directive.size());
        }
        const GLuint fs=Compile(GL_FRAGMENT_SHADER,text);const GLuint linked=Link(vertex,fs);
        std::printf("GL_MRT_EXTENSION_CONTROL blend=%u fetch=%u plan=%u linked=%u\n",
            blendExtension,fetchEnabled,plan.key,linked!=0);
        if(fs)glDeleteShader(fs);if(linked)glDeleteProgram(linked);
    }
    check(links, "GL actual TFX colour no-colour DepthAsRT direct-depth and dual-source variants compile and link");
    bool endpointCompiled=true;
    for(bool iip:{false,true})
    {
        PSSelector endpoint{};endpoint.tfx=4;endpoint.no_color1=true;endpoint.iip=iip;
        endpoint.blend_mix=1;endpoint.blend_a=0;endpoint.blend_b=2;endpoint.blend_c=0;endpoint.blend_d=2;
        // Full production main reaches ps_blend; a single output keeps this
        // compiler-only guard proof legal even without physical GLES SRC1.
        const GLuint fragment=Compile(GL_FRAGMENT_SHADER,device.GetPSSource(endpoint,GLFramebufferFetchPlan{}));
        endpointCompiled&=fragment!=0;if(fragment)glDeleteShader(fragment);
    }
    check(endpointCompiled,"GL full production MIX1 exact-endpoint fragment variants compile with flat and interpolated color without unsupported dual GPU submission");

    const ScopedGuard deleteColor([&]() { if (colorProgram) glDeleteProgram(colorProgram); });
    if (!colorProgram) return;

    GLuint textures[2]{}, fbo = 0, vao = 0;
    glGenTextures(2, textures);
    glGenFramebuffers(1, &fbo);
    glGenVertexArrays(1, &vao);
    const ScopedGuard deleteImages([&]() {
        glUseProgram(0); glBindFramebuffer(GL_FRAMEBUFFER, 0); glBindVertexArray(0);
        glDeleteVertexArrays(1, &vao); glDeleteFramebuffers(1, &fbo); glDeleteTextures(2, textures);
    });
    std::array<u8, 4 * 4 * 4> pixels{};
    for (u32 i = 0; i < pixels.size(); i += 4)
    { pixels[i] = 8; pixels[i + 1] = 16; pixels[i + 2] = 32; pixels[i + 3] = 64; }
    for (GLuint texture : textures)
    {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    }
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, textures[1]); // valid sampler fallback; never sample attachment0 directly
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[0], 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
        "GL fetch probe has exactly one complete UNORM attachment0");
    glViewport(0, 0, 4, 4);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_DITHER);
    glBindVertexArray(vao);
    glUseProgram(colorProgram);
    if(device.m_features.framebuffer_fetch_orders_overlap)
        glDrawArrays(GL_TRIANGLES,0,9);
    else
        for(GLint primitive=0;primitive<3;primitive++)glDrawArrays(GL_TRIANGLES,primitive*3,3);
    // Conservative EXT follows the selected production ordering policy; only
    // ordered ARM is entitled to overlap three primitives in one submission.
    glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    bool exact = physicalARM || physicalEXT;
    for (u32 i = 0; i < pixels.size(); i += 4)
        exact &= pixels[i] == 11 && pixels[i + 1] == 16 && pixels[i + 2] == 32 && pixels[i + 3] == 64;
    const GLenum overlapError=glGetError();
    if(!exact||overlapError)std::fprintf(stderr,"GL_FETCH_OVERLAP rgba=%u/%u/%u/%u ordered=%u error=%x\n",
        pixels[0],pixels[1],pixels[2],pixels[3],device.m_features.framebuffer_fetch_orders_overlap,overlapError);
    check(exact && overlapError == GL_NO_ERROR, "GL actual TFX sample helper reads prior UNORM pixels under selected overlap ordering");
    #include "GLLegacyDepthTests.inl"
    device.TestFramebufferFetchPlanProof(check);
    device.TestFramebufferFetchPixelProof(check);
    device.TestFramebufferFetchPlannerProof(check);
    GLint previous_uniform_buffer = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &previous_uniform_buffer);
    {
        auto stream = GLStreamBuffer::Create(GL_UNIFORM_BUFFER, 4096);
        bool visible = stream != nullptr;
        if (stream)
        {
            stream->Bind();
            GLint access = 0;
            glGetBufferParameteriv(GL_UNIFORM_BUFFER, GL_BUFFER_ACCESS_FLAGS, &access);
            visible = !(access & GL_MAP_PERSISTENT_BIT) || (access & GL_MAP_COHERENT_BIT);
            if (!visible)
                std::fprintf(stderr, "GL_STREAM_MAPPING access=0x%x: persistent writes lack coherent visibility\n", access);
        }
        check(visible, "GL persistent stream mappings make CPU writes visible without explicit flushes");
    }
    glBindBuffer(GL_UNIFORM_BUFFER, static_cast<GLuint>(previous_uniform_buffer));
}
