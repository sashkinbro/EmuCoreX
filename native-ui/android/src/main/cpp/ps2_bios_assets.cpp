#include "ps2_bios_assets.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>

namespace ps2ui {
namespace {

std::mutex g_assets_mutex;
BiosAssets g_assets;

struct RomEntry {
    size_t offset = 0;
    size_t size = 0;
};

uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// ROMDIR: 16-byte entries {name[10], extinfo u16, size u32}; files are laid out back to back,
// each aligned to 16 bytes, starting at offset 0 (the RESET entry).
bool FindRomEntry(const uint8_t* data, size_t len, const char* name, RomEntry& out) {
    const uint8_t kReset[6] = {'R', 'E', 'S', 'E', 'T', 0};
    const uint8_t* table = std::search(data, data + std::min<size_t>(len, 0x80000), kReset, kReset + 6);
    if (table == data + std::min<size_t>(len, 0x80000)) return false;
    size_t off = 0;
    for (const uint8_t* e = table; e + 16 <= data + len; e += 16) {
        if (e[0] == 0) break;
        char entryName[11] = {};
        std::memcpy(entryName, e, 10);
        const size_t size = ReadU32(e + 12);
        if (std::strcmp(entryName, name) == 0) {
            if (off + size > len) return false;
            out.offset = off;
            out.size = size;
            return true;
        }
        off += (size + 15) & ~static_cast<size_t>(15);
    }
    return false;
}

// Sony's LZ variant used for OSDSYS and the TEXIMAGE members (u32 size, then 30-bit flag words).
bool Unpack(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out) {
    if (srcLen < 8) return false;
    const size_t size = ReadU32(src);
    if (size == 0 || size > (8u << 20)) return false;
    out.clear();
    out.reserve(size);
    size_t p = 4;
    int count = 0;
    uint32_t flags = 0, mask = 0, lenShift = 0;
    while (out.size() < size) {
        if (count == 0) {
            if (p + 4 > srcLen) return false;
            flags = (static_cast<uint32_t>(src[p]) << 24) | (static_cast<uint32_t>(src[p + 1]) << 16) |
                    (static_cast<uint32_t>(src[p + 2]) << 8) | src[p + 3];
            p += 4;
            const uint32_t sh = flags & 3u;
            mask = 0x3fffu >> sh;
            lenShift = 14u - sh;
            count = 30;
        }
        if (p >= srcLen) return false;
        const uint8_t b = src[p++];
        if (flags & 0x80000000u) {
            if (p >= srcLen) return false;
            const uint32_t h = (static_cast<uint32_t>(b) << 8) | src[p++];
            const size_t back = (h & mask) + 1;
            const size_t n = (h >> lenShift) + 3;
            if (back > out.size()) return false;
            for (size_t i = 0; i < n; ++i) out.push_back(out[out.size() - back]);
        } else {
            out.push_back(b);
        }
        --count;
        flags <<= 1;
    }
    out.resize(size);
    return true;
}

// Member formats: 24-byte header {0x10, psm, ..., u16 w, u16 h} + pixels (psm 2 = CT16 A1B5G5R5),
// or a bare 64x64 CT32 block (16384 bytes). CT16 alpha is unused by these textures (they are
// drawn additively), so it is set opaque.
bool DecodeTexture(const std::vector<uint8_t>& u, BiosTexture& out) {
    if (u.size() >= 24 && ReadU32(u.data()) == 0x10u) {
        const uint32_t psm = ReadU32(u.data() + 4);
        const int w = u[16] | (u[17] << 8);
        const int h = u[18] | (u[19] << 8);
        if (psm != 2 || w <= 0 || h <= 0 || w > 1024 || h > 1024) return false;
        if (u.size() < 24 + static_cast<size_t>(w) * h * 2) return false;
        out.w = w;
        out.h = h;
        out.rgba.resize(static_cast<size_t>(w) * h * 4);
        for (int i = 0; i < w * h; ++i) {
            const uint16_t px = static_cast<uint16_t>(u[24 + i * 2] | (u[24 + i * 2 + 1] << 8));
            const uint8_t r = static_cast<uint8_t>((px & 31) << 3);
            const uint8_t g = static_cast<uint8_t>(((px >> 5) & 31) << 3);
            const uint8_t b = static_cast<uint8_t>(((px >> 10) & 31) << 3);
            out.rgba[i * 4 + 0] = r;
            out.rgba[i * 4 + 1] = g;
            out.rgba[i * 4 + 2] = b;
            out.rgba[i * 4 + 3] = 255;  // drawn additively: brightness comes from RGB
        }
        return true;
    }
    if (u.size() == 64 * 64) {
        // 8-bit mask (TEXOBLP/TEXOBLPR): PS2 alpha scale, 0x80 = 1.0.
        out.w = 64;
        out.h = 64;
        out.rgba.resize(64 * 64 * 4);
        for (int i = 0; i < 64 * 64; ++i) {
            const uint8_t v = static_cast<uint8_t>(std::min(255, u[i] * 2));
            out.rgba[i * 4 + 0] = v;
            out.rgba[i * 4 + 1] = v;
            out.rgba[i * 4 + 2] = v;
            out.rgba[i * 4 + 3] = v;
        }
        return true;
    }
    if (u.size() == 64 * 64 * 4) {
        out.w = 64;
        out.h = 64;
        out.rgba.assign(u.begin(), u.end());
        for (size_t i = 3; i < out.rgba.size(); i += 4) {
            out.rgba[i] = static_cast<uint8_t>(std::min(255, out.rgba[i] * 2));  // PS2 alpha 0x80 = 1.0
        }
        return true;
    }
    return false;
}

bool LoadMember(const uint8_t* arc, size_t arcLen, const char* name, BiosTexture& out) {
    RomEntry e;
    if (!FindRomEntry(arc, arcLen, name, e)) return false;
    std::vector<uint8_t> u;
    if (!Unpack(arc + e.offset, e.size, u)) return false;
    return DecodeTexture(u, out);
}

}  // namespace

bool LoadBiosAssets(const uint8_t* data, size_t len) {
    if (!data || len < (1u << 20)) return false;
    RomEntry tex;
    if (!FindRomEntry(data, len, "TEXIMAGE", tex)) return false;
    const uint8_t* arc = data + tex.offset;
    BiosAssets a;
    if (!LoadMember(arc, tex.size, "TEXOFOG0", a.fog)) return false;
    if (!LoadMember(arc, tex.size, "TEXOREF", a.refl)) return false;
    if (!LoadMember(arc, tex.size, "TEXOCRBL", a.glow)) return false;
    if (!LoadMember(arc, tex.size, "TEXOFOG2", a.noise)) return false;
    if (!LoadMember(arc, tex.size, "TEXOFOG1", a.cloud[0])) return false;
    if (!LoadMember(arc, tex.size, "TEXOFOG3", a.cloud[1])) return false;
    if (!LoadMember(arc, tex.size, "TEXOFOG4", a.cloud[2])) return false;
    if (!LoadMember(arc, tex.size, "TEXOBLP", a.smoke)) return false;
    std::lock_guard<std::mutex> lock(g_assets_mutex);
    a.generation = g_assets.generation + 1;
    g_assets = std::move(a);
    return true;
}

bool TakeBiosAssetsIfNewer(uint32_t knownGeneration, BiosAssets& out) {
    std::lock_guard<std::mutex> lock(g_assets_mutex);
    if (g_assets.generation == 0 || g_assets.generation == knownGeneration) return false;
    out = g_assets;
    return true;
}

}  // namespace ps2ui
