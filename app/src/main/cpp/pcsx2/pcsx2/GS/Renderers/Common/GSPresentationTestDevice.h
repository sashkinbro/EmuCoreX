// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once
// CPU-only device shared by native target coherence and hazard fixtures.
#include "GS/Renderers/Common/GSDevice.h"
#include <cstring>
#include <vector>

namespace
{
class PresentationTexture final : public GSTexture
{
public:
    std::vector<u32> pixels;
    u32 uploads = 0;

    PresentationTexture(Usage usage, int width, int height, int levels, Format format)
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

class PresentationDevice : public GSDevice
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
    };
    std::vector<Batch> batches;
    struct Stretch
    {
        GSTexture* source;
        GSTexture* destination;
        GSVector4 source_rect;
        GSVector4 destination_rect;
    };
    std::vector<Stretch> stretches;
    u32 allocations = 0;
    u32 unexpected_calls = 0;
    bool allow_stretch = false;
    GSVector4 merged_src[2], merged_dst[2];
    MergeTopBand merged_band[2]{};
    GSVector4 field_pad=GSVector4::zero();
    std::vector<ShaderInterlace> interlace_shaders;
    template<class T> static bool SetBrokenMAD(T& features, bool value)
    {
        if constexpr (requires { features.broken_mad_deinterlace; })
        { features.broken_mad_deinterlace = value; return true; }
        return !value;
    }
    void BrokenMAD(bool value) { SetBrokenMAD(m_features, value); }

    PresentationDevice() { m_max_texture_size = 2048; }
    GSTexture* CreateSurface(GSTexture::Usage usage, int width, int height, int levels, GSTexture::Format format) override
    {
        ++allocations;
        return new PresentationTexture(usage, width, height, levels, format);
    }
    void DrawMultiStretchRects(const MultiStretchRect* rects, u32 count, GSTexture*, ShaderConvertSelector shader) override
    {
        for (u32 i = 0; i < count; ++i)
        {
            const auto* source = static_cast<const PresentationTexture*>(rects[i].src);
            batches.push_back({source->pixels, rects[i].src_rect, rects[i].dst_rect,
                source->GetSize(), static_cast<u8>(rects[i].wmask.wrgba), shader.Shader()});
        }
    }
    void DoMerge(GSTexture*[], GSVector4* src, GSTexture*, GSVector4* dst, const GSRegPMODE&, const GSRegEXTBUF&, u32, Filter) override
    { for (int i=0;i<2;i++) { merged_src[i]=src[i]; merged_dst[i]=dst[i]; } }
    void DoMerge(GSTexture* tex[], GSVector4* src, GSTexture* target, GSVector4* dst, const MergeTopBand* bands, const GSRegPMODE& pmode, const GSRegEXTBUF& ext, u32 c, Filter filter) override
    { DoMerge(tex,src,target,dst,pmode,ext,c,filter); for(int i=0;i<2;i++)merged_band[i]=bands?bands[i]:MergeTopBand{}; }
    template<class T> static GSVector4 FieldPadOf(const T& cb) {
        if constexpr(requires { cb.FieldPad; }) return cb.FieldPad;
        return GSVector4::zero();
    }
    void DoInterlace(GSTexture*, const GSVector4&, GSTexture*, const GSVector4&, ShaderInterlace shader, Filter, const InterlaceConstantBuffer& cb) override
    { interlace_shaders.push_back(shader);field_pad=FieldPadOf(cb); }
    void DoFXAA(GSTexture*, GSTexture*) override { ++unexpected_calls; }
    void DoShadeBoost(GSTexture*, GSTexture*, const float[4]) override { ++unexpected_calls; }
    bool DoCAS(GSTexture*, GSTexture*, bool, const std::array<u32, NUM_CAS_CONSTANTS>&) override { ++unexpected_calls; return false; }
    void DoRenderHW(GSHWDrawConfig&) override { ++unexpected_calls; }
    void DoStretchRect(GSTexture* source, const GSVector4& source_rect, GSTexture* destination,
        const GSVector4& destination_rect, ShaderConvertSelector, Filter) override
    {
        stretches.push_back({source, destination, source_rect, destination_rect});
        if (!allow_stretch)
            ++unexpected_calls;
    }
    RenderAPI GetRenderAPI() const override { return RenderAPI::None; }
    bool HasSurface() const override { return false; }
    void DestroySurface() override {}
    bool UpdateWindow() override { return false; }
    void ResizeWindow(u32, u32, float) override {}
    bool SupportsExclusiveFullscreen() const override { return false; }
    PresentResult BeginPresent(bool) override { ++unexpected_calls; return PresentResult::FrameSkipped; }
    void EndPresent() override { ++unexpected_calls; }
    void SetVSyncMode(GSVSyncMode, bool) override {}
    std::string GetDriverInfo() const override { return "CPU target coherence fixture"; }
    bool SetGPUTimingEnabled(bool) override { return false; }
    float GetAndResetAccumulatedGPUTime() override { return 0.0f; }
    bool SetGPUPipelineStatisticsEnabled(bool) override { return false; }
    GPUPipelineStatistics GetAndResetAccumulatedGPUPipelineStatistics() override { return {}; }
    void PushDebugGroup(const char*, ...) override {}
    void PopDebugGroup() override {}
    void InsertDebugMessage(DebugMessageCategory, const char*, ...) override {}
    std::unique_ptr<GSDownloadTexture> CreateDownloadTexture(u32, u32, GSTexture::Format) override { ++unexpected_calls; return nullptr; }
    void CopyRect(GSTexture*, GSTexture*, const GSVector4i&, u32, u32) override { ++unexpected_calls; }
    void PresentRect(GSTexture*, const GSVector4&, GSTexture*, const GSVector4&, PresentShader, float, Filter) override { ++unexpected_calls; }
    void UpdateCLUTTexture(GSTexture*, float, u32, u32, GSTexture*, u32, u32) override { ++unexpected_calls; }
    void ConvertToIndexedTexture(GSTexture*, float, u32, u32, u32, u32, GSTexture*, u32, u32) override { ++unexpected_calls; }
    void FilteredDownsampleTexture(GSTexture*, GSTexture*, u32, const GSVector2i&, const GSVector4&) override { ++unexpected_calls; }
    void ClearSamplerCache() override {}
};

}
