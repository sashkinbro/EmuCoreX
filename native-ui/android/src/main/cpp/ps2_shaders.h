#pragma once

namespace ps2ui {

inline constexpr const char* kQuadVertexShader = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;

uniform vec2 u_screen;

out vec2 v_uv;
out vec4 v_color;

void main() {
    vec2 ndc = vec2(
        (a_pos.x / u_screen.x) * 2.0 - 1.0,
        1.0 - (a_pos.y / u_screen.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = a_uv;
    v_color = a_color;
}
)";

inline constexpr const char* kQuadFragmentShader = R"(#version 300 es
precision highp float;

uniform sampler2D u_texture;

in vec2 v_uv;
in vec4 v_color;

out vec4 o_color;

void main() {
    vec4 tex = texture(u_texture, v_uv);
    o_color = tex * v_color;
}
)";

inline constexpr const char* kBgVertexShader = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
out vec2 v_uv;

void main() {
    v_uv = a_pos * 0.5 + 0.5;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

// Full-screen PS2 Cosmic Void + Volumetric Blue Mist + 7 Orbiting Crystal Clock Orbs w/ Trails
inline constexpr const char* kBgFragmentShader = R"(#version 300 es
precision highp float;

in vec2 v_uv;
uniform vec2 u_screen;
uniform vec2 u_orb_center;
uniform float u_time;
uniform float u_core_boost;
uniform int u_theme;
uniform vec4 u_orbs[7];    // xy = pos (virtual px), z = radius (virtual px), w = alphaScale
uniform vec4 u_trails[7];  // xy = trail tail pos (virtual px), z = unused, w = trailAlpha

out vec4 o_color;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Fast analytic distance to line segment AB for crystal plasma trail
float segDist2(vec2 p, vec2 a, vec2 b, out float tProj) {
    vec2 ba = b - a;
    float denom = max(dot(ba, ba), 0.0001);
    tProj = clamp(dot(p - a, ba) / denom, 0.0, 1.0);
    vec2 d = p - (a + ba * tProj);
    return dot(d, d);
}

void main() {
    vec2 px = vec2(v_uv.x * u_screen.x, (1.0 - v_uv.y) * u_screen.y);

    // 6 curated PlayStation 2 BIOS color palettes
    vec3 voidCol = vec3(0.004, 0.008, 0.028);
    vec3 mistDeep = vec3(0.025, 0.085, 0.28);
    vec3 mistBright = vec3(0.14, 0.42, 0.88);
    vec3 orbCore = vec3(0.45, 0.82, 1.0);
    vec3 orbSpec = vec3(0.92, 0.97, 1.0);
    vec3 trailCol = vec3(0.24, 0.68, 1.0);

    if (u_theme == 1) {
        // Midnight Obsidian (PS2 Slim Stealth)
        voidCol = vec3(0.004, 0.005, 0.010);
        mistDeep = vec3(0.045, 0.065, 0.115);
        mistBright = vec3(0.24, 0.36, 0.54);
        orbCore = vec3(0.72, 0.88, 1.0);
        orbSpec = vec3(1.0, 1.0, 1.0);
        trailCol = vec3(0.55, 0.78, 0.96);
    } else if (u_theme == 2) {
        // Emerald Matrix (TEST Debug)
        voidCol = vec3(0.003, 0.014, 0.010);
        mistDeep = vec3(0.018, 0.14, 0.095);
        mistBright = vec3(0.08, 0.54, 0.36);
        orbCore = vec3(0.28, 0.96, 0.64);
        orbSpec = vec3(0.88, 1.0, 0.94);
        trailCol = vec3(0.18, 0.88, 0.56);
    } else if (u_theme == 3) {
        // Crimson Sakura
        voidCol = vec3(0.016, 0.004, 0.010);
        mistDeep = vec3(0.18, 0.024, 0.078);
        mistBright = vec3(0.68, 0.14, 0.34);
        orbCore = vec3(1.0, 0.40, 0.60);
        orbSpec = vec3(1.0, 0.92, 0.95);
        trailCol = vec3(0.96, 0.28, 0.52);
    } else if (u_theme == 4) {
        // Amethyst Nebula
        voidCol = vec3(0.010, 0.005, 0.026);
        mistDeep = vec3(0.13, 0.04, 0.30);
        mistBright = vec3(0.46, 0.20, 0.86);
        orbCore = vec3(0.74, 0.48, 1.0);
        orbSpec = vec3(0.96, 0.90, 1.0);
        trailCol = vec3(0.64, 0.34, 1.0);
    } else if (u_theme == 5) {
        // Solar Gold
        voidCol = vec3(0.014, 0.010, 0.004);
        mistDeep = vec3(0.16, 0.10, 0.022);
        mistBright = vec3(0.62, 0.42, 0.12);
        orbCore = vec3(1.0, 0.82, 0.38);
        orbSpec = vec3(1.0, 0.97, 0.86);
        trailCol = vec3(0.96, 0.72, 0.26);
    }

    // Undulating deep PS2 horizon mist (analytic trig — very fast on mobile GPUs)
    float nx = px.x / max(u_screen.x, 1.0);
    float ny = px.y / max(u_screen.y, 1.0);
    float waveA = 0.5 + 0.5 * sin(nx * 5.2 + u_time * 0.28 + sin(ny * 3.1 - u_time * 0.19));
    float waveB = 0.5 + 0.5 * cos(nx * 3.7 - u_time * 0.21 + ny * 4.4);
    float horizonFog = smoothstep(0.22, 0.98, ny);
    float mistDensity = horizonFog * (0.35 + 0.45 * waveA * waveB);

    vec3 bg = mix(voidCol, mistDeep, mistDensity * 0.85);
    bg += mistBright * (horizonFog * horizonFog * waveA * 0.16);

    // Central luminous PS2 orbital core halo
    vec2 relCore = (px - u_orb_center) / vec2(max(u_screen.y * 0.34, 1.0));
    float coreR2 = dot(relCore, relCore);
    float coreHalo = exp(-coreR2 * 2.2) * (0.28 + 0.34 * u_core_boost);
    float corePinpoint = exp(-coreR2 * 18.0) * (0.32 + 0.65 * u_core_boost);
    bg += mistBright * coreHalo + orbSpec * corePinpoint * 0.55;

    // 7 Orbiting Crystal Clock Orbs + Plasma Comet Trails
    for (int i = 0; i < 7; ++i) {
        vec4 orb = u_orbs[i];
        float aScale = orb.w;
        if (aScale <= 0.005 || orb.z <= 0.5) {
            continue;
        }

        // Comet trail behind orb
        vec4 tr = u_trails[i];
        if (tr.w > 0.01) {
            float maxTrailR = orb.z * 2.4;
            vec2 minB = min(orb.xy, tr.xy) - vec2(maxTrailR);
            vec2 maxB = max(orb.xy, tr.xy) + vec2(maxTrailR);
            if (px.x >= minB.x && px.x <= maxB.x && px.y >= minB.y && px.y <= maxB.y) {
                float tProj = 0.0;
                float sd2 = segDist2(px, orb.xy, tr.xy, tProj);
                float width = orb.z * mix(0.92, 0.22, tProj);
                float glow = exp(-sd2 / max(width * width * 0.55, 1.0));
                bg += trailCol * (glow * (1.0 - tProj * 0.85) * 0.48 * tr.w * aScale);
            }
        }

        // Soft outer crystal aura + glowing specular orb
        vec2 d = px - orb.xy;
        float d2 = dot(d, d);
        float rad = orb.z;
        float auraR = rad * 2.75;
        if (d2 >= auraR * auraR) {
            continue;
        }
        float invR2 = 1.0 / max(rad * rad, 1.0);
        float outerGlow = exp(-d2 * invR2 * 0.82) * 0.52 * aScale;
        float innerCore = exp(-d2 * invR2 * 3.4) * 0.92 * aScale;
        float specSpot = exp(-dot(d + vec2(rad * 0.26, rad * 0.26), d + vec2(rad * 0.26, rad * 0.26)) * invR2 * 9.0);
        bg += trailCol * outerGlow + orbCore * innerCore + orbSpec * (specSpot * 0.65 * aScale);
    }

    // Subtle dither to eliminate 8-bit gradient banding on large tablet displays
    float dither = (hash21(gl_FragCoord.xy) - 0.5) / 255.0;
    o_color = vec4(clamp(bg + dither, 0.0, 1.0), 1.0);
}
)";

// True 3D Translucent Glass Monolith Towers Shader (PS2 Boot & Horizon Skyline)
inline constexpr const char* kTowerVertexShader = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in float a_height_norm;
layout(location = 3) in vec4 a_color;

uniform mat4 u_mvp;

out vec3 v_world_pos;
out vec3 v_normal;
out float v_h;
out vec4 v_color;

void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    v_world_pos = a_pos;
    v_normal = a_normal;
    v_h = a_height_norm;
    v_color = a_color;
}
)";

inline constexpr const char* kTowerFragmentShader = R"(#version 300 es
precision highp float;

in vec3 v_world_pos;
in vec3 v_normal;
in float v_h;
in vec4 v_color;

uniform vec3 u_cam_pos;
uniform vec3 u_accent_col;
uniform float u_alpha;

out vec4 o_color;

void main() {
    vec3 N = normalize(v_normal);
    vec3 V = normalize(u_cam_pos - v_world_pos);
    float ndv = abs(dot(N, V));
    float oneMinus = max(0.0, 1.0 - ndv);
    float fresnel = oneMinus * oneMinus * sqrt(oneMinus);

    // Base mist dissolve + bright translucent PS2 top edge
    float baseFade = smoothstep(0.0, 0.32, v_h);
    float topRim = smoothstep(0.84, 1.0, v_h);
    float upFace = max(0.0, N.y);

    vec3 glass = mix(v_color.rgb, u_accent_col, 0.35 + 0.45 * fresnel);
    glass += vec3(0.55, 0.80, 1.0) * (topRim * 0.42 + upFace * 0.30 + fresnel * 0.38);

    float alpha = (0.14 + 0.38 * fresnel + 0.24 * upFace + 0.16 * topRim) * baseFade * v_color.a * u_alpha;
    o_color = vec4(glass, clamp(alpha, 0.0, 0.88));
}
)";

// Smoked PS2 Crystal Glass Panel Shader w/ Luminous Selection Beam
inline constexpr const char* kPanelVertexShader = R"(#version 300 es
layout(location = 0) in vec2 a_pos;

uniform vec2 u_screen;
uniform vec4 u_rect;
out vec2 v_uv;

void main() {
    vec2 uv = vec2(a_pos.x * 0.5 + 0.5, 0.5 - a_pos.y * 0.5);
    vec2 pos = u_rect.xy + uv * u_rect.zw;
    vec2 ndc = vec2(
        (pos.x / u_screen.x) * 2.0 - 1.0,
        1.0 - (pos.y / u_screen.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = uv;
}
)";

inline constexpr const char* kPanelFragmentShader = R"(#version 300 es
precision highp float;

in vec2 v_uv;

uniform vec2 u_size;
uniform float u_radius;
uniform float u_border_px;
uniform vec4 u_top_col;
uniform vec4 u_bot_col;
uniform vec4 u_border_col;
uniform vec4 u_glow_col; // xyz = beam color, w = horizontal crystal light-beam strength

out vec4 o_color;

float sdRoundedBox(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec2 px = v_uv * u_size;
    vec2 center = u_size * 0.5;
    float r = clamp(u_radius, 0.0, min(center.x, center.y) - 0.5);
    float d = sdRoundedBox(px - center, center, r);

    float aa = 1.0;
    float outerAlpha = 1.0 - smoothstep(-aa, aa, d);
    if (outerAlpha <= 0.001) {
        discard;
    }

    vec4 fill = mix(u_top_col, u_bot_col, v_uv.y);

    // Sleek PS2 horizontal crystal glow bar inside focused items / headers
    if (u_glow_col.w > 0.001) {
        vec2 ell = (v_uv - vec2(0.45, 0.5)) * vec2(1.25, 2.2);
        float beam = exp(-dot(ell, ell) * 2.6);
        float horizonLine = exp(-pow((v_uv.y - 0.5) * 8.0, 2.0)) * (1.0 - abs(v_uv.x - 0.5) * 1.4);
        fill.rgb = mix(fill.rgb, u_glow_col.rgb, clamp(beam * 0.72 + max(0.0, horizonLine) * 0.35, 0.0, 1.0) * u_glow_col.w);
    }

    // Translucent top glass specular reflection
    float topSheen = (1.0 - smoothstep(0.0, 0.22, v_uv.y)) * 0.07;
    fill.rgb += vec3(topSheen);

    if (u_border_px > 0.05) {
        float innerMask = 1.0 - smoothstep(-u_border_px - aa, -u_border_px + aa, d);
        vec3 col = mix(u_border_col.rgb, fill.rgb, innerMask);
        float alpha = mix(u_border_col.a, fill.a, innerMask) * outerAlpha;
        o_color = vec4(col, alpha);
    } else {
        o_color = vec4(fill.rgb, fill.a * outerAlpha);
    }
}
)";

// Spinning PS2 Optical Disc Shader (0 = Silver DVD-ROM, 1 = Blue-Bottom CD-ROM)
inline constexpr const char* kDiscVertexShader = R"(#version 300 es
layout(location = 0) in vec2 a_pos;

uniform vec2 u_screen;
uniform vec4 u_rect;
out vec2 v_uv;

void main() {
    vec2 uv = vec2(a_pos.x * 0.5 + 0.5, 0.5 - a_pos.y * 0.5);
    vec2 pos = u_rect.xy + uv * u_rect.zw;
    vec2 ndc = vec2(
        (pos.x / u_screen.x) * 2.0 - 1.0,
        1.0 - (pos.y / u_screen.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = uv;
}
)";

inline constexpr const char* kDiscFragmentShader = R"(#version 300 es
precision highp float;

in vec2 v_uv;
uniform float u_angle;
uniform float u_alpha;
uniform int u_disc_type; // 0 = Silver DVD, 1 = Cobalt Blue CD

out vec4 o_color;

void main() {
    vec2 p = v_uv * 2.0 - 1.0;
    float r = length(p);
    float aa = 0.014;

    if (r > 1.0 || r < 0.145) {
        discard;
    }

    float outerMask = 1.0 - smoothstep(1.0 - aa, 1.0, r);
    float holeMask = smoothstep(0.145, 0.145 + aa, r);
    float mask = outerMask * holeMask;

    float ang = atan(p.y, p.x) + u_angle;
    vec3 col;

    if (r > 0.968) {
        // Metallic outer rim bevel
        col = (u_disc_type == 1) ? vec3(0.44, 0.58, 0.86) : vec3(0.84, 0.87, 0.93);
    } else if (r > 0.35) {
        float c2 = max(0.0, cos(ang * 2.0));
        float c2s = max(0.0, cos(ang * 2.0 + 1.35));
        float cone1 = c2 * c2 * c2 * c2 * c2;
        float cone2 = c2s * c2s * c2s * c2s * c2s * c2s;
        float trackRings = 0.035 * sin(r * 165.0);
        float cSpec = max(0.0, cos(ang - 0.65));
        float spec = cSpec * cSpec * cSpec * cSpec * cSpec * cSpec * cSpec * cSpec;

        if (u_disc_type == 1) {
            // Iconic PS2 Blue-Bottom CD-ROM
            vec3 baseBlue = vec3(0.045, 0.14, 0.44);
            vec3 iridCyan = vec3(0.18, 0.72, 1.0) * cone1 * 0.56;
            vec3 iridViolet = vec3(0.55, 0.24, 0.95) * cone2 * 0.44;
            col = baseBlue + iridCyan + iridViolet + vec3(trackRings * 0.28);
            col += vec3(0.65, 0.82, 1.0) * spec * 0.42;
        } else {
            // PS2 Silver DVD-ROM w/ prismatic rainbow diffraction
            vec3 baseSilver = vec3(0.66, 0.70, 0.78);
            vec3 iridGold = vec3(0.32, 0.24, 0.06) * cone1;
            vec3 iridCyan = vec3(0.10, 0.28, 0.36) * cone2;
            col = baseSilver + iridGold + iridCyan + vec3(trackRings * 0.32);
            col += vec3(1.0, 0.99, 0.96) * spec * 0.28;
        }
    } else if (r > 0.25) {
        float ringSpec = 0.74 + 0.22 * cos(ang * 2.0);
        col = ((u_disc_type == 1) ? vec3(0.36, 0.48, 0.74) : vec3(0.56, 0.60, 0.68)) * ringSpec;
    } else {
        col = (u_disc_type == 1) ? vec3(0.20, 0.30, 0.54) : vec3(0.34, 0.38, 0.48);
    }

    float rimLine = exp(-pow((r - 0.35) * 90.0, 2.0)) * 0.38;
    float hubLine = exp(-pow((r - 0.25) * 90.0, 2.0)) * 0.38;
    col += vec3(0.75, 0.85, 1.0) * (rimLine + hubLine);

    o_color = vec4(col, mask * u_alpha);
}
)";

// 3D-Shaded PS2 MagicGate Memory Card Cartridge Shader
inline constexpr const char* kCardVertexShader = R"(#version 300 es
layout(location = 0) in vec2 a_pos;

uniform vec2 u_screen;
uniform vec4 u_rect;
out vec2 v_uv;

void main() {
    vec2 uv = vec2(a_pos.x * 0.5 + 0.5, 0.5 - a_pos.y * 0.5);
    vec2 pos = u_rect.xy + uv * u_rect.zw;
    vec2 ndc = vec2(
        (pos.x / u_screen.x) * 2.0 - 1.0,
        1.0 - (pos.y / u_screen.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = uv;
}
)";

inline constexpr const char* kCardFragmentShader = R"(#version 300 es
precision highp float;

in vec2 v_uv;
uniform vec2 u_size;
uniform vec4 u_body_col;    // rgb = shell tint, a = alpha
uniform vec4 u_accent_col;  // rgb = LED / highlight tint, a = focused (0..1)
uniform float u_is_empty;   // 1.0 = unmounted ghost slot, 0.0 = physical card
uniform float u_is_folder;  // 1.0 = folder card style, 0.0 = .ps2 file card
uniform float u_time;

out vec4 o_color;

void main() {
    vec2 p = (v_uv - 0.5) * u_size;
    vec2 halfS = u_size * 0.5;
    float r = 7.0;
    vec2 q = abs(p) - halfS + vec2(r);
    float dBox = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;

    // Signature PS2 memory card chamfered corner notch on top-right
    float notch = (p.x - halfS.x * 0.72) + (-p.y - halfS.y * 0.74);
    float d = max(dBox, notch);

    float aa = 1.1;
    float outerAlpha = 1.0 - smoothstep(-aa, aa, d);
    if (outerAlpha <= 0.002) {
        discard;
    }

    if (u_is_empty > 0.5) {
        // Ghosted translucent empty PS2 memory card port silhouette
        float rim = smoothstep(-2.4, -0.4, d);
        vec3 col = mix(u_body_col.rgb * 0.45, u_accent_col.rgb * 0.65, rim);
        float a = mix(0.22, 0.58, rim) * outerAlpha * u_body_col.a;
        o_color = vec4(col, a);
        return;
    }

    // Matte-glossy dark MagicGate plastic shell gradient
    vec3 shellCol = u_body_col.rgb * mix(1.18, 0.74, v_uv.y);

    // 4 horizontal tactile grip ridges along the bottom quarter (like real PS2 cards)
    if (v_uv.y > 0.73 && v_uv.y < 0.93 && abs(v_uv.x - 0.5) < 0.36) {
        float ridgePhase = sin((v_uv.y - 0.73) * 125.0);
        shellCol += vec3(0.075, 0.09, 0.13) * ridgePhase;
    }

    // Recessed sticker label plate
    vec2 lblMin = vec2(0.14, 0.24);
    vec2 lblMax = vec2(0.86, 0.66);
    vec2 lblCenter = (lblMin + lblMax) * 0.5;
    vec2 lblHalf = (lblMax - lblMin) * 0.5;
    vec2 ld = abs(v_uv - lblCenter) - lblHalf;
    float dLbl = length(max(ld, 0.0)) + min(max(ld.x, ld.y), 0.0);
    float inLbl = 1.0 - smoothstep(-0.012, 0.012, dLbl);
    vec3 lblTint = (u_is_folder > 0.5)
        ? mix(u_body_col.rgb * 1.25, u_accent_col.rgb * 0.42, 0.45)
        : mix(u_body_col.rgb * 0.68, u_accent_col.rgb * 0.26, 0.35);
    shellCol = mix(shellCol, lblTint, inLbl * 0.88);
    float lblBorder = exp(-abs(dLbl) * 140.0) * 0.28;
    shellCol += u_accent_col.rgb * lblBorder;

    // Top-left glowing LED strip (shows slot power/activity)
    vec2 ledD = abs(v_uv - vec2(0.32, 0.115)) - vec2(0.18, 0.022);
    float dLed = length(max(ledD, 0.0)) + min(max(ledD.x, ledD.y), 0.0);
    float ledCore = 1.0 - smoothstep(-0.008, 0.012, dLed);
    float ledGlow = exp(-max(dLed, 0.0) * 32.0);
    float pulse = 0.85 + 0.15 * sin(u_time * 3.0);
    shellCol += u_accent_col.rgb * (ledCore * 0.95 + ledGlow * 0.32 * u_accent_col.a) * pulse;

    // Diagonal sweeping glass reflection when focused
    float diag = (v_uv.x * 0.7 + v_uv.y * 0.5) - fract(u_time * 0.22) * 1.6 + 0.3;
    float sheen = exp(-diag * diag * 18.0) * (0.04 + 0.11 * u_accent_col.a);
    shellCol += vec3(0.75, 0.88, 1.0) * sheen;

    // Crisp rim highlight
    float rim = smoothstep(-2.0, 0.0, d);
    vec3 rimCol = mix(u_body_col.rgb * 1.8, u_accent_col.rgb, 0.45 + 0.55 * u_accent_col.a);
    shellCol = mix(shellCol, rimCol, rim * 0.82);

    o_color = vec4(shellCol, outerAlpha * u_body_col.a);
}
)";

// Optional Subtle CRT Scanlines + Vignette Overlay
inline constexpr const char* kScanlineFragmentShader = R"(#version 300 es
precision highp float;

in vec2 v_uv;
uniform vec2 u_viewport_px;
uniform float u_strength;

out vec4 o_color;

void main() {
    float yPx = v_uv.y * u_viewport_px.y;
    float line = 0.5 - 0.5 * cos(yPx * 3.14159265 * 0.65);
    float dark = line * u_strength;
    vec2 d = v_uv - 0.5;
    float vig = dot(d, d) * u_strength * 0.72;
    float alpha = clamp(dark + vig, 0.0, 0.55);
    o_color = vec4(0.0, 0.0, 0.04, alpha);
}
)";

} // namespace ps2ui
