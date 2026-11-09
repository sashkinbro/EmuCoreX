// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Append inside TestExactAlphaMask, after its initial consumer matrix.
{
    struct HeldCase{const char* name;bool sub,barrier,clip,stands;};
    constexpr HeldCase cases[]={
        {"substitution-stands",true,false,false,true},
        {"substitution-restored-for-blend-read",true,true,false,false},
        {"substitution-restored-for-colclip",true,false,true,false},
        {"identity-drop-stands-with-colclip",false,false,true,true},
        {"identity-drop-restored-for-blend-read",false,true,false,false}};
    for(const auto& c:cases)
    {
        r.m_conf={};r.ResetStates();
        r.m_exact_alpha_drop_fbmask_a=128;r.m_held_alpha_mask={};
        r.m_held_alpha_mask.fbmask=0x80000000u;r.m_held_alpha_mask.ps_fbmask=8;
        r.m_held_alpha_mask.substitute=c.sub;r.m_held_alpha_mask.substitution={~128u,0};
        r.m_conf.ps.quantize_color=true;r.m_conf.ps.colclip_hw=c.clip;
        r.m_conf.require_one_barrier=c.barrier;
        r.m_conf.cb_ps.SubstituteAlphaKeep=0x12345678u;r.m_conf.cb_ps.SubstituteAlphaValue=0x90abcdefu;
        r.m_conf.alpha_second_pass.enable=true;
        r.ResolveHeldAlphaMask();
        bool exact=r.m_held_alpha_mask.fbmask==0&&r.RequestedAlphaFbMask()==128&&
            r.m_conf.ps.fbmask==!c.stands&&r.m_conf.ps.quantize_color==c.stands&&
            r.m_conf.ps.substitute_alpha==(c.stands&&c.sub)&&r.m_conf.require_one_barrier==!c.stands&&
            r.m_conf.alpha_second_pass.ps.fbmask==r.m_conf.ps.fbmask&&
            r.m_conf.alpha_second_pass.ps.quantize_color==r.m_conf.ps.quantize_color&&
            r.m_conf.alpha_second_pass.ps.substitute_alpha==r.m_conf.ps.substitute_alpha;
        if(c.stands&&c.sub)exact&=r.m_conf.cb_ps.SubstituteAlphaKeep==~128u&&r.m_conf.cb_ps.SubstituteAlphaValue==0;
        else exact&=r.m_conf.cb_ps.SubstituteAlphaKeep==0x12345678u&&r.m_conf.cb_ps.SubstituteAlphaValue==0x90abcdefu;
        if(!c.stands)exact&=r.m_conf.cb_ps.FbMask.a==128;
        check(exact,c.name);
    }
}
{
    struct CoverCase{const char* name;bool tiling,united,date,fba;u8 lo,hi,bits,value;bool via_union;};
    constexpr CoverCase cases[]={
        {"known-full-tiling",true,false,false,false,64,64,255,64,false},
        {"known-full-union",false,true,false,false,64,64,255,64,true},
        {"known-partial-coverage",false,false,false,false,64,64,191,0,false},
        {"known-date-rejects-full-cover",true,false,true,false,64,64,191,0,false},
        {"known-FBA-straddling-source",true,false,false,true,64,192,128,0,false}};
    for(const auto& c:cases)
    {
        r.m_conf={};r.ResetStates();r.m_env={};r.PRIM=&r.m_env.PRIM;r.m_draw_env=&r.m_env;r.m_context=&r.m_env.CTXT[0];
        r.m_cached_ctx={};r.m_cached_ctx.FRAME.PSM=PSMCT32;r.m_context->FBA.FBA=c.fba;
        r.m_texture_shuffle=false;r.m_channel_shuffle=false;r.m_prim_overlap=PRIM_OVERLAP_NO;
        r.m_vt.m_alpha.min=c.lo;r.m_vt.m_alpha.max=c.hi;r.m_vt.m_alpha.valid=true;
        r.m_r=GSVector4i(0,0,64,64);r.m_primitive_covers_without_gaps=c.tiling?NoGapsType::FullCover:NoGapsType::GapsFound;
        r.m_primitive_union_covers_rect=c.united;
        r.m_conf.colormask.wa=true;r.m_conf.ps.fbmask=true;r.m_conf.cb_ps.FbMask.a=128;
        rt.m_TEX0.PSM=PSMCT32;rt.m_32_bits_fmt=true;rt.m_rt_alpha_scale=false;
        rt.m_alpha_min=rt.m_alpha_max=0;rt.m_alpha_known={255,0};rt.m_alpha_known_via_union=false;
        DATEOptions date{};date.enabled=c.date;int blendLo=0,blendHi=0,newLo=0,newHi=0;
        GSAlphaKnownBits::Known known;bool viaUnion=false;
        r.CalculateAlphaRange(&rt,nullptr,date,blendLo,blendHi,newLo,newHi,known,viaUnion);
        const bool exact=known.bits==c.bits&&known.value==c.value&&viaUnion==c.via_union;
        if(!exact)std::fprintf(stderr,"GS_ALPHA_KNOWN_DRAW case=%s known=%02x/%02x viaUnion=%u expected=%02x/%02x/%u\n",
            c.name,known.bits,known.value,viaUnion,c.bits,c.value,c.via_union);
        check(exact,c.name);
    }
}
{
    // ClearRenderTarget is the real deferred CPU state setter; no native API or
    // fake GPU allocation is involved. Borrowed texture is detached before free.
    CoherenceTexture color(GSTexture::RenderTarget,64,64,1,GSTexture::Format::Color);
    rt.m_texture=&color;const ScopedGuard detach([&](){rt.m_texture=nullptr;});
    r.m_conf={};r.ResetStates();r.m_env={};r.PRIM=&r.m_env.PRIM;r.m_draw_env=&r.m_env;r.m_context=&r.m_env.CTXT[0];
    r.m_cached_ctx={};r.m_cached_ctx.FRAME.PSM=PSMCT32;r.m_vt.m_primclass=GS_SPRITE_CLASS;
    r.m_vt.m_eq.rgba=0xffff;r.m_vt.m_eq.z=1;r.m_r=GSVector4i(0,0,64,64);
    r.m_index.buff[1]=1;r.m_vertex.buff[1].RGBAQ.U32[0]=0x40112233u;
    rt.m_TEX0.PSM=PSMCT32;rt.m_32_bits_fmt=true;rt.m_rt_alpha_scale=false;
    rt.m_alpha_min=0;rt.m_alpha_max=255;rt.m_alpha_known={255,255};rt.m_alpha_known_via_union=true;
    const bool cleared=r.TryTargetClear(&rt,nullptr,false,true);
    check(cleared&&rt.m_alpha_min==64&&rt.m_alpha_max==64&&rt.m_alpha_known==GSAlphaKnownBits::Known::All(64)&&
        !rt.m_alpha_known_via_union&&color.GetState()==GSTexture::State::Cleared,
        "GS actual target clear replaces exact alpha knowledge and clears union provenance");
}
{
    [&]<class Mode>() {
        if constexpr(requires { Mode::SPLIT_RGB_ONLY; })
        {
            const auto saved_features = g_gs_device->Features();
            const ScopedGuard restore_features([&]() { g_gs_device->SetFeaturesForTesting(saved_features); });
            // An uncreated device defaults to dual-source support. Explicitly
            // select both capabilities; no_color1 is only a shader selector.
            for (bool dual_source : {false, true})
            {
                auto features = saved_features;
                features.dual_source_blend = dual_source;
                g_gs_device->SetFeaturesForTesting(features);
                for (bool alpha : {false, true}) for (bool depth : {false, true})
                {
                    r.m_conf={};r.ResetStates();r.m_env.Reset();r.PRIM=&r.m_env.PRIM;
                    r.m_draw_env=&r.m_env;r.m_context=&r.m_env.CTXT[0];r.m_cached_ctx={};
                    r.m_cached_ctx.TEST.ATE=1;r.m_cached_ctx.TEST.ATST=ATST_GREATER;
                    r.m_cached_ctx.TEST.AREF=96;r.m_cached_ctx.TEST.AFAIL=AFAIL_RGB_ONLY;
                    r.m_cached_ctx.TEST.ZTE=1;r.m_cached_ctx.TEST.ZTST=ZTST_ALWAYS;
                    r.m_cached_ctx.ZBUF.ZMSK=!depth;r.m_context->ALPHA.C=0;
                    r.m_conf.colormask.wrgba=alpha?15:7;r.m_conf.depth.zwe=depth;
                    r.m_conf.ps.tfx=4;r.m_conf.ps.no_color1=true;r.m_prim_overlap=PRIM_OVERLAP_YES;
                    GSConfig.HWAccurateAlphaTest=false;DATEOptions options{};
                    r.EmulateAlphaTest(options);
                    const auto expected = alpha ? (dual_source ? Mode::SIMPLE_RGB_ONLY : Mode::SPLIT_RGB_ONLY) :
                        (depth ? Mode::SIMPLE_FB_ONLY : Mode::NONE);
                    bool exact = r.m_conf.alpha_test == expected;
                    r.EmulateAlphaTestSecondPass();
                    const bool dual_alpha = alpha && dual_source;
                    const bool split_alpha = alpha && !dual_source;
                    const bool second_pass = split_alpha || depth;
                    exact &= r.m_conf.colormask.wrgba == (dual_alpha ? 15 : 7) && !r.m_conf.depth.zwe;
                    exact &= r.m_conf.ps.atst == (dual_alpha ? PS_ATST::GEQUAL : PS_ATST::NONE);
                    exact &= r.m_conf.ps.no_color1 == !dual_alpha;
                    exact &= r.m_conf.alpha_second_pass.enable == second_pass;
                    if (second_pass)
                        exact &= r.m_conf.alpha_second_pass.colormask.wrgba == (split_alpha ? 8 : 0) &&
                            r.m_conf.alpha_second_pass.depth.zwe == depth &&
                            r.m_conf.alpha_second_pass.ps.atst == PS_ATST::GEQUAL &&
                            r.m_conf.alpha_second_pass.ps.afail == PS_AFAIL::KEEP;
                    char label[160];
                    std::snprintf(label, sizeof(label),
                        "GS actual RGB_ONLY capability dual=%u alpha=%u depth=%u preserves channel/pass contract",
                        unsigned(dual_source), unsigned(alpha), unsigned(depth));
                    if (!exact)
                        std::fprintf(stderr,
                            "GS_RGB_ONLY dual=%u alpha=%u depth=%u mode=%u expected=%u first=%u/%u/%u/color1off%u second=%u/%u/%u/%u\n",
                            unsigned(dual_source), unsigned(alpha), unsigned(depth), unsigned(r.m_conf.alpha_test), unsigned(expected),
                            unsigned(r.m_conf.colormask.wrgba), unsigned(r.m_conf.depth.zwe), unsigned(r.m_conf.ps.atst),
                            unsigned(r.m_conf.ps.no_color1), unsigned(r.m_conf.alpha_second_pass.enable),
                            unsigned(r.m_conf.alpha_second_pass.colormask.wrgba), unsigned(r.m_conf.alpha_second_pass.depth.zwe),
                            unsigned(r.m_conf.alpha_second_pass.ps.atst));
                    check(exact, label);
                }
            }
        }
        else check(false,"GS channel-split alpha test exists for hardware without dual-source blending");
    }.template operator()<GSHWDrawConfig::AlphaTestMode>();
}
