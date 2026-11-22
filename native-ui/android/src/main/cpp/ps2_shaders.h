#pragma once

namespace ps2ui {

// 2D Textured / Colored Quad Batch Shader
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

// Full-Screen Pure Pitch-Black #000000 Void + Localized Abyssal Mist
inline constexpr const char* kBgVertexShader = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
out vec2 v_uv;

void main() {
    v_uv = a_pos * 0.5 + 0.5;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

inline constexpr const char* kBgFragmentShader = R"(#version 300 es
precision highp float;

in vec2 v_uv;
uniform vec2 u_screen;
uniform vec2 u_mist_center;
uniform float u_time;
uniform float u_mist_strength;
uniform int u_theme;

out vec4 o_color;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    vec2 px = vec2(v_uv.x * u_screen.x, (1.0 - v_uv.y) * u_screen.y);

    // Pure #000000 OLED black void; localized cloud glow only when u_mist_strength > 0
    vec3 bg = vec3(0.0);
    if (u_mist_strength > 0.001) {
        vec3 mistDeep = vec3(0.015, 0.065, 0.24);
        vec3 mistBright = vec3(0.10, 0.36, 0.92);
        if (u_theme == 1) {
            mistDeep = vec3(0.04, 0.05, 0.08);
            mistBright = vec3(0.24, 0.30, 0.42);
        } else if (u_theme == 2) {
            mistDeep = vec3(0.012, 0.12, 0.075);
            mistBright = vec3(0.08, 0.48, 0.30);
        } else if (u_theme == 3) {
            mistDeep = vec3(0.14, 0.02, 0.06);
            mistBright = vec3(0.55, 0.12, 0.28);
        } else if (u_theme == 4) {
            mistDeep = vec3(0.08, 0.025, 0.22);
            mistBright = vec3(0.36, 0.16, 0.72);
        } else if (u_theme == 5) {
            mistDeep = vec3(0.12, 0.075, 0.015);
            mistBright = vec3(0.48, 0.32, 0.08);
        }

        vec2 dM = (px - u_mist_center) / vec2(max(u_screen.y * 0.78, 1.0), max(u_screen.y * 0.56, 1.0));
        float r2 = dot(dM, dM);
        float radial = exp(-r2 * 1.25);

        float nx = px.x / max(u_screen.x, 1.0);
        float ny = px.y / max(u_screen.y, 1.0);
        float swirlA = 0.5 + 0.5 * sin(nx * 5.6 + u_time * 0.32 + cos(ny * 4.1 - u_time * 0.22));
        float swirlB = 0.5 + 0.5 * cos(nx * 3.9 - u_time * 0.25 + ny * 5.1);
        float cloud = radial * (0.42 + 0.58 * swirlA * swirlB) * u_mist_strength;
        bg = mix(mistDeep, mistBright, smoothstep(0.15, 0.95, radial) * 0.58) * cloud;
    }

    float dither = (hash21(gl_FragCoord.xy) - 0.5) / 255.0;
    o_color = vec4(clamp(bg + dither, 0.0, 1.0), 1.0);
}
)";

// Translucent High-Angle 3D Crystal Monolith Towers w/ Exponential PS2 Fog
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
uniform vec3 u_fog_col;
uniform float u_fog_density;
uniform float u_alpha;
uniform float u_silhouette;
uniform float u_time;

out vec4 o_color;

void main() {
    vec3 N = normalize(v_normal);
    vec3 toCam = u_cam_pos - v_world_pos;
    float dist = length(toCam);
    vec3 V = toCam / max(dist, 0.001);

    float ndv = abs(dot(N, V));
    float fresnel = pow(max(0.0, 1.0 - ndv), 2.0);

    float baseFade = smoothstep(0.0, 0.26, v_h);
    float topEdge = smoothstep(0.84, 1.0, v_h);
    float upFace = max(0.0, N.y);

    // Authentic PS2 BIOS translucent sapphire/cobalt crystal monolith shading
    vec3 cobalt = mix(vec3(0.03, 0.14, 0.44), u_accent_col, 0.35 + 0.50 * fresnel);
    float sheen = 0.08 * (0.5 + 0.5 * sin(v_world_pos.y * 2.4 - u_time * 0.7 + v_world_pos.x * 1.1));
    vec3 crystalCol = cobalt +
                      vec3(0.68, 0.90, 1.0) * (upFace * 0.62 + topEdge * 0.48 + fresnel * 0.42) +
                      u_accent_col * sheen;

    float fogT = clamp(exp(-dist * u_fog_density), 0.0, 1.0);
    vec3 finalCol = mix(u_fog_col, crystalCol, fogT);

    float alpha;
    if (u_silhouette > 0.5) {
        alpha = (0.52 + 0.32 * fresnel + 0.24 * upFace + 0.16 * topEdge) *
                baseFade * u_alpha * (0.22 + 0.78 * fogT);
        alpha = clamp(alpha, 0.0, 0.92);
    } else {
        alpha = (0.22 + 0.38 * fresnel + 0.36 * upFace + 0.18 * topEdge) *
                baseFade * v_color.a * u_alpha * (0.28 + 0.72 * fogT);
        alpha = clamp(alpha, 0.0, 0.90);
    }
    o_color = vec4(finalCol, alpha);
}
)";

// Curved 3D Plasma Ribbon Trail Shader (Additive Blending)
inline constexpr const char* kOrbRibbonVertexShader = R"(#version 300 es
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

inline constexpr const char* kOrbRibbonFragmentShader = R"(#version 300 es
precision highp float;

in vec2 v_uv;
in vec4 v_color;

out vec4 o_color;

void main() {
    float cross = (v_uv.x - 0.5) * 2.0;
    float radialGlow = exp(-cross * cross * 5.2);
    float headCore = exp(-cross * cross * 15.0) * exp(-v_uv.y * 3.8);
    float tailFade = pow(clamp(1.0 - v_uv.y, 0.0, 1.0), 1.55);
    vec3 col = mix(v_color.rgb, vec3(0.95, 0.98, 1.0), headCore * 0.85);
    float alpha = (radialGlow * 0.65 + headCore * 0.50) * tailFade * v_color.a;
    o_color = vec4(col * alpha, alpha);
}
)";

// Luminous 4-Point Optical Star Orb Sprite Shader (Additive Blending)
inline constexpr const char* kOrbSpriteFragmentShader = R"(#version 300 es
precision highp float;

in vec2 v_uv;
in vec4 v_color;

out vec4 o_color;

void main() {
    vec2 p = v_uv * 2.0 - 1.0;
    float r2 = dot(p, p);
    if (r2 >= 1.0) discard;

    float aura = exp(-r2 * 4.5) * 0.55;
    float halo = exp(-r2 * 11.0) * 0.72;
    float core = exp(-r2 * 30.0) * 1.15;
    float flareH = exp(-abs(p.y) * 22.0) * exp(-abs(p.x) * 4.2);
    float flareV = exp(-abs(p.x) * 22.0) * exp(-abs(p.y) * 4.2);
    float flare = (flareH + flareV) * 0.28;

    vec3 col = v_color.rgb * (aura * 1.15 + halo * 0.78 + flare * 0.75) + vec3(0.95, 0.98, 1.0) * (core * 1.05 + halo * 0.28 + flare * 0.25);
    float a = (aura + halo + core + flare) * v_color.a;
    o_color = vec4(col * v_color.a, clamp(a, 0.0, 1.0));
}
)";

// Borderless Smoked Glass Modal + Horizontal PS2 Selection Light Beam Shader
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
uniform vec4 u_glow_col;

out vec4 o_color;

float sdRoundedBox(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    // Pure borderless horizontal PS2 light beam mode (no box border)
    if (u_border_px <= 0.01 && u_top_col.a <= 0.01 && u_bot_col.a <= 0.01) {
        if (u_glow_col.w <= 0.001) discard;
        float y = (v_uv.y - 0.5) * 2.0;
        float xFade = sin(clamp(v_uv.x, 0.0, 1.0) * 3.14159265);
        float softBand = exp(-y * y * 3.2) * (0.28 + 0.72 * xFade);
        float sharpCenter = exp(-y * y * 28.0) * xFade * 0.45;
        float a = (softBand * 0.34 + sharpCenter) * u_glow_col.w;
        if (a <= 0.002) discard;
        vec3 col = mix(u_glow_col.rgb, vec3(0.90, 0.97, 1.0), sharpCenter * 0.75);
        o_color = vec4(col, a);
        return;
    }

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

    if (u_glow_col.w > 0.001) {
        vec2 ell = (v_uv - vec2(0.45, 0.5)) * vec2(1.25, 2.2);
        float beam = exp(-dot(ell, ell) * 2.6);
        float horizonLine = exp(-pow((v_uv.y - 0.5) * 8.0, 2.0)) * (1.0 - abs(v_uv.x - 0.5) * 1.4);
        fill.rgb = mix(fill.rgb, u_glow_col.rgb, clamp(beam * 0.68 + max(0.0, horizonLine) * 0.32, 0.0, 1.0) * u_glow_col.w);
    }

    float topSheen = (1.0 - smoothstep(0.0, 0.22, v_uv.y)) * 0.04;
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

// True 3D Extruded & Depth-Tested Mesh Shader (Memory Cards + Real .icn 3D Save Icons)
inline constexpr const char* kMesh3DVertexShader = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_spec;

uniform mat4 u_mvp;
uniform mat4 u_model;

out vec3 v_world_pos;
out vec3 v_normal;
out vec4 v_color;
out float v_spec;

void main() {
    vec4 wp = u_model * vec4(a_pos, 1.0);
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    v_world_pos = wp.xyz;
    v_normal = normalize(mat3(u_model) * a_normal);
    v_color = a_color;
    v_spec = a_spec;
}
)";

inline constexpr const char* kMesh3DFragmentShader = R"(#version 300 es
precision highp float;

in vec3 v_world_pos;
in vec3 v_normal;
in vec4 v_color;
in float v_spec;

uniform vec3 u_cam_pos;
uniform vec3 u_accent_col;
uniform float u_alpha;
uniform float u_ghost;

out vec4 o_color;

void main() {
    vec3 N = normalize(v_normal);
    vec3 V = normalize(u_cam_pos - v_world_pos);
    float ndv = abs(dot(N, V));
    float fresnel = pow(max(0.0, 1.0 - ndv), 2.1);

    if (u_ghost > 0.5) {
        vec3 holo = mix(v_color.rgb * 0.45, u_accent_col, 0.48 + 0.52 * fresnel);
        float ga = (0.14 + 0.62 * fresnel) * u_alpha * v_color.a;
        o_color = vec4(holo, ga);
        return;
    }

    vec3 L1 = normalize(vec3(0.42, 0.72, 0.85));
    vec3 L2 = normalize(vec3(-0.75, 0.30, -0.45));
    float diff1 = max(dot(N, L1), 0.0);
    float diff2 = max(dot(N, L2), 0.0);

    vec3 H1 = normalize(L1 + V);
    float shininess = mix(18.0, 88.0, v_spec);
    float spec = pow(max(dot(N, H1), 0.0), shininess) * (0.18 + 0.88 * v_spec);

    vec3 lit = v_color.rgb * (0.34 + 0.78 * diff1) +
               u_accent_col * (0.28 * diff2 + 0.32 * fresnel) +
               vec3(0.95, 0.98, 1.0) * spec;

    o_color = vec4(lit, clamp(v_color.a * u_alpha, 0.0, 1.0));
}
)";

// True 3D Perspective-Tilted Optical Disc Shader (0 = Silver DVD-ROM, 1 = Cobalt Blue CD-ROM)
inline constexpr const char* kDiscVertexShader = R"(#version 300 es
layout(location = 0) in vec2 a_pos;

uniform mat4 u_mvp;
uniform float u_radius;

out vec2 v_uv;

void main() {
    vec4 world = vec4(a_pos.x * u_radius, a_pos.y * u_radius, 0.0, 1.0);
    gl_Position = u_mvp * world;
    v_uv = a_pos * 0.5 + 0.5;
}
)";

inline constexpr const char* kDiscFragmentShader = R"(#version 300 es
precision highp float;

in vec2 v_uv;
uniform float u_angle;
uniform float u_alpha;
uniform int u_disc_type;

out vec4 o_color;

void main() {
    vec2 p = v_uv * 2.0 - 1.0;
    float r = length(p);
    float aa = 0.015;

    if (r > 1.0 || r < 0.145) {
        discard;
    }

    float outerMask = 1.0 - smoothstep(1.0 - aa, 1.0, r);
    float holeMask = smoothstep(0.145, 0.145 + aa, r);
    float mask = outerMask * holeMask;

    float ang = atan(p.y, p.x) + u_angle;
    vec3 col;

    if (r > 0.968) {
        col = (u_disc_type == 1) ? vec3(0.46, 0.62, 0.92) : vec3(0.86, 0.90, 0.96);
    } else if (r > 0.35) {
        float c2 = max(0.0, cos(ang * 2.0));
        float c2s = max(0.0, cos(ang * 2.0 + 1.35));
        float cone1 = c2 * c2 * c2 * c2 * c2;
        float cone2 = c2s * c2s * c2s * c2s * c2s * c2s;
        float trackRings = 0.032 * sin(r * 170.0);
        float cSpec = max(0.0, cos(ang - 0.65));
        float spec = pow(cSpec, 8.0);

        if (u_disc_type == 1) {
            vec3 baseBlue = vec3(0.04, 0.14, 0.48);
            vec3 iridCyan = vec3(0.20, 0.76, 1.0) * cone1 * 0.58;
            vec3 iridViolet = vec3(0.56, 0.25, 0.96) * cone2 * 0.44;
            col = baseBlue + iridCyan + iridViolet + vec3(trackRings * 0.28);
            col += vec3(0.70, 0.86, 1.0) * spec * 0.45;
        } else {
            vec3 baseSilver = vec3(0.68, 0.72, 0.80);
            vec3 iridGold = vec3(0.32, 0.24, 0.06) * cone1;
            vec3 iridCyan = vec3(0.12, 0.30, 0.38) * cone2;
            col = baseSilver + iridGold + iridCyan + vec3(trackRings * 0.32);
            col += vec3(1.0, 0.99, 0.96) * spec * 0.30;
        }
    } else if (r > 0.25) {
        float ringSpec = 0.76 + 0.22 * cos(ang * 2.0);
        col = ((u_disc_type == 1) ? vec3(0.36, 0.50, 0.78) : vec3(0.58, 0.62, 0.70)) * ringSpec;
    } else {
        col = (u_disc_type == 1) ? vec3(0.20, 0.30, 0.54) : vec3(0.34, 0.38, 0.48);
    }

    float rimLine = exp(-pow((r - 0.35) * 90.0, 2.0)) * 0.40;
    float hubLine = exp(-pow((r - 0.25) * 90.0, 2.0)) * 0.40;
    col += vec3(0.78, 0.88, 1.0) * (rimLine + hubLine);

    o_color = vec4(col, mask * u_alpha);
}
)";

// Authentic PlayStation 2 Disc Boot Light Waves Splash Shader (Additive over #000000)
inline constexpr const char* kLaunchSplashFragmentShader = R"(#version 300 es
precision highp float;

in vec2 v_uv;
uniform vec2 u_screen;
uniform float u_progress;
uniform float u_time;
uniform vec3 u_accent_col;

out vec4 o_color;

void main() {
    vec2 uv = v_uv - 0.5;
    float env = sin(clamp(u_progress, 0.0, 1.0) * 3.14159265);
    float expand = 0.25 + 0.95 * u_progress;

    // Sweeping vertical refraction light shafts parting outward from center
    float w1 = exp(-pow((uv.x + 0.16 * expand + 0.02 * sin(uv.y * 5.0 + u_time * 3.0)) * 7.5, 2.0));
    float w2 = exp(-pow((uv.x - 0.16 * expand + 0.02 * cos(uv.y * 5.0 - u_time * 3.0)) * 7.5, 2.0));
    float w3 = exp(-pow((uv.x + 0.34 * expand) * 5.0, 2.0)) * 0.55;
    float w4 = exp(-pow((uv.x - 0.34 * expand) * 5.0, 2.0)) * 0.55;

    // Razor-sharp horizontal optical flare behind "PlayStation 2" logo
    float horizFlare = exp(-abs(uv.y) * 36.0) * exp(-abs(uv.x) * 2.1);
    float centerBloom = exp(-dot(uv * vec2(1.4, 2.4), uv * vec2(1.4, 2.4)) * 5.5);

    vec3 shafts = u_accent_col * ((w1 + w2 + w3 + w4) * (0.45 + 0.55 * cos(uv.y * 3.0)) * 0.55);
    vec3 flareCol = mix(u_accent_col, vec3(0.88, 0.96, 1.0), 0.70) * (horizFlare * 0.72 + centerBloom * 0.42);
    vec3 col = (shafts + flareCol) * env;
    float a = clamp(max(max(col.r, col.g), col.b), 0.0, 1.0);
    o_color = vec4(col, a);
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
    float vig = dot(d, d) * u_strength * 0.65;
    float alpha = clamp(dark + vig, 0.0, 0.55);
    o_color = vec4(0.0, 0.0, 0.0, alpha);
}
)";

} // namespace ps2ui
