#include "ps2_renderer.h"
#include "ps2_shaders.h"

#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#define LOG_TAG "Ps2Renderer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace ps2ui {

namespace {

constexpr size_t kMaxTextCache = 192;
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

float EaseInOutSine(float t) {
    t = Clamp01(t);
    return 0.5f * (1.0f - std::cos(kPi * t));
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

// Column-major 4x4 matrix multiplication out = a * b
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

    // Pre-allocate 3D glass towers buffer (up to 48 towers * 30 verts = 1440 verts)
    glGenVertexArrays(1, &tower_vao_);
    glGenBuffers(1, &tower_vbo_);
    glBindVertexArray(tower_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, tower_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 2048 * sizeof(TowerVertex), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TowerVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TowerVertex), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(TowerVertex), reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(TowerVertex), reinterpret_cast<void*>(7 * sizeof(float)));
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
    tower_verts_.reserve(1500);
    hit_targets_.reserve(64);
    RebuildTowersLayout();

    has_frame_tp_ = false;
    gl_ready_ = true;
    LOGI("PS2 native UI GLES3 renderer initialized");
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

    auto delProg = [](GLuint& p) {
        if (p) {
            glDeleteProgram(p);
            p = 0;
        }
    };
    delProg(prog_quad_);
    delProg(prog_bg_);
    delProg(prog_tower_);
    delProg(prog_panel_);
    delProg(prog_disc_);
    delProg(prog_card_);
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
            char logBuf[1024] = {};
            glGetShaderInfoLog(s, sizeof(logBuf), nullptr, logBuf);
            LOGE("Shader compile error: %s", logBuf);
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
        char logBuf[1024] = {};
        glGetProgramInfoLog(prog, sizeof(logBuf), nullptr, logBuf);
        LOGE("Program link error: %s", logBuf);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

bool Ps2Renderer::CompileShaders() {
    prog_quad_ = CompileProgram(kQuadVertexShader, kQuadFragmentShader);
    prog_bg_ = CompileProgram(kBgVertexShader, kBgFragmentShader);
    prog_tower_ = CompileProgram(kTowerVertexShader, kTowerFragmentShader);
    prog_panel_ = CompileProgram(kPanelVertexShader, kPanelFragmentShader);
    prog_disc_ = CompileProgram(kDiscVertexShader, kDiscFragmentShader);
    prog_card_ = CompileProgram(kCardVertexShader, kCardFragmentShader);
    prog_scan_ = CompileProgram(kBgVertexShader, kScanlineFragmentShader);

    if (!prog_quad_ || !prog_bg_ || !prog_tower_ || !prog_panel_ ||
        !prog_disc_ || !prog_card_ || !prog_scan_) {
        return false;
    }

    quad_u_screen_ = glGetUniformLocation(prog_quad_, "u_screen");
    quad_u_texture_ = glGetUniformLocation(prog_quad_, "u_texture");

    bg_u_screen_ = glGetUniformLocation(prog_bg_, "u_screen");
    bg_u_orb_center_ = glGetUniformLocation(prog_bg_, "u_orb_center");
    bg_u_time_ = glGetUniformLocation(prog_bg_, "u_time");
    bg_u_core_boost_ = glGetUniformLocation(prog_bg_, "u_core_boost");
    bg_u_theme_ = glGetUniformLocation(prog_bg_, "u_theme");
    bg_u_orbs_ = glGetUniformLocation(prog_bg_, "u_orbs");
    bg_u_trails_ = glGetUniformLocation(prog_bg_, "u_trails");

    twr_u_mvp_ = glGetUniformLocation(prog_tower_, "u_mvp");
    twr_u_cam_pos_ = glGetUniformLocation(prog_tower_, "u_cam_pos");
    twr_u_accent_col_ = glGetUniformLocation(prog_tower_, "u_accent_col");
    twr_u_alpha_ = glGetUniformLocation(prog_tower_, "u_alpha");

    pan_u_screen_ = glGetUniformLocation(prog_panel_, "u_screen");
    pan_u_rect_ = glGetUniformLocation(prog_panel_, "u_rect");
    pan_u_size_ = glGetUniformLocation(prog_panel_, "u_size");
    pan_u_radius_ = glGetUniformLocation(prog_panel_, "u_radius");
    pan_u_border_px_ = glGetUniformLocation(prog_panel_, "u_border_px");
    pan_u_top_col_ = glGetUniformLocation(prog_panel_, "u_top_col");
    pan_u_bot_col_ = glGetUniformLocation(prog_panel_, "u_bot_col");
    pan_u_border_col_ = glGetUniformLocation(prog_panel_, "u_border_col");
    pan_u_glow_col_ = glGetUniformLocation(prog_panel_, "u_glow_col");

    disc_u_screen_ = glGetUniformLocation(prog_disc_, "u_screen");
    disc_u_rect_ = glGetUniformLocation(prog_disc_, "u_rect");
    disc_u_angle_ = glGetUniformLocation(prog_disc_, "u_angle");
    disc_u_alpha_ = glGetUniformLocation(prog_disc_, "u_alpha");
    disc_u_disc_type_ = glGetUniformLocation(prog_disc_, "u_disc_type");

    card_u_screen_ = glGetUniformLocation(prog_card_, "u_screen");
    card_u_rect_ = glGetUniformLocation(prog_card_, "u_rect");
    card_u_size_ = glGetUniformLocation(prog_card_, "u_size");
    card_u_body_col_ = glGetUniformLocation(prog_card_, "u_body_col");
    card_u_accent_col_ = glGetUniformLocation(prog_card_, "u_accent_col");
    card_u_is_empty_ = glGetUniformLocation(prog_card_, "u_is_empty");
    card_u_is_folder_ = glGetUniformLocation(prog_card_, "u_is_folder");
    card_u_time_ = glGetUniformLocation(prog_card_, "u_time");

    scan_u_viewport_ = glGetUniformLocation(prog_scan_, "u_viewport_px");
    scan_u_strength_ = glGetUniformLocation(prog_scan_, "u_strength");

    return true;
}

void Ps2Renderer::Resize(int width, int height) {
    surface_width_ = std::max(1, width);
    surface_height_ = std::max(1, height);
}

void Ps2Renderer::ClearTextCache() {
    for (auto& kv : text_cache_) {
        if (kv.second.tex) glDeleteTextures(1, &kv.second.tex);
    }
    text_cache_.clear();
    text_lru_.clear();
}

void Ps2Renderer::ClearImageCache() {
    for (auto& kv : image_cache_) {
        if (kv.second.tex) glDeleteTextures(1, &kv.second.tex);
    }
    image_cache_.clear();
    image_lru_.clear();
}

void Ps2Renderer::EvictTextCacheIfNeeded() {
    while (text_cache_.size() >= kMaxTextCache && !text_lru_.empty()) {
        const std::string oldest = text_lru_.back();
        text_lru_.pop_back();
        auto it = text_cache_.find(oldest);
        if (it != text_cache_.end()) {
            if (it->second.tex) glDeleteTextures(1, &it->second.tex);
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

    int texW = 0;
    int texH = 0;
    float logW = 0.0f;
    float logH = 0.0f;
    std::vector<uint8_t> rgba;
    if (!HostRasterizeText(
            text, fontSize, style, maxWidth, maxLines, effAlign, raster_scale_,
            texW, texH, logW, logH, rgba)) {
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
    TextCacheEntry entry{tex, logW, logH, text_lru_.begin()};
    auto [insIt, _] = text_cache_.emplace(key, entry);
    return insIt->second;
}

const Ps2Renderer::ImageCacheEntry& Ps2Renderer::GetOrCreateImage(const std::string& path, int maxDim) {
    static const ImageCacheEntry kEmpty{};
    if (path.empty()) return kEmpty;

    const int effMaxDim = std::clamp(maxDim, 64, 320);
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

void Ps2Renderer::DrawOpticalDisc(
    float cx, float cy, float radius, float angleRad, int discType, float alpha) {
    if (alpha <= 0.005f || radius <= 1.0f) return;
    FlushQuadBatch();

    glUseProgram(prog_disc_);
    glUniform2f(disc_u_screen_, screen_width_, kScreenHeight);
    glUniform4f(disc_u_rect_, cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
    glUniform1f(disc_u_angle_, angleRad);
    glUniform1f(disc_u_alpha_, alpha);
    glUniform1i(disc_u_disc_type_, discType);

    glBindVertexArray(fs_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Ps2Renderer::DrawMemoryCard3D(
    float x, float y, float w, float h,
    const RgbaColor& bodyCol,
    const RgbaColor& accentCol,
    bool focused,
    bool isEmpty,
    bool isFolder) {
    if (w <= 2.0f || h <= 2.0f || bodyCol.a <= 0.005f) return;
    FlushQuadBatch();

    glUseProgram(prog_card_);
    glUniform2f(card_u_screen_, screen_width_, kScreenHeight);
    glUniform4f(card_u_rect_, x, y, w, h);
    glUniform2f(card_u_size_, w, h);
    glUniform4f(card_u_body_col_, bodyCol.r, bodyCol.g, bodyCol.b, bodyCol.a);
    glUniform4f(card_u_accent_col_, accentCol.r, accentCol.g, accentCol.b, focused ? 1.0f : 0.25f);
    glUniform1f(card_u_is_empty_, isEmpty ? 1.0f : 0.0f);
    glUniform1f(card_u_is_folder_, isFolder ? 1.0f : 0.0f);
    glUniform1f(card_u_time_, time_sec_);

    glBindVertexArray(fs_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Ps2Renderer::DrawDiamondCursor(float cx, float cy, float size, float alpha) {
    if (alpha <= 0.01f) return;
    float pulse = 0.88f + 0.12f * std::sin(time_sec_ * 4.5f);
    RgbaColor acc = AccentColor(alpha * pulse);
    RgbaColor core = {0.92f, 0.97f, 1.0f, alpha};
    const float r = size;
    // Sleek glowing PS2 horizontal arrow/diamond pointer
    PushTriangle(cx - r * 0.45f, cy - r * 0.48f, acc,
                 cx + r * 0.72f, cy, core,
                 cx - r * 0.45f, cy + r * 0.48f, acc);
    PushTriangle(cx - r * 0.85f, cy, {acc.r, acc.g, acc.b, alpha * 0.35f},
                 cx - r * 0.45f, cy - r * 0.48f, acc,
                 cx - r * 0.45f, cy + r * 0.48f, acc);
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
    // Dynamically vary tower density subtly with game + save library size, like real PS2 BIOS!
    const int libWeight = static_cast<int>(games_.size()) * 2 + static_cast<int>(saves_.size());
    const int count = std::clamp(22 + (libWeight % 18), 22, 40);

    // Deterministic pseudo-random layout creating an atmospheric PS2 glass monolith skyline
    uint32_t seed = 0x50533242u; // "PS2B"
    auto nextRand = [&seed]() -> float {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };

    for (int i = 0; i < count; ++i) {
        TowerDef t;
        // Arrange in symmetrical/organic clusters flanking a subtle central corridor
        float side = (i % 2 == 0) ? -1.0f : 1.0f;
        float lane = 0.9f + nextRand() * 7.8f;
        t.x = side * lane + (nextRand() - 0.5f) * 1.1f;
        t.z = -3.0f - (static_cast<float>(i) / static_cast<float>(count)) * 22.0f + (nextRand() - 0.5f) * 1.4f;
        t.width = 0.48f + nextRand() * 0.72f;
        t.depth = 0.48f + nextRand() * 0.72f;
        float extraH = (i < std::min(16, libWeight + 4)) ? (1.2f + nextRand() * 2.2f) : (0.5f + nextRand() * 1.4f);
        t.height = 1.6f + extraH;
        t.phase = nextRand() * 6.283f;
        t.brightness = 0.55f + 0.45f * nextRand();
        towers_.push_back(t);
    }
}

void Ps2Renderer::DrawTowers3D(
    const float* mvp,
    float camX, float camY, float camZ,
    float riseScale,
    float alpha) {
    if (alpha <= 0.005f || towers_.empty()) return;
    FlushQuadBatch();

    tower_verts_.clear();
    const RgbaColor baseCol = SecondaryAccentColor(1.0f);

    for (const auto& t : towers_) {
        const float sway = 1.0f + 0.04f * std::sin(time_sec_ * 0.85f + t.phase);
        const float h = std::max(0.25f, t.height * riseScale * sway);
        const float x0 = t.x - t.width * 0.5f;
        const float x1 = t.x + t.width * 0.5f;
        const float z0 = t.z - t.depth * 0.5f;
        const float z1 = t.z + t.depth * 0.5f;
        const float y0 = -1.6f;
        const float y1 = y0 + h;

        const float b = t.brightness;
        const float cr = baseCol.r * b;
        const float cg = baseCol.g * b;
        const float cb = baseCol.b * b;
        const float ca = 0.92f;

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

        // Front (+Z), Back (-Z), Left (-X), Right (+X), Top (+Y)
        pushFace(x0, y0, z1, 0.0f, x1, y0, z1, 0.0f, x1, y1, z1, 1.0f, x0, y1, z1, 1.0f, 0.0f, 0.0f, 1.0f);
        pushFace(x1, y0, z0, 0.0f, x0, y0, z0, 0.0f, x0, y1, z0, 1.0f, x1, y1, z0, 1.0f, 0.0f, 0.0f, -1.0f);
        pushFace(x0, y0, z0, 0.0f, x0, y0, z1, 0.0f, x0, y1, z1, 1.0f, x0, y1, z0, 1.0f, -1.0f, 0.0f, 0.0f);
        pushFace(x1, y0, z1, 0.0f, x1, y0, z0, 0.0f, x1, y1, z0, 1.0f, x1, y1, z1, 1.0f, 1.0f, 0.0f, 0.0f);
        pushFace(x0, y1, z1, 1.0f, x1, y1, z1, 1.0f, x1, y1, z0, 1.0f, x0, y1, z0, 1.0f, 0.0f, 1.0f, 0.0f);
    }

    if (tower_verts_.empty()) return;

    const RgbaColor acc = AccentColor(1.0f);
    glUseProgram(prog_tower_);
    glUniformMatrix4fv(twr_u_mvp_, 1, GL_FALSE, mvp);
    glUniform3f(twr_u_cam_pos_, camX, camY, camZ);
    glUniform3f(twr_u_accent_col_, acc.r, acc.g, acc.b);
    glUniform1f(twr_u_alpha_, alpha);

    glBindVertexArray(tower_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, tower_vbo_);
    const size_t maxBytes = 2048 * sizeof(TowerVertex);
    const size_t sendBytes = std::min(maxBytes, tower_verts_.size() * sizeof(TowerVertex));
    const GLsizei drawVerts = static_cast<GLsizei>(sendBytes / sizeof(TowerVertex));
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(sendBytes), tower_verts_.data());
    glDrawArrays(GL_TRIANGLES, 0, drawVerts);
    glBindVertexArray(0);
}

// Theme Palette Helpers (6 Distinct PS2 BIOS Themes)
RgbaColor Ps2Renderer::AccentColor(float alpha) const {
    switch (settings_.theme) {
        case 1: return {0.72f, 0.88f, 1.00f, alpha}; // Midnight Obsidian
        case 2: return {0.24f, 0.96f, 0.64f, alpha}; // Emerald Matrix
        case 3: return {1.00f, 0.40f, 0.62f, alpha}; // Crimson Sakura
        case 4: return {0.76f, 0.48f, 1.00f, alpha}; // Amethyst Nebula
        case 5: return {1.00f, 0.80f, 0.34f, alpha}; // Solar Gold
        default: return {0.36f, 0.78f, 1.00f, alpha}; // Ocean Blue (SCPH Classic)
    }
}

RgbaColor Ps2Renderer::SecondaryAccentColor(float alpha) const {
    switch (settings_.theme) {
        case 1: return {0.42f, 0.56f, 0.74f, alpha};
        case 2: return {0.10f, 0.62f, 0.42f, alpha};
        case 3: return {0.76f, 0.18f, 0.38f, alpha};
        case 4: return {0.48f, 0.24f, 0.86f, alpha};
        case 5: return {0.72f, 0.50f, 0.16f, alpha};
        default: return {0.16f, 0.46f, 0.92f, alpha};
    }
}

RgbaColor Ps2Renderer::PanelTopColor(float alpha) const {
    switch (settings_.theme) {
        case 1: return {0.04f, 0.05f, 0.08f, alpha};
        case 2: return {0.02f, 0.07f, 0.05f, alpha};
        case 3: return {0.09f, 0.02f, 0.05f, alpha};
        case 4: return {0.06f, 0.03f, 0.12f, alpha};
        case 5: return {0.08f, 0.06f, 0.02f, alpha};
        default: return {0.03f, 0.07f, 0.17f, alpha};
    }
}

RgbaColor Ps2Renderer::PanelBotColor(float alpha) const {
    switch (settings_.theme) {
        case 1: return {0.02f, 0.03f, 0.05f, alpha};
        case 2: return {0.01f, 0.04f, 0.03f, alpha};
        case 3: return {0.04f, 0.01f, 0.03f, alpha};
        case 4: return {0.03f, 0.015f, 0.08f, alpha};
        case 5: return {0.04f, 0.03f, 0.01f, alpha};
        default: return {0.015f, 0.035f, 0.10f, alpha};
    }
}

RgbaColor Ps2Renderer::PanelBorderColor(float alpha) const {
    RgbaColor acc = AccentColor(alpha);
    return {acc.r * 0.88f, acc.g * 0.92f, std::min(1.0f, acc.b * 1.05f), alpha};
}

RgbaColor Ps2Renderer::PrimaryTextColor(float alpha) const {
    return {0.95f, 0.98f, 1.00f, alpha};
}

RgbaColor Ps2Renderer::SecondaryTextColor(float alpha) const {
    switch (settings_.theme) {
        case 2: return {0.72f, 0.92f, 0.82f, alpha};
        case 3: return {0.94f, 0.78f, 0.86f, alpha};
        case 4: return {0.84f, 0.78f, 0.98f, alpha};
        case 5: return {0.95f, 0.88f, 0.72f, alpha};
        default: return {0.72f, 0.85f, 0.98f, alpha};
    }
}

Ps2Renderer::LayoutMetrics Ps2Renderer::ComputeMetrics() const {
    LayoutMetrics m;
    m.padX = (screen_width_ >= 820.0f) ? 34.0f : 22.0f;
    m.topBarH = 40.0f;
    m.bottomBarH = 38.0f;
    m.bodyTop = 58.0f;
    m.bodyBot = kScreenHeight - m.bottomBarH - 8.0f;
    m.bodyH = m.bodyBot - m.bodyTop;
    return m;
}

void Ps2Renderer::ComputeBackgroundOrbs(
    float& outCenterX, float& outCenterY,
    float& outCoreBoost,
    float* outOrbs28, float* outTrails28) const {

    // Base orbital center depending on active state
    if (boot_active_) {
        outCenterX = screen_width_ * 0.5f;
        outCenterY = kScreenHeight * 0.48f;
    } else if (launching_) {
        outCenterX = screen_width_ * 0.5f;
        outCenterY = kScreenHeight * 0.45f;
    } else if (screen_ == SCREEN_MAIN_MENU) {
        outCenterX = screen_width_ * 0.285f;
        outCenterY = kScreenHeight * 0.50f;
    } else {
        outCenterX = screen_width_ * 0.66f;
        outCenterY = kScreenHeight * 0.46f;
    }

    float motionScale = (settings_.orbsMode == 2) ? 0.0f : (settings_.orbsMode == 1 ? 0.38f : 1.0f);
    float screenAlpha = (screen_ == SCREEN_MAIN_MENU || boot_active_ || launching_) ? 1.0f : 0.42f;

    // Incorporate actual wall-clock seconds into PS2 clock rotation
    std::time_t nowSec = std::time(nullptr);
    std::tm tmNow{};
    localtime_r(&nowSec, &tmNow);
    const float clockAngle = (static_cast<float>(tmNow.tm_sec) / 60.0f) * (2.0f * kPi);

    float gather = 1.0f;
    float spreadMult = 1.0f;
    float trailWeight = (settings_.orbsMode == 2) ? 0.0f : 1.0f;
    outCoreBoost = 0.0f;

    if (boot_active_) {
        float p = Clamp01(boot_t_ / std::max(0.1f, boot_duration_));
        if (p < 0.42f) {
            gather = SmoothStep(0.05f, 0.42f, p) * 0.45f;
            spreadMult = 2.6f - 0.8f * SmoothStep(0.0f, 0.42f, p);
            outCoreBoost = 0.25f * gather;
        } else {
            float k = EaseInOutSine((p - 0.42f) / 0.58f);
            gather = 0.45f + 0.55f * k;
            spreadMult = 1.8f - 0.8f * k;
            outCoreBoost = 0.85f * std::sin(k * kPi);
        }
    } else if (launching_) {
        float p = EaseOutCubic(Clamp01(launch_t_ / 0.72f));
        spreadMult = 1.0f - 0.86f * p;
        outCoreBoost = 1.45f * p;
        trailWeight = 1.35f;
    } else if (screen_ != SCREEN_MAIN_MENU) {
        spreadMult = 1.18f;
        trailWeight = 0.45f;
    }

    const float baseOrbitRx = std::min(118.0f, screen_width_ * 0.135f) * spreadMult;
    const float baseOrbitRy = 88.0f * spreadMult;
    const float speed = (0.72f + 0.15f * motionScale) * (settings_.orbsMode == 2 ? 0.0f : 1.0f);
    const float tiltAngle = orb_tilt_cur_;

    for (int i = 0; i < 7; ++i) {
        float phase = (static_cast<float>(i) / 7.0f) * 2.0f * kPi;
        float theta = phase + time_sec_ * speed * motionScale + clockAngle * 0.25f;
        float prevTheta = theta - 0.24f * (settings_.orbsMode == 1 ? 0.5f : 1.0f);

        auto evalPos = [&](float ang, float& px, float& py, float& depth) {
            // Slight 3D inclined ellipse per orb like PS2 clock cluster
            float rx = baseOrbitRx * (0.86f + 0.20f * std::sin(time_sec_ * 0.31f * motionScale + i * 1.1f));
            float ry = baseOrbitRy * (0.78f + 0.22f * std::cos(time_sec_ * 0.27f * motionScale + i * 0.9f));
            float lx = std::cos(ang) * rx;
            float ly = std::sin(ang) * ry;
            // Apply subtle plane tilt from active menu selection
            float ct = std::cos(tiltAngle + i * 0.12f);
            float st = std::sin(tiltAngle + i * 0.12f);
            px = outCenterX + (lx * ct - ly * st);
            py = outCenterY + (lx * st + ly * ct) * 0.74f + std::sin(ang * 2.0f + i) * 9.0f * spreadMult;
            depth = 0.78f + 0.28f * std::sin(ang);
        };

        float x = 0.0f, y = 0.0f, depth = 1.0f;
        float tx = 0.0f, ty = 0.0f, tdepth = 1.0f;
        evalPos(theta, x, y, depth);
        evalPos(prevTheta, tx, ty, tdepth);

        const float orbRad = (8.8f + (i == 0 ? 2.4f : 0.0f)) * depth * (launching_ ? (1.0f + 0.25f * outCoreBoost) : 1.0f);
        outOrbs28[i * 4 + 0] = x;
        outOrbs28[i * 4 + 1] = y;
        outOrbs28[i * 4 + 2] = orbRad;
        outOrbs28[i * 4 + 3] = screenAlpha * gather * (0.72f + 0.28f * depth);

        outTrails28[i * 4 + 0] = tx;
        outTrails28[i * 4 + 1] = ty;
        outTrails28[i * 4 + 2] = 0.0f;
        outTrails28[i * 4 + 3] = trailWeight * screenAlpha * gather;
    }
}

void Ps2Renderer::DrawHeaderBar(const std::string& title, float alpha) {
    const LayoutMetrics m = ComputeMetrics();
    const RgbaColor acc = AccentColor(alpha * 0.85f);
    // Sleek PS2 horizontal luminous underline + crisp airy title
    DrawText(title, m.padX, m.bodyTop - 4.0f, 22.0f, STYLE_HEADER, PrimaryTextColor(alpha));
    FlushQuadBatch();
    BeginQuadBatch();
    PushQuad(white_tex_, m.padX, m.bodyTop + 26.0f, screen_width_ - m.padX * 2.0f, 1.5f,
             {acc.r, acc.g, acc.b, alpha * 0.55f}, {acc.r, acc.g, acc.b, alpha * 0.12f});
}

void Ps2Renderer::DrawStatusBar(float alpha) {
    if (alpha <= 0.01f) return;
    const LayoutMetrics m = ComputeMetrics();

    std::string statusLine;
    std::string slotBadge;
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
                std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
            } else {
                int h12 = tmNow.tm_hour % 12;
                if (h12 == 0) h12 = 12;
                std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d %s", h12, tmNow.tm_min, tmNow.tm_hour >= 12 ? "PM" : "AM");
            }

            char batBuf[24] = {};
            std::snprintf(batBuf, sizeof(batBuf), "%s%d%%", status_.batteryCharging ? "⚡" : "", status_.batteryLevel);

            std::string netIcons;
            if (status_.wifiConnected) netIcons += "Wi-Fi  ";
            if (status_.bluetoothConnected) netIcons += "BT  ";
            if (status_.audioMuted) netIcons += "MUTE  ";

            cached_status_line_ = netIcons + std::string(batBuf) + "   " + std::string(dateBuf) + "  " + std::string(timeBuf);
        }
        statusLine = cached_status_line_;
        biosOk = status_.biosValid;

        if (!slot1_card_name_.empty() || !slot2_card_name_.empty()) {
            slotBadge = "MC1:" + (slot1_card_name_.empty() ? "—" : slot1_card_name_) +
                        "   MC2:" + (slot2_card_name_.empty() ? "—" : slot2_card_name_);
        }
    }

    // Subtle smoked top bar strip
    DrawPanel(m.padX, 8.0f, screen_width_ - m.padX * 2.0f, 34.0f, 8.0f, 1.0f,
              PanelTopColor(0.56f * alpha), PanelBotColor(0.68f * alpha),
              PanelBorderColor(0.32f * alpha));

    BeginQuadBatch();
    const RgbaColor acc = AccentColor(alpha);
    DrawText("EmuCoreX  PS2", m.padX + 14.0f, 14.0f, 15.0f, STYLE_BODY_BOLD, acc);

    if (!slotBadge.empty() && screen_width_ >= 780.0f) {
        DrawText(slotBadge, m.padX + 160.0f, 16.0f, 11.5f, STYLE_MONO,
                 SecondaryTextColor(alpha * 0.78f), screen_width_ * 0.36f, 1);
    }

    if (!biosOk) {
        DrawText("NO BIOS", screen_width_ * 0.5f - 40.0f, 16.0f, 11.5f, STYLE_BODY_BOLD,
                 {1.0f, 0.45f, 0.38f, alpha});
    }

    DrawText(statusLine, m.padX, 15.0f, 12.5f, STYLE_MONO,
             PrimaryTextColor(alpha * 0.92f), screen_width_ - m.padX * 2.0f - 28.0f, 1, 2,
             screen_width_ - m.padX * 2.0f - 14.0f);
    FlushQuadBatch();
}

void Ps2Renderer::DrawBottomBar(float alpha) {
    if (alpha <= 0.01f) return;
    const LayoutMetrics m = ComputeMetrics();
    const float barY = kScreenHeight - m.bottomBarH - 6.0f;

    struct BtnHint {
        std::string glyph;
        std::string label;
        int key;
        RgbaColor col;
    };
    std::vector<BtnHint> btns;
    const std::string confirmSym = settings_.swapConfirm ? "○" : "×";
    const std::string backSym = settings_.swapConfirm ? "×" : "○";
    const RgbaColor confirmCol = AccentColor(alpha);
    const RgbaColor backCol = {0.96f, 0.48f, 0.56f, alpha};

    if (screen_ == SCREEN_MAIN_MENU) {
        btns.push_back({confirmSym, TextOr("ps2ui_hint_select", "Select"), KEY_CONFIRM, confirmCol});
        btns.push_back({"△", TextOr("ps2ui_hint_version", "Version"), KEY_OPTIONS, {0.42f, 0.92f, 0.68f, alpha}});
        btns.push_back({backSym, TextOr("ps2ui_hint_exit", "Exit"), KEY_BACK, backCol});
    } else if (screen_ == SCREEN_GAMES) {
        btns.push_back({confirmSym, TextOr("ps2ui_btn_start", "Start"), KEY_CONFIRM, confirmCol});
        if (!games_.empty() && game_index_ >= 0 && game_index_ < static_cast<int>(games_.size()) &&
            games_[game_index_].latestSaveSlot >= 0) {
            btns.push_back({"□", TextOr("ps2ui_btn_continue", "Continue"), KEY_SECONDARY, {0.82f, 0.62f, 1.0f, alpha}});
        }
        btns.push_back({"△", TextOr("ps2ui_btn_options", "Options"), KEY_OPTIONS, {0.42f, 0.92f, 0.68f, alpha}});
        btns.push_back({backSym, TextOr("ps2ui_hint_back", "Back"), KEY_BACK, backCol});
    } else if (screen_ == SCREEN_BROWSER) {
        if (mc_tab_ == 2) {
            btns.push_back({confirmSym, mc_card_pane_ == 0
                ? TextOr("ps2ui_hint_change", "Cycle")
                : TextOr("ps2ui_mc_actions", "Actions"), KEY_CONFIRM, confirmCol});
            btns.push_back({"□", TextOr("ps2ui_mc_swap", "Swap 1↔2"), KEY_SECONDARY, {0.84f, 0.58f, 1.0f, alpha}});
            btns.push_back({"△", TextOr("ps2ui_mc_create_short", "+ Create Card"), KEY_OPTIONS, {0.36f, 0.94f, 0.68f, alpha}});
        } else {
            btns.push_back({confirmSym, TextOr("ps2ui_mc_load", "Load"), KEY_CONFIRM, confirmCol});
            btns.push_back({"□", TextOr("ps2ui_mc_delete", "Delete"), KEY_SECONDARY, {0.96f, 0.48f, 0.42f, alpha}});
        }
        btns.push_back({"L1/R1", TextOr("ps2ui_hint_tab", "Tab"), KEY_NEXT_TAB, SecondaryTextColor(alpha)});
        btns.push_back({backSym, TextOr("ps2ui_hint_back", "Back"), KEY_BACK, backCol});
    } else if (screen_ == SCREEN_SETTINGS) {
        btns.push_back({confirmSym, TextOr("ps2ui_hint_change", "Change"), KEY_CONFIRM, confirmCol});
        btns.push_back({backSym, TextOr("ps2ui_hint_back", "Back"), KEY_BACK, backCol});
    }

    float totalW = 0.0f;
    std::vector<float> pillWidths(btns.size());
    for (size_t i = 0; i < btns.size(); ++i) {
        float gw = 0.0f, gh = 0.0f, lw = 0.0f, lh = 0.0f;
        MeasureText(btns[i].glyph, 13.5f, STYLE_BODY_BOLD, 0.0f, 1, gw, gh);
        MeasureText(btns[i].label, 12.5f, STYLE_BODY, 0.0f, 1, lw, lh);
        pillWidths[i] = gw + lw + 28.0f;
        totalW += pillWidths[i] + 10.0f;
    }
    if (!btns.empty()) totalW -= 10.0f;

    float curX = std::max(m.padX, (screen_width_ - totalW) * 0.5f);
    for (size_t i = 0; i < btns.size(); ++i) {
        const float pw = pillWidths[i];
        DrawPanel(curX, barY, pw, 30.0f, 10.0f, 1.0f,
                  PanelTopColor(0.68f * alpha), PanelBotColor(0.78f * alpha),
                  {btns[i].col.r, btns[i].col.g, btns[i].col.b, 0.42f * alpha});
        BeginQuadBatch();
        float gw = 0.0f, gh = 0.0f;
        MeasureText(btns[i].glyph, 13.5f, STYLE_BODY_BOLD, 0.0f, 1, gw, gh);
        DrawText(btns[i].glyph, curX + 10.0f, barY + 5.5f, 13.5f, STYLE_BODY_BOLD, btns[i].col);
        DrawText(btns[i].label, curX + 16.0f + gw, barY + 6.0f, 12.5f, STYLE_BODY, PrimaryTextColor(alpha * 0.94f));
        FlushQuadBatch();

        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(curX - 4.0f, barY - 4.0f, pw + 8.0f, 38.0f, HIT_BOTTOM_ACTION, btns[i].key);
        }
        curX += pw + 10.0f;
    }
}

void Ps2Renderer::DrawBootIntro(float alpha) {
    const float dur = std::max(0.1f, boot_duration_);
    const float p = Clamp01(boot_t_ / dur);

    // 3D Camera glide over translucent glowing PS2 monolith towers
    if (settings_.towersMode != 2) {
        const float aspect = screen_width_ / kScreenHeight;
        float proj[16], view[16], mvp[16];
        MakePerspective(0.88f, aspect, 0.2f, 65.0f, proj);

        const float camZ = 4.5f - p * 10.5f;
        const float camY = 5.4f - SmoothStep(0.0f, 0.6f, p) * 2.4f + SmoothStep(0.65f, 1.0f, p) * 1.6f;
        const float camX = std::sin(p * kPi) * 0.9f;
        MakeLookAt(camX, camY, camZ, 0.0f, 0.6f, camZ - 11.0f, 0.0f, 1.0f, 0.0f, view);
        Mat4Mul(proj, view, mvp);

        const float towerAlpha = (1.0f - SmoothStep(0.62f, 0.96f, p)) * 0.92f * alpha;
        const float rise = SmoothStep(0.02f, 0.35f, p) * (1.0f - SmoothStep(0.70f, 1.0f, p) * 0.45f);
        DrawTowers3D(mvp, camX, camY, camZ, rise, towerAlpha);
    }

    // Center crystal beam + EmuCoreX PS2 wordmark reveal
    const float wordAlpha = SmoothStep(0.24f, 0.48f, p) * (1.0f - SmoothStep(0.80f, 0.98f, p)) * alpha;
    if (wordAlpha > 0.01f) {
        const RgbaColor acc = AccentColor(wordAlpha);
        const float cx = screen_width_ * 0.5f;
        const float cy = kScreenHeight * 0.47f;
        const float beamW = std::min(screen_width_ * 0.72f, 520.0f) * EaseOutCubic(SmoothStep(0.20f, 0.56f, p));

        BeginQuadBatch();
        PushQuad(white_tex_, cx - beamW * 0.5f, cy + 28.0f, beamW, 2.0f,
                 {acc.r, acc.g, acc.b, wordAlpha * 0.75f});
        DrawText("EmuCoreX", 0.0f, cy - 30.0f, 44.0f, STYLE_MENU, PrimaryTextColor(wordAlpha),
                 screen_width_, 1, 1, screen_width_);
        DrawText(TextOr("ps2ui_boot_subtitle", "PlayStation 2 System Shell"),
                 0.0f, cy + 40.0f, 15.0f, STYLE_HEADER, SecondaryTextColor(wordAlpha * 0.88f),
                 screen_width_, 1, 1, screen_width_);
        FlushQuadBatch();
    }
}

void Ps2Renderer::DrawMainMenu(float alpha) {
    const LayoutMetrics m = ComputeMetrics();

    // Optional subtle background horizon towers rising softly from lower mist
    if (settings_.towersMode != 2) {
        const float aspect = screen_width_ / kScreenHeight;
        float proj[16], view[16], mvp[16];
        MakePerspective(0.82f, aspect, 0.2f, 65.0f, proj);
        const float camX = 0.35f * std::sin(time_sec_ * 0.14f);
        const float camY = 3.2f;
        const float camZ = 4.2f;
        MakeLookAt(camX, camY, camZ, -0.8f, 0.3f, -12.0f, 0.0f, 1.0f, 0.0f, view);
        Mat4Mul(proj, view, mvp);
        const float tAlpha = (settings_.towersMode == 0 ? 0.34f : 0.16f) * alpha;
        DrawTowers3D(mvp, camX, camY, camZ, 0.75f, tAlpha);
    }

    // Subtle rotating luminous clock arc & brand caption under the 7 orbs on the left
    const float orbCx = screen_width_ * 0.285f;
    const float orbCy = kScreenHeight * 0.50f;
    BeginQuadBatch();
    DrawText(TextOr("ps2ui_main_menu", "System Shell"),
             orbCx - 140.0f, orbCy + 114.0f, 13.0f, STYLE_HEADER,
             SecondaryTextColor(alpha * 0.68f), 280.0f, 1, 1, 280.0f);
    FlushQuadBatch();

    // Right-hand authentic airy PS2 BIOS Menu Stack
    const float menuX = std::max(screen_width_ * 0.51f, orbCx + 150.0f);
    const float menuW = screen_width_ - menuX - m.padX - 10.0f;
    const float rowH = 66.0f;
    const float rowGap = 16.0f;
    const float startY = kScreenHeight * 0.50f - (3.0f * rowH + 2.0f * rowGap) * 0.5f - 6.0f;

    struct MainEntry {
        std::string title;
        std::string sub;
    };
    std::string mcSubtitle;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%zu %s  •  %zu %s",
                      saves_.size(), TextOr("ps2ui_saves_word", "Saves").c_str(),
                      memcards_.size(), TextOr("ps2ui_cards_word", "Cards").c_str());
        mcSubtitle = buf;
    }
    std::string gamesSub;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%zu %s",
                      games_.size(), TextOr("ps2ui_discs", "Discs").c_str());
        gamesSub = buf;
    }

    MainEntry items[3] = {
        {TextOr("ps2ui_menu_games", "Games"), gamesSub},
        {TextOr("ps2ui_menu_browser", "Browser"), mcSubtitle},
        {TextOr("ps2ui_menu_settings", "System Configuration"), TextOr("ps2ui_menu_settings_sub", "Theme, Display & Audio")},
    };

    const RgbaColor acc = AccentColor(alpha);
    for (int i = 0; i < 3; ++i) {
        const float ry = startY + static_cast<float>(i) * (rowH + rowGap);
        const bool sel = (i == main_index_);
        const float focusBoost = sel ? 1.0f : 0.0f;

        if (sel) {
            // Sleek horizontal glowing crystal beam instead of heavy box
            DrawPanel(menuX, ry, menuW, rowH, 12.0f, 1.2f,
                      PanelTopColor(0.56f * alpha), PanelBotColor(0.72f * alpha),
                      {acc.r, acc.g, acc.b, 0.72f * alpha},
                      {acc.r, acc.g, acc.b, 0.52f * alpha});
            if (!cursor_pos_init_) {
                cursor_x_ = menuX - 18.0f;
                cursor_y_ = ry + rowH * 0.5f;
                cursor_pos_init_ = true;
            }
        } else {
            DrawPanel(menuX, ry, menuW, rowH, 12.0f, 0.8f,
                      PanelTopColor(0.24f * alpha), PanelBotColor(0.32f * alpha),
                      {acc.r, acc.g, acc.b, 0.16f * alpha});
        }

        BeginQuadBatch();
        if (sel) {
            DrawDiamondCursor(menuX - 18.0f, cursor_y_, 10.5f, alpha);
        }
        DrawText(items[i].title, menuX + 22.0f, ry + 11.0f, sel ? 26.0f : 23.0f,
                 STYLE_MENU, sel ? PrimaryTextColor(alpha) : SecondaryTextColor(alpha * 0.82f),
                 menuW - 44.0f, 1);
        DrawText(items[i].sub, menuX + 24.0f, ry + 42.0f, 12.0f,
                 STYLE_BODY, sel ? RgbaColor{acc.r, acc.g, acc.b, alpha} : SecondaryTextColor(alpha * 0.55f),
                 menuW - 48.0f, 1);
        FlushQuadBatch();

        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(menuX, ry, menuW, rowH, HIT_MAIN_ROW, i);
        }
        (void)focusBoost;
    }
}

void Ps2Renderer::DrawGamesScreen(float alpha) {
    const LayoutMetrics m = ComputeMetrics();
    DrawHeaderBar(TextOr("ps2ui_menu_games", "Games"), alpha);

    std::vector<GameEntry> gamesCopy;
    bool isLoading = false;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        gamesCopy = games_;
        isLoading = games_loading_;
    }

    const float listTop = m.bodyTop + 40.0f;
    const float listH = m.bodyBot - listTop;
    const float leftW = std::min(screen_width_ * 0.50f, 470.0f);
    const float rightX = m.padX + leftW + 20.0f;
    const float rightW = screen_width_ - rightX - m.padX;

    if (gamesCopy.empty()) {
        DrawPanel(m.padX, listTop, screen_width_ - m.padX * 2.0f, listH, 12.0f, 1.0f,
                  PanelTopColor(0.65f * alpha), PanelBotColor(0.75f * alpha), PanelBorderColor(0.42f * alpha));
        BeginQuadBatch();
        DrawText(isLoading ? TextOr("ps2ui_loading_games", "Scanning library...")
                           : TextOr("ps2ui_no_games", "No PS2 games found in library"),
                 m.padX, listTop + listH * 0.45f, 18.0f, STYLE_HEADER, SecondaryTextColor(alpha),
                 screen_width_ - m.padX * 2.0f, 2, 1, screen_width_ - m.padX * 2.0f);
        FlushQuadBatch();
        return;
    }

    game_index_ = std::clamp(game_index_, 0, static_cast<int>(gamesCopy.size()) - 1);

    // Left panel: smoked PS2 glass list
    DrawPanel(m.padX, listTop, leftW, listH, 12.0f, 1.0f,
              PanelTopColor(0.62f * alpha), PanelBotColor(0.76f * alpha), PanelBorderColor(0.38f * alpha));

    const float rowH = 54.0f;
    const float rowStep = 59.0f;
    const float maxScroll = std::max(0.0f, static_cast<float>(gamesCopy.size()) * rowStep - (listH - 16.0f));
    game_scroll_target_y_ = std::clamp(game_scroll_target_y_, 0.0f, maxScroll);
    game_scroll_y_ = std::clamp(game_scroll_y_, 0.0f, maxScroll);

    // Scissor clip inside left panel
    glEnable(GL_SCISSOR_TEST);
    const float sxScale = static_cast<float>(view_width_) / screen_width_;
    const float syScale = static_cast<float>(view_height_) / kScreenHeight;
    const int scX = view_x_ + static_cast<int>(m.padX * sxScale);
    const int scY = view_y_ + static_cast<int>((kScreenHeight - (listTop + listH - 6.0f)) * syScale);
    const int scW = static_cast<int>(leftW * sxScale);
    const int scH = static_cast<int>((listH - 12.0f) * syScale);
    glScissor(scX, scY, std::max(1, scW), std::max(1, scH));

    const RgbaColor acc = AccentColor(alpha);
    for (int i = 0; i < static_cast<int>(gamesCopy.size()); ++i) {
        const float ry = listTop + 8.0f + static_cast<float>(i) * rowStep - game_scroll_y_;
        if (ry + rowH < listTop || ry > listTop + listH) continue;

        const bool sel = (i == game_index_);
        const GameEntry& g = gamesCopy[i];
        const bool isBlueCd = (g.fileSizeBytes > 0 && g.fileSizeBytes < 750LL * 1024LL * 1024LL);

        DrawPanel(m.padX + 8.0f, ry, leftW - 16.0f, rowH, 9.0f, sel ? 1.3f : 0.7f,
                  sel ? PanelTopColor(0.84f * alpha) : PanelTopColor(0.32f * alpha),
                  sel ? PanelBotColor(0.92f * alpha) : PanelBotColor(0.42f * alpha),
                  sel ? RgbaColor{acc.r, acc.g, acc.b, 0.85f * alpha} : RgbaColor{acc.r, acc.g, acc.b, 0.14f * alpha},
                  sel ? RgbaColor{acc.r, acc.g, acc.b, 0.45f * alpha} : RgbaColor{0, 0, 0, 0});

        BeginQuadBatch();
        // Tiny DVD (silver) or CD (blue) pill indicator on left edge
        const RgbaColor discCol = isBlueCd ? RgbaColor{0.25f, 0.58f, 1.0f, alpha} : RgbaColor{0.82f, 0.86f, 0.94f, alpha};
        PushQuad(white_tex_, m.padX + 16.0f, ry + 12.0f, 3.5f, rowH - 24.0f, discCol);

        DrawText(g.title, m.padX + 28.0f, ry + 7.0f, 15.0f, sel ? STYLE_BODY_BOLD : STYLE_BODY,
                 sel ? PrimaryTextColor(alpha) : SecondaryTextColor(alpha * 0.9f), leftW - 56.0f, 1);

        std::string sub = g.serial.empty() ? (isBlueCd ? "CD-ROM" : "DVD-ROM") : g.serial;
        if (!g.sizeStr.empty()) sub += "  •  " + g.sizeStr;
        if (g.saveCount > 0) {
            char sb[40];
            std::snprintf(sb, sizeof(sb), "  •  %d %s", g.saveCount, TextOr("ps2ui_saves_word", "Saves").c_str());
            sub += sb;
        }
        DrawText(sub, m.padX + 28.0f, ry + 30.0f, 11.0f, STYLE_MONO,
                 sel ? RgbaColor{acc.r, acc.g, acc.b, alpha} : SecondaryTextColor(alpha * 0.60f), leftW - 56.0f, 1);
        FlushQuadBatch();

        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(m.padX + 8.0f, std::max(listTop, ry), leftW - 16.0f,
                        std::min(rowH, listTop + listH - ry), HIT_GAME_ROW, i);
        }
    }
    glDisable(GL_SCISSOR_TEST);

    // Right Showcase Panel: Tall DVD Box Cover Art (Aspect-Fit) + Spinning PS2 Disc + Action Buttons
    DrawPanel(rightX, listTop, rightW, listH, 12.0f, 1.0f,
              PanelTopColor(0.58f * alpha), PanelBotColor(0.74f * alpha), PanelBorderColor(0.38f * alpha));

    const GameEntry& selGame = gamesCopy[game_index_];
    const bool selBlueCd = (selGame.fileSizeBytes > 0 && selGame.fileSizeBytes < 750LL * 1024LL * 1024LL);

    // Top half of showcase: Spinning PS2 Silver DVD or Blue CD peeking out behind cover art
    const float artH = std::min(198.0f, listH * 0.50f);
    const float artW = artH * 0.72f; // Authentic tall PS2 DVD case proportions (~1:1.4)
    const float centerShowcaseX = rightX + rightW * 0.5f;
    const float artX = centerShowcaseX - artW * 0.62f;
    const float artY = listTop + 16.0f;

    const float discR = artH * 0.44f;
    const float discCx = artX + artW + discR * 0.34f;
    const float discCy = artY + artH * 0.50f;
    DrawOpticalDisc(discCx, discCy, discR, disc_spin_angle_, selBlueCd ? 1 : 0, alpha * 0.96f);

    DrawPanel(artX - 3.0f, artY - 3.0f, artW + 6.0f, artH + 6.0f, 7.0f, 1.2f,
              {0.02f, 0.03f, 0.06f, 0.94f * alpha}, {0.01f, 0.02f, 0.04f, 0.96f * alpha},
              {acc.r, acc.g, acc.b, 0.45f * alpha});

    const auto& coverImg = GetOrCreateImage(selGame.coverPath, 320);
    if (coverImg.tex) {
        BeginQuadBatch();
        DrawImageAspectFit(coverImg, artX, artY, artW, artH, alpha, false);
        FlushQuadBatch();
    } else {
        DrawOpticalDisc(artX + artW * 0.5f, artY + artH * 0.5f, artW * 0.36f, disc_spin_angle_ * 1.15f, selBlueCd ? 1 : 0, alpha * 0.75f);
    }

    // Game Title, Metadata & Action Buttons
    const float infoY = artY + artH + 14.0f;
    BeginQuadBatch();
    DrawText(selGame.title, rightX + 16.0f, infoY, 18.0f, STYLE_BODY_BOLD,
             PrimaryTextColor(alpha), rightW - 32.0f, 2, 1, rightW - 32.0f);

    std::string meta = (selBlueCd ? "PS2 CD-ROM" : "PS2 DVD-ROM");
    if (!selGame.serial.empty()) meta += "  •  " + selGame.serial;
    if (!selGame.sizeStr.empty()) meta += "  •  " + selGame.sizeStr;
    if (!selGame.compatStatus.empty()) meta += "  •  " + selGame.compatStatus;
    DrawText(meta, rightX + 14.0f, infoY + 46.0f, 11.5f, STYLE_MONO,
             SecondaryTextColor(alpha * 0.85f), rightW - 28.0f, 1, 1, rightW - 28.0f);
    FlushQuadBatch();

    const float btnY = listTop + listH - 48.0f;
    const bool hasCont = (selGame.latestSaveSlot >= 0);
    const float gap = 10.0f;
    const int btnCount = hasCont ? 3 : 2;
    const float btnW = (rightW - 32.0f - gap * static_cast<float>(btnCount - 1)) / static_cast<float>(btnCount);
    float bx = rightX + 16.0f;

    auto drawBtn = [&](const std::string& lbl, int hitKind, bool primary) {
        DrawPanel(bx, btnY, btnW, 34.0f, 8.0f, 1.2f,
                  primary ? PanelTopColor(0.95f * alpha) : PanelTopColor(0.64f * alpha),
                  PanelBotColor(0.95f * alpha),
                  {acc.r, acc.g, acc.b, (primary ? 0.90f : 0.45f) * alpha},
                  primary ? RgbaColor{acc.r, acc.g, acc.b, 0.40f * alpha} : RgbaColor{0, 0, 0, 0});
        BeginQuadBatch();
        DrawText(lbl, bx + 4.0f, btnY + 8.0f, 13.0f, STYLE_BODY_BOLD, PrimaryTextColor(alpha), btnW - 8.0f, 1, 1, btnW - 8.0f);
        FlushQuadBatch();
        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(bx, btnY, btnW, 34.0f, hitKind, game_index_);
        }
        bx += btnW + gap;
    };

    drawBtn(TextOr("ps2ui_btn_start", "Start"), HIT_GAME_START, true);
    if (hasCont) {
        drawBtn(TextOr("ps2ui_btn_continue", "Continue"), HIT_GAME_CONTINUE, false);
    }
    drawBtn(TextOr("ps2ui_btn_options", "Options"), HIT_GAME_OPTIONS, false);
}

void Ps2Renderer::DrawBrowserScreen(float alpha) {
    const LayoutMetrics m = ComputeMetrics();
    DrawHeaderBar(TextOr("ps2ui_menu_browser", "Browser"), alpha);

    const float tabY = m.bodyTop + 34.0f;
    const float tabW = std::min(195.0f, (screen_width_ - m.padX * 2.0f - 20.0f) / 3.0f);
    const std::string tabLabels[3] = {
        TextOr("ps2ui_mc_tab_all", "Quick Saves"),
        TextOr("ps2ui_mc_tab_game", "Game Saves"),
        TextOr("ps2ui_mc_tab_cards", "Memory Cards"),
    };
    const RgbaColor acc = AccentColor(alpha);
    for (int i = 0; i < 3; ++i) {
        const float tx = m.padX + static_cast<float>(i) * (tabW + 10.0f);
        const bool active = (mc_tab_ == i);
        DrawPanel(tx, tabY, tabW, 30.0f, 8.0f, active ? 1.3f : 0.8f,
                  active ? PanelTopColor(0.88f * alpha) : PanelTopColor(0.35f * alpha),
                  PanelBotColor(0.88f * alpha),
                  {acc.r, acc.g, acc.b, (active ? 0.90f : 0.25f) * alpha},
                  active ? RgbaColor{acc.r, acc.g, acc.b, 0.36f * alpha} : RgbaColor{0, 0, 0, 0});
        BeginQuadBatch();
        DrawText(tabLabels[i], tx + 6.0f, tabY + 6.5f, 12.5f, active ? STYLE_BODY_BOLD : STYLE_BODY,
                 active ? PrimaryTextColor(alpha) : SecondaryTextColor(alpha * 0.78f),
                 tabW - 12.0f, 1, 1, tabW - 12.0f);
        FlushQuadBatch();
        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(tx, tabY, tabW, 30.0f, HIT_MC_TAB, i);
        }
    }

    const float contentTop = tabY + 38.0f;
    const float contentH = m.bodyBot - contentTop;

    // TAB 2: Authentic PS2 Memory Card Browser (Slot 1 & Slot 2 + Available Cards List)
    if (mc_tab_ == 2) {
        std::string s1n, s1m, s1st, s2n, s2m, s2st;
        std::vector<MemoryCardEntry> cards;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            s1n = slot1_card_name_;
            s1m = slot1_card_meta_;
            s1st = slot1_card_status_;
            s2n = slot2_card_name_;
            s2m = slot2_card_meta_;
            s2st = slot2_card_status_;
            cards = memcards_;
        }

        const float slotsAreaH = 148.0f;
        const float slotW = (screen_width_ - m.padX * 2.0f - 18.0f) * 0.5f;

        auto drawSlotBox = [&](int slotIdx, float x, const std::string& name, const std::string& meta, const std::string& status) {
            const bool focused = (mc_card_pane_ == 0 && mc_card_slot_idx_ == slotIdx);
            const bool empty = name.empty();
            const bool isFolder = (!empty && name.find(".ps2") == std::string::npos &&
                                   meta.find("Folder") != std::string::npos);

            DrawPanel(x, contentTop, slotW, slotsAreaH, 12.0f, focused ? 1.5f : 0.9f,
                      focused ? PanelTopColor(0.84f * alpha) : PanelTopColor(0.50f * alpha),
                      PanelBotColor(0.90f * alpha),
                      {acc.r, acc.g, acc.b, (focused ? 0.92f : 0.30f) * alpha},
                      focused ? RgbaColor{acc.r, acc.g, acc.b, 0.28f * alpha} : RgbaColor{0, 0, 0, 0});

            // 3D PS2 MagicGate Card Cartridge on left of slot box
            const float cw = 82.0f;
            const float ch = 104.0f;
            const RgbaColor bodyCol = empty ? RgbaColor{0.18f, 0.24f, 0.36f, alpha} : RgbaColor{0.08f, 0.12f, 0.22f, alpha};
            DrawMemoryCard3D(x + 16.0f, contentTop + 22.0f, cw, ch, bodyCol, acc, focused, empty, isFolder);

            BeginQuadBatch();
            const float tx = x + 114.0f;
            const float tw = slotW - 128.0f;
            const std::string slotLbl = slotIdx == 0
                ? TextOr("ps2ui_mc_slot1", "MEMORY CARD (PS2) / 1")
                : TextOr("ps2ui_mc_slot2", "MEMORY CARD (PS2) / 2");
            DrawText(slotLbl, tx, contentTop + 16.0f, 12.5f, STYLE_BODY_BOLD, acc, tw, 1);

            if (empty) {
                DrawText(TextOr("ps2ui_mc_empty_slot", "Empty Slot"), tx, contentTop + 52.0f, 16.0f,
                         STYLE_BODY, SecondaryTextColor(alpha * 0.72f), tw, 1);
            } else {
                DrawText(name, tx, contentTop + 46.0f, 16.0f, STYLE_BODY_BOLD, PrimaryTextColor(alpha), tw, 1);
                if (!meta.empty()) {
                    DrawText(meta, tx, contentTop + 74.0f, 12.0f, STYLE_MONO, SecondaryTextColor(alpha * 0.85f), tw, 1);
                }
                if (!status.empty()) {
                    DrawText(status, tx, contentTop + 98.0f, 11.5f, STYLE_BODY_BOLD, {0.40f, 0.95f, 0.68f, alpha}, tw, 1);
                }
            }
            if (focused) {
                DrawText(TextOr("ps2ui_mc_tap_cycle", "Tap / × to cycle mounted card"), tx, contentTop + 122.0f, 10.5f,
                         STYLE_BODY, SecondaryTextColor(alpha * 0.65f), tw, 1);
            }
            FlushQuadBatch();

            if (overlay_ == OVERLAY_NONE) {
                RegisterHit(x, contentTop, slotW, slotsAreaH, HIT_CARD_SLOT, slotIdx);
            }
        };

        drawSlotBox(0, m.padX, s1n, s1m, s1st);
        drawSlotBox(1, m.padX + slotW + 18.0f, s2n, s2m, s2st);

        // Quick Action Toolbar between Slots and Card List
        const float toolY = contentTop + slotsAreaH + 8.0f;
        const float toolH = 30.0f;
        struct ToolDef { std::string lbl; int action; };
        ToolDef tools[4] = {
            {TextOr("ps2ui_mc_cycle", "Cycle Active Slot"), 0},
            {TextOr("ps2ui_mc_swap", "Swap 1↔2"), 1},
            {TextOr("ps2ui_mc_eject", "Eject Slot"), 2},
            {TextOr("ps2ui_mc_create_short", "+ Create Card"), 3},
        };
        const float toolW = (screen_width_ - m.padX * 2.0f - 30.0f) / 4.0f;
        for (int i = 0; i < 4; ++i) {
            const float bx = m.padX + static_cast<float>(i) * (toolW + 10.0f);
            DrawPanel(bx, toolY, toolW, toolH, 7.0f, 1.0f,
                      PanelTopColor(0.65f * alpha), PanelBotColor(0.75f * alpha),
                      {acc.r, acc.g, acc.b, (i == 3 ? 0.72f : 0.36f) * alpha});
            BeginQuadBatch();
            DrawText(tools[i].lbl, bx + 4.0f, toolY + 6.5f, 11.5f, STYLE_BODY_BOLD,
                     PrimaryTextColor(alpha), toolW - 8.0f, 1, 1, toolW - 8.0f);
            FlushQuadBatch();
            if (overlay_ == OVERLAY_NONE) {
                RegisterHit(bx, toolY, toolW, toolH, HIT_CARD_TOOL, tools[i].action);
            }
        }

        // Available Cards Scrollable List
        const float listY = toolY + toolH + 8.0f;
        const float listH = m.bodyBot - listY;
        DrawPanel(m.padX, listY, screen_width_ - m.padX * 2.0f, listH, 10.0f, 1.0f,
                  PanelTopColor(0.55f * alpha), PanelBotColor(0.70f * alpha), PanelBorderColor(0.32f * alpha));

        if (cards.empty()) {
            BeginQuadBatch();
            DrawText(TextOr("ps2ui_mc_no_cards", "No PS2 memory cards found"),
                     m.padX, listY + listH * 0.42f, 15.0f, STYLE_BODY, SecondaryTextColor(alpha),
                     screen_width_ - m.padX * 2.0f, 1, 1, screen_width_ - m.padX * 2.0f);
            FlushQuadBatch();
            return;
        }

        mc_card_list_idx_ = std::clamp(mc_card_list_idx_, 0, static_cast<int>(cards.size()) - 1);
        const float rowH = 38.0f;
        const float rowStep = 42.0f;
        const float maxSc = std::max(0.0f, static_cast<float>(cards.size()) * rowStep - (listH - 12.0f));
        mc_card_scroll_target_y_ = std::clamp(mc_card_scroll_target_y_, 0.0f, maxSc);
        mc_card_scroll_y_ = std::clamp(mc_card_scroll_y_, 0.0f, maxSc);

        glEnable(GL_SCISSOR_TEST);
        const float sxScale = static_cast<float>(view_width_) / screen_width_;
        const float syScale = static_cast<float>(view_height_) / kScreenHeight;
        glScissor(
            view_x_ + static_cast<int>(m.padX * sxScale),
            view_y_ + static_cast<int>((kScreenHeight - (listY + listH - 4.0f)) * syScale),
            std::max(1, static_cast<int>((screen_width_ - m.padX * 2.0f) * sxScale)),
            std::max(1, static_cast<int>((listH - 8.0f) * syScale)));

        for (int i = 0; i < static_cast<int>(cards.size()); ++i) {
            const float ry = listY + 6.0f + static_cast<float>(i) * rowStep - mc_card_scroll_y_;
            if (ry + rowH < listY || ry > listY + listH) continue;
            const bool sel = (mc_card_pane_ == 1 && mc_card_list_idx_ == i);
            const auto& c = cards[i];
            const bool inS1 = (!s1n.empty() && c.name == s1n);
            const bool inS2 = (!s2n.empty() && c.name == s2n);

            DrawPanel(m.padX + 8.0f, ry, screen_width_ - m.padX * 2.0f - 16.0f, rowH, 7.0f, sel ? 1.2f : 0.6f,
                      sel ? PanelTopColor(0.84f * alpha) : PanelTopColor(0.28f * alpha),
                      PanelBotColor(0.65f * alpha),
                      {acc.r, acc.g, acc.b, (sel ? 0.85f : 0.14f) * alpha},
                      sel ? RgbaColor{acc.r, acc.g, acc.b, 0.35f * alpha} : RgbaColor{0, 0, 0, 0});

            BeginQuadBatch();
            const std::string tag = c.isFolder ? "[DIR]" : "[PS2]";
            DrawText(tag, m.padX + 18.0f, ry + 10.0f, 11.5f, STYLE_MONO,
                     c.isFolder ? RgbaColor{0.42f, 0.94f, 0.72f, alpha} : acc);
            DrawText(c.name, m.padX + 70.0f, ry + 9.0f, 14.0f, sel ? STYLE_BODY_BOLD : STYLE_BODY,
                     PrimaryTextColor(alpha), screen_width_ * 0.42f, 1);

            std::string rightMeta = c.sizeStr;
            if (!c.dateStr.empty()) rightMeta += (rightMeta.empty() ? "" : "  •  ") + c.dateStr;
            if (inS1) rightMeta += "   [SLOT 1]";
            else if (inS2) rightMeta += "   [SLOT 2]";

            DrawText(rightMeta, m.padX + 18.0f, ry + 10.0f, 11.5f, STYLE_MONO,
                     (inS1 || inS2) ? RgbaColor{0.42f, 0.95f, 0.68f, alpha} : SecondaryTextColor(alpha * 0.82f),
                     screen_width_ - m.padX * 2.0f - 36.0f, 1, 2, screen_width_ - m.padX * 2.0f - 36.0f);
            FlushQuadBatch();

            if (overlay_ == OVERLAY_NONE) {
                RegisterHit(m.padX + 8.0f, std::max(listY, ry), screen_width_ - m.padX * 2.0f - 16.0f,
                            std::min(rowH, listY + listH - ry), HIT_CARD_ROW, i);
            }
        }
        glDisable(GL_SCISSOR_TEST);
        return;
    }

    // TAB 0 / TAB 1: Quick Saves (.p2s) Grid + Inspector
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

    const float gridW = screen_width_ * 0.56f;
    const float inspX = m.padX + gridW + 18.0f;
    const float inspW = screen_width_ - inspX - m.padX;

    DrawPanel(m.padX, contentTop, gridW, contentH, 12.0f, 1.0f,
              PanelTopColor(0.56f * alpha), PanelBotColor(0.72f * alpha), PanelBorderColor(0.35f * alpha));

    if (totalFiltered == 0) {
        BeginQuadBatch();
        DrawText(loadingSaves ? TextOr("ps2ui_loading_saves", "Reading saves...")
                              : TextOr("ps2ui_mc_no_saves", "No quick saves found"),
                 m.padX, contentTop + contentH * 0.44f, 16.0f, STYLE_BODY, SecondaryTextColor(alpha),
                 gridW, 2, 1, gridW);
        FlushQuadBatch();
    } else {
        const float cellGap = 8.0f;
        const float cellW = (gridW - 24.0f - cellGap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        const float cellH = (contentH - 42.0f - cellGap * static_cast<float>(rows - 1)) / static_cast<float>(rows);

        for (int idx = 0; idx < perPage; ++idx) {
            const int r = idx / cols;
            const int c = idx % cols;
            const float cx = m.padX + 12.0f + static_cast<float>(c) * (cellW + cellGap);
            const float cy = contentTop + 10.0f + static_cast<float>(r) * (cellH + cellGap);
            const int fIdx = mc_page_ * perPage + idx;
            const bool occupied = (fIdx < totalFiltered);
            const bool sel = (mc_focus_pane_ == 0 && mc_slot_index_ == idx);

            DrawPanel(cx, cy, cellW, cellH, 8.0f, sel ? 1.5f : 0.7f,
                      sel ? PanelTopColor(0.88f * alpha) : PanelTopColor(0.38f * alpha),
                      PanelBotColor(0.82f * alpha),
                      {acc.r, acc.g, acc.b, (sel ? 0.95f : (occupied ? 0.30f : 0.12f)) * alpha},
                      sel ? RgbaColor{acc.r, acc.g, acc.b, 0.35f * alpha} : RgbaColor{0, 0, 0, 0});

            if (occupied) {
                const SaveEntry& sv = savesCopy[filtered[fIdx]];
                const auto& img = GetOrCreateImage(sv.previewPath, 320);
                if (img.tex) {
                    BeginQuadBatch();
                    DrawImageAspectFit(img, cx + 4.0f, cy + 4.0f, cellW - 8.0f, cellH - 24.0f, alpha, true);
                    FlushQuadBatch();
                } else {
                    DrawOpticalDisc(cx + cellW * 0.5f, cy + (cellH - 18.0f) * 0.5f,
                                    std::min(cellW, cellH - 22.0f) * 0.34f, disc_spin_angle_, 0, alpha * 0.65f);
                }
                BeginQuadBatch();
                DrawText(sv.slotLabel, cx + 6.0f, cy + cellH - 18.0f, 10.5f, STYLE_BODY_BOLD,
                         sel ? acc : PrimaryTextColor(alpha * 0.9f), cellW - 12.0f, 1);
                FlushQuadBatch();
            } else {
                BeginQuadBatch();
                DrawText("—", cx, cy + cellH * 0.38f, 14.0f, STYLE_BODY,
                         SecondaryTextColor(alpha * 0.30f), cellW, 1, 1, cellW);
                FlushQuadBatch();
            }

            if (overlay_ == OVERLAY_NONE) {
                RegisterHit(cx, cy, cellW, cellH, HIT_MC_BLOCK, idx);
            }
        }

        // Page footer inside grid
        const float footY = contentTop + contentH - 26.0f;
        char pBuf[32];
        std::snprintf(pBuf, sizeof(pBuf), "%d / %d", mc_page_ + 1, totalPages);
        BeginQuadBatch();
        DrawText("<", m.padX + 16.0f, footY, 14.0f, STYLE_BODY_BOLD, PrimaryTextColor(alpha));
        DrawText(pBuf, m.padX, footY + 1.0f, 12.0f, STYLE_MONO, SecondaryTextColor(alpha), gridW, 1, 1, gridW);
        DrawText(">", m.padX + gridW - 28.0f, footY, 14.0f, STYLE_BODY_BOLD, PrimaryTextColor(alpha));
        FlushQuadBatch();
        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(m.padX + 8.0f, footY - 4.0f, 36.0f, 26.0f, HIT_MC_PAGE_PREV, 0);
            RegisterHit(m.padX + gridW - 44.0f, footY - 4.0f, 36.0f, 26.0f, HIT_MC_PAGE_NEXT, 0);
        }
    }

    // Right Inspector Panel
    DrawPanel(inspX, contentTop, inspW, contentH, 12.0f, 1.0f,
              PanelTopColor(0.62f * alpha), PanelBotColor(0.76f * alpha), PanelBorderColor(0.38f * alpha));

    const int curPos = mc_page_ * perPage + mc_slot_index_;
    if (curPos >= 0 && curPos < totalFiltered) {
        const SaveEntry& sv = savesCopy[filtered[curPos]];
        const float prevH = std::min(165.0f, contentH * 0.44f);
        const float prevW = inspW - 28.0f;
        const float prevX = inspX + 14.0f;
        const float prevY = contentTop + 14.0f;

        DrawPanel(prevX, prevY, prevW, prevH, 8.0f, 1.0f,
                  {0.02f, 0.03f, 0.06f, alpha}, {0.01f, 0.02f, 0.04f, alpha},
                  {acc.r, acc.g, acc.b, 0.42f * alpha});
        const auto& img = GetOrCreateImage(sv.previewPath, 320);
        if (img.tex) {
            BeginQuadBatch();
            DrawImageAspectFit(img, prevX + 2.0f, prevY + 2.0f, prevW - 4.0f, prevH - 4.0f, alpha, false);
            FlushQuadBatch();
        } else {
            DrawOpticalDisc(prevX + prevW * 0.5f, prevY + prevH * 0.5f, prevH * 0.34f, disc_spin_angle_, 0, alpha * 0.72f);
        }

        const float iy = prevY + prevH + 12.0f;
        BeginQuadBatch();
        DrawText(sv.title, inspX + 16.0f, iy, 16.5f, STYLE_BODY_BOLD, PrimaryTextColor(alpha), inspW - 32.0f, 2);
        DrawText(sv.slotLabel + (sv.serial.empty() ? "" : "  •  " + sv.serial),
                 inspX + 16.0f, iy + 44.0f, 12.5f, STYLE_BODY_BOLD, acc, inspW - 32.0f, 1);
        DrawText(sv.dateStr + (sv.sizeStr.empty() ? "" : "  •  " + sv.sizeStr),
                 inspX + 16.0f, iy + 64.0f, 11.5f, STYLE_MONO, SecondaryTextColor(alpha * 0.85f), inspW - 32.0f, 1);
        FlushQuadBatch();

        const float btnY = contentTop + contentH - 46.0f;
        const float btnGap = 8.0f;
        const float btnW = (inspW - 28.0f - btnGap * 2.0f) / 3.0f;
        const std::string bLbl[3] = {
            TextOr("ps2ui_mc_load", "Load"),
            TextOr("ps2ui_mc_delete", "Delete"),
            TextOr("ps2ui_mc_exit", "Back"),
        };
        for (int b = 0; b < 3; ++b) {
            const float bx = inspX + 14.0f + static_cast<float>(b) * (btnW + btnGap);
            const bool bf = (mc_focus_pane_ == 1 && mc_action_idx_ == b);
            DrawPanel(bx, btnY, btnW, 32.0f, 7.0f, bf ? 1.4f : 0.9f,
                      bf ? PanelTopColor(0.92f * alpha) : PanelTopColor(0.55f * alpha),
                      PanelBotColor(0.88f * alpha),
                      {acc.r, acc.g, acc.b, (bf ? 0.92f : 0.35f) * alpha},
                      bf ? RgbaColor{acc.r, acc.g, acc.b, 0.35f * alpha} : RgbaColor{0, 0, 0, 0});
            BeginQuadBatch();
            DrawText(bLbl[b], bx + 4.0f, btnY + 7.5f, 12.0f, STYLE_BODY_BOLD, PrimaryTextColor(alpha), btnW - 8.0f, 1, 1, btnW - 8.0f);
            FlushQuadBatch();
            if (overlay_ == OVERLAY_NONE) {
                RegisterHit(bx, btnY, btnW, 32.0f, HIT_MC_ACTION, b);
            }
        }
    } else {
        BeginQuadBatch();
        DrawText(TextOr("ps2ui_mc_empty_block", "Select a save state block"),
                 inspX + 12.0f, contentTop + contentH * 0.45f, 14.0f, STYLE_BODY,
                 SecondaryTextColor(alpha * 0.72f), inspW - 24.0f, 2, 1, inspW - 24.0f);
        FlushQuadBatch();
    }
}

void Ps2Renderer::DrawSettingsScreen(float alpha) {
    const LayoutMetrics m = ComputeMetrics();
    DrawHeaderBar(TextOr("ps2ui_menu_settings", "System Configuration"), alpha);

    const float listTop = m.bodyTop + 38.0f;
    const float listH = m.bodyBot - listTop;
    const float listW = std::min(780.0f, screen_width_ - m.padX * 2.0f);
    const float listX = (screen_width_ - listW) * 0.5f;

    DrawPanel(listX, listTop, listW, listH, 12.0f, 1.0f,
              PanelTopColor(0.60f * alpha), PanelBotColor(0.75f * alpha), PanelBorderColor(0.38f * alpha));

    const std::string themeNames[kThemeCount] = {
        TextOr("ps2ui_theme_ocean", "Ocean Blue"),
        TextOr("ps2ui_theme_obsidian", "Midnight Obsidian"),
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
        {TextOr("ps2ui_set_sysinfo", "System Version"), TextOr("ps2ui_view", "View")},
        {TextOr("ps2ui_set_restore", "Restore Defaults"), TextOr("ps2ui_reset", "Reset")},
    };

    const float rowH = 44.0f;
    const float rowStep = 49.0f;
    const float maxSc = std::max(0.0f, static_cast<float>(kSettingsCount) * rowStep - (listH - 16.0f));
    settings_scroll_target_y_ = std::clamp(settings_scroll_target_y_, 0.0f, maxSc);
    settings_scroll_y_ = std::clamp(settings_scroll_y_, 0.0f, maxSc);

    glEnable(GL_SCISSOR_TEST);
    const float sxScale = static_cast<float>(view_width_) / screen_width_;
    const float syScale = static_cast<float>(view_height_) / kScreenHeight;
    glScissor(
        view_x_ + static_cast<int>(listX * sxScale),
        view_y_ + static_cast<int>((kScreenHeight - (listTop + listH - 4.0f)) * syScale),
        std::max(1, static_cast<int>(listW * sxScale)),
        std::max(1, static_cast<int>((listH - 8.0f) * syScale)));

    const RgbaColor acc = AccentColor(alpha);
    for (int i = 0; i < kSettingsCount; ++i) {
        const float ry = listTop + 8.0f + static_cast<float>(i) * rowStep - settings_scroll_y_;
        if (ry + rowH < listTop || ry > listTop + listH) continue;
        const bool sel = (i == settings_index_);

        DrawPanel(listX + 10.0f, ry, listW - 20.0f, rowH, 8.0f, sel ? 1.3f : 0.6f,
                  sel ? PanelTopColor(0.86f * alpha) : PanelTopColor(0.28f * alpha),
                  PanelBotColor(0.68f * alpha),
                  {acc.r, acc.g, acc.b, (sel ? 0.88f : 0.14f) * alpha},
                  sel ? RgbaColor{acc.r, acc.g, acc.b, 0.38f * alpha} : RgbaColor{0, 0, 0, 0});

        BeginQuadBatch();
        DrawText(rows[i].label, listX + 26.0f, ry + 12.0f, 15.0f, sel ? STYLE_BODY_BOLD : STYLE_BODY,
                 sel ? PrimaryTextColor(alpha) : SecondaryTextColor(alpha * 0.9f), listW * 0.52f, 1);

        const float valAreaW = listW * 0.38f;
        const float valAreaX = listX + listW - valAreaW - 22.0f;
        DrawText("<", valAreaX, ry + 11.0f, 15.0f, STYLE_BODY_BOLD,
                 sel ? acc : SecondaryTextColor(alpha * 0.5f));
        DrawText(rows[i].value, valAreaX + 18.0f, ry + 12.0f, 14.5f, STYLE_BODY_BOLD,
                 sel ? acc : PrimaryTextColor(alpha * 0.88f), valAreaW - 36.0f, 1, 1, valAreaW - 36.0f);
        DrawText(">", valAreaX + valAreaW - 14.0f, ry + 11.0f, 15.0f, STYLE_BODY_BOLD,
                 sel ? acc : SecondaryTextColor(alpha * 0.5f));
        FlushQuadBatch();

        if (overlay_ == OVERLAY_NONE) {
            RegisterHit(valAreaX - 10.0f, std::max(listTop, ry), 46.0f, std::min(rowH, listTop + listH - ry), HIT_SETTING_LEFT, i);
            RegisterHit(valAreaX + valAreaW - 36.0f, std::max(listTop, ry), 46.0f, std::min(rowH, listTop + listH - ry), HIT_SETTING_RIGHT, i);
            RegisterHit(listX + 10.0f, std::max(listTop, ry), listW - 20.0f, std::min(rowH, listTop + listH - ry), HIT_SETTING_ROW, i);
        }
    }
    glDisable(GL_SCISSOR_TEST);
}

void Ps2Renderer::DrawLaunchStage() {
    const float p = Clamp01(launch_t_ / 0.85f);
    const float ease = EaseOutCubic(p);
    const RgbaColor acc = AccentColor(1.0f);

    if (settings_.towersMode != 2) {
        const float aspect = screen_width_ / kScreenHeight;
        float proj[16], view[16], mvp[16];
        MakePerspective(0.90f, aspect, 0.2f, 65.0f, proj);
        const float camZ = 4.5f - ease * 8.0f;
        MakeLookAt(0.0f, 2.2f, camZ, 0.0f, 0.8f, camZ - 12.0f, 0.0f, 1.0f, 0.0f, view);
        Mat4Mul(proj, view, mvp);
        DrawTowers3D(mvp, 0.0f, 2.2f, camZ, 1.0f + ease * 0.45f, 0.65f * (1.0f - p * 0.35f));
    }

    const float cx = screen_width_ * 0.5f;
    const float cy = kScreenHeight * 0.45f;
    const float r = 74.0f + 26.0f * ease;
    DrawOpticalDisc(cx, cy, r, disc_spin_angle_ + ease * 10.0f, launch_is_blue_cd_ ? 1 : 0, 0.96f);

    BeginQuadBatch();
    const std::string cap = launch_is_save_
        ? TextOr("ps2ui_launching_save", "Loading Quick Save...")
        : TextOr("ps2ui_launching_disc", "Reading PlayStation 2 Disc...");
    DrawText(cap, 0.0f, cy + r + 26.0f, 17.0f, STYLE_HEADER, acc, screen_width_, 1, 1, screen_width_);
    DrawText(launch_title_, 0.0f, cy + r + 52.0f, 20.0f, STYLE_BODY_BOLD, PrimaryTextColor(1.0f),
             screen_width_ * 0.78f, 1, 1, screen_width_);
    FlushQuadBatch();
}

void Ps2Renderer::DrawConfirmDialog(
    const std::string& title,
    const std::string& message,
    const std::string& yesText,
    const std::string& noText,
    int focusedBtn) {
    const float mw = std::min(430.0f, screen_width_ - 40.0f);
    const float mh = 186.0f;
    const float mx = (screen_width_ - mw) * 0.5f;
    const float my = (kScreenHeight - mh) * 0.5f;
    const RgbaColor acc = AccentColor(1.0f);

    DrawPanel(mx, my, mw, mh, 14.0f, 1.5f,
              PanelTopColor(0.95f), PanelBotColor(0.97f),
              {acc.r, acc.g, acc.b, 0.85f}, {acc.r, acc.g, acc.b, 0.25f});
    BeginQuadBatch();
    DrawText(title, mx + 20.0f, my + 18.0f, 18.5f, STYLE_HEADER, acc, mw - 40.0f, 1, 1, mw - 40.0f);
    DrawText(message, mx + 22.0f, my + 56.0f, 14.0f, STYLE_BODY, PrimaryTextColor(0.94f), mw - 44.0f, 3, 1, mw - 44.0f);
    FlushQuadBatch();

    const float bw = (mw - 68.0f) * 0.5f;
    const float by = my + mh - 50.0f;
    for (int b = 0; b < 2; ++b) {
        const float bx = mx + 24.0f + static_cast<float>(b) * (bw + 20.0f);
        const bool sel = (focusedBtn == b);
        DrawPanel(bx, by, bw, 34.0f, 8.0f, sel ? 1.4f : 0.9f,
                  sel ? PanelTopColor(0.95f) : PanelTopColor(0.55f), PanelBotColor(0.92f),
                  {acc.r, acc.g, acc.b, sel ? 0.92f : 0.35f},
                  sel ? RgbaColor{acc.r, acc.g, acc.b, 0.40f} : RgbaColor{0, 0, 0, 0});
        BeginQuadBatch();
        DrawText(b == 0 ? yesText : noText, bx + 6.0f, by + 7.5f, 13.5f, STYLE_BODY_BOLD,
                 PrimaryTextColor(1.0f), bw - 12.0f, 1, 1, bw - 12.0f);
        FlushQuadBatch();
        RegisterHit(bx, by, bw, 34.0f, HIT_OVERLAY_ITEM, b);
    }
}

void Ps2Renderer::DrawListModal(
    const std::string& title,
    const std::vector<std::string>& items,
    int selectedIdx,
    float modalWidth) {
    const float mw = std::min(modalWidth, screen_width_ - 40.0f);
    const float rowH = 38.0f;
    const float mh = 70.0f + static_cast<float>(items.size()) * (rowH + 6.0f);
    const float mx = (screen_width_ - mw) * 0.5f;
    const float my = (kScreenHeight - mh) * 0.5f;
    const RgbaColor acc = AccentColor(1.0f);

    DrawPanel(mx, my, mw, mh, 14.0f, 1.5f,
              PanelTopColor(0.95f), PanelBotColor(0.97f),
              {acc.r, acc.g, acc.b, 0.85f}, {acc.r, acc.g, acc.b, 0.25f});
    BeginQuadBatch();
    DrawText(title, mx + 18.0f, my + 16.0f, 18.0f, STYLE_HEADER, acc, mw - 36.0f, 1, 1, mw - 36.0f);
    FlushQuadBatch();

    for (size_t i = 0; i < items.size(); ++i) {
        const float ry = my + 54.0f + static_cast<float>(i) * (rowH + 6.0f);
        const bool sel = (static_cast<int>(i) == selectedIdx);
        DrawPanel(mx + 18.0f, ry, mw - 36.0f, rowH, 8.0f, sel ? 1.3f : 0.7f,
                  sel ? PanelTopColor(0.90f) : PanelTopColor(0.45f), PanelBotColor(0.85f),
                  {acc.r, acc.g, acc.b, sel ? 0.92f : 0.24f},
                  sel ? RgbaColor{acc.r, acc.g, acc.b, 0.36f} : RgbaColor{0, 0, 0, 0});
        BeginQuadBatch();
        DrawText(items[i], mx + 28.0f, ry + 9.0f, 14.0f, sel ? STYLE_BODY_BOLD : STYLE_BODY,
                 PrimaryTextColor(1.0f), mw - 56.0f, 1);
        FlushQuadBatch();
        RegisterHit(mx + 18.0f, ry, mw - 36.0f, rowH, HIT_OVERLAY_ITEM, static_cast<int>(i));
    }
}

void Ps2Renderer::DrawOverlay() {
    if (overlay_ == OVERLAY_NONE) return;
    FlushQuadBatch();
    BeginQuadBatch();
    PushQuad(white_tex_, 0.0f, 0.0f, screen_width_, kScreenHeight, {0.0f, 0.01f, 0.04f, 0.62f});
    FlushQuadBatch();
    RegisterHit(0.0f, 0.0f, screen_width_, kScreenHeight, HIT_OVERLAY_BACKDROP, 0);

    const RgbaColor acc = AccentColor(1.0f);

    if (overlay_ == OVERLAY_EXIT_CONFIRM) {
        DrawConfirmDialog(
            TextOr("ps2ui_exit_title", "Exit System Shell"),
            TextOr("ps2ui_exit_msg", "Return to the standard game library?"),
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
            TextOr("ps2ui_del_card_msg", "Delete memory card permanently?") +
                (cardName.empty() ? "" : "\n" + cardName),
            TextOr("ps2ui_yes", "Yes"), TextOr("ps2ui_no", "No"), dialog_btn_);
    } else if (overlay_ == OVERLAY_RESTORE_CONFIRM) {
        DrawConfirmDialog(
            TextOr("ps2ui_restore_title", "Restore Defaults"),
            TextOr("ps2ui_restore_msg", "Reset all PS2 System Shell settings to default?"),
            TextOr("ps2ui_yes", "Yes"), TextOr("ps2ui_no", "No"), dialog_btn_);
    } else if (overlay_ == OVERLAY_GAME_OPTIONS) {
        if (game_index_ >= 0 && game_index_ < static_cast<int>(games_.size())) {
            const GameEntry& g = games_[game_index_];
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
        DrawListModal(cardName.empty() ? TextOr("ps2ui_mc_actions", "Card Actions") : cardName,
                      opts, card_action_menu_idx_, 360.0f);
    } else if (overlay_ == OVERLAY_GAME_INFO) {
        if (game_index_ >= 0 && game_index_ < static_cast<int>(games_.size())) {
            const GameEntry& g = games_[game_index_];
            const bool isBlueCd = (g.fileSizeBytes > 0 && g.fileSizeBytes < 750LL * 1024LL * 1024LL);
            const float mw = std::min(460.0f, screen_width_ - 40.0f);
            const float mh = 226.0f;
            const float mx = (screen_width_ - mw) * 0.5f;
            const float my = (kScreenHeight - mh) * 0.5f;
            DrawPanel(mx, my, mw, mh, 14.0f, 1.5f, PanelTopColor(0.95f), PanelBotColor(0.97f),
                      {acc.r, acc.g, acc.b, 0.85f});
            BeginQuadBatch();
            DrawText(TextOr("ps2ui_info_title", "Disc Information"), mx + 20.0f, my + 16.0f, 18.0f, STYLE_HEADER, acc);
            DrawText(g.title, mx + 20.0f, my + 48.0f, 15.5f, STYLE_BODY_BOLD, PrimaryTextColor(1.0f), mw - 40.0f, 2);
            DrawText(TextOr("ps2ui_info_serial", "Serial: ") + (g.serial.empty() ? "—" : g.serial),
                     mx + 20.0f, my + 98.0f, 13.0f, STYLE_MONO, SecondaryTextColor(0.92f));
            DrawText(TextOr("ps2ui_info_media", "Media: ") + std::string(isBlueCd ? "PlayStation 2 CD-ROM" : "PlayStation 2 DVD-ROM"),
                     mx + 20.0f, my + 122.0f, 13.0f, STYLE_MONO, SecondaryTextColor(0.92f));
            DrawText(TextOr("ps2ui_info_size", "Size: ") + (g.sizeStr.empty() ? "—" : g.sizeStr),
                     mx + 20.0f, my + 146.0f, 13.0f, STYLE_MONO, SecondaryTextColor(0.92f));
            FlushQuadBatch();

            DrawPanel(mx + mw * 0.5f - 70.0f, my + mh - 46.0f, 140.0f, 32.0f, 8.0f, 1.2f,
                      PanelTopColor(0.92f), PanelBotColor(0.95f), {acc.r, acc.g, acc.b, 0.90f});
            BeginQuadBatch();
            DrawText(TextOr("ps2ui_close", "Close"), mx + mw * 0.5f - 70.0f, my + mh - 39.0f, 13.5f, STYLE_BODY_BOLD,
                     PrimaryTextColor(1.0f), 140.0f, 1, 1, 140.0f);
            FlushQuadBatch();
            RegisterHit(mx + mw * 0.5f - 70.0f, my + mh - 46.0f, 140.0f, 32.0f, HIT_OVERLAY_ITEM, 0);
        }
    } else if (overlay_ == OVERLAY_SYSTEM_INFO) {
        SystemStatus st;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            st = status_;
        }
        const float mw = std::min(460.0f, screen_width_ - 40.0f);
        const float mh = 232.0f;
        const float mx = (screen_width_ - mw) * 0.5f;
        const float my = (kScreenHeight - mh) * 0.5f;
        DrawPanel(mx, my, mw, mh, 14.0f, 1.5f, PanelTopColor(0.95f), PanelBotColor(0.97f),
                  {acc.r, acc.g, acc.b, 0.85f});
        BeginQuadBatch();
        DrawText(TextOr("ps2ui_set_sysinfo", "Version Information"), mx + 20.0f, my + 16.0f, 18.0f, STYLE_HEADER, acc);
        DrawText(TextOr("ps2ui_sys_app", "Console: ") + "EmuCoreX PS2  v" + st.appVersion,
                 mx + 20.0f, my + 54.0f, 13.5f, STYLE_MONO, PrimaryTextColor(0.95f));
        DrawText(TextOr("ps2ui_sys_device", "Device: ") + st.deviceName,
                 mx + 20.0f, my + 80.0f, 13.5f, STYLE_MONO, SecondaryTextColor(0.90f));
        DrawText(TextOr("ps2ui_sys_bios", "BIOS ROM: ") + st.biosStatus,
                 mx + 20.0f, my + 106.0f, 13.5f, STYLE_MONO,
                 st.biosValid ? RgbaColor{0.42f, 0.96f, 0.68f, 1.0f} : RgbaColor{1.0f, 0.45f, 0.40f, 1.0f});
        DrawText(TextOr("ps2ui_sys_storage", "Free Storage: ") + st.freeStorage,
                 mx + 20.0f, my + 132.0f, 13.5f, STYLE_MONO, SecondaryTextColor(0.90f));
        FlushQuadBatch();

        DrawPanel(mx + mw * 0.5f - 70.0f, my + mh - 46.0f, 140.0f, 32.0f, 8.0f, 1.2f,
                  PanelTopColor(0.92f), PanelBotColor(0.95f), {acc.r, acc.g, acc.b, 0.90f});
        BeginQuadBatch();
        DrawText(TextOr("ps2ui_close", "Close"), mx + mw * 0.5f - 70.0f, my + mh - 39.0f, 13.5f, STYLE_BODY_BOLD,
                 PrimaryTextColor(1.0f), 140.0f, 1, 1, 140.0f);
        FlushQuadBatch();
        RegisterHit(mx + mw * 0.5f - 70.0f, my + mh - 46.0f, 140.0f, 32.0f, HIT_OVERLAY_ITEM, 0);
    } else if (overlay_ == OVERLAY_TOUCH_GUIDE) {
        const float mw = std::min(480.0f, screen_width_ - 40.0f);
        const float mh = 236.0f;
        const float mx = (screen_width_ - mw) * 0.5f;
        const float my = (kScreenHeight - mh) * 0.5f;
        DrawPanel(mx, my, mw, mh, 14.0f, 1.5f, PanelTopColor(0.95f), PanelBotColor(0.97f),
                  {acc.r, acc.g, acc.b, 0.85f});
        BeginQuadBatch();
        DrawText(TextOr("ps2ui_touch_guide_title", "Touch Controls Guide"), mx + 20.0f, my + 16.0f, 18.0f, STYLE_HEADER, acc);
        DrawText("• " + TextOr("ps2ui_touch_tip_1", "Tap an item to focus, tap again to open or launch"),
                 mx + 20.0f, my + 56.0f, 13.0f, STYLE_BODY, PrimaryTextColor(0.94f), mw - 40.0f, 2);
        DrawText("• " + TextOr("ps2ui_touch_tip_2", "Swipe up or down to scroll lists smoothly"),
                 mx + 20.0f, my + 86.0f, 13.0f, STYLE_BODY, PrimaryTextColor(0.94f), mw - 40.0f, 2);
        DrawText("• " + TextOr("ps2ui_touch_tip_3", "Long-press a game or card for extra options"),
                 mx + 20.0f, my + 116.0f, 13.0f, STYLE_BODY, PrimaryTextColor(0.94f), mw - 40.0f, 2);
        DrawText("• " + TextOr("ps2ui_touch_tip_4", "Two-finger tap goes back anywhere"),
                 mx + 20.0f, my + 146.0f, 13.0f, STYLE_BODY, PrimaryTextColor(0.94f), mw - 40.0f, 2);
        FlushQuadBatch();

        DrawPanel(mx + mw * 0.5f - 75.0f, my + mh - 46.0f, 150.0f, 32.0f, 8.0f, 1.2f,
                  PanelTopColor(0.92f), PanelBotColor(0.95f), {acc.r, acc.g, acc.b, 0.90f});
        BeginQuadBatch();
        DrawText(TextOr("ps2ui_got_it", "Got it"), mx + mw * 0.5f - 75.0f, my + mh - 39.0f, 13.5f, STYLE_BODY_BOLD,
                 PrimaryTextColor(1.0f), 150.0f, 1, 1, 150.0f);
        FlushQuadBatch();
        RegisterHit(mx + mw * 0.5f - 75.0f, my + mh - 46.0f, 150.0f, 32.0f, HIT_OVERLAY_ITEM, 0);
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
    }
    last_frame_tp_ = now;
    has_frame_tp_ = true;

    time_sec_ += dt;
    disc_spin_angle_ += dt * (launching_ ? 7.5f : 1.55f);
    image_decodes_this_frame_ = 0;

    // Smoothly interpolate orbital tilt toward main menu selection
    const float targetTilt = (screen_ == SCREEN_MAIN_MENU) ? (-0.18f + static_cast<float>(main_index_) * 0.18f) : 0.0f;
    orb_tilt_cur_ += (targetTilt - orb_tilt_cur_) * std::min(1.0f, dt * 8.0f);

    // Smooth scroll kinetic damping
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
            screen_fade_t_ = 0.45f;
        }
    }

    if (launching_) {
        launch_t_ += dt;
        if (!launch_dispatched_ && launch_t_ >= 0.72f) {
            launch_dispatched_ = true;
            if (launch_is_save_) {
                HostNotifySaveLaunch(launch_path_, launch_save_slot_);
            } else {
                HostNotifyGameLaunch(launch_path_);
            }
        }
    }

    // Apply dirty image paths invalidated from Kotlin on resume
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

    // Compute viewport matching Aspect Mode (0 = Full Screen, 1 = 16:9, 2 = 4:3)
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
    raster_scale_ = std::clamp(static_cast<float>(view_height_) / kScreenHeight, 1.25f, 2.5f);
    if (std::fabs(raster_scale_ - last_raster_scale_) > 0.15f) {
        ClearTextCache();
        last_raster_scale_ = raster_scale_;
    }

    // Clear full physical surface to deep obsidian void for pillarbox/letterbox if 16:9/4:3 selected
    glViewport(0, 0, surface_width_, surface_height_);
    glClearColor(0.004f, 0.006f, 0.018f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glViewport(view_x_, view_y_, view_width_, view_height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    hit_targets_.clear();

    // Pass 1: PS2 Cosmic Void + Volumetric Mist + 7 Orbiting Crystal Clock Orbs
    float orbCx = 0.0f, orbCy = 0.0f, coreBoost = 0.0f;
    float orbsData[28] = {};
    float trailsData[28] = {};
    ComputeBackgroundOrbs(orbCx, orbCy, coreBoost, orbsData, trailsData);

    glUseProgram(prog_bg_);
    glUniform2f(bg_u_screen_, screen_width_, kScreenHeight);
    glUniform2f(bg_u_orb_center_, orbCx, orbCy);
    glUniform1f(bg_u_time_, time_sec_);
    glUniform1f(bg_u_core_boost_, coreBoost);
    glUniform1i(bg_u_theme_, std::clamp(settings_.theme, 0, kThemeCount - 1));
    glUniform4fv(bg_u_orbs_, 7, orbsData);
    glUniform4fv(bg_u_trails_, 7, trailsData);
    glBindVertexArray(fs_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Smooth cursor lerp
    if (screen_ == SCREEN_MAIN_MENU && !boot_active_ && !launching_) {
        const LayoutMetrics m = ComputeMetrics();
        const float orbCxRef = screen_width_ * 0.285f;
        const float menuX = std::max(screen_width_ * 0.51f, orbCxRef + 150.0f);
        const float rowH = 66.0f;
        const float rowGap = 16.0f;
        const float startY = kScreenHeight * 0.50f - (3.0f * rowH + 2.0f * rowGap) * 0.5f - 6.0f;
        const float targetY = startY + static_cast<float>(main_index_) * (rowH + rowGap) + rowH * 0.5f;
        cursor_x_ = menuX - 18.0f;
        cursor_y_ += (targetY - cursor_y_) * std::min(1.0f, dt * 16.0f);
        (void)m;
    }

    // Pass 2: Active Scene
    const float uiAlpha = EaseOutCubic(screen_fade_t_);
    if (boot_active_) {
        DrawBootIntro(1.0f);
    } else if (launching_) {
        DrawLaunchStage();
    } else {
        DrawStatusBar(uiAlpha);
        if (screen_ == SCREEN_MAIN_MENU) {
            DrawMainMenu(uiAlpha);
        } else if (screen_ == SCREEN_GAMES) {
            DrawGamesScreen(uiAlpha);
        } else if (screen_ == SCREEN_BROWSER) {
            DrawBrowserScreen(uiAlpha);
        } else if (screen_ == SCREEN_SETTINGS) {
            DrawSettingsScreen(uiAlpha);
        }
        DrawBottomBar(uiAlpha);
        DrawOverlay();
    }

    // Pass 3: Optional CRT Scanlines
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
        // Never drop to 0.0 opacity so switching screens never blinks transparent
        screen_fade_t_ = 0.45f;
    }
}

void Ps2Renderer::StartLaunchGame(
    const std::string& path,
    const std::string& title,
    const std::string& serial,
    const std::string& coverPath,
    bool isBlueCd) {
    if (path.empty() || launching_) return;
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
    const bool isBlueCd = (g.fileSizeBytes > 0 && g.fileSizeBytes < 750LL * 1024LL * 1024LL);
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
    if (!games_.empty()) {
        game_index_ = std::clamp(game_index_, 0, static_cast<int>(games_.size()) - 1);
    } else {
        game_index_ = 0;
    }
    if (!memcards_.empty()) {
        mc_card_list_idx_ = std::clamp(mc_card_list_idx_, 0, static_cast<int>(memcards_.size()) - 1);
    } else {
        mc_card_list_idx_ = 0;
    }
    settings_index_ = std::clamp(settings_index_, 0, kSettingsCount - 1);
}

void Ps2Renderer::OnKey(int key) {
    if (launching_) return;
    if (boot_active_) {
        if (key == KEY_CONFIRM || key == KEY_BACK || key == KEY_START) {
            boot_active_ = false;
            screen_fade_t_ = 0.45f;
        }
        return;
    }

    // Active Overlays
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
                        if (delIdx >= 0) {
                            HostNotifyDeleteSave(delIdx);
                        }
                    } else if (overlay_ == OVERLAY_DELETE_CARD_CONFIRM) {
                        int delCard = pending_card_idx_;
                        overlay_ = OVERLAY_NONE;
                        pending_card_idx_ = -1;
                        if (delCard >= 0) {
                            HostNotifyMemoryCardAction(MC_ACTION_DELETE_CARD, mc_card_slot_idx_ + 1, delCard, 0);
                        }
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
            GameEntry g;
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                if (game_index_ >= static_cast<int>(games_.size())) {
                    overlay_ = OVERLAY_NONE;
                    return;
                }
                g = games_[game_index_];
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
                    ActivateGameLaunch(game_index_);
                } else if (hasCont && sel == 1) {
                    const bool isBlueCd = (g.fileSizeBytes > 0 && g.fileSizeBytes < 750LL * 1024LL * 1024LL);
                    StartLaunchQuickSave(g.path, g.latestSaveSlot, g.title, g.serial, g.coverPath, isBlueCd);
                } else {
                    int rem = sel - (hasCont ? 2 : 1);
                    if (rem == 0) {
                        mc_filter_game_path_ = g.path;
                        mc_filter_game_title_ = g.title;
                        mc_tab_ = 1;
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
                        HostNotifyOpenGameSettings(game_index_);
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

        // Info / Touch guide modals
        if (key == KEY_CONFIRM || key == KEY_BACK) {
            overlay_ = OVERLAY_NONE;
            HostPlaySfx(SFX_CANCEL);
        }
        return;
    }

    // Main Menu
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
                SwitchScreen(SCREEN_GAMES);
            } else if (main_index_ == 1) {
                mc_tab_ = 0;
                mc_page_ = 0;
                mc_slot_index_ = 0;
                mc_focus_pane_ = 0;
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

    // Games Screen
    if (screen_ == SCREEN_GAMES) {
        if (key == KEY_BACK) {
            SwitchScreen(SCREEN_MAIN_MENU);
            HostPlaySfx(SFX_CANCEL);
            return;
        }
        int n = 0;
        GameEntry curGame;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            n = static_cast<int>(games_.size());
            if (n > 0) {
                game_index_ = std::clamp(game_index_, 0, n - 1);
                curGame = games_[game_index_];
            }
        }
        if (n == 0) return;

        auto ensureGameVisible = [&]() {
            const float rowStep = 59.0f;
            const float viewH = ComputeMetrics().bodyH - 44.0f;
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
            ActivateGameLaunch(game_index_);
        } else if (key == KEY_SECONDARY) {
            if (curGame.latestSaveSlot >= 0) {
                const bool isBlueCd = (curGame.fileSizeBytes > 0 && curGame.fileSizeBytes < 750LL * 1024LL * 1024LL);
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

    // Browser Screen (Quick Saves + PS2 Memory Cards)
    if (screen_ == SCREEN_BROWSER) {
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
            HostPlaySfx(SFX_MOVE);
            return;
        }
        if (key == KEY_NEXT_TAB) {
            mc_tab_ = (mc_tab_ + 1) % 3;
            mc_focus_pane_ = 0;
            mc_page_ = 0;
            HostPlaySfx(SFX_MOVE);
            return;
        }
        if (mc_tab_ == 2) {
            int cardCount = 0;
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                cardCount = static_cast<int>(memcards_.size());
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
                    HostPlaySfx(SFX_CONFIRM);
                    HostNotifyMemoryCardAction(MC_ACTION_CYCLE_SLOT, mc_card_slot_idx_ + 1, 0, 0);
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
        const int totalPages = std::max(1, (totalFiltered + perPage - 1) / perPage);
        const int curFilteredPos = mc_page_ * perPage + mc_slot_index_;
        const bool hasSave = (curFilteredPos >= 0 && curFilteredPos < totalFiltered);

        if (mc_focus_pane_ == 0) {
            const int col = mc_slot_index_ % cols;
            if (key == KEY_LEFT) {
                if (col > 0) {
                    mc_slot_index_--;
                    HostPlaySfx(SFX_MOVE);
                } else if (mc_page_ > 0) {
                    mc_page_--;
                    mc_slot_index_ += (cols - 1);
                    HostPlaySfx(SFX_MOVE);
                }
            } else if (key == KEY_RIGHT) {
                if (col < cols - 1) {
                    mc_slot_index_++;
                    HostPlaySfx(SFX_MOVE);
                } else {
                    mc_focus_pane_ = 1;
                    mc_action_idx_ = hasSave ? 0 : 2;
                    HostPlaySfx(SFX_MOVE);
                }
            } else if (key == KEY_UP) {
                if (mc_slot_index_ >= cols) {
                    mc_slot_index_ -= cols;
                    HostPlaySfx(SFX_MOVE);
                }
            } else if (key == KEY_DOWN) {
                if (mc_slot_index_ + cols < perPage) {
                    mc_slot_index_ += cols;
                    HostPlaySfx(SFX_MOVE);
                }
            } else if (key == KEY_CONFIRM || key == KEY_START) {
                if (hasSave) {
                    if (key == KEY_START) {
                        StartLaunchSave(curFilteredPos);
                    } else {
                        mc_focus_pane_ = 1;
                        mc_action_idx_ = 0;
                        HostPlaySfx(SFX_CONFIRM);
                    }
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
                if (mc_action_idx_ > 0) {
                    mc_action_idx_--;
                    HostPlaySfx(SFX_MOVE);
                } else {
                    mc_focus_pane_ = 0;
                    HostPlaySfx(SFX_MOVE);
                }
            } else if (key == KEY_RIGHT) {
                if (mc_action_idx_ < 2) {
                    mc_action_idx_++;
                    HostPlaySfx(SFX_MOVE);
                }
            } else if (key == KEY_UP) {
                mc_focus_pane_ = 0;
                HostPlaySfx(SFX_MOVE);
            } else if (key == KEY_CONFIRM) {
                if (mc_action_idx_ == 0 && hasSave) {
                    StartLaunchSave(curFilteredPos);
                } else if (mc_action_idx_ == 1 && hasSave) {
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
        (void)totalPages;
        return;
    }

    // Settings Screen
    if (screen_ == SCREEN_SETTINGS) {
        if (key == KEY_BACK) {
            SwitchScreen(SCREEN_MAIN_MENU);
            HostPlaySfx(SFX_CANCEL);
            return;
        }
        auto ensureSettingVisible = [&]() {
            const float rowStep = 49.0f;
            const float viewH = ComputeMetrics().bodyH - 42.0f;
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
        if (screen_ == SCREEN_GAMES && vx < screen_width_ * 0.56f) {
            touch_drag_target_ = 1;
        } else if (screen_ == SCREEN_SETTINGS) {
            touch_drag_target_ = 2;
        } else if (screen_ == SCREEN_BROWSER && mc_tab_ == 2 && vy > 250.0f) {
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
        screen_fade_t_ = 0.45f;
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

        case HIT_GAME_ROW:
            if (game_index_ == hit->index) {
                ActivateGameLaunch(hit->index);
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
            mc_focus_pane_ = 0;
            mc_page_ = 0;
            HostPlaySfx(SFX_MOVE);
            break;

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
            if (mc_page_ > 0) {
                mc_page_--;
                HostPlaySfx(SFX_MOVE);
            }
            break;

        case HIT_MC_PAGE_NEXT: {
            const int totalFiltered = static_cast<int>(FilteredSaveIndices().size());
            const int cols = (screen_width_ >= 780.0f) ? 4 : 3;
            const int totalPages = std::max(1, (totalFiltered + (cols * 3) - 1) / (cols * 3));
            if (mc_page_ + 1 < totalPages) {
                mc_page_++;
                HostPlaySfx(SFX_MOVE);
            }
            break;
        }

        case HIT_CARD_SLOT:
            mc_card_pane_ = 0;
            if (mc_card_slot_idx_ == hit->index) {
                HostPlaySfx(SFX_CONFIRM);
                HostNotifyMemoryCardAction(MC_ACTION_CYCLE_SLOT, mc_card_slot_idx_ + 1, 0, 0);
            } else {
                mc_card_slot_idx_ = hit->index;
                HostPlaySfx(SFX_MOVE);
            }
            break;

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
            settings_index_ = hit->index;
            CycleSetting(hit->index, 1);
            break;

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
    }
}

void Ps2Renderer::SetStrings(std::unordered_map<std::string, std::string> strings) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    strings_ = std::move(strings);
}

std::string Ps2Renderer::TextOr(const std::string& key, const char* fallback) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto it = strings_.find(key);
    if (it != strings_.end() && !it->second.empty()) {
        return it->second;
    }
    return fallback ? std::string(fallback) : std::string();
}

void Ps2Renderer::SetGames(std::vector<GameEntry> games) {
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        for (const auto& g : games) {
            if (!g.coverPath.empty()) dirty_image_paths_.insert(g.coverPath);
        }
        games_ = std::move(games);
        if (!dirty_image_paths_.empty()) image_cache_dirty_ = true;
    }
    ClampSelectionIndices();
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
    if (kind == 1) {
        boot_active_ = false;
        screen_fade_t_ = 0.45f;
    } else if (settings_.bootAnimMode == 2) {
        boot_active_ = false;
        screen_fade_t_ = 0.45f;
    } else {
        boot_active_ = true;
        boot_t_ = 0.0f;
        boot_sfx_played_ = false;
        boot_duration_ = (settings_.bootAnimMode == 1) ? 1.15f : 2.65f;
    }
}

std::vector<int> Ps2Renderer::GetState() const {
    return {screen_, main_index_, game_index_, mc_slot_index_};
}

void Ps2Renderer::SetState(int screen, int mainIndex, int gameIndex, int saveIndex) {
    screen_ = std::clamp(screen, 0, 3);
    main_index_ = std::clamp(mainIndex, 0, 2);
    game_index_ = std::max(0, gameIndex);
    mc_slot_index_ = std::max(0, saveIndex);
    ClampSelectionIndices();
}

} // namespace ps2ui
