// SPDX-License-Identifier: GPL-3.0+
#if !defined(EMUCOREX_GS_ORACLE_DEBUG)
#error GS oracle must only be compiled by the Debug-only CMake gate
#endif
#include "Common.h"
#include "Config.h"
#include "Memory.h"
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
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
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
Writes Scene(u32 scene, u32 seed)
{
    Writes w;
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
            Write(w, 0x44, 0x7362514073625140ull); Write(w, 0x45, 1);
        }
        if (scene == 36) Write(w, 0x4c, 1ull << 16 | 1ull << 24);
        if (scene == 37 || scene == 38) Write(w, 0x4e, 16 | static_cast<u64>(scene == 37 ? 2 : 1) << 24);
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

    void Save(const std::vector<u8>& bytes, u32 width, u32 height, u32 packet, const std::string& name, const char* kind)
    {
        Require(!bytes.empty() && bytes.size() == static_cast<size_t>(width) * height * 4, "Invalid capture size");
        const std::string file = fmt::format("{:06d}-{}.bin", checkpoints.size(), kind);
        std::ofstream stream(output + "/" + file, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Require(stream.good(), "Cannot write capture artifact");
        checkpoints.push_back({frames, packet, width, height, name, kind, file});
    }

    void Drain()
    {
        g_gs_renderer->Flush(GSState::VSYNC);
        // SWExtraThreads=0 selects GSSingleRasterizer: Flush completes SW work inline.
        g_gs_device->FlushDeferredDraws();
        if (g_gs_device->GetRenderAPI() == RenderAPI::Vulkan)
            static_cast<GSDeviceVK*>(g_gs_device.get())->ExecuteCommandBuffer(true);
        else
            glFinish();
    }

    void ReadBuffer(u32 packet, const std::string& name, bool depth)
    {
        Writes w;
        Write(w, 0x50, (depth ? 512ull | 0x30ull << 24 : 0) | 1ull << 16); // SBP ZBUF base / 256
        Write(w, 0x51, 0); Write(w, 0x52, Width | static_cast<u64>(Height) << 32); Write(w, 0x53, 1);
        Send(w);
        if (s_repro) s_repro->ReadFIFO(Width * Height / 4);
        std::vector<u8> bytes(Width * Height * 4 + 16, 0xcd);
        GSInitAndReadFIFO(bytes.data(), Width * Height / 4);
        bytes.resize(Width * Height * 4);
        Save(bytes, Width, Height, packet, name, depth ? "depth" : "rgba");
    }

    void Synthetic()
    {
        std::vector<u8> corpus;
        for (u32 i = 0; i < Names.size() + cases; ++i)
        {
            const auto start = std::chrono::steady_clock::now();
            GSreset(true);
            std::unique_ptr<GSDumpBase> repro;
            const std::string name = i < Names.size() ? Names[i] : fmt::format("matrix-{:04d}", i - Names.size());
            if (capture)
            {
                // Zero-length record denotes a hardware reset / new fixture.
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
            Send(Scene(i, seed));
            s_corpus = nullptr;
            g_gs_renderer->Flush(GSState::VSYNC);
            if (profile) RecordProfile(i, name, start);
            if (capture)
            {
                ReadBuffer(i, name, false);
                ReadBuffer(i, name, true);
                if (repro) repro->VSync(0, true, g_gs_renderer->m_regs);
                s_repro = nullptr;
            }
            ++frames;
        }
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
        Pcsx2Config::GSOptions config;
        config.Renderer = backend == "sw" ? GSRendererType::SW : backend == "vk" ? GSRendererType::VK : GSRendererType::OGL;
        config.UpscaleMultiplier = 1;
        config.AccurateBlendingUnit = AccBlendLevel::Maximum;
        config.BlendingAccuracyUserOverride = true;
        config.ManualUserHacks = true;
        config.HWDownloadMode = GSHardwareDownloadMode::Enabled;
        config.HWDownloadModeUserOverride = true;
        config.InterlaceMode = GSInterlaceMode::Automatic;
        config.LinearPresent = GSPostBilinearMode::Off;
        config.TextureFiltering = BiFiltering::PS2;
        config.SWExtraThreads = 0;
        config.SkipDuplicateFrames = false;
        config.FXAA = false; config.ShadeBoost = false;
        config.CASMode = GSCASMode::Disabled; config.SGSRMode = GSSGSRMode::Disabled;
        config.LoadTextureReplacements = false;
        config.OsdMessagesPos = OsdOverlayPos::None; config.OsdPerformancePos = OsdOverlayPos::None;
        Require(GSopen(config, config.Renderer, reinterpret_cast<u8*>(&regs), GSVSyncMode::Disabled, false), "GSopen failed");
        opened = true;
        {
            std::ofstream driver(output + "/driver.txt");
            driver << g_gs_device->GetDriverInfo() << '\n';
            Require(driver.good(), "Cannot write GPU driver identity");
        }
        Replay replay{backend, output, mode == "capture", {}, 0, cases, seed, export_case, mode == "profile", {}};
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
        manifest << fmt::format("{{\"schema\":1,\"complete\":true,\"mode\":\"{}\",\"backend\":\"{}\",\"settings\":\"{}\",\"frames\":{},\"frames_per_replay\":{},\"batch\":{},\"base_cases\":{},\"cases\":{},\"seed\":{},\"warmup\":{},\"checkpoints\":[", mode, backend, Settings,
            replay.frames * (mode == "benchmark" ? 10 : 1), replay.frames, mode == "benchmark" ? 10 : 1, Names.size(), cases, seed, warmup);
        for (size_t i = 0; i < replay.checkpoints.size(); ++i)
        {
            const auto& p = replay.checkpoints[i];
            if (i) manifest << ',';
            manifest << fmt::format("{{\"frame\":{},\"packet\":{},\"width\":{},\"height\":{},\"name\":\"{}\",\"kind\":\"{}\",\"file\":\"{}\"}}", p.frame, p.packet, p.width, p.height, p.name, p.kind, p.file);
        }
        manifest << "],\"samples_ms\":[";
        for (size_t i = 0; i < samples.size(); ++i)
        {
            if (i) manifest << ',';
            manifest << fmt::format("{:.9f}", samples[i]);
        }
        manifest << "],\"profiles\":[";
        constexpr std::array<const char*, GSPerfMon::CounterLast> counter_names{"primitives", "draws", "draw_calls", "readbacks",
            "swizzle_bytes", "unswizzle_bytes", "copies_or_fillrate", "uploads_or_syncpoints", "barriers", "render_passes",
            "rov_copies", "rov_draw_calls", "rov_barriers"};
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
