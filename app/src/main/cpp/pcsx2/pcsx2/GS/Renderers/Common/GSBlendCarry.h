// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "GS/Renderers/Common/GSDevice.h"

// Adapted from33cee24e: retain compatible mix1 state when the actual renderer
// elided (Cs-Cd)*As+Cd at As128. Local selectors and submission ownership differ.
namespace GSBlendCarry
{
    using PS=GSHWDrawConfig::PSSelector;
    constexpr u32 PackAlpha(u32 a,u32 b,u32 c,u32 d){return a|(b<<2)|(c<<4)|(d<<6);}
    constexpr u32 IdentityAlpha=PackAlpha(0,1,0,1);
    struct Donor
    {
        bool valid=false;
        PS ps{};
        GSHWDrawConfig::VSSelector vs{};
        GSHWDrawConfig::BlendState blend{};
        GSHWDrawConfig::ColorMaskSelector mask{};
        GSHWDrawConfig::DepthStencilSelector depth{};
        const void* logicalRT=nullptr;
        const void* logicalDS=nullptr;
        const GSTexture* rt=nullptr;
        const GSTexture* ds=nullptr;
        GSHWDrawConfig::Topology topology=GSHWDrawConfig::Topology::Triangle;
        u32 alpha=0;
    };
    inline PS WithoutBlendBits(PS p)
    {
        p.blend_a=p.blend_b=p.blend_c=p.blend_d=0;p.blend_hw=0;p.blend_mix=0;
        p.a_masked=p.round_inv=p.pabe=p.no_color1=p.af_in_src1=0;
        return p;
    }
    inline bool Plain(const GSHWDrawConfig& c)
    {
        const auto& p=c.ps;
        // Smooth fractional colors can round differently in fixed-function blending
        // even at exact As128. Keep their original unblended identity path.
        return !p.iip&&p.dst_fmt==0&&!p.colclip&&!p.colclip_hw&&!p.dither&&!p.fbmask&&!p.quantize_color&&!p.substitute_alpha&&!p.shuffle&&!p.tex_is_fb&&
            !p.no_color&&!p.abe&&!p.HasColorROV()&&!p.HasDepthROV()&&!p.IsFeedbackLoopRT()&&!p.IsFeedbackLoopDepth()&&
            !c.ds_as_rt&&!c.require_one_barrier&&!c.require_full_barrier&&!c.alpha_second_pass.enable&&
            !c.blend_multi_pass.enable&&!c.colormask.logic_op&&!c.date_copy&&!c.logic_op_split&&!c.line_expand&&!c.raster_order&&!c.offset_read_hits_write&&
            !c.drawlist&&!c.drawlist_bbox&&c.destination_alpha==GSHWDrawConfig::DestinationAlphaMode::Off&&
            c.tex_hazard==GSHWDrawConfig::TEX_HAZARD_NONE&&c.colclip_mode==GSHWDrawConfig::ColClipMode::NoModify&&
            !(c.tex&&(c.tex==c.rt||c.tex==c.ds||c.tex==c.ds_as_rt));
    }
    inline bool Elided(const GSHWDrawConfig& c)
    {
        const auto& p=c.ps;
        return Plain(c)&&!c.blend.enable&&!p.blend_a&&!p.blend_b&&!p.blend_c&&!p.blend_d&&!p.blend_hw&&
            !p.blend_mix&&!p.pabe&&!p.a_masked&&!p.round_inv&&!p.af_in_src1;
    }
    inline bool IsDonor(const GSHWDrawConfig& c,u32 alpha)
    {
        const auto& p=c.ps;
        return alpha==IdentityAlpha&&Plain(c)&&c.blend.enable&&p.blend_mix==1&&!p.blend_hw&&
            p.blend_a==0&&p.blend_b==2&&p.blend_c==0&&p.blend_d==2&&!p.pabe&&!p.a_masked&&!p.round_inv&&!p.af_in_src1;
    }
    inline Donor Make(const GSHWDrawConfig& c,const void* rt,const void* ds,u32 alpha)
    {
        return {true,c.ps,c.vs,c.blend,c.colormask,c.depth,rt,ds,c.rt,c.ds,c.topology,alpha};
    }
    inline bool Matches(const Donor& d,const GSHWDrawConfig& c,const void* rt,const void* ds,u32 alpha)
    {
        return d.valid&&d.alpha==alpha&&d.logicalRT==rt&&d.logicalDS==ds&&d.rt==c.rt&&d.ds==c.ds&&
            d.topology==c.topology&&d.vs.key==c.vs.key&&d.mask.key==c.colormask.key&&d.depth.key==c.depth.key&&
            WithoutBlendBits(d.ps)==WithoutBlendBits(c.ps);
    }
    inline void Adopt(const Donor& d,GSHWDrawConfig& c)
    {
        auto& p=c.ps;
        p.blend_a=d.ps.blend_a;p.blend_b=d.ps.blend_b;p.blend_c=d.ps.blend_c;p.blend_d=d.ps.blend_d;
        p.blend_hw=d.ps.blend_hw;p.a_masked=d.ps.a_masked;p.blend_mix=d.ps.blend_mix;
        p.round_inv=d.ps.round_inv;p.pabe=d.ps.pabe;p.no_color1=d.ps.no_color1;p.af_in_src1=d.ps.af_in_src1;
        c.blend=d.blend;
    }
}
