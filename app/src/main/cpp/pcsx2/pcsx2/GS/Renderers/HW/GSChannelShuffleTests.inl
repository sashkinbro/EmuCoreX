// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Calls the actual channel-shuffle renderer entry. No parallel decision policy,
// backend submission, test-only bypass, or GPU resource allocation is used.
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "common/ScopedGuard.h"
#include "cpuinfo.h"

void GSRendererHW::TestChannelShuffleGeometry(void (*check)(bool, const char*))
{
    cpuinfo_initialize();
    const bool isolated = !g_gs_renderer && !g_texture_cache && !g_gs_device;
    check(isolated, "GS channel shuffle owns isolated CPU renderer and uncreated device");
    if (!isolated)
        return;

    const auto settings = GSConfig;
    GSConfig = Pcsx2Config::GSOptions();
    GSConfig.UpscaleMultiplier = 1.0f;
    GSConfig.LoadTextureReplacements = false;
    GSConfig.DumpReplaceableTextures = false;
    g_gs_device = std::make_unique<GSDeviceVK>();
    g_gs_renderer = std::make_unique<GSRendererHW>();
    auto& r = *static_cast<GSRendererHW*>(g_gs_renderer.get());
    const ScopedGuard restore([&]() {
        g_gs_renderer.reset();
        g_gs_device.reset();
        GSConfig = settings;
    });

    class HostTexture final : public GSTexture
    {
    public:
        HostTexture()
        {
            m_size = GSVector2i(256, 256);
            m_mipmap_levels = 1;
            m_format = Format::Color;
            m_usage = RenderTarget;
        }
        void* GetNativeHandle() const override { return nullptr; }
        bool DoUpdate(const GSVector4i&, const void*, int, int) override { return false; }
        bool DoMap(GSMap&, const GSVector4i*, int) override { return false; }
        void Unmap() override {}
        void GenerateMipmap() override {}
#ifdef PCSX2_DEVBUILD
        void SetDebugName(std::string_view) override {}
#endif
    } color;

    GIFRegTEX0 source_tex0{};
    source_tex0.TBP0 = 0x800;
    source_tex0.TBW = 2;
    source_tex0.PSM = PSMCT32;
    GIFRegTEX0 target_tex0 = source_tex0;
    target_tex0.TBP0 = 0x1000;
    target_tex0.TBW = 4;
    GSTextureCache::Target src(source_tex0, GSTextureCache::RenderTarget, GSVector2i(256, 256), 1.0f, &color);
    GSTextureCache::Target rt(target_tex0, GSTextureCache::RenderTarget, GSVector2i(256, 256), 1.0f, &color);
    // These are borrowed host textures, never members of the live cache.
    const ScopedGuard detach([&]() { src.m_texture = rt.m_texture = nullptr; });

    constexpr u32 slots = 70;
    alignas(32) GSVertex original[slots]{};
    u16 indices[slots]{};
    for (u32 at = 0; at < slots; ++at)
    {
        u32 words[8];
        for (u32 lane = 0; lane < 8; ++lane)
            words[lane] = 0x3f000000u + at * 0x101u + lane * 0x113355u;
        std::memcpy(&original[at], words, sizeof(words));
        original[at].XYZ.X = static_cast<u16>(0x100 + (11 + (at % 8) * 3) * 16);
        original[at].XYZ.Y = static_cast<u16>(0x200 + (11 + (at % 8) * 5) * 16);
        original[at].U = static_cast<u16>(17 + at * 19);
        original[at].V = static_cast<u16>(29 + at * 23);
        indices[at] = static_cast<u16>(at);
    }

    struct Case
    {
        const char* name;
        u32 count;
        u32 wmt;
        bool page_copy;
        bool same_width;
        GSTextureInRtMode mode;
        bool preserve;
    };
    const Case cases[] = {
        {"inside-small-region-repeat", 6, 3, false, false, GSTextureInRtMode::InsideTargets, true},
        {"inside-boundary64-region-repeat", 64, 3, false, false, GSTextureInRtMode::InsideTargets, true},
        {"merge-small-region-repeat", 6, 3, false, false, GSTextureInRtMode::MergeTargets, true},
        {"non-region-repeat-rebuild", 6, 0, false, false, GSTextureInRtMode::InsideTargets, false},
        {"over64-region-repeat-rebuild", 66, 3, false, false, GSTextureInRtMode::InsideTargets, false},
        {"page-copy-rounded-rebuild", 6, 3, true, false, GSTextureInRtMode::InsideTargets, false},
        {"page-copy-fullscreen-rebuild", 6, 3, true, true, GSTextureInRtMode::InsideTargets, false},
        {"disabled-fullscreen-rebuild", 6, 0, false, false, GSTextureInRtMode::Disabled, false},
    };

    for (const Case& c : cases)
    {
        r.ResetStates();
        std::memset(static_cast<void*>(&r.m_conf), 0, sizeof(r.m_conf));
        r.PRIM = &r.m_env.PRIM;
        r.m_env.PRIM.PRIM = GS_SPRITE;
        r.m_env.PRIM.TME = c.page_copy;
        r.m_env.PRIM.FST = true;
        r.m_context = &r.m_env.CTXT[0];
        r.m_context->XYOFFSET.OFX = 0x100;
        r.m_context->XYOFFSET.OFY = 0x200;
        r.m_context->FRAME.U64 = 0;
        r.m_context->FRAME.FBP = target_tex0.TBP0 >> 5;
        r.m_context->FRAME.FBW = target_tex0.TBW;
        r.m_context->FRAME.PSM = PSMCT32;
        r.m_context->CLAMP.U64 = 0;
        r.m_context->CLAMP.WMS = 3;
        r.m_context->CLAMP.MAXU = 8;
        r.m_context->CLAMP.WMT = c.wmt;
        r.m_context->CLAMP.MAXV = 2;
        r.m_context->TEX0 = source_tex0;
        r.m_context->TEX1.U64 = 0;
        r.m_context->TEST.U64 = 0;
        r.m_context->ZBUF.U64 = 0;
        r.m_cached_ctx.FRAME = r.m_context->FRAME;
        r.m_cached_ctx.CLAMP = r.m_context->CLAMP;
        r.m_cached_ctx.TEX0 = r.m_context->TEX0;
        r.m_env.CTXT[1] = *r.m_context;
        r.m_env.CTXT[1].TEX0.TBP0 += c.page_copy ? 0x20 : 0;
        r.m_env.CTXT[1].FRAME.FBP += c.page_copy ? 1 : 0;
        r.m_backed_up_ctx = 1;
        r.m_state_flush_reason = UNKNOWN;
        r.m_draw_env = &r.m_env;
        r.m_channel_shuffle = true;
        r.m_channel_shuffle_rebuilt_quad = false;
        r.m_channel_shuffle_width = 0;
        r.m_in_target_draw = false;
        // Start in page zero so the helper's FRAME rebase does not change the
        // actual page-copy predicate halfway through a negative control.
        r.m_r = GSVector4i(7, 7, 57, 93);
        src.m_TEX0 = source_tex0;
        if (c.same_width)
            src.m_TEX0.TBW = target_tex0.TBW;
        src.m_valid = rt.m_valid = GSVector4i(0, 0, 256, 256);
        rt.m_TEX0 = target_tex0;
        rt.m_last_draw = 0;
        GSConfig.UserHacks_TextureInsideRt = c.mode;
        std::memcpy(r.m_vertex.buff, original, sizeof(original));
        std::memcpy(r.m_index.buff, indices, sizeof(indices));
        r.m_vertex.head = 0;
        r.m_vertex.tail = r.m_vertex.next = c.count + 2;
        r.m_index.tail = c.count;

        const bool page_copy = r.IsPageCopy();
        check(page_copy == c.page_copy, "GS channel shuffle case uses actual matching or nonmatching page-copy predicate");
        const bool accepted = r.EmulateChannelShuffle(&src, false, &rt) != 0;
        check(accepted && r.m_channel_shuffle, "GS actual channel shuffle accepts selected supported color channel including small WMT3 draws");

        alignas(32) GSVertex expected[slots];
        std::memcpy(expected, original, sizeof(expected));
        const bool fullscreen = c.mode == GSTextureInRtMode::Disabled || (c.page_copy && c.same_width);
        const GSVector4i expected_rect = fullscreen ? GSVector4i(0, 0, 1024, 1024) : GSVector4i(0, 0, 64, 96);
        if (!c.preserve)
        {
            expected[0].XYZ.X = static_cast<u16>(0x100 + (expected_rect.x << 4));
            expected[0].XYZ.Y = static_cast<u16>(0x200 + (expected_rect.y << 4));
            expected[1].XYZ.X = static_cast<u16>(0x100 + (expected_rect.z << 4));
            expected[1].XYZ.Y = static_cast<u16>(0x200 + (expected_rect.w << 4));
            expected[0].U = static_cast<u16>(expected_rect.x << 4);
            expected[0].V = static_cast<u16>(expected_rect.y << 4);
            expected[1].U = static_cast<u16>(expected_rect.z << 4);
            expected[1].V = static_cast<u16>(expected_rect.w << 4);
        }
        const bool exact_vertices = std::memcmp(r.m_vertex.buff, expected, sizeof(expected)) == 0;
        const bool exact_indices = std::memcmp(r.m_index.buff, indices, sizeof(indices)) == 0;
        const bool exact_counts = c.preserve ?
            (r.m_vertex.head == 0 && r.m_vertex.tail == c.count + 2 && r.m_vertex.next == c.count + 2 && r.m_index.tail == c.count) :
            (r.m_vertex.head == 2 && r.m_vertex.tail == 2 && r.m_vertex.next == 2 && r.m_index.tail == 2);
        const bool exact_flag = r.m_channel_shuffle_rebuilt_quad == !c.preserve;
        check(accepted && exact_vertices && exact_indices,
            "GS actual channel shuffle preserves all original XY UV payload and indices or rebuilds only the intended two vertices");
        check(accepted && exact_counts && exact_flag,
            "GS actual channel shuffle preserves region-repeat draw cursors and rebuilt flag while negative controls rebuild two vertices");
        check(accepted && r.m_r.eq(expected_rect), "GS actual channel shuffle rounds draw coverage independently of preserved geometry");
        std::printf("GS_CHANNEL_SHUFFLE case=%s accepted=%u pagecopy=%u preserve=%u vertices=%u indices=%u counts=%u flag=%u "
                    "actual_counts=%u,%u,%u/%u expected_counts=%u,%u,%u/%u actual_rebuilt=%u expected_rebuilt=%u "
                    "actual_xyuv0=%u,%u/%u,%u expected_xyuv0=%u,%u/%u,%u actual_xyuv1=%u,%u/%u,%u expected_xyuv1=%u,%u/%u,%u\n",
            c.name, unsigned(accepted), unsigned(page_copy), unsigned(c.preserve), unsigned(exact_vertices), unsigned(exact_indices),
            unsigned(exact_counts), unsigned(exact_flag), r.m_vertex.head, r.m_vertex.tail, r.m_vertex.next, r.m_index.tail,
            c.preserve ? 0u : 2u, c.preserve ? c.count + 2 : 2u, c.preserve ? c.count + 2 : 2u, c.preserve ? c.count : 2u,
            unsigned(r.m_channel_shuffle_rebuilt_quad), unsigned(!c.preserve),
            unsigned(r.m_vertex.buff[0].XYZ.X), unsigned(r.m_vertex.buff[0].XYZ.Y), unsigned(r.m_vertex.buff[0].U), unsigned(r.m_vertex.buff[0].V),
            unsigned(expected[0].XYZ.X), unsigned(expected[0].XYZ.Y), unsigned(expected[0].U), unsigned(expected[0].V),
            unsigned(r.m_vertex.buff[1].XYZ.X), unsigned(r.m_vertex.buff[1].XYZ.Y), unsigned(r.m_vertex.buff[1].U), unsigned(r.m_vertex.buff[1].V),
            unsigned(expected[1].XYZ.X), unsigned(expected[1].XYZ.Y), unsigned(expected[1].U), unsigned(expected[1].V));
    }
}

extern "C" void EmuCoreXOracleChannelShuffleGeometry(void (*check)(bool, const char*))
{
    GSRendererHW::TestChannelShuffleGeometry(check);
}
