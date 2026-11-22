#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps2ui {

// Textures taken at runtime from the user's own PS2 BIOS image (ROM -> TEXIMAGE archive).
// Nothing from the BIOS is shipped with the app; if no BIOS is available the shell keeps its
// procedural fallback.
struct BiosTexture {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;  // straight RGBA8
};

struct BiosAssets {
    BiosTexture fog;    // TEXOFOG0: smoky cloud used for the boot nebula
    BiosTexture refl;   // TEXOREF: environment map reflected by the glass cubes
    BiosTexture glow;   // TEXOCRBL: soft light sprite for the coloured lights
    BiosTexture noise;  // TEXOFOG2: wavy noise used as the glass refraction map
    BiosTexture cloud[3];  // TEXOFOG1/3/4: the three nebula layers (GS dump of the intro)
    BiosTexture smoke;     // TEXOBLP: 8-bit smoke mask the glass cube writes to dest alpha
    uint32_t generation = 0;  // bumps every time a new set is loaded
};

// Parses a full BIOS ROM image. Thread-safe; returns true when all textures were found.
bool LoadBiosAssets(const uint8_t* data, size_t len);

// Copies the current asset set if its generation differs from `knownGeneration`.
bool TakeBiosAssetsIfNewer(uint32_t knownGeneration, BiosAssets& out);

}  // namespace ps2ui
