// SPDX-License-Identifier: GPL-3.0+
#if !defined(EMUCOREX_GS_ORACLE_DEBUG)
#error GS oracle must only be compiled by the Debug-only CMake gate
#endif
#include "Common.h"
#include "Config.h"
#include "Memory.h"
#include "GameDatabase.h"
#include "GS/GS.h"
#include "GS/GSLzma.h"
#include "GS/GSDump.h"
#include "GS/GSState.h"
#include "GS/GSPerfMon.h"
#include "GS/Renderers/Common/GSDevice.h"
#include "GS/Renderers/Common/GSRenderer.h"
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "common/Error.h"
#include "common/Console.h"
#include "common/Timer.h"
#include "cpuinfo.h"
#include "fmt/format.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr u32 Width = 64, Height = 64;
constexpr const char* Settings = "native-exact-v2-interlace-auto";
struct alignas(16) Qword { u64 lo, hi; };
using Writes = std::vector<Qword>;
std::vector<u8>* s_corpus = nullptr;
GSDumpBase* s_repro = nullptr;
void Transfer(const Qword* packet, size_t count)
{
    if (s_repro)
        s_repro->Transfer(3, reinterpret_cast<const u8*>(packet), count * sizeof(Qword));
    if (s_corpus)
    {
        const u64 size = count * sizeof(Qword);
        const auto* header = reinterpret_cast<const u8*>(&size);
        s_corpus->insert(s_corpus->end(), header, header + sizeof(size));
        const auto* data = reinterpret_cast<const u8*>(packet);
        s_corpus->insert(s_corpus->end(), data, data + size);
    }
    GSgifTransfer(reinterpret_cast<const u8*>(packet), static_cast<u32>(count));
}
struct Checkpoint
{
    u32 frame, packet, width, height;
    std::string name, kind, file;
    std::string format; // native buffer format read back (CT32/CT24/CT16/CT16S, Z32/Z24/Z16/Z16S); empty if n/a
};
struct Profile
{
    u32 frame, packet;
    std::string name;
    double submit_ms, wait_ms;
    std::array<double, GSPerfMon::CounterLast> counters;
};

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void Send(const Writes& writes)
{
    Require(!writes.empty() && writes.size() < 32768, "Invalid GIF write count");
    Writes packet;
    packet.reserve(writes.size() + 1);
    packet.push_back({static_cast<u64>(writes.size()) | (1ull << 15) | (1ull << 60), 0xe});
    packet.insert(packet.end(), writes.begin(), writes.end());
    Transfer(packet.data(), packet.size());
}

void Write(Writes& w, u64 reg, u64 value) { w.push_back({value, reg}); }
u64 XY(u32 x, u32 y, u32 z = 1) { return x * 16ull | (y * 16ull << 16) | (static_cast<u64>(z) << 32); }
void Sprite(Writes& w, u32 color, u32 x0, u32 y0, u32 x1, u32 y1, u32 z = 1, u64 flags = 0)
{
    Write(w, 0x00, 6 | flags); // PRIM sprite
    Write(w, 0x01, color | (0x3f800000ull << 32)); // RGBAQ
    Write(w, 0x05, XY(x0, y0, z));
    Write(w, 0x05, XY(x1, y1, z));
}

const std::array<const char*, 44> Names = {"sprites", "scissor", "frame-mask", "alpha-test", "alpha-fail-rgb",
    "depth-greater", "destination-alpha", "fba", "alpha-blend", "fixed-alpha-wrap", "triangle", "texture-upload",
    "points", "lines", "line-strip", "gouraud-triangle", "triangle-strip", "triangle-fan", "fog", "context-two",
    "bilinear", "texture-repeat", "region-clamp", "region-repeat", "texture-modulate", "texture-highlight",
    "texture-highlight2", "texture-rgb", "clut8-csm1", "clut4-csm1", "clut8-csm2", "clut4-csm2",
    "ct16-dither", "ct16s-dither", "local-copy", "overlap-copy", "ct24", "depth16", "depth24", "texture16-alpha",
    "perspective-stq", "clut8h", "clut4hl", "clut4hh"};

void Upload(Writes& w, u32 bp, u32 bw, u32 psm, u32 width, u32 height, const std::vector<u8>& bytes)
{
    Require(bytes.size() % 16 == 0, "Unaligned synthetic upload");
    Write(w, 0x50, static_cast<u64>(bp) << 32 | static_cast<u64>(bw) << 48 | static_cast<u64>(psm) << 56);
    Write(w, 0x51, 0); Write(w, 0x52, width | static_cast<u64>(height) << 32); Write(w, 0x53, 0);
    Send(w); w.clear();
    Writes image(1 + bytes.size()/16);
    image[0] = {bytes.size()/16 | 1ull << 15 | 2ull << 58, 0};
    std::memcpy(image.data()+1, bytes.data(), bytes.size());
    Transfer(image.data(), image.size());
}

// Native GIF/GS work, not shader approximations. Each fixture resets state and
// initializes both tested buffers, so a previous test cannot hide a missing draw.
void Prologue(Writes& w)
{
    Write(w, 0x1a, 1); // PRMODECONT
    Write(w, 0x18, 0); // XYOFFSET_1
    Write(w, 0x4c, 1ull << 16); // FRAME_1: base 0, FBW 1, CT32
    Write(w, 0x4e, 16); // ZBUF_1: base 131072, Z32, disjoint from framebuffer and texture
    Write(w, 0x40, 63ull << 16 | 63ull << 48); // SCISSOR_1
    Write(w, 0x47, 1ull << 16 | 1ull << 17); // ZTE + ALWAYS
    Write(w, 0x46, 1); // COLCLAMP
    Write(w, 0x45, 0); // DTHE
    Write(w, 0x49, 0); // FBA_1
    Write(w, 0x1b, 0); // PABE
    Write(w, 0x42, 0x44 | (0x40ull << 32)); // (Cs-Cd)*As+Cd
    Sprite(w, 0x80102030, 0, 0, Width, Height, 1);
}

Writes Scene(u32 scene, u32 seed)
{
    Writes w;
    Prologue(w);
    if (scene >= Names.size())
    {
        const u32 variant = scene - static_cast<u32>(Names.size());
        // Systematic selector matrices, then seeded geometry/colors. Every
        // combination is valid GS work; the exact stream is saved and hashed.
        u32 random = seed ^ (scene * 0x9e3779b9u);
        auto next = [&]() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
        switch ((variant / 32) % 8)
        {
            case 0: Write(w, 0x47, 1 | static_cast<u64>(variant % 8) << 1 | 0x80ull << 4 |
                static_cast<u64>((variant / 8) % 4) << 12 | 1ull << 16 | 1ull << 17); break;
            case 1: Write(w, 0x47, 1ull << 16 | static_cast<u64>(variant % 4) << 17);
                Write(w, 0x4e, 16 | static_cast<u64>((variant / 4) % 2) << 32); break;
            case 2: Write(w, 0x47, 1ull << 14 | static_cast<u64>(variant % 2) << 15 | 1ull << 16 | 1ull << 17);
                Write(w, 0x49, (variant / 2) % 2); break;
            case 3: Write(w, 0x4c, 1ull << 16 | static_cast<u64>(next()) << 32); break;
            case 4: Write(w, 0x46, variant % 2); Write(w, 0x1b, (variant / 2) % 2); break;
            case 5: Write(w, 0x40, 7ull | 53ull << 16 | 9ull << 32 | 51ull << 48); break;
            case 6: // CT24/CT16/CT16S and corresponding depth layouts, then raw VRAM readback.
            {
                constexpr std::array<u32, 4> psm{0, 1, 2, 10};
                Write(w, 0x4c, 1ull << 16 | static_cast<u64>(psm[variant % 4]) << 24);
                Write(w, 0x4e, 16 | static_cast<u64>(psm[(variant / 4) % 4]) << 24);
                break;
            }
            case 7: Write(w, 0x49, variant % 2); break;
        }
        // Across generated cases this cycles the complete 3^4 ALPHA selector space.
        const u32 blend = variant % 81;
        Write(w, 0x42, (blend % 3) | static_cast<u64>((blend / 3) % 3) << 2 |
            static_cast<u64>((blend / 9) % 3) << 4 | static_cast<u64>((blend / 27) % 3) << 6 |
            static_cast<u64>(std::array<u32, 4>{0, 0x40, 0x80, 0xff}[variant % 4]) << 32);
        for (u32 n = 0; n < 4; ++n)
        {
            const u32 x = 1 + next() % 30, y = 1 + next() % 30;
            const u32 color = (next() & 0xffffff) | (std::array<u32, 4>{0x00, 0x7f, 0x80, 0xff}[n] << 24);
            Sprite(w, color, x, y, x + 1 + next() % 30, y + 1 + next() % 30, n,
                (variant % 2) ? 1ull << 6 : 0);
        }
        return w;
    }
    if (scene == 1) // scissor edges
        Write(w, 0x40, 9ull | 47ull << 16 | 11ull << 32 | 39ull << 48);
    if (scene == 2) // preserve green/alpha bytes
        Write(w, 0x4c, (1ull << 16) | (0xff00ff00ull << 32));
    if (scene == 3) // alpha GREATER, fail KEEP
        Write(w, 0x47, 1 | 6ull << 1 | 0x80ull << 4 | 1ull << 16 | 1ull << 17);
    if (scene == 4) // alpha failure RGB_ONLY
        Write(w, 0x47, 1 | 0x80ull << 4 | 3ull << 12 | 1ull << 16 | 1ull << 17);
    if (scene == 5) // Z GREATER rejects same/lower and admits higher
        Write(w, 0x47, 1ull << 16 | 3ull << 17);
    if (scene == 6) // destination alpha test, DATM=1 (background A=0x80)
        Write(w, 0x47, 1ull << 14 | 1ull << 15 | 1ull << 16 | 1ull << 17);
    if (scene == 7) // FBA forces high alpha bit
        Write(w, 0x49, 1);
    const u64 flags = scene == 8 || scene == 9 ? 1ull << 6 : 0; // ABE
    if (scene == 9) // fixed alpha selector, overflow wrapping
    {
        Write(w, 0x42, 0x68 | 0xc0ull << 32); // (Cs-0)*FIX+Cd
        Write(w, 0x46, 0);
    }
    Sprite(w, 0x4060a0e0, 4, 6, 44, 42, 1, flags);
    Sprite(w, 0xc0e04070, 21, 23, 60, 57, 2, flags);
    if (scene == 10) // flat triangle rasterization and overlap
    {
        Write(w, 0x00, 3);
        Write(w, 0x01, 0x3f8000008090d020ull);
        Write(w, 0x05, XY(5, 3, 3)); Write(w, 0x05, XY(55, 15, 3)); Write(w, 0x05, XY(17, 61, 3));
    }
    if (scene >= 12 && scene <= 18)
    {
        const u32 primitive = scene == 12 ? 0 : scene == 13 ? 1 : scene == 14 ? 2 : scene == 16 ? 4 : scene == 17 ? 5 : 3;
        Write(w, 0x00, primitive | (scene == 15 ? 1ull << 3 : 0) | (scene == 18 ? 1ull << 5 : 0));
        Write(w, 0x3d, 0x00f01080); // FOGCOL
        constexpr std::array<u32, 6> colors{0x80ff1020, 0x8000ff40, 0x804020ff, 0x8080f080, 0x80f080c0, 0x80808080};
        constexpr std::array<std::array<u32, 2>, 6> points{{{3,5},{51,12},{9,53},{57,56},{31,7},{35,59}}};
        for (u32 n = 0; n < points.size(); ++n)
        {
            Write(w, 1, colors[n] | 0x3f800000ull << 32);
            Write(w, 0x0a, static_cast<u64>(n * 45) << 56); // FOG
            Write(w, 5, XY(points[n][0], points[n][1], 3 + n));
        }
    }
    if (scene == 19)
    {
        Write(w, 0x19, 0); Write(w, 0x4d, 1ull << 16); Write(w, 0x4f, 16);
        Write(w, 0x41, 63ull << 16 | 63ull << 48); Write(w, 0x48, 1ull << 16 | 1ull << 17);
        Sprite(w, 0x80ef3080, 12, 16, 49, 51, 5, 1ull << 9);
    }
    if (scene == 32 || scene == 33 || scene == 36 || scene == 37 || scene == 38)
    {
        if (scene == 32 || scene == 33)
        {
            Write(w, 0x4c, 1ull << 16 | static_cast<u64>(scene == 32 ? 2 : 10) << 24);
            // Re-clear in the 16-bit format: the prologue wrote the background as CT32, which aliases differently.
            Sprite(w, 0x80102030, 0, 0, Width, Height, 1);
            Write(w, 0x44, 0x7362514073625140ull); Write(w, 0x45, 1);
        }
        if (scene == 36) Write(w, 0x4c, 1ull << 16 | 1ull << 24);
        if (scene == 37 || scene == 38) Write(w, 0x4e, 16 | static_cast<u64>(scene == 37 ? 2 : 1) << 24);
        if (scene == 37) Sprite(w, 0x80102030, 0, 0, Width, Height, 1); // re-clear depth in Z16 (prologue wrote Z32)
        Sprite(w, 0x403b739b, 5, 7, 58, 59, 0xfffffffe);
    }
    if (scene == 34 || scene == 35)
    {
        Write(w, 0x50, 1ull << 16 | (scene == 34 ? 256ull << 32 : 0) | 1ull << 48);
        Write(w, 0x51, 4ull | 6ull << 16 | 12ull << 32 | 14ull << 48);
        Write(w, 0x52, 32ull | 32ull << 32); Write(w, 0x53, 2);
        if (scene == 34)
        {
            // Copy back from the disjoint temporary target to test the full round trip.
            Write(w, 0x50, 256ull | 1ull << 16 | 1ull << 48);
            Write(w, 0x51, 12ull | 14ull << 16 | 16ull << 32 | 19ull << 48);
            Write(w, 0x53, 2);
        }
    }
    if (scene == 11 || (scene >= 20 && scene <= 31) || scene >= 39)
    {
        const bool indexed = (scene >= 28 && scene <= 31) || scene >= 41;
        const bool four = scene == 29 || scene == 31 || scene >= 42;
        const bool csm2 = scene == 30 || scene == 31;
        const u32 psm = scene == 39 ? 2 : scene == 41 ? 0x1b : scene == 42 ? 0x24 : scene == 43 ? 0x2c : indexed ? (four ? 0x14 : 0x13) : 0;
        if (indexed)
        {
            std::vector<u8> palette(256 * 4);
            for (u32 i = 0; i < 256; ++i)
            {
                const u32 c = (i & 255) | ((255-i) << 8) | ((i*37 & 255) << 16) | 0x80000000;
                std::memcpy(palette.data()+i*4, &c, 4);
            }
            Upload(w, 0x300, csm2 ? 4 : 1, 0, csm2 ? 256 : 16, csm2 ? 1 : 16, palette);
            Write(w, 0x1c, csm2 ? 4 : 1);
        }
        const size_t texture_bytes = 8 * 8 * GSLocalMemory::m_psm[psm].trbpp / 8;
        std::vector<u8> pixels(texture_bytes);
        for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<u8>(i*19+7);
        if (!indexed && scene != 39)
            for (u32 i = 0; i < 64; ++i)
            {
                const u32 c = ((i/4) & 1) ? 0x80ff1020 : 0x8020ef40;
                std::memcpy(pixels.data()+i*4, &c, 4);
            }
        Upload(w, 0x80, 1, psm, 8, 8, pixels);
        Write(w, 0x3f, 0); // TEXFLUSH
        const u32 tfx = scene == 24 ? 0 : scene == 25 ? 2 : scene == 26 ? 3 : 1;
        Write(w, 0x06, 0x80 | 1ull << 14 | static_cast<u64>(psm) << 20 | 3ull << 26 | 3ull << 30 |
            (scene == 27 ? 0 : 1ull << 34) | static_cast<u64>(tfx) << 35 |
            (indexed ? 0x300ull << 37 | static_cast<u64>(csm2) << 55 | 1ull << 61 : 0));
        Write(w, 0x3b, 0x40 | 0x80ull << 32); // TEXA
        Write(w, 0x14, scene == 20 ? 1ull << 5 | 1ull << 6 : 0);
        const u64 clamp = scene == 21 ? 0 : scene == 22 ? 2 | 2ull << 2 | 2ull << 4 | 5ull << 14 | 2ull << 24 | 5ull << 34 :
            scene == 23 ? 3 | 3ull << 2 | 3ull << 4 | 4ull << 14 | 3ull << 24 | 4ull << 34 : 1 | 1ull << 2;
        Write(w, 0x08, clamp);
        Write(w, 0, (scene == 40 ? 3 : 6) | 1ull << 4 | (scene == 40 ? 0 : 1ull << 8));
        Write(w, 1, 0x3f80000080808080ull);
        if (scene == 40)
        {
            auto st = [&](float s, float t, float q, u32 x, u32 y) {
                u32 sb, tb, qb; std::memcpy(&sb,&s,4); std::memcpy(&tb,&t,4); std::memcpy(&qb,&q,4);
                Write(w, 2, sb | static_cast<u64>(tb) << 32);
                Write(w, 1, 0x80808080ull | static_cast<u64>(qb) << 32); Write(w, 5, XY(x,y,4));
            };
            st(0,0,1,8,8); st(2,0,2,56,12); st(0,1,1,12,56);
        }
        else
        {
            Write(w, 3, 0); Write(w, 5, XY(8, 8, 4));
            const u64 uv = (scene >= 21 && scene <= 23) ? 16*16 : 8*16;
            Write(w, 3, uv | uv << 16); Write(w, 5, XY(56, 56, 4));
        }
    }
    return w;
}

// Extra scenes. They run after the generated matrix (indices Names.size()+cases...) so base-scene and
// matrix indices, names and the baselines/goldens that key on them stay stable. Each reads back
// width x height at FBW fbw (Z32 at ZBP zbp); 16-bit scenes are read in their native PSM.
struct Extra { const char* name; u32 width, height, fbw, zbp; };
const std::array<Extra, 24> Extras = {{
    {"mipmap-lod-q", 64, 64, 1, 16}, {"mipmap-fixed-k", 64, 64, 1, 16}, {"mipmap-trilinear", 64, 64, 1, 16},
    {"tex-from-rt", 64, 64, 1, 16}, {"tex-feedback-same-fbp", 64, 64, 1, 16},
    {"shuffle-ct16-tex", 64, 64, 1, 16}, {"shuffle-ct16-rt", 64, 64, 1, 16}, {"shuffle-8h", 64, 64, 1, 16},
    {"shuffle-4hl", 64, 64, 1, 16}, {"shuffle-4hh", 64, 64, 1, 16},
    {"large-256-multipage", 256, 256, 4, 40}, {"large-640x448-multipage", 640, 448, 10, 160},
    {"fst-large-uv-repeat", 64, 64, 1, 16}, {"stq-large-uv-repeat", 64, 64, 1, 16},
    {"fst-large-uv-region-repeat", 64, 64, 1, 16}, {"stq-large-uv-region-repeat", 64, 64, 1, 16},
    {"clut-reload-cld", 64, 64, 1, 16},
    {"aa1-lines", 64, 64, 1, 16}, {"aa1-line-strip", 64, 64, 1, 16}, {"aa1-triangle", 64, 64, 1, 16},
    {"ct16-colclamp-wrap", 64, 64, 1, 16}, {"ct16s-colclamp-wrap", 64, 64, 1, 16},
    {"ct16-fba", 64, 64, 1, 16}, {"ct16-colclamp-fba", 64, 64, 1, 16}}};

u32 FloatBits(float f) { u32 b; std::memcpy(&b, &f, 4); return b; }
// TEX0: decal, TCC=1, texture width/height 2^tw x 2^th, TBW 1.
u64 Tex0(u32 tbp, u32 psm, u32 tw, u32 th, u64 clut = 0)
{
    return tbp | 1ull << 14 | static_cast<u64>(psm) << 20 | static_cast<u64>(tw) << 26 | static_cast<u64>(th) << 30 |
        1ull << 34 | 1ull << 35 | clut;
}
u64 Tex1(u32 lcm, u32 mxl, u32 mmag, u32 mmin, u32 k)
{
    return lcm | static_cast<u64>(mxl) << 2 | static_cast<u64>(mmag) << 5 | static_cast<u64>(mmin) << 6 |
        static_cast<u64>(k & 0xfff) << 32;
}
// Textured sprite with raw 12.4 UVs (FST).
void UvSprite(Writes& w, u32 x0, u32 y0, u32 x1, u32 y1, u32 u0, u32 v0, u32 u1, u32 v1)
{
    Write(w, 0x00, 6 | 1ull << 4 | 1ull << 8);
    Write(w, 0x01, 0x3f80000080808080ull);
    Write(w, 0x03, u0 | static_cast<u64>(v0) << 16); Write(w, 0x05, XY(x0, y0, 4));
    Write(w, 0x03, u1 | static_cast<u64>(v1) << 16); Write(w, 0x05, XY(x1, y1, 4));
}
// STQ vertex: ST, then RGBAQ carrying Q, then XYZ2.
void StVertex(Writes& w, float s, float t, float q, u32 x, u32 y)
{
    Write(w, 0x02, FloatBits(s) | static_cast<u64>(FloatBits(t)) << 32);
    Write(w, 0x01, 0x80808080ull | static_cast<u64>(FloatBits(q)) << 32);
    Write(w, 0x05, XY(x, y, 4));
}
// Every texel distinct in both axes (the texture-upload pattern only varies per 4-pixel run along x).
u32 ExtraTexel(u32 i) { return 0x80000000u | ((i * 37) & 255) | (((i * 91 + 13) & 255) << 8) | (((i * 53 + 7) & 255) << 16); }
std::vector<u8> Pattern32() // 8x8 CT32 texture used by the FST/STQ scenes
{
    std::vector<u8> pixels(8 * 8 * 4);
    for (u32 i = 0; i < 64; ++i)
    {
        const u32 c = ExtraTexel(i);
        std::memcpy(pixels.data() + i * 4, &c, 4);
    }
    return pixels;
}
u32 PaletteEntry(u32 variant, u32 i)
{
    switch (variant)
    {
        case 0: return (i & 255) | ((255 - i) << 8) | ((i * 37 & 255) << 16) | 0x80000000;
        case 1: return (255 - i) | ((i * 5 & 255) << 8) | (i << 16) | 0x80000000;
        default: return (i * 3 & 255) | (i << 8) | ((255 - i) << 16) | 0x80000000;
    }
}
void UploadPalette(Writes& w, u32 variant)
{
    std::vector<u8> palette(256 * 4);
    for (u32 i = 0; i < 256; ++i) { const u32 c = PaletteEntry(variant, i); std::memcpy(palette.data() + i * 4, &c, 4); }
    Upload(w, 0x300, 1, 0, 16, 16, palette);
}

Writes ExtraScene(u32 k)
{
    const std::string_view n = Extras[k].name;
    Writes w;
    Prologue(w);
    auto frame = [&](u32 fbp, u32 psm) { Write(w, 0x4c, static_cast<u64>(fbp) | 1ull << 16 | static_cast<u64>(psm) << 24); };
    auto clear = [&](u32 fbp, u32 psm) { frame(fbp, psm); Sprite(w, 0x80102030, 0, 0, Width, Height, 1); };
    auto texflush = [&]() { Write(w, 0x3f, 0); };
    auto triangle = [&](u64 prim, std::array<std::array<u32, 3>, 3> v, std::array<u32, 3> colors)
    {
        Write(w, 0x00, prim);
        for (u32 i = 0; i < 3; ++i) { Write(w, 0x01, colors[i] | 0x3f800000ull << 32); Write(w, 0x05, XY(v[i][0], v[i][1], v[i][2])); }
    };
    // Source render target at FBP 4 (TBP 128) with distinct alpha per region, for the shuffle scenes.
    auto render_source = [&]()
    {
        frame(4, 0);
        Sprite(w, 0x10305070, 0, 0, 64, 64, 1);
        Sprite(w, 0x60ff4020, 4, 4, 34, 34, 2);
        Sprite(w, 0xa020ff40, 30, 8, 60, 40, 3);
        Sprite(w, 0xe04020ff, 10, 30, 50, 60, 4);
        frame(0, 0);
        texflush();
    };

    if (n.substr(0, 6) == "mipmap")
    {
        constexpr std::array<u32, 3> level_color{0x800000ff, 0x8000ff00, 0x80ff0000};
        for (u32 level = 0; level < 3; ++level)
        {
            const u32 size = 16 >> level;
            std::vector<u8> bytes(size * size * 4);
            for (u32 i = 0; i < size * size; ++i) std::memcpy(bytes.data() + i * 4, &level_color[level], 4);
            Upload(w, 0x80 + 0x20 * level, 1, 0, size, size, bytes);
        }
        texflush();
        Write(w, 0x06, Tex0(0x80, 0, 4, 4));
        Write(w, 0x34, 0xa0ull | 1ull << 14 | 0xc0ull << 20 | 1ull << 34); // MIPTBP1: levels 1,2 (3 unused)
        Write(w, 0x36, 0); // MIPTBP2
        Write(w, 0x08, 0);
        auto triangles = [&]()
        {
            // Q = 1, 0.5, 0.25, 0.125: LOD = -log2(Q) = 0..3; U/V cover the whole texture at every vertex.
            Write(w, 0x00, 3 | 1ull << 4);
            StVertex(w, 0, 0, 1, 4, 4); StVertex(w, 0.5f, 0, 0.5f, 60, 4); StVertex(w, 0, 0.25f, 0.25f, 4, 60);
            StVertex(w, 0.5f, 0, 0.5f, 60, 4); StVertex(w, 0, 0.25f, 0.25f, 4, 60); StVertex(w, 0.125f, 0.125f, 0.125f, 60, 60);
        };
        if (n == "mipmap-lod-q")
        {
            Write(w, 0x14, Tex1(0, 2, 0, 2, 0)); // LCM=0 (LOD from Q), MXL=2, nearest-mipmap-nearest
            triangles();
        }
        else if (n == "mipmap-fixed-k")
        {
            constexpr std::array<u32, 4> k_value{0, 16, 32, 48}; // 1/16 units: LOD 0,1,2 and 3 (clamped to MXL=2)
            for (u32 i = 0; i < 4; ++i)
            {
                Write(w, 0x14, Tex1(1, 2, 0, 2, k_value[i])); // LCM=1: fixed K
                UvSprite(w, 2 + 16 * i, 8, 16 + 16 * i, 56, 0, 0, 16 * 16, 16 * 16);
            }
        }
        else
        {
            Write(w, 0x14, Tex1(0, 2, 1, 5, 0)); // LCM=0, linear-mipmap-linear (trilinear), MMAG linear
            triangles();
            Write(w, 0x14, Tex1(1, 2, 1, 5, 24)); // fixed LOD 1.5: blend of levels 1 and 2
            UvSprite(w, 20, 20, 44, 44, 0, 0, 16 * 16, 16 * 16);
        }
        return w;
    }
    if (n == "tex-from-rt")
    {
        frame(4, 0);
        Sprite(w, 0x80305070, 0, 0, 64, 64, 1);
        Sprite(w, 0x80ff4020, 8, 8, 32, 32, 2);
        Sprite(w, 0x8020ff40, 24, 24, 56, 56, 3);
        triangle(3 | 1ull << 3, {{{40, 4, 4}, {60, 24, 4}, {36, 20, 4}}}, {0x800000ff, 0x8000ff00, 0x80ff0000});
        frame(0, 0);
        texflush();
        Write(w, 0x06, Tex0(128, 0, 6, 6)); Write(w, 0x14, 0);
        UvSprite(w, 4, 4, 36, 36, 0, 0, 64 * 16, 64 * 16); // 2x minify, nearest
        Write(w, 0x14, 1ull << 5 | 1ull << 6);
        UvSprite(w, 36, 36, 60, 60, 16 * 16, 16 * 16, 48 * 16, 48 * 16); // 1.33x magnify, bilinear
        return w;
    }
    if (n == "tex-feedback-same-fbp")
    {
        // Texture base == frame base. Every draw samples regions written earlier and writes elsewhere.
        Sprite(w, 0x80ff4020, 2, 2, 18, 30, 2);
        Sprite(w, 0x8020ff40, 14, 10, 30, 30, 3);
        texflush();
        Write(w, 0x06, Tex0(0, 0, 6, 6)); Write(w, 0x14, 0);
        UvSprite(w, 32, 0, 64, 32, 0, 0, 32 * 16, 32 * 16);
        texflush();
        UvSprite(w, 0, 32, 32, 64, 32 * 16, 0, 64 * 16, 32 * 16); // copy of the copy
        texflush();
        Write(w, 0x14, 1ull << 5 | 1ull << 6);
        UvSprite(w, 32, 32, 64, 64, 0, 0, 32 * 16, 32 * 16);
        return w;
    }
    if (n.substr(0, 7) == "shuffle")
    {
        const bool indexed = n == "shuffle-8h" || n == "shuffle-4hl" || n == "shuffle-4hh";
        if (indexed) { UploadPalette(w, 0); Write(w, 0x1c, 1); }
        render_source();
        Write(w, 0x3b, 0x40 | 0x80ull << 32); // TEXA
        Write(w, 0x14, 0);
        if (n == "shuffle-ct16-tex") // CT32 render target sampled as a CT16 texture
        {
            Write(w, 0x06, Tex0(128, 2, 6, 6));
            UvSprite(w, 0, 0, 64, 64, 0, 0, 64 * 16, 64 * 16);
        }
        else if (n == "shuffle-ct16-rt") // CT32 render target sampled as CT32 and written to a CT16 target
        {
            clear(0, 2);
            Write(w, 0x06, Tex0(128, 0, 6, 6));
            UvSprite(w, 0, 0, 64, 64, 0, 0, 64 * 16, 64 * 16);
        }
        else // alpha byte / nibbles of the CT32 target used as a CLUT index
        {
            const u32 psm = n == "shuffle-8h" ? 0x1b : n == "shuffle-4hl" ? 0x24 : 0x2c;
            Write(w, 0x06, Tex0(128, psm, 6, 6, 0x300ull << 37 | 1ull << 61));
            UvSprite(w, 0, 0, 64, 64, 0, 0, 64 * 16, 64 * 16);
        }
        return w;
    }
    if (n.substr(0, 5) == "large")
    {
        const u32 W = Extras[k].width, H = Extras[k].height, fbw = Extras[k].fbw, zbp = Extras[k].zbp;
        Write(w, 0x4c, static_cast<u64>(fbw) << 16);
        Write(w, 0x4e, zbp);
        Write(w, 0x40, static_cast<u64>(W - 1) << 16 | static_cast<u64>(H - 1) << 48);
        Sprite(w, 0x80102030, 0, 0, W, H, 1); // fully initializes color and Z at the new size
        auto fx = [&](u32 num) { return W * num / 100; };
        auto fy = [&](u32 num) { return H * num / 100; };
        // Pages are 64x32 pixels: these all straddle several page boundaries in both directions.
        Sprite(w, 0x40e06020, fx(18), fy(10), fx(55), fy(40), 2, 1ull << 6);
        Sprite(w, 0x60a020e0, fx(40), fy(5), fx(97), fy(30), 3, 1ull << 6);
        Sprite(w, 0x80ff8020, fx(5), fy(35), fx(35), fy(75), 4); // opaque
        triangle(3 | 1ull << 3 | 1ull << 6, {{{fx(4), fy(96), 5}, {fx(96), fy(90), 5}, {fx(50), fy(20), 5}}},
            {0x40ff2020, 0x6020ff20, 0x802020ff});
        Write(w, 0x47, 1ull << 16 | 3ull << 17); // ZTE, Z GREATER
        Sprite(w, 0x80ffffff, fx(20), fy(45), fx(80), fy(80), 1, 0); // lower Z than the earlier draws: rejected there
        Sprite(w, 0x8000ffff, fx(30), fy(50), fx(70), fy(70), 100, 1ull << 6); // admitted, blended
        return w;
    }
    if (n.substr(0, 3) == "fst" || n.substr(0, 3) == "stq")
    {
        Upload(w, 0x80, 1, 0, 8, 8, Pattern32());
        texflush();
        Write(w, 0x06, Tex0(0x80, 0, 3, 3)); Write(w, 0x3b, 0x40 | 0x80ull << 32); Write(w, 0x14, 0);
        const bool region = n.find("region") != std::string_view::npos;
        // REPEAT, or REGION_REPEAT with (u & 3) | 4 on both axes (a clean power-of-two region: texels 4..7).
        Write(w, 0x08, region ? 3 | 3ull << 2 | 3ull << 4 | 4ull << 14 | 3ull << 24 | 4ull << 34 : 0);
        if (n.substr(0, 3) == "fst")
        {
            const u32 u0 = 803, v0 = 517; // 1:1 scale at large texel offsets (12.4 limit is 1023.9375)
            UvSprite(w, 8, 8, 56, 56, u0 * 16, v0 * 16, (u0 + 48) * 16, (v0 + 48) * 16);
        }
        else
        {
            const float q = region ? 2.0f : 1.0f; // S/T are texel/8*Q: 1:1 scale at large offsets
            const float u0 = 1403, v0 = 1217;
            Write(w, 0x00, 6 | 1ull << 4);
            StVertex(w, u0 / 8 * q, v0 / 8 * q, q, 8, 8);
            StVertex(w, (u0 + 48) / 8 * q, (v0 + 48) / 8 * q, q, 56, 56);
        }
        return w;
    }
    if (n == "clut-reload-cld")
    {
        std::vector<u8> pixels(64);
        for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<u8>(i * 19 + 7);
        UploadPalette(w, 0);
        Upload(w, 0x80, 1, 0x13, 8, 8, pixels);
        Write(w, 0x1c, 1); Write(w, 0x3b, 0x40 | 0x80ull << 32); Write(w, 0x14, 0); Write(w, 0x08, 0);
        // CLD 1 loads; 0 keeps the stale CLUT buffer after the palette memory changed; 4/5 load when CBP0/CBP1 differs.
        constexpr std::array<u32, 7> cld{1, 0, 1, 4, 2, 3, 5};
        constexpr std::array<u32, 7> palette_before{99, 1, 1, 2, 0, 1, 2}; // variant uploaded before each quadrant (99 = none)
        for (u32 i = 0; i < 7; ++i)
        {
            if (palette_before[i] != 99) UploadPalette(w, palette_before[i]);
            Write(w, 0x06, Tex0(0x80, 0x13, 3, 3, 0x300ull << 37 | static_cast<u64>(cld[i]) << 61));
            texflush();
            const u32 x = 4 + 20 * (i % 3), y = 4 + 20 * (i / 3);
            UvSprite(w, x, y, x + 16, y + 16, 0, 0, 8 * 16, 8 * 16);
        }
        return w;
    }
    if (n.substr(0, 3) == "aa1")
    {
        constexpr u64 AA1_ABE = 1ull << 6 | 1ull << 7;
        const u32 color = 0x80e08040;
        if (n == "aa1-lines")
        {
            constexpr std::array<std::array<u32, 4>, 5> lines{{{4, 4, 60, 20}, {4, 10, 60, 58}, {10, 60, 40, 6}, {8, 32, 56, 34}, {30, 4, 32, 60}}};
            for (const auto& l : lines)
            {
                Write(w, 0x00, 1 | AA1_ABE); Write(w, 0x01, color | 0x3f800000ull << 32);
                Write(w, 0x05, XY(l[0], l[1], 2)); Write(w, 0x05, XY(l[2], l[3], 2));
            }
        }
        else if (n == "aa1-line-strip")
        {
            Write(w, 0x00, 2 | AA1_ABE); Write(w, 0x01, color | 0x3f800000ull << 32);
            constexpr std::array<std::array<u32, 2>, 6> pts{{{4, 6}, {58, 14}, {12, 30}, {54, 44}, {20, 60}, {44, 8}}};
            for (const auto& p : pts) Write(w, 0x05, XY(p[0], p[1], 2));
        }
        else
            triangle(3 | AA1_ABE, {{{6, 8, 2}, {58, 20, 2}, {24, 58, 2}}}, {0x80ff2020, 0x8020ff20, 0x802020ff});
        return w;
    }
    // 16-bit color: clear in the 16-bit format first, then clamp/wrap and FBA tests.
    const u32 psm = n == "ct16s-colclamp-wrap" ? 10 : 2;
    clear(0, psm);
    if (n == "ct16-fba" || n == "ct16-colclamp-fba") Write(w, 0x4a, 1); // FBA_1 (0x4a; 0x49 is PABE)
    if (n != "ct16-fba")
    {
        Write(w, 0x42, 0x68 | 0xc0ull << 32); // (Cs-0)*FIX+Cd, overflow wraps with COLCLAMP=0
        Write(w, 0x46, 0);
    }
    const u64 flags = n != "ct16-fba" ? 1ull << 6 : 0;
    Sprite(w, 0x4060a0e0, 4, 6, 44, 42, 2, flags);
    Sprite(w, 0xc0e04070, 21, 23, 60, 57, 3, flags);
    Sprite(w, 0x0040c0a0, 8, 40, 30, 60, 4, flags);
    return w;
}

struct Replay
{
    std::string backend, output;
    bool capture;
    std::vector<Checkpoint> checkpoints;
    u32 frames = 0;
    u32 cases = 256, seed = 1;
    s32 export_case = -1;
    bool profile = false;
    std::vector<Profile> profiles;
    // Sequence mode keeps GS, texture-cache and target state across scenes so
    // cross-draw reuse/invalidation paths are compared, then reads all VRAM.
    bool sequence = false;

    void RecordProfile(u32 packet, const std::string& name, std::chrono::steady_clock::time_point start)
    {
        const auto submitted = std::chrono::steady_clock::now();
        Drain();
        const auto finished = std::chrono::steady_clock::now();
        Profile p{frames, packet, name, std::chrono::duration<double, std::milli>(submitted-start).count(),
            std::chrono::duration<double, std::milli>(finished-submitted).count(), {}};
        for (size_t c = 0; c < p.counters.size(); ++c)
            p.counters[c] = g_perfmon.GetCounter(static_cast<GSPerfMon::counter_t>(c));
        profiles.push_back(std::move(p));
        g_perfmon.Reset();
    }

    void Save(const std::vector<u8>& bytes, u32 width, u32 height, u32 packet, const std::string& name, const char* kind, const char* format = "")
    {
        Require(!bytes.empty() && bytes.size() == static_cast<size_t>(width) * height * 4, "Invalid capture size");
        const std::string file = fmt::format("{:06d}-{}.bin", checkpoints.size(), kind);
        std::ofstream stream(output + "/" + file, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Require(stream.good(), "Cannot write capture artifact");
        checkpoints.push_back({frames, packet, width, height, name, kind, file, format});
    }

    void Drain()
    {
        GSFlushPendingDraws();
        // SWExtraThreads=0 selects GSSingleRasterizer: Flush completes SW work inline.
        g_gs_device->FlushDeferredDraws();
        if (g_gs_device->GetRenderAPI() == RenderAPI::Vulkan)
            static_cast<GSDeviceVK*>(g_gs_device.get())->ExecuteCommandBuffer(true);
        else
            glFinish();
    }

    // bpp16: read the region in its native 16-bit PSM (bitbltbuf already carries it) and expand to
    // 32-bit words so both backends are compared in the format the scene actually drew in. Reading a
    // CT16/Z16 target through a CT32/Z32 view compares format-aliasing, not rendering.
    void ReadRegion(u32 packet, const std::string& name, const char* kind, u64 bitbltbuf, u32 width, u32 height,
        const char* format = "", bool bpp16 = false)
    {
        Writes w;
        Write(w, 0x50, bitbltbuf);
        Write(w, 0x51, 0); Write(w, 0x52, width | static_cast<u64>(height) << 32); Write(w, 0x53, 1);
        Send(w);
        const u32 qwc = width * height / (bpp16 ? 8 : 4);
        if (s_repro) s_repro->ReadFIFO(qwc);
        const size_t raw_bytes = static_cast<size_t>(width) * height * (bpp16 ? 2 : 4);
        std::vector<u8> bytes(raw_bytes + 16, 0xcd);
        GSInitAndReadFIFO(bytes.data(), qwc);
        bytes.resize(raw_bytes);
        if (bpp16)
        {
            std::vector<u8> wide(static_cast<size_t>(width) * height * 4);
            const bool depth = std::strcmp(kind, "depth") == 0;
            for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
            {
                const u32 v = bytes[i * 2] | static_cast<u32>(bytes[i * 2 + 1]) << 8;
                // Color: RGB5551 -> RGBA8 (5-bit channels left-aligned, A 0x00/0x80 like CT16 alpha). Depth: raw u16.
                const u32 out = depth ? v : ((v & 31) << 3) | (((v >> 5) & 31) << 3) << 8 |
                    (((v >> 10) & 31) << 3) << 16 | (v & 0x8000 ? 0x80000000u : 0u);
                std::memcpy(wide.data() + i * 4, &out, 4);
            }
            bytes = std::move(wide);
        }
        Save(bytes, width, height, packet, name, kind, format);
    }

    void ReadBuffer(u32 packet, const std::string& name, bool depth, u32 width = Width, u32 height = Height, u32 fbw = 1, u32 zbp = 16)
    {
        // Read in the format the scene actually configured (context 1). 16-bit formats are read natively;
        // CT24/Z24 and 32-bit formats keep the CT32/Z32 view (verified bit-identical aliasing for CT24/Z24).
        const auto& ctx = g_gs_renderer->m_env.CTXT[0];
        const u32 psm = depth ? ctx.ZBUF.PSM & 0xf : ctx.FRAME.PSM; // ZBUF.PSM carries the 0x30 Z base (0x30,0x31,0x32,0x3a)
        const bool b16 = psm == 2 || psm == 10;
        const char* format = depth ? (psm == 0 ? "Z32" : psm == 1 ? "Z24" : psm == 2 ? "Z16" : psm == 10 ? "Z16S" : "Z?")
                                   : (psm == 0 ? "CT32" : psm == 1 ? "CT24" : psm == 2 ? "CT16" : psm == 10 ? "CT16S" : "CT?");
        const u64 spsm = b16 ? static_cast<u64>((depth ? 0x30 : 0) | psm) : (depth ? 0x30ull : 0);
        // SBP ZBUF base / 256 (ZBP * 32), SBW fbw
        ReadRegion(packet, name, depth ? "depth" : "rgba", (depth ? zbp * 32ull : 0) | static_cast<u64>(fbw) << 16 | spsm << 24, width, height, format, b16);
    }

    void ReadAllMemory(u32 packet)
    {
        // Whole 4 MiB local memory as CT32, 1024x1024 with SBW 16. Catches
        // corruption outside the 64x64 frame/depth windows (textures, CLUTs).
        ReadRegion(packet, "vram", "rgba", 16ull << 16, 1024, 1024);
    }

    void Synthetic()
    {
        std::vector<u8> corpus;
        for (u32 i = 0; i < Names.size() + cases + Extras.size(); ++i)
        {
            const auto start = std::chrono::steady_clock::now();
            const bool reset = !sequence || i == 0;
            if (reset)
                GSreset(true);
            std::unique_ptr<GSDumpBase> repro;
            const bool extra = i >= Names.size() + cases;
            const Extra* extra_info = extra ? &Extras[i - Names.size() - cases] : nullptr;
            const std::string name = extra ? extra_info->name : i < Names.size() ? Names[i] : fmt::format("matrix-{:04d}", i - Names.size());
            if (capture)
            {
                // Zero-length record denotes a hardware reset / new fixture.
                if (reset)
                    corpus.insert(corpus.end(), sizeof(u64), 0);
                s_corpus = &corpus;
                // Record a replayable initial state and command stream for every
                // fixture. Only Software needs to export these identical inputs.
                if (backend == "sw" && (i < 12 || static_cast<s32>(i) == export_case))
                {
                    freezeData fd{};
                    Require(GSfreeze(FreezeAction::Size, &fd) == 0, "Cannot size repro state");
                    std::vector<u8> state(fd.size); fd.data = state.data();
                    Require(GSfreeze(FreezeAction::Save, &fd) == 0, "Cannot save repro state");
                    repro = GSDumpBase::CreateUncompressedDump(output + "/repro-" + name, "GS-ORACLE", 0,
                        0, 0, nullptr, fd, g_gs_renderer->m_regs);
                    s_repro = repro.get();
                }
            }
            Send(extra ? ExtraScene(i - static_cast<u32>(Names.size()) - cases) : Scene(i, seed));
            s_corpus = nullptr;
            GSFlushPendingDraws();
            if (profile) RecordProfile(i, name, start);
            if (capture)
            {
                if (extra)
                {
                    ReadBuffer(i, name, false, extra_info->width, extra_info->height, extra_info->fbw, extra_info->zbp);
                    ReadBuffer(i, name, true, extra_info->width, extra_info->height, extra_info->fbw, extra_info->zbp);
                }
                else
                {
                    ReadBuffer(i, name, false);
                    ReadBuffer(i, name, true);
                }
                if (repro) repro->VSync(0, true, g_gs_renderer->m_regs);
                s_repro = nullptr;
            }
            ++frames;
        }
        if (capture && sequence)
            ReadAllMemory(static_cast<u32>(Names.size() + cases + Extras.size()));
        if (capture)
        {
            std::ofstream stream(output + "/corpus.bin", std::ios::binary);
            stream.write(reinterpret_cast<const char*>(corpus.data()), corpus.size());
            Require(stream.good(), "Cannot write synthetic input evidence");
        }
    }

    void Dump(const GSDumpFile& dump)
    {
        auto& state = dump.GetStateData();
        freezeData fd{static_cast<int>(state.size()), const_cast<u8*>(state.data())};
        Require(GSfreeze(FreezeAction::Load, &fd) == 0, "GS dump state load failed (version mismatch?)");
        const auto& packets = dump.GetPackets();
        auto frame_start = std::chrono::steady_clock::now();
        for (u32 index = 0; index < packets.size(); ++index)
        {
            const auto& p = packets[index];
            switch (p.id)
            {
                case GSDumpTypes::GSType::Transfer:
                    Require(p.length && p.length % 16 == 0, "Unaligned dump transfer");
                    switch (p.path)
                    {
                        case GSDumpTypes::GSTransferPath::Path1Old:
                        {
                            Require(p.length <= 16384, "Oversized Path1Old transfer");
                            alignas(16) std::array<u8, 16384> buffer{};
                            std::memcpy(buffer.data() + buffer.size() - p.length, p.data, p.length);
                            GSgifTransfer1(buffer.data(), buffer.size() - p.length);
                            break;
                        }
                        case GSDumpTypes::GSTransferPath::Path1New: GSgifTransfer(p.data, p.length / 16); break;
                        case GSDumpTypes::GSTransferPath::Path2: GSgifTransfer2(const_cast<u8*>(p.data), p.length / 16); break;
                        case GSDumpTypes::GSTransferPath::Path3: GSgifTransfer3(const_cast<u8*>(p.data), p.length / 16); break;
                        default: throw std::runtime_error("Unknown GIF path");
                    }
                    break;
                case GSDumpTypes::GSType::Registers:
                    Require(p.length == sizeof(GSPrivRegSet), "Invalid GS register packet");
                    std::memcpy(g_gs_renderer->m_regs, p.data, p.length);
                    break;
                case GSDumpTypes::GSType::VSync:
                {
                    Require(p.length == 1 && p.data[0] <= 1, "Invalid VSync packet");
                    GSvsync(p.data[0], true);
                    // Include this frame's merge/deinterlace/present work and
                    // counters before draining and starting the next frame.
                    if (profile) RecordProfile(index, "display", frame_start);
                    if (capture)
                    {
                        GSTexture* texture = g_gs_device->GetCurrent();
                        // A dump can begin at the VSync which precedes its
                        // first completed draw. No host output exists yet.
                        if (!texture && checkpoints.empty())
                        {
                            ++frames;
                            frame_start = std::chrono::steady_clock::now();
                            break;
                        }
                        Require(texture != nullptr, "Dump frame has no output texture");
                        const u32 width = texture->GetWidth(), height = texture->GetHeight();
                        Require(width && height && static_cast<u64>(width)*height <= 16777216, "Invalid output texture dimensions");
                        auto download = g_gs_device->CreateDownloadTexture(width, height, texture->GetFormat());
                        Require(download != nullptr && texture->GetFormat() == GSTexture::Format::Color, "Cannot download RGBA output");
                        const GSVector4i rect(0, 0, width, height);
                        download->CopyFromTexture(rect, texture, rect, 0);
                        download->Flush();
                        std::vector<u8> bytes(static_cast<size_t>(width)*height*4);
                        Require(download->ReadTexels(rect, bytes.data(), width*4), "GPU frame readback failed");
                        Save(bytes, width, height, index, "display", "rgba");
                    }
                    ++frames;
                    frame_start = std::chrono::steady_clock::now();
                    break;
                }
                case GSDumpTypes::GSType::ReadFIFO2:
                {
                    Require(p.length == 4, "Invalid FIFO packet");
                    u32 qwc; std::memcpy(&qwc, p.data, 4);
                    Require(qwc && qwc <= 262144, "Invalid FIFO read size");
                    std::vector<u8> bytes((static_cast<size_t>(qwc)+1)*16, 0xcd);
                    GSInitAndReadFIFO(bytes.data(), qwc);
                    if (capture)
                    {
                        bytes.resize(static_cast<size_t>(qwc)*16);
                        Save(bytes, qwc*4, 1, index, "fifo", "fifo");
                    }
                    break;
                }
                default: throw std::runtime_error("Unknown GS dump packet");
            }
        }
    }
};

u32 Number(const char* text, u32 low, u32 high)
{
    u32 value = 0;
    auto [end, error] = std::from_chars(text, text + std::strlen(text), value);
    Require(error == std::errc() && *end == '\0' && value >= low && value <= high, "Invalid warmup/repeat count");
    return value;
}
} // namespace

extern "C" __attribute__((visibility("default"))) int EmuCoreXRunGSOracle(int argc, const char* const* argv)
{
    bool opened = false;
    bool allocated = false;
    try
    {
        Require(argc == 9, "Invalid GS oracle arguments");
        Log::SetConsoleOutputLevel(LOGLEVEL_INFO);
        const std::string backend(argv[0]), input(argv[1]), output(argv[2]), mode(argv[3]);
        Require(backend == "sw" || backend == "vk" || backend == "gl", "Unknown backend");
        Require(mode == "capture" || mode == "benchmark" || mode == "profile", "Unknown mode");
        const u32 warmup = Number(argv[4], 1, 100), repeats = Number(argv[5], 1, 100);
        const u32 cases = Number(argv[6], 0, 4096), seed = Number(argv[7], 1, 0x7fffffffu);
        const s32 export_case = std::strcmp(argv[8], "-1") == 0 ? -1 : static_cast<s32>(Number(argv[8], 0, 4139));
        std::filesystem::create_directories(output);
        Require(cpuinfo_initialize(), "cpuinfo initialization failed");
        if (backend == "sw")
        {
            Require(SysMemory::Allocate(), "Cannot allocate Software renderer code memory");
            allocated = true;
        }
        Common::Timer::GetCurrentValue();
        EmuFolders::Resources = "resources";
        EmuFolders::Cache = "cache";
        EmuFolders::DataRoot = ".";
        EmuFolders::UserResources = "user-resources";
        std::filesystem::create_directories(EmuFolders::Cache);
        std::unique_ptr<GSDumpFile> dump;
        if (input != "builtin")
        {
            Error error;
            dump = GSDumpFile::OpenGSDump(input.c_str(), &error);
            Require(dump && dump->ReadFile(&error, true), "Cannot parse complete GS dump");
            Require(dump->GetRegsData().size() == sizeof(GSPrivRegSet) && !dump->GetStateData().empty() &&
                !dump->GetPackets().empty(), "Empty or invalid GS dump");
        }
        alignas(16) GSPrivRegSet regs{};
        if (dump)
            std::memcpy(&regs, dump->GetRegsData().data(), sizeof(regs));
        else
        {
            regs.PMODE.EN1 = 1; regs.PMODE.MMOD = 1; regs.PMODE.ALP = 0x80;
            regs.SMODE1.CMOD = 2;
            regs.DISP[0].DISPFB.FBW = 1;
            regs.DISP[0].DISPLAY.DW = 63; regs.DISP[0].DISPLAY.DH = 63;
        }
        std::string settings(Settings);
        const auto note_setting = [&settings](const char* env, const char* key) {
            if (const char* value = std::getenv(env))
                settings += fmt::format(";{}={}", key, value);
        };
        note_setting("EMUCOREX_GS_ORACLE_SCALE", "scale");
        note_setting("EMUCOREX_GS_ORACLE_BLEND", "blend");
        note_setting("EMUCOREX_GS_ORACLE_HW_DOWNLOAD", "hwdownload");
        note_setting("EMUCOREX_GS_ORACLE_COALESCE", "coalesce");
        note_setting("EMUCOREX_GS_ORACLE_GAMEDB", "gamedb");
        const char* sequence_env = std::getenv("EMUCOREX_GS_ORACLE_SEQUENCE");
        const bool sequence = !dump && sequence_env && std::strcmp(sequence_env, "1") == 0;
        if (sequence)
            settings += ";sequence=1";
        Pcsx2Config::GSOptions config;
        config.Renderer = backend == "sw" ? GSRendererType::SW : backend == "vk" ? GSRendererType::VK : GSRendererType::OGL;
        config.UpscaleMultiplier = 1;
        config.AccurateBlendingUnit = AccBlendLevel::Maximum;
        config.BlendingAccuracyUserOverride = true;
        config.ManualUserHacks = true;
        config.HWDownloadMode = GSHardwareDownloadMode::Enabled;
        config.HWDownloadModeUserOverride = true;
        if (const char* value = std::getenv("EMUCOREX_GS_ORACLE_SCALE"))
        {
            char* end = nullptr;
            const float scale = std::strtof(value, &end);
            Require(end != value && *end == '\0' && std::isfinite(scale) && scale >= 0.25f && scale <= 8.0f,
                "Invalid GS oracle scale");
            config.UpscaleMultiplier = scale;
        }
        const auto integer_option = [](const char* name, long fallback, long maximum) {
            const char* value = std::getenv(name);
            if (!value) return fallback;
            char* end = nullptr;
            const long parsed = std::strtol(value, &end, 10);
            Require(end != value && *end == '\0' && parsed >= 0 && parsed <= maximum,
                "Invalid GS oracle option");
            return parsed;
        };
        config.AccurateBlendingUnit = static_cast<AccBlendLevel>(integer_option(
            "EMUCOREX_GS_ORACLE_BLEND", static_cast<long>(config.AccurateBlendingUnit), 5));
        config.HWDownloadMode = static_cast<GSHardwareDownloadMode>(integer_option(
            "EMUCOREX_GS_ORACLE_HW_DOWNLOAD", static_cast<long>(config.HWDownloadMode), 5));
        config.InterlaceMode = GSInterlaceMode::Automatic;
        config.LinearPresent = GSPostBilinearMode::Off;
        config.TextureFiltering = BiFiltering::PS2;
        config.SWExtraThreads = 0;
        config.SkipDuplicateFrames = false;
        config.PrecompilePipelines = false;
        config.DisableAsyncTFXCompile = true;
        config.FXAA = false; config.ShadeBoost = false;
        config.CASMode = GSCASMode::Disabled; config.SGSRMode = GSSGSRMode::Disabled;
        config.LoadTextureReplacements = false;
        config.OsdMessagesPos = OsdOverlayPos::None; config.OsdPerformancePos = OsdOverlayPos::None;
        // Opt-in game-dump reproduction must include its hardware fixes. Keep
        // synthetic corpus defaults unchanged and record the effective inputs.
        const char* use_gamedb = std::getenv("EMUCOREX_GS_ORACLE_GAMEDB");
        if (dump && use_gamedb && std::strcmp(use_gamedb, "1") == 0)
        {
            const auto* entry = GameDatabase::findGame(dump->GetSerial());
            Require(entry != nullptr, "Dump serial has no GameDB entry");
            config.ManualUserHacks = false;
            entry->applyGSHardwareFixes(config);
            config.ManualUserHacks = true;
            std::ofstream effective(output + "/game-settings.txt");
            effective << "serial=" << dump->GetSerial() << '\n'
                << "textureInsideRT=" << static_cast<u32>(config.UserHacks_TextureInsideRt) << '\n'
                << "halfPixelOffset=" << static_cast<u32>(config.UserHacks_HalfPixelOffset) << '\n'
                << "autoFlush=" << static_cast<u32>(config.UserHacks_AutoFlush) << '\n'
                << "blending=" << static_cast<u32>(config.AccurateBlendingUnit) << '\n'
                << "upscale=" << config.UpscaleMultiplier << '\n'
                << "hwDownload=" << static_cast<u32>(config.HWDownloadMode) << '\n';
            Require(effective.good(), "Cannot record effective game fixes");
        }
        config.CoalesceRenderPasses = integer_option("EMUCOREX_GS_ORACLE_COALESCE",
            config.CoalesceRenderPasses ? 1 : 0, 1) != 0;
        {
            std::ofstream effective(output + "/scheduler-settings.txt");
            effective << "coalesceRenderPasses=" << config.CoalesceRenderPasses << '\n';
            Require(effective.good(), "Cannot record GS scheduler mode");
        }
        if (const char* draw_frame = std::getenv("EMUCOREX_GS_ORACLE_DRAW_FRAME"))
        {
            config.DumpGSData = true;
            config.SaveRT = config.SaveTexture = config.SaveInfo = config.SaveHWConfig = true;
            config.SaveFrameStart = Number(draw_frame, 0, 100);
            config.SaveFrameCount = 1;
            config.HWDumpDirectory = output + "/draws";
            std::filesystem::create_directories(config.HWDumpDirectory);
        }
        Require(GSopen(config, config.Renderer, reinterpret_cast<u8*>(&regs), GSVSyncMode::Disabled, false), "GSopen failed");
        opened = true;
        {
            std::ofstream driver(output + "/driver.txt");
            driver << g_gs_device->GetDriverInfo() << '\n';
            Require(driver.good(), "Cannot write GPU driver identity");
        }
        Replay replay{backend, output, mode == "capture", {}, 0, cases, seed, export_case, mode == "profile", {}};
        replay.sequence = sequence;
        if (replay.profile) GSSetPerformanceCountersEnabled(true);
        auto run = [&]() {
            replay.frames = 0;
            if (dump)
            {
                std::memcpy(&regs, dump->GetRegsData().data(), sizeof(regs));
                replay.Dump(*dump);
            }
            else replay.Synthetic();
            replay.Drain();
            Require(replay.frames > 0, "No frames executed");
            if (dump && replay.capture)
                Require(std::any_of(replay.checkpoints.begin(), replay.checkpoints.end(),
                    [](const Checkpoint& p) { return p.name == "display" && p.kind == "rgba"; }),
                    "Dump produced no display output");
        };
        std::vector<double> samples;
        if (replay.capture || replay.profile) run();
        else
        {
            for (u32 i = 0; i < warmup; ++i) run();
            for (u32 i = 0; i < repeats; ++i)
            {
                const auto start = std::chrono::steady_clock::now();
                for (u32 batch = 0; batch < 10; ++batch) run();
                samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count());
            }
        }
        // Write completion only after GS teardown has also succeeded.
        GSclose(); opened = false;
        if (allocated) { SysMemory::Release(); allocated = false; }
        std::ofstream manifest(output + "/native-" + mode + ".json");
        manifest << fmt::format("{{\"schema\":1,\"complete\":true,\"mode\":\"{}\",\"backend\":\"{}\",\"settings\":\"{}\",\"frames\":{},\"frames_per_replay\":{},\"batch\":{},\"base_cases\":{},\"extra_cases\":{},\"cases\":{},\"seed\":{},\"warmup\":{},\"sequence\":{},\"checkpoints\":[", mode, backend, settings,
            replay.frames * (mode == "benchmark" ? 10 : 1), replay.frames, mode == "benchmark" ? 10 : 1, Names.size(), Extras.size(), cases, seed, warmup, sequence ? "true" : "false");
        for (size_t i = 0; i < replay.checkpoints.size(); ++i)
        {
            const auto& p = replay.checkpoints[i];
            if (i) manifest << ',';
            manifest << fmt::format("{{\"frame\":{},\"packet\":{},\"width\":{},\"height\":{},\"name\":\"{}\",\"kind\":\"{}\",\"file\":\"{}\"{}}}", p.frame, p.packet, p.width, p.height, p.name, p.kind, p.file,
                p.format.empty() ? std::string() : fmt::format(",\"format\":\"{}\"", p.format));
        }
        manifest << "],\"samples_ms\":[";
        for (size_t i = 0; i < samples.size(); ++i)
        {
            if (i) manifest << ',';
            manifest << fmt::format("{:.9f}", samples[i]);
        }
        manifest << "],\"profiles\":[";
        constexpr const char* counter_names[]{"primitives", "draws", "draw_calls", "readbacks",
            "swizzle_bytes", "unswizzle_bytes", "copies_or_fillrate", "uploads_or_syncpoints", "barriers", "render_passes",
            "rov_copies", "rov_draw_calls", "rov_barriers", "sw_palette_block_copies"};
        static_assert(std::size(counter_names) == GSPerfMon::CounterLast);
        for (size_t i = 0; i < replay.profiles.size(); ++i)
        {
            if (i) manifest << ',';
            const auto& p = replay.profiles[i];
            manifest << fmt::format("{{\"frame\":{},\"packet\":{},\"name\":\"{}\",\"submit_ms\":{:.9f},\"wait_ms\":{:.9f},\"counters\":{{", p.frame, p.packet, p.name, p.submit_ms, p.wait_ms);
            for (size_t c = 0; c < p.counters.size(); ++c)
            {
                if (c) manifest << ',';
                manifest << fmt::format("\"{}\":{:.0f}", counter_names[c], p.counters[c]);
            }
            manifest << "}}";
        }
        manifest << "]}\n";
        manifest.close();
        Require(manifest.good(), "Cannot finalize GS oracle manifest");
        std::printf("GS_ORACLE_COMPLETE backend=%s mode=%s frames=%u checkpoints=%zu samples=%zu\n", backend.c_str(), mode.c_str(), replay.frames, replay.checkpoints.size(), samples.size());
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GS_ORACLE_ERROR: %s\n", error.what());
        if (opened) GSclose();
        if (allocated) SysMemory::Release();
        return 2;
    }
}
