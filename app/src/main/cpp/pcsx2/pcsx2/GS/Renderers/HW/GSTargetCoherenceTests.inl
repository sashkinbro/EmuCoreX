// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include from GSTextureCache.cpp under EMUCOREX_ENABLE_NATIVE_SELF_TESTS.
// Exercises the production target updater/preloader with a CPU capture device.
#include "common/ScopedGuard.h"
#include "cpuinfo.h"
#include "GSCoherenceTestDevice.h"

namespace
{
class CoherenceCache final : public GSTextureCache
{
public:
    using GSTextureCache::PreloadTarget;
    using GSTextureCache::ReadWithStatus;
    size_t PendingReads() const { return m_pending_downloads.size(); }
    void PoolReadback(std::unique_ptr<GSDownloadTexture> texture) { m_async_download_texture_pool.push_back(std::move(texture)); }
    std::shared_ptr<Palette> CurrentPalette(u16 pal) { return m_palette_map.LookupPalette(pal, false); }
    size_t HashEntries() const { return m_hash_cache.size(); }
    HashCacheEntry* FixtureHashEntry(const HashCacheKey& key) { auto it=m_hash_cache.find(key);return it==m_hash_cache.end()?nullptr:&it->second; }
    template<typename Cache = CoherenceCache>
    bool RebaseFixture(Target* target, const GIFRegTEX0& incoming, const GSVector4i& draw_rect, bool shuffle = false)
    {
        if constexpr (requires(Cache& c) { c.TryRebaseTargetForOverwrite(target, incoming, draw_rect, nullptr, nullptr, 0, shuffle); })
            return static_cast<Cache*>(this)->TryRebaseTargetForOverwrite(target, incoming, draw_rect, nullptr, nullptr, 0, shuffle);
        else
            return false;
    }
    Target* AddFixtureTarget(GIFRegTEX0 tex0, GSVector2i size, GSVector4i valid, float scale = 1.0f, int type = RenderTarget)
    {
        const GSVector2i physical(static_cast<int>(std::ceil(size.x * scale)), static_cast<int>(std::ceil(size.y * scale)));
        auto* texture = type == RenderTarget ?
            g_gs_device->CreateRenderTarget(physical.x, physical.y, GSTexture::Format::Color) :
            g_gs_device->CreateDepthStencil(physical.x, physical.y, GSTexture::Format::DepthStencil);
        auto* target = new Target(tex0, type, size, scale, texture);
        target->m_used = true;
        target->m_valid_rgb = target->m_valid_alpha_low = target->m_valid_alpha_high = true;
        target->m_alpha_max = 255;
        target->UpdateValidity(valid);
        m_target_memory_usage += texture->GetMemUsage();
        m_dst[type].push_front(target);
        return target;
    }
    bool HasFixtureTarget(const Target* target) const
    {
        for (const auto& targets : m_dst)
            for (const Target* candidate : targets)
                if (candidate == target)
                    return true;
        return false;
    }
};
}

#include "GSLocalMemoryParityTests.inl"
#include "GSTextureReplacementParityTests.inl"

extern "C" void EmuCoreXOracleTestTargetCoherence(void (*check)(bool, const char*))
{
    const bool isolated = !g_gs_renderer && !g_texture_cache && !g_gs_device;
    check(isolated, "GS target coherence requires isolated renderer/cache/device globals");
    if (!isolated)
        return;

    cpuinfo_initialize();
    const auto settings = GSConfig;
    const u64 saved_draw = GSState::s_n;
    const u64 saved_transfer = GSState::s_transfer_n;
    GSConfig = Pcsx2Config::GSOptions();
    GSConfig.UserHacks_TextureInsideRt = GSTextureInRtMode::InsideTargets;
    g_gs_device = std::make_unique<CoherenceDevice>();
    g_gs_renderer = std::make_unique<GSRendererHW>();
    // The renderer constructs a base cache. Replace it with an exposing subclass;
    // explicitly delete as the derived type below (base destructor is nonvirtual).
    g_texture_cache.reset();
    auto* cache = new CoherenceCache();
    g_texture_cache.reset(cache);
    const ScopedGuard restore([&]() {
        delete static_cast<CoherenceCache*>(g_texture_cache.release());
        g_gs_renderer.reset();
        g_gs_device->Destroy();
        g_gs_device.reset();
        GSConfig = settings;
        GSState::s_n = saved_draw;
        GSState::s_transfer_n = saved_transfer;
    });
    auto& renderer = *GSRendererHW::GetInstance();
    auto& device = *static_cast<CoherenceDevice*>(g_gs_device.get());
    GSState::s_n = 1000;

    // Literal VRAM words are the oracle: a target upload must retain the high
    // byte even while its current colour layout is CT24. No second ReadTexture
    // using the implementation's selected PSM is used to predict those bytes.
    GIFRegTEX0 tex0 = {};
    tex0.TBP0 = 0x1000;
    tex0.TBW = 1;
    tex0.PSM = PSMCT24;
    const GSVector2i size(64, 32);
    const GSVector4i area(0, 0, 64, 32);
    std::vector<u32> expected(64 * 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x)
        {
            const u32 alpha = 17 + ((x + y) % 177);
            const u32 pixel = (alpha << 24) | (u32(y + 1) << 16) | (u32(x + 3) << 8) | 0x35;
            expected[y * 64 + x] = pixel;
            renderer.m_mem.WritePixel32(x, y, pixel, tex0.TBP0, tex0.TBW);
        }
    {
        CoherenceTexture destination(GSTexture::RenderTarget, 64, 32, 1, GSTexture::Format::Color);
        GSTextureCache::Target target(tex0, GSTextureCache::RenderTarget, size, 1.0f, &destination);
        const ScopedGuard detach([&]() { target.m_texture = nullptr; });
        target.m_valid = area;
        target.m_valid_rgb = target.m_valid_alpha_low = target.m_valid_alpha_high = true;
        RGBAMask mask;
        mask._u32 = 0xf;
        GSTextureCache::AddDirtyRectTarget(&target, area, PSMCT32, 1, mask);
        device.batches.clear();
        target.Update();
        check(device.batches.size() == 1, "GS actual CT24 Target::Update emits one captured upload draw");
        if (device.batches.size() == 1)
        {
            const auto& batch = device.batches.front();
            check(batch.pixels == expected, "GS actual CT24 target upload preserves literal VRAM alpha bytes");
            check(batch.mask == 0xf && batch.shader == ShaderConvert::COPY,
                "GS actual CT24 alpha upload retains RGBA mask and unscaled copy shader");
        }
        check(target.m_alpha_min == 17 && target.m_alpha_max == 111,
            "GS actual CT24 alpha upload tracks extrema from preserved VRAM bytes");
        check(target.m_dirty.empty(), "GS successful actual target update consumes its dirty rectangles");
        [&]<typename T>(const T& uploaded) {
            if constexpr (requires { uploaded.m_alpha_known.bits; uploaded.m_alpha_known.value; })
                check(uploaded.m_alpha_known.bits == 0x80 && uploaded.m_alpha_known.value == 0,
                    "GS actual CT24 upload seeds exact common alpha bit from literal VRAM range");
            else
                check(false, "GS target exact alpha-known-bit contract is present");
        }(target);

        // RGB-only dirt must not alter alpha metadata or enable alpha writes.
        target.m_alpha_min = 23;
        target.m_alpha_max = 91;
        mask._u32 = 0x7;
        GSTextureCache::AddDirtyRectTarget(&target, area, PSMCT24, 1, mask);
        device.batches.clear();
        target.Update();
        check(device.batches.size() == 1 && device.batches.front().mask == 0x7,
            "GS actual CT24 RGB-only update preserves the alpha write mask");
        check(target.m_alpha_min == 23 && target.m_alpha_max == 91,
            "GS actual CT24 RGB-only update preserves existing alpha extrema");
    }

    {
        GIFRegTEX0 depth_tex0 = tex0;
        depth_tex0.PSM = PSMZ32;
        CoherenceTexture destination(GSTexture::DepthStencil, 96, 48, 1, GSTexture::Format::DepthStencil);
        GSTextureCache::Target depth(depth_tex0, GSTextureCache::DepthStencil, size, 1.5f, &destination);
        const ScopedGuard detach([&]() { depth.m_texture = nullptr; });
        depth.UpdateValidity(area);
        RGBAMask channels;
        channels._u32 = 0xf;
        GSTextureCache::AddDirtyRectTarget(&depth, area, PSMZ32, 1, channels);
        device.batches.clear();
        depth.Update();
        check(device.batches.size() == 1 && device.batches.front().filter == Filter::Biln,
            "GS actual fractional-scale depth upload requests bilinear conversion");
    }

    tex0.PSM = PSMCT32;
    const auto upload = [&](u32 bw, GSVector4i rect, u64 draw) {
        GSState::GSUploadQueue entry = {};
        entry.blit.DBP = tex0.TBP0;
        entry.blit.DBW = bw;
        entry.blit.DPSM = PSMCT32;
        entry.rect = rect;
        entry.draw = draw;
        entry.transfer_type = GSState::Clear;
        entry.zero_clear = true;
        renderer.m_draw_transfers.assign(1, entry);
    };
    const auto preload = [&](GIFRegTEX0 requested, GSTextureCache::Target& target, bool force_preload) {
        return cache->PreloadTarget(requested, size, size, false, force_preload, true, area, &target);
    };
    {
        CoherenceTexture destination(GSTexture::RenderTarget, 64, 32, 1, GSTexture::Format::Color);
        GSTextureCache::Target target(tex0, GSTextureCache::RenderTarget, size, 1.0f, &destination);
        const ScopedGuard detach([&]() { target.m_texture = nullptr; });
        upload(1, GSVector4i(0, 0, 64, 64), GSState::s_n);
        check(preload(tex0, target, false), "GS actual preload recognizes first part of shared RT/Z clear");
        check(renderer.m_draw_transfers.size() == 1,
            "GS actual preload retains a larger shared clear for the second target");

        GIFRegTEX0 second = tex0;
        second.TBP0 += 32; // The second 8KiB page of the same clear.
        CoherenceTexture second_destination(GSTexture::RenderTarget, 64, 32, 1, GSTexture::Format::Color);
        GSTextureCache::Target second_target(second, GSTextureCache::RenderTarget, size, 1.0f, &second_destination);
        const ScopedGuard detach_second([&]() { second_target.m_texture = nullptr; });
        check(preload(second, second_target, false),
            "GS actual preload still recognizes the second target covered by shared clear");
    }
    {
        CoherenceTexture destination(GSTexture::RenderTarget, 64, 32, 1, GSTexture::Format::Color);
        GSTextureCache::Target target(tex0, GSTextureCache::RenderTarget, size, 1.0f, &destination);
        const ScopedGuard detach([&]() { target.m_texture = nullptr; });
        upload(2, GSVector4i(0, 0, 128, 32), GSState::s_n);
        preload(tex0, target, false);
        check(target.m_valid.eq(area),
            "GS actual preload rejects validity dimensions expressed with incompatible transfer DBW");
    }
    {
        CoherenceTexture destination(GSTexture::RenderTarget, 64, 32, 1, GSTexture::Format::Color);
        GSTextureCache::Target target(tex0, GSTextureCache::RenderTarget, size, 1.0f, &destination);
        const ScopedGuard detach([&]() { target.m_texture = nullptr; });
        upload(1, area, GSState::s_n);
        check(preload(tex0, target, false) && renderer.m_draw_transfers.empty(),
            "GS actual preload consumes an exact matching clear as a positive control");
    }
    {
        CoherenceTexture destination(GSTexture::RenderTarget, 64, 32, 1, GSTexture::Format::Color);
        GSTextureCache::Target target(tex0, GSTextureCache::RenderTarget, size, 1.0f, &destination);
        const ScopedGuard detach([&]() { target.m_texture = nullptr; });
        upload(1, area, GSState::s_n - 25);
        check(!preload(tex0, target, true),
            "GS actual forced preload does not treat an old clear as the current target contents");
        check(renderer.m_draw_transfers.size() == 1,
            "GS actual forced preload leaves stale clear evidence for normal pruning");
    }
    {
        CoherenceTexture destination(GSTexture::RenderTarget, 64, 32, 1, GSTexture::Format::Color);
        GSTextureCache::Target target(tex0, GSTextureCache::RenderTarget, size, 1.0f, &destination);
        const ScopedGuard detach([&]() { target.m_texture = nullptr; });
        upload(1, area, GSState::s_n - 1);
        check(preload(tex0, target, true) && renderer.m_draw_transfers.empty(),
            "GS actual forced preload accepts a matching immediately preceding clear");
    }
    {
        cache->RemoveAll(true, true, true);
        renderer.m_draw_transfers.clear();
        tex0.TBP0 = 0x1400;
        tex0.TBW = 1;
        auto* owner = cache->AddFixtureTarget(tex0, size, area);
        check(cache->GetTargetWithSharedBits(0x1401, PSMCT32) == owner,
            "GS actual inside-target shared-bits lookup accepts an interior block");
        check(cache->GetTargetWithSharedBits(owner->EndBlock(), PSMCT32) == nullptr,
            "GS actual inside-target shared-bits lookup excludes the end-block sentinel");
        cache->RemoveAll(true, true, true);
    }
    {
        CoherenceTexture destination(GSTexture::RenderTarget, 64, 64, 1, GSTexture::Format::Color);
        GSTextureCache::Target target(tex0, GSTextureCache::RenderTarget, GSVector2i(64, 64), 1.0f, &destination);
        const ScopedGuard detach([&]() { target.m_texture = nullptr; });
        [&]<typename T>(T& growing) {
            if constexpr (requires { growing.m_alpha_known.bits; growing.m_alpha_known.value; })
            {
                growing.UpdateValidity(area);
                growing.m_alpha_known = {0xff, 0xa5};
                growing.UpdateValidity(area);
                check(growing.m_alpha_known.bits == 0xff && growing.m_alpha_known.value == 0xa5,
                    "GS actual unchanged validity preserves exact alpha knowledge");
                growing.UpdateValidity(GSVector4i(0, 0, 64, 64));
                check(growing.m_alpha_known.bits == 0x5a && growing.m_alpha_known.value == 0,
                    "GS actual validity growth retains only previously known zero alpha bits");
            }
        }(target);
    }
    {
        cache->RemoveAll(true, true, true);
        renderer.m_draw_transfers.clear();
        tex0.TBP0 = 0x3fc0;
        tex0.TBW = 1;
        auto* owner = cache->AddFixtureTarget(tex0, GSVector2i(64, 96), GSVector4i(0, 0, 64, 96));
        owner->m_last_draw = GSState::s_n - 1;
        GIFRegTEX0 requested = tex0;
        requested.TBP0 = 0x3fe0;
        device.allow_stretch = true;
        auto* selected = cache->LookupTarget(requested, size, 1.0f, GSTextureCache::RenderTarget,
            true, 0, false, false, true, true, area);
        check(selected == owner,
            "GS actual inside-target lookup keeps wrapped owner using unwrapped overlap bounds");
        cache->RemoveAll(true, true, true);
        device.allow_stretch = false;
    }
    // Keep this fixture compilable before the writer contract exists so RED is a
    // behavioral failure. Once present it exercises the real bulk VRAM writers.
    [&]<typename Memory>(Memory& memory) {
        if constexpr (requires { memory.WriteSeq(); memory.HashDirty(0); memory.PageStamp(0);
                                 memory.TestAndClearHashDirty(0); memory.MarkPagesWritten(memory.GetOffset(0, 1, PSMCT32), area); })
        {
            for (u32 block = 0; block < GS_MAX_BLOCKS; ++block)
                memory.TestAndClearHashDirty(block);
            const auto off = memory.GetOffset(0x2800, 1, PSMCT32);
            const GSVector4i block_rect(0, 0, 8, 8);
            alignas(32) u32 words[64] = {};
            const u64 before = memory.WriteSeq();
            memory.WritePixel32(reinterpret_cast<u8*>(words), 8 * sizeof(u32), off, block_rect);
            check(memory.WriteSeq() > before && memory.PageStamp(0x2800 / 32) == memory.WriteSeq(),
                "GS actual bulk VRAM writer advances covered page stamp");
            check(memory.TestAndClearHashDirty(0x2800) && !memory.HashDirty(0x2801),
                "GS actual bulk VRAM writer invalidates only touched block hashes");
            const u64 before_same_bytes = memory.WriteSeq();
            memory.WritePixel32(reinterpret_cast<u8*>(words), 8 * sizeof(u32), off, block_rect, 0x00ffffff);
            check(memory.WriteSeq() > before_same_bytes && memory.HashDirty(0x2800),
                "GS actual masked same-byte VRAM rewrite still invalidates cached block hash");
            for (u32 block = 0; block < GS_MAX_BLOCKS; ++block)
                memory.TestAndClearHashDirty(block);
            const auto wrapping = memory.GetOffset(GS_MAX_BLOCKS - 32, 1, PSMCT32);
            memory.MarkPagesWritten(wrapping, GSVector4i(0, 0, 64, 64));
            check(memory.HashDirty(GS_MAX_BLOCKS - 1) && memory.HashDirty(0) && !memory.HashDirty(32),
                "GS actual writer mark follows GS-memory wrap without touching unrelated pages");
            const u64 before_invalid = memory.MarkAllEvents();
            memory.MarkPagesWritten(off, GSVector4i(2047, 0, 2049, 1));
            check(memory.MarkAllEvents() == before_invalid + 1 && memory.HashDirty(32),
                "GS actual out-of-domain writer conservatively invalidates all hash windows");
        }
        else
        {
            check(false, "GS bulk VRAM writer tracking contract is present");
        }
    }(renderer.m_mem);
    // A transfer with another pitch can land outside the target's valid rows,
    // hence InvalidateVideoMem need not have left a dirty rect on that target.
    // The lookup must retire the stale owner using the recent upload evidence.
    {
        cache->RemoveAll(true, true, true);
        renderer.m_draw_transfers.clear();
        tex0.TBP0 = 0x1800;
        tex0.TBW = 1;
        tex0.TW = 6;
        tex0.TH = 5;
        auto* stale = cache->AddFixtureTarget(tex0, GSVector2i(64, 64), GSVector4i(0, 32, 64, 64));
        GIFRegTEX0 requested = tex0;
        requested.TBW = 2;
        upload(2, area, GSState::s_n);
        GIFRegCLAMP clamp = {};
        clamp.WMS = clamp.WMT = 1;
        GIFRegFRAME frame = {};
        frame.FBP = 0x20;
        auto* source = cache->LookupSource(true, requested, {}, clamp, area, nullptr, false, false, frame);
        check(source && !source->m_from_target,
            "GS actual source lookup uses memory for a differing-pitch upload outside valid rows");
        check(!cache->HasFixtureTarget(stale),
            "GS actual source lookup retires stale target despite an empty dirty list");
        cache->RemoveAll(true, true, true);
    }
    // Alpha-index fonts with a changed pitch cannot take the whole-target road:
    // the second page row would be read using the old framebuffer's page order.
    // Exact pitch and single-row inputs remain positive controls.
    for (int control = 0; control < 3; ++control)
    {
        cache->RemoveAll(true, true, true);
        renderer.m_draw_transfers.clear();
        tex0.TBP0 = 0x2000;
        tex0.TBW = 2;
        tex0.TW = tex0.TH = 7;
        auto* owner = cache->AddFixtureTarget(tex0, GSVector2i(128, 64), GSVector4i(0, 0, 128, 64));
        GIFRegTEX0 requested = tex0;
        requested.PSM = PSMT8H;
        requested.TBW = control == 1 ? 2 : 1;
        requested.TW = 6;
        requested.TH = control == 2 ? 5 : 6;
        GIFRegCLAMP clamp = {};
        clamp.WMS = clamp.WMT = 1;
        GIFRegFRAME frame = {};
        frame.FBP = 0x20;
        device.allow_stretch = true;
        GSConfig.GPUPaletteConversion = false;
        auto* source = cache->LookupSource(true, requested, {}, clamp,
            GSVector4i(0, 0, 64, 1 << requested.TH), nullptr, false, false, frame, false, true);
        check(source && (control == 0 ? !source->m_from_target : source->m_from_target == owner),
            control == 0 ? "GS actual alpha-index lookup refuses differing-pitch multirow target" :
            control == 1 ? "GS actual alpha-index lookup retains exact-pitch target control" :
                           "GS actual alpha-index lookup retains single-page-row target control");
        cache->RemoveAll(true, true, true);
        device.allow_stretch = false;
    }
    {
        cache->RemoveAll(true, true, true);
        GSTextureCache::HashCacheKey pending;
        pending.TEX0Hash = 0x10203040;
        cache->InjectHashCacheTexture(pending, g_gs_device->CreateTexture(16, 16, 1, GSTexture::Format::Color), {0, 255});
        check(cache->HashEntries() == 1, "GS actual completed replacement enters hash cache without a source");
        for (int frame = 0; frame < 31; ++frame)
            cache->IncAge();
        check(cache->HashEntries() == 0, "GS actual unreferenced completed replacement ages out after thirty frames");
        cache->RemoveAll(true, true, true);
    }
    [&]<typename Cache>(Cache& tracked) {
        if constexpr (requires { Cache::TargetsMayReach(0, 0); })
        {
            cache->RemoveAll(true, true, true);
            tex0.TBP0 = 0x2c00;
            tex0.TBW = 1;
            tex0.PSM = PSMCT32;
            auto* target = tracked.AddFixtureTarget(tex0, size, area);
            check(Cache::TargetsMayReach(0x2c00, 0x2c1f) && !Cache::TargetsMayReach(0x3000, 0x301f),
                "GS actual target page index distinguishes owned and empty ranges");
            target->SetTBP0(0x2c40);
            target->UpdateValidity(area);
            check(!Cache::TargetsMayReach(0x2c00, 0x2c1f) && Cache::TargetsMayReach(0x2c40, 0x2c5f),
                "GS actual target base and end setters relocate page-index ownership");
            cache->RemoveAll(true, true, true);
            check(!Cache::TargetsMayReach(0x2c40, 0x2c5f),
                "GS actual target destruction removes page-index ownership");
        }
        else
            check(false, "GS cache target page-index lifecycle contract is present");
    }(*cache);
    {
        cache->RemoveAll(true, true, true);
        renderer.m_draw_transfers.clear();
        tex0.TBP0 = 0x2400;
        tex0.TBW = 2;
        tex0.PSM = PSMCT32;
        auto* retained = cache->AddFixtureTarget(tex0, GSVector2i(128, 96), GSVector4i(0, 0, 128, 96));
        retained->m_drawn_since_read = GSVector4i(0, 64, 128, 96);
        auto* texture = static_cast<CoherenceTexture*>(retained->m_texture);
        for (int y = 0; y < 96; ++y)
            for (int x = 0; x < 128; ++x)
                texture->pixels[y * 128 + x] = 0x81000000u | (u32(y) << 8) | u32(x);
        const std::vector<u32> suffix(texture->pixels.begin() + 64 * 128, texture->pixels.end());
        GIFRegTEX0 incoming = tex0;
        incoming.TBP0 += 64;
        incoming.PSM = PSMZ32;
        device.allow_copy = true;
        const u32 before = device.copies;
        const bool rebased = cache->RebaseFixture(retained, incoming, GSVector4i(0, 0, 128, 32));
        check(rebased, "GS actual clean native C32 suffix rebases before disjoint Z32 overwrite");
        if (rebased)
        {
            check(retained->m_TEX0.TBP0 == tex0.TBP0 + 128 && retained->m_valid.eq(GSVector4i(0, 0, 128, 32)),
                "GS actual suffix rebase publishes the preserved page address and exact validity");
            check(device.copies == before + 1 && static_cast<CoherenceTexture*>(retained->m_texture)->pixels == suffix,
                "GS actual suffix rebase copies literal retained rows exactly once");
        }
        cache->RemoveAll(true, true, true);
        device.allow_copy = false;
        // A shuffle cannot use the suffix proof, even when its page envelopes are disjoint.
        retained = cache->AddFixtureTarget(tex0, GSVector2i(128, 96), GSVector4i(0, 0, 128, 96));
        retained->m_drawn_since_read = GSVector4i(0, 64, 128, 96);
        check(!cache->RebaseFixture(retained, incoming, GSVector4i(0, 0, 128, 32), true),
            "GS actual suffix rebase rejects the shuffle negative control");
        cache->RemoveAll(true, true, true);
    }
    [&]<typename Cache>(Cache& tracked) {
        if constexpr (requires { tracked.BlockHash(renderer.m_mem, 0); tracked.GetHashMemoStats();
                                Cache::SetBlockHashCacheEnabled(true); })
        {
            cache->RemoveAll(true, true, true);
            auto& memory = renderer.m_mem;
            GIFRegTEX0 large = {};
            large.TBP0 = 0x1000;
            large.TBW = 4;
            large.TW = large.TH = 8;
            large.PSM = PSMCT32;
            alignas(32) u32 block[64] = {};
            const auto off = memory.GetOffset(large.TBP0, large.TBW, large.PSM);
            const GSVector4i block_rect(0, 0, 8, 8);
            memory.WritePixel32(reinterpret_cast<u8*>(block), 8 * sizeof(u32), off, block_rect);
            const u64 hash_zero = tracked.BlockHash(memory, large.TBP0);
            block[0] = 0x332211ff;
            memory.WritePixel32(reinterpret_cast<u8*>(block), 8 * sizeof(u32), off, block_rect);
            check(tracked.BlockHash(memory, large.TBP0) != hash_zero,
                "GS actual cached block hash follows the bulk writer dirty stamp");
            using Key = typename Cache::HashCacheKey;
            using Region = typename Cache::SourceRegion;
            const auto first = Key::Create(large, {}, nullptr, nullptr, Region(), false);
            const auto hits = tracked.GetHashMemoStats().hits;
            const auto second = Key::Create(large, {}, nullptr, nullptr, Region(), false);
            check(first == second && tracked.GetHashMemoStats().hits == hits + 1,
                "GS actual texture hash memo reuses an unchanged large window");
            const auto elsewhere = memory.GetOffset(0x3000, 1, PSMCT32);
            memory.WritePixel32(reinterpret_cast<u8*>(block), 8 * sizeof(u32), elsewhere, block_rect);
            const auto unrelated_hits = tracked.GetHashMemoStats().hits;
            check(Key::Create(large, {}, nullptr, nullptr, Region(), false) == first && tracked.GetHashMemoStats().hits == unrelated_hits + 1,
                "GS actual texture hash memo survives a write to an unrelated page");
            block[0] ^= 0x10;
            memory.WritePixel32(reinterpret_cast<u8*>(block), 8 * sizeof(u32), off, block_rect);
            const auto stale = tracked.GetHashMemoStats().stale;
            check(Key::Create(large, {}, nullptr, nullptr, Region(), false) != first && tracked.GetHashMemoStats().stale == stale + 1,
                "GS actual texture hash memo rejects a covered-page rewrite");
            GIFRegTEX0 indexed = large;
            indexed.PSM = PSMT8H;
            indexed.TW = indexed.TH = 6;
            Region window = {};
            window.SetX(1, 33);
            Cache::SetBlockHashCacheEnabled(true);
            const auto alpha = Key::Create(indexed, {}, nullptr, nullptr, window, true);
            // The H format only reads the alpha byte. RGB rewrites cannot change its key.
            block[0] ^= 0x550000;
            memory.WritePixel32(reinterpret_cast<u8*>(block), 8 * sizeof(u32), off, block_rect, 0x00ffffff);
            check(Key::Create(indexed, {}, nullptr, nullptr, window, true) == alpha,
                "GS actual raw alpha-index key ignores unrelated RGB bytes");
            Region shifted = {};
            shifted.SetX(2, 34);
            check(Key::Create(indexed, {}, nullptr, nullptr, shifted, true) != alpha,
                "GS actual raw window key includes sub-block texel offset");
            Cache::SetBlockHashCacheEnabled(false);
            const auto uncached = Key::Create(indexed, {}, nullptr, nullptr, window, true);
            check(Key::Create(indexed, {}, nullptr, nullptr, window, true) == uncached,
                "GS actual uncached raw-block key is stable as a negative control");
            Cache::SetBlockHashCacheEnabled(true);
            cache->RemoveAll(true, true, true);
        }
        else
            check(false, "GS cache block hash and large-window memo contract is present");
    }(*cache);
    {
        cache->RemoveAll(true, true, true);
        auto& memory = renderer.m_mem;
        auto& clut = memory.m_clut;
        GSConfig.UserHacks_GPUTargetCLUTMode = GSGPUTargetCLUTMode::Disabled;
        GIFRegTEX0 palette = {};
        palette.CBP = 0x3000;
        palette.CPSM = PSMCT32;
        palette.CSM = 1;
        palette.PSM = PSMT8;
        GIFRegTEXCLUT layout = {};
        layout.CBW = 4;
        layout.COU = 2;
        layout.COV = 3;
        for (u32 index = 0; index < 384; ++index)
            memory.WritePixel32((layout.COU << 4) + index, layout.COV, 0x80000000u | index, palette.CBP, layout.CBW);
        clut.Write(palette, layout);
        clut.Read32(palette, {});
        bool literal = true;
        for (u32 index = 0; index < 256; ++index)
            literal &= clut[index] == (0x80000000u | (128 + (index & 127)));
        check(literal, "GS actual CSM2 32-bit wide palette reads origin plus 128 and repeats its 128 words");
        GIFRegTEX0 grouped = palette;
        grouped.CSM = 0;
        grouped.CSA = 1;
        clut.Read32(grouped, {});
        literal = true;
        for (u32 index = 0; index < 256; ++index)
            literal &= clut[index] == (0x80000000u | (128 + ((index | 16) & 127)));
        check(literal, "GS actual wide palette reads CSA by ORing the sixteen-entry group");
        palette.PSM = PSMT4;
        palette.CPSM = 3; // Undocumented CPSM: bit 1 still selects a 16-bit entry.
        for (u32 index = 0; index < 16; ++index)
            memory.WritePixel16((layout.COU << 4) + index, layout.COV, 0x801f, palette.CBP, layout.CBW);
        GIFRegTEXA alpha = {};
        alpha.TA1 = 128;
        clut.Write(palette, layout);
        clut.Read32(palette, alpha);
        literal = true;
        for (u32 index = 0; index < 16; ++index)
            literal &= clut[index] == 0x800000f8u;
        check(literal, "GS actual undocumented CPSM keeps the console entry-width decode");
        [&]<typename Clut>(Clut& observed) {
            if constexpr (requires { observed.GetReadSerial(); })
            {
                const auto before = observed.GetReadSerial();
                observed.Read32(palette, alpha);
                check(observed.GetReadSerial() == before,
                    "GS actual unchanged expanded palette preserves its read serial");
                observed.Reset();
                observed.Write(palette, layout);
                observed.Read32(palette, alpha);
                check(observed.GetReadSerial() != before,
                    "GS actual palette reset and expansion invalidate the palette memo serial");
            }
            else
                check(false, "GS expanded-palette memo read-serial contract is present");
        }(clut);
        const auto cached = cache->CurrentPalette(16);
        check(cache->CurrentPalette(16) == cached,
            "GS actual current-palette lookup retains identical palette ownership");
        // The unsafe GPU road must really be declined by Read32, not just by a pure guard.
        tex0 = GIFRegTEX0{};
        tex0.TBP0 = palette.CBP;
        tex0.TBW = 1;
        tex0.PSM = PSMCT32;
        cache->AddFixtureTarget(tex0, size, area);
        palette = GIFRegTEX0{};
        palette.PSM = PSMT8;
        palette.CPSM = PSMCT32;
        palette.CBP = tex0.TBP0;
        palette.CSA = 1;
        GSConfig.UserHacks_GPUTargetCLUTMode = GSGPUTargetCLUTMode::Enabled;
        device.allow_clut = true;
        const u32 before_gpu = device.clut_updates;
        clut.Write(palette, {});
        clut.Read32(palette, {});
        check(clut.GetGPUTexture() == nullptr && device.clut_updates == before_gpu,
            "GS actual Read32 refuses the inexact wide-CSA GPU palette road");
        palette.CSA = 0;
        clut.Write(palette, {});
        clut.Read32(palette, {});
        check(clut.GetGPUTexture() != nullptr && device.clut_updates > before_gpu,
            "GS actual Read32 retains the exact wide-CSA-zero GPU palette control");
        GSConfig.UserHacks_GPUTargetCLUTMode = GSGPUTargetCLUTMode::Disabled;
        device.allow_clut = false;
        clut.Reset();
        cache->RemoveAll(true, true, true);
    }
    {
        cache->RemoveAll(true, true, true);
        renderer.m_draw_transfers.clear();
        tex0 = GIFRegTEX0{};
        tex0.TBP0 = 0x1000;
        tex0.TBW = 1;
        tex0.PSM = PSMCT32;
        auto* source = cache->AddFixtureTarget(tex0, size, area);
        GIFRegTEX0 other = tex0;
        other.TBP0 = 0x1800;
        auto* destination = cache->AddFixtureTarget(other, size, area);
        RGBAMask channels;
        channels._u32 = 0xf;
        GSTextureCache::AddDirtyRectTarget(source, GSVector4i(0, 0, 8, 8), PSMCT32, 1, channels);
        GSTextureCache::AddDirtyRectTarget(destination, GSVector4i(0, 0, 8, 8), PSMCT32, 1, channels);
        device.batches.clear();
        check(cache->PageMove(tex0.TBP0, other.TBP0, 1, PSMCT32, 0, 0, 0, 0, 64, 32),
            "GS actual full-page move takes the target copy road");
        check(source->m_dirty.empty() && destination->m_dirty.empty() && device.batches.size() == 3,
            "GS actual page move uploads both pending CPU writes before copying the page");
        cache->RemoveAll(true, true, true);
    }
    {
        cache->RemoveAll(true, true, true);
        tex0 = GIFRegTEX0{};
        tex0.TBP0 = 0x1000;
        tex0.TBW = 1;
        tex0.PSM = PSMCT32;
        cache->AddFixtureTarget(tex0, size, area, 1.5f);
        GIFRegTEX0 depth = tex0;
        depth.TBP0 = 0x1800;
        depth.PSM = PSMZ32;
        cache->AddFixtureTarget(depth, size, area, 1.5f, GSTextureCache::DepthStencil);
        device.stretches.clear();
        device.allow_stretch = true;
        check(cache->Move(tex0.TBP0, 1, PSMCT32, 1, 2, depth.TBP0, 1, PSMZ32, 3, 4, 8, 8),
            "GS actual fractional-scale color-to-depth move takes conversion road");
        const bool coordinates = device.stretches.size() == 1 &&
            device.stretches[0].destination_rect.x == 5 && device.stretches[0].destination_rect.y == 6 &&
            device.stretches[0].destination_rect.z == 17 && device.stretches[0].destination_rect.w == 18;
        check(coordinates, "GS actual color-to-depth move uses destination device coordinates");
        cache->RemoveAll(true, true, true);
        device.allow_stretch = false;
    }
    {
        cache->RemoveAll(true, true, true);
        renderer.m_draw_transfers.clear();
        tex0 = GIFRegTEX0{};
        tex0.TBP0 = 0x2420;
        tex0.TBW = 1;
        tex0.PSM = PSMCT24;
        auto* old_target = cache->AddFixtureTarget(tex0, GSVector2i(64, 96), GSVector4i(0, 0, 64, 96), 1.1f);
        auto* old_texture = static_cast<CoherenceTexture*>(old_target->m_texture);
        for (int y = 0; y < old_texture->GetHeight(); ++y)
            for (int x = 0; x < old_texture->GetWidth(); ++x)
                old_texture->pixels[y * old_texture->GetWidth() + x] = 0x80000000u | (u32(y) << 8) | u32(x);
        GIFRegTEX0 incoming = tex0;
        incoming.TBP0 = 0x2400;
        incoming.PSM = PSMCT32;
        auto* target = cache->AddFixtureTarget(incoming, GSVector2i(8, 72), GSVector4i(0, 0, 8, 72), 1.1f);
        device.allow_copy = true;
        const u32 invalid = device.invalid_copies;
        cache->PreloadTarget(incoming, GSVector2i(8, 72), GSVector2i(8, 72), false,
            false, false, GSVector4i(0, 0, 8, 72), target);
        check(old_target->m_texture->GetWidth() == 71 && old_target->m_texture->GetHeight() == 106 && device.invalid_copies == invalid,
            "GS actual fractional preload reallocation retains ceil-scaled physical bounds");
        check(static_cast<CoherenceTexture*>(old_target->m_texture)->pixels[0] == (0x80000000u | (36u << 8)),
            "GS actual fractional preload suffix starts at the first device row owned by native row 32");
        cache->RemoveAll(true, true, true);
        device.allow_copy = false;
    }
    {
        cache->RemoveAll(true, true, true);
        renderer.m_draw_transfers.clear();
        const int saved_frame = g_perfmon.GetFrame();
        const ScopedGuard reset_frame([&]() { g_perfmon.SetFrame(saved_frame); });
        tex0 = GIFRegTEX0{};
        tex0.TBP0 = 0x1000;
        tex0.TBW = 1;
        tex0.PSM = PSMCT16;
        auto* target = cache->AddFixtureTarget(tex0, size, area);
        auto* pixels = static_cast<CoherenceTexture*>(target->m_texture);
        constexpr u32 rgba[8] = {0x800000f8, 0x8000f800, 0x80f80000, 0x80f8f8f8, 0x7ff8f8f8, 0, 0x80000000, 8};
        constexpr u16 packed[8] = {0x801f, 0x83e0, 0xfc00, 0xffff, 0x7fff, 0, 0x8000, 1};
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                pixels->pixels[y * 64 + x] = rgba[x];
        device.allow_stretch = device.allow_readback = true;
        device.stretches.clear();
        GSConfig.HWDownloadMode = GSHardwareDownloadMode::Enabled;
        const GSVector4i read_rect(0, 0, 8, 8);
        check(cache->ReadWithStatus(target, read_rect, true), "GS actual native RGB5A1 readback succeeds");
        bool literal = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                literal &= renderer.m_mem.ReadPixel16(x, y, tex0.TBP0, tex0.TBW) == packed[x];
        check(literal, "GS actual RGB5A1 readback preserves literal red green blue and alpha threshold words");
        check(device.stretches.empty() && device.readbacks.last_copy_format == GSTexture::Format::Color,
            "GS actual native RGB5A1 readback packs on CPU without a conversion render pass");
        GSConfig.HWDownloadMode = GSHardwareDownloadMode::Asynchronous;
        device.stretches.clear();
        g_perfmon.SetFrame(100);
        check(cache->ReadWithStatus(target, read_rect, false) && cache->PendingReads() == 1,
            "GS actual asynchronous RGB5A1 readback queues without waiting");
        g_perfmon.SetFrame(104);
        cache->ProcessPendingDownloads();
        literal = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                literal &= renderer.GetAsyncReadbackMemory().ReadPixel16(x, y, tex0.TBP0, tex0.TBW) == packed[x];
        check(literal && cache->PendingReads() == 0,
            "GS actual asynchronous RGB5A1 readback publishes literal packed words to shadow VRAM");
        cache->RemoveAll(true, true, true);
        // A staging allocation's format is part of its identity, even if its dimensions fit.
        cache->PoolReadback(std::make_unique<CoherenceDownload>(64, 32, GSTexture::Format::UInt16, device.readbacks));
        tex0.PSM = PSMCT32;
        target = cache->AddFixtureTarget(tex0, size, area);
        cache->ReadWithStatus(target, read_rect, false);
        check(device.readbacks.last_copy_format == GSTexture::Format::Color,
            "GS actual asynchronous readback skips a pooled allocation of another format");
        cache->DiscardPendingDownloads();
        device.readbacks.ready = false;
        g_perfmon.SetFrame(200);
        const u32 before_flush = device.readbacks.flushes;
        cache->ReadWithStatus(target, read_rect, false);
        check(device.readbacks.flushes == before_flush,
            "GS actual pending readback does not flush during its enqueue");
        g_perfmon.SetFrame(204);
        cache->ProcessPendingDownloads();
        check(cache->PendingReads() == 0 && device.readbacks.flushes == before_flush + 1,
            "GS actual stalled backend readback retires after the bounded four-frame deadline");
        device.readbacks.ready = true;
        cache->RemoveAll(true, true, true);
        device.allow_stretch = device.allow_readback = false;
        GSConfig.HWDownloadMode = GSHardwareDownloadMode::Enabled;
    }
    [&]<typename Cache>(Cache& display) {
        if constexpr (requires { display.NoteColdStart(); })
        {
            cache->RemoveAll(true, true, true);
            renderer.m_draw_transfers.clear();
            tex0 = GIFRegTEX0{};
            tex0.TBP0 = 0x1000;
            tex0.TBW = 1;
            tex0.PSM = PSMCT32;
            std::vector<u32> literal(64 * 32);
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 64; ++x)
                {
                    literal[y * 64 + x] = 0x71000000u | (u32(y) << 8) | u32(x);
                    renderer.m_mem.WritePixel32(x, y, literal[y * 64 + x], tex0.TBP0, tex0.TBW);
                }
            renderer.m_mem.MarkPagesWritten(renderer.m_mem.GetOffset(tex0.TBP0, tex0.TBW, PSMCT32), area);
            display.NoteColdStart();
            auto* cold = display.LookupDisplayTarget(tex0, size, 1.0f, false);
            check(cold != nullptr, "GS actual cold-start display builds its first frame from snapshot VRAM");
            if (cold)
            {
                device.batches.clear();
                cold->Update();
                check(device.batches.size() == 1 && device.batches[0].pixels == literal,
                    "GS actual cold-start display uploads literal snapshot words");
                const size_t before_alpha_copy = device.stretches.size();
                device.allow_stretch = true;
                const bool same_target = display.LookupDisplayTarget(tex0, size, 1.0f, false) == cold;
                device.allow_stretch = false;
                check(same_target,
                    "GS actual first display hit retains its target and ends cold-start preload");
                check(device.stretches.size() == before_alpha_copy + 1 &&
                    device.stretches.back().shader == ShaderConvert::COPY && device.stretches.back().mask == 0x8 &&
                    device.stretches.back().source_rect.x == 0 && device.stretches.back().source_rect.y == 0 &&
                    device.stretches.back().source_rect.z == 1 && device.stretches.back().source_rect.w == 1 &&
                    device.stretches.back().destination_rect.x == 0 && device.stretches.back().destination_rect.y == 0 &&
                    device.stretches.back().destination_rect.z == 64 && device.stretches.back().destination_rect.w == 32,
                    "GS actual first display hit preserves only alpha while repopulating frame RGB");
            }
            cache->RemoveAll(true, true, true);
            check(display.LookupDisplayTarget(tex0, size, 1.0f, false) == nullptr,
                "GS actual warm cache purge does not preload an unproduced display buffer");
            display.NoteColdStart();
            GSState::GSUploadQueue clear = {};
            clear.blit.DBP = tex0.TBP0;
            clear.blit.DBW = 1;
            clear.blit.DPSM = PSMCT32;
            clear.rect = area;
            clear.draw = GSState::s_n;
            clear.transfer_type = GSState::Clear;
            clear.zero_clear = true;
            renderer.m_draw_transfers.assign(1, clear);
            check(display.LookupDisplayTarget(tex0, size, 1.0f, false) == nullptr,
                "GS actual cold-start display respects a current explicit clear marker");
            renderer.m_draw_transfers.clear();
            cache->RemoveAll(true, true, true);
        }
        else
            check(false, "GS cache cold-start display lifecycle contract is present");
    }(*cache);
    {
        cache->RemoveAll(true, true, true);
        tex0 = GIFRegTEX0{};
        tex0.TBP0 = 0x1800;
        tex0.TBW = 1;
        tex0.PSM = PSMCT32;
        GSState::GSUploadQueue frame_upload = {};
        frame_upload.blit.DBP = tex0.TBP0;
        frame_upload.blit.DBW = 1;
        frame_upload.blit.DPSM = PSMCT32;
        frame_upload.rect = area;
        frame_upload.draw = GSState::s_n;
        frame_upload.transfer_type = GSState::EE_to_GS;
        renderer.m_draw_transfers.assign(1, frame_upload);
        auto* uploaded = cache->LookupDisplayTarget(tex0, size, 1.0f, false);
        check(uploaded && uploaded->m_valid_rgb && uploaded->m_valid_alpha_low && uploaded->m_valid_alpha_high,
            "GS actual exact uploaded display frame retains RGB and alpha authority for overlays");
        check(renderer.m_draw_transfers.size() == 1,
            "GS actual repeated display lookup retains exact upload evidence until normal pruning");
        renderer.m_draw_transfers.clear();
        cache->RemoveAll(true, true, true);
    }
    for (const float original_scale : {1.5f, 0.75f, 2.0f})
    {
        cache->RemoveAll(true, true, true);
        renderer.m_draw_transfers.clear();
        tex0 = GIFRegTEX0{};
        tex0.TBP0 = 0x1000;
        tex0.TBW = 1;
        tex0.PSM = PSMCT32;
        auto* target = cache->AddFixtureTarget(tex0, size, area, original_scale);
        const u32 before_downsample = device.downsample_calls;
        device.stretches.clear();
        device.allow_downsample = device.allow_stretch = true;
        auto* resized = cache->LookupTarget(tex0, size, 1.0f, GSTextureCache::RenderTarget,
            true, 0, false, false, true, true, area);
        device.allow_downsample = device.allow_stretch = false;
        check(resized == target && target->m_texture->GetSize() == size,
            "GS actual target rescale reaches the requested native physical extent");
        check(original_scale == 2.0f ?
            device.downsample_calls == before_downsample + 1 && device.stretches.empty() :
            device.downsample_calls == before_downsample && device.stretches.size() == 1,
            original_scale == 2.0f ? "GS actual integer target downscale retains the box-filter control" :
                "GS actual fractional and sub-native target rescale avoids an integer box filter");
        cache->RemoveAll(true, true, true);
    }
    RunGSLocalMemoryParity(renderer, check);
    {
        BlockHashState hasher;
        [&]<typename State>(State& stream) {
            if constexpr (requires { stream.pending; stream.state; })
            {
                alignas(32) u8 blocks[4][GS_BLOCK_SIZE];
                std::vector<u8> flat;
                for (u32 b = 0; b < 4; ++b)
                    for (u32 i = 0; i < GS_BLOCK_SIZE; ++i)
                        blocks[b][i] = static_cast<u8>(b * 39 + i * 17);
                BlockHashReset(stream);
                for (u32 b : {2u, 0u, 3u, 1u})
                {
                    BlockHashAccumulate(stream, blocks[b]);
                    flat.insert(flat.end(), blocks[b], blocks[b] + GS_BLOCK_SIZE);
                }
                check(stream.pending == blocks[1],
                    "GS actual scattered-block hasher retains the final block without a buffer copy");
                check(FinishBlockHash(stream) == XXH3_64bits(flat.data(), flat.size()),
                    "GS actual scattered GS-block stream matches independent one-shot XXH3");
                const u8 tail[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
                BlockHashAccumulate(stream, tail, sizeof(tail));
                flat.insert(flat.end(), std::begin(tail), std::end(tail));
                check(FinishBlockHash(stream) == XXH3_64bits(flat.data(), flat.size()),
                    "GS actual block stream keeps identity through digest and a mixed-size tail");
                BlockHashReset(stream);
                flat.assign(blocks[0], blocks[0] + GS_BLOCK_SIZE);
                BlockHashAccumulate(stream, blocks[0], GS_BLOCK_SIZE);
                std::memset(blocks[0], 0xee, GS_BLOCK_SIZE);
                check(FinishBlockHash(stream) == XXH3_64bits(flat.data(), flat.size()),
                    "GS actual sized update copies temporary masked-block bytes before reuse");
            }
            else
                check(false, "GS scattered-block zero-copy hash stream contract is present");
        }(hasher);
    }
    {
        cache->RemoveAll(true, true, true);
        renderer.m_draw_transfers.clear();
        tex0 = GIFRegTEX0{};
        tex0.TBP0 = 0x1800;
        tex0.TBW = 2;
        tex0.PSM = PSMCT32;
        auto* target = cache->AddFixtureTarget(tex0, GSVector2i(128, 64), GSVector4i(0, 0, 128, 64));
        cache->InvalidateVideoMem(renderer.m_mem.GetOffset(tex0.TBP0 + 32, 1, PSMCT32), area);
        check(cache->HasFixtureTarget(target) && target->m_dirty.GetTotalRect(tex0, GSVector2i(128, 64)).eq(GSVector4i(64, 0, 128, 32)),
            "GS actual differing-pitch write dirties only its physical page inside a wider target");
        cache->RemoveAll(true, true, true);
        tex0.TBW = 1;
        tex0.PSM = PSMZ32;
        target = cache->AddFixtureTarget(tex0, size, area, 1.0f, GSTextureCache::DepthStencil);
        cache->InvalidateVideoMem(renderer.m_mem.GetOffset(tex0.TBP0, 1, PSMCT32), GSVector4i(0, 0, 8, 8));
        check(cache->HasFixtureTarget(target) && target->m_dirty.GetTotalRect(tex0, size).eq(GSVector4i(32, 16, 40, 24)),
            "GS actual C32 block write maps to the literal Z32 XOR quadrant");
        cache->RemoveAll(true, true, true);
        tex0.PSM = PSMCT32;
        target = cache->AddFixtureTarget(tex0, size, area);
        const auto off = renderer.m_mem.GetOffset(tex0.TBP0, 1, PSMCT32);
        for (const GSVector4i& piece : {GSVector4i(0, 0, 32, 32), GSVector4i(32, 0, 64, 16),
             GSVector4i(32, 24, 64, 32), GSVector4i(40, 16, 64, 24)})
            cache->InvalidateVideoMem(off, piece);
        check(cache->HasFixtureTarget(target),
            "GS actual tiled dirty union retains the target while one block is clean");
        cache->InvalidateVideoMem(off, GSVector4i(32, 16, 40, 24));
        check(!cache->HasFixtureTarget(target),
            "GS actual cached dirty union retires its target when the final clean block is overwritten");
        cache->RemoveAll(true, true, true);
    }
    {
        // Exercise LookupTarget's actual depth rescale/reinterpret/repopulation roads.
        // The recorded selector must describe a depth attachment, not an R32F colour
        // attachment sharing the same conversion entry point.
        const auto saved_download = GSConfig.HWDownloadMode;
        const bool saved_stretch = device.allow_stretch;
        const bool saved_copy = device.allow_copy;
        const ScopedGuard restore_depth_copy([&]() {
            cache->RemoveAll(true, true, true);
            GSConfig.HWDownloadMode = saved_download;
            device.allow_stretch = saved_stretch;
            device.allow_copy = saved_copy;
        });
        GSConfig.HWDownloadMode = GSHardwareDownloadMode::Disabled;
        device.allow_stretch = device.allow_copy = true;
        struct DepthLookupCase { u32 old_psm, new_psm; float scale; bool retained_source; ShaderConvert expected; };
        const DepthLookupCase cases[] = {
            {PSMZ32, PSMZ32, 2.0f, false, ShaderConvert::DEPTH_COPY},
            {PSMZ32, PSMZ24, 1.0f, false, ShaderConvert::DEPTH32_TO_DEPTH24},
            {PSMZ32, PSMZ16, 1.0f, false, ShaderConvert::DEPTH_COPY},
            {PSMZ16, PSMZ32, 1.0f, false, ShaderConvert::DEPTH_COPY},
            {PSMZ32, PSMZ16, 1.0f, true, ShaderConvert::DEPTH_COPY},
        };
        for (const auto& c : cases)
        {
            cache->RemoveAll(true, true, true);
            renderer.m_draw_transfers.clear();
            GIFRegTEX0 old_tex = {};
            old_tex.TBP0 = 0x1000;
            old_tex.TBW = 1;
            old_tex.PSM = c.old_psm;
            const GSVector4i valid = c.old_psm == PSMZ32 && c.new_psm == PSMZ16 ?
                GSVector4i(0, 0, 64, 16) : area;
            auto* target = cache->AddFixtureTarget(old_tex, size, valid, 1.0f, GSTextureCache::DepthStencil);
            std::unique_ptr<GSTextureCache::Source> source;
            if (c.retained_source)
            {
                source = std::make_unique<GSTextureCache::Source>(old_tex, GIFRegTEXA{});
                source->m_texture = target->m_texture;
                source->m_from_target = target;
                source->m_target = source->m_target_direct = source->m_shared_texture = true;
            }
            GIFRegTEX0 requested = old_tex;
            requested.PSM = c.new_psm;
            device.stretches.clear();
            auto* selected = cache->LookupTarget(requested, size, c.scale, GSTextureCache::DepthStencil,
                true, 0, false, false, true, true, area, false, false, false, source.get());
            const bool reached = selected == target && device.stretches.size() == 1;
            check(reached, "GS actual LookupTarget depth rescale and format roads emit one captured conversion");
            const bool depth = reached && device.stretches[0].shader == c.expected &&
                device.stretches[0].destination == target->m_texture && target->m_texture->IsDepthStencil() &&
                device.stretches[0].depth_output && device.stretches[0].float32_output;
            if (!depth) std::fprintf(stderr, "GS_DEPTH_LOOKUP old=%u new=%u scale=%.1f retained=%u stretches=%zu depth=%u shader=%u\n",
                c.old_psm, c.new_psm, static_cast<double>(c.scale), u32(c.retained_source), device.stretches.size(),
                device.stretches.empty() ? 0u : u32(device.stretches[0].depth_output),
                device.stretches.empty() ? 0u : u32(device.stretches[0].shader));
            check(depth, "GS actual LookupTarget depth conversions select depth output rather than an R32F colour pipeline");
            source.reset();
        }
        cache->RemoveAll(true, true, true);
        GIFRegTEX0 depth_tex = {};
        depth_tex.TBP0 = 0x1000; depth_tex.TBW = 1; depth_tex.PSM = PSMZ32;
        auto* depth_target = cache->AddFixtureTarget(depth_tex, size, area, 1.0f, GSTextureCache::DepthStencil);
        depth_target->m_valid_rgb = false;
        GIFRegTEX0 color_tex = depth_tex; color_tex.PSM = PSMCT32;
        cache->AddFixtureTarget(color_tex, size, area);
        device.stretches.clear();
        auto* repopulated = cache->LookupTarget(depth_tex, size, 1.0f, GSTextureCache::DepthStencil,
            true, 0, false, false, true, true, area);
        check(repopulated == depth_target && device.stretches.size() == 1 &&
            device.stretches[0].shader == ShaderConvert::RGBA8_TO_DEPTH32 &&
            device.stretches[0].depth_output && device.stretches[0].float32_output,
            "GS actual LookupTarget RGB repopulation routes its generated colour-to-depth conversion to the depth attachment");
    }
    {
        const auto saved_download = GSConfig.HWDownloadMode;
        const bool saved_stretch = device.allow_stretch;
        const ScopedGuard restore_depth_followups([&]() {
            cache->RemoveAll(true, true, true);
            GSConfig.HWDownloadMode = saved_download;
            device.allow_stretch = saved_stretch;
        });
        GSConfig.HWDownloadMode = GSHardwareDownloadMode::Disabled;
        device.allow_stretch = true;
        for (const int type : {GSTextureCache::RenderTarget, GSTextureCache::DepthStencil})
        {
            cache->RemoveAll(true, true, true);
            renderer.m_draw_transfers.clear();
            GIFRegTEX0 requested = {};
            requested.TBP0 = 0x1000; requested.TBW = 1;
            requested.PSM = type == GSTextureCache::RenderTarget ? PSMCT32 : PSMZ32;
            auto* target = cache->AddFixtureTarget(requested, size, area, 1.0f, type);
            target->m_valid_rgb = false;
            device.stretches.clear();
            auto* selected = cache->LookupTarget(requested, size, 1.0f, type,
                true, 0, false, false, true, true, area);
            const bool depth = type == GSTextureCache::DepthStencil;
            check(selected == target && device.stretches.size() == 1 &&
                device.stretches[0].shader == (depth ? ShaderConvert::DEPTH_COPY : ShaderConvert::COPY) &&
                device.stretches[0].depth_output == depth && device.stretches[0].float32_output == depth &&
                device.stretches[0].mask == (depth ? 0xf : 0x8),
                depth ? "GS actual LookupTarget missing-RGB fallback preserves its depth attachment selector" :
                    "GS actual LookupTarget missing-RGB colour fallback retains its alpha-only colour mask");
        }
        for (const int type : {GSTextureCache::RenderTarget, GSTextureCache::DepthStencil})
        {
            cache->RemoveAll(true, true, true);
            renderer.m_draw_transfers.clear();
            GIFRegTEX0 src_tex = {};
            src_tex.TBP0 = 0x1000; src_tex.TBW = 1;
            src_tex.PSM = type == GSTextureCache::RenderTarget ? PSMCT32 : PSMZ32;
            GIFRegTEX0 dst_tex = src_tex; dst_tex.TBP0 = 0x1800;
            cache->AddFixtureTarget(src_tex, size, area, 1.0f, type);
            cache->AddFixtureTarget(dst_tex, size, area, 1.0f, type);
            device.batches.clear();
            const bool moved = cache->PageMove(src_tex.TBP0, dst_tex.TBP0, 1, src_tex.PSM, 0, 0, 0, 0, 64, 32);
            const bool depth = type == GSTextureCache::DepthStencil;
            check(moved && device.batches.size() == 1 &&
                device.batches[0].shader == (depth ? ShaderConvert::DEPTH_COPY : ShaderConvert::COPY) &&
                device.batches[0].depth_output == depth && device.batches[0].float32_output == depth &&
                device.batches[0].mask == 0xf,
                depth ? "GS actual depth PageMove CopyPages selects depth-copy output for its generated batch" :
                    "GS actual colour PageMove CopyPages retains the RGBA colour-copy control");
        }
    }
    TestTextureReplacementParity(*cache,device,renderer,check);
    check(device.unexpected_calls == 0,
        "GS target coherence executes production upload/preload decisions without GPU/readback/present calls");
}
