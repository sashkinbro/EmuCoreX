#include "ps2_renderer.h"
#include "ps2_shaders.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <time.h>
#include <vector>

#if defined(PS2UI_DISABLE_LOGS) || defined(NDEBUG)
#define LOGE(...) ((void)0)
#define LOGI(...) ((void)0)
#else
#include <android/log.h>
#define LOG_TAG "Ps2Renderer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#endif

namespace ps2ui {

namespace {

constexpr size_t kMaxTextCache = 512;
// GPU memory cap for cached text textures (raster scale goes up to 4x on tall panels).
constexpr size_t kMaxTextCacheBytes = 48u * 1024u * 1024u;
// Per-frame time budget for new text rasterization (JNI -> Kotlin StaticLayout). At least one
// string is always rasterized per frame; the rest appear on following frames instead of
// stalling a single frame when a long list scrolls in.
constexpr double kTextRasterBudgetMs = 8.0;

double MonotonicMs() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) * 1e-6;
}
// 7 orbs * 28 trail segments * 6 verts + 7 sprites * 6 verts, with headroom.
constexpr size_t kOrbVboVerts = 1536;
constexpr size_t kMaxImageCache = 64;
constexpr int kSettingsCount = 16;
constexpr int kThemeCount = 6;
constexpr float kPi = 3.1415926535f;

float Clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

float SmoothStep(float a, float b, float x) {
    float t = Clamp01((x - a) / std::max(0.0001f, b - a));
    return t * t * (3.0f - 2.0f * t);
}

float EaseOutCubic(float t) {
    t = Clamp01(t);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

bool IsArcadeEntry(const GameEntry& g) {
    if (g.path.size() >= 7) {
        const size_t n = g.path.size();
        const char* ext = ".acgame";
        bool match = true;
        for (size_t i = 0; i < 7; ++i) {
            if (std::tolower(static_cast<unsigned char>(g.path[n - 7 + i])) != ext[i]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    if (g.serial.size() >= 2) {
        const char c0 = static_cast<char>(std::toupper(static_cast<unsigned char>(g.serial[0])));
        const char c1 = static_cast<char>(std::toupper(static_cast<unsigned char>(g.serial[1])));
        if (c0 == 'N' && c1 == 'M') return true;
    }
    return false;
}

std::string MakeTextCacheKey(
    const std::string& text,
    float fontSize,
    int style,
    float maxWidth,
    int maxLines,
    int align) {
    char buf[96];
    std::snprintf(
        buf,
        sizeof(buf),
        "|%.1f|%d|%.0f|%d|%d",
        fontSize,
        style,
        maxWidth,
        maxLines,
        align);
    return text + buf;
}

void Mat4Identity(float* out) {
    std::memset(out, 0, 16 * sizeof(float));
    out[0] = out[5] = out[10] = out[15] = 1.0f;
}

void Mat4Mul(const float* a, const float* b, float* out) {
    float tmp[16];
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            tmp[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    std::memcpy(out, tmp, sizeof(tmp));
}

void MakeScale(float sx, float sy, float sz, float* out) {
    Mat4Identity(out);
    out[0] = sx;
    out[5] = sy;
    out[10] = sz;
}

void MakeRotateX(float rad, float* out) {
    Mat4Identity(out);
    float c = std::cos(rad), s = std::sin(rad);
    out[5] = c;  out[9] = -s;
    out[6] = s;  out[10] = c;
}

void MakeRotateY(float rad, float* out) {
    Mat4Identity(out);
    float c = std::cos(rad), s = std::sin(rad);
    out[0] = c;   out[8] = s;
    out[2] = -s;  out[10] = c;
}

void MakeRotateZ(float rad, float* out) {
    Mat4Identity(out);
    float c = std::cos(rad), s = std::sin(rad);
    out[0] = c; out[4] = -s;
    out[1] = s; out[5] = c;
}

void MakePerspective(float fovRad, float aspect, float zNear, float zFar, float* out) {
    std::memset(out, 0, 16 * sizeof(float));
    float f = 1.0f / std::tan(fovRad * 0.5f);
    out[0] = f / std::max(0.1f, aspect);
    out[5] = f;
    out[10] = (zFar + zNear) / (zNear - zFar);
    out[11] = -1.0f;
    out[14] = (2.0f * zFar * zNear) / (zNear - zFar);
}

void MakeLookAt(
    float ex, float ey, float ez,
    float cx, float cy, float cz,
    float ux, float uy, float uz,
    float* out) {
    float fx = cx - ex;
    float fy = cy - ey;
    float fz = cz - ez;
    float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (fl < 1e-5f) fl = 1.0f;
    fx /= fl; fy /= fl; fz /= fl;

    float sx = fy * uz - fz * uy;
    float sy = fz * ux - fx * uz;
    float sz = fx * uy - fy * ux;
    float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
    if (sl < 1e-5f) sl = 1.0f;
    sx /= sl; sy /= sl; sz /= sl;

    float vx = sy * fz - sz * fy;
    float vy = sz * fx - sx * fz;
    float vz = sx * fy - sy * fx;

    out[0] = sx;  out[4] = sy;  out[8]  = sz;  out[12] = -(sx * ex + sy * ey + sz * ez);
    out[1] = vx;  out[5] = vy;  out[9]  = vz;  out[13] = -(vx * ex + vy * ey + vz * ez);
    out[2] = -fx; out[6] = -fy; out[10] = -fz; out[14] = (fx * ex + fy * ey + fz * ez);
    out[3] = 0.0f; out[7] = 0.0f; out[11] = 0.0f; out[15] = 1.0f;
}

} // namespace

bool Ps2Renderer::Init() {
    if (gl_ready_) {
        Shutdown();
    }
    if (!CompileShaders()) {
        LOGE("Failed to compile PS2 UI shaders");
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(4 * sizeof(float)));

    // Dedicated stream buffer for orb ribbons + sprites: fixed size, orphaned once per frame,
    // so the shared quad VBO is never re-allocated mid-frame.
    glGenVertexArrays(1, &orb_vao_);
    glGenBuffers(1, &orb_vbo_);
    glBindVertexArray(orb_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, orb_vbo_);
    glBufferData(GL_ARRAY_BUFFER, kOrbVboVerts * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(4 * sizeof(float)));

    const float fsQuad[8] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };
    glGenVertexArrays(1, &fs_vao_);
    glGenBuffers(1, &fs_vbo_);
    glBindVertexArray(fs_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, fs_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(fsQuad), fsQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    // 3D Crystal Towers buffer (64 towers * 30 verts = 1920 verts, reserve 4096)
    glGenVertexArrays(1, &tower_vao_);
    glGenBuffers(1, &tower_vbo_);
    glBindVertexArray(tower_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, tower_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(TowerVertex), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TowerVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TowerVertex), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(TowerVertex), reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(TowerVertex), reinterpret_cast<void*>(7 * sizeof(float)));

    // 3D Memory Card & .icn Save Icon Mesh buffer
    glGenVertexArrays(1, &mesh3d_vao_);
    glGenBuffers(1, &mesh3d_vbo_);
    glBindVertexArray(mesh3d_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, mesh3d_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(Mesh3DVertex), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Mesh3DVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Mesh3DVertex), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Mesh3DVertex), reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Mesh3DVertex), reinterpret_cast<void*>(10 * sizeof(float)));
    glBindVertexArray(0);

    const uint8_t whitePx[4] = {255, 255, 255, 255};
    glGenTextures(1, &white_tex_);
    glBindTexture(GL_TEXTURE_2D, white_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePx);

    batch_verts_.reserve(4096);
    ribbon_verts_.reserve(2048);
    orb_sprite_verts_.reserve(64);
    tower_verts_.reserve(2048);
    hit_targets_.reserve(64);

    RebuildTowersLayout();
    BuildMemoryCardMeshes();

    has_frame_tp_ = false;
    gl_ready_ = true;
    LOGI("PS2 native UI GLES3 renderer initialized (v2)");
    return true;
}

void Ps2Renderer::Shutdown() {
    ClearTextCache();
    ClearImageCache();
    if (white_tex_) {
        glDeleteTextures(1, &white_tex_);
        white_tex_ = 0;
    }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (fs_vbo_) { glDeleteBuffers(1, &fs_vbo_); fs_vbo_ = 0; }
    if (fs_vao_) { glDeleteVertexArrays(1, &fs_vao_); fs_vao_ = 0; }
    if (tower_vbo_) { glDeleteBuffers(1, &tower_vbo_); tower_vbo_ = 0; }
    if (tower_vao_) { glDeleteVertexArrays(1, &tower_vao_); tower_vao_ = 0; }
    if (orb_vbo_) { glDeleteBuffers(1, &orb_vbo_); orb_vbo_ = 0; }
    if (orb_vao_) { glDeleteVertexArrays(1, &orb_vao_); orb_vao_ = 0; }
    if (mesh3d_vbo_) { glDeleteBuffers(1, &mesh3d_vbo_); mesh3d_vbo_ = 0; }
    if (mesh3d_vao_) { glDeleteVertexArrays(1, &mesh3d_vao_); mesh3d_vao_ = 0; }

    auto delProg = [](GLuint& p) {
        if (p) {
            glDeleteProgram(p);
            p = 0;
        }
    };
    delProg(prog_quad_);
    delProg(prog_bg_);
    delProg(prog_tower_);
    delProg(prog_boot_cube_);
    for (GLuint* t : {&bios_fog_tex_, &bios_refl_tex_, &bios_glow_tex_, &bios_noise_tex_, &scene_copy_tex_, &boot_prev_tex_, &bios_smoke_tex_}) {
        if (*t) { glDeleteTextures(1, t); *t = 0; }
    }
    for (GLuint& t : bios_cloud_tex_) {
        if (t) { glDeleteTextures(1, &t); t = 0; }
    }
    bios_assets_gen_ = 0;
    scene_copy_w_ = 0;
    scene_copy_h_ = 0;
    boot_prev_w_ = 0;
    boot_prev_h_ = 0;
    boot_prev_valid_ = false;
    delProg(prog_ribbon_);
    delProg(prog_orb_);
    delProg(prog_panel_);
    delProg(prog_disc_);
    delProg(prog_mesh3d_);
    delProg(prog_launch_);
    delProg(prog_scan_);
    gl_ready_ = false;
}

GLuint Ps2Renderer::CompileProgram(const char* vsSrc, const char* fsSrc) {
    auto compileShader = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
#if !defined(PS2UI_DISABLE_LOGS) && !defined(NDEBUG)
            char logBuf[1024] = {};
            glGetShaderInfoLog(s, sizeof(logBuf), nullptr, logBuf);
            LOGE("Shader compile error: %s", logBuf);
#endif
            glDeleteShader(s);
            return 0;
        }
        return s;
    };

    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
#if !defined(PS2UI_DISABLE_LOGS) && !defined(NDEBUG)
        char logBuf[1024] = {};
        glGetProgramInfoLog(prog, sizeof(logBuf), nullptr, logBuf);
        LOGE("Program link error: %s", logBuf);
#endif
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

bool Ps2Renderer::CompileShaders() {
    prog_quad_ = CompileProgram(kQuadVertexShader, kQuadFragmentShader);
    prog_bg_ = CompileProgram(kBgVertexShader, kBgFragmentShader);
    prog_tower_ = CompileProgram(kTowerVertexShader, kTowerFragmentShader);
    prog_boot_cube_ = CompileProgram(kTowerVertexShader, kBootCubeFragmentShader);
    prog_ribbon_ = CompileProgram(kOrbRibbonVertexShader, kOrbRibbonFragmentShader);
    prog_orb_ = CompileProgram(kOrbRibbonVertexShader, kOrbSpriteFragmentShader);
    prog_panel_ = CompileProgram(kPanelVertexShader, kPanelFragmentShader);
    prog_disc_ = CompileProgram(kDiscVertexShader, kDiscFragmentShader);
    prog_mesh3d_ = CompileProgram(kMesh3DVertexShader, kMesh3DFragmentShader);
    prog_launch_ = CompileProgram(kBgVertexShader, kLaunchSplashFragmentShader);
    prog_scan_ = CompileProgram(kBgVertexShader, kScanlineFragmentShader);

    if (!prog_quad_ || !prog_bg_ || !prog_tower_ || !prog_boot_cube_ || !prog_ribbon_ ||
        !prog_orb_ || !prog_panel_ || !prog_disc_ || !prog_mesh3d_ ||
        !prog_launch_ || !prog_scan_) {
        return false;
    }

    quad_u_screen_ = glGetUniformLocation(prog_quad_, "u_screen");
    quad_u_texture_ = glGetUniformLocation(prog_quad_, "u_texture");

    bg_u_screen_ = glGetUniformLocation(prog_bg_, "u_screen");
    bg_u_mist_center_ = glGetUniformLocation(prog_bg_, "u_mist_center");
    bg_u_time_ = glGetUniformLocation(prog_bg_, "u_time");
    bg_u_mist_strength_ = glGetUniformLocation(prog_bg_, "u_mist_strength");
    bg_u_theme_ = glGetUniformLocation(prog_bg_, "u_theme");

    twr_u_mvp_ = glGetUniformLocation(prog_tower_, "u_mvp");
    twr_u_cam_pos_ = glGetUniformLocation(prog_tower_, "u_cam_pos");
    twr_u_accent_col_ = glGetUniformLocation(prog_tower_, "u_accent_col");
    twr_u_fog_col_ = glGetUniformLocation(prog_tower_, "u_fog_col");
    twr_u_fog_density_ = glGetUniformLocation(prog_tower_, "u_fog_density");
    twr_u_alpha_ = glGetUniformLocation(prog_tower_, "u_alpha");
    twr_u_silhouette_ = glGetUniformLocation(prog_tower_, "u_silhouette");
    twr_u_time_ = glGetUniformLocation(prog_tower_, "u_time");
    bcube_u_mvp_ = glGetUniformLocation(prog_boot_cube_, "u_mvp");
    bcube_u_cam_pos_ = glGetUniformLocation(prog_boot_cube_, "u_cam_pos");
    bcube_u_alpha_ = glGetUniformLocation(prog_boot_cube_, "u_alpha");
    bcube_u_refl_ = glGetUniformLocation(prog_boot_cube_, "u_refl");
    bcube_u_has_refl_ = glGetUniformLocation(prog_boot_cube_, "u_has_refl");
    bcube_u_glass_ = glGetUniformLocation(prog_boot_cube_, "u_glass");
    bcube_u_scene_ = glGetUniformLocation(prog_boot_cube_, "u_scene");
    bcube_u_view_ = glGetUniformLocation(prog_boot_cube_, "u_view");
    bcube_u_noise_ = glGetUniformLocation(prog_boot_cube_, "u_noise");
    bcube_u_time_ = glGetUniformLocation(prog_boot_cube_, "u_time");
    bcube_u_smoke_ = glGetUniformLocation(prog_boot_cube_, "u_smoke");

    rib_u_screen_ = glGetUniformLocation(prog_ribbon_, "u_screen");
    orb_u_screen_ = glGetUniformLocation(prog_orb_, "u_screen");

    pan_u_screen_ = glGetUniformLocation(prog_panel_, "u_screen");
    pan_u_rect_ = glGetUniformLocation(prog_panel_, "u_rect");
    pan_u_size_ = glGetUniformLocation(prog_panel_, "u_size");
    pan_u_radius_ = glGetUniformLocation(prog_panel_, "u_radius");
    pan_u_border_px_ = glGetUniformLocation(prog_panel_, "u_border_px");
    pan_u_top_col_ = glGetUniformLocation(prog_panel_, "u_top_col");
    pan_u_bot_col_ = glGetUniformLocation(prog_panel_, "u_bot_col");
    pan_u_border_col_ = glGetUniformLocation(prog_panel_, "u_border_col");
    pan_u_glow_col_ = glGetUniformLocation(prog_panel_, "u_glow_col");

    disc_u_mvp_ = glGetUniformLocation(prog_disc_, "u_mvp");
    disc_u_radius_ = glGetUniformLocation(prog_disc_, "u_radius");
    disc_u_angle_ = glGetUniformLocation(prog_disc_, "u_angle");
    disc_u_alpha_ = glGetUniformLocation(prog_disc_, "u_alpha");
    disc_u_disc_type_ = glGetUniformLocation(prog_disc_, "u_disc_type");

    m3d_u_mvp_ = glGetUniformLocation(prog_mesh3d_, "u_mvp");
    m3d_u_model_ = glGetUniformLocation(prog_mesh3d_, "u_model");
    m3d_u_cam_pos_ = glGetUniformLocation(prog_mesh3d_, "u_cam_pos");
    m3d_u_accent_col_ = glGetUniformLocation(prog_mesh3d_, "u_accent_col");
    m3d_u_alpha_ = glGetUniformLocation(prog_mesh3d_, "u_alpha");
    m3d_u_ghost_ = glGetUniformLocation(prog_mesh3d_, "u_ghost");

    lch_u_screen_ = glGetUniformLocation(prog_launch_, "u_screen");
    lch_u_progress_ = glGetUniformLocation(prog_launch_, "u_progress");
    lch_u_time_ = glGetUniformLocation(prog_launch_, "u_time");
    lch_u_accent_col_ = glGetUniformLocation(prog_launch_, "u_accent_col");

    scan_u_viewport_ = glGetUniformLocation(prog_scan_, "u_viewport_px");
    scan_u_strength_ = glGetUniformLocation(prog_scan_, "u_strength");

    return true;
}

void Ps2Renderer::Resize(int width, int height) {
    surface_width_ = std::max(1, width);
    surface_height_ = std::max(1, height);
}

void Ps2Renderer::SetUiScale(float scale) {
    ui_scale_ = std::clamp(scale, 0.90f, 1.45f);
}

void Ps2Renderer::ClearTextCache() {
    for (auto& kv : text_cache_) {
        if (kv.second.tex) glDeleteTextures(1, &kv.second.tex);
    }
    text_cache_.clear();
    text_lru_.clear();
    text_cache_bytes_ = 0;
}

void Ps2Renderer::ClearImageCache() {
    for (auto& kv : image_cache_) {
        if (kv.second.tex) glDeleteTextures(1, &kv.second.tex);
    }
    image_cache_.clear();
    image_lru_.clear();
}

void Ps2Renderer::EvictTextCacheIfNeeded() {
    while ((text_cache_.size() >= kMaxTextCache || text_cache_bytes_ > kMaxTextCacheBytes) &&
           !text_lru_.empty()) {
        const std::string oldest = text_lru_.back();
        text_lru_.pop_back();
        auto it = text_cache_.find(oldest);
        if (it != text_cache_.end()) {
            if (it->second.tex) glDeleteTextures(1, &it->second.tex);
            text_cache_bytes_ -= std::min(text_cache_bytes_, it->second.bytes);
            text_cache_.erase(it);
        }
    }
}

void Ps2Renderer::EvictImageCacheIfNeeded() {
    while (image_cache_.size() >= kMaxImageCache && !image_lru_.empty()) {
        const std::string oldest = image_lru_.back();
        image_lru_.pop_back();
        auto it = image_cache_.find(oldest);
        if (it != image_cache_.end()) {
            if (it->second.tex) glDeleteTextures(1, &it->second.tex);
            image_cache_.erase(it);
        }
    }
}

const Ps2Renderer::TextCacheEntry& Ps2Renderer::GetOrCreateText(
    const std::string& text,
    float fontSize,
    int style,
    float maxWidth,
    int maxLines,
    int align) {
    static const TextCacheEntry kEmpty{};
    if (text.empty()) return kEmpty;

    const int effAlign = (maxLines <= 1) ? 0 : align;
    const std::string key = MakeTextCacheKey(text, fontSize, style, maxWidth, maxLines, effAlign);
    auto it = text_cache_.find(key);
    if (it != text_cache_.end()) {
        if (it->second.lruIt != text_lru_.begin()) {
            text_lru_.splice(text_lru_.begin(), text_lru_, it->second.lruIt);
        }
        return it->second;
    }

    if (text_raster_ms_this_frame_ >= kTextRasterBudgetMs) {
        return kEmpty;
    }
    const double rasterStartMs = MonotonicMs();

    int texW = 0;
    int texH = 0;
    float logW = 0.0f;
    float logH = 0.0f;
    std::vector<uint8_t> rgba;
    const bool rastered = HostRasterizeText(
            text, fontSize, style, maxWidth, maxLines, effAlign, raster_scale_,
            texW, texH, logW, logH, rgba);
    text_raster_ms_this_frame_ += std::max(0.05, MonotonicMs() - rasterStartMs);
    if (!rastered) {
        return kEmpty;
    }

    EvictTextCacheIfNeeded();

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    text_lru_.push_front(key);
    const size_t texBytes = static_cast<size_t>(texW) * static_cast<size_t>(texH) * 4u;
    text_cache_bytes_ += texBytes;
    TextCacheEntry entry{tex, logW, logH, texBytes, text_lru_.begin()};
    auto [insIt, _] = text_cache_.emplace(key, entry);
    return insIt->second;
}

const Ps2Renderer::ImageCacheEntry& Ps2Renderer::GetOrCreateImage(const std::string& path, int maxDim) {
    static const ImageCacheEntry kEmpty{};
    if (path.empty()) return kEmpty;

    const int effMaxDim = std::clamp(maxDim, 64, 384);
    const std::string& key = path;
    auto it = image_cache_.find(key);
    if (it != image_cache_.end()) {
        if (it->second.lruIt != image_lru_.begin()) {
            image_lru_.splice(image_lru_.begin(), image_lru_, it->second.lruIt);
        }
        return it->second;
    }

    const int maxDecodesPerFrame = launching_ ? 2 : 1;
    if (image_decodes_this_frame_ >= maxDecodesPerFrame) {
        return kEmpty;
    }
    ++image_decodes_this_frame_;

    EvictImageCacheIfNeeded();

    ImageCacheEntry entry{};
    entry.attempted = true;
    std::vector<uint8_t> rgba;
    if (HostDecodeImage(path, effMaxDim, entry.w, entry.h, rgba) && entry.w > 0 && entry.h > 0) {
        glGenTextures(1, &entry.tex);
        glBindTexture(GL_TEXTURE_2D, entry.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, entry.w, entry.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    image_lru_.push_front(key);
    entry.lruIt = image_lru_.begin();
    auto [insIt, _] = image_cache_.emplace(key, entry);
    return insIt->second;
}

void Ps2Renderer::MeasureText(
    const std::string& text,
    float fontSize,
    int style,
    float maxWidth,
    int maxLines,
    float& outW,
    float& outH) {
    const auto& entry = GetOrCreateText(text, fontSize, style, maxWidth, maxLines, 0);
    outW = entry.logW;
    outH = entry.logH;
}

void Ps2Renderer::DrawText(
    const std::string& text,
    float x,
    float y,
    float fontSize,
    int style,
    const RgbaColor& color,
    float maxWidth,
    int maxLines,
    int align,
    float boxW) {
    if (text.empty() || color.a <= 0.005f) return;
    const auto& entry = GetOrCreateText(text, fontSize, style, maxWidth, maxLines, align);
    if (!entry.tex || entry.logW <= 0.0f || entry.logH <= 0.0f) return;

    float drawX = x;
    if (align == 1 && boxW > 0.0f) {
        drawX = x + (boxW - entry.logW) * 0.5f;
    } else if (align == 2 && boxW > 0.0f) {
        drawX = x + (boxW - entry.logW);
    }
    PushQuad(entry.tex, drawX, y, entry.logW, entry.logH, color);
}

void Ps2Renderer::BeginQuadBatch() {
    batch_verts_.clear();
    batch_tex_ = 0;
}

void Ps2Renderer::FlushQuadBatch() {
    if (batch_verts_.empty() || !batch_tex_) {
        batch_verts_.clear();
        return;
    }
    glUseProgram(prog_quad_);
    glUniform2f(quad_u_screen_, screen_width_, kScreenHeight);
    glUniform1i(quad_u_texture_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, batch_tex_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const size_t byteSize = batch_verts_.size() * sizeof(Vertex);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(byteSize), batch_verts_.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch_verts_.size()));
    batch_verts_.clear();
}

void Ps2Renderer::PushQuad(
    GLuint tex,
    float x, float y, float w, float h,
    const RgbaColor& cTop,
    const RgbaColor& cBot,
    float u0, float v0, float u1, float v1) {
    if (!tex || w <= 0.0f || h <= 0.0f) return;
    if (batch_tex_ != tex || batch_verts_.size() >= 3900) {
        FlushQuadBatch();
        batch_tex_ = tex;
    }
    const float x2 = x + w;
    const float y2 = y + h;
    batch_verts_.push_back({x,  y,  u0, v0, cTop.r, cTop.g, cTop.b, cTop.a});
    batch_verts_.push_back({x2, y,  u1, v0, cTop.r, cTop.g, cTop.b, cTop.a});
    batch_verts_.push_back({x2, y2, u1, v1, cBot.r, cBot.g, cBot.b, cBot.a});
    batch_verts_.push_back({x,  y,  u0, v0, cTop.r, cTop.g, cTop.b, cTop.a});
    batch_verts_.push_back({x2, y2, u1, v1, cBot.r, cBot.g, cBot.b, cBot.a});
    batch_verts_.push_back({x,  y2, u0, v1, cBot.r, cBot.g, cBot.b, cBot.a});
}

void Ps2Renderer::PushQuad(GLuint tex, float x, float y, float w, float h, const RgbaColor& c) {
    PushQuad(tex, x, y, w, h, c, c);
}

void Ps2Renderer::PushTriangle(
    float x0, float y0, const RgbaColor& c0,
    float x1, float y1, const RgbaColor& c1,
    float x2, float y2, const RgbaColor& c2) {
    if (batch_tex_ != white_tex_ || batch_verts_.size() >= 3900) {
        FlushQuadBatch();
        batch_tex_ = white_tex_;
    }
    batch_verts_.push_back({x0, y0, 0.5f, 0.5f, c0.r, c0.g, c0.b, c0.a});
    batch_verts_.push_back({x1, y1, 0.5f, 0.5f, c1.r, c1.g, c1.b, c1.a});
    batch_verts_.push_back({x2, y2, 0.5f, 0.5f, c2.r, c2.g, c2.b, c2.a});
}

void Ps2Renderer::DrawPanel(
    float x, float y, float w, float h,
    float radius,
    float borderPx,
    const RgbaColor& topCol,
    const RgbaColor& botCol,
    const RgbaColor& borderCol,
    const RgbaColor& glowCol) {
    if (w <= 0.5f || h <= 0.5f) return;
    FlushQuadBatch();

    glUseProgram(prog_panel_);
    glUniform2f(pan_u_screen_, screen_width_, kScreenHeight);
    glUniform4f(pan_u_rect_, x, y, w, h);
    glUniform2f(pan_u_size_, w, h);
    glUniform1f(pan_u_radius_, radius);
    glUniform1f(pan_u_border_px_, borderPx);
    glUniform4f(pan_u_top_col_, topCol.r, topCol.g, topCol.b, topCol.a);
    glUniform4f(pan_u_bot_col_, botCol.r, botCol.g, botCol.b, botCol.a);
    glUniform4f(pan_u_border_col_, borderCol.r, borderCol.g, borderCol.b, borderCol.a);
    glUniform4f(pan_u_glow_col_, glowCol.r, glowCol.g, glowCol.b, glowCol.a);

    glBindVertexArray(fs_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Ps2Renderer::DrawFocusBeam(float x, float y, float w, float h, const RgbaColor& col, float alpha) {
    if (alpha <= 0.01f) return;
    DrawPanel(x, y, w, h, 0.0f, 0.0f,
              {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f},
              {0.0f, 0.0f, 0.0f, 0.0f}, {col.r, col.g, col.b, alpha});
}

void Ps2Renderer::DrawOpticalDisc3D(
    float cx, float cy, float radius,
    float angleRad, float tiltXRad, float tiltYRad,
    int discType, float alpha) {
    if (alpha <= 0.005f || radius <= 2.0f) return;
    FlushQuadBatch();

    // Render disc inside a generous square perspective viewport centered on (cx, cy)
    const float vpHalf = radius * 1.45f;
    const float vx = cx - vpHalf;
    const float vy = cy - vpHalf;
    const float vw = vpHalf * 2.0f;
    const float vh = vpHalf * 2.0f;

    const float sx = static_cast<float>(view_width_) / screen_width_;
    const float sy = static_cast<float>(view_height_) / kScreenHeight;
    const int pxX = view_x_ + static_cast<int>(std::round(vx * sx));
    const int pxY = view_y_ + static_cast<int>(std::round((kScreenHeight - (vy + vh)) * sy));
    const int pxW = std::max(1, static_cast<int>(std::round(vw * sx)));
    const int pxH = std::max(1, static_cast<int>(std::round(vh * sy)));

    glViewport(pxX, pxY, pxW, pxH);

    float proj[16], viewM[16], rx[16], ry[16], rot[16], vp[16], mvp[16];
    MakePerspective(0.68f, 1.0f, 0.5f, 20.0f, proj);
    MakeLookAt(0.0f, 0.0f, 4.1f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, viewM);
    MakeRotateY(tiltYRad, ry);
    MakeRotateX(tiltXRad, rx);
    Mat4Mul(ry, rx, rot);
    Mat4Mul(proj, viewM, vp);
    Mat4Mul(vp, rot, mvp);

    glUseProgram(prog_disc_);
    glUniformMatrix4fv(disc_u_mvp_, 1, GL_FALSE, mvp);
    glUniform1f(disc_u_radius_, 1.02f);
    glUniform1f(disc_u_angle_, angleRad);
    glUniform1f(disc_u_alpha_, alpha);
    glUniform1i(disc_u_disc_type_, discType);

    glBindVertexArray(fs_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glViewport(view_x_, view_y_, view_width_, view_height_);
}

void Ps2Renderer::SetCursorTarget(float x, float y) {
    if (!cursor_pos_init_) {
        cursor_x_ = cursor_target_x_ = cursor_trail_x_ = x;
        cursor_y_ = cursor_target_y_ = cursor_trail_y_ = y;
        cursor_pos_init_ = true;
        return;
    }
    if (std::fabs(cursor_target_x_ - x) > 1.5f || std::fabs(cursor_target_y_ - y) > 1.5f) {
        cursor_flash_t_ = 1.0f;
    }
    cursor_target_x_ = x;
    cursor_target_y_ = y;
}

void Ps2Renderer::DrawAnimatedCursor(float x, float y, float size, float alpha) {
    if (alpha <= 0.01f) return;
    const float bob = std::sin(time_sec_ * 5.2f) * 3.8f;
    const float pulse = 1.0f + 0.11f * std::sin(time_sec_ * 4.6f) + 0.22f * cursor_flash_t_;
    const float px = x + bob;
    const float py = y;
    const float r = size * pulse;
    const RgbaColor acc = AccentColor(alpha);
    const RgbaColor core{0.97f, 0.99f, 1.00f, alpha};

    // Soft additive halo corona behind cursor
    FlushQuadBatch();
    glBlendFunc(GL_ONE, GL_ONE);
    BeginQuadBatch();

    const float trDx = cursor_trail_x_ - px;
    const float trDy = cursor_trail_y_ - py;
    if (trDx * trDx + trDy * trDy > 16.0f) {
        PushTriangle(cursor_trail_x_ - r * 0.4f, cursor_trail_y_, {acc.r * 0.35f, acc.g * 0.35f, acc.b * 0.35f, 0.0f},
                     px - r * 0.3f, py - r * 0.65f, {acc.r * 0.45f, acc.g * 0.45f, acc.b * 0.45f, alpha * 0.45f},
                     px - r * 0.3f, py + r * 0.65f, {acc.r * 0.45f, acc.g * 0.45f, acc.b * 0.45f, alpha * 0.45f});
    }

    // Glowing electric-blue outer arrowhead aura
    const float ar = r * 1.48f;
    PushTriangle(px - ar * 0.58f, py - ar * 0.72f, {acc.r * 0.35f, acc.g * 0.35f, acc.b * 0.35f, alpha * 0.55f},
                 px + ar * 0.92f, py, {acc.r * 0.75f, acc.g * 0.88f, acc.b, alpha * 0.75f},
                 px - ar * 0.22f, py, {acc.r * 0.65f, acc.g * 0.80f, acc.b, alpha * 0.65f});
    PushTriangle(px - ar * 0.58f, py + ar * 0.72f, {acc.r * 0.35f, acc.g * 0.35f, acc.b * 0.35f, alpha * 0.55f},
                 px - ar * 0.22f, py, {acc.r * 0.65f, acc.g * 0.80f, acc.b, alpha * 0.65f},
                 px + ar * 0.92f, py, {acc.r * 0.75f, acc.g * 0.88f, acc.b, alpha * 0.75f});

    // Crisp faceted white crystal PS2 arrow core
    PushTriangle(px - r * 0.45f, py - r * 0.56f, acc,
                 px + r * 0.82f, py, core,
                 px - r * 0.14f, py, core);
    PushTriangle(px - r * 0.45f, py + r * 0.56f, {acc.r * 0.75f, acc.g * 0.82f, acc.b, alpha},
                 px - r * 0.14f, py, core,
                 px + r * 0.82f, py, core);

    FlushQuadBatch();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    BeginQuadBatch();
}

void Ps2Renderer::DrawAnimatedGridBrackets(float x, float y, float w, float h, const RgbaColor& acc, float alpha) {
    if (alpha <= 0.01f) return;
    const float breath = 2.0f + 2.2f * (0.5f + 0.5f * std::sin(time_sec_ * 4.8f));
    const float gx = x - breath;
    const float gy = y - breath;
    const float gw = w + breath * 2.0f;
    const float gh = h + breath * 2.0f;
    const float arm = std::min(15.0f, std::min(gw, gh) * 0.25f);
    const float t = 2.4f;
    const RgbaColor tipCol{0.94f, 0.98f, 1.0f, alpha};
    const RgbaColor glow{acc.r, acc.g, acc.b, alpha * 0.85f};

    // 4 Corner Brackets
    PushQuad(white_tex_, gx, gy, arm, t, tipCol, glow);
    PushQuad(white_tex_, gx, gy, t, arm, tipCol, glow);

    PushQuad(white_tex_, gx + gw - arm, gy, arm, t, glow, tipCol);
    PushQuad(white_tex_, gx + gw - t, gy, t, arm, tipCol, glow);

    PushQuad(white_tex_, gx, gy + gh - t, arm, t, tipCol, glow);
    PushQuad(white_tex_, gx, gy + gh - arm, t, arm, glow, tipCol);

    PushQuad(white_tex_, gx + gw - arm, gy + gh - t, arm, t, glow, tipCol);
    PushQuad(white_tex_, gx + gw - t, gy + gh - arm, t, arm, glow, tipCol);
}

void Ps2Renderer::BuildMemoryCardMeshes() {
    auto pushTri = [](std::vector<Mesh3DVertex>& dst,
                      float ax, float ay, float az,
                      float bx, float by, float bz,
                      float cx, float cy, float cz,
                      float nx, float ny, float nz,
                      float r, float g, float b, float a, float spec) {
        dst.push_back({ax, ay, az, nx, ny, nz, r, g, b, a, spec});
        dst.push_back({bx, by, bz, nx, ny, nz, r, g, b, a, spec});
        dst.push_back({cx, cy, cz, nx, ny, nz, r, g, b, a, spec});
    };

    auto pushBox = [&](std::vector<Mesh3DVertex>& dst,
                       float x0, float y0, float z0,
                       float x1, float y1, float z1,
                       float r, float g, float b, float a, float spec) {
        // Front (+Z), Back (-Z), Left (-X), Right (+X), Top (+Y), Bottom (-Y)
        pushTri(dst, x0, y0, z1, x1, y0, z1, x1, y1, z1, 0, 0, 1, r, g, b, a, spec);
        pushTri(dst, x0, y0, z1, x1, y1, z1, x0, y1, z1, 0, 0, 1, r, g, b, a, spec);

        pushTri(dst, x1, y0, z0, x0, y0, z0, x0, y1, z0, 0, 0, -1, r, g, b, a, spec);
        pushTri(dst, x1, y0, z0, x0, y1, z0, x1, y1, z0, 0, 0, -1, r, g, b, a, spec);

        pushTri(dst, x0, y0, z0, x0, y0, z1, x0, y1, z1, -1, 0, 0, r, g, b, a, spec);
        pushTri(dst, x0, y0, z0, x0, y1, z1, x0, y1, z0, -1, 0, 0, r, g, b, a, spec);

        pushTri(dst, x1, y0, z1, x1, y0, z0, x1, y1, z0, 1, 0, 0, r, g, b, a, spec);
        pushTri(dst, x1, y0, z1, x1, y1, z0, x1, y1, z1, 1, 0, 0, r, g, b, a, spec);

        pushTri(dst, x0, y1, z1, x1, y1, z1, x1, y1, z0, 0, 1, 0, r, g, b, a, spec);
        pushTri(dst, x0, y1, z1, x1, y1, z0, x0, y1, z0, 0, 1, 0, r, g, b, a, spec);

        pushTri(dst, x0, y0, z0, x1, y0, z0, x1, y0, z1, 0, -1, 0, r, g, b, a, spec);
        pushTri(dst, x0, y0, z0, x1, y0, z1, x0, y0, z1, 0, -1, 0, r, g, b, a, spec);
    };

    auto buildCardVariant = [&](std::vector<Mesh3DVertex>& dst,
                                float br, float bg, float bb, float ba,
                                float lr, float lg, float lb, float stripR, float stripG, float stripB,
                                bool withPins) {
        dst.clear();
        // Signature chamfered PS2 SCPH-10020 silhouette (CCW loop)
        const float px[6] = {-0.54f, -0.54f, 0.54f, 0.54f, 0.34f, -0.54f};
        const float py[6] = { 0.70f, -0.70f, -0.70f, 0.50f, 0.70f,  0.70f};
        constexpr float zF = 0.085f;
        constexpr float zB = -0.085f;
        constexpr float shellSpec = 0.28f;

        // Front & Back fans + Side perimeter walls
        for (int i = 0; i < 5; ++i) {
            float x0 = px[i], y0 = py[i];
            float x1 = px[i + 1], y1 = py[i + 1];
            pushTri(dst, 0, 0, zF, x0, y0, zF, x1, y1, zF, 0, 0, 1, br, bg, bb, ba, shellSpec);
            pushTri(dst, 0, 0, zB, x1, y1, zB, x0, y0, zB, 0, 0, -1, br * 0.8f, bg * 0.8f, bb * 0.8f, ba, shellSpec);

            float dx = x1 - x0;
            float dy = y1 - y0;
            float len = std::sqrt(dx * dx + dy * dy);
            float nx = dy / std::max(0.0001f, len);
            float ny = -dx / std::max(0.0001f, len);
            pushTri(dst, x0, y0, zB, x0, y0, zF, x1, y1, zF, nx, ny, 0, br * 0.9f, bg * 0.9f, bb * 0.9f, ba, 0.38f);
            pushTri(dst, x0, y0, zB, x1, y1, zF, x1, y1, zB, nx, ny, 0, br * 0.9f, bg * 0.9f, bb * 0.9f, ba, 0.38f);
        }

        // Top-right connector / front label pocket frame + glossy plate
        pushBox(dst, -0.40f, -0.14f, zF, 0.40f, 0.46f, zF + 0.018f, lr, lg, lb, ba, 0.72f);
        // Glossy MagicGate silver-blue metallic header stripe across sticker
        pushBox(dst, -0.37f, 0.34f, zF + 0.018f, 0.37f, 0.43f, zF + 0.025f, stripR, stripG, stripB, ba, 0.90f);

        // 4 Sculpted Raised Horizontal Grip Ribs (signature lower-quarter ridges)
        for (int rIdx = 0; rIdx < 4; ++rIdx) {
            float ry0 = -0.25f - static_cast<float>(rIdx) * 0.085f;
            float ry1 = ry0 - 0.042f;
            pushBox(dst, -0.38f, ry1, zF, 0.38f, ry0, zF + 0.024f,
                    br * 1.28f, bg * 1.28f, bb * 1.28f, ba, 0.48f);
        }

        // Top center raised insertion indicator triangle (▲)
        pushTri(dst, -0.08f, 0.54f, zF + 0.018f,
                0.08f, 0.54f, zF + 0.018f,
                0.00f, 0.65f, zF + 0.018f,
                0, 0, 1, stripR, stripG, stripB, ba, 0.85f);

        // 8 Metallic Gold Edge Connector Teeth at bottom port
        if (withPins) {
            for (int p = 0; p < 8; ++p) {
                float px0 = -0.42f + static_cast<float>(p) * 0.112f;
                float px1 = px0 + 0.068f;
                pushBox(dst, px0, -0.735f, -0.055f, px1, -0.66f, 0.055f,
                        0.90f, 0.72f, 0.24f, 1.0f, 0.95f);
            }
        }
    };

    // Standard SCPH-10020 Matte Charcoal-Obsidian PS2 Memory Card
    buildCardVariant(memcard_solid_verts_,
                     0.11f, 0.14f, 0.21f, 1.0f,
                     0.06f, 0.09f, 0.16f,
                     0.42f, 0.76f, 1.00f, true);

    // Emerald-Sapphire Translucent Island-Style Folder Memory Card
    buildCardVariant(memcard_folder_verts_,
                     0.08f, 0.28f, 0.38f, 0.96f,
                     0.05f, 0.18f, 0.26f,
                     0.38f, 0.94f, 0.76f, true);

    // Ghost Empty Slot Blueprint Silhouette
    buildCardVariant(memcard_ghost_verts_,
                     0.24f, 0.42f, 0.72f, 0.85f,
                     0.16f, 0.30f, 0.56f,
                     0.50f, 0.80f, 1.00f, false);

    // Sculpted 3D Faceted PS2 Crystal Data Cube (fallback when no .icn 3D file is present)
    save_cube_verts_.clear();
    pushBox(save_cube_verts_, -0.44f, -0.44f, -0.44f, 0.44f, 0.44f, 0.44f,
            0.34f, 0.68f, 1.0f, 0.94f, 0.78f);
    pushBox(save_cube_verts_, -0.50f, -0.16f, -0.50f, 0.50f, 0.16f, 0.50f,
            0.78f, 0.92f, 1.0f, 0.96f, 0.92f);
    pushBox(save_cube_verts_, -0.16f, -0.50f, -0.50f, 0.16f, 0.50f, 0.50f,
            0.78f, 0.92f, 1.0f, 0.96f, 0.92f);
}

void Ps2Renderer::Draw3DMeshInRect(
    const std::vector<Mesh3DVertex>& verts,
    float vx, float vy, float vw, float vh,
    float rotXDeg, float rotYDeg, float rotZDeg,
    float camDist,
    float scale,
    const RgbaColor& accentCol,
    float alpha,
    bool isGhost) {
    if (verts.empty() || vw <= 4.0f || vh <= 4.0f || alpha <= 0.005f) return;
    FlushQuadBatch();

    const float sx = static_cast<float>(view_width_) / screen_width_;
    const float sy = static_cast<float>(view_height_) / kScreenHeight;
    const int pxX = view_x_ + static_cast<int>(std::round(vx * sx));
    const int pxY = view_y_ + static_cast<int>(std::round((kScreenHeight - (vy + vh)) * sy));
    const int pxW = std::max(1, static_cast<int>(std::round(vw * sx)));
    const int pxH = std::max(1, static_cast<int>(std::round(vh * sy)));

    glViewport(pxX, pxY, pxW, pxH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(pxX, pxY, pxW, pxH);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(isGhost ? GL_FALSE : GL_TRUE);

    float proj[16], viewM[16], sc[16], rx[16], ry[16], rz[16], rxy[16], rxyz[16], model[16], vp[16], mvp[16];
    MakePerspective(0.62f, vw / std::max(1.0f, vh), 0.25f, 30.0f, proj);
    MakeLookAt(0.0f, 0.15f, camDist, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, viewM);
    MakeScale(scale, scale, scale, sc);
    MakeRotateX(rotXDeg * kPi / 180.0f, rx);
    MakeRotateY(rotYDeg * kPi / 180.0f, ry);
    MakeRotateZ(rotZDeg * kPi / 180.0f, rz);
    Mat4Mul(ry, rx, rxy);
    Mat4Mul(rxy, rz, rxyz);
    Mat4Mul(rxyz, sc, model);
    Mat4Mul(proj, viewM, vp);
    Mat4Mul(vp, model, mvp);

    glUseProgram(prog_mesh3d_);
    glUniformMatrix4fv(m3d_u_mvp_, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(m3d_u_model_, 1, GL_FALSE, model);
    glUniform3f(m3d_u_cam_pos_, 0.0f, 0.15f, camDist);
    glUniform3f(m3d_u_accent_col_, accentCol.r, accentCol.g, accentCol.b);
    glUniform1f(m3d_u_alpha_, alpha);
    glUniform1f(m3d_u_ghost_, isGhost ? 1.0f : 0.0f);

    glBindVertexArray(mesh3d_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, mesh3d_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(Mesh3DVertex)),
                 verts.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glViewport(view_x_, view_y_, view_width_, view_height_);
}

void Ps2Renderer::DrawMemoryCard3DViewport(
    float vx, float vy, float vw, float vh,
    float rotXDeg, float rotYDeg,
    bool focused, bool isEmpty, bool isFolder, float alpha) {
    const RgbaColor acc = AccentColor(1.0f);
    const auto& mesh = isEmpty
        ? memcard_ghost_verts_
        : (isFolder ? memcard_folder_verts_ : memcard_solid_verts_);
    Draw3DMeshInRect(
        mesh, vx, vy, vw, vh,
        rotXDeg, rotYDeg, 0.0f,
        3.15f, focused ? 1.06f : 0.96f,
        acc, alpha * (isEmpty ? 0.65f : 1.0f), isEmpty);
}

void Ps2Renderer::DrawSaveBlockIcon3D(
    float vx, float vy, float vw, float vh,
    const McSaveBlockEntry& save,
    bool focused, float alpha) {
    const float tr = static_cast<float>((save.colorRgb >> 16) & 0xFF) / 255.0f;
    const float tg = static_cast<float>((save.colorRgb >> 8) & 0xFF) / 255.0f;
    const float tb = static_cast<float>(save.colorRgb & 0xFF) / 255.0f;
    const RgbaColor tint{
        std::max(0.25f, tr),
        std::max(0.35f, tg),
        std::max(0.45f, tb),
        1.0f
    };
    const float spinSpeed = focused ? 52.0f : 26.0f;
    const float rotY = std::fmod(time_sec_ * spinSpeed, 360.0f);
    const float rotX = 16.0f + std::sin(time_sec_ * 1.4f) * 6.0f;
    const float scale = focused ? 1.08f : 0.90f;

    if (save.iconMesh.size() >= 27) {
        const size_t vc = save.iconMesh.size() / 9;
        temp_mesh_verts_.resize(vc);
        for (size_t i = 0; i < vc; ++i) {
            const float* src = &save.iconMesh[i * 9];
            temp_mesh_verts_[i] = {
                src[0], src[1], src[2],
                src[3], src[4], src[5],
                src[6], src[7], src[8], 1.0f, 0.56f
            };
        }
        Draw3DMeshInRect(temp_mesh_verts_, vx, vy, vw, vh, rotX, rotY, 0.0f, 2.35f, scale, tint, alpha, false);
    } else {
        Draw3DMeshInRect(save_cube_verts_, vx, vy, vw, vh, rotX, rotY, 12.0f, 2.45f, scale, tint, alpha, false);
    }
}

void Ps2Renderer::DrawImageAspectFit(
    const ImageCacheEntry& img,
    float x, float y, float w, float h,
    float alpha,
    bool fillCrop) {
    if (!img.tex || img.w <= 0 || img.h <= 0 || alpha <= 0.005f) return;
    const float imgAsp = static_cast<float>(img.w) / static_cast<float>(img.h);
    const float boxAsp = w / std::max(1.0f, h);
    float drawX = x, drawY = y, drawW = w, drawH = h;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;

    if (fillCrop) {
        if (imgAsp > boxAsp) {
            float keep = boxAsp / imgAsp;
            u0 = 0.5f - 0.5f * keep;
            u1 = 0.5f + 0.5f * keep;
        } else {
            float keep = imgAsp / boxAsp;
            v0 = 0.5f - 0.5f * keep;
            v1 = 0.5f + 0.5f * keep;
        }
    } else {
        if (imgAsp > boxAsp) {
            drawH = w / imgAsp;
            drawY = y + (h - drawH) * 0.5f;
        } else {
            drawW = h * imgAsp;
            drawX = x + (w - drawW) * 0.5f;
        }
    }
    PushQuad(img.tex, drawX, drawY, drawW, drawH,
             {1.0f, 1.0f, 1.0f, alpha}, {1.0f, 1.0f, 1.0f, alpha}, u0, v0, u1, v1);
}

void Ps2Renderer::RebuildTowersLayout() {
    towers_.clear();
    // 8x8 Orthogonal PS2 Crystal Monolith City weighted by user library (just like real PS2 BIOS)
    const int libWeight = static_cast<int>(games_.size()) * 3 + static_cast<int>(saves_.size()) * 2;
    uint32_t seed = 0x50533242u ^ static_cast<uint32_t>(libWeight * 2654435761u);
    auto nextRand = [&seed]() -> float {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };

    for (int gz = 0; gz < 8; ++gz) {
        for (int gx = 0; gx < 8; ++gx) {
            TowerDef t;
            const float alleyGap = (gx < 4) ? -0.85f : 0.85f;
            const float cx = (static_cast<float>(gx) - 3.5f) * 1.42f + alleyGap;
            const float cz = (static_cast<float>(gz) - 3.5f) * 1.42f - 4.0f;
            t.x = cx;
            t.z = cz;
            t.width = 0.78f;
            t.depth = 0.78f;
            const int idx = gz * 8 + gx;
            const float libBoost = (idx < std::min(54, libWeight + 8)) ? (1.6f + nextRand() * 3.8f) : (0.6f + nextRand() * 1.4f);
            t.height = 2.0f + libBoost;
            t.phase = nextRand() * 6.283f;
            t.brightness = 0.56f + 0.44f * nextRand();
            towers_.push_back(t);
        }
    }
}

void Ps2Renderer::DrawTowers3D(
    const float* mvp,
    float camX, float camY, float camZ,
    float riseScale,
    const RgbaColor& fogCol,
    float fogDensity,
    float alpha,
    float silhouetteMode) {
    if (alpha <= 0.005f || towers_.empty()) return;
    FlushQuadBatch();

    tower_verts_.clear();
    const RgbaColor baseCol = SecondaryAccentColor(1.0f);
    int cubeCount = 0;

    for (size_t ti = 0; ti < towers_.size(); ++ti) {
        const auto& t = towers_[ti];
        const float sway = 1.0f + 0.03f * std::sin(time_sec_ * 0.85f + t.phase);
        const float h = std::max(0.35f, t.height * riseScale * sway);
        const float x0 = t.x - t.width * 0.5f;
        const float x1 = t.x + t.width * 0.5f;
        const float z0 = t.z - t.depth * 0.5f;
        const float z1 = t.z + t.depth * 0.5f;
        const float y0 = -2.0f;
        const float y1 = y0 + h;

        const float b = t.brightness;
        const float cr = baseCol.r * b;
        const float cg = baseCol.g * b;
        const float cb = baseCol.b * b;
        const float ca = 0.94f;

        auto pushFace = [&](
            float ax, float ay, float az, float ha,
            float bx, float by, float bz, float hb,
            float cx, float cy, float cz, float hc,
            float dx, float dy, float dz, float hd,
            float nx, float ny, float nz) {
            tower_verts_.push_back({ax, ay, az, nx, ny, nz, ha, cr, cg, cb, ca});
            tower_verts_.push_back({bx, by, bz, nx, ny, nz, hb, cr, cg, cb, ca});
            tower_verts_.push_back({cx, cy, cz, nx, ny, nz, hc, cr, cg, cb, ca});
            tower_verts_.push_back({ax, ay, az, nx, ny, nz, ha, cr, cg, cb, ca});
            tower_verts_.push_back({cx, cy, cz, nx, ny, nz, hc, cr, cg, cb, ca});
            tower_verts_.push_back({dx, dy, dz, nx, ny, nz, hd, cr, cg, cb, ca});
        };

        pushFace(x0, y0, z1, 0.0f, x1, y0, z1, 0.0f, x1, y1, z1, 1.0f, x0, y1, z1, 1.0f, 0.0f, 0.0f, 1.0f);
        pushFace(x1, y0, z0, 0.0f, x0, y0, z0, 0.0f, x0, y1, z0, 1.0f, x1, y1, z0, 1.0f, 0.0f, 0.0f, -1.0f);
        pushFace(x0, y0, z0, 0.0f, x0, y0, z1, 0.0f, x0, y1, z1, 1.0f, x0, y1, z0, 1.0f, -1.0f, 0.0f, 0.0f);
        pushFace(x1, y0, z1, 0.0f, x1, y0, z0, 0.0f, x1, y1, z0, 1.0f, x1, y1, z1, 1.0f, 1.0f, 0.0f, 0.0f);
        pushFace(x0, y1, z1, 1.0f, x1, y1, z1, 1.0f, x1, y1, z0, 1.0f, x0, y1, z0, 1.0f, 0.0f, 1.0f, 0.0f);

        // Emit up to 24 hovering translucent crystal cubes above grid columns (iconic PS2 BIOS blocks)
        if (cubeCount < 24 && ((ti % 5 == 0) || (ti % 3 == 0))) {
            cubeCount++;
            constexpr float cubeSize = 0.56f;
            const float halfC = cubeSize * 0.5f;
            const float cx0 = t.x - halfC;
            const float cx1 = t.x + halfC;
            const float cz0 = t.z - halfC;
            const float cz1 = t.z + halfC;
            const float cy0 = y1 + (0.65f + 0.42f * std::sin(time_sec_ * 0.75f + t.phase)) * riseScale;
            const float cy1 = cy0 + cubeSize;

            pushFace(cx0, cy0, cz1, 0.62f, cx1, cy0, cz1, 0.62f, cx1, cy1, cz1, 1.0f, cx0, cy1, cz1, 1.0f, 0.0f, 0.0f, 1.0f);
            pushFace(cx1, cy0, cz0, 0.62f, cx0, cy0, cz0, 0.62f, cx0, cy1, cz0, 1.0f, cx1, cy1, cz0, 1.0f, 0.0f, 0.0f, -1.0f);
            pushFace(cx0, cy0, cz0, 0.62f, cx0, cy0, cz1, 0.62f, cx0, cy1, cz1, 1.0f, cx0, cy1, cz0, 1.0f, -1.0f, 0.0f, 0.0f);
            pushFace(cx1, cy0, cz1, 0.62f, cx1, cy0, cz0, 0.62f, cx1, cy1, cz0, 1.0f, cx1, cy1, cz1, 1.0f, 1.0f, 0.0f, 0.0f);
            pushFace(cx0, cy1, cz1, 1.00f, cx1, cy1, cz1, 1.00f, cx1, cy1, cz0, 1.00f, cx0, cy1, cz0, 1.00f, 0.0f, 1.0f, 0.0f);
            pushFace(cx0, cy0, cz0, 0.50f, cx1, cy0, cz0, 0.50f, cx1, cy0, cz1, 0.50f, cx0, cy0, cz1, 0.50f, 0.0f, -1.0f, 0.0f);
        }
    }

    if (tower_verts_.empty()) return;

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);

    const RgbaColor acc = AccentColor(1.0f);
    glUseProgram(prog_tower_);
    glUniformMatrix4fv(twr_u_mvp_, 1, GL_FALSE, mvp);
    glUniform3f(twr_u_cam_pos_, camX, camY, camZ);
    glUniform3f(twr_u_accent_col_, acc.r, acc.g, acc.b);
    glUniform3f(twr_u_fog_col_, fogCol.r, fogCol.g, fogCol.b);
    glUniform1f(twr_u_fog_density_, fogDensity);
    glUniform1f(twr_u_alpha_, alpha);
    glUniform1f(twr_u_silhouette_, silhouetteMode);
    glUniform1f(twr_u_time_, time_sec_);

    glBindVertexArray(tower_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, tower_vbo_);
    const size_t maxBytes = 4096 * sizeof(TowerVertex);
    const size_t sendBytes = std::min(maxBytes, tower_verts_.size() * sizeof(TowerVertex));
    const GLsizei drawVerts = static_cast<GLsizei>(sendBytes / sizeof(TowerVertex));
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(sendBytes), tower_verts_.data());
    glDrawArrays(GL_TRIANGLES, 0, drawVerts);
    glBindVertexArray(0);
    glDisable(GL_DEPTH_TEST);
}

// Theme Palette Helpers (6 Curated PS2 Themes, Pure #000000 Background!)
RgbaColor Ps2Renderer::AccentColor(float alpha) const {
    switch (settings_.theme) {
        case 1: return {0.80f, 0.88f, 0.98f, alpha}; // Midnight Silver
        case 2: return {0.26f, 0.96f, 0.66f, alpha}; // Emerald Matrix
        case 3: return {1.00f, 0.42f, 0.64f, alpha}; // Crimson Sakura
        case 4: return {0.76f, 0.50f, 1.00f, alpha}; // Amethyst Nebula
        case 5: return {1.00f, 0.82f, 0.36f, alpha}; // Solar Gold
        default: return {0.34f, 0.72f, 1.00f, alpha}; // PS2 Classic (Electric Ice-Blue on #000000)
    }
}

RgbaColor Ps2Renderer::SecondaryAccentColor(float alpha) const {
    switch (settings_.theme) {
        case 1: return {0.42f, 0.52f, 0.66f, alpha};
        case 2: return {0.10f, 0.64f, 0.42f, alpha};
        case 3: return {0.72f, 0.16f, 0.38f, alpha};
        case 4: return {0.46f, 0.24f, 0.84f, alpha};
        case 5: return {0.74f, 0.52f, 0.14f, alpha};
        default: return {0.14f, 0.42f, 0.90f, alpha};
    }
}

RgbaColor Ps2Renderer::PanelTopColor(float alpha) const {
    return {0.03f, 0.04f, 0.07f, alpha};
}

RgbaColor Ps2Renderer::PanelBotColor(float alpha) const {
    return {0.01f, 0.015f, 0.03f, alpha};
}

RgbaColor Ps2Renderer::PanelBorderColor(float alpha) const {
    RgbaColor acc = AccentColor(alpha);
    return {acc.r * 0.80f, acc.g * 0.88f, acc.b, alpha};
}

RgbaColor Ps2Renderer::PrimaryTextColor(float alpha) const {
    return {0.96f, 0.98f, 1.00f, alpha};
}

RgbaColor Ps2Renderer::SecondaryTextColor(float alpha) const {
    return {0.66f, 0.78f, 0.92f, alpha};
}

Ps2Renderer::LayoutMetrics Ps2Renderer::ComputeMetrics() const {
    LayoutMetrics m;
    const float barScale = 0.55f + 0.45f * ui_scale_;
    m.padX = (screen_width_ >= 820.0f) ? 38.0f : 26.0f;
    m.topBarH = 34.0f * barScale;
    m.bottomBarH = 38.0f * barScale;
    m.bodyTop = m.topBarH + 10.0f;
    m.bodyBot = kScreenHeight - m.bottomBarH - 8.0f;
    m.bodyH = m.bodyBot - m.bodyTop;
    return m;
}

void Ps2Renderer::UpdateAndDrawOrbs3D(float dt) {
    // ------------------------------------------------------------------------------------------
    // PS2 BIOS (OSDSYS) clock orbs, measured from the real BIOS running in the emulator
    // (EE RAM traced every frame for 1057 frames; model error ~0.3 deg):
    //   * 7 orbs on a sphere. The whole system precesses around axis C (screen-down, tilted
    //     7.6 deg) at 0.030252 rad/frame.
    //   * In that rotating frame every orb spins around the same horizontal axis U at
    //     -(2.1 + 0.1*k) deg/frame.
    //   * Relative phase is locked to the real-time clock: orb k lags orb 0 by k*6 deg per
    //     second of the current minute -> all 7 meet at :00 and form two groups at :30.
    // World Y == screen Y (down), as in the BIOS.
    // ------------------------------------------------------------------------------------------
    const float motionScale = (settings_.orbsMode == 2) ? 0.0f : (settings_.orbsMode == 1 ? 0.38f : 1.0f);

    // Main-menu anchor inside a 16:10 frame, following the BIOS layout.
    const float frameW = std::min(screen_width_, kScreenHeight * 1.6f);
    const float frameX0 = (screen_width_ - frameW) * 0.5f;
    // BIOS: 0.43 of the frame; nudged left because translated labels run longer than English.
    const float menuCx = frameX0 + frameW * 0.40f;
    const float menuCy = kScreenHeight * 0.496f;

    if (!orb_pos_init_) {
        orb_center_cur_x_ = menuCx;
        orb_center_cur_y_ = menuCy;
        orb_pos_init_ = true;
    }

    float targetCx = menuCx;
    float targetCy = menuCy;
    float targetRing = 0.0f;
    float targetMist = 0.0f;
    float screenAlpha = 1.0f;
    float spread = 1.0f;
    float paletteMix = 0.0f;   // 1 = per-orb rainbow colours (return-to-menu transition)

    const float swoopU = 1.0f - Clamp01(orb_swoop_t_);
    const bool swooping = !boot_active_ && !launching_ && orb_swoop_t_ > 0.001f;

    if (boot_active_) {
        // Boot intro: the background mist is the nebula; orbs stay hidden and open up in the
        // main menu once the intro ends.
        const float s = Clamp01(boot_t_ / std::max(0.1f, boot_duration_)) * 7.0f;
        targetCx = screen_width_ * 0.47f;
        targetCy = kScreenHeight * 0.50f;
        targetMist = (bios_fog_tex_ && bios_cloud_tex_[0]) ? 0.0f
                                   : SmoothStep(0.0f, 0.4f, s) * (1.0f - SmoothStep(6.0f, 7.0f, s)) * 0.95f;
        mist_strength_cur_ = targetMist;
        orb_center_cur_x_ = targetCx;
        orb_center_cur_y_ = targetCy;
        screenAlpha = 0.0f;
    } else if (launching_) {
        targetCx = screen_width_ * 0.50f;
        targetCy = kScreenHeight * 0.50f;
        screenAlpha = 0.0f;
    } else if (screen_ == SCREEN_MAIN_MENU) {
        if (swooping) {
            // Back to the main menu (BIOS): small multicoloured orbs appear in the centre,
            // open up into the sphere while fading to white/blue, then glide under the menu.
            const float grow = EaseOutCubic(SmoothStep(0.0f, 0.75f, swoopU));
            spread = 0.22f + 0.78f * grow;
            paletteMix = 1.0f - SmoothStep(0.30f, 0.95f, swoopU);
            screenAlpha = SmoothStep(0.0f, 0.25f, swoopU);
            const float slide = SmoothStep(0.45f, 1.0f, swoopU);
            targetCx = screen_width_ * 0.5f + (menuCx - screen_width_ * 0.5f) * slide;
            targetCy = kScreenHeight * 0.5f + (menuCy - kScreenHeight * 0.5f) * slide;
        }
    } else if (screen_ == SCREEN_SETTINGS) {
        // System Configuration: orbs line up into the clock ring.
        const LayoutMetrics m = ComputeMetrics();
        const float clockAreaW = screen_width_ * 0.34f;
        targetCx = m.padX + clockAreaW * 0.50f;
        targetCy = m.bodyTop + m.bodyH * 0.48f;
        targetRing = 1.0f;
        screenAlpha = 0.95f;
    } else {
        // Browser / Games: the sphere drifts to the centre, orbs burst outward and fade away.
        targetCx = screen_width_ * 0.5f;
        targetCy = kScreenHeight * 0.5f;
        if (swooping) {
            const float out = SmoothStep(0.10f, 1.0f, swoopU);
            spread = 1.0f + 1.6f * out * out;
            screenAlpha = 1.0f - SmoothStep(0.15f, 0.95f, swoopU);
        } else {
            screenAlpha = 0.0f;
        }
    }

    const float lerpRate = std::min(1.0f, dt * 5.8f);
    orb_center_cur_x_ += (targetCx - orb_center_cur_x_) * lerpRate;
    orb_center_cur_y_ += (targetCy - orb_center_cur_y_) * lerpRate;
    orb_ring_blend_ += (targetRing - orb_ring_blend_) * lerpRate;
    mist_strength_cur_ += (targetMist - mist_strength_cur_) * std::min(1.0f, dt * 4.5f);

    // Orbit clock in seconds (60 BIOS frames per second). Static mode freezes it.
    orb_phase_acc_ += static_cast<double>(dt) * static_cast<double>(motionScale);
    if (orb_phase_acc_ > 1.0e6) orb_phase_acc_ = 0.0;
    const double T = orb_phase_acc_;

    // Seconds within the current minute (timezone offsets are whole minutes).
    struct timespec tsNow{};
    clock_gettime(CLOCK_REALTIME, &tsNow);
    const double subSec = static_cast<double>(tsNow.tv_sec % 60) + static_cast<double>(tsNow.tv_nsec) * 1e-9;
    const float clockAngle = static_cast<float>((subSec / 60.0) * (2.0 * kPi));
    const float secOfMinute = static_cast<float>(subSec);

    if (screenAlpha <= 0.005f) return;

    constexpr float kFps = 60.0f;
    constexpr float kTwoPi = 2.0f * kPi;
    constexpr float kPrecPerSec = 0.030252f * kFps;                  // rad/s around C
    constexpr float kSpinPerSec = 2.1f * (kPi / 180.0f) * kFps;       // rad/s, orb 0
    constexpr float kSpinStepPerSec = 0.1f * (kPi / 180.0f) * kFps;   // extra rad/s per orb index
    constexpr float kTrailSec = 0.75f;

    // Precession axis C and the in-frame spin plane basis (e1, e2) around U0.
    const float cLen = std::sqrt(0.1327f * 0.1327f + 0.9912f * 0.9912f);
    const float Cx = -0.1327f / cLen, Cy = -0.9912f / cLen, Cz = 0.0f;
    const float U0x = 0.0f, U0y = 0.0f, U0z = 1.0f;   // horizontal, perpendicular to C
    const float e1x = U0y * Cz - U0z * Cy, e1y = U0z * Cx - U0x * Cz, e1z = U0x * Cy - U0y * Cx;
    const float e2x = U0y * e1z - U0z * e1y, e2y = U0z * e1x - U0x * e1z, e2z = U0x * e1y - U0y * e1x;

    const float sphereR = kScreenHeight * 0.162f * spread;
    const float clockRingR = std::min(110.0f, (screen_width_ * 0.34f) * 0.42f);

    // PS2 look: white core, blue glow, blue trails. Themes tint the glow.
    const RgbaColor acc = AccentColor(1.0f);
    RgbaColor glow{0.22f, 0.55f, 1.00f, 1.0f};
    RgbaColor trail{0.08f, 0.36f, 1.00f, 1.0f};
    if (settings_.theme == 1) {
        glow = {0.70f, 0.78f, 0.92f, 1.0f};
        trail = {0.42f, 0.50f, 0.68f, 1.0f};
    } else if (settings_.theme != 0) {
        glow = {acc.r, acc.g, acc.b, 1.0f};
        trail = {acc.r * 0.75f, acc.g * 0.75f, acc.b * 0.95f, 1.0f};
    }
    static constexpr RgbaColor kRainbow[7] = {
        {0.40f, 1.00f, 0.55f, 1.0f}, {1.00f, 0.45f, 0.80f, 1.0f}, {0.70f, 0.50f, 1.00f, 1.0f},
        {0.35f, 0.90f, 1.00f, 1.0f}, {1.00f, 0.80f, 0.40f, 1.0f}, {0.45f, 0.60f, 1.00f, 1.0f},
        {0.60f, 1.00f, 0.90f, 1.0f},
    };
    const auto mixC = [](const RgbaColor& a, const RgbaColor& b, float t) -> RgbaColor {
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0f};
    };

    ribbon_verts_.clear();
    orb_sprite_verts_.clear();

    for (int i = 0; i < 7; ++i) {
        const float fi = static_cast<float>(i);
        const RgbaColor orbGlow = mixC(glow, kRainbow[i], paletteMix);
        const RgbaColor orbTrail = mixC(trail, kRainbow[i], paletteMix * 0.85f);

        // tOff: seconds into the past (trail samples). Every position is a pure function of
        // time, so the trail is exactly the path the orb travelled.
        auto evalPoint = [&](float tOff, float& px, float& py, float& pz) {
            const double t = T - static_cast<double>(tOff * motionScale);
            const float sec = secOfMinute - tOff;
            const float theta = static_cast<float>(std::fmod(-static_cast<double>(kSpinPerSec) * t, 2.0 * kPi)) - fi * kSpinStepPerSec * sec;
            const float prec = static_cast<float>(std::fmod(static_cast<double>(kPrecPerSec) * t, 2.0 * kPi));
            // Position on the great circle around U0 (in the precessing frame).
            const float ct = std::cos(theta), st = std::sin(theta);
            const float qx = ct * e1x + st * e2x, qy = ct * e1y + st * e2y, qz = ct * e1z + st * e2z;
            // Rotate by prec around C (Rodrigues).
            const float cp = std::cos(prec), sp = std::sin(prec);
            const float dotCq = Cx * qx + Cy * qy + Cz * qz;
            const float crx = Cy * qz - Cz * qy, cry = Cz * qx - Cx * qz, crz = Cx * qy - Cy * qx;
            const float wx = qx * cp + crx * sp + Cx * dotCq * (1.0f - cp);
            const float wy = qy * cp + cry * sp + Cy * dotCq * (1.0f - cp);
            const float wz = qz * cp + crz * sp + Cz * dotCq * (1.0f - cp);

            // Settings: clock ring formation.
            const float angC = (fi / 7.0f) * kTwoPi - kPi * 0.5f + clockAngle - tOff * 0.6f;
            const float rx = std::cos(angC) * clockRingR, ry = std::sin(angC) * clockRingR;

            const float fx = wx * sphereR + (rx - wx * sphereR) * orb_ring_blend_;
            const float fy = wy * sphereR + (ry - wy * sphereR) * orb_ring_blend_;
            const float fz = wz * (1.0f - orb_ring_blend_);
            const float persp = 4.0f / (4.0f - fz);   // fz is in sphere radii
            px = orb_center_cur_x_ + fx * persp;
            py = orb_center_cur_y_ + fy * persp;
            pz = persp;
        };

        float headX = 0.0f, headY = 0.0f, headPersp = 1.0f;
        evalPoint(0.0f, headX, headY, headPersp);

        if (settings_.orbsMode != 2) {
            constexpr int kTrailSteps = 28;
            const float trailSec = (orb_ring_blend_ > 0.5f) ? 0.45f : kTrailSec;
            float prevPx = headX, prevPy = headY, prevNx = 0.0f, prevNy = 1.0f, prevW = 0.0f;
            for (int s = 0; s <= kTrailSteps; ++s) {
                const float frac = static_cast<float>(s) / static_cast<float>(kTrailSteps);
                float sx = 0.0f, sy = 0.0f, sp = 1.0f;
                evalPoint(frac * trailSec, sx, sy, sp);
                float nextX = 0.0f, nextY = 0.0f, nextP = 1.0f;
                evalPoint((frac + 0.5f / kTrailSteps) * trailSec, nextX, nextY, nextP);
                float tx = nextX - sx, ty = nextY - sy;
                const float tlen = std::sqrt(tx * tx + ty * ty);
                if (tlen < 0.01f) { tx = 1.0f; ty = 0.0f; } else { tx /= tlen; ty /= tlen; }
                const float nx = -ty, ny = tx;
                const float halfW = 10.5f * sp * (1.0f - frac * 0.45f);
                if (s == 0) { prevW = halfW; prevNx = nx; prevNy = ny; continue; }

                const float fPrev = static_cast<float>(s - 1) / static_cast<float>(kTrailSteps);
                const float aPrev = screenAlpha * (1.0f - fPrev) * 0.95f;
                const float aCur = screenAlpha * (1.0f - frac) * 0.95f;
                const float p0x = prevPx + prevNx * prevW, p0y = prevPy + prevNy * prevW;
                const float p1x = prevPx - prevNx * prevW, p1y = prevPy - prevNy * prevW;
                const float q0x = sx + nx * halfW, q0y = sy + ny * halfW;
                const float q1x = sx - nx * halfW, q1y = sy - ny * halfW;
                ribbon_verts_.push_back({p0x, p0y, 0.0f, fPrev, orbTrail.r, orbTrail.g, orbTrail.b, aPrev});
                ribbon_verts_.push_back({p1x, p1y, 1.0f, fPrev, orbTrail.r, orbTrail.g, orbTrail.b, aPrev});
                ribbon_verts_.push_back({q1x, q1y, 1.0f, frac,  orbTrail.r, orbTrail.g, orbTrail.b, aCur});
                ribbon_verts_.push_back({p0x, p0y, 0.0f, fPrev, orbTrail.r, orbTrail.g, orbTrail.b, aPrev});
                ribbon_verts_.push_back({q1x, q1y, 1.0f, frac,  orbTrail.r, orbTrail.g, orbTrail.b, aCur});
                ribbon_verts_.push_back({q0x, q0y, 0.0f, frac,  orbTrail.r, orbTrail.g, orbTrail.b, aCur});
                prevPx = sx; prevPy = sy; prevNx = nx; prevNy = ny; prevW = halfW;
            }
        }

        // Orb: white core + soft round glow (no star flares, like the BIOS).
        const float spriteHalf = 22.0f * headPersp * (1.0f + 0.25f * orb_flash_t_);
        const float orbA = screenAlpha * std::clamp(0.70f + 0.30f * headPersp, 0.4f, 1.0f);
        const float x0 = headX - spriteHalf, y0 = headY - spriteHalf;
        const float x1 = headX + spriteHalf, y1 = headY + spriteHalf;
        orb_sprite_verts_.push_back({x0, y0, 0.0f, 0.0f, orbGlow.r, orbGlow.g, orbGlow.b, orbA});
        orb_sprite_verts_.push_back({x1, y0, 1.0f, 0.0f, orbGlow.r, orbGlow.g, orbGlow.b, orbA});
        orb_sprite_verts_.push_back({x1, y1, 1.0f, 1.0f, orbGlow.r, orbGlow.g, orbGlow.b, orbA});
        orb_sprite_verts_.push_back({x0, y0, 0.0f, 0.0f, orbGlow.r, orbGlow.g, orbGlow.b, orbA});
        orb_sprite_verts_.push_back({x1, y1, 1.0f, 1.0f, orbGlow.r, orbGlow.g, orbGlow.b, orbA});
        orb_sprite_verts_.push_back({x0, y1, 0.0f, 1.0f, orbGlow.r, orbGlow.g, orbGlow.b, orbA});
    }

    FlushQuadBatch();
    glBlendFunc(GL_ONE, GL_ONE);

    // One orphan of the fixed-size orb buffer per frame, then both batches as sub-ranges.
    const size_t ribbonCount = std::min(ribbon_verts_.size(), kOrbVboVerts);
    const size_t spriteCount = std::min(orb_sprite_verts_.size(), kOrbVboVerts - ribbonCount);
    if (ribbonCount + spriteCount > 0) {
        glBindVertexArray(orb_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, orb_vbo_);
        glBufferData(GL_ARRAY_BUFFER, kOrbVboVerts * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
        if (ribbonCount > 0) {
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            static_cast<GLsizeiptr>(ribbonCount * sizeof(Vertex)), ribbon_verts_.data());
        }
        if (spriteCount > 0) {
            glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(ribbonCount * sizeof(Vertex)),
                            static_cast<GLsizeiptr>(spriteCount * sizeof(Vertex)), orb_sprite_verts_.data());
        }
        if (ribbonCount > 0) {
            glUseProgram(prog_ribbon_);
            glUniform2f(rib_u_screen_, screen_width_, kScreenHeight);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(ribbonCount));
        }
        if (spriteCount > 0) {
            glUseProgram(prog_orb_);
            glUniform2f(orb_u_screen_, screen_width_, kScreenHeight);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(ribbonCount), static_cast<GLsizei>(spriteCount));
        }
    }

    glBindVertexArray(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Ps2Renderer::DrawHeaderBar(const std::string& title, float alpha) {
    const LayoutMetrics m = ComputeMetrics();
    const RgbaColor acc = AccentColor(alpha * 0.72f);
    DrawText(title, m.padX, m.bodyTop - 7.0f, 24.0f, STYLE_HEADER, PrimaryTextColor(alpha));
    FlushQuadBatch();
    BeginQuadBatch();
    PushQuad(white_tex_, m.padX, m.bodyTop + 28.0f, screen_width_ - m.padX * 2.0f, 1.2f,
             {acc.r, acc.g, acc.b, alpha * 0.48f}, {acc.r, acc.g, acc.b, 0.0f});
}

void Ps2Renderer::DrawStatusBar(float alpha) {
    if (alpha <= 0.01f) return;
    const LayoutMetrics m = ComputeMetrics();

    std::string statusLine;
    bool biosOk = true;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        const std::time_t nowSec = std::time(nullptr);
        if (nowSec != cached_status_sec_ ||
            status_.batteryLevel != cached_status_battery_ ||
            status_.batteryCharging != cached_status_charging_ ||
            settings_.time24h != cached_status_time24h_ ||
            settings_.dateFormat != cached_status_date_fmt_) {
            cached_status_sec_ = nowSec;
            cached_status_battery_ = status_.batteryLevel;
            cached_status_charging_ = status_.batteryCharging;
            cached_status_time24h_ = settings_.time24h;
            cached_status_date_fmt_ = settings_.dateFormat;

            std::tm tmNow{};
            localtime_r(&nowSec, &tmNow);
            char dateBuf[32] = {};
            if (settings_.dateFormat == 1) {
                std::snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d/%04d", tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_year + 1900);
            } else if (settings_.dateFormat == 2) {
                std::snprintf(dateBuf, sizeof(dateBuf), "%04d/%02d/%02d", tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday);
            } else {
                std::snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d/%04d", tmNow.tm_mday, tmNow.tm_mon + 1, tmNow.tm_year + 1900);
            }

            char timeBuf[24] = {};
            if (settings_.time24h) {
                std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", tmNow.tm_hour, tmNow.tm_min);
            } else {
                int h12 = tmNow.tm_hour % 12;
                if (h12 == 0) h12 = 12;
                std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d %s", h12, tmNow.tm_min, tmNow.tm_hour >= 12 ? "PM" : "AM");
            }

            char batBuf[24] = {};
            std::snprintf(batBuf, sizeof(batBuf), "%s%d%%", status_.batteryCharging ? "⚡" : "", status_.batteryLevel);

            std::string icons;
            if (status_.wifiConnected) icons += "Wi-Fi  ";
            if (status_.bluetoothConnected) icons += "BT  ";
            if (status_.audioMuted) icons += "MUTE  ";
            cached_status_line_ = icons + std::string(batBuf) + "   " + std::string(dateBuf) + "  " + std::string(timeBuf);
        }
        statusLine = cached_status_line_;
        biosOk = status_.biosValid;
    }

    // Pure #000000 borderless header scaled smoothly by physical screen size
    const float stSize = 14.5f * ui_scale_;
    const float stY = std::max(5.0f, (m.topBarH - stSize * 1.20f) * 0.5f);
    BeginQuadBatch();
    if (!biosOk) {
        DrawText("NO BIOS", m.padX, stY, stSize, STYLE_BODY_BOLD, {1.0f, 0.45f, 0.38f, alpha});
    }
    DrawText(statusLine, m.padX, stY, stSize, STYLE_MONO,
             SecondaryTextColor(alpha * 0.88f),
             screen_width_ - m.padX * 2.0f, 1, 2, screen_width_ - m.padX * 2.0f);
    FlushQuadBatch();
}

void Ps2Renderer::DrawBottomBar(float alpha) {
    if (alpha <= 0.01f) return;
    const LayoutMetrics m = ComputeMetrics();
    const float barTop = kScreenHeight - m.bottomBarH;
    const float barScale = 0.55f + 0.45f * ui_scale_;

    struct BtnHint {
        std::string glyph;
        std::string label;
        int key;
        RgbaColor col;
    };
    std::vector<BtnHint> btns;
    const std::string confirmSym = settings_.swapConfirm ? "○" : "×";
    const std::string backSym = settings_.swapConfirm ? "×" : "○";
    // PS2 controller symbol colours as the BIOS draws them.
    const RgbaColor crossCol{0.56f, 0.58f, 1.0f, alpha};
    const RgbaColor circleCol{1.0f, 0.36f, 0.40f, alpha};
    const RgbaColor confirmCol = settings_.swapConfirm ? circleCol : crossCol;
    const RgbaColor backCol = settings_.swapConfirm ? crossCol : circleCol;

    if (screen_ == SCREEN_MAIN_MENU) {
        btns.push_back({confirmSym, TextOr("ps2ui_hint_select", "Enter"), KEY_CONFIRM, confirmCol});
        btns.push_back({"△", TextOr("ps2ui_hint_version", "Version"), KEY_OPTIONS, {0.36f, 0.94f, 0.68f, alpha}});
        btns.push_back({backSym, TextOr("ps2ui_hint_exit", "Exit"), KEY_BACK, backCol});
    } else if (screen_ == SCREEN_GAMES) {
        btns.push_back({confirmSym, TextOr("ps2ui_btn_start", "Start"), KEY_CONFIRM, confirmCol});
        const int realIdx = SelectedMasterGameIndex();
        bool hasCont = false;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            hasCont = (realIdx >= 0 && realIdx < static_cast<int>(games_.size()) &&
                       games_[realIdx].latestSaveSlot >= 0);
        }
        if (hasCont) {
            btns.push_back({"□", TextOr("ps2ui_btn_continue", "Continue"), KEY_SECONDARY, {0.86f, 0.60f, 1.0f, alpha}});
        }
        btns.push_back({"△", TextOr("ps2ui_btn_options", "Options"), KEY_OPTIONS, {0.36f, 0.94f, 0.68f, alpha}});
        if (GameTabsShown()) {
            btns.push_back({"L1/R1", TextOr("ps2ui_hint_tab", "Tab"), KEY_NEXT_TAB, SecondaryTextColor(alpha)});
        }
        btns.push_back({backSym, TextOr("ps2ui_hint_back", "Back"), KEY_BACK, backCol});
    } else if (screen_ == SCREEN_BROWSER) {
        if (mc_tab_ == 2) {
            if (mc_open_slot_ > 0) {
                btns.push_back({backSym, TextOr("ps2ui_hint_back", "Back"), KEY_BACK, backCol});
            } else {
                btns.push_back({confirmSym, mc_card_pane_ == 0
                    ? TextOr("ps2ui_mc_open_card", "Open Card")
                    : TextOr("ps2ui_mc_actions", "Options"), KEY_CONFIRM, confirmCol});
                btns.push_back({"□", TextOr("ps2ui_mc_swap", "Swap 1↔2"), KEY_SECONDARY, {0.86f, 0.60f, 1.0f, alpha}});
                btns.push_back({"△", TextOr("ps2ui_mc_create_short", "+ Create Card"), KEY_OPTIONS, {0.36f, 0.94f, 0.68f, alpha}});
                btns.push_back({"L1/R1", TextOr("ps2ui_hint_tab", "Tab"), KEY_NEXT_TAB, SecondaryTextColor(alpha)});
                btns.push_back({backSym, TextOr("ps2ui_hint_back", "Back"), KEY_BACK, backCol});
            }
        } else {
            btns.push_back({confirmSym, TextOr("ps2ui_mc_load", "Load"), KEY_CONFIRM, confirmCol});
            btns.push_back({"□", TextOr("ps2ui_mc_delete", "Delete"), KEY_SECONDARY, {1.0f, 0.50f, 0.54f, alpha}});
            btns.push_back({"L1/R1", TextOr("ps2ui_hint_tab", "Tab"), KEY_NEXT_TAB, SecondaryTextColor(alpha)});
            btns.push_back({backSym, TextOr("ps2ui_hint_back", "Back"), KEY_BACK, backCol});
        }
    } else if (screen_ == SCREEN_SETTINGS) {
        btns.push_back({confirmSym, TextOr("ps2ui_hint_change", "Change"), KEY_CONFIRM, confirmCol});
        btns.push_back({backSym, TextOr("ps2ui_hint_back", "Back"), KEY_BACK, backCol});
    }

    // BIOS-style hints: symbol inside a dark round button, large light-grey label.
    float glyphSize = 17.0f * ui_scale_;
    float labelSize = 20.0f * ui_scale_;
    float itemPad = 14.0f * barScale;
    float gapW = 34.0f * barScale;

    auto measureRow = [&](std::vector<float>& itemW, std::vector<float>& glyphW) -> float {
        float tot = 0.0f;
        itemW.resize(btns.size());
        glyphW.resize(btns.size());
        for (size_t i = 0; i < btns.size(); ++i) {
            float gw = 0.0f, gh = 0.0f, lw = 0.0f, lh = 0.0f;
            const bool round = btns[i].glyph.size() <= 3;  // single PS symbol vs text tag ("L1/R1")
            if (round) {
                MeasureText(btns[i].glyph, glyphSize, STYLE_BODY_BOLD, 0.0f, 1, gw, gh);
                glyphW[i] = glyphSize * 1.55f;            // round button diameter
            } else {
                MeasureText(btns[i].glyph, labelSize * 0.8f, STYLE_BODY_BOLD, 0.0f, 1, gw, gh);
                glyphW[i] = gw;                           // text tag takes its real width
            }
            MeasureText(btns[i].label, labelSize, STYLE_BODY, 0.0f, 1, lw, lh);
            itemW[i] = glyphW[i] + 8.0f + lw + itemPad * 0.5f;
            tot += itemW[i] + gapW;
        }
        if (!btns.empty()) tot -= gapW;
        return tot;
    };

    std::vector<float> itemW;
    std::vector<float> glyphW;
    float totalW = measureRow(itemW, glyphW);
    const float availW = std::max(120.0f, screen_width_ - m.padX * 2.0f);
    if (totalW > availW) {
        const float fit = availW / totalW;
        glyphSize *= fit;
        labelSize *= fit;
        itemPad *= fit;
        gapW *= fit;
        totalW = measureRow(itemW, glyphW);
    }

    const float labelY = barTop + (m.bottomBarH - labelSize * 1.25f) * 0.5f;
    const float discD = glyphSize * 1.55f;
    const float discCy = barTop + m.bottomBarH * 0.5f;

    // Spread hints across the lower edge like the BIOS (wide gaps), but never wider than 70%.
    if (btns.size() > 1) {
        const float wantSpan = std::min(availW, screen_width_ * 0.70f);
        const float extra = (wantSpan - totalW) / static_cast<float>(btns.size() - 1);
        if (extra > 0.0f) {
            gapW += std::min(extra, screen_width_ * 0.12f);
            totalW = measureRow(itemW, glyphW);
        }
    }
    float curX = std::max(m.padX, (screen_width_ - totalW) * 0.5f);
    BeginQuadBatch();
    for (size_t i = 0; i < btns.size(); ++i) {
        const bool roundBtn = btns[i].glyph.size() <= 3;  // single PS symbol (UTF-8), not "L1/R1"
        if (roundBtn) {
            // Dark round button with the symbol centred on it.
            float dw = 0.0f, dh = 0.0f;
            MeasureText("●", discD, STYLE_BODY, 0.0f, 1, dw, dh);
            DrawText("●", curX + (discD - dw) * 0.5f, discCy - dh * 0.5f, discD, STYLE_BODY,
                     {0.20f, 0.21f, 0.24f, alpha * 0.95f});
            float gw = 0.0f, gh = 0.0f;
            MeasureText(btns[i].glyph, glyphSize, STYLE_BODY_BOLD, 0.0f, 1, gw, gh);
            DrawText(btns[i].glyph, curX + (discD - gw) * 0.5f, discCy - gh * 0.5f, glyphSize,
                     STYLE_BODY_BOLD, btns[i].col);
        } else {
            DrawText(btns[i].glyph, curX, labelY, labelSize * 0.8f, STYLE_BODY_BOLD, btns[i].col);
        }
        DrawText(btns[i].label, curX + glyphW[i] + 8.0f, labelY, labelSize, STYLE_BODY,
                 {0.70f, 0.70f, 0.70f, alpha});
        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(curX - 6.0f, barTop, itemW[i] + 12.0f, m.bottomBarH, HIT_BOTTOM_ACTION, btns[i].key);
        }
        curX += itemW[i] + gapW;
    }
    FlushQuadBatch();
}

bool Ps2Renderer::EnsureSceneCopyTexture() {
    if (view_width_ <= 0 || view_height_ <= 0) return false;
    if (scene_copy_tex_ && scene_copy_w_ == view_width_ && scene_copy_h_ == view_height_) return true;
    if (!scene_copy_tex_) glGenTextures(1, &scene_copy_tex_);
    glBindTexture(GL_TEXTURE_2D, scene_copy_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, view_width_, view_height_, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    scene_copy_w_ = view_width_;
    scene_copy_h_ = view_height_;
    return true;
}

void Ps2Renderer::UpdateBiosTextures() {
    BiosAssets assets;
    if (!TakeBiosAssetsIfNewer(bios_assets_gen_, assets)) return;
    auto upload = [](GLuint& tex, const BiosTexture& t, bool repeat) {
        if (t.w <= 0 || t.h <= 0 || t.rgba.empty()) return;
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.rgba.data());
        glGenerateMipmap(GL_TEXTURE_2D);
    };
    upload(bios_fog_tex_, assets.fog, false);
    upload(bios_refl_tex_, assets.refl, true);
    upload(bios_glow_tex_, assets.glow, false);
    upload(bios_noise_tex_, assets.noise, true);
    for (int i = 0; i < 3; ++i) upload(bios_cloud_tex_[i], assets.cloud[i], true);
    upload(bios_smoke_tex_, assets.smoke, false);
    bios_assets_gen_ = assets.generation;
}

void Ps2Renderer::DrawBootIntro(float alpha) {
    // PS2 BIOS power-on intro, timed from a capture of the real BIOS (in BIOS seconds `s`):
    //   0.0-0.4  smoky blue nebula + dark glass cubes fade in, 4 coloured light trails
    //   0.0-7.0  the camera flies through from the first frame; title fades in over the flight
    //   6.0-7.0  everything fades to black, then the orbs open up in the main menu
    // The nebula itself is the background mist pass (driven from UpdateAndDrawOrbs3D).
    constexpr float kBiosLen = 7.0f;
    const float dur = std::max(0.1f, boot_duration_);
    const float s = Clamp01(boot_t_ / dur) * kBiosLen;
    const float sceneA = SmoothStep(0.0f, 0.4f, s) * (1.0f - SmoothStep(6.0f, 7.0f, s)) * alpha;
    // Camera: flies straight ahead past the glass cube; steady glide for the first 80% of the
    // intro, then it accelerates for the last 20%.
    constexpr float kPassS = 4.2f;
    constexpr float kAccelS = kBiosLen * 0.8f;
    float camZ = 0.0f;
    if (s < kAccelS) {
        const float u = s / kAccelS;
        camZ = -11.0f * (0.6f * u + 0.4f * u * u);
    } else {
        const float u = Clamp01((s - kAccelS) / (kBiosLen - kAccelS));
        camZ = -11.0f - 1.4f * 11.0f / kAccelS * (s - kAccelS) - 16.0f * u * u;
    }
    const float camY = 0.0f;
    const float camEase = Clamp01(-camZ / 30.0f);
    const float dollyEase = camEase;
    const float aspect = screen_width_ / kScreenHeight;
    float proj[16], view[16], mvp[16];
    MakePerspective(0.95f, aspect, 0.1f, 80.0f, proj);
    MakeLookAt(0.0f, camY, camZ, 0.0f, camY, camZ - 10.0f, 0.0f, 1.0f, 0.0f, view);
    Mat4Mul(proj, view, mvp);
    // Clear glass "hero" cube the camera flies towards.
    constexpr float kHeroX = 0.4f, kHeroY = 0.7f, kHeroZ = -17.0f;  // inside the bright nebula, as in the BIOS
    if (sceneA <= 0.005f) return;

    FlushQuadBatch();
    UpdateBiosTextures();

    // BIOS intro draws the previous frame back at FIX=80/128 before the new one (frame feedback),
    // which gives the whole scene its soft trails.
    if (boot_prev_valid_ && boot_prev_tex_ && boot_prev_w_ == view_width_ && boot_prev_h_ == view_height_) {
        BeginQuadBatch();
        PushQuad(boot_prev_tex_, 0.0f, 0.0f, screen_width_, kScreenHeight, {1.0f, 1.0f, 1.0f, 0.625f},
                 {1.0f, 1.0f, 1.0f, 0.625f}, 0.0f, 1.0f, 1.0f, 0.0f);
        FlushQuadBatch();
    }

    // ---- Nebula, as in the GS dump of the BIOS intro: three meshes textured with TEXOFOG1/3/4,
    // Gouraud colours from (3,5,13) at the rim to (10,17,43) in the middle, each added at
    // FIX = 20/128 on top of the 0.625 frame feedback.
    if (bios_cloud_tex_[0] && bios_cloud_tex_[1] && bios_cloud_tex_[2]) {
        const float ncx = screen_width_ * 0.47f;
        const float ncy = kScreenHeight * 0.50f;
        const float grow = 1.0f + 0.35f * dollyEase;
        struct Layer { float size, spin, phase, tile, scrollU, scrollV, dx, dy; };
        static constexpr Layer kLayers[3] = {
            {1.45f, 0.040f, 0.3f, 1.6f, 0.020f, 0.012f, -0.04f, 0.02f},
            {1.30f, -0.055f, 1.9f, 1.9f, -0.016f, 0.018f, 0.05f, -0.03f},
            {1.15f, 0.070f, 4.1f, 2.2f, 0.012f, -0.020f, -0.02f, -0.04f}};
        // GS vertex colours, R/G trimmed to match the measured BIOS hue (0.20 : 0.33 : 1).
        const float inner[3] = {7.5f / 128.0f, 13.6f / 128.0f, 43.0f / 128.0f};
        const float outer[3] = {2.2f / 128.0f, 4.0f / 128.0f, 13.0f / 128.0f};
        const float add = (20.0f / 128.0f) * sceneA;
        constexpr int kFan = 28;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        for (int li = 0; li < 3; ++li) {
            const Layer& L = kLayers[li];
            BeginQuadBatch();
            batch_tex_ = bios_cloud_tex_[li];
            const float half = kScreenHeight * L.size * 0.5f * grow;
            const float cx = ncx + L.dx * kScreenHeight * grow;
            const float cy = ncy + L.dy * kScreenHeight * grow;
            const float ang = L.phase + L.spin * s;
            const float ca = std::cos(ang), sa = std::sin(ang);
            const float su = L.scrollU * s, sv = L.scrollV * s;
            auto uvAt = [&](float px, float py, float& u, float& v) {
                const float lx = (px - cx) / (2.0f * half), ly = (py - cy) / (2.0f * half);
                u = (lx * ca - ly * sa) * L.tile + 0.5f + su;
                v = (lx * sa + ly * ca) * L.tile + 0.5f + sv;
            };
            for (int k = 0; k < kFan; ++k) {
                const float t0 = (static_cast<float>(k) / kFan) * 2.0f * kPi;
                const float t1 = (static_cast<float>(k + 1) / kFan) * 2.0f * kPi;
                const float x0 = cx + std::cos(t0) * half, y0 = cy + std::sin(t0) * half;
                const float x1 = cx + std::cos(t1) * half, y1 = cy + std::sin(t1) * half;
                const float xm0 = cx + std::cos(t0) * half * 0.55f, ym0 = cy + std::sin(t0) * half * 0.55f;
                const float xm1 = cx + std::cos(t1) * half * 0.55f, ym1 = cy + std::sin(t1) * half * 0.55f;
                float uc, vc, u0, v0, u1, v1, um0, vm0, um1, vm1;
                uvAt(cx, cy, uc, vc); uvAt(x0, y0, u0, v0); uvAt(x1, y1, u1, v1);
                uvAt(xm0, ym0, um0, vm0); uvAt(xm1, ym1, um1, vm1);
                const float mid[3] = {(inner[0] + outer[0]) * 0.5f, (inner[1] + outer[1]) * 0.5f, (inner[2] + outer[2]) * 0.5f};
                // centre -> mid ring
                batch_verts_.push_back({cx, cy, uc, vc, inner[0], inner[1], inner[2], add});
                batch_verts_.push_back({xm0, ym0, um0, vm0, mid[0], mid[1], mid[2], add});
                batch_verts_.push_back({xm1, ym1, um1, vm1, mid[0], mid[1], mid[2], add});
                // mid ring -> rim (rim fades out)
                batch_verts_.push_back({xm0, ym0, um0, vm0, mid[0], mid[1], mid[2], add});
                batch_verts_.push_back({x0, y0, u0, v0, outer[0], outer[1], outer[2], 0.0f});
                batch_verts_.push_back({x1, y1, u1, v1, outer[0], outer[1], outer[2], 0.0f});
                batch_verts_.push_back({xm0, ym0, um0, vm0, mid[0], mid[1], mid[2], add});
                batch_verts_.push_back({x1, y1, u1, v1, outer[0], outer[1], outer[2], 0.0f});
                batch_verts_.push_back({xm1, ym1, um1, vm1, mid[0], mid[1], mid[2], add});
            }
            FlushQuadBatch();
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // ---- 4 coloured lights (OSDSYS opening formula) -------------------------------------
    //   a = (f + seed + 17l) * 0.001 * (l + 10),  b = (f + seed + 15l) * 0.0005 * (l + 10)
    //   pos = ((10 - l) cos a, (3 + l) sin b, 12 cos a + 88), trail = last 128 frames.
    {
        static constexpr RgbaColor kLightCol[4] = {
            {1.00f, 0.22f, 0.32f, 1.0f}, {0.30f, 1.00f, 0.38f, 1.0f},
            {0.38f, 0.42f, 1.00f, 1.0f}, {0.95f, 0.32f, 0.92f, 1.0f}};
        constexpr float kSeed = 1400.0f;
        const float frame = s * 60.0f;
        const float cx = screen_width_ * 0.5f;
        const float cy = kScreenHeight * 0.5f;
        const float K = kScreenHeight * 3.4f * (1.0f + 0.35f * dollyEase);
        const float zPush = 70.0f * dollyEase;
        auto project = [&](int l, float f, float& px, float& py, float& depth) {
            const float fl = static_cast<float>(l);
            const float a = (f + kSeed + 17.0f * fl) * 0.001f * (fl + 10.0f);
            const float b = (f + kSeed + 15.0f * fl) * 0.0005f * (fl + 10.0f);
            const float x = (10.0f - fl) * std::cos(a);
            const float y = (3.0f + fl) * std::sin(b);
            const float z = std::max(6.0f, 12.0f * std::cos(a) + 88.0f - zPush);
            px = cx + x / z * K;
            py = cy + y / z * K;
            depth = 88.0f / z;
        };

        ribbon_verts_.clear();
        orb_sprite_verts_.clear();
        constexpr int kSteps = 32;      // 128 frames sampled every 4
        constexpr float kStepFrames = 4.0f;
        for (int l = 0; l < 4; ++l) {
            const RgbaColor& c = kLightCol[l];
            float prevX = 0, prevY = 0, prevNx = 0, prevNy = 1, prevW = 0;
            for (int k = 0; k <= kSteps; ++k) {
                const float frac = static_cast<float>(k) / kSteps;
                float x, y, d, x2, y2, d2;
                project(l, frame - k * kStepFrames, x, y, d);
                project(l, frame - k * kStepFrames - 1.0f, x2, y2, d2);
                float tx = x2 - x, ty = y2 - y;
                const float tl = std::sqrt(tx * tx + ty * ty);
                if (tl < 0.001f) { tx = 1; ty = 0; } else { tx /= tl; ty /= tl; }
                const float nx = -ty, ny = tx;
                const float w = 1.7f * std::min(3.0f, d);  // BIOS draws these as thin LINESTRIPs
                if (k > 0) {
                    const float fp = static_cast<float>(k - 1) / kSteps;
                    const float ap = sceneA * 0.55f * (1.0f - fp * 0.8f);
                    const float ac = sceneA * 0.55f * (1.0f - frac * 0.8f);
                    ribbon_verts_.push_back({prevX + prevNx * prevW, prevY + prevNy * prevW, 0.0f, fp, c.r, c.g, c.b, ap});
                    ribbon_verts_.push_back({prevX - prevNx * prevW, prevY - prevNy * prevW, 1.0f, fp, c.r, c.g, c.b, ap});
                    ribbon_verts_.push_back({x - nx * w, y - ny * w, 1.0f, frac, c.r, c.g, c.b, ac});
                    ribbon_verts_.push_back({prevX + prevNx * prevW, prevY + prevNy * prevW, 0.0f, fp, c.r, c.g, c.b, ap});
                    ribbon_verts_.push_back({x - nx * w, y - ny * w, 1.0f, frac, c.r, c.g, c.b, ac});
                    ribbon_verts_.push_back({x + nx * w, y + ny * w, 0.0f, frac, c.r, c.g, c.b, ac});
                } else {
                    const float sh = 4.5f * std::min(3.0f, d);
                    const float sa = sceneA;
                    orb_sprite_verts_.push_back({x - sh, y - sh, 0, 0, c.r, c.g, c.b, sa});
                    orb_sprite_verts_.push_back({x + sh, y - sh, 1, 0, c.r, c.g, c.b, sa});
                    orb_sprite_verts_.push_back({x + sh, y + sh, 1, 1, c.r, c.g, c.b, sa});
                    orb_sprite_verts_.push_back({x - sh, y - sh, 0, 0, c.r, c.g, c.b, sa});
                    orb_sprite_verts_.push_back({x + sh, y + sh, 1, 1, c.r, c.g, c.b, sa});
                    orb_sprite_verts_.push_back({x - sh, y + sh, 0, 1, c.r, c.g, c.b, sa});
                }
                prevX = x; prevY = y; prevNx = nx; prevNy = ny; prevW = w;
            }
        }

        // Hero light: comes out of the depth, passes straight through the glass cube at kPassS,
        // then rushes towards the camera as the fly-through accelerates.
        {
            constexpr float kStart = 2.6f, kEnd = 5.6f;
            auto heroPos = [&](float t, float& wx, float& wy, float& wz) {
                const float u = (t - kPassS);
                wx = kHeroX - 0.9f * u + 0.25f * u * u;
                wy = kHeroY - 0.55f * u - 0.10f * u * u;
                wz = kHeroZ + 8.5f * u + 2.0f * u * std::fabs(u);
            };
            auto toScreen = [&](float wx, float wy, float wz, float& px, float& py, float& w) -> bool {
                const float cxp = mvp[0] * wx + mvp[4] * wy + mvp[8] * wz + mvp[12];
                const float cyp = mvp[1] * wx + mvp[5] * wy + mvp[9] * wz + mvp[13];
                const float cwp = mvp[3] * wx + mvp[7] * wy + mvp[11] * wz + mvp[15];
                if (cwp < 0.3f) return false;
                px = (cxp / cwp * 0.5f + 0.5f) * screen_width_;
                py = (1.0f - (cyp / cwp * 0.5f + 0.5f)) * kScreenHeight;
                w = std::min(4.0f, 8.0f / cwp);
                return true;
            };
            if (s > kStart && s < kEnd) {
                const RgbaColor hc{0.95f, 0.32f, 0.92f, 1.0f};
                const float life = SmoothStep(kStart, kStart + 0.4f, s) * (1.0f - SmoothStep(kEnd - 0.4f, kEnd, s));
                constexpr int kHSteps = 24;
                float prevX = 0, prevY = 0, prevNx = 0, prevNy = 1, prevW = 0;
                bool havePrev = false;
                for (int k = 0; k <= kHSteps; ++k) {
                    const float t = s - k * 0.025f;
                    if (t < kStart) break;
                    float wx, wy, wz, x, y, w, x2, y2, w2;
                    heroPos(t, wx, wy, wz);
                    if (!toScreen(wx, wy, wz, x, y, w)) { havePrev = false; continue; }
                    float wx2, wy2, wz2;
                    heroPos(t - 0.01f, wx2, wy2, wz2);
                    if (!toScreen(wx2, wy2, wz2, x2, y2, w2)) { x2 = x + 1.0f; y2 = y; }
                    float tx = x2 - x, ty = y2 - y;
                    const float tl = std::sqrt(tx * tx + ty * ty);
                    if (tl < 0.001f) { tx = 1; ty = 0; } else { tx /= tl; ty /= tl; }
                    const float nx = -ty, ny = tx;
                    const float hw = 1.7f * w;
                    const float frac = static_cast<float>(k) / kHSteps;
                    if (havePrev) {
                        const float fp = static_cast<float>(k - 1) / kHSteps;
                        const float ap = sceneA * life * 0.6f * (1.0f - fp);
                        const float ac = sceneA * life * 0.6f * (1.0f - frac);
                        ribbon_verts_.push_back({prevX + prevNx * prevW, prevY + prevNy * prevW, 0.0f, fp, hc.r, hc.g, hc.b, ap});
                        ribbon_verts_.push_back({prevX - prevNx * prevW, prevY - prevNy * prevW, 1.0f, fp, hc.r, hc.g, hc.b, ap});
                        ribbon_verts_.push_back({x - nx * hw, y - ny * hw, 1.0f, frac, hc.r, hc.g, hc.b, ac});
                        ribbon_verts_.push_back({prevX + prevNx * prevW, prevY + prevNy * prevW, 0.0f, fp, hc.r, hc.g, hc.b, ap});
                        ribbon_verts_.push_back({x - nx * hw, y - ny * hw, 1.0f, frac, hc.r, hc.g, hc.b, ac});
                        ribbon_verts_.push_back({x + nx * hw, y + ny * hw, 0.0f, frac, hc.r, hc.g, hc.b, ac});
                    } else if (k == 0) {
                        const float sh = 3.5f * w;
                        const float sa = sceneA * life * 0.6f;
                        orb_sprite_verts_.push_back({x - sh, y - sh, 0, 0, hc.r, hc.g, hc.b, sa});
                        orb_sprite_verts_.push_back({x + sh, y - sh, 1, 0, hc.r, hc.g, hc.b, sa});
                        orb_sprite_verts_.push_back({x + sh, y + sh, 1, 1, hc.r, hc.g, hc.b, sa});
                        orb_sprite_verts_.push_back({x - sh, y - sh, 0, 0, hc.r, hc.g, hc.b, sa});
                        orb_sprite_verts_.push_back({x + sh, y + sh, 1, 1, hc.r, hc.g, hc.b, sa});
                        orb_sprite_verts_.push_back({x - sh, y + sh, 0, 1, hc.r, hc.g, hc.b, sa});
                    }
                    prevX = x; prevY = y; prevNx = nx; prevNy = ny; prevW = hw;
                    havePrev = true;
                }
            }
        }

        const size_t ribbonCount = std::min(ribbon_verts_.size(), kOrbVboVerts);
        const size_t spriteCount = std::min(orb_sprite_verts_.size(), kOrbVboVerts - ribbonCount);
        glBlendFunc(GL_ONE, GL_ONE);
        glBindVertexArray(orb_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, orb_vbo_);
        glBufferData(GL_ARRAY_BUFFER, kOrbVboVerts * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(ribbonCount * sizeof(Vertex)), ribbon_verts_.data());
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(ribbonCount * sizeof(Vertex)),
                        static_cast<GLsizeiptr>(spriteCount * sizeof(Vertex)), orb_sprite_verts_.data());
        glUseProgram(prog_ribbon_);
        glUniform2f(rib_u_screen_, screen_width_, kScreenHeight);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(ribbonCount));
        if (!bios_glow_tex_) {
            glUseProgram(prog_orb_);
            glUniform2f(orb_u_screen_, screen_width_, kScreenHeight);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(ribbonCount), static_cast<GLsizei>(spriteCount));
        }
        glBindVertexArray(0);
        if (bios_glow_tex_ && spriteCount > 0) {
            // BIOS light sprite (TEXOCRBL), additive.
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            BeginQuadBatch();
            batch_tex_ = bios_glow_tex_;
            batch_verts_.insert(batch_verts_.end(), orb_sprite_verts_.begin(),
                                orb_sprite_verts_.begin() + static_cast<std::ptrdiff_t>(spriteCount));
            FlushQuadBatch();
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // ---- Glass cubes, drawn after the lights so lights behind them are seen through the glass ------------------------
    if (settings_.towersMode != 2) {

        struct BootCube { float x, y, z, size, ax, ay, az, speed, phase; };
        static const std::vector<BootCube> kCubes = [] {
            std::vector<BootCube> v;
            uint32_t seed = 0x0B5D2000u;
            auto rnd = [&seed]() {
                seed = seed * 1664525u + 1013904223u;
                return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
            };
            // A handful of big cubes around the edges of the frame, like the BIOS shot.
            // Six smallish cubes around the rim of the nebula (BIOS layout).
            static const float kPos[6][4] = {
                {-6.2f, 3.4f, -14.0f, 1.15f}, {6.6f, 3.0f, -15.0f, 1.2f}, {6.0f, -3.2f, -13.0f, 1.0f},
                {-6.4f, -3.6f, -13.5f, 1.15f}, {kHeroX, kHeroY, kHeroZ, 1.6f}, {-1.0f, -4.4f, -22.0f, 1.05f}};
            for (const auto& kp : kPos) {
                BootCube c{};
                c.x = kp[0]; c.y = kp[1]; c.z = kp[2]; c.size = kp[3];
                c.ax = rnd() - 0.5f; c.ay = rnd() - 0.5f; c.az = rnd() - 0.5f;
                const float l = std::max(0.001f, std::sqrt(c.ax * c.ax + c.ay * c.ay + c.az * c.az));
                c.ax /= l; c.ay /= l; c.az /= l;
                c.speed = 0.10f + rnd() * 0.12f;
                c.phase = rnd() * 6.283f;
                v.push_back(c);
            }
            return v;
        }();

        std::vector<int> order(kCubes.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return kCubes[a].z < kCubes[b].z; });

        tower_verts_.clear();
        size_t heroStart = 0, heroCount = 0;
        order.erase(std::remove(order.begin(), order.end(), 4), order.end());
        order.push_back(4);  // hero (clear glass) cube drawn last
        for (int ci : order) {
            const BootCube& c = kCubes[ci];
            if (c.z > camZ - 0.4f) continue;  // already behind the camera
            if (ci == 4) heroStart = tower_verts_.size();
            const float ang = c.phase + c.speed * s;
            const float ca = std::cos(ang), sa = std::sin(ang), oc = 1.0f - ca;
            const float x = c.ax, y = c.ay, z = c.az;
            float R[9] = {
                ca + x * x * oc,     x * y * oc - z * sa, x * z * oc + y * sa,
                y * x * oc + z * sa, ca + y * y * oc,     y * z * oc - x * sa,
                z * x * oc - y * sa, z * y * oc + x * sa, ca + z * z * oc};
            if (ci == 4) {
                // Glass cube stands on a corner like the BIOS one (body diagonal vertical: hexagonal
                // outline, vertex at the bottom, three faces visible) and turns slowly about it.
                // Riso maps the (1,1,1) diagonal onto +Y: rotate 54.7356 deg about (1,0,-1)/sqrt2.
                const float ia = 0.9553166f;  // acos(1/sqrt(3))
                const float ic = std::cos(ia), is = std::sin(ia), io = 1.0f - ic;
                const float kx = 0.70710678f, ky = 0.0f, kz = -0.70710678f;
                const float Ri[9] = {
                    ic + kx * kx * io,      kx * ky * io - kz * is, kx * kz * io + ky * is,
                    ky * kx * io + kz * is, ic + ky * ky * io,      ky * kz * io - kx * is,
                    kz * kx * io - ky * is, kz * ky * io + kx * is, ic + kz * kz * io};
                const float yaw = 0.35f + 0.16f * s;
                const float cy = std::cos(yaw), sy = std::sin(yaw);
                const float Ry[9] = {cy, 0.0f, sy, 0.0f, 1.0f, 0.0f, -sy, 0.0f, cy};
                for (int r = 0; r < 3; ++r) {
                    for (int cc = 0; cc < 3; ++cc) {
                        R[r * 3 + cc] = Ry[r * 3 + 0] * Ri[0 * 3 + cc] + Ry[r * 3 + 1] * Ri[1 * 3 + cc] +
                                        Ry[r * 3 + 2] * Ri[2 * 3 + cc];
                    }
                }
            }
            const float h = c.size * 0.5f;
            auto xf = [&](float lx, float ly, float lz, float& ox, float& oy, float& oz) {
                ox = c.x + R[0] * lx + R[1] * ly + R[2] * lz;
                oy = c.y + R[3] * lx + R[4] * ly + R[5] * lz;
                oz = c.z + R[6] * lx + R[7] * ly + R[8] * lz;
            };
            // 6 faces; each face = 4 triangles fanned from its centre (edge factor 0.55 -> 1).
            static const float kFaces[6][3][3] = {
                {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
                {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
                {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}};
            for (const auto& f : kFaces) {
                float nx, ny, nz;
                xf(f[0][0], f[0][1], f[0][2], nx, ny, nz);
                nx -= c.x; ny -= c.y; nz -= c.z;
                const float cl[3] = {f[0][0] * h, f[0][1] * h, f[0][2] * h};
                float corner[4][3];
                const float sgn[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
                for (int k = 0; k < 4; ++k) {
                    for (int d = 0; d < 3; ++d) {
                        corner[k][d] = cl[d] + f[1][d] * h * sgn[k][0] + f[2][d] * h * sgn[k][1];
                    }
                }
                float cx, cy, cz;
                xf(cl[0], cl[1], cl[2], cx, cy, cz);
                // Hero glass cube: PS2-style Gouraud shade per vertex (corner direction vs light),
                // giving each face a smooth light-to-dark gradient.
                auto shadeAt = [&](float wx, float wy, float wz) {
                    if (ci != 4) return 1.0f;
                    float dx = wx - c.x, dy = wy - c.y, dz = wz - c.z;
                    const float l = std::max(0.001f, std::sqrt(dx * dx + dy * dy + dz * dz));
                    dx /= l; dy /= l; dz /= l;
                    const float d = -0.55f * dx + 0.65f * dy + 0.52f * dz;
                    return 0.15f + 0.85f * std::clamp(0.5f + 0.6f * d, 0.0f, 1.0f);
                };
                const float shC = shadeAt(cx, cy, cz);
                // Hero cube: face-local UV (0..1 per face, as the GS passes use) in colour g/b.
                const float faceU[4] = {0.0f, 1.0f, 1.0f, 0.0f};
                const float faceV[4] = {0.0f, 0.0f, 1.0f, 1.0f};
                for (int k = 0; k < 4; ++k) {
                    const float* p0 = corner[k];
                    const float* p1 = corner[(k + 1) % 4];
                    float ax, ay, az, bx, by, bz;
                    xf(p0[0], p0[1], p0[2], ax, ay, az);
                    xf(p1[0], p1[1], p1[2], bx, by, bz);
                    const float shA = shadeAt(ax, ay, az), shB = shadeAt(bx, by, bz);
                    const int k1 = (k + 1) % 4;
                    tower_verts_.push_back({cx, cy, cz, nx, ny, nz, 0.55f, shC, 0.5f, 0.5f, 1});
                    tower_verts_.push_back({ax, ay, az, nx, ny, nz, 1.0f, shA, faceU[k], faceV[k], 1});
                    tower_verts_.push_back({bx, by, bz, nx, ny, nz, 1.0f, shB, faceU[k1], faceV[k1], 1});
                }
            }
            if (ci == 4) heroCount = tower_verts_.size() - heroStart;
        }
        if (!tower_verts_.empty()) {
            glUseProgram(prog_boot_cube_);
            glUniformMatrix4fv(bcube_u_mvp_, 1, GL_FALSE, mvp);
            glUniform3f(bcube_u_cam_pos_, 0.0f, camY, camZ);
            glUniform1f(bcube_u_alpha_, sceneA * 0.45f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, bios_refl_tex_);
            glUniform1i(bcube_u_refl_, 0);
            glUniform1f(bcube_u_has_refl_, bios_refl_tex_ ? 1.0f : 0.0f);
            glUniform1f(bcube_u_glass_, 0.0f);
            glBindVertexArray(tower_vao_);
            glBindBuffer(GL_ARRAY_BUFFER, tower_vbo_);
            const size_t maxVerts = 4096;
            const size_t n = std::min(maxVerts, tower_verts_.size());
            const size_t heroN = (heroStart + heroCount <= n) ? heroCount : 0;
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(n * sizeof(TowerVertex)), tower_verts_.data());
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(heroN ? heroStart : n));
            if (heroN) {
                // Glass: grab what is already on screen (nebula + lights) and let the cube faces
                // show it refracted, like the BIOS glass block.
                if (EnsureSceneCopyTexture()) {
                    glActiveTexture(GL_TEXTURE1);
                    glBindTexture(GL_TEXTURE_2D, scene_copy_tex_);
                    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, view_x_, view_y_, view_width_, view_height_);
                    glUniform1i(bcube_u_scene_, 1);
                    glUniform4f(bcube_u_view_, static_cast<float>(view_x_), static_cast<float>(view_y_),
                                static_cast<float>(view_width_), static_cast<float>(view_height_));
                    glActiveTexture(GL_TEXTURE2);
                    glBindTexture(GL_TEXTURE_2D, bios_noise_tex_ ? bios_noise_tex_ : white_tex_);
                    glUniform1i(bcube_u_noise_, 2);
                    glUniform1f(bcube_u_time_, s);
                    glActiveTexture(GL_TEXTURE3);
                    glBindTexture(GL_TEXTURE_2D, bios_smoke_tex_ ? bios_smoke_tex_ : white_tex_);
                    glUniform1i(bcube_u_smoke_, 3);
                    glUniform1f(bcube_u_glass_, 1.0f);
                    glUniform1f(bcube_u_alpha_, sceneA);
                    glDrawArrays(GL_TRIANGLES, static_cast<GLint>(heroStart), static_cast<GLsizei>(heroN));
                    glActiveTexture(GL_TEXTURE0);
                }
            }
            glBindVertexArray(0);
        }
    }

    // Keep this frame (without the title) for the next frame's feedback pass.
    if (view_width_ > 0 && view_height_ > 0) {
        if (!boot_prev_tex_ || boot_prev_w_ != view_width_ || boot_prev_h_ != view_height_) {
            if (!boot_prev_tex_) glGenTextures(1, &boot_prev_tex_);
            glBindTexture(GL_TEXTURE_2D, boot_prev_tex_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, view_width_, view_height_, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
            boot_prev_w_ = view_width_;
            boot_prev_h_ = view_height_;
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, boot_prev_tex_);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, view_x_, view_y_, view_width_, view_height_);
        boot_prev_valid_ = true;
    }

    // ---- Title: EmuCoreX / line / PlayStation 2 (fades in over the flight) -------------------
    // Shown briefly like the BIOS company line: in 0.6-1.2, hold, gone by ~3.2; flight continues.
    const float wordA = SmoothStep(0.6f, 1.2f, s) * (1.0f - SmoothStep(2.6f, 3.2f, s)) * alpha;
    if (wordA > 0.01f) {
        const float cx = screen_width_ * 0.5f;
        const float lineY = kScreenHeight * 0.48f + 6.0f;
        const float beamW = std::min(screen_width_ * 0.30f, 230.0f) * EaseOutCubic(SmoothStep(0.6f, 1.6f, s));
        float emW = 0.0f, emH = 0.0f, psW = 0.0f, psH = 0.0f;
        MeasureText("EmuCoreX", 28.0f, STYLE_PS2_LOGO, 0.0f, 1, emW, emH);
        MeasureText("PlayStation 2", 14.0f, STYLE_HEADER, 0.0f, 1, psW, psH);
        BeginQuadBatch();
        DrawText("EmuCoreX", cx - emW * 0.5f, lineY - emH - 3.0f, 28.0f, STYLE_PS2_LOGO, PrimaryTextColor(wordA));
        // Hairline that fades out towards both ends (drawn as short segments).
        constexpr int kSeg = 32;
        const float segW = beamW / kSeg;
        for (int i = 0; i < kSeg; ++i) {
            const float t = (static_cast<float>(i) + 0.5f) / kSeg * 2.0f - 1.0f;
            const float fade = 1.0f - t * t;
            PushQuad(white_tex_, cx - beamW * 0.5f + segW * i, lineY, segW + 0.5f, 1.0f,
                     {0.70f, 0.85f, 1.0f, wordA * 0.85f * fade});
        }
        DrawText("PlayStation 2", cx - psW * 0.5f, lineY + 6.0f, 14.0f, STYLE_HEADER, {0.72f, 0.80f, 0.92f, wordA * 0.95f});
        FlushQuadBatch();
    }
}

void Ps2Renderer::DrawMainMenu(float alpha) {
    // PS2 BIOS main menu: plain centred labels to the right of the orb sphere, no frames or
    // cursor. Selected = BIOS cyan (#34CCF8), others = grey. Measured from the real BIOS.
    const std::string items[3] = {
        TextOr("ps2ui_menu_games", "Games"),
        TextOr("ps2ui_menu_browser", "Browser"),
        TextOr("ps2ui_menu_settings", "System Configuration"),
    };

    // Labels fade in only once the orbs have settled after returning to the menu.
    const float settle = (orb_swoop_t_ > 0.001f) ? SmoothStep(0.55f, 1.0f, 1.0f - orb_swoop_t_) : 1.0f;
    const float a = alpha * settle;

    const float frameW = std::min(screen_width_, kScreenHeight * 1.6f);
    const float frameX0 = (screen_width_ - frameW) * 0.5f;
    const float centerX = frameX0 + frameW * 0.665f;
    const float boxW = std::min(frameW * 0.50f, (screen_width_ - centerX) * 2.0f - 16.0f);
    const float boxX = centerX - boxW * 0.5f;
    const float fontSize = 30.0f;
    const float rowStep = kScreenHeight * 0.074f;
    const float rowH = rowStep;
    const float firstY = kScreenHeight * 0.505f - rowStep - fontSize * 0.62f;

    const RgbaColor selCol{0.204f, 0.800f, 0.973f, a};
    const RgbaColor idleCol{0.56f, 0.56f, 0.56f, a};

    BeginQuadBatch();
    for (int i = 0; i < 3; ++i) {
        const float ry = firstY + static_cast<float>(i) * rowStep;
        const bool sel = (i == main_index_);
        DrawText(items[i], boxX, ry, fontSize, STYLE_MENU, sel ? selCol : idleCol, boxW, 1, 1, boxW);
        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(boxX, ry - 4.0f, boxW, rowH, HIT_MAIN_ROW, i);
        }
    }
    FlushQuadBatch();
}

void Ps2Renderer::DrawGamesScreen(float alpha) {
    const LayoutMetrics m = ComputeMetrics();
    DrawHeaderBar(TextOr("ps2ui_menu_games", "Games"), alpha);

    std::vector<GameEntry> gamesCopy;
    std::vector<int> recentCopy;
    bool isLoading = false;
    bool hasArcade = false;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        gamesCopy = games_;
        recentCopy = recent_indices_;
        isLoading = games_loading_;
        hasArcade = has_arcade_games_;
    }

    const RgbaColor acc = AccentColor(alpha);
    int ps2Count = 0;
    int arcadeCount = 0;
    for (const auto& g : gamesCopy) {
        if (g.isArcade) arcadeCount++;
        else ps2Count++;
    }

    float listTop = m.bodyTop + 38.0f;
    const std::vector<int> tabs = VisibleGameTabsFor(!recentCopy.empty(), hasArcade);
    if (!tabs.empty() && !gamesCopy.empty()) {
        const float tabY = m.bodyTop + 36.0f;
        const float tabH = 30.0f;
        const float tabGap = 12.0f;
        const int tabCount = static_cast<int>(tabs.size());
        const float tabW = std::min(196.0f,
            (screen_width_ - m.padX * 2.0f - tabGap * static_cast<float>(tabCount - 1)) /
                static_cast<float>(tabCount));

        for (int t = 0; t < tabCount; ++t) {
            const int kind = tabs[t];
            char lbl[128];
            if (kind == GAME_TAB_RECENT) {
                std::snprintf(lbl, sizeof(lbl), "%s (%d)",
                              TextOr("ps2ui_games_tab_recent", "Recent").c_str(),
                              static_cast<int>(recentCopy.size()));
            } else if (kind == GAME_TAB_ARCADE) {
                std::snprintf(lbl, sizeof(lbl), "%s (%d)",
                              TextOr("ps2ui_games_tab_arcade", "Namco Arcade").c_str(), arcadeCount);
            } else {
                std::snprintf(lbl, sizeof(lbl), "%s (%d)",
                              TextOr("ps2ui_games_tab_ps2", "PlayStation 2").c_str(),
                              hasArcade ? ps2Count : static_cast<int>(gamesCopy.size()));
            }
            const float tx = m.padX + static_cast<float>(t) * (tabW + tabGap);
            const bool active = (game_tab_ == kind);
            if (active) {
                DrawFocusBeam(tx, tabY, tabW, tabH, acc, alpha * 0.65f);
            }
            BeginQuadBatch();
            DrawText(lbl, tx + 4.0f, tabY + 5.0f, 14.5f,
                     active ? STYLE_BODY_BOLD : STYLE_BODY,
                     active ? PrimaryTextColor(alpha) : SecondaryTextColor(alpha * 0.68f),
                     tabW - 8.0f, 1, 1, tabW - 8.0f);
            if (active) {
                PushQuad(white_tex_, tx + 10.0f, tabY + tabH - 2.5f, tabW - 20.0f, 2.2f, acc);
            }
            FlushQuadBatch();
            if (overlay_ == OVERLAY_NONE) {
                RegisterHit(tx, tabY, tabW, tabH, HIT_GAME_TAB, kind);
            }
        }
        listTop = tabY + tabH + 8.0f;
    }

    const float listH = m.bodyBot - listTop;
    const float leftW = std::min(screen_width_ * 0.44f, 430.0f);
    const float rightX = m.padX + leftW + 28.0f;
    const float rightW = screen_width_ - rightX - m.padX;

    const std::vector<int> filtered = BuildGameFilter(gamesCopy, recentCopy, hasArcade, game_tab_);

    if (gamesCopy.empty() || filtered.empty()) {
        BeginQuadBatch();
        DrawText(isLoading ? TextOr("ps2ui_loading_games", "Scanning library...")
                           : TextOr("ps2ui_no_games", "No PS2 games found in library"),
                 m.padX, listTop + listH * 0.45f, 18.0f, STYLE_HEADER, SecondaryTextColor(alpha),
                 screen_width_ - m.padX * 2.0f, 2, 1, screen_width_ - m.padX * 2.0f);
        FlushQuadBatch();
        return;
    }

    game_index_ = std::clamp(game_index_, 0, static_cast<int>(filtered.size()) - 1);
    const int selRealIdx = filtered[game_index_];
    if (selRealIdx != last_disc_game_idx_) {
        last_disc_game_idx_ = selRealIdx;
        disc_slide_t_ = 0.0f;
    }

    const float rowH = 54.0f;
    const float rowStep = 60.0f;
    const float maxScroll = std::max(0.0f, static_cast<float>(filtered.size()) * rowStep - (listH - 12.0f));
    game_scroll_target_y_ = std::clamp(game_scroll_target_y_, 0.0f, maxScroll);
    game_scroll_y_ = std::clamp(game_scroll_y_, 0.0f, maxScroll);

    glEnable(GL_SCISSOR_TEST);
    const float sxScale = static_cast<float>(view_width_) / screen_width_;
    const float syScale = static_cast<float>(view_height_) / kScreenHeight;
    glScissor(
        view_x_ + static_cast<int>((m.padX - 24.0f) * sxScale),
        view_y_ + static_cast<int>((kScreenHeight - (listTop + listH)) * syScale),
        std::max(1, static_cast<int>((leftW + 28.0f) * sxScale)),
        std::max(1, static_cast<int>(listH * syScale)));

    for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
        const float ry = listTop + 4.0f + static_cast<float>(i) * rowStep - game_scroll_y_;
        if (ry + rowH < listTop || ry > listTop + listH) continue;

        const bool sel = (i == game_index_);
        const GameEntry& g = gamesCopy[filtered[i]];
        const bool isBlueCd = (!g.isArcade && g.fileSizeBytes > 0 && g.fileSizeBytes < 750LL * 1024LL * 1024LL);

        if (sel) {
            DrawFocusBeam(m.padX, ry - 2.0f, leftW, rowH + 2.0f, acc, alpha * 0.90f);
            SetCursorTarget(m.padX + 6.0f, ry + rowH * 0.45f);
        }

        BeginQuadBatch();
        DrawText(g.title, m.padX + 26.0f, ry + 5.0f, 17.5f, sel ? STYLE_BODY_BOLD : STYLE_BODY,
                 sel ? PrimaryTextColor(alpha) : SecondaryTextColor(alpha * 0.74f), leftW - 32.0f, 1);

        std::string sub = g.serial.empty() ? (g.isArcade ? "ARCADE" : (isBlueCd ? "CD-ROM" : "DVD-ROM")) : g.serial;
        if (!g.sizeStr.empty()) sub += " • " + g.sizeStr;
        if (g.saveCount > 0) {
            char sb[40];
            std::snprintf(sb, sizeof(sb), " • %d %s", g.saveCount, TextOr("ps2ui_saves_word", "Saves").c_str());
            sub += sb;
        }
        DrawText(sub, m.padX + 26.0f, ry + 29.0f, 13.0f, STYLE_MONO,
                 sel ? RgbaColor{0.88f, 0.94f, 1.00f, alpha} : RgbaColor{0.62f, 0.70f, 0.80f, alpha},
                 leftW - 32.0f, 1);
        FlushQuadBatch();

        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(m.padX, std::max(listTop, ry), leftW, std::min(rowH, listTop + listH - ry), HIT_GAME_ROW, i);
        }
    }
    glDisable(GL_SCISSOR_TEST);

    // Animated Cursor outside clipped area
    if (overlay_ == OVERLAY_NONE &&
        cursor_y_ >= listTop - 10.0f && cursor_y_ <= listTop + listH + 10.0f) {
        DrawAnimatedCursor(cursor_x_, cursor_y_, 12.0f, alpha);
    }

    // Right Open Showcase: FRAMELESS Floating 3D Cover Art + True 3D Tilted Sliding Optical Disc
    const GameEntry& selGame = gamesCopy[selRealIdx];
    const bool selBlueCd = (!selGame.isArcade && selGame.fileSizeBytes > 0 && selGame.fileSizeBytes < 750LL * 1024LL * 1024LL);

    const float artH = std::min(242.0f, listH * 0.58f);
    const float artW = artH * 0.72f;
    const float slideEase = EaseOutCubic(disc_slide_t_);
    const float hoverY = std::sin(time_sec_ * 1.45f) * 3.2f;

    const float groupW = artW + artH * 0.48f * slideEase;
    const float artX = rightX + (rightW - groupW) * 0.5f;
    const float artY = listTop + 6.0f + hoverY;

    // Soft radial floor reflection under floating case & disc
    DrawFocusBeam(artX - 24.0f, artY + artH - 8.0f, groupW + 48.0f, 26.0f, acc, alpha * 0.55f);

    // 3D Perspective-Tilted Spinning PS2 Disc sliding gracefully behind the cover art!
    const float discR = artH * 0.42f;
    const float discCx = artX + artW + discR * (-0.15f + 0.52f * slideEase);
    const float discCy = artY + artH * 0.48f;
    DrawOpticalDisc3D(discCx, discCy, discR, disc_spin_angle_, 0.14f, -0.26f, selBlueCd ? 1 : 0, alpha * 0.96f);

    // Frameless floating Cover Art PNG (ZERO border, ZERO box!)
    const auto& coverImg = GetOrCreateImage(selGame.coverPath, 384);
    if (coverImg.tex) {
        BeginQuadBatch();
        DrawImageAspectFit(coverImg, artX, artY, artW, artH, alpha, false);
        FlushQuadBatch();
    } else {
        DrawOpticalDisc3D(rightX + rightW * 0.5f, artY + artH * 0.5f, artH * 0.42f,
                          disc_spin_angle_, 0.14f, -0.22f, selBlueCd ? 1 : 0, alpha * 0.85f);
    }

    // Crisp Floating Title, Metadata & Minimal Glow Action Buttons
    const float infoY = listTop + 4.0f + artH + 10.0f;
    BeginQuadBatch();
    DrawText(selGame.title, rightX + 6.0f, infoY, 21.0f, STYLE_HEADER,
             PrimaryTextColor(alpha), rightW - 12.0f, 2, 1, rightW - 12.0f);

    std::string meta = selGame.isArcade ? "ARCADE" : (selBlueCd ? "CD-ROM" : "DVD-ROM");
    if (!selGame.serial.empty()) meta += " • " + selGame.serial;
    if (!selGame.sizeStr.empty()) meta += " • " + selGame.sizeStr;
    if (!selGame.compatStatus.empty()) meta += " • " + selGame.compatStatus;
    DrawText(meta, rightX + 2.0f, infoY + 48.0f, 13.0f, STYLE_MONO,
             SecondaryTextColor(alpha * 0.88f), rightW - 4.0f, 1, 1, rightW - 4.0f);
    FlushQuadBatch();

    const float btnY = listTop + listH - 44.0f;
    const bool hasCont = (selGame.latestSaveSlot >= 0);
    const float gap = 10.0f;
    const int btnCount = hasCont ? 3 : 2;
    const float btnW = (rightW - gap * static_cast<float>(btnCount - 1)) / static_cast<float>(btnCount);
    const std::string confirmSym = settings_.swapConfirm ? "○" : "×";
    float bx = rightX;

    auto drawActionBtn = [&](const std::string& lbl, int hitKind, bool primary) {
        const RgbaColor topC = primary
            ? RgbaColor{acc.r * 0.24f, acc.g * 0.34f, acc.b * 0.52f, 0.85f * alpha}
            : PanelTopColor(0.55f * alpha);
        const RgbaColor botC = primary
            ? RgbaColor{acc.r * 0.10f, acc.g * 0.16f, acc.b * 0.32f, 0.90f * alpha}
            : PanelBotColor(0.68f * alpha);
        const RgbaColor brdC = primary
            ? RgbaColor{acc.r, acc.g, acc.b, 0.88f * alpha}
            : RgbaColor{acc.r, acc.g, acc.b, 0.36f * alpha};
        DrawPanel(bx, btnY, btnW, 38.0f, 8.0f, primary ? 1.3f : 0.9f, topC, botC, brdC);

        float lw = 0.0f, lh = 0.0f;
        const float baseBtnFont = 14.5f;
        MeasureText(lbl, baseBtnFont, STYLE_BODY_BOLD, 0.0f, 1, lw, lh);
        const float availBw = std::max(24.0f, btnW - 10.0f);
        const float fitFont = (lw > availBw && lw > 1.0f) ? std::max(10.5f, baseBtnFont * (availBw / lw)) : baseBtnFont;

        BeginQuadBatch();
        DrawText(lbl, bx + 5.0f, btnY + (38.0f - fitFont * 1.25f) * 0.5f, fitFont, STYLE_BODY_BOLD,
                 PrimaryTextColor(alpha), availBw, 1, 1, availBw);
        FlushQuadBatch();
        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(bx, btnY, btnW, 38.0f, hitKind, selRealIdx);
        }
        bx += btnW + gap;
    };

    drawActionBtn(confirmSym + "  " + TextOr("ps2ui_btn_start", "Start"), HIT_GAME_START, true);
    if (hasCont) {
        drawActionBtn("□  " + TextOr("ps2ui_btn_continue", "Continue"), HIT_GAME_CONTINUE, false);
    }
    drawActionBtn("△  " + TextOr("ps2ui_btn_options", "Options"), HIT_GAME_OPTIONS, false);
}

void Ps2Renderer::DrawBrowserInsideCard(float alpha, const LayoutMetrics& m) {
    const RgbaColor acc = AccentColor(alpha);
    McSlotContents contents;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        contents = (mc_open_slot_ == 1) ? slot1_contents_ : slot2_contents_;
    }

    const float topY = m.bodyTop + 36.0f;
    const std::string slotHeader = (mc_open_slot_ == 1)
        ? TextOr("ps2ui_mc_slot1", "MEMORY CARD (PS2) / 1")
        : TextOr("ps2ui_mc_slot2", "MEMORY CARD (PS2) / 2");

    char freeBuf[64];
    std::snprintf(freeBuf, sizeof(freeBuf), "%d KB Free", contents.freeKb);
    const std::string subTitle = slotHeader + "  —  " + contents.cardName + "    (" + freeBuf + ")";

    BeginQuadBatch();
    DrawText(subTitle, m.padX, topY, 15.0f, STYLE_MONO, acc, screen_width_ - m.padX * 2.0f - 125.0f, 1);
    DrawText("← " + TextOr("ps2ui_mc_exit", "Back"), screen_width_ - m.padX - 120.0f, topY, 15.0f, STYLE_BODY_BOLD,
             SecondaryTextColor(alpha), 120.0f, 1, 2, 120.0f);
    FlushQuadBatch();
    if (overlay_ == OVERLAY_NONE) {
        RegisterHit(screen_width_ - m.padX - 130.0f, topY - 6.0f, 134.0f, 34.0f, HIT_MC_INNER_BACK, 0);
    }

    const float gridTop = topY + 30.0f;
    const float gridH = m.bodyBot - gridTop;
    const int nSaves = static_cast<int>(contents.saves.size());

    if (nSaves == 0) {
        // Spin translucent 3D data cube cleanly centered with "There is no data."
        McSaveBlockEntry emptyCube;
        emptyCube.colorRgb = 0x4298FF;
        const float cw = 150.0f;
        DrawSaveBlockIcon3D(screen_width_ * 0.5f - cw * 0.5f, gridTop + 32.0f, cw, cw, emptyCube, false, alpha * 0.72f);
        BeginQuadBatch();
        DrawText(TextOr("ps2ui_mc_no_data", "There is no data."), 0.0f, gridTop + 205.0f, 19.0f,
                 STYLE_HEADER, SecondaryTextColor(alpha * 0.85f), screen_width_, 1, 1, screen_width_);
        DrawText(freeBuf, 0.0f, gridTop + 240.0f, 13.0f, STYLE_MONO, acc, screen_width_, 1, 1, screen_width_);
        FlushQuadBatch();
        return;
    }

    mc_inner_idx_ = std::clamp(mc_inner_idx_, 0, nSaves - 1);
    const int cols = (screen_width_ >= 800.0f) ? 5 : 4;
    const int rows = 3;
    const int perPage = cols * rows;
    const int totalPages = std::max(1, (nSaves + perPage - 1) / perPage);
    mc_inner_page_ = mc_inner_idx_ / perPage;
    mc_inner_page_ = std::clamp(mc_inner_page_, 0, totalPages - 1);

    const float gridW = screen_width_ * 0.57f;
    const float inspX = m.padX + gridW + 26.0f;
    const float inspW = screen_width_ - inspX - m.padX;
    const float cellGap = 10.0f;
    const float cellW = (gridW - cellGap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const float cellH = (gridH - 36.0f - cellGap * static_cast<float>(rows - 1)) / static_cast<float>(rows);

    // Draw Animated Cursor Brackets first under/around selected 3D icon
    for (int idx = 0; idx < perPage; ++idx) {
        const int absIdx = mc_inner_page_ * perPage + idx;
        if (absIdx >= nSaves) break;
        const int r = idx / cols;
        const int c = idx % cols;
        const float cx = m.padX + static_cast<float>(c) * (cellW + cellGap);
        const float cy = gridTop + static_cast<float>(r) * (cellH + cellGap);
        const bool sel = (absIdx == mc_inner_idx_);
        if (sel) {
            DrawFocusBeam(cx - 4.0f, cy + cellH * 0.72f, cellW + 8.0f, 22.0f, acc, alpha * 0.85f);
            BeginQuadBatch();
            DrawAnimatedGridBrackets(cx, cy, cellW, cellH, acc, alpha);
            FlushQuadBatch();
            SetCursorTarget(cx + cellW * 0.5f, cy + cellH * 0.5f);
        }
        DrawSaveBlockIcon3D(cx + 4.0f, cy + 4.0f, cellW - 8.0f, cellH - 8.0f, contents.saves[absIdx], sel, alpha);
        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(cx, cy, cellW, cellH, HIT_MC_INNER_ICON, absIdx);
        }
    }

    if (totalPages > 1) {
        char pBuf[32];
        std::snprintf(pBuf, sizeof(pBuf), "%d / %d", mc_inner_page_ + 1, totalPages);
        const float footY = gridTop + gridH - 24.0f;
        BeginQuadBatch();
        DrawText("<", m.padX, footY, 15.0f, STYLE_BODY_BOLD, PrimaryTextColor(alpha));
        DrawText(pBuf, m.padX, footY + 1.0f, 12.0f, STYLE_MONO, SecondaryTextColor(alpha), gridW, 1, 1, gridW);
        DrawText(">", m.padX + gridW - 14.0f, footY, 15.0f, STYLE_BODY_BOLD, PrimaryTextColor(alpha));
        FlushQuadBatch();
        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(m.padX - 6.0f, footY - 6.0f, 38.0f, 28.0f, HIT_MC_INNER_PREV, 0);
            RegisterHit(m.padX + gridW - 32.0f, footY - 6.0f, 38.0f, 28.0f, HIT_MC_INNER_NEXT, 0);
        }
    }

    // Right Open Inspector: Big Rotating 3D Save Icon + Decoded Title + Exact KB Size & Date
    const McSaveBlockEntry& selSave = contents.saves[mc_inner_idx_];
    const float heroSize = std::min(190.0f, inspW * 0.72f);
    const float heroX = inspX + (inspW - heroSize) * 0.5f;
    const float heroY = gridTop + 8.0f;
    DrawFocusBeam(heroX - 24.0f, heroY + heroSize - 16.0f, heroSize + 48.0f, 26.0f, acc, alpha * 0.75f);
    DrawSaveBlockIcon3D(heroX, heroY, heroSize, heroSize, selSave, true, alpha);

    const float infoY = heroY + heroSize + 12.0f;
    char szBuf[32];
    std::snprintf(szBuf, sizeof(szBuf), "%d KB", selSave.sizeKb);
    BeginQuadBatch();
    DrawText(selSave.title, inspX, infoY, 20.0f, STYLE_HEADER, PrimaryTextColor(alpha), inspW, 2, 1, inspW);
    DrawText(selSave.dirName, inspX, infoY + 50.0f, 14.0f, STYLE_MONO, acc, inspW, 1, 1, inspW);
    DrawText(std::string(szBuf) + (selSave.dateStr.empty() ? "" : "  •  " + selSave.dateStr),
             inspX, infoY + 74.0f, 14.0f, STYLE_MONO, SecondaryTextColor(alpha * 0.85f), inspW, 1, 1, inspW);
    FlushQuadBatch();
}

void Ps2Renderer::DrawBrowserScreen(float alpha) {
    const LayoutMetrics m = ComputeMetrics();
    DrawHeaderBar(TextOr("ps2ui_menu_browser", "Browser"), alpha);

    if (mc_tab_ == 2 && mc_open_slot_ > 0) {
        DrawBrowserInsideCard(alpha, m);
        return;
    }

    // Borderless Clean PS2 Floating Tab Headers w/ Luminous Cyan Underline
    const float tabY = m.bodyTop + 34.0f;
    const float tabW = std::min(220.0f, (screen_width_ - m.padX * 2.0f - 24.0f) / 3.0f);
    const std::string tabLabels[3] = {
        TextOr("ps2ui_mc_tab_all", "Quick Saves"),
        TextOr("ps2ui_mc_tab_game", "Game Saves"),
        TextOr("ps2ui_mc_tab_cards", "Memory Cards"),
    };
    const RgbaColor acc = AccentColor(alpha);
    for (int i = 0; i < 3; ++i) {
        const float tx = m.padX + static_cast<float>(i) * (tabW + 12.0f);
        const bool active = (mc_tab_ == i);
        BeginQuadBatch();
        DrawText(tabLabels[i], tx, tabY + 1.0f, 16.0f, active ? STYLE_BODY_BOLD : STYLE_BODY,
                 active ? PrimaryTextColor(alpha) : SecondaryTextColor(alpha * 0.58f), tabW, 1, 1, tabW);
        if (active) {
            PushQuad(white_tex_, tx + tabW * 0.12f, tabY + 26.0f, tabW * 0.76f, 2.0f, acc);
        }
        FlushQuadBatch();
        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(tx, tabY - 2.0f, tabW, 32.0f, HIT_MC_TAB, i);
        }
    }

    const float contentTop = tabY + 34.0f;
    const float contentH = m.bodyBot - contentTop;

    // TAB 2: True 3D Sculpted Rotating MagicGate Memory Cards Root (Zero Boxes around Slots!)
    if (mc_tab_ == 2) {
        std::string s1n, s1m, s1st, s2n, s2m, s2st;
        McSlotContents c1, c2;
        std::vector<MemoryCardEntry> cards;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            s1n = slot1_card_name_; s1m = slot1_card_meta_; s1st = slot1_card_status_;
            s2n = slot2_card_name_; s2m = slot2_card_meta_; s2st = slot2_card_status_;
            c1 = slot1_contents_;
            c2 = slot2_contents_;
            cards = memcards_;
        }

        const float slotsAreaH = 166.0f;
        const float slotW = (screen_width_ - m.padX * 2.0f - 28.0f) * 0.5f;

        auto drawSlot3D = [&](int slotIdx, float x, const std::string& name, const std::string& meta,
                              const McSlotContents& cnt) {
            const bool focused = (mc_card_pane_ == 0 && mc_card_slot_idx_ == slotIdx);
            const bool empty = name.empty();
            bool isFolder = false;
            if (!empty) {
                for (const auto& c : cards) {
                    if (c.name == name) { isFolder = c.isFolder; break; }
                }
            }

            if (focused) {
                DrawFocusBeam(x + 8.0f, contentTop + slotsAreaH - 22.0f, slotW - 16.0f, 26.0f, acc, alpha * 0.90f);
            }

            // Large, Sculpted, Slowly Rotating 3D PS2 MagicGate Cartridge
            const float mcVw = 136.0f;
            const float mcVh = 158.0f;
            const float rotY = focused
                ? (std::sin(time_sec_ * 1.15f) * 26.0f + (slotIdx == 0 ? 12.0f : -12.0f))
                : (slotIdx == 0 ? 16.0f : -16.0f);
            const float rotX = focused ? (10.0f + std::sin(time_sec_ * 1.4f) * 4.0f) : 12.0f;
            DrawMemoryCard3DViewport(x + 6.0f, contentTop + 2.0f, mcVw, mcVh, rotX, rotY,
                                     focused, empty, isFolder, alpha);

            if (focused) {
                SetCursorTarget(x + mcVw + 4.0f, contentTop + 42.0f);
            }

            BeginQuadBatch();
            const float tx = x + mcVw + 16.0f;
            const float tw = slotW - mcVw - 22.0f;
            const std::string slotLbl = slotIdx == 0
                ? TextOr("ps2ui_mc_slot1", "MEMORY CARD (PS2) / 1")
                : TextOr("ps2ui_mc_slot2", "MEMORY CARD (PS2) / 2");
            DrawText(slotLbl, tx, contentTop + 14.0f, 13.5f, STYLE_BODY_BOLD, acc, tw, 1);

            if (empty) {
                DrawText(TextOr("ps2ui_mc_empty_slot", "Empty Slot"), tx, contentTop + 48.0f, 18.0f,
                         STYLE_BODY, SecondaryTextColor(alpha * 0.65f), tw, 1);
            } else {
                DrawText(name, tx, contentTop + 40.0f, 19.5f, STYLE_HEADER, PrimaryTextColor(alpha), tw, 1);
                char capBuf[96];
                if (cnt.totalKb > 0) {
                    std::snprintf(capBuf, sizeof(capBuf), "%zu %s • %d KB Free",
                                  cnt.saves.size(), TextOr("ps2ui_saves_word", "Saves").c_str(), cnt.freeKb);
                } else {
                    std::snprintf(capBuf, sizeof(capBuf), "%s", meta.c_str());
                }
                DrawText(capBuf, tx, contentTop + 74.0f, 13.5f, STYLE_MONO, SecondaryTextColor(alpha * 0.88f), tw, 1);
                if (focused) {
                    DrawText("×  " + TextOr("ps2ui_mc_open_card", "Open Card"), tx, contentTop + 106.0f, 14.0f,
                             STYLE_BODY_BOLD, {0.42f, 0.96f, 0.72f, alpha}, tw, 1);
                }
            }
            FlushQuadBatch();

            if (overlay_ == OVERLAY_NONE) {
                RegisterHit(x, contentTop, slotW, slotsAreaH, HIT_MC_OPEN_SLOT, slotIdx);
            }
        };

        drawSlot3D(0, m.padX, s1n, s1m, c1);
        drawSlot3D(1, m.padX + slotW + 28.0f, s2n, s2m, c2);

        // Borderless Clean Action Toolbar
        const float toolY = contentTop + slotsAreaH + 2.0f;
        const float toolH = 34.0f;
        struct ToolDef { std::string lbl; int action; };
        ToolDef tools[4] = {
            {TextOr("ps2ui_mc_cycle", "Cycle Slot"), 0},
            {TextOr("ps2ui_mc_swap", "Swap 1↔2"), 1},
            {TextOr("ps2ui_mc_eject", "Eject Slot"), 2},
            {TextOr("ps2ui_mc_create_short", "+ Create Card"), 3},
        };
        const float toolW = (screen_width_ - m.padX * 2.0f - 36.0f) / 4.0f;
        for (int i = 0; i < 4; ++i) {
            const float bx = m.padX + static_cast<float>(i) * (toolW + 12.0f);
            DrawPanel(bx, toolY, toolW, toolH, 6.0f, 0.8f,
                      PanelTopColor(0.38f * alpha), PanelBotColor(0.45f * alpha),
                      {acc.r, acc.g, acc.b, (i == 3 ? 0.65f : 0.25f) * alpha});
            BeginQuadBatch();
            DrawText(tools[i].lbl, bx + 4.0f, toolY + 7.5f, 13.5f, STYLE_BODY_BOLD,
                     PrimaryTextColor(alpha), toolW - 8.0f, 1, 1, toolW - 8.0f);
            FlushQuadBatch();
            if (overlay_ == OVERLAY_NONE) {
                RegisterHit(bx, toolY, toolW, toolH, HIT_CARD_TOOL, tools[i].action);
            }
        }

        // Floating Available Cards List
        const float listY = toolY + toolH + 8.0f;
        const float listH = m.bodyBot - listY;

        if (cards.empty()) {
            BeginQuadBatch();
            DrawText(TextOr("ps2ui_mc_no_cards", "No PS2 memory cards found"),
                     m.padX, listY + listH * 0.42f, 16.5f, STYLE_BODY, SecondaryTextColor(alpha),
                     screen_width_ - m.padX * 2.0f, 1, 1, screen_width_ - m.padX * 2.0f);
            FlushQuadBatch();
        } else {
            mc_card_list_idx_ = std::clamp(mc_card_list_idx_, 0, static_cast<int>(cards.size()) - 1);
            const float rowH = 40.0f;
            const float rowStep = 44.0f;
            const float maxSc = std::max(0.0f, static_cast<float>(cards.size()) * rowStep - (listH - 8.0f));
            mc_card_scroll_target_y_ = std::clamp(mc_card_scroll_target_y_, 0.0f, maxSc);
            mc_card_scroll_y_ = std::clamp(mc_card_scroll_y_, 0.0f, maxSc);

            glEnable(GL_SCISSOR_TEST);
            const float sxScale = static_cast<float>(view_width_) / screen_width_;
            const float syScale = static_cast<float>(view_height_) / kScreenHeight;
            glScissor(
                view_x_ + static_cast<int>(m.padX * sxScale),
                view_y_ + static_cast<int>((kScreenHeight - (listY + listH)) * syScale),
                std::max(1, static_cast<int>((screen_width_ - m.padX * 2.0f) * sxScale)),
                std::max(1, static_cast<int>(listH * syScale)));

            for (int i = 0; i < static_cast<int>(cards.size()); ++i) {
                const float ry = listY + 4.0f + static_cast<float>(i) * rowStep - mc_card_scroll_y_;
                if (ry + rowH < listY || ry > listY + listH) continue;
                const bool sel = (mc_card_pane_ == 1 && mc_card_list_idx_ == i);
                const auto& c = cards[i];
                const bool inS1 = (!s1n.empty() && c.name == s1n);
                const bool inS2 = (!s2n.empty() && c.name == s2n);

                if (sel) {
                    DrawFocusBeam(m.padX, ry, screen_width_ - m.padX * 2.0f, rowH, acc, alpha * 0.85f);
                    SetCursorTarget(m.padX + 6.0f, ry + rowH * 0.45f);
                }

                BeginQuadBatch();
                const std::string tag = c.isFolder ? "[DIR]" : "[PS2]";
                DrawText(tag, m.padX + 22.0f, ry + 10.0f, 13.0f, STYLE_MONO,
                         c.isFolder ? RgbaColor{0.40f, 0.94f, 0.72f, alpha} : acc);
                DrawText(c.name, m.padX + 82.0f, ry + 8.5f, 15.5f, sel ? STYLE_BODY_BOLD : STYLE_BODY,
                         PrimaryTextColor(alpha), screen_width_ * 0.42f, 1);

                std::string rightMeta = c.sizeStr;
                if (!c.dateStr.empty()) rightMeta += (rightMeta.empty() ? "" : " • ") + c.dateStr;
                if (inS1) rightMeta += "   [SLOT 1]";
                else if (inS2) rightMeta += "   [SLOT 2]";

                DrawText(rightMeta, m.padX, ry + 10.0f, 13.0f, STYLE_MONO,
                         (inS1 || inS2) ? RgbaColor{0.42f, 0.96f, 0.68f, alpha} : SecondaryTextColor(alpha * 0.78f),
                         screen_width_ - m.padX * 2.0f - 10.0f, 1, 2, screen_width_ - m.padX * 2.0f - 10.0f);
                FlushQuadBatch();

                if (overlay_ == OVERLAY_NONE) {
                    RegisterHit(m.padX, std::max(listY, ry), screen_width_ - m.padX * 2.0f,
                                std::min(rowH, listY + listH - ry), HIT_CARD_ROW, i);
                }
            }
            glDisable(GL_SCISSOR_TEST);
        }

        if (overlay_ == OVERLAY_NONE) {
            DrawAnimatedCursor(cursor_x_, cursor_y_, 12.0f, alpha);
        }
        return;
    }

    // TAB 0 / TAB 1: Quick Saves (.p2s) Floating Borderless Grid + Frameless Inspector on Pure #000000
    std::vector<SaveEntry> savesCopy;
    bool loadingSaves = false;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        savesCopy = saves_;
        loadingSaves = saves_loading_;
    }

    const std::vector<int> filtered = FilteredSaveIndices();
    const int totalFiltered = static_cast<int>(filtered.size());
    const int cols = (screen_width_ >= 780.0f) ? 4 : 3;
    const int rows = 3;
    const int perPage = cols * rows;
    const int totalPages = std::max(1, (totalFiltered + perPage - 1) / perPage);
    mc_page_ = std::clamp(mc_page_, 0, totalPages - 1);
    mc_slot_index_ = std::clamp(mc_slot_index_, 0, perPage - 1);

    const float gridW = screen_width_ * 0.525f;
    const float inspX = m.padX + gridW + 24.0f;
    const float inspW = screen_width_ - inspX - m.padX;

    if (totalFiltered == 0) {
        BeginQuadBatch();
        DrawText(loadingSaves ? TextOr("ps2ui_loading_saves", "Reading saves...")
                              : TextOr("ps2ui_mc_no_saves", "No quick saves found"),
                 m.padX, contentTop + contentH * 0.44f, 16.0f, STYLE_BODY, SecondaryTextColor(alpha),
                 gridW, 2, 1, gridW);
        FlushQuadBatch();
    } else {
        const float cellGap = 10.0f;
        const float cellW = (gridW - cellGap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        const float cellH = (contentH - 36.0f - cellGap * static_cast<float>(rows - 1)) / static_cast<float>(rows);

        for (int idx = 0; idx < perPage; ++idx) {
            const int r = idx / cols;
            const int c = idx % cols;
            const float cx = m.padX + static_cast<float>(c) * (cellW + cellGap);
            const float cy = contentTop + static_cast<float>(r) * (cellH + cellGap);
            const int fIdx = mc_page_ * perPage + idx;
            const bool occupied = (fIdx < totalFiltered);
            const bool sel = (mc_focus_pane_ == 0 && mc_slot_index_ == idx);

            if (sel) {
                DrawFocusBeam(cx - 4.0f, cy - 4.0f, cellW + 8.0f, cellH + 8.0f, acc, alpha * 0.65f);
                SetCursorTarget(cx - 6.0f, cy + cellH * 0.5f);
            }

            if (occupied) {
                const SaveEntry& sv = savesCopy[filtered[fIdx]];
                const auto& img = GetOrCreateImage(sv.previewPath, 320);
                if (img.tex) {
                    BeginQuadBatch();
                    DrawImageAspectFit(img, cx + 4.0f, cy + 4.0f, cellW - 8.0f, cellH - 24.0f,
                                       alpha * (sel ? 1.0f : 0.72f), true);
                    FlushQuadBatch();
                } else {
                    DrawOpticalDisc3D(cx + cellW * 0.5f, cy + (cellH - 18.0f) * 0.5f,
                                      std::min(cellW, cellH - 22.0f) * 0.34f, disc_spin_angle_, 0.12f, -0.18f, 0, alpha * 0.72f);
                }
                BeginQuadBatch();
                DrawText(sv.slotLabel, cx + 2.0f, cy + cellH - 18.0f, 12.5f, STYLE_BODY_BOLD,
                         sel ? PrimaryTextColor(alpha) : SecondaryTextColor(alpha * 0.74f), cellW - 4.0f, 1);
                FlushQuadBatch();
            } else {
                BeginQuadBatch();
                DrawText("•", cx, cy + cellH * 0.38f, 14.0f, STYLE_BODY,
                         SecondaryTextColor(alpha * (sel ? 0.65f : 0.24f)), cellW, 1, 1, cellW);
                FlushQuadBatch();
            }

            if (sel) {
                BeginQuadBatch();
                DrawAnimatedGridBrackets(cx, cy, cellW, cellH, acc, alpha);
                FlushQuadBatch();
            }

            if (overlay_ == OVERLAY_NONE) {
                RegisterHit(cx, cy, cellW, cellH, HIT_MC_BLOCK, idx);
            }
        }

        const float footY = contentTop + contentH - 26.0f;
        char pBuf[32];
        std::snprintf(pBuf, sizeof(pBuf), "%d / %d", mc_page_ + 1, totalPages);
        BeginQuadBatch();
        DrawText("<", m.padX, footY, 15.0f, STYLE_BODY_BOLD, PrimaryTextColor(alpha));
        DrawText(pBuf, m.padX, footY + 1.0f, 14.0f, STYLE_MONO, SecondaryTextColor(alpha), gridW, 1, 1, gridW);
        DrawText(">", m.padX + gridW - 14.0f, footY, 15.0f, STYLE_BODY_BOLD, PrimaryTextColor(alpha));
        FlushQuadBatch();
        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(m.padX - 4.0f, footY - 4.0f, 38.0f, 28.0f, HIT_MC_PAGE_PREV, 0);
            RegisterHit(m.padX + gridW - 34.0f, footY - 4.0f, 38.0f, 28.0f, HIT_MC_PAGE_NEXT, 0);
        }
    }

    // Frameless Open Save Preview + Action Buttons
    const int curPos = mc_page_ * perPage + mc_slot_index_;
    if (curPos >= 0 && curPos < totalFiltered) {
        const SaveEntry& sv = savesCopy[filtered[curPos]];
        const float prevH = std::min(182.0f, contentH * 0.46f);
        const float prevW = inspW;
        const float prevX = inspX;
        const float prevY = contentTop + 4.0f;

        const auto& img = GetOrCreateImage(sv.previewPath, 384);
        if (img.tex) {
            BeginQuadBatch();
            DrawImageAspectFit(img, prevX, prevY, prevW, prevH, alpha, false);
            FlushQuadBatch();
        } else {
            DrawOpticalDisc3D(prevX + prevW * 0.5f, prevY + prevH * 0.5f, prevH * 0.36f,
                              disc_spin_angle_, 0.12f, -0.20f, 0, alpha * 0.78f);
        }

        const float iy = prevY + prevH + 12.0f;
        BeginQuadBatch();
        DrawText(sv.title, inspX, iy, 19.5f, STYLE_HEADER, PrimaryTextColor(alpha), inspW, 2);
        DrawText(sv.slotLabel + (sv.serial.empty() ? "" : " • " + sv.serial),
                 inspX, iy + 50.0f, 14.5f, STYLE_BODY_BOLD, acc, inspW, 1);
        DrawText(sv.dateStr + (sv.sizeStr.empty() ? "" : " • " + sv.sizeStr),
                 inspX, iy + 74.0f, 13.5f, STYLE_MONO, SecondaryTextColor(alpha * 0.85f), inspW, 1);
        FlushQuadBatch();

        const float btnY = contentTop + contentH - 42.0f;
        const float btnGap = 6.0f;
        const float btnW = (inspW - btnGap * 2.0f) / 3.0f;
        const std::string bLbl[3] = {
            TextOr("ps2ui_mc_load", "Load"),
            TextOr("ps2ui_mc_delete", "Delete"),
            TextOr("ps2ui_mc_exit", "Back"),
        };
        for (int b = 0; b < 3; ++b) {
            const float bx = inspX + static_cast<float>(b) * (btnW + btnGap);
            const bool bf = (mc_focus_pane_ == 1 && mc_action_idx_ == b);
            const RgbaColor topC = bf
                ? RgbaColor{acc.r * 0.24f, acc.g * 0.34f, acc.b * 0.52f, 0.85f * alpha}
                : PanelTopColor(0.50f * alpha);
            const RgbaColor botC = bf
                ? RgbaColor{acc.r * 0.10f, acc.g * 0.16f, acc.b * 0.32f, 0.90f * alpha}
                : PanelBotColor(0.62f * alpha);
            const RgbaColor brdC = bf
                ? RgbaColor{acc.r, acc.g, acc.b, 0.88f * alpha}
                : RgbaColor{acc.r, acc.g, acc.b, 0.34f * alpha};
            DrawPanel(bx, btnY, btnW, 36.0f, 8.0f, bf ? 1.3f : 0.9f, topC, botC, brdC);
            if (bf) {
                SetCursorTarget(bx - 4.0f, btnY + 18.0f);
            }
            BeginQuadBatch();
            DrawText(bLbl[b], bx + 2.0f, btnY + 8.5f, 13.0f, STYLE_BODY_BOLD, PrimaryTextColor(alpha),
                     btnW - 4.0f, 1, 1, btnW - 4.0f);
            FlushQuadBatch();
            if (overlay_ == OVERLAY_NONE) {
                RegisterHit(bx, btnY, btnW, 36.0f, HIT_MC_ACTION, b);
            }
        }
    } else {
        BeginQuadBatch();
        DrawText(TextOr("ps2ui_mc_empty_block", "Select a save block"),
                 inspX, contentTop + contentH * 0.44f, 15.5f, STYLE_BODY,
                 SecondaryTextColor(alpha * 0.65f), inspW, 2, 1, inspW);
        FlushQuadBatch();
    }

    if (overlay_ == OVERLAY_NONE && mc_focus_pane_ == 1) {
        DrawAnimatedCursor(cursor_x_, cursor_y_, 11.0f, alpha);
    }
}

void Ps2Renderer::DrawSettingsScreen(float alpha) {
    const LayoutMetrics m = ComputeMetrics();
    DrawHeaderBar(TextOr("ps2ui_menu_settings", "System Configuration"), alpha);

    // Authentic PS2 System Configuration split: Left = Live 3D Crystal Clock Ring + Centered Digital Clock
    const float listTop = m.bodyTop + 36.0f;
    const float listH = m.bodyBot - listTop;
    const float clockAreaW = screen_width_ * 0.34f;
    const float clockCx = m.padX + clockAreaW * 0.50f;
    const float clockCy = m.bodyTop + m.bodyH * 0.48f;
    const float ringR = std::min(106.0f, clockAreaW * 0.41f);
    const RgbaColor accRing = AccentColor(alpha);

    std::time_t nowSec = std::time(nullptr);
    std::tm tmNow{};
    localtime_r(&nowSec, &tmNow);
    char bigTime[24] = {};
    if (settings_.time24h) {
        std::snprintf(bigTime, sizeof(bigTime), "%02d:%02d:%02d", tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
    } else {
        int h12 = tmNow.tm_hour % 12;
        if (h12 == 0) h12 = 12;
        std::snprintf(bigTime, sizeof(bigTime), "%02d:%02d %s", h12, tmNow.tm_min, tmNow.tm_hour >= 12 ? "PM" : "AM");
    }
    char bigDate[32] = {};
    std::snprintf(bigDate, sizeof(bigDate), "%04d/%02d/%02d", tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday);

    float timeW = 0.0f, timeH = 0.0f, dateW = 0.0f, dateH = 0.0f;
    MeasureText(bigTime, 21.0f, STYLE_MONO, 0.0f, 1, timeW, timeH);
    MeasureText(bigDate, 13.5f, STYLE_MONO, 0.0f, 1, dateW, dateH);
    const float totalClkH = timeH + 4.0f + dateH;
    const float topClkY = clockCy - totalClkH * 0.5f;

    BeginQuadBatch();
    for (int hTick = 0; hTick < 12; ++hTick) {
        const float tickAng = (static_cast<float>(hTick) / 12.0f) * 2.0f * kPi - kPi * 0.5f;
        const float tickR = ringR * 0.84f;
        const float tx = clockCx + std::cos(tickAng) * tickR;
        const float ty = clockCy + std::sin(tickAng) * tickR;
        const bool isCardinal = (hTick % 3 == 0);
        const float dotSz = isCardinal ? 4.0f : 2.5f;
        const RgbaColor dotCol{accRing.r, accRing.g, accRing.b, alpha * (isCardinal ? 0.72f : 0.36f)};
        PushQuad(white_tex_, tx - dotSz * 0.5f, ty - dotSz * 0.5f, dotSz, dotSz, dotCol);
    }
    DrawText(bigTime, clockCx - timeW * 0.5f, topClkY, 21.0f, STYLE_MONO, PrimaryTextColor(alpha));
    DrawText(bigDate, clockCx - dateW * 0.5f, topClkY + timeH + 4.0f, 13.5f, STYLE_MONO,
             SecondaryTextColor(alpha * 0.78f));
    FlushQuadBatch();

    const float listX = m.padX + clockAreaW + 12.0f;
    const float listW = screen_width_ - listX - m.padX;

    const std::string themeNames[kThemeCount] = {
        TextOr("ps2ui_theme_ocean", "PS2 Classic"),
        TextOr("ps2ui_theme_obsidian", "Midnight Silver"),
        TextOr("ps2ui_theme_emerald", "Emerald Matrix"),
        TextOr("ps2ui_theme_crimson", "Crimson Sakura"),
        TextOr("ps2ui_theme_amethyst", "Amethyst Nebula"),
        TextOr("ps2ui_theme_gold", "Solar Gold"),
    };
    const std::string aspectNames[3] = {
        TextOr("ps2ui_aspect_auto", "Full Screen"),
        TextOr("ps2ui_aspect_16_9", "16:9"),
        TextOr("ps2ui_aspect_4_3", "4:3"),
    };
    const std::string orbsNames[3] = {
        TextOr("ps2ui_orbs_normal", "Normal"),
        TextOr("ps2ui_orbs_calm", "Calm"),
        TextOr("ps2ui_orbs_static", "Static"),
    };
    const std::string towersNames[3] = {
        TextOr("ps2ui_towers_dynamic", "Dynamic"),
        TextOr("ps2ui_towers_subtle", "Subtle"),
        TextOr("ps2ui_off", "Off"),
    };
    const std::string scanNames[3] = {
        TextOr("ps2ui_off", "Off"),
        TextOr("ps2ui_scan_subtle", "Subtle"),
        TextOr("ps2ui_scan_classic", "Classic"),
    };
    const std::string bootNames[3] = {
        TextOr("ps2ui_boot_full", "Full"),
        TextOr("ps2ui_boot_fast", "Fast"),
        TextOr("ps2ui_off", "Off"),
    };
    const std::string dateNames[3] = {
        "DD/MM/YYYY", "MM/DD/YYYY", "YYYY/MM/DD"
    };
    const std::string onStr = TextOr("ps2ui_on", "On");
    const std::string offStr = TextOr("ps2ui_off", "Off");

    struct SettingRow {
        std::string label;
        std::string value;
    };
    const int th = std::clamp(settings_.theme, 0, kThemeCount - 1);
    const SettingRow rows[kSettingsCount] = {
        {TextOr("ps2ui_set_theme", "Color Theme"), themeNames[th]},
        {TextOr("ps2ui_set_aspect", "Screen Aspect"), aspectNames[std::clamp(settings_.aspectMode, 0, 2)]},
        {TextOr("ps2ui_set_orbs", "Crystal Orbs"), orbsNames[std::clamp(settings_.orbsMode, 0, 2)]},
        {TextOr("ps2ui_set_towers", "Background Towers"), towersNames[std::clamp(settings_.towersMode, 0, 2)]},
        {TextOr("ps2ui_set_scanlines", "CRT Scanlines"), scanNames[std::clamp(settings_.scanlinesMode, 0, 2)]},
        {TextOr("ps2ui_set_boot_anim", "Boot Sequence"), bootNames[std::clamp(settings_.bootAnimMode, 0, 2)]},
        {TextOr("ps2ui_set_key_tone", "Key Tone"), settings_.keyTone ? onStr : offStr},
        {TextOr("ps2ui_set_ambient", "Ambient Sound"), settings_.ambientSound ? onStr : offStr},
        {TextOr("ps2ui_set_confirm_btn", "Confirm Button"), settings_.swapConfirm ? TextOr("ps2ui_btn_circle", "○ Circle") : TextOr("ps2ui_btn_cross", "× Cross")},
        {TextOr("ps2ui_set_touch", "Touch Controls"), settings_.touchControls ? onStr : offStr},
        {TextOr("ps2ui_set_touch_guide", "Touch Guide"), TextOr("ps2ui_view", "View")},
        {TextOr("ps2ui_set_time_fmt", "Clock Format"), settings_.time24h ? TextOr("ps2ui_time_24h", "24-Hour") : TextOr("ps2ui_time_12h", "12-Hour")},
        {TextOr("ps2ui_set_date_fmt", "Date Format"), dateNames[std::clamp(settings_.dateFormat, 0, 2)]},
        {TextOr("ps2ui_set_keep_screen", "Keep Screen On"), settings_.keepScreenOn ? onStr : offStr},
        {TextOr("ps2ui_set_sysinfo", "Version Information"), TextOr("ps2ui_view", "View")},
        {TextOr("ps2ui_set_restore", "Restore Defaults"), TextOr("ps2ui_reset", "Reset")},
    };

    const float rowH = 44.0f;
    const float rowStep = 48.0f;
    const float maxSc = std::max(0.0f, static_cast<float>(kSettingsCount) * rowStep - (listH - 8.0f));
    settings_scroll_target_y_ = std::clamp(settings_scroll_target_y_, 0.0f, maxSc);
    settings_scroll_y_ = std::clamp(settings_scroll_y_, 0.0f, maxSc);

    glEnable(GL_SCISSOR_TEST);
    const float sxScale = static_cast<float>(view_width_) / screen_width_;
    const float syScale = static_cast<float>(view_height_) / kScreenHeight;
    glScissor(
        view_x_ + static_cast<int>((listX - 24.0f) * sxScale),
        view_y_ + static_cast<int>((kScreenHeight - (listTop + listH)) * syScale),
        std::max(1, static_cast<int>((listW + 28.0f) * sxScale)),
        std::max(1, static_cast<int>(listH * syScale)));

    const RgbaColor acc = AccentColor(alpha);
    for (int i = 0; i < kSettingsCount; ++i) {
        const float ry = listTop + 4.0f + static_cast<float>(i) * rowStep - settings_scroll_y_;
        if (ry + rowH < listTop || ry > listTop + listH) continue;
        const bool sel = (i == settings_index_);

        if (sel) {
            DrawFocusBeam(listX, ry, listW, rowH, acc, alpha * 0.90f);
            SetCursorTarget(listX + 4.0f, ry + rowH * 0.46f);
        }

        BeginQuadBatch();
        DrawText(rows[i].label, listX + 22.0f, ry + 10.0f, 16.0f, sel ? STYLE_BODY_BOLD : STYLE_BODY,
                 sel ? PrimaryTextColor(alpha) : SecondaryTextColor(alpha * 0.74f), listW * 0.50f, 1);

        const float valAreaW = listW * 0.42f;
        const float valAreaX = listX + listW - valAreaW - 4.0f;
        DrawText("<", valAreaX, ry + 9.5f, 15.5f, STYLE_BODY_BOLD,
                 sel ? acc : SecondaryTextColor(alpha * 0.45f));
        DrawText(rows[i].value, valAreaX + 16.0f, ry + 10.5f, 15.0f, STYLE_BODY_BOLD,
                 sel ? acc : PrimaryTextColor(alpha * 0.88f), valAreaW - 32.0f, 1, 1, valAreaW - 32.0f);
        DrawText(">", valAreaX + valAreaW - 14.0f, ry + 9.5f, 15.5f, STYLE_BODY_BOLD,
                 sel ? acc : SecondaryTextColor(alpha * 0.45f));
        FlushQuadBatch();

        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(valAreaX - 10.0f, std::max(listTop, ry), 44.0f, std::min(rowH, listTop + listH - ry), HIT_SETTING_LEFT, i);
            RegisterHit(valAreaX + valAreaW - 34.0f, std::max(listTop, ry), 44.0f, std::min(rowH, listTop + listH - ry), HIT_SETTING_RIGHT, i);
            RegisterHit(listX, std::max(listTop, ry), listW, std::min(rowH, listTop + listH - ry), HIT_SETTING_ROW, i);
        }
    }
    glDisable(GL_SCISSOR_TEST);

    if (overlay_ == OVERLAY_NONE &&
        cursor_y_ >= listTop - 10.0f && cursor_y_ <= listTop + listH + 10.0f) {
        DrawAnimatedCursor(cursor_x_, cursor_y_, 11.5f, alpha);
    }
}

void Ps2Renderer::DrawLaunchStage() {
    // Sleek, clutter-free #000000 spinning 3D optical disc launch w/ subtle parting blue light curtains
    const float p = Clamp01(launch_t_ / 1.05f);
    const float ease = EaseOutCubic(p);
    const float vis = SmoothStep(0.0f, 0.14f, p) * (1.0f - SmoothStep(0.86f, 1.0f, p));
    const RgbaColor acc = AccentColor(1.0f);

    FlushQuadBatch();
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(prog_launch_);
    glUniform2f(lch_u_screen_, screen_width_, kScreenHeight);
    glUniform1f(lch_u_progress_, p);
    glUniform1f(lch_u_time_, time_sec_);
    glUniform3f(lch_u_accent_col_, acc.r * 0.36f, acc.g * 0.36f, acc.b * 0.36f);
    glBindVertexArray(fs_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const auto& coverImg = launch_cover_.empty() ? ImageCacheEntry{} : GetOrCreateImage(launch_cover_, 384);
    float captionY = 0.0f;
    if (coverImg.tex) {
        // Cover art with the disc sliding out from behind it and spinning up.
        const float pop = EaseOutCubic(SmoothStep(0.0f, 0.45f, p));
        const float artH = std::min(kScreenHeight * 0.50f, 280.0f) * (0.92f + 0.08f * pop);
        const float artW = artH * 0.72f;
        const float discR = artH * 0.42f;
        const float slide = EaseOutCubic(SmoothStep(0.10f, 0.75f, p));
        const float groupW = artW + discR * (0.35f + 0.85f * slide);
        const float artX = (screen_width_ - groupW) * 0.5f;
        const float artY = kScreenHeight * 0.40f - artH * 0.5f + std::sin(time_sec_ * 1.45f) * 2.5f;
        const float discCx = artX + artW + discR * (-0.15f + 0.85f * slide);
        const float discCy = artY + artH * 0.48f;
        const float spin = disc_spin_angle_ + ease * ease * 14.0f;

        DrawFocusBeam(artX - 24.0f, artY + artH - 8.0f, groupW + 48.0f, 26.0f, acc, 0.55f * vis);
        DrawOpticalDisc3D(discCx, discCy, discR, spin, 0.14f * (1.0f - ease * 0.5f), -0.26f,
                          launch_is_blue_cd_ ? 1 : 0, 0.98f * vis);
        BeginQuadBatch();
        DrawImageAspectFit(coverImg, artX, artY, artW, artH, vis, false);
        FlushQuadBatch();
        captionY = artY + artH + 22.0f;
    } else {
        const float cx = screen_width_ * 0.5f;
        const float cy = kScreenHeight * 0.42f;
        const float r = 70.0f + 26.0f * ease;
        const float tiltX = 0.28f * (1.0f - ease);
        const float tiltY = -0.24f * (1.0f - ease);
        DrawFocusBeam(cx - r * 1.15f, cy + r - 12.0f, r * 2.30f, 28.0f, acc, 0.55f * vis);
        DrawOpticalDisc3D(cx, cy, r, disc_spin_angle_ + ease * 10.0f,
                          tiltX, tiltY, launch_is_blue_cd_ ? 1 : 0, 0.98f * vis);
        captionY = cy + r + 24.0f;
    }

    BeginQuadBatch();
    const std::string cap = launch_is_save_
        ? TextOr("ps2ui_launching_save", "Loading Quick Save...")
        : TextOr("ps2ui_launching_disc", "Reading PlayStation 2 Disc...");
    DrawText(cap, 0.0f, captionY, 16.0f, STYLE_HEADER,
             {0.58f, 0.84f, 1.0f, 0.95f * vis}, screen_width_, 1, 1, screen_width_);
    DrawText(launch_title_, screen_width_ * 0.10f, captionY + 28.0f, 22.0f, STYLE_BODY_BOLD,
             PrimaryTextColor(vis), screen_width_ * 0.80f, 1, 1, screen_width_ * 0.80f);
    FlushQuadBatch();
}

void Ps2Renderer::DrawConfirmDialog(
    const std::string& title,
    const std::string& message,
    const std::string& yesText,
    const std::string& noText,
    int focusedBtn) {
    const float mw = std::min(460.0f, screen_width_ - 40.0f);
    const float mh = 198.0f;
    const float mx = (screen_width_ - mw) * 0.5f;
    const float my = (kScreenHeight - mh) * 0.5f;
    const RgbaColor acc = AccentColor(1.0f);

    DrawPanel(mx, my, mw, mh, 12.0f, 1.0f,
              PanelTopColor(0.95f), PanelBotColor(0.97f),
              {acc.r, acc.g, acc.b, 0.55f});
    BeginQuadBatch();
    DrawText(title, mx + 20.0f, my + 18.0f, 20.5f, STYLE_HEADER, acc, mw - 40.0f, 1, 1, mw - 40.0f);
    DrawText(message, mx + 22.0f, my + 58.0f, 15.5f, STYLE_BODY, PrimaryTextColor(0.94f), mw - 44.0f, 3, 1, mw - 44.0f);
    FlushQuadBatch();

    const float bw = (mw - 68.0f) * 0.5f;
    const float by = my + mh - 52.0f;
    for (int b = 0; b < 2; ++b) {
        const float bx = mx + 24.0f + static_cast<float>(b) * (bw + 20.0f);
        const bool sel = (focusedBtn == b);
        const RgbaColor topC = sel
            ? RgbaColor{acc.r * 0.24f, acc.g * 0.34f, acc.b * 0.52f, 0.88f}
            : PanelTopColor(0.55f);
        const RgbaColor botC = sel
            ? RgbaColor{acc.r * 0.10f, acc.g * 0.16f, acc.b * 0.32f, 0.92f}
            : PanelBotColor(0.82f);
        const RgbaColor brdC = sel
            ? RgbaColor{acc.r, acc.g, acc.b, 0.88f}
            : RgbaColor{acc.r, acc.g, acc.b, 0.32f};
        DrawPanel(bx, by, bw, 36.0f, 8.0f, sel ? 1.3f : 0.9f, topC, botC, brdC);
        BeginQuadBatch();
        DrawText(b == 0 ? yesText : noText, bx + 6.0f, by + 7.5f, 15.0f, STYLE_BODY_BOLD,
                 PrimaryTextColor(1.0f), bw - 12.0f, 1, 1, bw - 12.0f);
        FlushQuadBatch();
        RegisterHit(bx, by, bw, 36.0f, HIT_OVERLAY_ITEM, b);
    }
}

void Ps2Renderer::DrawListModal(
    const std::string& title,
    const std::vector<std::string>& items,
    int selectedIdx,
    float modalWidth) {
    const float mw = std::min(std::max(modalWidth, 400.0f), screen_width_ - 40.0f);
    const float rowH = 42.0f;
    const float mh = 76.0f + static_cast<float>(items.size()) * (rowH + 6.0f);
    const float mx = (screen_width_ - mw) * 0.5f;
    const float my = (kScreenHeight - mh) * 0.5f;
    const RgbaColor acc = AccentColor(1.0f);

    DrawPanel(mx, my, mw, mh, 12.0f, 1.0f,
              PanelTopColor(0.95f), PanelBotColor(0.97f),
              {acc.r, acc.g, acc.b, 0.55f});
    BeginQuadBatch();
    DrawText(title, mx + 18.0f, my + 16.0f, 20.5f, STYLE_HEADER, acc, mw - 36.0f, 1, 1, mw - 36.0f);
    FlushQuadBatch();

    for (size_t i = 0; i < items.size(); ++i) {
        const float ry = my + 58.0f + static_cast<float>(i) * (rowH + 6.0f);
        const bool sel = (static_cast<int>(i) == selectedIdx);
        if (sel) {
            DrawFocusBeam(mx + 14.0f, ry, mw - 28.0f, rowH, acc, 0.92f);
        }
        BeginQuadBatch();
        DrawText(items[i], mx + 28.0f, ry + 10.0f, 15.5f, sel ? STYLE_BODY_BOLD : STYLE_BODY,
                 PrimaryTextColor(1.0f), mw - 56.0f, 1);
        FlushQuadBatch();
        RegisterHit(mx + 18.0f, ry, mw - 36.0f, rowH, HIT_OVERLAY_ITEM, static_cast<int>(i));
    }
}

void Ps2Renderer::DrawOverlay() {
    if (overlay_ == OVERLAY_NONE) return;
    FlushQuadBatch();
    BeginQuadBatch();
    PushQuad(white_tex_, 0.0f, 0.0f, screen_width_, kScreenHeight, {0.0f, 0.0f, 0.0f, 0.72f});
    FlushQuadBatch();
    RegisterHit(0.0f, 0.0f, screen_width_, kScreenHeight, HIT_OVERLAY_BACKDROP, 0);

    const RgbaColor acc = AccentColor(1.0f);

    auto colonSpaced = [](std::string s) {
        if (!s.empty() && s.back() != ' ') s.push_back(' ');
        return s;
    };

    if (overlay_ == OVERLAY_EXIT_CONFIRM) {
        DrawConfirmDialog(
            TextOr("ps2ui_exit_title", "Exit"),
            TextOr("ps2ui_exit_msg", "Return to game library?"),
            TextOr("ps2ui_yes", "Yes"), TextOr("ps2ui_no", "No"), dialog_btn_);
    } else if (overlay_ == OVERLAY_DELETE_SAVE_CONFIRM) {
        DrawConfirmDialog(
            TextOr("ps2ui_del_title", "Delete Quick Save"),
            TextOr("ps2ui_del_msg", "Delete this save state permanently?"),
            TextOr("ps2ui_yes", "Yes"), TextOr("ps2ui_no", "No"), dialog_btn_);
    } else if (overlay_ == OVERLAY_DELETE_CARD_CONFIRM) {
        std::string cardName;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (pending_card_idx_ >= 0 && pending_card_idx_ < static_cast<int>(memcards_.size())) {
                cardName = memcards_[pending_card_idx_].name;
            }
        }
        DrawConfirmDialog(
            TextOr("ps2ui_del_card_title", "Delete Memory Card"),
            TextOr("ps2ui_del_card_msg", "Delete this memory card permanently?") +
                (cardName.empty() ? "" : "\n" + cardName),
            TextOr("ps2ui_yes", "Yes"), TextOr("ps2ui_no", "No"), dialog_btn_);
    } else if (overlay_ == OVERLAY_RESTORE_CONFIRM) {
        DrawConfirmDialog(
            TextOr("ps2ui_restore_title", "Restore Defaults"),
            TextOr("ps2ui_restore_msg", "Restore default settings?"),
            TextOr("ps2ui_yes", "Yes"), TextOr("ps2ui_no", "No"), dialog_btn_);
    } else if (overlay_ == OVERLAY_GAME_OPTIONS) {
        const int realIdx = SelectedMasterGameIndex();
        GameEntry g;
        bool hasGame = false;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (realIdx >= 0 && realIdx < static_cast<int>(games_.size())) {
                g = games_[realIdx];
                hasGame = true;
            }
        }
        if (hasGame) {
            std::vector<std::string> opts;
            opts.push_back(TextOr("ps2ui_opt_start", "Start Disc"));
            if (g.latestSaveSlot >= 0) opts.push_back(TextOr("ps2ui_opt_continue", "Continue Quick Save"));
            opts.push_back(TextOr("ps2ui_opt_saves", "Game Saves"));
            opts.push_back(TextOr("ps2ui_opt_info", "Disc Information"));
            opts.push_back(TextOr("ps2ui_opt_settings", "Per-Game Settings"));
            DrawListModal(g.title, opts, options_index_, 370.0f);
        }
    } else if (overlay_ == OVERLAY_CREATE_CARD) {
        const std::vector<std::string> opts = {
            TextOr("ps2ui_mc_size_8", "8 MB PS2 Card (.ps2)"),
            TextOr("ps2ui_mc_size_16", "16 MB PS2 Card (.ps2)"),
            TextOr("ps2ui_mc_size_32", "32 MB PS2 Card (.ps2)"),
            TextOr("ps2ui_mc_size_64", "64 MB PS2 Card (.ps2)"),
            TextOr("ps2ui_mc_type_folder", "Folder Memory Card (Auto)"),
        };
        DrawListModal(TextOr("ps2ui_mc_create_title", "Create PS2 Memory Card"), opts, create_card_idx_, 390.0f);
    } else if (overlay_ == OVERLAY_CARD_ACTIONS) {
        std::string cardName;
        bool isDef = false;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (pending_card_idx_ >= 0 && pending_card_idx_ < static_cast<int>(memcards_.size())) {
                cardName = memcards_[pending_card_idx_].name;
                isDef = memcards_[pending_card_idx_].isDefault;
            }
        }
        std::vector<std::string> opts = {
            TextOr("ps2ui_mc_mount_1", "Mount to Slot 1"),
            TextOr("ps2ui_mc_mount_2", "Mount to Slot 2"),
            TextOr("ps2ui_mc_duplicate", "Duplicate Card"),
        };
        if (!isDef) {
            opts.push_back(TextOr("ps2ui_mc_delete", "Delete"));
        }
        DrawListModal(cardName.empty() ? TextOr("ps2ui_mc_actions", "Options") : cardName,
                      opts, card_action_menu_idx_, 360.0f);
    } else if (overlay_ == OVERLAY_GAME_INFO) {
        const int realIdx = SelectedMasterGameIndex();
        GameEntry g;
        bool hasGame = false;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (realIdx >= 0 && realIdx < static_cast<int>(games_.size())) {
                g = games_[realIdx];
                hasGame = true;
            }
        }
        if (hasGame) {
            const bool isBlueCd = (!g.isArcade && g.fileSizeBytes > 0 && g.fileSizeBytes < 750LL * 1024LL * 1024LL);
            const std::string mediaLabel = g.isArcade ? "Namco System 246 / 256 Arcade"
                : (isBlueCd ? "PlayStation 2 CD-ROM" : "PlayStation 2 DVD-ROM");
            const float mw = std::min(460.0f, screen_width_ - 40.0f);
            const float mh = 226.0f;
            const float mx = (screen_width_ - mw) * 0.5f;
            const float my = (kScreenHeight - mh) * 0.5f;
            DrawPanel(mx, my, mw, mh, 12.0f, 1.0f, PanelTopColor(0.95f), PanelBotColor(0.97f),
                      {acc.r, acc.g, acc.b, 0.55f});
            BeginQuadBatch();
            DrawText(TextOr("ps2ui_info_title", "Disc Information"), mx + 20.0f, my + 16.0f, 20.5f, STYLE_HEADER, acc);
            DrawText(g.title, mx + 20.0f, my + 50.0f, 16.5f, STYLE_BODY_BOLD, PrimaryTextColor(1.0f), mw - 40.0f, 2);
            DrawText(colonSpaced(TextOr("ps2ui_info_serial", "Serial: ")) + (g.serial.empty() ? "—" : g.serial),
                     mx + 20.0f, my + 102.0f, 14.5f, STYLE_MONO, SecondaryTextColor(0.92f));
            DrawText(colonSpaced(TextOr("ps2ui_info_media", "Media: ")) + mediaLabel,
                     mx + 20.0f, my + 128.0f, 14.5f, STYLE_MONO, SecondaryTextColor(0.92f));
            DrawText(colonSpaced(TextOr("ps2ui_info_size", "Size: ")) + (g.sizeStr.empty() ? "—" : g.sizeStr),
                     mx + 20.0f, my + 154.0f, 14.5f, STYLE_MONO, SecondaryTextColor(0.92f));
            FlushQuadBatch();

            DrawFocusBeam(mx + mw * 0.5f - 75.0f, my + mh - 48.0f, 150.0f, 36.0f, acc, 0.90f);
            BeginQuadBatch();
            DrawText(TextOr("ps2ui_close", "Close"), mx + mw * 0.5f - 75.0f, my + mh - 40.0f, 15.0f, STYLE_BODY_BOLD,
                     PrimaryTextColor(1.0f), 150.0f, 1, 1, 150.0f);
            FlushQuadBatch();
            RegisterHit(mx + mw * 0.5f - 75.0f, my + mh - 48.0f, 150.0f, 36.0f, HIT_OVERLAY_ITEM, 0);
        }
    } else if (overlay_ == OVERLAY_SYSTEM_INFO) {
        SystemStatus st;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            st = status_;
        }
        const float mw = std::min(480.0f, screen_width_ - 40.0f);
        const float mh = 246.0f;
        const float mx = (screen_width_ - mw) * 0.5f;
        const float my = (kScreenHeight - mh) * 0.5f;
        DrawPanel(mx, my, mw, mh, 12.0f, 1.0f, PanelTopColor(0.95f), PanelBotColor(0.97f),
                  {acc.r, acc.g, acc.b, 0.55f});
        BeginQuadBatch();
        DrawText(TextOr("ps2ui_set_sysinfo", "Version Information"), mx + 20.0f, my + 16.0f, 20.5f, STYLE_HEADER, acc);
        DrawText(colonSpaced(TextOr("ps2ui_sys_app", "Console: ")) + "PlayStation 2",
                 mx + 20.0f, my + 56.0f, 15.0f, STYLE_MONO, PrimaryTextColor(0.95f));
        DrawText(colonSpaced(TextOr("ps2ui_sys_device", "Device: ")) + st.deviceName,
                 mx + 20.0f, my + 84.0f, 15.0f, STYLE_MONO, SecondaryTextColor(0.90f));
        DrawText(colonSpaced(TextOr("ps2ui_sys_bios", "BIOS ROM: ")) + st.biosStatus,
                 mx + 20.0f, my + 112.0f, 15.0f, STYLE_MONO,
                 st.biosValid ? RgbaColor{0.42f, 0.96f, 0.68f, 1.0f} : RgbaColor{1.0f, 0.45f, 0.40f, 1.0f});
        DrawText(colonSpaced(TextOr("ps2ui_sys_storage", "Free Storage: ")) + st.freeStorage,
                 mx + 20.0f, my + 140.0f, 15.0f, STYLE_MONO, SecondaryTextColor(0.90f));
        FlushQuadBatch();

        DrawFocusBeam(mx + mw * 0.5f - 75.0f, my + mh - 48.0f, 150.0f, 36.0f, acc, 0.90f);
        BeginQuadBatch();
        DrawText(TextOr("ps2ui_close", "Close"), mx + mw * 0.5f - 75.0f, my + mh - 40.0f, 15.0f, STYLE_BODY_BOLD,
                 PrimaryTextColor(1.0f), 150.0f, 1, 1, 150.0f);
        FlushQuadBatch();
        RegisterHit(mx + mw * 0.5f - 75.0f, my + mh - 48.0f, 150.0f, 36.0f, HIT_OVERLAY_ITEM, 0);
    } else if (overlay_ == OVERLAY_TOUCH_GUIDE) {
        const float mw = std::min(510.0f, screen_width_ - 40.0f);
        const float mh = 256.0f;
        const float mx = (screen_width_ - mw) * 0.5f;
        const float my = (kScreenHeight - mh) * 0.5f;
        DrawPanel(mx, my, mw, mh, 12.0f, 1.0f, PanelTopColor(0.95f), PanelBotColor(0.97f),
                  {acc.r, acc.g, acc.b, 0.55f});
        BeginQuadBatch();
        DrawText(TextOr("ps2ui_touch_guide_title", "Touch Controls Guide"), mx + 20.0f, my + 16.0f, 20.5f, STYLE_HEADER, acc);
        DrawText("• " + TextOr("ps2ui_touch_tip_1", "Tap an item to focus, tap again to open or launch"),
                 mx + 20.0f, my + 58.0f, 15.0f, STYLE_BODY, PrimaryTextColor(0.94f), mw - 40.0f, 2);
        DrawText("• " + TextOr("ps2ui_touch_tip_2", "Swipe up or down to scroll lists smoothly"),
                 mx + 20.0f, my + 92.0f, 15.0f, STYLE_BODY, PrimaryTextColor(0.94f), mw - 40.0f, 2);
        DrawText("• " + TextOr("ps2ui_touch_tip_3", "Long-press a game or card for extra options"),
                 mx + 20.0f, my + 126.0f, 15.0f, STYLE_BODY, PrimaryTextColor(0.94f), mw - 40.0f, 2);
        DrawText("• " + TextOr("ps2ui_touch_tip_4", "Two-finger tap goes back anywhere"),
                 mx + 20.0f, my + 160.0f, 15.0f, STYLE_BODY, PrimaryTextColor(0.94f), mw - 40.0f, 2);
        FlushQuadBatch();

        DrawFocusBeam(mx + mw * 0.5f - 80.0f, my + mh - 48.0f, 160.0f, 36.0f, acc, 0.90f);
        BeginQuadBatch();
        DrawText(TextOr("ps2ui_got_it", "Got it"), mx + mw * 0.5f - 80.0f, my + mh - 40.0f, 15.0f, STYLE_BODY_BOLD,
                 PrimaryTextColor(1.0f), 160.0f, 1, 1, 160.0f);
        FlushQuadBatch();
        RegisterHit(mx + mw * 0.5f - 80.0f, my + mh - 48.0f, 160.0f, 36.0f, HIT_OVERLAY_ITEM, 0);
    }
}

void Ps2Renderer::DrawScanlinesPass() {
    if (settings_.scanlinesMode == 0) return;
    FlushQuadBatch();
    glUseProgram(prog_scan_);
    glUniform2f(scan_u_viewport_, static_cast<float>(view_width_), static_cast<float>(view_height_));
    glUniform1f(scan_u_strength_, settings_.scanlinesMode == 2 ? 0.24f : 0.11f);
    glBindVertexArray(fs_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Ps2Renderer::Draw() {
    if (!gl_ready_) return;

    const auto now = std::chrono::steady_clock::now();
    float dt = 0.016f;
    if (has_frame_tp_) {
        dt = std::chrono::duration<float>(now - last_frame_tp_).count();
        dt = std::clamp(dt, 0.001f, 0.05f);
        smooth_dt_ += (dt - smooth_dt_) * 0.25f;
        dt = smooth_dt_;
    }
    last_frame_tp_ = now;
    has_frame_tp_ = true;

    time_sec_ += dt;
    disc_spin_angle_ += dt * (launching_ ? 7.5f : 1.55f);
    if (disc_slide_t_ < 1.0f) {
        disc_slide_t_ = std::min(1.0f, disc_slide_t_ + dt * 3.8f);
    }
    image_decodes_this_frame_ = 0;
    text_raster_ms_this_frame_ = 0.0;

    // Smooth autonomous harmonic orbital course changes + swoop decay (decoupled from menu cursor)
    const float targetTilt = (screen_ == SCREEN_MAIN_MENU)
        ? (std::sin(time_sec_ * 0.29f) * 0.65f + std::cos(time_sec_ * 0.17f) * 0.35f)
        : 0.0f;
    orb_tilt_cur_ += (targetTilt - orb_tilt_cur_) * std::min(1.0f, dt * 4.5f);
    orb_swoop_t_ = std::max(0.0f, orb_swoop_t_ - dt * 0.90f);
    orb_flash_t_ = std::max(0.0f, orb_flash_t_ - dt * 3.0f);

    if (cursor_pos_init_) {
        cursor_x_ += (cursor_target_x_ - cursor_x_) * std::min(1.0f, dt * 18.0f);
        cursor_y_ += (cursor_target_y_ - cursor_y_) * std::min(1.0f, dt * 18.0f);
        cursor_trail_x_ += (cursor_x_ - cursor_trail_x_) * std::min(1.0f, dt * 7.5f);
        cursor_trail_y_ += (cursor_y_ - cursor_trail_y_) * std::min(1.0f, dt * 7.5f);
    }
    cursor_flash_t_ = std::max(0.0f, cursor_flash_t_ - dt * 3.5f);

    game_scroll_y_ += (game_scroll_target_y_ - game_scroll_y_) * std::min(1.0f, dt * 16.0f);
    settings_scroll_y_ += (settings_scroll_target_y_ - settings_scroll_y_) * std::min(1.0f, dt * 16.0f);
    mc_card_scroll_y_ += (mc_card_scroll_target_y_ - mc_card_scroll_y_) * std::min(1.0f, dt * 16.0f);

    if (screen_fade_t_ < 1.0f) {
        screen_fade_t_ = std::min(1.0f, screen_fade_t_ + dt * 4.5f);
    }

    if (boot_active_) {
        if (!boot_sfx_played_) {
            boot_sfx_played_ = true;
            HostPlaySfx(SFX_BOOT_INTRO);
        }
        boot_t_ += dt;
        if (boot_t_ >= boot_duration_) {
            boot_active_ = false;
            screen_fade_t_ = 0.55f;
            if (screen_ == SCREEN_MAIN_MENU) {
                // Like the BIOS: after the intro the orbs open up from the centre.
                orb_swoop_t_ = 1.0f;
                orb_swoop_dir_ = -1;
            }
        }
    }

    if (launching_) {
        launch_t_ += dt;
        if (!launch_dispatched_ && launch_t_ >= 1.05f) {
            launch_dispatched_ = true;
            if (launch_is_save_) {
                HostNotifySaveLaunch(launch_path_, launch_save_slot_);
            } else {
                HostNotifyGameLaunch(launch_path_);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (image_cache_dirty_) {
            for (const auto& p : dirty_image_paths_) {
                auto it = image_cache_.find(p);
                if (it != image_cache_.end()) {
                    if (it->second.tex) glDeleteTextures(1, &it->second.tex);
                    image_lru_.erase(it->second.lruIt);
                    image_cache_.erase(it);
                }
            }
            dirty_image_paths_.clear();
            image_cache_dirty_ = false;
        }
    }

    const float surfAspect = static_cast<float>(surface_width_) / static_cast<float>(surface_height_);
    float targetAspect = surfAspect;
    if (settings_.aspectMode == 1) targetAspect = 16.0f / 9.0f;
    else if (settings_.aspectMode == 2) targetAspect = 4.0f / 3.0f;

    if (surfAspect > targetAspect + 0.005f) {
        view_height_ = surface_height_;
        view_width_ = std::max(1, static_cast<int>(std::round(surface_height_ * targetAspect)));
        view_x_ = (surface_width_ - view_width_) / 2;
        view_y_ = 0;
    } else if (surfAspect < targetAspect - 0.005f) {
        view_width_ = surface_width_;
        view_height_ = std::max(1, static_cast<int>(std::round(surface_width_ / targetAspect)));
        view_x_ = 0;
        view_y_ = (surface_height_ - view_height_) / 2;
    } else {
        view_x_ = 0;
        view_y_ = 0;
        view_width_ = surface_width_;
        view_height_ = surface_height_;
    }

    screen_width_ = kScreenHeight * (static_cast<float>(view_width_) / static_cast<float>(view_height_));
    // Rasterize text at the panel's real pixel density (1:1), so it stays crisp on tall screens.
    raster_scale_ = std::clamp(static_cast<float>(view_height_) / kScreenHeight, 1.25f, 4.0f);
    if (std::fabs(raster_scale_ - last_raster_scale_) > 0.15f) {
        ClearTextCache();
        last_raster_scale_ = raster_scale_;
    }

    glViewport(0, 0, surface_width_, surface_height_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glViewport(view_x_, view_y_, view_width_, view_height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    hit_targets_.clear();

    // Pass 1: Pure #000000 Black Void (+ Localized Mist behind Boot / Main Menu Vortex).
    // With no mist the shader outputs pure black, which the glClear above already gave us,
    // so skip the full-screen pass entirely (saves a blended full-res fill on most screens).
    if (mist_strength_cur_ > 0.002f) {
        glUseProgram(prog_bg_);
        glUniform2f(bg_u_screen_, screen_width_, kScreenHeight);
        glUniform2f(bg_u_mist_center_, orb_center_cur_x_, orb_center_cur_y_);
        glUniform1f(bg_u_time_, time_sec_);
        glUniform1f(bg_u_mist_strength_, mist_strength_cur_);
        glUniform1i(bg_u_theme_, std::clamp(settings_.theme, 0, kThemeCount - 1));
        glBindVertexArray(fs_vao_);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // Pass 2: 3D Orbiting Crystal Clock Orbs w/ Curved Luminous Plasma Ribbons
    const float uiAlpha = EaseOutCubic(screen_fade_t_);
    UpdateAndDrawOrbs3D(dt);

    // Pass 3: Active Scene UI (Status Bar & Bottom Bar drawn last so nothing paints over them)
    if (boot_active_) {
        DrawBootIntro(1.0f);
    } else if (launching_) {
        DrawLaunchStage();
    } else {
        if (screen_ == SCREEN_MAIN_MENU) {
            DrawMainMenu(uiAlpha);
        } else if (screen_ == SCREEN_GAMES) {
            DrawGamesScreen(uiAlpha);
        } else if (screen_ == SCREEN_BROWSER) {
            DrawBrowserScreen(uiAlpha);
        } else if (screen_ == SCREEN_SETTINGS) {
            DrawSettingsScreen(uiAlpha);
        }
        DrawStatusBar(uiAlpha);
        DrawBottomBar(uiAlpha);
        DrawOverlay();
    }

    // Pass 4: Optional CRT Scanlines
    DrawScanlinesPass();
}

void Ps2Renderer::RegisterHit(float x, float y, float w, float h, int kind, int index) {
    if (w <= 0.0f || h <= 0.0f) return;
    hit_targets_.push_back({x, y, w, h, kind, index});
}

const Ps2Renderer::HitTarget* Ps2Renderer::FindHit(float vx, float vy) const {
    for (auto it = hit_targets_.rbegin(); it != hit_targets_.rend(); ++it) {
        if (vx >= it->x && vx <= it->x + it->w && vy >= it->y && vy <= it->y + it->h) {
            return &(*it);
        }
    }
    return nullptr;
}

bool Ps2Renderer::SurfaceNormToVirtual(float normX, float normY, float& outVx, float& outVy) const {
    const float px = normX * static_cast<float>(surface_width_);
    const float py = normY * static_cast<float>(surface_height_);
    if (view_width_ <= 0 || view_height_ <= 0) return false;
    outVx = ((px - static_cast<float>(view_x_)) / static_cast<float>(view_width_)) * screen_width_;
    outVy = ((py - static_cast<float>(view_y_)) / static_cast<float>(view_height_)) * kScreenHeight;
    return outVx >= 0.0f && outVx <= screen_width_ && outVy >= 0.0f && outVy <= kScreenHeight;
}

void Ps2Renderer::SwitchScreen(int nextScreen) {
    if (screen_ != nextScreen) {
        screen_ = nextScreen;
        screen_fade_t_ = 0.45f;
        orb_swoop_t_ = 1.0f;
        orb_swoop_dir_ = (nextScreen == SCREEN_MAIN_MENU) ? -1 : 1;
        orb_flash_t_ = 1.0f;
        cursor_pos_init_ = false;
        if (nextScreen == SCREEN_GAMES) {
            SnapGameScrollToSelected();
        }
        // Persist right away, so a process kill can't reopen the shell on an older screen.
        HostNotifyNavigation(screen_, main_index_, game_index_, mc_slot_index_);
    }
}

void Ps2Renderer::StartLaunchGame(
    const std::string& path,
    const std::string& title,
    const std::string& serial,
    const std::string& coverPath,
    bool isBlueCd) {
    if (path.empty() || launching_) return;
    pending_restore_game_path_ = path;
    selected_game_path_ = path;
    launching_ = true;
    launch_is_save_ = false;
    launch_dispatched_ = false;
    launch_is_blue_cd_ = isBlueCd;
    launch_t_ = 0.0f;
    launch_path_ = path;
    launch_title_ = title;
    launch_serial_ = serial;
    launch_cover_ = coverPath;
    HostPlaySfx(SFX_DISC_LAUNCH);
}

void Ps2Renderer::StartLaunchQuickSave(
    const std::string& path,
    int slot,
    const std::string& title,
    const std::string& serial,
    const std::string& coverPath,
    bool isBlueCd) {
    if (path.empty() || launching_) return;
    pending_restore_game_path_ = path;
    selected_game_path_ = path;
    launching_ = true;
    launch_is_save_ = true;
    launch_save_slot_ = slot;
    launch_dispatched_ = false;
    launch_is_blue_cd_ = isBlueCd;
    launch_t_ = 0.0f;
    launch_path_ = path;
    launch_title_ = title;
    launch_serial_ = serial;
    launch_cover_ = coverPath;
    HostPlaySfx(SFX_DISC_LAUNCH);
}

void Ps2Renderer::StartLaunchSave(int filteredIndex) {
    std::vector<int> filtered = FilteredSaveIndices();
    if (filteredIndex < 0 || filteredIndex >= static_cast<int>(filtered.size())) {
        HostPlaySfx(SFX_ERROR);
        return;
    }
    SaveEntry sv;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (filtered[filteredIndex] >= static_cast<int>(saves_.size())) {
            HostPlaySfx(SFX_ERROR);
            return;
        }
        sv = saves_[filtered[filteredIndex]];
    }
    if (sv.gamePath.empty()) {
        HostPlaySfx(SFX_ERROR);
        return;
    }
    StartLaunchQuickSave(sv.gamePath, sv.slotNum, sv.title, sv.serial, sv.previewPath, false);
}

void Ps2Renderer::ActivateGameLaunch(int gameIdx) {
    GameEntry g;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (gameIdx < 0 || gameIdx >= static_cast<int>(games_.size())) {
            HostPlaySfx(SFX_ERROR);
            return;
        }
        g = games_[gameIdx];
    }
    const bool isBlueCd = (!g.isArcade && g.fileSizeBytes > 0 && g.fileSizeBytes < 750LL * 1024LL * 1024LL);
    StartLaunchGame(g.path, g.title, g.serial, g.coverPath, isBlueCd);
}

void Ps2Renderer::PushSettingsToHost() {
    int32_t arr[13] = {
        settings_.theme,
        settings_.aspectMode,
        settings_.orbsMode,
        settings_.towersMode,
        settings_.scanlinesMode,
        settings_.bootAnimMode,
        settings_.keyTone ? 1 : 0,
        settings_.ambientSound ? 1 : 0,
        settings_.swapConfirm ? 1 : 0,
        settings_.touchControls ? 1 : 0,
        settings_.time24h ? 1 : 0,
        settings_.dateFormat,
        settings_.keepScreenOn ? 1 : 0,
    };
    HostNotifySettingsChanged(arr, 13);
}

void Ps2Renderer::CycleSetting(int row, int delta) {
    auto wrap = [delta](int val, int count) -> int {
        return ((val + delta) % count + count) % count;
    };
    bool changed = true;
    switch (row) {
        case 0: settings_.theme = wrap(settings_.theme, kThemeCount); break;
        case 1: settings_.aspectMode = wrap(settings_.aspectMode, 3); break;
        case 2: settings_.orbsMode = wrap(settings_.orbsMode, 3); break;
        case 3: settings_.towersMode = wrap(settings_.towersMode, 3); break;
        case 4: settings_.scanlinesMode = wrap(settings_.scanlinesMode, 3); break;
        case 5: settings_.bootAnimMode = wrap(settings_.bootAnimMode, 3); break;
        case 6: settings_.keyTone = !settings_.keyTone; break;
        case 7: settings_.ambientSound = !settings_.ambientSound; break;
        case 8: settings_.swapConfirm = !settings_.swapConfirm; break;
        case 9: settings_.touchControls = !settings_.touchControls; break;
        case 10:
            changed = false;
            overlay_ = OVERLAY_TOUCH_GUIDE;
            break;
        case 11: settings_.time24h = !settings_.time24h; break;
        case 12: settings_.dateFormat = wrap(settings_.dateFormat, 3); break;
        case 13: settings_.keepScreenOn = !settings_.keepScreenOn; break;
        case 14:
            changed = false;
            overlay_ = OVERLAY_SYSTEM_INFO;
            break;
        case 15:
            changed = false;
            dialog_btn_ = 1;
            overlay_ = OVERLAY_RESTORE_CONFIRM;
            break;
        default:
            changed = false;
            break;
    }
    HostPlaySfx(SFX_CONFIRM);
    if (changed) {
        PushSettingsToHost();
    }
}

std::vector<int> Ps2Renderer::BuildGameFilter(const std::vector<GameEntry>& games,
                                              const std::vector<int>& recentIndices,
                                              bool hasArcade, int tab) {
    std::vector<int> out;
    if (tab == GAME_TAB_RECENT && !recentIndices.empty()) {
        out.reserve(recentIndices.size());
        for (int idx : recentIndices) {
            if (idx >= 0 && idx < static_cast<int>(games.size())) out.push_back(idx);
        }
        if (!out.empty()) return out;
    }
    // PS2 tab (or a Recent tab with nothing left in it): PS2 titles when arcade games split
    // the library, otherwise the whole library.
    out.reserve(games.size());
    for (int i = 0; i < static_cast<int>(games.size()); ++i) {
        if (!hasArcade || games[i].isArcade == (tab == GAME_TAB_ARCADE)) {
            out.push_back(i);
        }
    }
    return out;
}

std::vector<int> Ps2Renderer::FilteredGameIndices() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return BuildGameFilter(games_, recent_indices_, has_arcade_games_, game_tab_);
}

std::vector<int> Ps2Renderer::VisibleGameTabsFor(bool hasRecent, bool hasArcade) {
    std::vector<int> tabs;
    if (!hasRecent && !hasArcade) return tabs;
    if (hasRecent) tabs.push_back(GAME_TAB_RECENT);
    tabs.push_back(GAME_TAB_PS2);
    if (hasArcade) tabs.push_back(GAME_TAB_ARCADE);
    return tabs;
}

std::vector<int> Ps2Renderer::VisibleGameTabs() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return VisibleGameTabsFor(!recent_indices_.empty(), has_arcade_games_);
}

void Ps2Renderer::SelectGameTab(int tab) {
    game_tab_ = tab;
    game_index_ = 0;
    game_scroll_target_y_ = 0.0f;
    last_disc_game_idx_ = -1;
    cursor_pos_init_ = false;
}

void Ps2Renderer::RebuildRecentIndicesLocked() {
    recent_indices_.clear();
    if (recent_paths_.empty() || games_.empty()) return;
    std::unordered_map<std::string, int> byPath;
    byPath.reserve(games_.size());
    for (int i = 0; i < static_cast<int>(games_.size()); ++i) {
        byPath.emplace(games_[i].path, i);
    }
    for (const auto& p : recent_paths_) {
        auto it = byPath.find(p);
        if (it != byPath.end() &&
            std::find(recent_indices_.begin(), recent_indices_.end(), it->second) == recent_indices_.end()) {
            recent_indices_.push_back(it->second);
        }
    }
}

void Ps2Renderer::NormalizeGameTabLocked() {
    if (game_tab_ == GAME_TAB_RECENT && !recent_indices_.empty()) return;
    if (game_tab_ == GAME_TAB_ARCADE && has_arcade_games_) return;
    if (game_tab_ == GAME_TAB_PS2 && (has_ps2_games_ || !has_arcade_games_)) return;
    game_tab_ = (has_arcade_games_ && !has_ps2_games_) ? GAME_TAB_ARCADE : GAME_TAB_PS2;
}

bool Ps2Renderer::LocateGameInCurrentTab(const std::string& path) {
    if (path.empty()) return false;
    std::vector<int> filtered;
    int masterIdx = -1;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        filtered = BuildGameFilter(games_, recent_indices_, has_arcade_games_, game_tab_);
        for (int i = 0; i < static_cast<int>(games_.size()); ++i) {
            if (games_[i].path == path) {
                masterIdx = i;
                break;
            }
        }
    }
    if (masterIdx < 0) return false;
    for (int fi = 0; fi < static_cast<int>(filtered.size()); ++fi) {
        if (filtered[fi] == masterIdx) {
            game_index_ = fi;
            return true;
        }
    }
    return false;
}

int Ps2Renderer::SelectedMasterGameIndex() const {
    const std::vector<int> filtered = FilteredGameIndices();
    if (filtered.empty()) return -1;
    return filtered[std::clamp(game_index_, 0, static_cast<int>(filtered.size()) - 1)];
}

std::vector<int> Ps2Renderer::FilteredSaveIndices() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    std::vector<int> out;
    if (mc_tab_ == 0 || mc_filter_game_path_.empty()) {
        out.reserve(saves_.size());
        for (int i = 0; i < static_cast<int>(saves_.size()); ++i) {
            out.push_back(i);
        }
        return out;
    }
    for (int i = 0; i < static_cast<int>(saves_.size()); ++i) {
        if (saves_[i].gamePath == mc_filter_game_path_) {
            out.push_back(i);
        }
    }
    return out;
}

void Ps2Renderer::ClampSelectionIndices() {
    main_index_ = std::clamp(main_index_, 0, 2);
    const std::vector<int> filteredGames = FilteredGameIndices();
    if (!filteredGames.empty()) {
        game_index_ = std::clamp(game_index_, 0, static_cast<int>(filteredGames.size()) - 1);
        std::lock_guard<std::mutex> lock(data_mutex_);
        const int realIdx = filteredGames[game_index_];
        if (realIdx >= 0 && realIdx < static_cast<int>(games_.size())) {
            selected_game_path_ = games_[realIdx].path;
        }
    } else {
        game_index_ = 0;
    }
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (!memcards_.empty()) {
            mc_card_list_idx_ = std::clamp(mc_card_list_idx_, 0, static_cast<int>(memcards_.size()) - 1);
        } else {
            mc_card_list_idx_ = 0;
        }
    }
    settings_index_ = std::clamp(settings_index_, 0, kSettingsCount - 1);
}

void Ps2Renderer::SnapGameScrollToSelected() {
    const std::vector<int> filtered = FilteredGameIndices();
    if (filtered.empty()) {
        game_scroll_target_y_ = 0.0f;
        game_scroll_y_ = 0.0f;
        return;
    }
    game_index_ = std::clamp(game_index_, 0, static_cast<int>(filtered.size()) - 1);
    const float rowStep = 60.0f;
    const float viewH = std::max(120.0f, ComputeMetrics().bodyH - (GameTabsShown() ? 80.0f : 42.0f));
    const float maxScroll = std::max(0.0f, static_cast<float>(filtered.size()) * rowStep - (viewH - 12.0f));
    const float target = std::clamp(static_cast<float>(game_index_) * rowStep - viewH * 0.35f, 0.0f, maxScroll);
    game_scroll_target_y_ = target;
    game_scroll_y_ = target;
    cursor_pos_init_ = false;
}

void Ps2Renderer::OnKey(int key) {
    if (launching_) return;
    if (boot_active_) {
        if (key == KEY_CONFIRM || key == KEY_BACK || key == KEY_START) {
            boot_active_ = false;
            screen_fade_t_ = 0.55f;
        }
        return;
    }

    if (overlay_ != OVERLAY_NONE) {
        if (overlay_ == OVERLAY_EXIT_CONFIRM ||
            overlay_ == OVERLAY_DELETE_SAVE_CONFIRM ||
            overlay_ == OVERLAY_DELETE_CARD_CONFIRM ||
            overlay_ == OVERLAY_RESTORE_CONFIRM) {
            if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN) {
                dialog_btn_ = 1 - dialog_btn_;
                HostPlaySfx(SFX_MOVE);
            } else if (key == KEY_CONFIRM) {
                if (dialog_btn_ == 0) {
                    HostPlaySfx(SFX_CONFIRM);
                    if (overlay_ == OVERLAY_EXIT_CONFIRM) {
                        overlay_ = OVERLAY_NONE;
                        HostNotifyExitRequested();
                    } else if (overlay_ == OVERLAY_DELETE_SAVE_CONFIRM) {
                        int delIdx = pending_delete_save_idx_;
                        overlay_ = OVERLAY_NONE;
                        pending_delete_save_idx_ = -1;
                        if (delIdx >= 0) HostNotifyDeleteSave(delIdx);
                    } else if (overlay_ == OVERLAY_DELETE_CARD_CONFIRM) {
                        int delCard = pending_card_idx_;
                        overlay_ = OVERLAY_NONE;
                        pending_card_idx_ = -1;
                        if (delCard >= 0) HostNotifyMemoryCardAction(MC_ACTION_DELETE_CARD, mc_card_slot_idx_ + 1, delCard, 0);
                    } else if (overlay_ == OVERLAY_RESTORE_CONFIRM) {
                        settings_ = ShellSettings{};
                        overlay_ = OVERLAY_NONE;
                        PushSettingsToHost();
                    }
                } else {
                    HostPlaySfx(SFX_CANCEL);
                    overlay_ = OVERLAY_NONE;
                }
            } else if (key == KEY_BACK) {
                HostPlaySfx(SFX_CANCEL);
                overlay_ = OVERLAY_NONE;
            }
            return;
        }

        if (overlay_ == OVERLAY_GAME_OPTIONS) {
            const int realIdx = SelectedMasterGameIndex();
            GameEntry g;
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                if (realIdx < 0 || realIdx >= static_cast<int>(games_.size())) {
                    overlay_ = OVERLAY_NONE;
                    return;
                }
                g = games_[realIdx];
            }
            const bool hasCont = (g.latestSaveSlot >= 0);
            const int optCount = hasCont ? 5 : 4;
            if (key == KEY_UP) {
                options_index_ = (options_index_ - 1 + optCount) % optCount;
                HostPlaySfx(SFX_MOVE);
            } else if (key == KEY_DOWN) {
                options_index_ = (options_index_ + 1) % optCount;
                HostPlaySfx(SFX_MOVE);
            } else if (key == KEY_BACK || key == KEY_OPTIONS) {
                overlay_ = OVERLAY_NONE;
                HostPlaySfx(SFX_CANCEL);
            } else if (key == KEY_CONFIRM) {
                int sel = options_index_;
                overlay_ = OVERLAY_NONE;
                if (sel == 0) {
                    ActivateGameLaunch(realIdx);
                } else if (hasCont && sel == 1) {
                    const bool isBlueCd = (!g.isArcade && g.fileSizeBytes > 0 && g.fileSizeBytes < 750LL * 1024LL * 1024LL);
                    StartLaunchQuickSave(g.path, g.latestSaveSlot, g.title, g.serial, g.coverPath, isBlueCd);
                } else {
                    int rem = sel - (hasCont ? 2 : 1);
                    if (rem == 0) {
                        mc_filter_game_path_ = g.path;
                        mc_filter_game_title_ = g.title;
                        mc_tab_ = 1;
                        mc_open_slot_ = 0;
                        mc_page_ = 0;
                        mc_slot_index_ = 0;
                        mc_focus_pane_ = 0;
                        return_screen_ = SCREEN_GAMES;
                        SwitchScreen(SCREEN_BROWSER);
                        HostPlaySfx(SFX_CONFIRM);
                    } else if (rem == 1) {
                        overlay_ = OVERLAY_GAME_INFO;
                        HostPlaySfx(SFX_CONFIRM);
                    } else if (rem == 2) {
                        HostPlaySfx(SFX_CONFIRM);
                        HostNotifyOpenGameSettings(realIdx);
                    }
                }
            }
            return;
        }

        if (overlay_ == OVERLAY_CREATE_CARD) {
            constexpr int optCount = 5;
            if (key == KEY_UP) {
                create_card_idx_ = (create_card_idx_ - 1 + optCount) % optCount;
                HostPlaySfx(SFX_MOVE);
            } else if (key == KEY_DOWN) {
                create_card_idx_ = (create_card_idx_ + 1) % optCount;
                HostPlaySfx(SFX_MOVE);
            } else if (key == KEY_BACK) {
                overlay_ = OVERLAY_NONE;
                HostPlaySfx(SFX_CANCEL);
            } else if (key == KEY_CONFIRM) {
                const int sel = create_card_idx_;
                overlay_ = OVERLAY_NONE;
                HostPlaySfx(SFX_CONFIRM);
                const int targetSlot = mc_card_slot_idx_ + 1;
                if (sel == 4) {
                    HostNotifyMemoryCardAction(MC_ACTION_CREATE_FOLDER, targetSlot, 0, 0);
                } else {
                    const int sizes[4] = {8, 16, 32, 64};
                    HostNotifyMemoryCardAction(MC_ACTION_CREATE_PS2, targetSlot, 0, sizes[std::clamp(sel, 0, 3)]);
                }
            }
            return;
        }

        if (overlay_ == OVERLAY_CARD_ACTIONS) {
            bool isDef = false;
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                if (pending_card_idx_ >= 0 && pending_card_idx_ < static_cast<int>(memcards_.size())) {
                    isDef = memcards_[pending_card_idx_].isDefault;
                }
            }
            const int optCount = isDef ? 3 : 4;
            if (key == KEY_UP) {
                card_action_menu_idx_ = (card_action_menu_idx_ - 1 + optCount) % optCount;
                HostPlaySfx(SFX_MOVE);
            } else if (key == KEY_DOWN) {
                card_action_menu_idx_ = (card_action_menu_idx_ + 1) % optCount;
                HostPlaySfx(SFX_MOVE);
            } else if (key == KEY_BACK || key == KEY_OPTIONS) {
                overlay_ = OVERLAY_NONE;
                HostPlaySfx(SFX_CANCEL);
            } else if (key == KEY_CONFIRM) {
                const int sel = card_action_menu_idx_;
                const int cIdx = pending_card_idx_;
                overlay_ = OVERLAY_NONE;
                if (sel == 0) {
                    HostPlaySfx(SFX_CONFIRM);
                    HostNotifyMemoryCardAction(MC_ACTION_MOUNT_CARD, 1, cIdx, 0);
                } else if (sel == 1) {
                    HostPlaySfx(SFX_CONFIRM);
                    HostNotifyMemoryCardAction(MC_ACTION_MOUNT_CARD, 2, cIdx, 0);
                } else if (sel == 2) {
                    HostPlaySfx(SFX_CONFIRM);
                    HostNotifyMemoryCardAction(MC_ACTION_DUPLICATE_CARD, 0, cIdx, 0);
                } else if (!isDef && sel == 3) {
                    pending_card_idx_ = cIdx;
                    dialog_btn_ = 1;
                    overlay_ = OVERLAY_DELETE_CARD_CONFIRM;
                    HostPlaySfx(SFX_CONFIRM);
                }
            }
            return;
        }

        if (key == KEY_CONFIRM || key == KEY_BACK) {
            overlay_ = OVERLAY_NONE;
            HostPlaySfx(SFX_CANCEL);
        }
        return;
    }

    if (screen_ == SCREEN_MAIN_MENU) {
        if (key == KEY_UP) {
            main_index_ = (main_index_ + 2) % 3;
            HostPlaySfx(SFX_MOVE);
        } else if (key == KEY_DOWN) {
            main_index_ = (main_index_ + 1) % 3;
            HostPlaySfx(SFX_MOVE);
        } else if (key == KEY_OPTIONS) {
            overlay_ = OVERLAY_SYSTEM_INFO;
            HostPlaySfx(SFX_CONFIRM);
        } else if (key == KEY_CONFIRM || key == KEY_START) {
            HostPlaySfx(SFX_CONFIRM);
            return_screen_ = SCREEN_MAIN_MENU;
            if (main_index_ == 0) {
                // The games screen opens on Recent once at least one game has been played.
                bool hasRecent = false;
                {
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    hasRecent = !recent_indices_.empty();
                }
                if (hasRecent) {
                    SelectGameTab(GAME_TAB_RECENT);
                    SnapGameScrollToSelected();
                }
                SwitchScreen(SCREEN_GAMES);
            } else if (main_index_ == 1) {
                mc_tab_ = 0; // Restore Quick Saves as default Browser tab
                mc_open_slot_ = 0;
                mc_page_ = 0;
                mc_slot_index_ = 0;
                mc_focus_pane_ = 0;
                mc_card_pane_ = 0;
                mc_filter_game_path_.clear();
                SwitchScreen(SCREEN_BROWSER);
            } else {
                SwitchScreen(SCREEN_SETTINGS);
            }
        } else if (key == KEY_BACK) {
            dialog_btn_ = 1;
            overlay_ = OVERLAY_EXIT_CONFIRM;
            HostPlaySfx(SFX_CANCEL);
        }
        return;
    }

    if (screen_ == SCREEN_GAMES) {
        if (key == KEY_BACK) {
            SwitchScreen(SCREEN_MAIN_MENU);
            HostPlaySfx(SFX_CANCEL);
            return;
        }
        if (key == KEY_PREV_TAB || key == KEY_NEXT_TAB || key == KEY_LEFT || key == KEY_RIGHT) {
            const std::vector<int> tabs = VisibleGameTabs();
            if (tabs.size() >= 2) {
                const int count = static_cast<int>(tabs.size());
                int pos = static_cast<int>(std::find(tabs.begin(), tabs.end(), game_tab_) - tabs.begin());
                if (pos >= count) pos = 0;
                const int dir = (key == KEY_PREV_TAB || key == KEY_LEFT) ? -1 : 1;
                SelectGameTab(tabs[(pos + dir + count) % count]);
                HostPlaySfx(SFX_MOVE);
                return;
            }
        }
        const std::vector<int> filtered = FilteredGameIndices();
        const int n = static_cast<int>(filtered.size());
        if (n == 0) return;

        game_index_ = std::clamp(game_index_, 0, n - 1);
        const int realIdx = filtered[game_index_];
        GameEntry curGame;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (realIdx < static_cast<int>(games_.size())) {
                curGame = games_[realIdx];
            }
        }

        auto ensureGameVisible = [&]() {
            const float rowStep = 60.0f;
            const float viewH = ComputeMetrics().bodyH - (GameTabsShown() ? 80.0f : 42.0f);
            const float itemTop = static_cast<float>(game_index_) * rowStep;
            const float itemBot = itemTop + 54.0f;
            if (itemTop < game_scroll_target_y_) game_scroll_target_y_ = itemTop;
            else if (itemBot > game_scroll_target_y_ + viewH) game_scroll_target_y_ = itemBot - viewH;
        };

        if (key == KEY_UP) {
            game_index_ = (game_index_ - 1 + n) % n;
            ensureGameVisible();
            HostPlaySfx(SFX_MOVE);
        } else if (key == KEY_DOWN) {
            game_index_ = (game_index_ + 1) % n;
            ensureGameVisible();
            HostPlaySfx(SFX_MOVE);
        } else if (key == KEY_PREV_TAB) {
            game_index_ = std::max(0, game_index_ - 6);
            ensureGameVisible();
            HostPlaySfx(SFX_MOVE);
        } else if (key == KEY_NEXT_TAB) {
            game_index_ = std::min(n - 1, game_index_ + 6);
            ensureGameVisible();
            HostPlaySfx(SFX_MOVE);
        } else if (key == KEY_CONFIRM || key == KEY_START) {
            ActivateGameLaunch(realIdx);
        } else if (key == KEY_SECONDARY) {
            if (curGame.latestSaveSlot >= 0) {
                const bool isBlueCd = (!curGame.isArcade && curGame.fileSizeBytes > 0 && curGame.fileSizeBytes < 750LL * 1024LL * 1024LL);
                StartLaunchQuickSave(curGame.path, curGame.latestSaveSlot, curGame.title, curGame.serial, curGame.coverPath, isBlueCd);
            } else {
                HostPlaySfx(SFX_ERROR);
            }
        } else if (key == KEY_OPTIONS) {
            options_index_ = 0;
            overlay_ = OVERLAY_GAME_OPTIONS;
            HostPlaySfx(SFX_CONFIRM);
        }
        return;
    }

    if (screen_ == SCREEN_BROWSER) {
        // Inside an opened PS2 Memory Card showing 3D animated save icons
        if (mc_tab_ == 2 && mc_open_slot_ > 0) {
            if (key == KEY_BACK) {
                mc_open_slot_ = 0;
                cursor_pos_init_ = false;
                HostPlaySfx(SFX_CANCEL);
                return;
            }
            int nSaves = 0;
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                nSaves = static_cast<int>((mc_open_slot_ == 1 ? slot1_contents_ : slot2_contents_).saves.size());
            }
            if (nSaves <= 0) return;
            const int cols = (screen_width_ >= 800.0f) ? 5 : 4;
            if (key == KEY_LEFT) {
                mc_inner_idx_ = (mc_inner_idx_ - 1 + nSaves) % nSaves;
                HostPlaySfx(SFX_MOVE);
            } else if (key == KEY_RIGHT) {
                mc_inner_idx_ = (mc_inner_idx_ + 1) % nSaves;
                HostPlaySfx(SFX_MOVE);
            } else if (key == KEY_UP) {
                mc_inner_idx_ = (mc_inner_idx_ - cols + nSaves * 2) % nSaves;
                HostPlaySfx(SFX_MOVE);
            } else if (key == KEY_DOWN) {
                mc_inner_idx_ = (mc_inner_idx_ + cols) % nSaves;
                HostPlaySfx(SFX_MOVE);
            }
            return;
        }

        if (key == KEY_BACK) {
            if (mc_focus_pane_ == 1) {
                mc_focus_pane_ = 0;
                HostPlaySfx(SFX_CANCEL);
            } else {
                SwitchScreen(return_screen_);
                HostPlaySfx(SFX_CANCEL);
            }
            return;
        }
        if (key == KEY_PREV_TAB) {
            mc_tab_ = (mc_tab_ + 2) % 3;
            mc_focus_pane_ = 0;
            mc_page_ = 0;
            cursor_pos_init_ = false;
            HostPlaySfx(SFX_MOVE);
            return;
        }
        if (key == KEY_NEXT_TAB) {
            mc_tab_ = (mc_tab_ + 1) % 3;
            mc_focus_pane_ = 0;
            mc_page_ = 0;
            cursor_pos_init_ = false;
            HostPlaySfx(SFX_MOVE);
            return;
        }
        if (mc_tab_ == 2) {
            int cardCount = 0;
            bool slot1Mounted = false, slot2Mounted = false;
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                cardCount = static_cast<int>(memcards_.size());
                slot1Mounted = !slot1_card_name_.empty();
                slot2Mounted = !slot2_card_name_.empty();
            }
            if (key == KEY_SECONDARY) {
                HostPlaySfx(SFX_CONFIRM);
                HostNotifyMemoryCardAction(MC_ACTION_SWAP_SLOTS, 0, 0, 0);
                return;
            }
            if (key == KEY_OPTIONS) {
                create_card_idx_ = 0;
                overlay_ = OVERLAY_CREATE_CARD;
                HostPlaySfx(SFX_CONFIRM);
                return;
            }
            if (mc_card_pane_ == 0) {
                if (key == KEY_LEFT || key == KEY_RIGHT) {
                    mc_card_slot_idx_ = 1 - mc_card_slot_idx_;
                    HostPlaySfx(SFX_MOVE);
                } else if (key == KEY_DOWN && cardCount > 0) {
                    mc_card_pane_ = 1;
                    HostPlaySfx(SFX_MOVE);
                } else if (key == KEY_CONFIRM || key == KEY_START) {
                    bool mounted = (mc_card_slot_idx_ == 0) ? slot1Mounted : slot2Mounted;
                    if (mounted) {
                        mc_open_slot_ = mc_card_slot_idx_ + 1;
                        mc_inner_idx_ = 0;
                        mc_inner_page_ = 0;
                        cursor_pos_init_ = false;
                        HostPlaySfx(SFX_CONFIRM);
                    } else {
                        HostPlaySfx(SFX_CONFIRM);
                        HostNotifyMemoryCardAction(MC_ACTION_CYCLE_SLOT, mc_card_slot_idx_ + 1, 0, 0);
                    }
                }
            } else {
                if (key == KEY_UP) {
                    if (mc_card_list_idx_ > 0) {
                        mc_card_list_idx_--;
                        HostPlaySfx(SFX_MOVE);
                    } else {
                        mc_card_pane_ = 0;
                        HostPlaySfx(SFX_MOVE);
                    }
                } else if (key == KEY_DOWN && cardCount > 0) {
                    mc_card_list_idx_ = (mc_card_list_idx_ + 1) % cardCount;
                    HostPlaySfx(SFX_MOVE);
                } else if ((key == KEY_CONFIRM || key == KEY_START) && cardCount > 0) {
                    pending_card_idx_ = std::clamp(mc_card_list_idx_, 0, cardCount - 1);
                    card_action_menu_idx_ = 0;
                    overlay_ = OVERLAY_CARD_ACTIONS;
                    HostPlaySfx(SFX_CONFIRM);
                }
            }
            return;
        }

        const std::vector<int> filtered = FilteredSaveIndices();
        const int totalFiltered = static_cast<int>(filtered.size());
        const int cols = (screen_width_ >= 780.0f) ? 4 : 3;
        const int perPage = cols * 3;
        const int curFilteredPos = mc_page_ * perPage + mc_slot_index_;
        const bool hasSave = (curFilteredPos >= 0 && curFilteredPos < totalFiltered);

        if (mc_focus_pane_ == 0) {
            const int col = mc_slot_index_ % cols;
            if (key == KEY_LEFT) {
                if (col > 0) { mc_slot_index_--; HostPlaySfx(SFX_MOVE); }
                else if (mc_page_ > 0) { mc_page_--; mc_slot_index_ += (cols - 1); HostPlaySfx(SFX_MOVE); }
            } else if (key == KEY_RIGHT) {
                if (col < cols - 1) { mc_slot_index_++; HostPlaySfx(SFX_MOVE); }
                else { mc_focus_pane_ = 1; mc_action_idx_ = hasSave ? 0 : 2; HostPlaySfx(SFX_MOVE); }
            } else if (key == KEY_UP) {
                if (mc_slot_index_ >= cols) { mc_slot_index_ -= cols; HostPlaySfx(SFX_MOVE); }
            } else if (key == KEY_DOWN) {
                if (mc_slot_index_ + cols < perPage) { mc_slot_index_ += cols; HostPlaySfx(SFX_MOVE); }
            } else if (key == KEY_CONFIRM || key == KEY_START) {
                if (hasSave) {
                    if (key == KEY_START) StartLaunchSave(curFilteredPos);
                    else { mc_focus_pane_ = 1; mc_action_idx_ = 0; HostPlaySfx(SFX_CONFIRM); }
                } else {
                    HostPlaySfx(SFX_ERROR);
                }
            } else if (key == KEY_SECONDARY && hasSave) {
                pending_delete_save_idx_ = filtered[curFilteredPos];
                dialog_btn_ = 1;
                overlay_ = OVERLAY_DELETE_SAVE_CONFIRM;
                HostPlaySfx(SFX_CONFIRM);
            }
        } else {
            if (key == KEY_LEFT) {
                if (mc_action_idx_ > 0) { mc_action_idx_--; HostPlaySfx(SFX_MOVE); }
                else { mc_focus_pane_ = 0; HostPlaySfx(SFX_MOVE); }
            } else if (key == KEY_RIGHT) {
                if (mc_action_idx_ < 2) { mc_action_idx_++; HostPlaySfx(SFX_MOVE); }
            } else if (key == KEY_UP) {
                mc_focus_pane_ = 0;
                HostPlaySfx(SFX_MOVE);
            } else if (key == KEY_CONFIRM) {
                if (mc_action_idx_ == 0 && hasSave) StartLaunchSave(curFilteredPos);
                else if (mc_action_idx_ == 1 && hasSave) {
                    pending_delete_save_idx_ = filtered[curFilteredPos];
                    dialog_btn_ = 1;
                    overlay_ = OVERLAY_DELETE_SAVE_CONFIRM;
                    HostPlaySfx(SFX_CONFIRM);
                } else if (mc_action_idx_ == 2) {
                    SwitchScreen(return_screen_);
                    HostPlaySfx(SFX_CANCEL);
                } else {
                    HostPlaySfx(SFX_ERROR);
                }
            }
        }
        return;
    }

    if (screen_ == SCREEN_SETTINGS) {
        if (key == KEY_BACK) {
            SwitchScreen(SCREEN_MAIN_MENU);
            HostPlaySfx(SFX_CANCEL);
            return;
        }
        auto ensureSettingVisible = [&]() {
            const float rowStep = 48.0f;
            const float viewH = ComputeMetrics().bodyH - 40.0f;
            const float top = static_cast<float>(settings_index_) * rowStep;
            const float bot = top + 44.0f;
            if (top < settings_scroll_target_y_) settings_scroll_target_y_ = top;
            else if (bot > settings_scroll_target_y_ + viewH) settings_scroll_target_y_ = bot - viewH;
        };
        if (key == KEY_UP) {
            settings_index_ = (settings_index_ - 1 + kSettingsCount) % kSettingsCount;
            ensureSettingVisible();
            HostPlaySfx(SFX_MOVE);
        } else if (key == KEY_DOWN) {
            settings_index_ = (settings_index_ + 1) % kSettingsCount;
            ensureSettingVisible();
            HostPlaySfx(SFX_MOVE);
        } else if (key == KEY_LEFT) {
            CycleSetting(settings_index_, -1);
        } else if (key == KEY_RIGHT || key == KEY_CONFIRM) {
            CycleSetting(settings_index_, 1);
        }
    }
}

void Ps2Renderer::OnTouchDown(float normX, float normY) {
    float vx = 0.0f, vy = 0.0f;
    SurfaceNormToVirtual(normX, normY, vx, vy);
    touch_down_vx_ = vx;
    touch_down_vy_ = vy;
    touch_last_vy_ = vy;
    touch_moved_ = false;
    touch_dragging_ = false;
    touch_drag_target_ = 0;

    if (overlay_ == OVERLAY_NONE) {
        if (screen_ == SCREEN_GAMES && vx < screen_width_ * 0.52f) {
            touch_drag_target_ = 1;
        } else if (screen_ == SCREEN_SETTINGS && vx > screen_width_ * 0.34f) {
            touch_drag_target_ = 2;
        } else if (screen_ == SCREEN_BROWSER && mc_tab_ == 2 && mc_open_slot_ == 0 && vy > 275.0f) {
            touch_drag_target_ = 3;
        }
    }
}

void Ps2Renderer::OnTouchMove(float normX, float normY) {
    float vx = 0.0f, vy = 0.0f;
    SurfaceNormToVirtual(normX, normY, vx, vy);
    const float dyTotal = vy - touch_down_vy_;
    const float dxTotal = vx - touch_down_vx_;
    if (!touch_moved_ && (std::fabs(dyTotal) > 10.0f || std::fabs(dxTotal) > 10.0f)) {
        touch_moved_ = true;
    }
    if (touch_drag_target_ != 0 && std::fabs(dyTotal) > 10.0f) {
        touch_dragging_ = true;
        const float delta = touch_last_vy_ - vy;
        if (touch_drag_target_ == 1) {
            game_scroll_target_y_ += delta;
            game_scroll_y_ = game_scroll_target_y_;
        } else if (touch_drag_target_ == 2) {
            settings_scroll_target_y_ += delta;
            settings_scroll_y_ = settings_scroll_target_y_;
        } else if (touch_drag_target_ == 3) {
            mc_card_scroll_target_y_ += delta;
            mc_card_scroll_y_ = mc_card_scroll_target_y_;
        }
    }
    touch_last_vy_ = vy;
}

void Ps2Renderer::OnTouchUp(float normX, float normY) {
    const bool wasDragging = touch_moved_ || touch_dragging_;
    touch_dragging_ = false;
    touch_moved_ = false;
    touch_drag_target_ = 0;
    if (wasDragging || launching_) return;

    if (boot_active_) {
        boot_active_ = false;
        screen_fade_t_ = 0.55f;
        return;
    }

    float vx = 0.0f, vy = 0.0f;
    if (!SurfaceNormToVirtual(normX, normY, vx, vy)) return;
    const HitTarget* hit = FindHit(vx, vy);
    if (!hit) return;

    switch (hit->kind) {
        case HIT_OVERLAY_BACKDROP:
            overlay_ = OVERLAY_NONE;
            HostPlaySfx(SFX_CANCEL);
            break;

        case HIT_OVERLAY_ITEM:
            if (overlay_ == OVERLAY_EXIT_CONFIRM ||
                overlay_ == OVERLAY_DELETE_SAVE_CONFIRM ||
                overlay_ == OVERLAY_DELETE_CARD_CONFIRM ||
                overlay_ == OVERLAY_RESTORE_CONFIRM) {
                dialog_btn_ = hit->index;
                OnKey(KEY_CONFIRM);
            } else if (overlay_ == OVERLAY_GAME_OPTIONS) {
                options_index_ = hit->index;
                OnKey(KEY_CONFIRM);
            } else if (overlay_ == OVERLAY_CREATE_CARD) {
                create_card_idx_ = hit->index;
                OnKey(KEY_CONFIRM);
            } else if (overlay_ == OVERLAY_CARD_ACTIONS) {
                card_action_menu_idx_ = hit->index;
                OnKey(KEY_CONFIRM);
            } else {
                overlay_ = OVERLAY_NONE;
                HostPlaySfx(SFX_CANCEL);
            }
            break;

        case HIT_BOTTOM_ACTION:
            OnKey(hit->index);
            break;

        case HIT_MAIN_ROW:
            main_index_ = hit->index;
            OnKey(KEY_CONFIRM);
            break;

        case HIT_GAME_TAB:
            if (game_tab_ != hit->index) {
                const std::vector<int> tabs = VisibleGameTabs();
                if (std::find(tabs.begin(), tabs.end(), hit->index) != tabs.end()) {
                    SelectGameTab(hit->index);
                    HostPlaySfx(SFX_MOVE);
                }
            }
            break;

        case HIT_GAME_ROW:
            if (game_index_ == hit->index) {
                ActivateGameLaunch(SelectedMasterGameIndex());
            } else {
                game_index_ = hit->index;
                HostPlaySfx(SFX_MOVE);
            }
            break;

        case HIT_GAME_START:
            ActivateGameLaunch(hit->index);
            break;

        case HIT_GAME_CONTINUE: {
            GameEntry g;
            bool ok = false;
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                if (hit->index >= 0 && hit->index < static_cast<int>(games_.size())) {
                    g = games_[hit->index];
                    ok = true;
                }
            }
            if (ok && g.latestSaveSlot >= 0) {
                const bool isBlueCd = (g.fileSizeBytes > 0 && g.fileSizeBytes < 750LL * 1024LL * 1024LL);
                StartLaunchQuickSave(g.path, g.latestSaveSlot, g.title, g.serial, g.coverPath, isBlueCd);
            } else {
                HostPlaySfx(SFX_ERROR);
            }
            break;
        }

        case HIT_GAME_OPTIONS:
            options_index_ = 0;
            overlay_ = OVERLAY_GAME_OPTIONS;
            HostPlaySfx(SFX_CONFIRM);
            break;

        case HIT_MC_TAB:
            mc_tab_ = hit->index;
            mc_open_slot_ = 0;
            mc_focus_pane_ = 0;
            mc_page_ = 0;
            cursor_pos_init_ = false;
            HostPlaySfx(SFX_MOVE);
            break;

        case HIT_MC_OPEN_SLOT: {
            mc_card_pane_ = 0;
            bool mounted = false;
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                mounted = (hit->index == 0) ? !slot1_card_name_.empty() : !slot2_card_name_.empty();
            }
            if (mc_card_slot_idx_ == hit->index) {
                if (mounted) {
                    mc_open_slot_ = hit->index + 1;
                    mc_inner_idx_ = 0;
                    mc_inner_page_ = 0;
                    cursor_pos_init_ = false;
                    HostPlaySfx(SFX_CONFIRM);
                } else {
                    HostPlaySfx(SFX_CONFIRM);
                    HostNotifyMemoryCardAction(MC_ACTION_CYCLE_SLOT, mc_card_slot_idx_ + 1, 0, 0);
                }
            } else {
                mc_card_slot_idx_ = hit->index;
                HostPlaySfx(SFX_MOVE);
            }
            break;
        }

        case HIT_MC_INNER_ICON:
            mc_inner_idx_ = hit->index;
            HostPlaySfx(SFX_MOVE);
            break;

        case HIT_MC_INNER_BACK:
            mc_open_slot_ = 0;
            cursor_pos_init_ = false;
            HostPlaySfx(SFX_CANCEL);
            break;

        case HIT_MC_INNER_PREV:
            if (mc_inner_page_ > 0) {
                mc_inner_page_--;
                const int cols = (screen_width_ >= 800.0f) ? 5 : 4;
                mc_inner_idx_ = mc_inner_page_ * cols * 3;
                HostPlaySfx(SFX_MOVE);
            }
            break;

        case HIT_MC_INNER_NEXT: {
            int nSaves = 0;
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                nSaves = static_cast<int>((mc_open_slot_ == 1 ? slot1_contents_ : slot2_contents_).saves.size());
            }
            const int cols = (screen_width_ >= 800.0f) ? 5 : 4;
            const int totalPages = std::max(1, (nSaves + cols * 3 - 1) / (cols * 3));
            if (mc_inner_page_ + 1 < totalPages) {
                mc_inner_page_++;
                mc_inner_idx_ = mc_inner_page_ * cols * 3;
                HostPlaySfx(SFX_MOVE);
            }
            break;
        }

        case HIT_MC_BLOCK:
            mc_focus_pane_ = 0;
            if (mc_slot_index_ == hit->index) {
                const std::vector<int> filtered = FilteredSaveIndices();
                const int cols = (screen_width_ >= 780.0f) ? 4 : 3;
                const int fPos = mc_page_ * (cols * 3) + mc_slot_index_;
                if (fPos >= 0 && fPos < static_cast<int>(filtered.size())) {
                    StartLaunchSave(fPos);
                } else {
                    HostPlaySfx(SFX_ERROR);
                }
            } else {
                mc_slot_index_ = hit->index;
                HostPlaySfx(SFX_MOVE);
            }
            break;

        case HIT_MC_ACTION:
            mc_focus_pane_ = 1;
            mc_action_idx_ = hit->index;
            OnKey(KEY_CONFIRM);
            break;

        case HIT_MC_PAGE_PREV:
            if (mc_page_ > 0) { mc_page_--; HostPlaySfx(SFX_MOVE); }
            break;

        case HIT_MC_PAGE_NEXT: {
            const int totalFiltered = static_cast<int>(FilteredSaveIndices().size());
            const int cols = (screen_width_ >= 780.0f) ? 4 : 3;
            const int totalPages = std::max(1, (totalFiltered + (cols * 3) - 1) / (cols * 3));
            if (mc_page_ + 1 < totalPages) { mc_page_++; HostPlaySfx(SFX_MOVE); }
            break;
        }

        case HIT_CARD_TOOL:
            if (hit->index == 0) {
                HostPlaySfx(SFX_CONFIRM);
                HostNotifyMemoryCardAction(MC_ACTION_CYCLE_SLOT, mc_card_slot_idx_ + 1, 0, 0);
            } else if (hit->index == 1) {
                HostPlaySfx(SFX_CONFIRM);
                HostNotifyMemoryCardAction(MC_ACTION_SWAP_SLOTS, 0, 0, 0);
            } else if (hit->index == 2) {
                HostPlaySfx(SFX_CONFIRM);
                HostNotifyMemoryCardAction(MC_ACTION_EJECT_SLOT, mc_card_slot_idx_ + 1, 0, 0);
            } else if (hit->index == 3) {
                create_card_idx_ = 0;
                overlay_ = OVERLAY_CREATE_CARD;
                HostPlaySfx(SFX_CONFIRM);
            }
            break;

        case HIT_CARD_ROW:
            mc_card_pane_ = 1;
            if (mc_card_list_idx_ == hit->index) {
                pending_card_idx_ = hit->index;
                card_action_menu_idx_ = 0;
                overlay_ = OVERLAY_CARD_ACTIONS;
                HostPlaySfx(SFX_CONFIRM);
            } else {
                mc_card_list_idx_ = hit->index;
                HostPlaySfx(SFX_MOVE);
            }
            break;

        case HIT_SETTING_LEFT:
            settings_index_ = hit->index;
            CycleSetting(hit->index, -1);
            break;

        case HIT_SETTING_RIGHT:
        case HIT_SETTING_ROW:
            settings_index_ = hit->index;
            CycleSetting(hit->index, 1);
            break;

        default:
            break;
    }
}

void Ps2Renderer::OnTouchCancel() {
    touch_dragging_ = false;
    touch_moved_ = false;
    touch_drag_target_ = 0;
}

void Ps2Renderer::OnTouchTwoFingerBack() {
    OnKey(KEY_BACK);
}

void Ps2Renderer::OnTouchLongPress(float normX, float normY) {
    if (boot_active_ || launching_ || overlay_ != OVERLAY_NONE) return;
    float vx = 0.0f, vy = 0.0f;
    if (!SurfaceNormToVirtual(normX, normY, vx, vy)) return;
    const HitTarget* hit = FindHit(vx, vy);
    if (!hit) return;

    if (hit->kind == HIT_GAME_ROW) {
        game_index_ = hit->index;
        options_index_ = 0;
        overlay_ = OVERLAY_GAME_OPTIONS;
        HostPlaySfx(SFX_CONFIRM);
    } else if (hit->kind == HIT_MC_BLOCK) {
        const std::vector<int> filtered = FilteredSaveIndices();
        const int cols = (screen_width_ >= 780.0f) ? 4 : 3;
        const int fPos = mc_page_ * (cols * 3) + hit->index;
        if (fPos >= 0 && fPos < static_cast<int>(filtered.size())) {
            mc_slot_index_ = hit->index;
            pending_delete_save_idx_ = filtered[fPos];
            dialog_btn_ = 1;
            overlay_ = OVERLAY_DELETE_SAVE_CONFIRM;
            HostPlaySfx(SFX_CONFIRM);
        }
    } else if (hit->kind == HIT_CARD_ROW) {
        mc_card_pane_ = 1;
        mc_card_list_idx_ = hit->index;
        pending_card_idx_ = hit->index;
        card_action_menu_idx_ = 0;
        overlay_ = OVERLAY_CARD_ACTIONS;
        HostPlaySfx(SFX_CONFIRM);
    } else if (hit->kind == HIT_MC_OPEN_SLOT) {
        HostPlaySfx(SFX_CONFIRM);
        HostNotifyMemoryCardAction(MC_ACTION_CYCLE_SLOT, hit->index + 1, 0, 0);
    }
}

void Ps2Renderer::SetStrings(std::unordered_map<std::string, std::string> strings) {
    std::lock_guard<std::mutex> lock(strings_mutex_);
    strings_ = std::move(strings);
}

std::string Ps2Renderer::TextOr(const std::string& key, const char* fallback) const {
    std::lock_guard<std::mutex> lock(strings_mutex_);
    auto it = strings_.find(key);
    if (it != strings_.end() && !it->second.empty()) {
        return it->second;
    }
    return fallback ? std::string(fallback) : std::string();
}

void Ps2Renderer::SetGames(std::vector<GameEntry> games) {
    std::string targetPath = pending_restore_game_path_;
    if (targetPath.empty()) {
        const int curMaster = SelectedMasterGameIndex();
        if (curMaster >= 0) {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (curMaster < static_cast<int>(games_.size())) {
                targetPath = games_[curMaster].path;
            }
        }
        if (targetPath.empty()) {
            targetPath = selected_game_path_;
        }
    }

    bool hasArcade = false;
    bool hasPs2 = false;
    for (auto& g : games) {
        g.isArcade = IsArcadeEntry(g);
        if (g.isArcade) hasArcade = true;
        else hasPs2 = true;
    }
    int matchedMasterIdx = -1;
    bool matchedIsArcade = false;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        for (const auto& g : games) {
            if (!g.coverPath.empty()) dirty_image_paths_.insert(g.coverPath);
        }
        has_arcade_games_ = hasArcade;
        has_ps2_games_ = hasPs2;
        games_ = std::move(games);
        const bool hadRecent = !recent_indices_.empty();
        RebuildRecentIndicesLocked();
        // Recent tab just became available (library loaded after the recent list): open it.
        if (!hadRecent && !recent_indices_.empty()) game_tab_ = GAME_TAB_RECENT;
        NormalizeGameTabLocked();
        if (!dirty_image_paths_.empty()) image_cache_dirty_ = true;

        if (!targetPath.empty()) {
            for (int i = 0; i < static_cast<int>(games_.size()); ++i) {
                if (games_[i].path == targetPath) {
                    matchedMasterIdx = i;
                    matchedIsArcade = games_[i].isArcade;
                    break;
                }
            }
        }
    }

    if (matchedMasterIdx >= 0) {
        // Keep the current tab when it still lists the game (a library refresh must not jump
        // tabs); otherwise move to the tab that owns it.
        if (!LocateGameInCurrentTab(targetPath)) {
            game_tab_ = (hasArcade && matchedIsArcade) ? GAME_TAB_ARCADE : GAME_TAB_PS2;
            LocateGameInCurrentTab(targetPath);
        }
        selected_game_path_ = targetPath;
        pending_restore_game_path_.clear();
        pending_restore_game_index_ = -1;
        SnapGameScrollToSelected();
        ClampSelectionIndices();
    } else if (pending_restore_game_index_ >= 0 && !FilteredGameIndices().empty()) {
        game_index_ = pending_restore_game_index_;
        pending_restore_game_index_ = -1;
        ClampSelectionIndices();
        SnapGameScrollToSelected();
    } else {
        ClampSelectionIndices();
    }
    RebuildTowersLayout();
}

void Ps2Renderer::SetSaves(std::vector<SaveEntry> saves) {
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        for (const auto& s : saves) {
            if (!s.previewPath.empty()) dirty_image_paths_.insert(s.previewPath);
        }
        saves_ = std::move(saves);
        if (!dirty_image_paths_.empty()) image_cache_dirty_ = true;
    }
    RebuildTowersLayout();
}

void Ps2Renderer::SetMemoryCards(
    std::string slot1Name,
    std::string slot1Meta,
    std::string slot1Status,
    std::string slot2Name,
    std::string slot2Meta,
    std::string slot2Status,
    std::vector<MemoryCardEntry> cards) {
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        slot1_card_name_ = std::move(slot1Name);
        slot1_card_meta_ = std::move(slot1Meta);
        slot1_card_status_ = std::move(slot1Status);
        slot2_card_name_ = std::move(slot2Name);
        slot2_card_meta_ = std::move(slot2Meta);
        slot2_card_status_ = std::move(slot2Status);
        memcards_ = std::move(cards);
    }
    ClampSelectionIndices();
}

void Ps2Renderer::SetMemoryCardContents(
    int slot,
    std::string cardName,
    int freeKb,
    int totalKb,
    std::vector<McSaveBlockEntry> saves) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    McSlotContents& dst = (slot == 2) ? slot2_contents_ : slot1_contents_;
    dst.cardName = std::move(cardName);
    dst.freeKb = freeKb;
    dst.totalKb = totalKb;
    dst.saves = std::move(saves);
}

void Ps2Renderer::SetSettings(const ShellSettings& settings) {
    settings_ = settings;
    settings_.theme = std::clamp(settings_.theme, 0, kThemeCount - 1);
}

void Ps2Renderer::SetSystemStatus(const SystemStatus& status) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    status_ = status;
}

void Ps2Renderer::SetTouchMode(bool active, bool showTutorial) {
    touch_mode_active_ = active;
    if (showTutorial && overlay_ == OVERLAY_NONE && !boot_active_) {
        overlay_ = OVERLAY_TOUCH_GUIDE;
    }
}

void Ps2Renderer::SetGamesLoading(bool loading) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    games_loading_ = loading;
}

void Ps2Renderer::SetSavesLoading(bool loading) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    saves_loading_ = loading;
}

void Ps2Renderer::TriggerTransition(int kind) {
    launching_ = false;
    launch_dispatched_ = false;
    launch_t_ = 0.0f;
    overlay_ = OVERLAY_NONE;
    if (kind == 1 || settings_.bootAnimMode == 2) {
        boot_active_ = false;
        screen_fade_t_ = 0.55f;
    } else {
        boot_active_ = true;
        boot_t_ = 0.0f;
        boot_prev_valid_ = false;
        boot_sfx_played_ = false;
        boot_duration_ = (settings_.bootAnimMode == 1) ? 2.8f : 5.5f;
    }
}

std::vector<int> Ps2Renderer::GetState() const {
    return {screen_, main_index_, game_index_, mc_slot_index_};
}

void Ps2Renderer::SetState(int screen, int mainIndex, int gameIndex, int saveIndex) {
    screen_ = std::clamp(screen, 0, 3);
    main_index_ = std::clamp(mainIndex, 0, 2);
    pending_restore_game_index_ = std::max(0, gameIndex);
    game_index_ = pending_restore_game_index_;
    mc_slot_index_ = std::max(0, saveIndex);
    ClampSelectionIndices();
    if (!FilteredGameIndices().empty()) {
        pending_restore_game_index_ = -1;
        SnapGameScrollToSelected();
    }
}

void Ps2Renderer::SelectGameByPath(const std::string& path) {
    if (path.empty()) return;
    pending_restore_game_path_ = path;
    selected_game_path_ = path;
    int masterIdx = -1;
    bool isArcade = false;
    bool hasArcade = false;
    bool inRecent = false;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        hasArcade = has_arcade_games_;
        for (int i = 0; i < static_cast<int>(games_.size()); ++i) {
            if (games_[i].path == path) {
                masterIdx = i;
                isArcade = games_[i].isArcade;
                break;
            }
        }
        inRecent = masterIdx >= 0 &&
            std::find(recent_indices_.begin(), recent_indices_.end(), masterIdx) != recent_indices_.end();
    }
    if (masterIdx >= 0) {
        // The games screen opens on Recent whenever it lists the game (e.g. back from a game).
        if (inRecent) {
            game_tab_ = GAME_TAB_RECENT;
        } else {
            game_tab_ = (hasArcade && isArcade) ? GAME_TAB_ARCADE : GAME_TAB_PS2;
        }
        LocateGameInCurrentTab(path);
        pending_restore_game_path_.clear();
        pending_restore_game_index_ = -1;
        last_disc_game_idx_ = -1;
        SnapGameScrollToSelected();
    }
}

void Ps2Renderer::SetRecentGames(std::vector<std::string> paths) {
    std::string selectedPath;
    {
        const int curMaster = SelectedMasterGameIndex();
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (curMaster >= 0 && curMaster < static_cast<int>(games_.size())) {
            selectedPath = games_[curMaster].path;
        }
    }
    if (selectedPath.empty()) selectedPath = selected_game_path_;

    bool hadRecent = false;
    bool hasRecent = false;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (paths == recent_paths_) return;
        hadRecent = !recent_indices_.empty();
        recent_paths_ = std::move(paths);
        RebuildRecentIndicesLocked();
        hasRecent = !recent_indices_.empty();
        // The tab appears with the first played game: open it right away.
        if (!hadRecent && hasRecent) game_tab_ = GAME_TAB_RECENT;
        NormalizeGameTabLocked();
    }
    if (!LocateGameInCurrentTab(selectedPath)) {
        game_index_ = 0;
    }
    last_disc_game_idx_ = -1;
    ClampSelectionIndices();
    SnapGameScrollToSelected();
}

} // namespace ps2ui
