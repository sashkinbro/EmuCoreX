// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
void GSDeviceVK::TestStencilCounterPixels(void (*check)(bool,const char*))
{
    if (!m_features.stencil_counter)
    {
        std::fprintf(stderr,"UNAVAILABLE VK sampled stencil counter on physical device\n");
        return;
    }
    EndRenderPass();
    const ScopedGuard clearTemporaryPool([&](){EndRenderPass();PurgePool();ExecuteCommandBuffer(true);});
    const auto oldPass=m_utility_color_render_pass_load;
    const ScopedGuard restorePass([&](){m_utility_color_render_pass_load=oldPass;});
    m_utility_color_render_pass_load=GetRenderPass(LookupNativeFormat(GSTexture::Format::Color), VK_FORMAT_UNDEFINED,
        VK_ATTACHMENT_LOAD_OP_LOAD,VK_ATTACHMENT_STORE_OP_STORE);
    m_point_sampler=GetSampler(GSHWDrawConfig::SamplerSelector::Point());
    const bool ready=CreateStencilCounterLayouts()&&m_utility_color_render_pass_load&&m_point_sampler;
    check(ready,"VK physical counter creates production resolve layouts sampled stencil bindings and render pass");
    if(!ready)return;
    auto target=GSTextureVK::Create(GSTexture::RenderTarget,GSTexture::Format::Color,4,4,1);
    check(bool(target),"VK exact counter allocates physical render target");
    if(!target)return;
    alignas(32) GSVertex v[6]{};
    for(u32 i=0;i<6;i++)
    {
        v[i].XYZ.X=i%3==1?128:0;v[i].XYZ.Y=i%3==2?128:0;
        v[i].RGBAQ.R=i<3?3:253;v[i].RGBAQ.G=i<3?7:249;
        v[i].RGBAQ.B=128;v[i].RGBAQ.A=128;v[i].RGBAQ.Q=1.f;
    }
    bool exact=true,submitted=true;u32 cases=0;
    for(u32 mask:{1u,2u,4u,7u})for(bool alpha:{false,true})for(bool wrap:{false,true})
    {
        const u32 plus=wrap?513:1,minus=wrap?256:2;
        std::vector<u16> indices;indices.reserve((plus+minus)*3);
        for(u32 p=0;p<plus+minus;p++)for(u16 i=0;i<3;i++)indices.push_back(i+(p<plus?0:3));
        std::array<u32,16> seed;seed.fill(0x613f2111u);
        if(!target->Update(target->GetRect(),seed.data(),16)){exact=false;continue;}
        InitializeState();
        GSHWDrawConfig config{};config.rt=target.get();config.verts=v;config.nverts=6;
        config.indices=indices.data();config.nindices=indices.size();config.indices_per_prim=3;
        config.topology=GSHWDrawConfig::Topology::Triangle;
        config.scissor=config.drawarea=config.samplearea=target->GetRect();
        config.depth=GSHWDrawConfig::DepthStencilSelector::NoDepth();config.ps.tfx=4;
        config.ps.no_color1=1;config.ps.colclip_hw=1;config.colormask.wrgba=mask|(alpha?8u:0u);
        config.colclip_mode=GSHWDrawConfig::ColClipMode::ConvertAndResolve;
        config.stencil_counter={true,{3,7,128},plus*3};
        config.cb_vs.vertex_scale=GSVector2(1.f/32.f,1.f/32.f);
        config.cb_vs.vertex_offset=GSVector2(1.f,1.f);config.cb_vs.max_depth=0xffffffff;
        // Call the real optimized backend, then read the physical image. No feature is forced.
        submitted&=DrawStencilCounter(config);EndRenderPass();
        auto download=GSDownloadTextureVK::Create(4,4,GSTexture::Format::Color);
        bool same=bool(download);std::array<u8,64> image{};
        if(download)
        {
            download->CopyFromTexture(target->GetRect(),target.get(),target->GetRect(),0);download->Flush();
            same&=download->Map(target->GetRect());
            if(same){for(u32 y=0;y<4;y++)std::memcpy(image.data()+y*16,download->GetMapPointer()+y*download->GetMapPitch(),16);download->Unmap();}
        }
        const u8 base[4]={17,33,63,97},step[3]={3,7,128};
        for(u32 pixel=0;pixel<16;pixel++)for(u32 c=0;c<4;c++)
        {
            const u8 expected=c==3?(alpha?128:base[c]):
                ((mask&(1u<<c))?static_cast<u8>(base[c]+step[c]*(plus-minus)):base[c]);
            same&=image[pixel*4+c]==expected;
        }
        if(!same)std::fprintf(stderr,"VK_COUNTER_PIXEL mask=%u alpha=%u wrap=%u rgba=%u/%u/%u/%u\n",mask,alpha,wrap,image[0],image[1],image[2],image[3]);
        exact&=same;cases++;
    }
    check(submitted&&cases==16,"VK physical sampled-stencil counter submits positive and negative runs including modulo wrap");
    check(exact,"VK physical counter resolve preserves masked RGB and alpha with independent exact modulo steps");
    if(m_features.fast_stencil_shadow)
    {
        bool shadow=true;
        const u16 triangle[3]={0,1,2};
        for(u8 alpha:{u8(64),u8(96),u8(128),u8(191)})for(u8 factor:{u8(127),u8(130)})
        {
            std::array<u32,16> seed;seed.fill((u32(alpha)<<24)|0x003f2111u);
            shadow&=target->Update(target->GetRect(),seed.data(),16);
            for(auto& vertex:v)vertex.RGBAQ.A=factor;
            InitializeState();
            GSHWDrawConfig config{};config.rt=target.get();config.verts=v;config.nverts=3;
            config.indices=triangle;config.nindices=config.indices_per_prim=3;
            config.topology=GSHWDrawConfig::Topology::Triangle;
            config.scissor=config.drawarea=config.samplearea=target->GetRect();
            config.depth=GSHWDrawConfig::DepthStencilSelector::NoDepth();config.ps.tfx=4;
            config.ps.stencil_counter=1;config.colormask.wrgba=8;
            config.blend=GSHWDrawConfig::BlendState(true,CONST_ZERO,CONST_ONE,OP_ADD,DST_ALPHA,SRC1_ALPHA,false,0);
            config.cb_vs.vertex_scale=GSVector2(1.f/32.f,1.f/32.f);
            config.cb_vs.vertex_offset=GSVector2(1.f,1.f);config.cb_vs.max_depth=0xffffffff;
            DoRenderHW(config);EndRenderPass();
            auto download=GSDownloadTextureVK::Create(4,4,GSTexture::Format::Color);
            bool same=bool(download);
            if(download)
            {
                download->CopyFromTexture(target->GetRect(),target.get(),target->GetRect(),0);download->Flush();
                same&=download->Map(target->GetRect());
                if(same)
                {
                    // Upstream's documented down-step exception at alpha 128 is intentional.
                    const u8 expected=alpha==128&&factor==127?126:((u32(alpha)*factor)>>7);
                    for(u32 y=0;y<4;y++)for(u32 x=0;x<4;x++)
                    {
                        const u8* pixel=download->GetMapPointer()+y*download->GetMapPitch()+x*4;
                        same&=pixel[0]==17&&pixel[1]==33&&pixel[2]==63&&pixel[3]==expected;
                    }
                    download->Unmap();
                }
            }
            if(!same)std::fprintf(stderr,"VK_SHADOW_PIXEL alpha=%u factor=%u\n",alpha,factor);
            shadow&=same;
        }
        check(shadow,"VK physical alpha-shadow blend follows upstream counter steps and retains RGB");
    }
}
