#include "ps2_renderer.h"

#include <algorithm>
#include <cstring>
#include <jni.h>
#include <memory>
#include <string>
#include <vector>

#if defined(PS2UI_DISABLE_LOGS) || defined(NDEBUG)
#define LOGE(...) ((void)0)
#else
#include <android/log.h>
#define LOG_TAG "Ps2ShellJNI"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#endif

namespace {

JavaVM* g_vm = nullptr;
jclass g_host_cls = nullptr;
jmethodID g_on_game_launch = nullptr;
jmethodID g_on_save_launch = nullptr;
jmethodID g_on_delete_save = nullptr;
jmethodID g_on_mc_action = nullptr;
jmethodID g_on_open_game_settings = nullptr;
jmethodID g_on_settings_changed = nullptr;
jmethodID g_on_exit_requested = nullptr;

jclass g_text_cls = nullptr;
jmethodID g_rasterize_text = nullptr;

jclass g_img_cls = nullptr;
jmethodID g_decode_rgba = nullptr;

jclass g_audio_cls = nullptr;
jmethodID g_play_sfx = nullptr;

std::unique_ptr<ps2ui::Ps2Renderer> g_renderer;

JNIEnv* GetEnv() {
    if (!g_vm) return nullptr;
    JNIEnv* env = nullptr;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return nullptr;
        }
    }
    return env;
}

jstring Utf8ToJstring(JNIEnv* env, const std::string& utf8) {
    if (utf8.empty()) {
        return env->NewString(reinterpret_cast<const jchar*>(u""), 0);
    }
    std::vector<jchar> utf16;
    utf16.reserve(utf8.size());
    size_t i = 0;
    const size_t n = utf8.size();
    while (i < n) {
        uint32_t cp = 0;
        uint8_t c = static_cast<uint8_t>(utf8[i]);
        if (c < 0x80) {
            cp = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < n) {
            cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(utf8[i + 1]) & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < n) {
            cp = ((c & 0x0F) << 12) |
                 ((static_cast<uint8_t>(utf8[i + 1]) & 0x3F) << 6) |
                 (static_cast<uint8_t>(utf8[i + 2]) & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < n) {
            cp = ((c & 0x07) << 18) |
                 ((static_cast<uint8_t>(utf8[i + 1]) & 0x3F) << 12) |
                 ((static_cast<uint8_t>(utf8[i + 2]) & 0x3F) << 6) |
                 (static_cast<uint8_t>(utf8[i + 3]) & 0x3F);
            i += 4;
        } else {
            cp = 0xFFFD;
            i += 1;
        }
        if (cp <= 0xFFFF) {
            utf16.push_back(static_cast<jchar>(cp));
        } else {
            cp -= 0x10000;
            utf16.push_back(static_cast<jchar>(0xD800 + ((cp >> 10) & 0x3FF)));
            utf16.push_back(static_cast<jchar>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return env->NewString(utf16.data(), static_cast<jsize>(utf16.size()));
}

std::string JstringToUtf8(JNIEnv* env, jstring jstr) {
    if (!jstr) return {};
    const jchar* chars = env->GetStringChars(jstr, nullptr);
    const jsize len = env->GetStringLength(jstr);
    if (!chars || len <= 0) {
        if (chars) env->ReleaseStringChars(jstr, chars);
        return {};
    }
    std::string out;
    out.reserve(static_cast<size_t>(len) * 2);
    for (jsize i = 0; i < len; ++i) {
        uint32_t cp = chars[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len) {
            uint32_t low = chars[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                ++i;
            }
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    env->ReleaseStringChars(jstr, chars);
    return out;
}

std::vector<std::string> ReadStringArray(JNIEnv* env, jobjectArray arr) {
    std::vector<std::string> out;
    if (!arr) return out;
    const jsize n = env->GetArrayLength(arr);
    out.reserve(static_cast<size_t>(n));
    for (jsize i = 0; i < n; ++i) {
        auto* elem = static_cast<jstring>(env->GetObjectArrayElement(arr, i));
        out.push_back(JstringToUtf8(env, elem));
        if (elem) env->DeleteLocalRef(elem);
    }
    return out;
}

std::vector<int> ReadIntArray(JNIEnv* env, jintArray arr) {
    std::vector<int> out;
    if (!arr) return out;
    const jsize n = env->GetArrayLength(arr);
    out.resize(static_cast<size_t>(n));
    if (n > 0) {
        env->GetIntArrayRegion(arr, 0, n, reinterpret_cast<jint*>(out.data()));
    }
    return out;
}

std::vector<int64_t> ReadLongArray(JNIEnv* env, jlongArray arr) {
    std::vector<int64_t> out;
    if (!arr) return out;
    const jsize n = env->GetArrayLength(arr);
    out.resize(static_cast<size_t>(n));
    if (n > 0) {
        env->GetLongArrayRegion(arr, 0, n, reinterpret_cast<jlong*>(out.data()));
    }
    return out;
}

std::vector<uint8_t> ReadBoolArray(JNIEnv* env, jbooleanArray arr) {
    std::vector<uint8_t> out;
    if (!arr) return out;
    const jsize n = env->GetArrayLength(arr);
    out.resize(static_cast<size_t>(n));
    if (n > 0) {
        env->GetBooleanArrayRegion(arr, 0, n, reinterpret_cast<jboolean*>(out.data()));
    }
    return out;
}

} // namespace

namespace ps2ui {

void HostNotifyGameLaunch(const std::string& gamePath) {
    JNIEnv* env = GetEnv();
    if (!env || !g_host_cls || !g_on_game_launch) return;
    jstring jpath = Utf8ToJstring(env, gamePath);
    env->CallStaticVoidMethod(g_host_cls, g_on_game_launch, jpath);
    env->DeleteLocalRef(jpath);
}

void HostNotifySaveLaunch(const std::string& gamePath, int slot) {
    JNIEnv* env = GetEnv();
    if (!env || !g_host_cls || !g_on_save_launch) return;
    jstring jpath = Utf8ToJstring(env, gamePath);
    env->CallStaticVoidMethod(g_host_cls, g_on_save_launch, jpath, static_cast<jint>(slot));
    env->DeleteLocalRef(jpath);
}

void HostNotifyDeleteSave(int saveIndex) {
    JNIEnv* env = GetEnv();
    if (!env || !g_host_cls || !g_on_delete_save) return;
    env->CallStaticVoidMethod(g_host_cls, g_on_delete_save, static_cast<jint>(saveIndex));
}

void HostNotifyMemoryCardAction(int action, int slot, int cardIndex, int sizeMb) {
    JNIEnv* env = GetEnv();
    if (!env || !g_host_cls || !g_on_mc_action) return;
    env->CallStaticVoidMethod(
        g_host_cls,
        g_on_mc_action,
        static_cast<jint>(action),
        static_cast<jint>(slot),
        static_cast<jint>(cardIndex),
        static_cast<jint>(sizeMb));
}

void HostNotifyOpenGameSettings(int gameIndex) {
    JNIEnv* env = GetEnv();
    if (!env || !g_host_cls || !g_on_open_game_settings) return;
    env->CallStaticVoidMethod(g_host_cls, g_on_open_game_settings, static_cast<jint>(gameIndex));
}

void HostNotifySettingsChanged(const int32_t* values, int count) {
    JNIEnv* env = GetEnv();
    if (!env || !g_host_cls || !g_on_settings_changed || !values || count <= 0) return;
    jintArray jarr = env->NewIntArray(count);
    if (!jarr) return;
    env->SetIntArrayRegion(jarr, 0, count, reinterpret_cast<const jint*>(values));
    env->CallStaticVoidMethod(g_host_cls, g_on_settings_changed, jarr);
    env->DeleteLocalRef(jarr);
}

void HostNotifyExitRequested() {
    JNIEnv* env = GetEnv();
    if (!env || !g_host_cls || !g_on_exit_requested) return;
    env->CallStaticVoidMethod(g_host_cls, g_on_exit_requested);
}

void HostPlaySfx(int kind) {
    JNIEnv* env = GetEnv();
    if (!env || !g_audio_cls || !g_play_sfx) return;
    env->CallStaticVoidMethod(g_audio_cls, g_play_sfx, static_cast<jint>(kind));
}

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
    std::vector<uint8_t>& outRgba) {
    JNIEnv* env = GetEnv();
    if (!env || !g_text_cls || !g_rasterize_text) return false;
    jstring jtext = Utf8ToJstring(env, text);
    auto* bytes = static_cast<jbyteArray>(env->CallStaticObjectMethod(
        g_text_cls,
        g_rasterize_text,
        jtext,
        static_cast<jfloat>(virtualFontSize),
        static_cast<jint>(style),
        static_cast<jfloat>(maxVirtualWidth),
        static_cast<jint>(maxLines),
        static_cast<jint>(align),
        static_cast<jfloat>(rasterScale)));
    env->DeleteLocalRef(jtext);
    if (!bytes) return false;

    const jsize len = env->GetArrayLength(bytes);
    if (len < 16) {
        env->DeleteLocalRef(bytes);
        return false;
    }

    std::vector<uint8_t> buf(static_cast<size_t>(len));
    env->GetByteArrayRegion(bytes, 0, len, reinterpret_cast<jbyte*>(buf.data()));
    env->DeleteLocalRef(bytes);

    int32_t hdr[4] = {};
    std::memcpy(hdr, buf.data(), 16);
    outTexW = hdr[0];
    outTexH = hdr[1];
    outLogW = static_cast<float>(hdr[2]) / 100.0f;
    outLogH = static_cast<float>(hdr[3]) / 100.0f;

    const size_t expected = 16 + static_cast<size_t>(outTexW) * static_cast<size_t>(outTexH) * 4;
    if (outTexW <= 0 || outTexH <= 0 || buf.size() < expected) return false;

    outRgba.assign(buf.begin() + 16, buf.begin() + static_cast<ptrdiff_t>(expected));
    return true;
}

bool HostDecodeImage(
    const std::string& path,
    int maxDim,
    int& outW,
    int& outH,
    std::vector<uint8_t>& outRgba) {
    JNIEnv* env = GetEnv();
    if (!env || !g_img_cls || !g_decode_rgba || path.empty()) return false;
    jstring jpath = Utf8ToJstring(env, path);
    auto* bytes = static_cast<jbyteArray>(
        env->CallStaticObjectMethod(g_img_cls, g_decode_rgba, jpath, static_cast<jint>(maxDim)));
    env->DeleteLocalRef(jpath);
    if (!bytes) return false;

    const jsize len = env->GetArrayLength(bytes);
    if (len < 8) {
        env->DeleteLocalRef(bytes);
        return false;
    }

    std::vector<uint8_t> buf(static_cast<size_t>(len));
    env->GetByteArrayRegion(bytes, 0, len, reinterpret_cast<jbyte*>(buf.data()));
    env->DeleteLocalRef(bytes);

    int32_t hdr[2] = {};
    std::memcpy(hdr, buf.data(), 8);
    outW = hdr[0];
    outH = hdr[1];
    const size_t expected = 8 + static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4;
    if (outW <= 0 || outH <= 0 || buf.size() < expected) return false;

    outRgba.assign(buf.begin() + 8, buf.begin() + static_cast<ptrdiff_t>(expected));
    return true;
}

} // namespace ps2ui

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env) {
        return JNI_ERR;
    }

    auto cacheClass = [env](const char* name) -> jclass {
        jclass local = env->FindClass(name);
        if (!local) {
            LOGE("Failed to find class %s", name);
            return nullptr;
        }
        auto global = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        return global;
    };

    g_host_cls = cacheClass("com/sbro/emucorex/ps2ui/Ps2ShellHost");
    if (g_host_cls) {
        g_on_game_launch = env->GetStaticMethodID(g_host_cls, "onGameLaunch", "(Ljava/lang/String;)V");
        g_on_save_launch = env->GetStaticMethodID(g_host_cls, "onSaveLaunch", "(Ljava/lang/String;I)V");
        g_on_delete_save = env->GetStaticMethodID(g_host_cls, "onDeleteSave", "(I)V");
        g_on_mc_action = env->GetStaticMethodID(g_host_cls, "onMemoryCardAction", "(IIII)V");
        g_on_open_game_settings = env->GetStaticMethodID(g_host_cls, "onOpenGameSettings", "(I)V");
        g_on_settings_changed = env->GetStaticMethodID(g_host_cls, "onSettingsChanged", "([I)V");
        g_on_exit_requested = env->GetStaticMethodID(g_host_cls, "onExitRequested", "()V");
    }

    g_text_cls = cacheClass("com/sbro/emucorex/ps2ui/Ps2TextRasterizer");
    if (g_text_cls) {
        g_rasterize_text = env->GetStaticMethodID(
            g_text_cls, "rasterizeText", "(Ljava/lang/String;FIFIIF)[B");
    }

    g_img_cls = cacheClass("com/sbro/emucorex/ps2ui/Ps2ImageDecoder");
    if (g_img_cls) {
        g_decode_rgba = env->GetStaticMethodID(
            g_img_cls, "decodeRgba", "(Ljava/lang/String;I)[B");
    }

    g_audio_cls = cacheClass("com/sbro/emucorex/ps2ui/Ps2ShellAudio");
    if (g_audio_cls) {
        g_play_sfx = env->GetStaticMethodID(g_audio_cls, "playSfx", "(I)V");
    }

    g_renderer = std::make_unique<ps2ui::Ps2Renderer>();
    return JNI_VERSION_1_6;
}

JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeInit(JNIEnv* /*env*/, jobject /*thiz*/) {
    if (!g_renderer) {
        g_renderer = std::make_unique<ps2ui::Ps2Renderer>();
    }
    return g_renderer->Init() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeSetUiScale(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat scale) {
    if (g_renderer) g_renderer->SetUiScale(scale);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeResize(
    JNIEnv* /*env*/, jobject /*thiz*/, jint width, jint height) {
    if (g_renderer) g_renderer->Resize(width, height);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeDraw(JNIEnv* /*env*/, jobject /*thiz*/) {
    if (g_renderer) g_renderer->Draw();
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeShutdown(JNIEnv* /*env*/, jobject /*thiz*/) {
    if (g_renderer) g_renderer->Shutdown();
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeKey(
    JNIEnv* /*env*/, jobject /*thiz*/, jint key) {
    if (g_renderer) g_renderer->OnKey(key);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeTouchDown(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat normX, jfloat normY) {
    if (g_renderer) g_renderer->OnTouchDown(normX, normY);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeTouchMove(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat normX, jfloat normY) {
    if (g_renderer) g_renderer->OnTouchMove(normX, normY);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeTouchUp(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat normX, jfloat normY) {
    if (g_renderer) g_renderer->OnTouchUp(normX, normY);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeTouchCancel(JNIEnv* /*env*/, jobject /*thiz*/) {
    if (g_renderer) g_renderer->OnTouchCancel();
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeTouchTwoFingerBack(JNIEnv* /*env*/, jobject /*thiz*/) {
    if (g_renderer) g_renderer->OnTouchTwoFingerBack();
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeTouchLongPress(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat normX, jfloat normY) {
    if (g_renderer) g_renderer->OnTouchLongPress(normX, normY);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeSetStrings(
    JNIEnv* env, jobject /*thiz*/, jobjectArray keys, jobjectArray values) {
    if (!g_renderer) return;
    auto k = ReadStringArray(env, keys);
    auto v = ReadStringArray(env, values);
    std::unordered_map<std::string, std::string> map;
    const size_t n = std::min(k.size(), v.size());
    map.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (!v[i].empty()) {
            map[k[i]] = v[i];
        }
    }
    g_renderer->SetStrings(std::move(map));
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeSetGames(
    JNIEnv* env,
    jobject /*thiz*/,
    jobjectArray titles,
    jobjectArray paths,
    jobjectArray serials,
    jobjectArray covers,
    jobjectArray sizes,
    jobjectArray dates,
    jlongArray fileSizesBytes,
    jintArray saveCounts,
    jintArray latestSaveSlots,
    jobjectArray compatStatuses) {
    if (!g_renderer) return;
    auto t = ReadStringArray(env, titles);
    auto p = ReadStringArray(env, paths);
    auto s = ReadStringArray(env, serials);
    auto c = ReadStringArray(env, covers);
    auto sz = ReadStringArray(env, sizes);
    auto dt = ReadStringArray(env, dates);
    auto fs = ReadLongArray(env, fileSizesBytes);
    auto sc = ReadIntArray(env, saveCounts);
    auto ls = ReadIntArray(env, latestSaveSlots);
    auto cs = ReadStringArray(env, compatStatuses);

    const size_t n = t.size();
    std::vector<ps2ui::GameEntry> games;
    games.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        ps2ui::GameEntry g;
        g.title = t[i];
        if (i < p.size()) g.path = p[i];
        if (i < s.size()) g.serial = s[i];
        if (i < c.size()) g.coverPath = c[i];
        if (i < sz.size()) g.sizeStr = sz[i];
        if (i < dt.size()) g.dateStr = dt[i];
        if (i < fs.size()) g.fileSizeBytes = fs[i];
        if (i < sc.size()) g.saveCount = sc[i];
        if (i < ls.size()) g.latestSaveSlot = ls[i];
        if (i < cs.size()) g.compatStatus = cs[i];
        games.push_back(std::move(g));
    }
    g_renderer->SetGames(std::move(games));
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeSetSaves(
    JNIEnv* env,
    jobject /*thiz*/,
    jobjectArray titles,
    jobjectArray slots,
    jintArray slotNums,
    jobjectArray dates,
    jobjectArray sizes,
    jobjectArray serials,
    jobjectArray previews,
    jobjectArray savePaths,
    jobjectArray gamePaths) {
    if (!g_renderer) return;
    auto t = ReadStringArray(env, titles);
    auto sl = ReadStringArray(env, slots);
    auto sn = ReadIntArray(env, slotNums);
    auto dt = ReadStringArray(env, dates);
    auto sz = ReadStringArray(env, sizes);
    auto ser = ReadStringArray(env, serials);
    auto pv = ReadStringArray(env, previews);
    auto sp = ReadStringArray(env, savePaths);
    auto gp = ReadStringArray(env, gamePaths);

    const size_t n = t.size();
    std::vector<ps2ui::SaveEntry> saves;
    saves.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        ps2ui::SaveEntry e;
        e.title = t[i];
        if (i < sl.size()) e.slotLabel = sl[i];
        if (i < sn.size()) e.slotNum = sn[i];
        if (i < dt.size()) e.dateStr = dt[i];
        if (i < sz.size()) e.sizeStr = sz[i];
        if (i < ser.size()) e.serial = ser[i];
        if (i < pv.size()) e.previewPath = pv[i];
        if (i < sp.size()) e.savePath = sp[i];
        if (i < gp.size()) e.gamePath = gp[i];
        saves.push_back(std::move(e));
    }
    g_renderer->SetSaves(std::move(saves));
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeSetMemoryCards(
    JNIEnv* env,
    jobject /*thiz*/,
    jstring slot1Name,
    jstring slot1Meta,
    jstring slot1Status,
    jstring slot2Name,
    jstring slot2Meta,
    jstring slot2Status,
    jobjectArray cardNames,
    jobjectArray cardSizes,
    jobjectArray cardDates,
    jbooleanArray cardIsFolder,
    jbooleanArray cardIsDefault) {
    if (!g_renderer) return;
    auto names = ReadStringArray(env, cardNames);
    auto sizes = ReadStringArray(env, cardSizes);
    auto dates = ReadStringArray(env, cardDates);
    auto folders = ReadBoolArray(env, cardIsFolder);
    auto defaults = ReadBoolArray(env, cardIsDefault);
    const size_t n = names.size();
    std::vector<ps2ui::MemoryCardEntry> cards;
    cards.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        ps2ui::MemoryCardEntry c;
        c.name = names[i];
        if (i < sizes.size()) c.sizeStr = sizes[i];
        if (i < dates.size()) c.dateStr = dates[i];
        if (i < folders.size()) c.isFolder = (folders[i] != 0);
        if (i < defaults.size()) c.isDefault = (defaults[i] != 0);
        cards.push_back(std::move(c));
    }
    g_renderer->SetMemoryCards(
        JstringToUtf8(env, slot1Name),
        JstringToUtf8(env, slot1Meta),
        JstringToUtf8(env, slot1Status),
        JstringToUtf8(env, slot2Name),
        JstringToUtf8(env, slot2Meta),
        JstringToUtf8(env, slot2Status),
        std::move(cards));
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeSetMemoryCardContents(
    JNIEnv* env,
    jobject /*thiz*/,
    jint slot,
    jstring cardName,
    jint freeKb,
    jint totalKb,
    jobjectArray dirNames,
    jobjectArray titles,
    jintArray sizesKb,
    jobjectArray dates,
    jintArray colorsRgb,
    jobjectArray iconMeshes) {
    if (!g_renderer) return;
    auto dirs = ReadStringArray(env, dirNames);
    auto ttl = ReadStringArray(env, titles);
    auto sz = ReadIntArray(env, sizesKb);
    auto dt = ReadStringArray(env, dates);
    auto col = ReadIntArray(env, colorsRgb);

    const size_t n = ttl.size();
    const jsize meshArrLen = iconMeshes ? env->GetArrayLength(iconMeshes) : 0;
    std::vector<ps2ui::McSaveBlockEntry> saves;
    saves.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        ps2ui::McSaveBlockEntry entry;
        if (i < dirs.size()) entry.dirName = dirs[i];
        entry.title = ttl[i];
        if (i < sz.size()) entry.sizeKb = sz[i];
        if (i < dt.size()) entry.dateStr = dt[i];
        if (i < col.size()) entry.colorRgb = col[i];
        if (static_cast<jsize>(i) < meshArrLen && iconMeshes) {
            auto* jmesh = static_cast<jfloatArray>(env->GetObjectArrayElement(iconMeshes, static_cast<jsize>(i)));
            if (jmesh) {
                const jsize flen = env->GetArrayLength(jmesh);
                if (flen >= 27 && flen <= 50000) {
                    entry.iconMesh.resize(static_cast<size_t>(flen));
                    env->GetFloatArrayRegion(jmesh, 0, flen, entry.iconMesh.data());
                }
                env->DeleteLocalRef(jmesh);
            }
        }
        saves.push_back(std::move(entry));
    }

    g_renderer->SetMemoryCardContents(
        static_cast<int>(slot),
        JstringToUtf8(env, cardName),
        static_cast<int>(freeKb),
        static_cast<int>(totalKb),
        std::move(saves));
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeSetSettings(
    JNIEnv* /*env*/,
    jobject /*thiz*/,
    jint theme,
    jint aspectMode,
    jint orbsMode,
    jint towersMode,
    jint scanlinesMode,
    jint bootAnimMode,
    jboolean keyTone,
    jboolean ambientSound,
    jboolean swapConfirm,
    jboolean touchControls,
    jboolean time24h,
    jint dateFormat,
    jboolean keepScreenOn) {
    if (!g_renderer) return;
    ps2ui::ShellSettings s;
    s.theme = theme;
    s.aspectMode = aspectMode;
    s.orbsMode = orbsMode;
    s.towersMode = towersMode;
    s.scanlinesMode = scanlinesMode;
    s.bootAnimMode = bootAnimMode;
    s.keyTone = (keyTone == JNI_TRUE);
    s.ambientSound = (ambientSound == JNI_TRUE);
    s.swapConfirm = (swapConfirm == JNI_TRUE);
    s.touchControls = (touchControls == JNI_TRUE);
    s.time24h = (time24h == JNI_TRUE);
    s.dateFormat = dateFormat;
    s.keepScreenOn = (keepScreenOn == JNI_TRUE);
    g_renderer->SetSettings(s);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeSetSystemStatus(
    JNIEnv* env,
    jobject /*thiz*/,
    jint batteryLevel,
    jboolean batteryCharging,
    jboolean wifiConnected,
    jboolean bluetoothConnected,
    jboolean audioMuted,
    jstring freeStorage,
    jstring deviceName,
    jstring appVersion,
    jstring biosStatus,
    jboolean biosValid) {
    if (!g_renderer) return;
    ps2ui::SystemStatus st;
    st.batteryLevel = batteryLevel;
    st.batteryCharging = (batteryCharging == JNI_TRUE);
    st.wifiConnected = (wifiConnected == JNI_TRUE);
    st.bluetoothConnected = (bluetoothConnected == JNI_TRUE);
    st.audioMuted = (audioMuted == JNI_TRUE);
    st.freeStorage = JstringToUtf8(env, freeStorage);
    st.deviceName = JstringToUtf8(env, deviceName);
    st.appVersion = JstringToUtf8(env, appVersion);
    st.biosStatus = JstringToUtf8(env, biosStatus);
    st.biosValid = (biosValid == JNI_TRUE);
    g_renderer->SetSystemStatus(st);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeSetTouchMode(
    JNIEnv* /*env*/, jobject /*thiz*/, jboolean active, jboolean showTutorial) {
    if (g_renderer) {
        g_renderer->SetTouchMode(active == JNI_TRUE, showTutorial == JNI_TRUE);
    }
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeSetGamesLoading(
    JNIEnv* /*env*/, jobject /*thiz*/, jboolean loading) {
    if (g_renderer) g_renderer->SetGamesLoading(loading == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeSetSavesLoading(
    JNIEnv* /*env*/, jobject /*thiz*/, jboolean loading) {
    if (g_renderer) g_renderer->SetSavesLoading(loading == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeTriggerTransition(
    JNIEnv* /*env*/, jobject /*thiz*/, jint kind) {
    if (g_renderer) g_renderer->TriggerTransition(kind);
}

JNIEXPORT jintArray JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeGetState(JNIEnv* env, jobject /*thiz*/) {
    std::vector<int> st = g_renderer ? g_renderer->GetState() : std::vector<int>{0, 0, 0, 0};
    jintArray out = env->NewIntArray(static_cast<jsize>(st.size()));
    if (out && !st.empty()) {
        env->SetIntArrayRegion(out, 0, static_cast<jsize>(st.size()), reinterpret_cast<const jint*>(st.data()));
    }
    return out;
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeSetState(
    JNIEnv* /*env*/, jobject /*thiz*/, jint screen, jint mainIndex, jint gameIndex, jint saveIndex) {
    if (g_renderer) g_renderer->SetState(screen, mainIndex, gameIndex, saveIndex);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorex_ps2ui_Ps2ShellNative_nativeSelectGameByPath(
    JNIEnv* env, jobject /*thiz*/, jstring path) {
    if (g_renderer) g_renderer->SelectGameByPath(JstringToUtf8(env, path));
}

} // extern "C"
