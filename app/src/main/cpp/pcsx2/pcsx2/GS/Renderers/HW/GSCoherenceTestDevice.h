// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once
// CPU-only device shared by native target coherence and hazard fixtures.
#include "GS/Renderers/Common/GSDevice.h"
#include <cstring>
#include <cstdio>
#include <vector>

namespace
{
class CoherenceTexture final : public GSTexture
{
public:
    std::vector<u32> pixels;
    u32 uploads = 0;

    CoherenceTexture(Usage usage, int width, int height, int levels, Format format)
        : pixels(static_cast<size_t>(width) * height, 0xcdcdcdcd)
    {
        m_size = GSVector2i(width, height);
        m_usage = usage;
        m_format = format;
        m_mipmap_levels = levels;
    }
    void* GetNativeHandle() const override { return nullptr; }
    bool DoUpdate(const GSVector4i& r, const void* data, int pitch, int) override
    {
        if (r.x < 0 || r.y < 0 || r.z > m_size.x || r.w > m_size.y)
            return false;
        ++uploads;
        for (int y = 0; y < r.height(); ++y)
            std::memcpy(pixels.data() + static_cast<size_t>(r.y + y) * m_size.x + r.x,
                static_cast<const u8*>(data) + static_cast<size_t>(y) * pitch, r.width() * sizeof(u32));
        return true;
    }
    bool DoMap(GSMap&, const GSVector4i*, int) override { return false; }
    void Unmap() override {}
    void GenerateMipmap() override {}
#ifdef PCSX2_DEVBUILD
    void SetDebugName(std::string_view) override {}
#endif
};

struct CoherenceReadbackStats
{
    u32 allocations = 0, copies = 0, flushes = 0;
    bool ready = true;
    GSTexture::Format last_copy_format = GSTexture::Format::Invalid;
};
class CoherenceDownload final : public GSDownloadTexture
{
    CoherenceReadbackStats& stats;
    std::vector<u8> bytes;
public:
    CoherenceDownload(u32 w, u32 h, GSTexture::Format f, CoherenceReadbackStats& s)
        : GSDownloadTexture(w, h, f), stats(s), bytes(static_cast<size_t>(w) * h * (f == GSTexture::Format::UInt16 ? 2 : 4)) {}
    void DoCopyFromTexture(const GSVector4i& dst, GSTexture* texture, const GSVector4i& source, u32, bool) override
    {
        ++stats.copies;
        stats.last_copy_format = m_format;
        const auto* tex = static_cast<const CoherenceTexture*>(texture);
        const u32 bpp = m_format == GSTexture::Format::UInt16 ? 2 : 4;
        m_current_pitch = m_width * bpp;
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x)
            {
                const u32 word = tex->pixels[(source.y + y) * tex->GetWidth() + source.x + x];
                std::memcpy(bytes.data() + (dst.y + y) * m_current_pitch + (dst.x + x) * bpp, &word, bpp);
            }
        m_needs_flush = true;
    }
    bool Map(const GSVector4i&) override { m_map_pointer = bytes.data(); return true; }
    void Unmap() override { m_map_pointer = nullptr; }
    void Flush() override { ++stats.flushes; m_needs_flush = false; }
    bool Poll() override { return stats.ready || !m_needs_flush; }
#ifdef PCSX2_DEVBUILD
    void SetDebugName(std::string_view) override {}
#endif
};
class CoherenceDevice final : public GSDevice
{
public:
    struct Batch
    {
        std::vector<u32> pixels;
        GSVector4 source_rect;
        GSVector4 destination_rect;
        GSVector2i source_size;
        u8 mask;
        ShaderConvert shader;
        Filter filter;
        bool depth_output;
        bool float32_output;
    };
    std::vector<Batch> batches;
    struct Stretch
    {
        GSTexture* source;
        GSTexture* destination;
        GSVector4 source_rect;
        GSVector4 destination_rect;
        ShaderConvert shader;
        u8 mask;
        bool depth_output;
        bool float32_output;
    };
    std::vector<Stretch> stretches;
    u32 allocations = 0;
    u32 unexpected_calls = 0;
    u32 clut_updates = 0;
    bool allow_clut = false;
    bool allow_stretch = false;
    bool allow_downsample = false;
    u32 downsample_calls = 0;
    bool allow_readback = false;
    CoherenceReadbackStats readbacks;
    bool allow_copy = false;
    u32 copies = 0;
    u32 invalid_copies = 0;

    void Unexpected(const char* method) { ++unexpected_calls; std::fprintf(stderr, "GS coherence unexpected %s\n", method); }

    CoherenceDevice() { m_max_texture_size = 2048; }
    void SetASTCCap(bool enabled) { m_features.astc_textures=enabled; }
    template<class Facts=FeatureSupport> void SetStencilCounterCap(bool enabled) {
        if constexpr(requires(Facts& f){f.stencil_counter;}) static_cast<Facts&>(m_features).stencil_counter=enabled;
    }
    void SetDepthQuantizationOptOut(bool enabled) { m_features.no_ps2_z_quantization=enabled; }
    GSTexture* CreateSurface(GSTexture::Usage usage, int width, int height, int levels, GSTexture::Format format) override
    {
        ++allocations;
        return new CoherenceTexture(usage, width, height, levels, format);
    }
    void DrawMultiStretchRects(const MultiStretchRect* rects, u32 count, GSTexture*, ShaderConvertSelector shader) override
    {
        for (u32 i = 0; i < count; ++i)
        {
            const auto* source = static_cast<const CoherenceTexture*>(rects[i].src);
            batches.push_back({source->pixels, rects[i].src_rect, rects[i].dst_rect,
                source->GetSize(), static_cast<u8>(rects[i].wmask.wrgba), shader.Shader(), rects[i].filter, shader.DepthOutput(), shader.Float32Output()});
        }
    }
    void DoMerge(GSTexture*[], GSVector4*, GSTexture*, GSVector4*, const GSRegPMODE&, const GSRegEXTBUF&, u32, Filter) override { Unexpected(__func__); }
    void DoInterlace(GSTexture*, const GSVector4&, GSTexture*, const GSVector4&, ShaderInterlace, Filter, const InterlaceConstantBuffer&) override { Unexpected(__func__); }
    void DoFXAA(GSTexture*, GSTexture*) override { Unexpected(__func__); }
    void DoShadeBoost(GSTexture*, GSTexture*, const float[4]) override { Unexpected(__func__); }
    bool DoCAS(GSTexture*, GSTexture*, bool, const std::array<u32, NUM_CAS_CONSTANTS>&) override { Unexpected(__func__); return false; }
    void DoRenderHW(GSHWDrawConfig&) override { Unexpected(__func__); }
    void DoStretchRect(GSTexture* source, const GSVector4& source_rect, GSTexture* destination,
        const GSVector4& destination_rect, ShaderConvertSelector shader, Filter) override
    {
        stretches.push_back({source, destination, source_rect, destination_rect, shader.Shader(), shader.Mask(), shader.DepthOutput(), shader.Float32Output()});
        if (allow_readback)
        {
            const auto* src = static_cast<const CoherenceTexture*>(source);
            auto* dst = static_cast<CoherenceTexture*>(destination);
            const int sx = static_cast<int>(source_rect.x * src->GetWidth());
            const int sy = static_cast<int>(source_rect.y * src->GetHeight());
            for (int y = 0; y < dst->GetHeight(); ++y)
                for (int x = 0; x < dst->GetWidth(); ++x)
                {
                    const u32 c = src->pixels[(sy + y) * src->GetWidth() + sx + x];
                    dst->pixels[y * dst->GetWidth() + x] = shader.Shader() == ShaderConvert::RGB5A1_TO_16_BITS ?
                        ((c >> 3) & 31) | ((c >> 6) & 0x3e0) | ((c >> 9) & 0x7c00) | ((c >> 16) & 0x8000) : c;
                }
        }
        if (!allow_stretch)
            Unexpected(__func__);
    }
    RenderAPI GetRenderAPI() const override { return RenderAPI::None; }
    bool HasSurface() const override { return false; }
    void DestroySurface() override {}
    bool UpdateWindow() override { return false; }
    void ResizeWindow(u32, u32, float) override {}
    bool SupportsExclusiveFullscreen() const override { return false; }
    PresentResult BeginPresent(bool) override { Unexpected(__func__); return PresentResult::FrameSkipped; }
    void EndPresent() override { Unexpected(__func__); }
    void SetVSyncMode(GSVSyncMode, bool) override {}
    std::string GetDriverInfo() const override { return "CPU target coherence fixture"; }
    bool SetGPUTimingEnabled(bool) override { return false; }
    float GetAndResetAccumulatedGPUTime() override { return 0.0f; }
    bool SetGPUPipelineStatisticsEnabled(bool) override { return false; }
    GPUPipelineStatistics GetAndResetAccumulatedGPUPipelineStatistics() override { return {}; }
    void PushDebugGroup(const char*, ...) override {}
    void PopDebugGroup() override {}
    void InsertDebugMessage(DebugMessageCategory, const char*, ...) override {}
    std::unique_ptr<GSDownloadTexture> CreateDownloadTexture(u32 w, u32 h, GSTexture::Format f) override
    {
        if (!allow_readback) { Unexpected(__func__); return nullptr; }
        ++readbacks.allocations;
        return std::make_unique<CoherenceDownload>(w, h, f, readbacks);
    }
    void CopyRect(GSTexture* source, GSTexture* destination, const GSVector4i& rect, u32 dx, u32 dy) override
    {
        ++copies;
        if (!allow_copy) { Unexpected(__func__); return; }
        auto* src = static_cast<CoherenceTexture*>(source);
        auto* dst = static_cast<CoherenceTexture*>(destination);
        if (rect.x < 0 || rect.y < 0 || rect.z > src->GetWidth() || rect.w > src->GetHeight() ||
            static_cast<s64>(dx) + rect.width() > dst->GetWidth() || static_cast<s64>(dy) + rect.height() > dst->GetHeight())
        { ++invalid_copies; return; }
        for (int y = 0; y < rect.height(); ++y)
            std::copy_n(src->pixels.data() + (rect.y + y) * src->GetWidth() + rect.x, rect.width(),
                dst->pixels.data() + (dy + y) * dst->GetWidth() + dx);
    }
    void PresentRect(GSTexture*, const GSVector4&, GSTexture*, const GSVector4&, PresentShader, float, Filter) override { Unexpected(__func__); }
    void UpdateCLUTTexture(GSTexture*, float, u32, u32, GSTexture*, u32, u32) override { ++clut_updates; if (!allow_clut) Unexpected(__func__); }
    void ConvertToIndexedTexture(GSTexture*, float, u32, u32, u32, u32, GSTexture*, u32, u32) override { Unexpected(__func__); }
    void FilteredDownsampleTexture(GSTexture*, GSTexture*, u32, const GSVector2i&, const GSVector4&) override { ++downsample_calls; if (!allow_downsample) Unexpected(__func__); }
    void ClearSamplerCache() override {}
};

}
