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
    static GLContext* activeContext = nullptr;
    static bool syntheticArmMrtQuery = false;

    const GLubyte* GLAPIENTRY GetString(GLenum name)
    {
        if (vendor && name == GL_VENDOR) return reinterpret_cast<const GLubyte*>(vendor);
        if (renderer && name == GL_RENDERER) return reinterpret_cast<const GLubyte*>(renderer);
        return realGetString(name);
    }

    void GLAPIENTRY GetBooleanv(GLenum name, GLboolean* value)
    {
        if (syntheticArmMrtQuery && name == GL_FRAGMENT_SHADER_FRAMEBUFFER_FETCH_MRT_ARM)
        {
            *value = GL_FALSE;
            return;
        }
        realGetBooleanv(name, value);
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

    // Common full-screen vertex source for the physical TFX fragment probes.
    // Keep the varying block identical to tfx_vgs.glsl so strict GLES linkers
    // see the same cross-stage contract used by production programs.
    std::string FullscreenVertexSource(GSDeviceOGL& device, bool interpolatedColor = false)
    {
        return device.GenGlslHeader("main", GL_VERTEX_SHADER,
            interpolatedColor ? "#define VS_IIP 1\n" : "#define VS_IIP 0\n") + R"(
out SHADER
{
    vec4 t_float;
    vec4 t_int;
#if VS_IIP != 0
    vec4 c;
#else
    flat vec4 c;
#endif
    float inv_cov;
    flat uint interior;
} VSout;
void main(){
    int i=gl_VertexID%3;
    vec2 p=i==0?vec2(-1.,-1.):i==1?vec2(3.,-1.):vec2(-1.,3.);
    gl_Position=vec4(p,0.,1.);
    VSout.t_float=vec4(0.0);
    VSout.t_int=vec4(0.0);
    VSout.c=vec4(1.0);
    VSout.inv_cov=0.0;
    VSout.interior=1u;
}
)";
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
    const bool physicalARM = GLAD_GL_ARM_shader_framebuffer_fetch;
    const bool physicalEXT = GLAD_GL_EXT_shader_framebuffer_fetch;
    const auto savedConfig = GSConfig;
    const auto savedResources = EmuFolders::Resources;
    const ScopedGuard restore([&]() {
        vendor = renderer = nullptr;
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

    bool featureAgreement = true, directives = true, ordering = true, veto = true;
    bool structuralNoGLErrors = true;
    u32 structuralCases = 0;
    struct Identity { const char* vendor; const char* renderer; bool mobile; };
    constexpr Identity identities[] = {{"ARM", "Mali-G57", true},
        {"Imagination Technologies", "PowerVR Rogue", true},
        {"Qualcomm", "Adreno (TM) 740", false}, {"Mesa", "llvmpipe", false}};
    // The synthetic ARM extension identity is used only to exercise selector
    // logic. On this physical driver the ARM-only query enum is unsupported;
    // intercept that one synthetic query and delegate every physical query.
    syntheticArmMrtQuery = true;
    glad_glGetBooleanv = GetBooleanv;
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
        const GLenum beforeCheckFeatures = glGetError();
        const bool initialized = device.CheckFeatures();
        const GLenum afterCheckFeatures = glGetError();
        structuralNoGLErrors &= beforeCheckFeatures == GL_NO_ERROR && afterCheckFeatures == GL_NO_ERROR;
        if (beforeCheckFeatures != GL_NO_ERROR || afterCheckFeatures != GL_NO_ERROR)
            std::fprintf(stderr, "GL_FETCH_STRUCTURAL_ERROR arm=%u ext=%u disabled=%u depth=%u before=%x after=%x\n",
                unsigned(arm), unsigned(ext), unsigned(disabled), unsigned(depth), beforeCheckFeatures, afterCheckFeatures);
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
    syntheticArmMrtQuery = false;
    glad_glGetBooleanv = realGetBooleanv;
    check(featureAgreement, "GL runtime ARM-only dual-extension and user-veto decisions match selected backend policy");
    check(directives, "GL shader directives enable the selected ARM or EXT backend");
    check(ordering, "GL selected ARM publishes overlap ordering without promoting other backends");
    check(veto, "GL disabled or absent fetch never resurrects in the shader header");
    check(structuralNoGLErrors, "GL synthetic selector matrix issues no unsupported physical capability queries");
    std::printf("GL_FETCH_STRUCTURAL cases=%u physicalARM=%u physicalEXT=%u\n", structuralCases, physicalARM, physicalEXT);

    // Physical driver only from here: no synthetic extension is compiled or
    // submitted, and unsupported MRT/float configurations are not GPU-run.
    vendor = renderer = nullptr;
    glad_glGetString = realGetString;
    GLAD_GL_ARM_shader_framebuffer_fetch = physicalARM;
    GLAD_GL_EXT_shader_framebuffer_fetch = physicalEXT;
    GSConfig = savedConfig;
    GSConfig.DisableFramebufferFetch = false;
    device.m_features = {};
    GLenum pixelErrors = GL_NO_ERROR;
    const auto capturePixelErrors = [&](const char* stage) {
        for (u32 i = 0; i < 16; ++i)
        {
            const GLenum error = glGetError();
            if (error == GL_NO_ERROR)
                break;
            pixelErrors |= error;
            std::fprintf(stderr, "GL_FETCH_PIXEL_ERROR stage=%s error=%x\n", stage, error);
        }
    };
    capturePixelErrors("before-physical-check-features");
    if (!device.CheckFeatures()) { check(false, "GL fetch physical feature initialization"); return; }
    capturePixelErrors("physical-check-features");
    const std::string vertexSource = FullscreenVertexSource(device, true);
    GLuint vertex = Compile(GL_VERTEX_SHADER, vertexSource);
    const ScopedGuard deleteVertex([&]() { if (vertex) glDeleteShader(vertex); });
    bool links = vertex != 0;
    GLuint colorProgram = 0;
    capturePixelErrors("physical-vertex-compile");
    GLint maxDrawBuffers = 0;
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
    capturePixelErrors("max-draw-buffers-query");
    const bool physicalDualSource = GLAD_GL_EXT_blend_func_extended || GLAD_GL_ARB_blend_func_extended;
    GLint maxDualSourceDrawBuffers = -1;
    if (physicalDualSource)
        glGetIntegerv(GL_MAX_DUAL_SOURCE_DRAW_BUFFERS, &maxDualSourceDrawBuffers);
    capturePixelErrors("max-dual-source-draw-buffers-query");
    std::printf("GL_FETCH_PHYSICAL version=%s vendor=%s renderer=%s arm=%u ext=%u draw_buffers=%d dual_source_ext=%u dual_source_draw_buffers=%d selected_backend=%u selected_ext=%u selected_arm_depth=%u selected_arm_mrt=%u selected_adreno=%u selected_dual_source=%u\n",
        reinterpret_cast<const char*>(glGetString(GL_VERSION)), reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
        reinterpret_cast<const char*>(glGetString(GL_RENDERER)), unsigned(physicalARM), unsigned(physicalEXT),
        maxDrawBuffers, unsigned(physicalDualSource), maxDualSourceDrawBuffers,
        unsigned(device.m_fetch_caps.preferred), unsigned(device.m_fetch_caps.ext), unsigned(device.m_fetch_caps.arm_depth),
        unsigned(device.m_fetch_caps.arm_mrt), unsigned(device.m_fetch_caps.adreno), unsigned(device.m_features.dual_source_blend));
    static constexpr const char* variantNames[] = {"color", "no-color", "depth-as-rt", "direct-depth", "dual-source"};
    GLFramebufferFetchPlan sampleFetchPlan{};
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
        const GLFramebufferFetchPlan fetchPlan = device.SelectFetchPlan(selector, true,
            device.m_features.depth_feedback == DepthFeedbackSupport::DepthAsRT && selector.IsFeedbackLoopDepth());
        if (variant == 0)
            sampleFetchPlan = fetchPlan;
        const char* probe = variant == 1 ? "\nvoid main(){gl_FragDepth=0.5;}\n" :
            "\nvoid main(){o_col0=sample_from_rt()+vec4(1.0/255.0,0.,0.,0.);}\n";
        std::printf("GL_FETCH_LINK_CASE begin id=%u name=%s depth_feedback=%u depth_mode=%u no_color=%u no_color1=%u dual_source_supported=%u fetch_plan=%u\n",
            variant, variantNames[variant], unsigned(selector.depth_feedback), unsigned(device.m_features.depth_feedback),
            unsigned(selector.no_color), unsigned(selector.no_color1), unsigned(device.m_features.dual_source_blend), fetchPlan.key);
        std::string fragmentSource = ProbeMain(device.GetPSSource(selector, fetchPlan), probe);
        const GLuint fragment = fragmentSource.empty() ? 0 : Compile(GL_FRAGMENT_SHADER, fragmentSource);
        const GLuint program = Link(vertex, fragment);
        links &= program != 0;
        if (!program)
            std::fprintf(stderr, "GL_FETCH_LINK_CASE failed id=%u name=%s\n", variant, variantNames[variant]);
        if (fragment) glDeleteShader(fragment);

        // Causal-only probes: retain the actual DepthAsRT selector and all
        // generated macros, but remove one extension directive at a time from
        // a duplicate fragment. These results are diagnostic only and never
        // affect the strict production-variant link assertion above.
        if (variant == 2 && !program)
        {
            constexpr const char* directives[] = {
                "#extension GL_EXT_blend_func_extended : require",
                "#extension GL_EXT_shader_framebuffer_fetch : require",
            };
            constexpr const char* extensionNames[] = {"GL_EXT_blend_func_extended", "GL_EXT_shader_framebuffer_fetch"};
            for (u32 probeIndex = 0; probeIndex < 2; ++probeIndex)
            {
                std::string isolatedSource = fragmentSource;
                const size_t directiveOffset = isolatedSource.find(directives[probeIndex]);
                if (directiveOffset == std::string::npos)
                {
                    std::fprintf(stderr, "GL_FETCH_CAUSAL_PROBE extension=%s directive=absent result=not-run\n",
                        extensionNames[probeIndex]);
                    continue;
                }
                isolatedSource.erase(directiveOffset, std::string(directives[probeIndex]).size());
                const GLuint isolatedFragment = Compile(GL_FRAGMENT_SHADER, isolatedSource);
                const GLuint isolatedProgram = Link(vertex, isolatedFragment);
                std::fprintf(stderr, "GL_FETCH_CAUSAL_PROBE extension=%s removed=1 compile=%u link=%u excluded_from_strict_result=1\n",
                    extensionNames[probeIndex], unsigned(isolatedFragment != 0), unsigned(isolatedProgram != 0));
                if (isolatedFragment) glDeleteShader(isolatedFragment);
                if (isolatedProgram) glDeleteProgram(isolatedProgram);
            }
        }
        if (variant == 0) colorProgram = program;
        else if (program) glDeleteProgram(program);
    }
    bool singleOutputHeaderPolicy = true;
    if (device.m_is_gles && device.m_fetch_caps.adreno && physicalDualSource)
    {
        constexpr std::string_view dualSourceDirective = "#extension GL_EXT_blend_func_extended : require";
        constexpr std::string_view framebufferFetchDirective = "#extension GL_EXT_shader_framebuffer_fetch : require";
        PSSelector singleOutput{};
        singleOutput.no_color1 = true;
        PSSelector dualOutput{};
        dualOutput.no_color1 = false;
        const std::string singleSource = device.GetPSSource(singleOutput, sampleFetchPlan);
        const std::string dualSource = device.GetPSSource(dualOutput, sampleFetchPlan);
        const std::string defaultHeader = device.GenGlslHeader("ps_main", GL_FRAGMENT_SHADER, "", sampleFetchPlan);
        singleOutputHeaderPolicy = singleSource.find(dualSourceDirective) == std::string::npos &&
            singleSource.find("#define DISABLE_DUAL_SOURCE\n") != std::string::npos &&
            singleSource.find(framebufferFetchDirective) != std::string::npos &&
            dualSource.find(dualSourceDirective) != std::string::npos &&
            dualSource.find("#define DISABLE_DUAL_SOURCE\n") == std::string::npos &&
            defaultHeader.find(dualSourceDirective) != std::string::npos;
    }
    else
    {
        std::printf("GL_FETCH_SINGLE_OUTPUT_HEADER_POLICY not_applicable gles=%u adreno=%u physical_dual_source=%u\n",
            unsigned(device.m_is_gles), unsigned(device.m_fetch_caps.adreno), unsigned(physicalDualSource));
    }
    check(singleOutputHeaderPolicy,
        "Adreno single-output TFX omits only dual-source GLSL extension while retaining framebuffer fetch and dual-output support");
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
    capturePixelErrors("shader-compile-link");

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
    capturePixelErrors("texture-and-fbo-setup");
    std::array<u8, 4> initialPixel{};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, initialPixel.data());
    capturePixelErrors("initial-read");
    glViewport(0, 0, 4, 4);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_DITHER);
    glBindVertexArray(vao);
    glUseProgram(colorProgram);
    capturePixelErrors("pre-draw-state");
    glDrawArrays(GL_TRIANGLES, 0, 9); // three overlapping primitives, one real driver draw
    capturePixelErrors("draw");
    glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    capturePixelErrors("final-read");
    bool exact = physicalARM || physicalEXT;
    for (u32 i = 0; i < pixels.size(); i += 4)
        exact &= pixels[i] == 11 && pixels[i + 1] == 16 && pixels[i + 2] == 32 && pixels[i + 3] == 64;
    const GLenum pixelError = pixelErrors;
    if (!exact || pixelError != GL_NO_ERROR)
    {
        const size_t center = (2 * 4 + 2) * 4;
        std::fprintf(stderr,
            "GL_FETCH_PIXEL_DIFF fetch_plan=%u backend=%u initial=%u,%u,%u,%u center=%u,%u,%u,%u expected_center=11,16,32,64 error=%x\n",
            sampleFetchPlan.key, unsigned(sampleFetchPlan.Color()), initialPixel[0], initialPixel[1], initialPixel[2], initialPixel[3],
            pixels[center], pixels[center + 1], pixels[center + 2], pixels[center + 3], pixelError);
    }
    check(exact && pixelError == GL_NO_ERROR, "GL actual TFX sample helper reads the prior tile pixel for overlapping UNORM primitives");
    std::fprintf(stderr, "GL_FETCH_PHASE plan-tests begin\n");
    device.TestFramebufferFetchPlanProof(check);
    std::fprintf(stderr, "GL_FETCH_PHASE plan-tests done\nGL_FETCH_PHASE pixel-tests begin\n");
    device.TestFramebufferFetchPixelProof(check);
    std::fprintf(stderr, "GL_FETCH_PHASE pixel-tests done\nGL_FETCH_PHASE planner-tests begin\n");
    device.TestFramebufferFetchPlannerProof(check);
    std::fprintf(stderr, "GL_FETCH_PHASE planner-tests done\n");
}
