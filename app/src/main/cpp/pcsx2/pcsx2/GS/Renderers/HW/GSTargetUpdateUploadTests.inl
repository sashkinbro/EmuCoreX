// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Native self-tests: actual GSLocalMemory decoder and production strip helper.
namespace
{
class TargetUploadCapture final : public GSTexture
{
public:
    std::vector<u8> bytes;
    u32 pitch, uploads = 0, maps = 0, reject_at = 0;
    TargetUploadCapture(int width, int height)
        : bytes(static_cast<size_t>(Common::AlignUpPow2(width * 4, 64)) * height, 0xcd),
          pitch(Common::AlignUpPow2(width * 4, 64))
    {
        m_size = GSVector2i(width, height);
        m_mipmap_levels = 1;
        m_format = Format::Color;
    }
    void* GetNativeHandle() const override { return nullptr; }
    bool DoUpdate(const GSVector4i& r, const void* data, int source_pitch, int) override
    {
        if (++uploads == reject_at) return false;
        for (int y = 0; y < r.height(); ++y)
            std::memcpy(bytes.data() + static_cast<size_t>(r.y + y) * pitch + r.x * 4,
                static_cast<const u8*>(data) + static_cast<size_t>(y) * source_pitch, r.width() * 4);
        return true;
    }
    bool DoMap(GSMap&, const GSVector4i*, int) override { ++maps; return false; }
    void Unmap() override {}
    void GenerateMipmap() override {}
#ifdef PCSX2_DEVBUILD
    void SetDebugName(std::string_view) override {}
#endif
};
}

extern "C" void EmuCoreXOracleTestTargetScratchUpload(void (*check)(bool, const char*))
{
    auto memory = std::make_unique<GSLocalMemory>();
    u32 random = 0x571312a9;
    for (u32 i = 0; i < GSLocalMemory::m_vmsize; ++i)
    {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        memory->vm8()[i] = static_cast<u8>(random);
    }
    GIFRegTEXA texa = {};
    texa.TA1 = 0x80;
    for (u32 psm : {PSMCT32, PSMCT24, PSMCT16, PSMCT16S, PSMZ32, PSMZ24, PSMZ16, PSMZ16S})
        for (u32 kind = 0; kind < 4; ++kind)
            for (bool alpha : {false, true})
    {
        const GSVector4i source = kind == 0 ? GSVector4i(16, 8, 80, 40) :
            kind == 1 ? GSVector4i(3, 1, 71, 37) : kind == 2 ? GSVector4i(0, 0, 2048, 1536) : GSVector4i(7, 5, 8, 6);
        const GSVector4i destination(4, 3, source.width() + 4, source.height() + 3);
        const GSOffset off(memory->GetOffset(0x3ff0, 32, psm)); // exercise GS memory wrap too
        TargetUploadCapture captured(source.width() + 16, source.height() + 16);
        std::vector<u8> expected = captured.bytes;
        memory->ReadTexture(off, source, expected.data() + destination.y * captured.pitch + destination.x * 4,
            captured.pitch, texa);
        const u32 pitch = VectorAlign(source.width() * sizeof(u32));
        const size_t budget = kind == 1 ? 4096 : UNSWIZZLE_BUFFER_SIZE;
        u8* scratch = static_cast<u8*>(_aligned_malloc(budget + 64, VECTOR_ALIGNMENT));
        std::memset(scratch, 0xee, budget + 64);
        u64 decoded = 0;
        u32 decodes = 0;
        std::pair<u8, u8> range{255, 0}, expected_range{255, 0};
        const bool scan = (psm & 0xf) != PSMCT24 && alpha && GSLocalMemory::m_psm[psm].bpp >= 16;
        if (scan)
            for (int y = destination.y; y < destination.w; ++y)
                for (int x = destination.x; x < destination.z; ++x)
                {
                    const u8 a = expected[static_cast<size_t>(y) * captured.pitch + x * 4 + 3];
                    expected_range.first = std::min(expected_range.first, a);
                    expected_range.second = std::max(expected_range.second, a);
                }
        const bool uploaded = GSUploadTargetRect(source, destination, scratch, budget, pitch,
            GSLocalMemory::m_psm[psm].bs.y,
            [&](const GSVector4i& r, u8* data, u32 row_pitch) {
                ++decodes;
                decoded += static_cast<u64>(r.width()) * r.height() * 4;
                memory->ReadTexture(off, r, data, row_pitch, texa);
            },
            [&](const u8* data, u32 width, u32 height, u32 row_pitch) {
                if (!scan) return;
                const auto part = GSGetRGBA8AlphaMinMax(data, width, height, row_pitch);
                range.first = std::min(range.first, part.first);
                range.second = std::max(range.second, part.second);
            },
            [&](const GSVector4i& r, const u8* data, u32 row_pitch) { return captured.Update(r, data, row_pitch); });
        std::memset(scratch, 0x42, budget); // no retained source pointers
        check(uploaded && captured.bytes == expected, "GS scratch upload matches direct decoder bytes, offsets and row padding");
        check(range == expected_range, "GS scratch scan preserves alpha extrema and local24-bit/dirty-channel policy");
        check(!captured.maps && captured.uploads == decodes && decoded == static_cast<u64>(source.width()) * source.height() * 4,
            "GS scratch upload decodes every texel exactly once without mapping upload memory");
        check(std::all_of(scratch + budget, scratch + budget + 64, [](u8 b) { return b == 0xee; }),
            "GS scratch upload keeps9MiB/tiny-budget guards intact");
        check(kind != 2 || decodes > 1, "GS large target update is split within the existing9MiB allocation");
        _aligned_free(scratch);
    }
    TargetUploadCapture rejected(64, 64);
    rejected.reject_at = 2;
    alignas(VECTOR_ALIGNMENT) u8 scratch[512];
    u32 decoded = 0;
    check(!GSUploadTargetRect(GSVector4i(0, 0, 32, 32), GSVector4i(0, 0, 32, 32), scratch, sizeof(scratch), 128, 8,
        [&](const GSVector4i&, u8* data, u32) { ++decoded; std::memset(data, 0, sizeof(scratch)); },
        [](const u8*, u32, u32, u32) {},
        [&](const GSVector4i& r, const u8* data, u32 pitch) { return rejected.Update(r, data, pitch); }) && decoded == 2,
        "GS target strip upload reports failure immediately for caller dirty-list retry");
}
