#pragma once

#include <GLES3/gl3.h>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ps2ui {

// Host bridge callbacks implemented in ps2_jni.cpp
void HostNotifyGameLaunch(const std::string& gamePath);
void HostNotifySaveLaunch(const std::string& gamePath, int slot);
void HostNotifyDeleteSave(int saveIndex);
void HostNotifyMemoryCardAction(int action, int slot, int cardIndex, int sizeMb);
void HostNotifyOpenGameSettings(int gameIndex);
void HostNotifySettingsChanged(const int32_t* values, int count);
void HostNotifyExitRequested();
void HostPlaySfx(int kind);

bool HostRasterizeText(
    const std::string& text,
    float virtualFontSize,
    int style,
    float maxVirtualWidth,
    int maxLines,
    int align,
    float rasterScale,
    int& outTexW,
    int& outTexH,
    float& outLogW,
    float& outLogH,
    std::vector<uint8_t>& outRgba);

bool HostDecodeImage(
    const std::string& path,
    int maxDim,
    int& outW,
    int& outH,
    std::vector<uint8_t>& outRgba);

struct GameEntry {
    std::string title;
    std::string path;
    std::string serial;
    std::string coverPath;
    std::string sizeStr;
    std::string dateStr;
    int64_t fileSizeBytes = 0;
    int saveCount = 0;
    int latestSaveSlot = -1;
    std::string compatStatus;
    bool isArcade = false;
};

struct SaveEntry {
    std::string title;
    std::string slotLabel;
    int slotNum = 0;
    std::string dateStr;
    std::string sizeStr;
    std::string serial;
    std::string previewPath;
    std::string savePath;
    std::string gamePath;
};

struct MemoryCardEntry {
    std::string name;
    std::string sizeStr;
    std::string dateStr;
    bool isFolder = false;
    bool isDefault = false;
};

struct McSaveBlockEntry {
    std::string dirName;
    std::string title;
    int sizeKb = 0;
    std::string dateStr;
    int colorRgb = 0x48A6FF;
    std::vector<float> iconMesh; // 9 floats/vertex: x,y,z, nx,ny,nz, r,g,b
};

struct McSlotContents {
    std::string cardName;
    int freeKb = 0;
    int totalKb = 0;
    std::vector<McSaveBlockEntry> saves;
};

struct ShellSettings {
    int theme = 0;           // 0..5: PS2 Classic, Midnight Silver, Emerald Matrix, Crimson Sakura, Amethyst Nebula, Solar Gold
    int aspectMode = 0;      // 0 = Auto Full Screen, 1 = 16:9, 2 = 4:3
    int orbsMode = 0;        // 0 = Normal, 1 = Calm, 2 = Static
    int towersMode = 0;      // 0 = Dynamic, 1 = Subtle, 2 = Off
    int scanlinesMode = 0;   // 0 = Off, 1 = Subtle, 2 = Classic
    int bootAnimMode = 0;    // 0 = Full, 1 = Fast, 2 = Off
    bool keyTone = true;
    bool ambientSound = true;
    bool swapConfirm = false;
    bool touchControls = true;
    bool time24h = true;
    int dateFormat = 0;      // 0 = DD/MM/YYYY, 1 = MM/DD/YYYY, 2 = YYYY/MM/DD
    bool keepScreenOn = true;
};

struct SystemStatus {
    int batteryLevel = 100;
    bool batteryCharging = false;
    bool wifiConnected = false;
    bool bluetoothConnected = false;
    bool audioMuted = false;
    std::string freeStorage;
    std::string deviceName;
    std::string appVersion;
    std::string biosStatus;
    bool biosValid = true;
};

enum ScreenId {
    SCREEN_MAIN_MENU = 0,
    SCREEN_GAMES = 1,
    SCREEN_BROWSER = 2,
    SCREEN_SETTINGS = 3
};

enum OverlayId {
    OVERLAY_NONE = 0,
    OVERLAY_EXIT_CONFIRM = 1,
    OVERLAY_GAME_OPTIONS = 2,
    OVERLAY_GAME_INFO = 3,
    OVERLAY_DELETE_SAVE_CONFIRM = 4,
    OVERLAY_CREATE_CARD = 5,
    OVERLAY_CARD_ACTIONS = 6,
    OVERLAY_DELETE_CARD_CONFIRM = 7,
    OVERLAY_RESTORE_CONFIRM = 8,
    OVERLAY_SYSTEM_INFO = 9,
    OVERLAY_TOUCH_GUIDE = 10
};

enum KeyCode {
    KEY_UP = 0,
    KEY_DOWN = 1,
    KEY_LEFT = 2,
    KEY_RIGHT = 3,
    KEY_CONFIRM = 4,
    KEY_BACK = 5,
    KEY_OPTIONS = 6,
    KEY_SECONDARY = 7,
    KEY_PREV_TAB = 8,
    KEY_NEXT_TAB = 9,
    KEY_START = 10
};

enum McActionCode {
    MC_ACTION_CYCLE_SLOT = 1,
    MC_ACTION_SWAP_SLOTS = 2,
    MC_ACTION_EJECT_SLOT = 3,
    MC_ACTION_CREATE_PS2 = 4,
    MC_ACTION_CREATE_FOLDER = 5,
    MC_ACTION_MOUNT_CARD = 6,
    MC_ACTION_DELETE_CARD = 7,
    MC_ACTION_DUPLICATE_CARD = 8
};

enum SfxKind {
    SFX_MOVE = 0,
    SFX_CONFIRM = 1,
    SFX_CANCEL = 2,
    SFX_ERROR = 3,
    SFX_BOOT_INTRO = 4,
    SFX_DISC_LAUNCH = 5
};

enum TextStyle {
    STYLE_HEADER = 0,
    STYLE_MENU = 1,
    STYLE_BODY = 2,
    STYLE_BODY_BOLD = 3,
    STYLE_MONO = 4,
    STYLE_PS2_LOGO = 5
};

struct RgbaColor {
    float r, g, b, a;
};

class Ps2Renderer {
public:
    static constexpr float kScreenHeight = 540.0f;

    Ps2Renderer() = default;
    ~Ps2Renderer() = default;

    bool Init();
    void Shutdown();
    void Resize(int width, int height);
    void SetUiScale(float scale);
    void Draw();

    void OnKey(int key);
    void OnTouchDown(float normX, float normY);
    void OnTouchMove(float normX, float normY);
    void OnTouchUp(float normX, float normY);
    void OnTouchCancel();
    void OnTouchTwoFingerBack();
    void OnTouchLongPress(float normX, float normY);

    void SetStrings(std::unordered_map<std::string, std::string> strings);
    void SetGames(std::vector<GameEntry> games);
    void SetSaves(std::vector<SaveEntry> saves);
    void SetMemoryCards(
        std::string slot1Name,
        std::string slot1Meta,
        std::string slot1Status,
        std::string slot2Name,
        std::string slot2Meta,
        std::string slot2Status,
        std::vector<MemoryCardEntry> cards);
    void SetMemoryCardContents(
        int slot,
        std::string cardName,
        int freeKb,
        int totalKb,
        std::vector<McSaveBlockEntry> saves);
    void SetSettings(const ShellSettings& settings);
    void SetSystemStatus(const SystemStatus& status);
    void SetTouchMode(bool active, bool showTutorial);
    void SetGamesLoading(bool loading);
    void SetSavesLoading(bool loading);
    void TriggerTransition(int kind); // 0 = initial/boot, 1 = returned from game

    std::vector<int> GetState() const;
    void SetState(int screen, int mainIndex, int gameIndex, int saveIndex);
    void SelectGameByPath(const std::string& path);

private:
    struct Vertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };

    struct TowerVertex {
        float x, y, z;
        float nx, ny, nz;
        float hNorm;
        float r, g, b, a;
    };

    struct Mesh3DVertex {
        float x, y, z;
        float nx, ny, nz;
        float r, g, b, a;
        float spec;
    };

    struct TowerDef {
        float x = 0.0f;
        float z = 0.0f;
        float width = 1.0f;
        float depth = 1.0f;
        float height = 3.0f;
        float phase = 0.0f;
        float brightness = 1.0f;
    };

    struct TextCacheEntry {
        GLuint tex = 0;
        float logW = 0.0f;
        float logH = 0.0f;
        std::list<std::string>::iterator lruIt{};
    };

    struct ImageCacheEntry {
        GLuint tex = 0;
        int w = 0;
        int h = 0;
        bool attempted = false;
        std::list<std::string>::iterator lruIt{};
    };

    enum HitKind {
        HIT_MAIN_ROW = 1,
        HIT_GAME_ROW = 2,
        HIT_GAME_START = 3,
        HIT_GAME_CONTINUE = 4,
        HIT_GAME_OPTIONS = 5,
        HIT_MC_TAB = 6,
        HIT_MC_BLOCK = 7,
        HIT_MC_ACTION = 8,
        HIT_MC_PAGE_PREV = 9,
        HIT_MC_PAGE_NEXT = 10,
        HIT_CARD_SLOT = 11,
        HIT_CARD_ROW = 12,
        HIT_CARD_TOOL = 13,
        HIT_SETTING_ROW = 14,
        HIT_SETTING_LEFT = 15,
        HIT_SETTING_RIGHT = 16,
        HIT_OVERLAY_ITEM = 17,
        HIT_OVERLAY_BACKDROP = 18,
        HIT_BOTTOM_ACTION = 19,
        HIT_MC_OPEN_SLOT = 20,
        HIT_MC_INNER_ICON = 21,
        HIT_MC_INNER_BACK = 22,
        HIT_MC_INNER_PREV = 23,
        HIT_MC_INNER_NEXT = 24,
        HIT_GAME_TAB = 25
    };

    struct HitTarget {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        int kind = 0;
        int index = 0;
    };

    struct LayoutMetrics {
        float padX = 32.0f;
        float topBarH = 34.0f;
        float bottomBarH = 36.0f;
        float bodyTop = 48.0f;
        float bodyBot = 496.0f;
        float bodyH = 448.0f;
    };

    // GL Setup / Teardown
    bool CompileShaders();
    GLuint CompileProgram(const char* vsSrc, const char* fsSrc);
    void ClearTextCache();
    void ClearImageCache();
    void EvictTextCacheIfNeeded();
    void EvictImageCacheIfNeeded();

    // Text & Images
    std::string TextOr(const std::string& key, const char* fallback) const;
    const TextCacheEntry& GetOrCreateText(
        const std::string& text,
        float fontSize,
        int style,
        float maxWidth,
        int maxLines = 1,
        int align = 0);
    const ImageCacheEntry& GetOrCreateImage(const std::string& path, int maxDim = 512);
    void DrawText(
        const std::string& text,
        float x,
        float y,
        float fontSize,
        int style,
        const RgbaColor& color,
        float maxWidth = 0.0f,
        int maxLines = 1,
        int align = 0,
        float boxW = 0.0f);
    void MeasureText(
        const std::string& text,
        float fontSize,
        int style,
        float maxWidth,
        int maxLines,
        float& outW,
        float& outH);

    // Primitives & 3D Meshes
    void BeginQuadBatch();
    void FlushQuadBatch();
    void PushQuad(
        GLuint tex,
        float x, float y, float w, float h,
        const RgbaColor& cTop,
        const RgbaColor& cBot,
        float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f);
    void PushQuad(GLuint tex, float x, float y, float w, float h, const RgbaColor& c);
    void PushTriangle(
        float x0, float y0, const RgbaColor& c0,
        float x1, float y1, const RgbaColor& c1,
        float x2, float y2, const RgbaColor& c2);

    void DrawPanel(
        float x, float y, float w, float h,
        float radius,
        float borderPx,
        const RgbaColor& topCol,
        const RgbaColor& botCol,
        const RgbaColor& borderCol,
        const RgbaColor& glowCol = {0.0f, 0.0f, 0.0f, 0.0f});
    void DrawFocusBeam(float x, float y, float w, float h, const RgbaColor& col, float alpha);
    void DrawHeaderBar(const std::string& title, float alpha = 1.0f);
    void DrawOpticalDisc3D(
        float cx, float cy, float radius,
        float angleRad, float tiltXRad, float tiltYRad,
        int discType, float alpha);

    // High-Visibility Animated PS2 Focus Cursor & Grid Brackets
    void SetCursorTarget(float x, float y);
    void DrawAnimatedCursor(float x, float y, float size, float alpha);
    void DrawAnimatedGridBrackets(float x, float y, float w, float h, const RgbaColor& acc, float alpha);
    void DrawImageAspectFit(
        const ImageCacheEntry& img,
        float x, float y, float w, float h,
        float alpha,
        bool fillCrop = false);

    // 3D Extruded Memory Card & Save Icon Meshes
    void BuildMemoryCardMeshes();
    void Draw3DMeshInRect(
        const std::vector<Mesh3DVertex>& verts,
        float vx, float vy, float vw, float vh,
        float rotXDeg, float rotYDeg, float rotZDeg,
        float camDist,
        float scale,
        const RgbaColor& accentCol,
        float alpha,
        bool isGhost);
    void DrawMemoryCard3DViewport(
        float vx, float vy, float vw, float vh,
        float rotXDeg, float rotYDeg,
        bool focused, bool isEmpty, bool isFolder, float alpha);
    void DrawSaveBlockIcon3D(
        float vx, float vy, float vw, float vh,
        const McSaveBlockEntry& save,
        bool focused, float alpha);

    // 3D Towers City & 3D Orbiting Crystal Orbs w/ Curved Ribbons
    void RebuildTowersLayout();
    void DrawTowers3D(
        const float* mvp,
        float camX, float camY, float camZ,
        float riseScale,
        const RgbaColor& fogCol,
        float fogDensity,
        float alpha,
        float silhouetteMode = 0.0f);
    void UpdateAndDrawOrbs3D(float dt);

    // Screen Drawing
    LayoutMetrics ComputeMetrics() const;
    void DrawBootIntro(float alpha);
    void DrawStatusBar(float alpha);
    void DrawBottomBar(float alpha);
    void DrawMainMenu(float alpha);
    void DrawGamesScreen(float alpha);
    void DrawBrowserScreen(float alpha);
    void DrawBrowserInsideCard(float alpha, const LayoutMetrics& m);
    void DrawSettingsScreen(float alpha);
    void DrawLaunchStage();
    void DrawOverlay();
    void DrawConfirmDialog(
        const std::string& title,
        const std::string& message,
        const std::string& yesText,
        const std::string& noText,
        int focusedBtn);
    void DrawListModal(
        const std::string& title,
        const std::vector<std::string>& items,
        int selectedIdx,
        float modalWidth = 370.0f);
    void DrawScanlinesPass();

    // State & Navigation Helpers
    void RegisterHit(float x, float y, float w, float h, int kind, int index = 0);
    const HitTarget* FindHit(float vx, float vy) const;
    bool SurfaceNormToVirtual(float normX, float normY, float& outVx, float& outVy) const;
    void SwitchScreen(int nextScreen);
    void StartLaunchGame(const std::string& path, const std::string& title, const std::string& serial, const std::string& coverPath, bool isBlueCd);
    void StartLaunchQuickSave(const std::string& path, int slot, const std::string& title, const std::string& serial, const std::string& coverPath, bool isBlueCd);
    void StartLaunchSave(int filteredIndex);
    void ActivateGameLaunch(int gameIdx);
    void CycleSetting(int row, int delta);
    void PushSettingsToHost();
    std::vector<int> FilteredGameIndices() const;
    int SelectedMasterGameIndex() const;
    std::vector<int> FilteredSaveIndices() const;
    void ClampSelectionIndices();
    void SnapGameScrollToSelected();
    std::string FormatClockText() const;
    std::string FormatCurrentDateText() const;

    // Theme Palette Helpers
    RgbaColor PanelTopColor(float alpha = 0.90f) const;
    RgbaColor PanelBotColor(float alpha = 0.94f) const;
    RgbaColor PanelBorderColor(float alpha = 0.65f) const;
    RgbaColor AccentColor(float alpha = 1.0f) const;
    RgbaColor SecondaryAccentColor(float alpha = 1.0f) const;
    RgbaColor PrimaryTextColor(float alpha = 1.0f) const;
    RgbaColor SecondaryTextColor(float alpha = 0.82f) const;

    // GL Resources
    bool gl_ready_ = false;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint fs_vao_ = 0;
    GLuint fs_vbo_ = 0;
    GLuint tower_vao_ = 0;
    GLuint tower_vbo_ = 0;
    GLuint mesh3d_vao_ = 0;
    GLuint mesh3d_vbo_ = 0;
    GLuint white_tex_ = 0;

    GLuint prog_quad_ = 0;
    GLint quad_u_screen_ = -1;
    GLint quad_u_texture_ = -1;

    GLuint prog_bg_ = 0;
    GLint bg_u_screen_ = -1;
    GLint bg_u_mist_center_ = -1;
    GLint bg_u_time_ = -1;
    GLint bg_u_mist_strength_ = -1;
    GLint bg_u_theme_ = -1;

    GLuint prog_tower_ = 0;
    GLint twr_u_mvp_ = -1;
    GLint twr_u_cam_pos_ = -1;
    GLint twr_u_accent_col_ = -1;
    GLint twr_u_fog_col_ = -1;
    GLint twr_u_fog_density_ = -1;
    GLint twr_u_alpha_ = -1;
    GLint twr_u_silhouette_ = -1;
    GLint twr_u_time_ = -1;

    GLuint prog_ribbon_ = 0;
    GLint rib_u_screen_ = -1;

    GLuint prog_orb_ = 0;
    GLint orb_u_screen_ = -1;

    GLuint prog_panel_ = 0;
    GLint pan_u_screen_ = -1;
    GLint pan_u_rect_ = -1;
    GLint pan_u_size_ = -1;
    GLint pan_u_radius_ = -1;
    GLint pan_u_border_px_ = -1;
    GLint pan_u_top_col_ = -1;
    GLint pan_u_bot_col_ = -1;
    GLint pan_u_border_col_ = -1;
    GLint pan_u_glow_col_ = -1;

    GLuint prog_disc_ = 0;
    GLint disc_u_mvp_ = -1;
    GLint disc_u_radius_ = -1;
    GLint disc_u_angle_ = -1;
    GLint disc_u_alpha_ = -1;
    GLint disc_u_disc_type_ = -1;

    GLuint prog_mesh3d_ = 0;
    GLint m3d_u_mvp_ = -1;
    GLint m3d_u_model_ = -1;
    GLint m3d_u_cam_pos_ = -1;
    GLint m3d_u_accent_col_ = -1;
    GLint m3d_u_alpha_ = -1;
    GLint m3d_u_ghost_ = -1;

    GLuint prog_launch_ = 0;
    GLint lch_u_screen_ = -1;
    GLint lch_u_progress_ = -1;
    GLint lch_u_time_ = -1;
    GLint lch_u_accent_col_ = -1;

    GLuint prog_scan_ = 0;
    GLint scan_u_viewport_ = -1;
    GLint scan_u_strength_ = -1;

    std::vector<Vertex> batch_verts_;
    std::vector<Vertex> ribbon_verts_;
    std::vector<Vertex> orb_sprite_verts_;
    std::vector<TowerVertex> tower_verts_;
    std::vector<TowerDef> towers_;
    std::vector<Mesh3DVertex> memcard_solid_verts_;
    std::vector<Mesh3DVertex> memcard_folder_verts_;
    std::vector<Mesh3DVertex> memcard_ghost_verts_;
    std::vector<Mesh3DVertex> save_cube_verts_;
    std::vector<Mesh3DVertex> temp_mesh_verts_;
    GLuint batch_tex_ = 0;

    // Viewport & Scaling
    int surface_width_ = 1280;
    int surface_height_ = 720;
    float screen_width_ = 960.0f;
    int view_x_ = 0;
    int view_y_ = 0;
    int view_width_ = 1280;
    int view_height_ = 720;
    float raster_scale_ = 2.0f;
    float last_raster_scale_ = 0.0f;
    float ui_scale_ = 1.0f;

    // Timing & Spring Animations
    std::chrono::steady_clock::time_point last_frame_tp_;
    bool has_frame_tp_ = false;
    float time_sec_ = 0.0f;
    float smooth_dt_ = 0.016f;
    float screen_fade_t_ = 1.0f;

    // Spring-Animated PS2 Cursor
    float cursor_x_ = 0.0f;
    float cursor_y_ = 0.0f;
    float cursor_target_x_ = 0.0f;
    float cursor_target_y_ = 0.0f;
    float cursor_trail_x_ = 0.0f;
    float cursor_trail_y_ = 0.0f;
    float cursor_flash_t_ = 0.0f;
    bool cursor_pos_init_ = false;

    // 3D Orbital Vortex & Camera Fly-Through Transitions
    float disc_spin_angle_ = 0.0f;
    float disc_slide_t_ = 1.0f;
    int last_disc_game_idx_ = -1;
    float orb_tilt_cur_ = 0.0f;
    float orb_center_cur_x_ = 280.0f;
    float orb_center_cur_y_ = 270.0f;
    float orb_scale_cur_ = 1.0f;
    float orb_ring_blend_ = 0.0f;
    float orb_swoop_t_ = 0.0f;
    int orb_swoop_dir_ = 1;
    float orb_flash_t_ = 0.0f;
    double orb_phase_acc_ = 0.0;
    double orb_rot_time_ = 0.0;
    float mist_strength_cur_ = 0.16f;
    bool orb_pos_init_ = false;

    bool boot_active_ = false;
    float boot_t_ = 0.0f;
    float boot_duration_ = 3.8f;
    bool boot_sfx_played_ = false;

    bool launching_ = false;
    bool launch_is_save_ = false;
    bool launch_dispatched_ = false;
    bool launch_is_blue_cd_ = false;
    float launch_t_ = 0.0f;
    std::string launch_title_;
    std::string launch_serial_;
    std::string launch_cover_;
    std::string launch_path_;
    int launch_save_slot_ = 0;

    // Navigation State
    int screen_ = SCREEN_MAIN_MENU;
    int return_screen_ = SCREEN_MAIN_MENU;
    int overlay_ = OVERLAY_NONE;
    int main_index_ = 0;
    int game_index_ = 0;
    int game_tab_ = 0;        // 0 = PlayStation 2, 1 = Namco Arcade
    bool has_arcade_games_ = false;
    float game_scroll_y_ = 0.0f;
    float game_scroll_target_y_ = 0.0f;
    int pending_restore_game_index_ = -1;
    std::string pending_restore_game_path_;
    std::string selected_game_path_;

    int mc_tab_ = 0;          // 0 = Quick Saves, 1 = Game Saves, 2 = Memory Cards
    int mc_page_ = 0;
    int mc_slot_index_ = 0;
    int mc_focus_pane_ = 0;   // 0 = Grid / Cards, 1 = Action Buttons
    int mc_action_idx_ = 0;
    int mc_card_slot_idx_ = 0; // 0 = Slot 1, 1 = Slot 2
    int mc_card_pane_ = 0;   // 0 = Top Slots (1 & 2), 1 = Available Card List
    int mc_card_list_idx_ = 0;
    float mc_card_scroll_y_ = 0.0f;
    float mc_card_scroll_target_y_ = 0.0f;
    int mc_open_slot_ = 0;    // 0 = Slot overview, 1 = inside Slot 1, 2 = inside Slot 2
    int mc_inner_idx_ = 0;
    int mc_inner_page_ = 0;
    float mc_inner_zoom_t_ = 0.0f;
    std::string mc_filter_game_path_;
    std::string mc_filter_game_title_;

    int settings_index_ = 0;
    float settings_scroll_y_ = 0.0f;
    float settings_scroll_target_y_ = 0.0f;

    int dialog_btn_ = 1;
    int options_index_ = 0;
    int create_card_idx_ = 0;
    int card_action_menu_idx_ = 0;
    int pending_delete_save_idx_ = -1;
    int pending_card_idx_ = -1;

    // Touch Tracking
    bool touch_mode_active_ = false;
    bool touch_dragging_ = false;
    bool touch_moved_ = false;
    float touch_down_vx_ = 0.0f;
    float touch_down_vy_ = 0.0f;
    float touch_last_vy_ = 0.0f;
    int touch_drag_target_ = 0;
    std::vector<HitTarget> hit_targets_;

    // Host Data
    mutable std::mutex data_mutex_;
    mutable std::mutex strings_mutex_;
    std::unordered_map<std::string, std::string> strings_;
    std::vector<GameEntry> games_;
    std::vector<SaveEntry> saves_;
    std::string slot1_card_name_;
    std::string slot1_card_meta_;
    std::string slot1_card_status_;
    std::string slot2_card_name_;
    std::string slot2_card_meta_;
    std::string slot2_card_status_;
    McSlotContents slot1_contents_;
    McSlotContents slot2_contents_;
    std::vector<MemoryCardEntry> memcards_;
    ShellSettings settings_;
    SystemStatus status_;
    bool games_loading_ = false;
    bool saves_loading_ = false;
    bool image_cache_dirty_ = false;
    std::unordered_set<std::string> dirty_image_paths_;
    int image_decodes_this_frame_ = 0;

    // Cached once-per-second status bar string
    std::time_t cached_status_sec_ = 0;
    int cached_status_battery_ = -1;
    bool cached_status_charging_ = false;
    bool cached_status_time24h_ = true;
    int cached_status_date_fmt_ = -1;
    std::string cached_status_line_;

    // LRU Texture Caches (GL thread only)
    std::unordered_map<std::string, TextCacheEntry> text_cache_;
    std::list<std::string> text_lru_;
    std::unordered_map<std::string, ImageCacheEntry> image_cache_;
    std::list<std::string> image_lru_;
};

} // namespace ps2ui
