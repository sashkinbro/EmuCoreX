// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Actual HW-consumed planner в†’ actual OGL bindings в†’ actual SendHWDraw.
// Synthetic native-barrier availability is CPU control-flow evidence ONLY:
// both draw and barrier entry points are intercepted without forwarding.
namespace GLFetchPlannerOracle
{
    inline u32 draws=0,barriers=0;
    void GLAPIENTRY Draw(GLenum,GLsizei,GLenum,const void*,GLint){++draws;}
    void GLAPIENTRY Barrier(){++barriers;}
}
void GSDeviceOGL::TestFramebufferFetchPlannerProof(void (*check)(bool,const char*))
{
    if(g_gs_device){check(false,"GL planner proof starts without another device");return;}
    g_gs_device=std::make_unique<GSDeviceOGL>();
    auto& target=*static_cast<GSDeviceOGL*>(g_gs_device.get());
    const ScopedGuard destroy([&](){g_gs_device.reset();});
    target.m_is_gles=true;target.m_features=m_features;target.m_fetch_caps=m_fetch_caps;
    target.m_index.start=target.m_vertex.start=0;target.m_index.count=9;target.m_draw_topology=GL_TRIANGLES;
    GSTextureOGL rt(GSTexture::RenderTarget,4,4,1,GSTexture::Format::Color);
    GSTextureOGL depth(GSTexture::RenderTarget,4,4,1,GSTexture::Format::DepthColor);
    const auto draw=glad_glDrawElementsBaseVertex;
    const auto barrier=glad_glTextureBarrier;
    glad_glDrawElementsBaseVertex=GLFetchPlannerOracle::Draw;
    glad_glTextureBarrier=GLFetchPlannerOracle::Barrier;
    const ScopedGuard restore([&](){glad_glDrawElementsBaseVertex=draw;glad_glTextureBarrier=barrier;});
    const std::vector<size_t> groups={1,1,1};
    const std::vector<GSVector4i> boxes(3,GSVector4i(0,0,4,4));
    bool planned=true,bound=true,ordered=true,coherent=true,alpha=true;
    for(u32 variant=0;variant<6;++variant)
    {
        target.m_features=m_features;
        target.m_features.texture_barrier=variant!=4; // structural only; no corresponding GPU call is forwarded
        target.m_features.framebuffer_fetch=true;
        target.m_features.framebuffer_fetch_orders_overlap=variant!=2&&variant!=4;
        target.m_features.depth_feedback=DepthFeedbackSupport::DepthAsRT;
        target.m_fetch_caps=m_fetch_caps;
        target.m_fetch_caps.preferred=(variant==2||variant==4)?GSFramebufferFetchBackend::EXT:GSFramebufferFetchBackend::ARM;
        target.m_fetch_caps.arm_mrt=false;
        GSHWDrawConfig config{};
        config.rt=&rt;config.ds_as_rt=(variant==1||variant==5)?&depth:nullptr;
        config.ps.tfx=4;config.ps.no_color1=variant!=1;
        config.ps.color_feedback=true;config.ps.depth_feedback=variant==1;
        if(variant==5)
        {
            config.ps.color_feedback=false;
            config.alpha_second_pass.enable=true;
            config.alpha_second_pass.ps=config.ps;
            config.alpha_second_pass.ps.depth_feedback=true;
            config.alpha_second_pass.ps.no_color1=false;
        }
        config.nindices=9;config.indices_per_prim=3;config.drawlist=&groups;
        config.drawlist_bbox=&boxes;
        config.drawarea=config.samplearea=GSVector4i(0,0,4,4);
        config.require_full_barrier=true;
        // This is the same mutating function called by actual DrawPrims.
        GSApplyPlannedFeedbackBarriers(config,target.m_features,RenderAPI::OpenGL,true,false,false);
        if(variant!=3&&variant!=5)planned&=config.require_full_barrier;
        if(variant==5)config.alpha_second_pass.require_full_barrier=config.require_full_barrier;
        const bool unorm=variant!=0; // HDR/float attachment0 cannot use ARM colour fetch.
        const auto& ps=variant==5?config.alpha_second_pass.ps:config.ps;
        const auto plan=target.SelectFetchPlan(ps,unorm,config.ds_as_rt!=nullptr);
        glBindTextureUnit(2,0);GLState::tex_unit[2]=0;
        glBindTextureUnit(4,0);GLState::tex_unit[4]=0;
        // The same binding method is called by actual DoRenderHW, before Send.
        target.BindFeedbackTextures(config,nullptr);
        if(variant<2)
        {
            GLint texture=0;glActiveTexture(GL_TEXTURE2);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
            bound&=texture==static_cast<GLint>(rt.GetID());
        }
        if(variant==5)
        {
            GLint texture=0;glActiveTexture(GL_TEXTURE4);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
            alpha&=texture==static_cast<GLint>(depth.GetID());
        }
        GLFetchPlannerOracle::draws=GLFetchPlannerOracle::barriers=0;
        target.SendHWDraw(config,nullptr,&rt,nullptr,config.ds_as_rt,false,
            variant==5?config.alpha_second_pass.require_full_barrier:config.require_full_barrier,ps,plan);
        const bool result=GLFetchPlannerOracle::draws==(variant==3?1u:3u)&&
            GLFetchPlannerOracle::barriers==((variant==3||variant==4)?0u:3u);
        if(variant==3)coherent&=result;else if(variant==5)alpha&=result;else ordered&=result;
        if(!result)std::fprintf(stderr,"GL_PLANNER_ORDER variant=%u plan=%u flags=%u draws=%u barriers=%u\n",
            variant,plan.key,config.require_full_barrier,GLFetchPlannerOracle::draws,GLFetchPlannerOracle::barriers);
    }
    check(planned,"GL actual HW planner preserves barriers for per-draw ARM exclusions and conservative EXT");
    check(bound,"GL actual planner-to-device native sampler fallback binds current RT before submission");
    check(ordered,"GL actual Send CPU native-barrier route orders excluded ARM and conservative EXT primitives");
    check(coherent,"GL actual eligible ordered ARM Send avoids redundant native barriers and splits");
    check(alpha,"GL actual planner-to-device alpha-only native depth binding and primitive ordering remain complete");
}
