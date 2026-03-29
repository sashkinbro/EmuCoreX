#include <jni.h>
#include <android/native_window_jni.h>
#include <unistd.h>
#include <cstdlib>
#include "PrecompiledHeader.h"
#include "common/StringUtil.h"
#include "common/FileSystem.h"
#include "common/ZipHelpers.h"
#include "common/Error.h"
#include "pcsx2/GS.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/Config.h"
#include "pcsx2/SIO/Memcard/MemoryCardFile.h"
#include "pcsx2/SIO/Pad/Pad.h" // For GenericInputBinding

#include "pcsx2/Patch.h"
#include "PerformanceMetrics.h"
#include "GameList.h"
#include "GS/GSPerfMon.h"
#include "GSDumpReplayer.h"
#include "ImGui/ImGuiManager.h"
#include "common/Path.h"
#include "common/HTTPDownloader.h"
#include "pcsx2/INISettingsInterface.h"
#include "pcsx2/CDVD/CDVD.h"
#include "pcsx2/CDVD/IsoReader.h"
#include "3rdparty/rcheevos/include/rc_api_request.h"
#include "3rdparty/rcheevos/include/rc_api_runtime.h"
#include "3rdparty/rcheevos/include/rc_api_user.h"
#include "SIO/Pad/Pad.h"
#include "Input/InputManager.h"
#include "ImGui/ImGuiFullscreen.h"
#include "Achievements.h"
#include "Host.h"
#include "ImGui/FullscreenUI.h"
#include "SIO/Pad/PadDualshock2.h"
#include "MTGS.h"
#include "SDL3/SDL.h"
#include <future>
#include <memory>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <set>
#include <mutex>

namespace
{
	static jclass s_native_app_class = nullptr;
	static jmethodID s_on_pad_vibration = nullptr;
    static jmethodID s_native_ensure_resource_dir = nullptr;
	static jmethodID s_native_open_content_uri = nullptr;
    static jmethodID s_native_log_method = nullptr;
    static jmethodID s_on_performance_metrics = nullptr;
	static std::chrono::steady_clock::time_point s_last_fps_sample_time{};

	static jclass s_ra_bridge_class = nullptr;
	static jmethodID s_ra_notify_login_requested = nullptr;
	static jmethodID s_ra_notify_login_success = nullptr;
	static jmethodID s_ra_notify_state_changed = nullptr;
	static jmethodID s_ra_notify_hardcore_changed = nullptr;
	static std::mutex s_ra_bridge_mutex;

    static std::string EscapeJsonString(std::string_view value)
    {
        std::string escaped;
        escaped.reserve(value.size() + 16);
        for (const char ch : value)
        {
            switch (ch)
            {
                case '\\': escaped += "\\\\"; break;
                case '"': escaped += "\\\""; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default: escaped += ch; break;
            }
        }
        return escaped;
    }

    struct SimpleHttpResult
    {
        s32 status_code = HTTPDownloader::HTTP_STATUS_CANCELLED;
        std::string body;
    };

    static bool PerformRcApiRequest(const rc_api_request_t& request, SimpleHttpResult* out_result)
    {
        if (!out_result)
            return false;

        std::unique_ptr<HTTPDownloader> downloader = HTTPDownloader::Create(Host::GetHTTPUserAgent());
        if (!downloader)
            return false;

        auto callback = [out_result](
            s32 status_code,
            const std::string&,
            HTTPDownloader::Request::Data data
        ) {
            out_result->status_code = status_code;
            out_result->body.assign(reinterpret_cast<const char*>(data.data()), data.size());
        };

        if (request.post_data)
            downloader->CreatePostRequest(request.url, request.post_data, std::move(callback));
        else
            downloader->CreateRequest(request.url, std::move(callback));

        downloader->WaitForAllRequests();
        return out_result->status_code > 0;
    }

    static std::string BuildRcImageUrl(const char* image_name, uint32_t image_type)
    {
        if (!image_name || !image_name[0])
            return {};

        rc_api_fetch_image_request_t image_request = {};
        image_request.image_name = image_name;
        image_request.image_type = image_type;

        rc_api_request_t request = {};
        if (rc_api_init_fetch_image_request(&request, &image_request) != RC_OK)
            return {};

        const std::string url = request.url ? request.url : "";
        rc_api_destroy_request(&request);
        return url;
    }

    static std::string FetchRetroAchievementGameDataJson(const std::string& path)
    {
        std::string game_hash;

        if (VMManager::IsElfFileName(path))
        {
            game_hash = Achievements::GetGameHashForELF(path);
        }
        else if (VMManager::IsDiscFileName(path))
        {
            Error error;
            CDVD = &CDVDapi_Iso;
            if (!CDVD->open(path, &error))
                return {};

            std::string elf_path;
            cdvdGetDiscInfo(nullptr, &elf_path, nullptr, nullptr, nullptr);
            if (!elf_path.empty())
                game_hash = Achievements::GetGameHashForELF(elf_path);

            DoCDVDclose();
        }

        if (game_hash.empty())
            return {};

        rc_api_request_t resolve_request = {};
        rc_api_resolve_hash_request_t resolve_params = {};
        resolve_params.game_hash = game_hash.c_str();
        if (rc_api_init_resolve_hash_request(&resolve_request, &resolve_params) != RC_OK)
            return {};

        SimpleHttpResult resolve_result;
        const bool resolve_ok = PerformRcApiRequest(resolve_request, &resolve_result);
        rc_api_destroy_request(&resolve_request);
        if (!resolve_ok)
            return {};

        rc_api_server_response_t resolve_server_response = {};
        resolve_server_response.body = resolve_result.body.c_str();
        resolve_server_response.body_length = resolve_result.body.size();
        resolve_server_response.http_status_code = resolve_result.status_code;

        rc_api_resolve_hash_response_t resolve_response = {};
        if (rc_api_process_resolve_hash_server_response(&resolve_response, &resolve_server_response) != RC_OK ||
            !resolve_response.response.succeeded || resolve_response.game_id == 0)
        {
            rc_api_destroy_resolve_hash_response(&resolve_response);
            return {};
        }

        const std::string username = Host::GetBaseStringSettingValue("Achievements", "Username");
        const std::string token = Host::GetBaseStringSettingValue("Achievements", "Token");

        rc_api_request_t game_request = {};
        rc_api_fetch_game_data_request_t game_params = {};
        game_params.username = username.empty() ? nullptr : username.c_str();
        game_params.api_token = token.empty() ? nullptr : token.c_str();
        game_params.game_id = resolve_response.game_id;
        const int game_request_init_result = rc_api_init_fetch_game_data_request(&game_request, &game_params);
        if (game_request_init_result != RC_OK)
        {
            std::string json = "{";
            json += fmt::format("\"gameId\":{},", resolve_response.game_id);
            json += "\"title\":\"\",";
            json += "\"gameImageUrl\":\"\",";
            json += "\"resolvedOnly\":true,";
            json += "\"achievements\":[]}";
            rc_api_destroy_resolve_hash_response(&resolve_response);
            return json;
        }

        SimpleHttpResult game_result;
        const bool game_ok = PerformRcApiRequest(game_request, &game_result);
        rc_api_destroy_request(&game_request);
        if (!game_ok)
        {
            std::string json = "{";
            json += fmt::format("\"gameId\":{},", resolve_response.game_id);
            json += "\"title\":\"\",";
            json += "\"gameImageUrl\":\"\",";
            json += "\"resolvedOnly\":true,";
            json += "\"achievements\":[]}";
            rc_api_destroy_resolve_hash_response(&resolve_response);
            return json;
        }

        rc_api_server_response_t game_server_response = {};
        game_server_response.body = game_result.body.c_str();
        game_server_response.body_length = game_result.body.size();
        game_server_response.http_status_code = game_result.status_code;

        rc_api_fetch_game_data_response_t game_response = {};
        if (rc_api_process_fetch_game_data_server_response(&game_response, &game_server_response) != RC_OK ||
            !game_response.response.succeeded)
        {
            std::string json = "{";
            json += fmt::format("\"gameId\":{},", resolve_response.game_id);
            json += "\"title\":\"\",";
            json += "\"gameImageUrl\":\"\",";
            json += "\"resolvedOnly\":true,";
            json += "\"achievements\":[]}";
            rc_api_destroy_fetch_game_data_response(&game_response);
            rc_api_destroy_resolve_hash_response(&resolve_response);
            return json;
        }

        std::set<uint32_t> softcore_unlocks;
        std::set<uint32_t> hardcore_unlocks;

        if (!username.empty() && !token.empty())
        {
            for (uint32_t hardcore = 0; hardcore <= 1; hardcore++)
            {
                rc_api_request_t unlocks_request = {};
                rc_api_fetch_user_unlocks_request_t unlocks_params = {};
                unlocks_params.username = username.c_str();
                unlocks_params.api_token = token.c_str();
                unlocks_params.game_id = resolve_response.game_id;
                unlocks_params.hardcore = hardcore;
                if (rc_api_init_fetch_user_unlocks_request(&unlocks_request, &unlocks_params) != RC_OK)
                    continue;

                SimpleHttpResult unlocks_result;
                const bool unlocks_ok = PerformRcApiRequest(unlocks_request, &unlocks_result);
                rc_api_destroy_request(&unlocks_request);
                if (!unlocks_ok)
                    continue;

                rc_api_server_response_t unlocks_server_response = {};
                unlocks_server_response.body = unlocks_result.body.c_str();
                unlocks_server_response.body_length = unlocks_result.body.size();
                unlocks_server_response.http_status_code = unlocks_result.status_code;

                rc_api_fetch_user_unlocks_response_t unlocks_response = {};
                if (rc_api_process_fetch_user_unlocks_server_response(&unlocks_response, &unlocks_server_response) == RC_OK &&
                    unlocks_response.response.succeeded)
                {
                    for (uint32_t index = 0; index < unlocks_response.num_achievement_ids; index++)
                    {
                        const uint32_t achievement_id = unlocks_response.achievement_ids[index];
                        if (hardcore != 0)
                            hardcore_unlocks.insert(achievement_id);
                        else
                            softcore_unlocks.insert(achievement_id);
                    }
                }
                rc_api_destroy_fetch_user_unlocks_response(&unlocks_response);
            }
        }

        std::string json = "{";
        json += fmt::format("\"gameId\":{},", resolve_response.game_id);
        json += fmt::format("\"title\":\"{}\",", EscapeJsonString(game_response.title ? game_response.title : ""));
        json += fmt::format("\"consoleId\":{},", game_response.console_id);
        json += fmt::format("\"gameImageUrl\":\"{}\",",
            EscapeJsonString(BuildRcImageUrl(game_response.image_name, RC_IMAGE_TYPE_GAME)));
        json += "\"resolvedOnly\":false,";
        json += "\"achievements\":[";

        bool first = true;
        for (uint32_t index = 0; index < game_response.num_achievements; index++)
        {
            const rc_api_achievement_definition_t& achievement = game_response.achievements[index];
            if (!first)
                json += ",";
            first = false;

            const bool earnedSoftcore = softcore_unlocks.find(achievement.id) != softcore_unlocks.end();
            const bool earnedHardcore = hardcore_unlocks.find(achievement.id) != hardcore_unlocks.end();
            json += "{";
            json += fmt::format("\"id\":{},", achievement.id);
            json += fmt::format("\"title\":\"{}\",", EscapeJsonString(achievement.title ? achievement.title : ""));
            json += fmt::format("\"description\":\"{}\",", EscapeJsonString(achievement.description ? achievement.description : ""));
            json += fmt::format("\"points\":{},", achievement.points);
            json += fmt::format("\"category\":{},", achievement.category);
            json += fmt::format("\"type\":{},", achievement.type);
            json += fmt::format("\"rarity\":{:.2f},", achievement.rarity);
            json += fmt::format("\"rarityHardcore\":{:.2f},", achievement.rarity_hardcore);
            json += fmt::format("\"earnedSoftcore\":{},", earnedSoftcore ? "true" : "false");
            json += fmt::format("\"earnedHardcore\":{},", earnedHardcore ? "true" : "false");
            json += fmt::format("\"badgeUrl\":\"{}\",",
                EscapeJsonString(BuildRcImageUrl(achievement.badge_name, RC_IMAGE_TYPE_ACHIEVEMENT)));
            json += fmt::format("\"badgeLockedUrl\":\"{}\"",
                EscapeJsonString(BuildRcImageUrl(achievement.badge_name, RC_IMAGE_TYPE_ACHIEVEMENT_LOCKED)));
            json += "}";
        }

        json += "]}";

        rc_api_destroy_fetch_game_data_response(&game_response);
        rc_api_destroy_resolve_hash_response(&resolve_response);
        return json;
    }

	static void EnsureAchievementsClientInitialized()
	{
        if (!EmuConfig.Achievements.Enabled)
            return;

        if (!Achievements::IsActive())
            Achievements::Initialize();
	}

	static void ClearJNIExceptions(JNIEnv* env)
	{
		if (env && env->ExceptionCheck())
		{
			env->ExceptionDescribe();
			env->ExceptionClear();
		}
	}

    static bool EnsureNativeAppMethods(JNIEnv* env)
    {
        if (!env)
            return false;

        if (!s_native_app_class)
        {
            jclass local = env->FindClass("com/sbro/emucorex/core/NativeApp");
            if (!local)
            {
                ClearJNIExceptions(env);
                return false;
            }

            s_native_app_class = reinterpret_cast<jclass>(env->NewGlobalRef(local));
            env->DeleteLocalRef(local);
            if (!s_native_app_class)
            {
                ClearJNIExceptions(env);
                return false;
            }
        }

        if (!s_on_pad_vibration)
            s_on_pad_vibration = env->GetStaticMethodID(s_native_app_class, "onPadVibration", "(IFF)V");

        if (!s_native_ensure_resource_dir)
            s_native_ensure_resource_dir = env->GetStaticMethodID(
                s_native_app_class, "ensureResourceSubdirectoryCopied", "(Ljava/lang/String;)V");

        if (!s_native_log_method)
            s_native_log_method = env->GetStaticMethodID(s_native_app_class, "nativeLog", "(Ljava/lang/String;)V");

        if (!s_native_open_content_uri)
            s_native_open_content_uri = env->GetStaticMethodID(s_native_app_class, "openContentUri", "(Ljava/lang/String;)I");

        if (!s_on_performance_metrics)
            s_on_performance_metrics = env->GetStaticMethodID(s_native_app_class, "onPerformanceMetrics", "(FFFFF)V");

        ClearJNIExceptions(env);
        return s_native_app_class != nullptr;
    }

    static void NativeLogCallback(LOGLEVEL level, ConsoleColors color, std::string_view message)
    {
        auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (!env || !s_native_app_class || !s_native_log_method)
            return;

        jstring j_msg = env->NewStringUTF(std::string(message).c_str());
        env->CallStaticVoidMethod(s_native_app_class, s_native_log_method, j_msg);
        env->DeleteLocalRef(j_msg);
        ClearJNIExceptions(env);
    }

	static bool EnsureRetroAchievementsBridge(JNIEnv* env)
	{
		if (!env)
			return false;

        std::lock_guard<std::mutex> lock(s_ra_bridge_mutex);
        if (!s_ra_bridge_class)
		{
			jclass local = env->FindClass("com/sbro/emucorex/core/utils/RetroAchievementsBridge");
			if (!local)
				return false;

			s_ra_bridge_class = reinterpret_cast<jclass>(env->NewGlobalRef(local));
			env->DeleteLocalRef(local);
			if (!s_ra_bridge_class)
				return false;

			s_ra_notify_login_requested =
				env->GetStaticMethodID(s_ra_bridge_class, "notifyLoginRequested", "(I)V");
			s_ra_notify_login_success =
				env->GetStaticMethodID(s_ra_bridge_class, "notifyLoginSuccess", "(Ljava/lang/String;III)V");
			s_ra_notify_state_changed = env->GetStaticMethodID(
				s_ra_bridge_class, "notifyStateChanged",
				"(ZZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;IIIZZZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;IIIIIZZZ)V");
			s_ra_notify_hardcore_changed =
				env->GetStaticMethodID(s_ra_bridge_class, "notifyHardcoreModeChanged", "(Z)V");

			if (!s_ra_notify_login_requested || !s_ra_notify_login_success || !s_ra_notify_state_changed ||
				!s_ra_notify_hardcore_changed)
			{
				return false;
			}
		}

		return true;
	}

	static void NotifyRetroAchievementsState()
	{
        auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (!env)
            return;
		if (!EnsureRetroAchievementsBridge(env))
			return;

		Achievements::UserStats user_stats;
		Achievements::GameStats game_stats;
		const bool have_user = Achievements::GetCurrentUserStats(&user_stats);
		const bool have_game = Achievements::GetCurrentGameStats(&game_stats);

		const bool achievements_enabled = EmuConfig.Achievements.Enabled;
		const bool hardcore_preference = EmuConfig.Achievements.HardcoreMode;
		const bool hardcore_active = Achievements::IsHardcoreModeActive();

		jstring j_username = have_user ? env->NewStringUTF(user_stats.username.c_str()) : nullptr;
		jstring j_display_name = have_user ? env->NewStringUTF(user_stats.display_name.c_str()) : nullptr;
		jstring j_avatar = (have_user && !user_stats.avatar_path.empty()) ?
			env->NewStringUTF(user_stats.avatar_path.c_str()) :
			nullptr;
		jstring j_game_title = have_game ? env->NewStringUTF(game_stats.title.c_str()) : nullptr;
		jstring j_rich_presence = (have_game && !game_stats.rich_presence.empty()) ?
			env->NewStringUTF(game_stats.rich_presence.c_str()) :
			nullptr;
		jstring j_icon_path = (have_game && !game_stats.icon_path.empty()) ?
			env->NewStringUTF(game_stats.icon_path.c_str()) :
			nullptr;

		env->CallStaticVoidMethod(
			s_ra_bridge_class, s_ra_notify_state_changed,
			achievements_enabled ? JNI_TRUE : JNI_FALSE,
			have_user ? JNI_TRUE : JNI_FALSE,
			j_username,
			j_display_name,
			j_avatar,
			static_cast<jint>(have_user ? user_stats.points : 0),
			static_cast<jint>(have_user ? user_stats.softcore_points : 0),
			static_cast<jint>(have_user ? user_stats.unread_messages : 0),
			hardcore_preference ? JNI_TRUE : JNI_FALSE,
			hardcore_active ? JNI_TRUE : JNI_FALSE,
			have_game ? JNI_TRUE : JNI_FALSE,
			j_game_title,
			j_rich_presence,
			j_icon_path,
			static_cast<jint>(have_game ? game_stats.unlocked_achievements : 0),
			static_cast<jint>(have_game ? game_stats.total_achievements : 0),
			static_cast<jint>(have_game ? game_stats.unlocked_points : 0),
			static_cast<jint>(have_game ? game_stats.total_points : 0),
			static_cast<jint>(have_game ? game_stats.game_id : 0),
			have_game && game_stats.has_achievements ? JNI_TRUE : JNI_FALSE,
			have_game && game_stats.has_leaderboards ? JNI_TRUE : JNI_FALSE,
			have_game && game_stats.has_rich_presence ? JNI_TRUE : JNI_FALSE);

		ClearJNIExceptions(env);

		if (j_username)
			env->DeleteLocalRef(j_username);
		if (j_display_name)
			env->DeleteLocalRef(j_display_name);
		if (j_avatar)
			env->DeleteLocalRef(j_avatar);
		if (j_game_title)
			env->DeleteLocalRef(j_game_title);
		if (j_rich_presence)
			env->DeleteLocalRef(j_rich_presence);
		if (j_icon_path)
			env->DeleteLocalRef(j_icon_path);
	}
}

namespace Host::Internal
{
void EnsureAndroidResourceSubdirCopied(const char* relative_path)
{
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env)
        return;

    if (!EnsureNativeAppMethods(env) || !s_native_ensure_resource_dir)
        return;

    const char* safe_path = relative_path ? relative_path : "";
    jstring j_path = env->NewStringUTF(safe_path);
    env->CallStaticVoidMethod(s_native_app_class, s_native_ensure_resource_dir, j_path);
    if (j_path)
        env->DeleteLocalRef(j_path);
    ClearJNIExceptions(env);
}
} // namespace Host::Internal

void AndroidUpdatePadVibration(u32 pad_index, float large_intensity, float small_intensity)
{
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env)
        return;

    if (!EnsureNativeAppMethods(env) || !s_on_pad_vibration)
        return;

    env->CallStaticVoidMethod(s_native_app_class, s_on_pad_vibration, static_cast<jint>(pad_index),
                              static_cast<jfloat>(large_intensity), static_cast<jfloat>(small_intensity));
}


bool s_execute_exit;
int s_window_width = 0;
int s_window_height = 0;
ANativeWindow* s_window = nullptr;

static std::unique_ptr<INISettingsInterface> s_settings_interface;
static bool IsFullscreenUIEnabled()
{
    if (!s_settings_interface)
        return false;
    return s_settings_interface->GetBoolValue("UI", "EnableFullscreenUI", false);
}

////
std::string GetJavaString(JNIEnv *env, jstring jstr) {
    if (!jstr) {
        return "";
    }
    const char *str = env->GetStringUTFChars(jstr, nullptr);
    std::string cpp_string = std::string(str);
    env->ReleaseStringUTFChars(jstr, str);
    return cpp_string;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_initialize(JNIEnv *env, jclass clazz,
                                                jstring p_szpath, jint p_apiVer) {
    std::string _szPath = GetJavaString(env, p_szpath);
    EmuFolders::AppRoot = _szPath;
    EmuFolders::DataRoot = _szPath;
    EmuFolders::SetResourcesDirectory();

    Log::SetConsoleOutputLevel(LOGLEVEL_DEBUG);
    ImGuiManager::SetFontPathAndRange(Path::Combine(EmuFolders::Resources, "fonts" FS_OSPATH_SEPARATOR_STR "Roboto-Regular.ttf"), {});

    if (!s_settings_interface)
    {
        const std::string ini_path = EmuFolders::DataRoot + "/PCSX2-Android.ini";
        s_settings_interface = std::make_unique<INISettingsInterface>(ini_path);
        Host::Internal::SetBaseSettingsLayer(s_settings_interface.get());
        s_settings_interface->Load();
        if (s_settings_interface->IsEmpty())
        {
            VMManager::SetDefaultSettings(*s_settings_interface, true, true, true, true, true);
            s_settings_interface->SetBoolValue("EmuCore", "EnableDiscordPresence", true);
            s_settings_interface->SetBoolValue("EmuCore/GS", "FrameLimitEnable", false);
            s_settings_interface->SetIntValue("EmuCore/GS", "VsyncEnable", false);
            s_settings_interface->SetBoolValue("InputSources", "SDL", true);
            s_settings_interface->SetBoolValue("InputSources", "XInput", false);
            s_settings_interface->SetStringValue("SPU2/Output", "OutputModule", "nullout");
            s_settings_interface->SetBoolValue("Logging", "EnableSystemConsole", true);
            s_settings_interface->SetBoolValue("Logging", "EnableTimestamps", true);
            s_settings_interface->SetBoolValue("Logging", "EnableVerbose", false);
            s_settings_interface->SetBoolValue("EmuCore/GS", "OsdShowFPS", false);
            s_settings_interface->SetBoolValue("EmuCore/GS", "OsdShowResolution", false);
            s_settings_interface->SetBoolValue("EmuCore/GS", "OsdShowGSStats", false);
            s_settings_interface->SetIntValue("EmuCore/GS", "OsdPerformancePos", 0); 
            s_settings_interface->SetBoolValue("UI", "EnableFullscreenUI", false);
            s_settings_interface->SetBoolValue("UI", "ExpandIntoDisplayCutout", true);
            s_settings_interface->SetBoolValue("Achievements", "Enabled", false);
            s_settings_interface->SetBoolValue("Achievements", "ChallengeMode", false);
            s_settings_interface->SetBoolValue("Achievements", "AndroidMigrationV1", true);
            s_settings_interface->Save();
        }
        else
        {
            bool needs_save = false;
            if (!s_settings_interface->GetBoolValue("Achievements", "AndroidMigrationV1", false))
            {
                if (!s_settings_interface->ContainsValue("Achievements", "Enabled"))
                    s_settings_interface->SetBoolValue("Achievements", "Enabled", false);
                s_settings_interface->SetBoolValue("Achievements", "AndroidMigrationV1", true);
                needs_save = true;
            }
            if (!s_settings_interface->ContainsValue("Achievements", "ChallengeMode"))
            {
                s_settings_interface->SetBoolValue("Achievements", "ChallengeMode", false);
                needs_save = true;
            }
            if (!s_settings_interface->ContainsValue("UI", "ExpandIntoDisplayCutout"))
            {
                s_settings_interface->SetBoolValue("UI", "ExpandIntoDisplayCutout", true);
                needs_save = true;
            }
            if (needs_save)
                s_settings_interface->Save();
        }
    }
    VMManager::Internal::LoadStartupSettings();
    if (s_settings_interface)
        s_settings_interface->SetBoolValue("EmuCore/GS", "OsdShowFPS", false);
    VMManager::ApplySettings();
    GSConfig.OsdPerformancePos = EmuConfig.GS.OsdPerformancePos;
    GSConfig.CustomDriverPath = EmuConfig.GS.CustomDriverPath;
    if (MTGS::IsOpen()) MTGS::ApplySettings();
    VMManager::ReloadInputSources();
    VMManager::ReloadInputBindings(true);
    EnsureAchievementsClientInitialized();
    NotifyRetroAchievementsState();

    if (EnsureNativeAppMethods(env))
    {
        Log::SetHostOutputLevel(LOGLEVEL_INFO, NativeLogCallback);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_reloadDataRoot(JNIEnv* env, jclass, jstring p_szpath)
{
    std::string new_path = GetJavaString(env, p_szpath);
    if (new_path.empty())
        return;

    EmuFolders::AppRoot = new_path;
    EmuFolders::DataRoot = new_path;
    EmuFolders::SetResourcesDirectory();

    Log::SetConsoleOutputLevel(LOGLEVEL_DEBUG);
    ImGuiManager::SetFontPathAndRange(Path::Combine(EmuFolders::Resources, "fonts" FS_OSPATH_SEPARATOR_STR "Roboto-Regular.ttf"), {});

    if (s_settings_interface)
        s_settings_interface->Save();
    s_settings_interface.reset();

    const std::string ini_path = EmuFolders::DataRoot + "/PCSX2-Android.ini";
    s_settings_interface = std::make_unique<INISettingsInterface>(ini_path);
    Host::Internal::SetBaseSettingsLayer(s_settings_interface.get());
    s_settings_interface->Load();
    if (s_settings_interface->IsEmpty())
    {
            VMManager::SetDefaultSettings(*s_settings_interface, true, true, true, true, true);
        s_settings_interface->SetBoolValue("EmuCore", "EnableDiscordPresence", true);
        s_settings_interface->SetBoolValue("EmuCore/GS", "FrameLimitEnable", false);
        s_settings_interface->SetIntValue("EmuCore/GS", "VsyncEnable", false);
        s_settings_interface->SetBoolValue("InputSources", "SDL", true);
        s_settings_interface->SetBoolValue("InputSources", "XInput", false);
        s_settings_interface->SetStringValue("SPU2/Output", "OutputModule", "nullout");
        s_settings_interface->SetBoolValue("Logging", "EnableSystemConsole", true);
        s_settings_interface->SetBoolValue("Logging", "EnableTimestamps", true);
        s_settings_interface->SetBoolValue("Logging", "EnableVerbose", false);
        s_settings_interface->SetBoolValue("EmuCore/GS", "OsdShowFPS", false);
        s_settings_interface->SetBoolValue("EmuCore/GS", "OsdShowResolution", false);
        s_settings_interface->SetBoolValue("EmuCore/GS", "OsdShowGSStats", false);
        s_settings_interface->SetIntValue("EmuCore/GS", "OsdPerformancePos", 0);
        s_settings_interface->SetBoolValue("UI", "EnableFullscreenUI", false);
        s_settings_interface->SetBoolValue("UI", "ExpandIntoDisplayCutout", true);
        s_settings_interface->SetBoolValue("Achievements", "Enabled", false);
        s_settings_interface->SetBoolValue("Achievements", "ChallengeMode", false);
        s_settings_interface->SetBoolValue("Achievements", "AndroidMigrationV1", true);
        s_settings_interface->Save();
    }
    else
    {
        bool needs_save = false;
        if (!s_settings_interface->GetBoolValue("Achievements", "AndroidMigrationV1", false))
        {
            if (!s_settings_interface->ContainsValue("Achievements", "Enabled"))
                s_settings_interface->SetBoolValue("Achievements", "Enabled", false);
            s_settings_interface->SetBoolValue("Achievements", "AndroidMigrationV1", true);
            needs_save = true;
        }
        if (!s_settings_interface->ContainsValue("Achievements", "ChallengeMode"))
        {
            s_settings_interface->SetBoolValue("Achievements", "ChallengeMode", false);
            needs_save = true;
        }
        if (!s_settings_interface->ContainsValue("UI", "ExpandIntoDisplayCutout"))
        {
            s_settings_interface->SetBoolValue("UI", "ExpandIntoDisplayCutout", true);
            needs_save = true;
        }
        if (needs_save)
            s_settings_interface->Save();
    }
}

static GenericInputBinding AndroidKeyToGeneric(int key) {
	switch (key) {
		case 19: return GenericInputBinding::DPadUp;
		case 20: return GenericInputBinding::DPadDown;
		case 21: return GenericInputBinding::DPadLeft;
		case 22: return GenericInputBinding::DPadRight;
		case 96: return GenericInputBinding::Cross;
		case 97: return GenericInputBinding::Circle;
		case 99: return GenericInputBinding::Square;
		case 100: return GenericInputBinding::Triangle;
		case 102: return GenericInputBinding::L1;
		case 103: return GenericInputBinding::R1;
		case 104: return GenericInputBinding::L2;
		case 105: return GenericInputBinding::R2;
		case 106: return GenericInputBinding::L3;
		case 107: return GenericInputBinding::R3;
		case 108: return GenericInputBinding::Start;
		case 109: return GenericInputBinding::Select;
		case 110: return GenericInputBinding::LeftStickUp;
		case 111: return GenericInputBinding::LeftStickRight;
		case 112: return GenericInputBinding::LeftStickDown;
		case 113: return GenericInputBinding::LeftStickLeft;
		case 120: return GenericInputBinding::RightStickUp;
		case 121: return GenericInputBinding::RightStickRight;
		case 122: return GenericInputBinding::RightStickDown;
		case 123: return GenericInputBinding::RightStickLeft;
		default: return GenericInputBinding::Unknown;
	}
}

static void SetPadButtonState(u32 pad_index, GenericInputBinding generic_key, float value) {
    if (generic_key == GenericInputBinding::Unknown) return;
    
    PadBase* pad = Pad::GetPad(pad_index);
    if (!pad) return;

    const auto& ci = pad->GetInfo();
    for (u32 i = 0; i < (u32)ci.bindings.size(); i++) {
        if (ci.bindings[i].generic_mapping == generic_key) {
            pad->Set(i, value);
        }
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_setPadButton(JNIEnv *env, jclass clazz,
                                                  jint p_index, jint p_range, jboolean p_pressed) {
    if (VMManager::HasValidVM()) {
        const GenericInputBinding generic_key = AndroidKeyToGeneric(p_index);
        float value = (p_range > 0) ? (static_cast<float>(p_range) / 255.0f) : (p_pressed ? 1.0f : 0.0f);
        SetPadButtonState(0, generic_key, value);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_setPadParams(JNIEnv *env, jclass clazz,
                                                   jintArray p_indices, jfloatArray p_values) {
    if (VMManager::HasValidVM()) {
        jsize len = env->GetArrayLength(p_indices);
        jsize valLen = env->GetArrayLength(p_values);
        if (len == 0 || len != valLen) return;

        jint* indices = env->GetIntArrayElements(p_indices, nullptr);
        jfloat* values = env->GetFloatArrayElements(p_values, nullptr);

        if (indices && values) {
            for (jsize i = 0; i < len; i++) {
                const GenericInputBinding generic_key = AndroidKeyToGeneric(indices[i]);
                SetPadButtonState(0, generic_key, values[i]);
            }
        }

        if (indices) env->ReleaseIntArrayElements(p_indices, indices, JNI_ABORT);
        if (values) env->ReleaseFloatArrayElements(p_values, values, JNI_ABORT);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_resetKeyStatus(JNIEnv *env, jclass clazz) {
    if (VMManager::HasValidVM()) {
        PadBase* pad = Pad::GetPad(0);
        if (pad) {
            for (u32 i = 0; i < (u32)pad->GetInfo().bindings.size(); i++) {
                pad->Set(i, 0.0f);
            }
        }
        InputManager::PauseVibration();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_setPadVibration(JNIEnv *env, jclass clazz,
                                                     jboolean p_enabled) {
    if (s_settings_interface) {
        s_settings_interface->SetBoolValue("InputSources", "PadVibration", p_enabled == JNI_TRUE);
        s_settings_interface->Save();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_setAspectRatio(JNIEnv *env, jclass clazz,
                                                    jint p_type) {
    EmuConfig.GS.AspectRatio = static_cast<AspectRatioType>(p_type);
    GSConfig.AspectRatio = static_cast<AspectRatioType>(p_type);
    
    // Crucial for Android: we need to update the runtime state as well
    // and reset any custom ratio set by widescreen patches
    EmuConfig.CurrentAspectRatio = static_cast<AspectRatioType>(p_type);
    EmuConfig.CurrentCustomAspectRatio = 0.0f;

    VMManager::ApplySettings();
    if (MTGS::IsOpen())
        MTGS::ApplySettings();
    if (s_settings_interface)
    {
        const auto aspect_ratio_index = static_cast<size_t>(p_type);
        const char* aspect_ratio_name =
            (aspect_ratio_index < static_cast<size_t>(AspectRatioType::MaxCount))
                ? Pcsx2Config::GSOptions::AspectRatioNames[aspect_ratio_index]
                : Pcsx2Config::GSOptions::AspectRatioNames[static_cast<size_t>(AspectRatioType::RAuto4_3_3_2)];
        s_settings_interface->SetStringValue("EmuCore/GS", "AspectRatio", aspect_ratio_name);
        s_settings_interface->Save();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_renderUpscalemultiplier(JNIEnv *env, jclass clazz,
                                                              jfloat p_value) {
    EmuConfig.GS.UpscaleMultiplier = p_value;
    GSConfig.UpscaleMultiplier = p_value;

    VMManager::ApplySettings();
    if (MTGS::IsOpen())
        MTGS::ApplySettings();
    if (s_settings_interface)
    {
        s_settings_interface->SetFloatValue("EmuCore/GS", "upscale_multiplier", p_value);
        s_settings_interface->Save();
    }
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_sbro_emucorex_core_NativeApp_convertIsoToChd(JNIEnv *env, jclass clazz,
                                                    jstring p_inputIsoPath) {
    return -1;
}


extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_isFullscreenUIEnabled(JNIEnv* env, jclass clazz)
{
    return IsFullscreenUIEnabled() ? JNI_TRUE : JNI_FALSE;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_getGameTitle(JNIEnv *env, jclass clazz,
                                                  jstring p_szpath) {
    std::string _szPath = GetJavaString(env, p_szpath);

    const GameList::Entry *entry;
    entry = GameList::GetEntryForPath(_szPath.c_str());

    if (entry == nullptr) {
        const std::string fallbackTitle(Path::GetFileTitle(_szPath));
        return env->NewStringUTF(fallbackTitle.c_str());
    }

    std::string ret;
    ret.append(entry->title);
    ret.append("|");
    ret.append(entry->serial);
    ret.append("|");
    ret.append(StringUtil::StdStringFromFormat("%s (%08X)", entry->serial.c_str(), entry->crc));

    return env->NewStringUTF(ret.c_str());
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_getGameSerial(JNIEnv *env, jclass clazz) {
    std::string ret = VMManager::GetDiscSerial();
    return env->NewStringUTF(ret.c_str());
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_hasWidescreenPatch(JNIEnv* env, jclass clazz)
{
	(void)env;
	(void)clazz;
	if (!VMManager::HasValidVM())
		return JNI_FALSE;
	const std::string serial = VMManager::GetDiscSerial();
	if (serial.empty())
		return JNI_FALSE;
	const u32 crc = VMManager::GetDiscCRC();
	const Patch::PatchInfoList patches = Patch::GetPatchInfo(serial, crc, false, false, nullptr);
	for (const Patch::PatchInfo& info : patches)
	{
		if (info.name == "Widescreen 16:9")
			return JNI_TRUE;
	}
	return JNI_FALSE;
}

extern "C"
JNIEXPORT jfloat JNICALL
Java_com_sbro_emucorex_core_NativeApp_getFPS(JNIEnv *env, jclass clazz) {
    float current_fps = PerformanceMetrics::GetFPS();
    if (current_fps <= 0.0f) {
        current_fps = PerformanceMetrics::GetInternalFPS();
    }
    if (current_fps <= 0.0f && VMManager::HasValidVM()) {
        current_fps = VMManager::GetFrameRate();
    }
    return current_fps;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_renderPreloading(JNIEnv *env, jclass clazz,
                                                      jint p_value) {
    const s32 min_value = 0;
    const s32 max_value = static_cast<s32>(TexturePreloadingLevel::Full);
    const s32 clamped = std::clamp(static_cast<s32>(p_value), min_value, max_value);
    const TexturePreloadingLevel level = static_cast<TexturePreloadingLevel>(clamped);

    EmuConfig.GS.TexturePreloading = level;
    GSConfig.TexturePreloading = level;

    if (s_settings_interface)
    {
        s_settings_interface->SetIntValue("EmuCore/GS", "texture_preloading", clamped);
        s_settings_interface->Save();
    }

    VMManager::ApplySettings();
    if (MTGS::IsOpen())
        MTGS::ApplySettings();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_renderGpu(JNIEnv *env, jclass clazz,
                                               jint p_value) {
    EmuConfig.GS.Renderer = static_cast<GSRendererType>(p_value);
    GSConfig.Renderer = static_cast<GSRendererType>(p_value);

    VMManager::ApplySettings();
    if (MTGS::IsOpen())
        MTGS::ApplySettings();
    if (s_settings_interface)
    {
        s_settings_interface->SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(p_value));
        s_settings_interface->Save();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_setCustomDriverPath(JNIEnv *env, jclass clazz,
                                                          jstring p_path) {
    std::string driver_path = GetJavaString(env, p_path);
    
    // Store old config to check if restart is needed
    Pcsx2Config::GSOptions old_gs_config = GSConfig;
    std::string old_emu_config_path = EmuConfig.GS.CustomDriverPath;

    EmuConfig.GS.CustomDriverPath = driver_path;
    GSConfig.CustomDriverPath = driver_path;

    if (s_settings_interface)
    {
        if (driver_path.empty())
            s_settings_interface->DeleteValue("EmuCore/GS", "CustomDriverPath");
        else
            s_settings_interface->SetStringValue("EmuCore/GS", "CustomDriverPath", driver_path.c_str());
        s_settings_interface->Save();
    }
    
    // If graphics device is already initialized and driver path changed, trigger restart
    if (old_gs_config.CustomDriverPath != driver_path || old_emu_config_path != driver_path)
    {
        // Ensure GSConfig matches EmuConfig (they should already match, but be explicit)
        GSConfig.CustomDriverPath = EmuConfig.GS.CustomDriverPath;
        
        // Trigger graphics restart if device is already open
        if (MTGS::IsOpen())
        {
            MTGS::ApplySettings();
        }
    }
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_getCustomDriverPath(JNIEnv *env, jclass clazz) {
    std::string driver_path;
    if (s_settings_interface)
    {
        s_settings_interface->GetStringValue("EmuCore/GS", "CustomDriverPath", &driver_path);
        EmuConfig.GS.CustomDriverPath = driver_path;
        GSConfig.CustomDriverPath = driver_path;
    }
    else
    {
        driver_path = GSConfig.CustomDriverPath;
    }
    
    return env->NewStringUTF(driver_path.c_str());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_setNativeLibraryDir(JNIEnv *env, jclass clazz,
                                                         jstring p_path) {
    std::string native_lib_dir = GetJavaString(env, p_path);
    if (!native_lib_dir.empty())
    {
        // Set env variable for libadrenotools to use
        setenv("ANDROID_NATIVE_LIB_DIR", native_lib_dir.c_str(), 1);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_setSetting(JNIEnv* env, jclass, jstring j_section, jstring j_key, jstring j_type, jstring j_value)
{
    const std::string section = GetJavaString(env, j_section);
    const std::string key = GetJavaString(env, j_key);
    const std::string type = GetJavaString(env, j_type);
    const std::string value = GetJavaString(env, j_value);

    if (!s_settings_interface)
        return; 
    INISettingsInterface& si = *s_settings_interface;

    if (type == "bool")
    {
        const bool b = (value == "1" || value == "true" || value == "TRUE" || value == "True");
        si.SetBoolValue(section.c_str(), key.c_str(), b);
    }
    else if (type == "int")
    {
        si.SetIntValue(section.c_str(), key.c_str(), static_cast<s32>(std::strtol(value.c_str(), nullptr, 10)));
    }
    else if (type == "uint")
    {
        si.SetUIntValue(section.c_str(), key.c_str(), static_cast<u32>(std::strtoul(value.c_str(), nullptr, 10)));
    }
    else if (type == "float")
    {
        si.SetFloatValue(section.c_str(), key.c_str(), std::strtof(value.c_str(), nullptr));
    }
    else if (type == "double")
    {
        si.SetDoubleValue(section.c_str(), key.c_str(), std::strtod(value.c_str(), nullptr));
    }
    else // string
    {
        si.SetStringValue(section.c_str(), key.c_str(), value.c_str());
    }

    // Apply live where it makes sense
    VMManager::ApplySettings();
    if (MTGS::IsOpen()) {
        MTGS::ApplySettings();
    }

    // Folder settings are cached separately from the INI values.
    // Reload them immediately so BIOS/game-related paths take effect before boot.
    if (section == "Folders")
    {
        if (VMManager::HasValidVM())
            Host::RunOnCPUThread(&VMManager::Internal::UpdateEmuFolders);
        else
            VMManager::Internal::UpdateEmuFolders();
    }

    si.Save();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_getSetting(JNIEnv* env, jclass, jstring j_section, jstring j_key, jstring j_type)
{
    const std::string section = GetJavaString(env, j_section);
    const std::string key = GetJavaString(env, j_key);
    const std::string type = GetJavaString(env, j_type);

    if (type == "bool")
    {
        bool v = false;
        if (s_settings_interface)
            s_settings_interface->GetBoolValue(section.c_str(), key.c_str(), &v);
        return env->NewStringUTF(v ? "true" : "false");
    }
    else if (type == "int")
    {
        s32 v = 0;
        if (s_settings_interface)
            s_settings_interface->GetIntValue(section.c_str(), key.c_str(), &v);
        return env->NewStringUTF(StringUtil::StdStringFromFormat("%d", v).c_str());
    }
    else if (type == "uint")
    {
        u32 v = 0;
        if (s_settings_interface)
            s_settings_interface->GetUIntValue(section.c_str(), key.c_str(), &v);
        return env->NewStringUTF(StringUtil::StdStringFromFormat("%u", v).c_str());
    }
    else if (type == "float")
    {
        float v = 0.0f;
        if (s_settings_interface)
            s_settings_interface->GetFloatValue(section.c_str(), key.c_str(), &v);
        return env->NewStringUTF(StringUtil::StdStringFromFormat("%g", v).c_str());
    }
    else if (type == "double")
    {
        double v = 0.0;
        if (s_settings_interface)
            s_settings_interface->GetDoubleValue(section.c_str(), key.c_str(), &v);
        return env->NewStringUTF(StringUtil::StdStringFromFormat("%g", v).c_str());
    }
    else 
    {
        std::string v;
        if (s_settings_interface)
            s_settings_interface->GetStringValue(section.c_str(), key.c_str(), &v);
        return env->NewStringUTF(v.c_str());
    }
}



extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_onNativeSurfaceCreated(JNIEnv *env, jclass clazz) {
    NativeLogCallback(LOGLEVEL_INFO, Color_White, "Android surface created");
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_onNativeSurfaceChanged(JNIEnv *env, jclass clazz,
                                                            jobject p_surface, jint p_width, jint p_height) {
    NativeLogCallback(LOGLEVEL_INFO, Color_White,
                      StringUtil::StdStringFromFormat("Android surface changed: %dx%d surface=%p",
                                                      static_cast<int>(p_width), static_cast<int>(p_height),
                                                      p_surface));
    if(s_window) {
        ANativeWindow_release(s_window);
        s_window = nullptr;
    }

    if(p_surface != nullptr) {
        s_window = ANativeWindow_fromSurface(env, p_surface);
    }

    if(p_width > 0 && p_height > 0) {
        s_window_width = p_width;
        s_window_height = p_height;
        if(MTGS::IsOpen()) {
            NativeLogCallback(LOGLEVEL_INFO, Color_White, "MTGS open during surface change, updating display window");
            MTGS::UpdateDisplayWindow();
        }
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_onNativeSurfaceDestroyed(JNIEnv *env, jclass clazz) {
    NativeLogCallback(LOGLEVEL_INFO, Color_White, "Android surface destroyed");
    if(s_window) {
        ANativeWindow_release(s_window);
        s_window = nullptr;
    }
}

std::optional<WindowInfo> Host::AcquireRenderWindow(bool recreate_window)
{
    NativeLogCallback(LOGLEVEL_INFO, Color_White,
                      StringUtil::StdStringFromFormat("AcquireRenderWindow recreate=%d window=%p size=%dx%d",
                                                      recreate_window ? 1 : 0, s_window,
                                                      static_cast<int>(s_window_width),
                                                      static_cast<int>(s_window_height)));
    float _fScale = 1.0;
    if (s_window_width > 0 && s_window_height > 0) {
        int _nSize = s_window_width;
        if (s_window_width <= s_window_height) {
            _nSize = s_window_height;
        }
        _fScale = (float)_nSize / 800.0f;
    }
    ////
    WindowInfo _windowInfo;
    memset(&_windowInfo, 0, sizeof(_windowInfo));
    _windowInfo.type = WindowInfo::Type::Android;
    _windowInfo.surface_width = s_window_width;
    _windowInfo.surface_height = s_window_height;
    _windowInfo.surface_scale = _fScale;
    _windowInfo.window_handle = s_window;

    return _windowInfo;
}

void Host::ReleaseRenderWindow() {

}

static s32 s_loop_count = 1;

// Owned by the GS thread.
static u32 s_dump_frame_number = 0;
static u32 s_loop_number = s_loop_count;
static double s_last_internal_draws = 0;
static double s_last_draws = 0;
static double s_last_render_passes = 0;
static double s_last_barriers = 0;
static double s_last_copies = 0;
static double s_last_uploads = 0;
static double s_last_readbacks = 0;
static u64 s_total_internal_draws = 0;
static u64 s_total_draws = 0;
static u64 s_total_render_passes = 0;
static u64 s_total_barriers = 0;
static u64 s_total_copies = 0;
static u64 s_total_uploads = 0;
static u64 s_total_readbacks = 0;
static u32 s_total_frames = 0;
static u32 s_total_drawn_frames = 0;

void Host::BeginPresentFrame() {
    if (GSIsHardwareRenderer())
    {
        const u32 last_draws = s_total_internal_draws;
        const u32 last_uploads = s_total_uploads;

        static constexpr auto update_stat = [](GSPerfMon::counter_t counter, u64& dst, double& last) {
            // perfmon resets every 30 frames to zero
            const double val = g_perfmon.GetCounter(counter);
            dst += static_cast<u64>((val < last) ? val : (val - last));
            last = val;
        };

        update_stat(GSPerfMon::Draw, s_total_internal_draws, s_last_internal_draws);
        update_stat(GSPerfMon::DrawCalls, s_total_draws, s_last_draws);
        
        s_total_frames++;

        // Push metrics to Java every 500ms
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last_fps_sample_time).count();
        if (elapsed >= 500) {
            s_last_fps_sample_time = now;

            auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            if (env && s_native_app_class && s_on_performance_metrics) {
                float fps = PerformanceMetrics::GetFPS();
                if (fps <= 0.0f) fps = PerformanceMetrics::GetInternalFPS();
                const float speed = PerformanceMetrics::GetSpeed();
                float frame_time = PerformanceMetrics::GetAverageFrameTime();
                const float cpu_load = static_cast<float>(PerformanceMetrics::GetCPUThreadUsage());
                float gpu_load = PerformanceMetrics::GetGPUUsage();
                if (gpu_load <= 0.0f)
                    gpu_load = PerformanceMetrics::GetGSThreadUsage();

                env->CallStaticVoidMethod(
                    s_native_app_class,
                    s_on_performance_metrics,
                    fps,
                    speed,
                    frame_time,
                    cpu_load,
                    gpu_load
                );
            }
        }

        std::atomic_thread_fence(std::memory_order_release);
    }
}

void Host::OnGameChanged(const std::string& title, const std::string& elf_override, const std::string& disc_path,
                         const std::string& disc_serial, u32 disc_crc, u32 current_crc) {
}

void Host::PumpMessagesOnCPUThread() {
}

int FileSystem::OpenFDFileContent(const char* filename)
{
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    if(!env || !EnsureNativeAppMethods(env) || !s_native_open_content_uri) {
        return -1;
    }

    jstring j_filename = env->NewStringUTF(filename);
    int fd = env->CallStaticIntMethod(s_native_app_class, s_native_open_content_uri, j_filename);
    env->DeleteLocalRef(j_filename);
    return fd;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeConfigure(JNIEnv* env, jclass, jlong application_id,
                                                         jstring scheme, jstring display_name, jstring image_key)
{
    VMManager::AndroidDiscordConfigure(static_cast<uint64_t>(application_id), GetJavaString(env, scheme),
        GetJavaString(env, display_name), GetJavaString(env, image_key));
    VMManager::InitializeDiscordPresence();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeProvideStoredToken(JNIEnv* env, jclass,
                                                                  jstring access_token, jstring refresh_token,
                                                                  jstring token_type, jlong expires_at,
                                                                  jstring scope)
{
    VMManager::AndroidDiscordProvideStoredToken(GetJavaString(env, access_token), GetJavaString(env, refresh_token),
        GetJavaString(env, token_type), static_cast<int64_t>(expires_at), GetJavaString(env, scope));
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeBeginAuthorize(JNIEnv*, jclass)
{
    VMManager::AndroidDiscordBeginAuthorize();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeSetAppForeground(JNIEnv*, jclass, jboolean is_foreground)
{
    VMManager::AndroidDiscordSetAppForeground(is_foreground == JNI_TRUE);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativePollCallbacks(JNIEnv*, jclass)
{
    VMManager::PollDiscordPresence();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeClearTokens(JNIEnv*, jclass)
{
    VMManager::AndroidDiscordClearTokens();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeIsLoggedIn(JNIEnv*, jclass)
{
    return VMManager::AndroidDiscordIsLoggedIn() ? JNI_TRUE : JNI_FALSE;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeIsClientReady(JNIEnv*, jclass)
{
    return VMManager::AndroidDiscordIsClientReady() ? JNI_TRUE : JNI_FALSE;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeConsumeLastError(JNIEnv* env, jclass)
{
    const std::string error = VMManager::AndroidDiscordConsumeLastError();
    if (error.empty())
        return nullptr;
    return env->NewStringUTF(error.c_str());
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_runVMThread(JNIEnv *env, jclass clazz,
                                                 jstring p_szpath) {
    std::string _szPath = GetJavaString(env, p_szpath);

    /////////////////////////////

    {
    }

    s_execute_exit = false;

//    const char* error;
//    if (!VMManager::PerformEarlyHardwareChecks(&error)) {
//        return false;
//    }

    // fast_boot : (false: bios->game, true: direct-to-game)
    VMBootParameters boot_params;
    boot_params.filename = _szPath;

    if (!VMManager::Internal::CPUThreadInitialize()) {
        VMManager::Internal::CPUThreadShutdown();
    }

    VMManager::ApplySettings();
    GSDumpReplayer::SetIsDumpRunner(false);

    if (VMManager::Initialize(boot_params))
    {
        VMState _vmState = VMState::Running;
        VMManager::SetState(_vmState);
        ////
        while (true) {
            _vmState = VMManager::GetState();
            if (_vmState == VMState::Stopping || _vmState == VMState::Shutdown) {
                break;
            } else if (_vmState == VMState::Running) {
                s_execute_exit = false;
                VMManager::Execute();
                s_execute_exit = true;
            } else {
                usleep(250000);
            }
        }
        ////
        VMManager::Shutdown(false);
    }
    ////
    VMManager::Internal::CPUThreadShutdown();

    return true;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_pause(JNIEnv *env, jclass clazz) {
    std::thread([] {
        VMManager::SetPaused(true);
    }).detach();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_resume(JNIEnv *env, jclass clazz) {
    std::thread([] {
        VMManager::SetPaused(false);
    }).detach();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_shutdown(JNIEnv *env, jclass clazz) {
    std::thread([] {
        VMManager::SetState(VMState::Stopping);
    }).detach();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_refreshBIOS(JNIEnv* env, jclass clazz)
{
    if (!s_settings_interface)
        return;

    if (VMManager::HasValidVM())
        Host::RunOnCPUThread(&VMManager::Internal::UpdateEmuFolders);
    else
        VMManager::Internal::UpdateEmuFolders();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_hasValidVm(JNIEnv*, jclass)
{
    return VMManager::HasValidVM() ? JNI_TRUE : JNI_FALSE;
}


extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_saveStateToSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    if (!VMManager::HasValidVM()) {
        return false;
    }

    std::future<bool> ret = std::async([p_slot]
    {
       if(VMManager::GetDiscCRC() != 0) {
           if(VMManager::GetState() != VMState::Paused) {
               VMManager::SetPaused(true);
           }

           for (int i = 0; i < 50; ++i) {
               if (!VMManager::HasValidVM()) {
                   return false;
               }
               if (VMManager::GetState() == VMState::Paused) {
                   break;
               }
               usleep(100000);
           }
           if(VMManager::GetState() == VMState::Paused) {
               return VMManager::SaveStateToSlot(p_slot, false);
           }
       }
       return false;

    });

    return ret.get();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_loadStateFromSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    if (!VMManager::HasValidVM()) {
        return false;
    }

    std::future<bool> ret = std::async([p_slot]
    {
       u32 _crc = VMManager::GetDiscCRC();
       if(_crc != 0) {
           if (VMManager::HasSaveStateInSlot(VMManager::GetDiscSerial().c_str(), _crc, p_slot)) {
               if(VMManager::GetState() != VMState::Paused) {
                   VMManager::SetPaused(true);
               }

               for (int i = 0; i < 50; ++i) {
                   if (!VMManager::HasValidVM()) {
                       return false;
                   }
                   if (VMManager::GetState() == VMState::Paused) {
                       break;
                   }
                   usleep(100000);
               }
               if(VMManager::GetState() == VMState::Paused) {
                   return VMManager::LoadStateFromSlot(p_slot);
               }
           }
       }
       return false;
    });

    return ret.get();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_getGamePathSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    std::string _filename = VMManager::GetSaveStateFileName(VMManager::GetDiscSerial().c_str(), VMManager::GetDiscCRC(), p_slot);
    if(!_filename.empty()) {
        return env->NewStringUTF(_filename.c_str());
    }
    return nullptr;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_getSaveStatePathForFile(JNIEnv *env, jclass clazz, jstring p_szpath, jint p_slot) {
    std::string _szPath = GetJavaString(env, p_szpath);
    std::string _filename = VMManager::GetSaveStateFileName(_szPath.c_str(), p_slot);
    if(!_filename.empty()) {
        return env->NewStringUTF(_filename.c_str());
    }
    return nullptr;
}

extern "C"
JNIEXPORT jbyteArray JNICALL
Java_com_sbro_emucorex_core_NativeApp_getSaveStateScreenshot(JNIEnv* env, jclass, jstring j_path) {
    const std::string path = GetJavaString(env, j_path);
    if (path.empty())
        return nullptr;

    zip_error_t ze = {};
    auto zf = zip_open_managed(path.c_str(), ZIP_RDONLY, &ze);
    if (!zf)
        return nullptr;

    auto zff = zip_fopen_managed(zf.get(), "Screenshot.png", 0);
    if (!zff)
        return nullptr;

    std::optional<std::vector<u8>> optdata(ReadBinaryFileInZip(zff.get()));
    if (!optdata.has_value() || optdata->empty())
        return nullptr;

    const auto length = static_cast<jsize>(optdata->size());
    jbyteArray retArr = env->NewByteArray(length);
    if (!retArr)
        return nullptr;

    env->SetByteArrayRegion(
        retArr,
        0,
        length,
        reinterpret_cast<const jbyte*>(optdata->data())
    );
    return retArr;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_listMemoryCards(JNIEnv* env, jclass clazz) {
    const std::vector<AvailableMcdInfo> cards = FileMcd_GetAvailableCards(true);

    const auto escape_json = [](const std::string& value) {
        std::string escaped;
        escaped.reserve(value.size() + 8);
        for (const char ch : value) {
            switch (ch) {
                case '\\': escaped += "\\\\"; break;
                case '"': escaped += "\\\""; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default: escaped += ch; break;
            }
        }
        return escaped;
    };

    std::string json = "[";
    bool first = true;
    for (const AvailableMcdInfo& card : cards) {
        if (!first)
            json += ",";
        first = false;
        json += fmt::format(
            "{{\"name\":\"{}\",\"path\":\"{}\",\"modifiedTime\":{},\"type\":{},\"fileType\":{},\"sizeBytes\":{},\"formatted\":{}}}",
            escape_json(card.name),
            escape_json(card.path),
            static_cast<long long>(card.modified_time),
            static_cast<int>(card.type),
            static_cast<int>(card.file_type),
            static_cast<long long>(card.size),
            card.formatted ? "true" : "false"
        );
    }
    json += "]";
    return env->NewStringUTF(json.c_str());
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_createMemoryCard(JNIEnv* env, jclass clazz, jstring j_name, jint j_type, jint j_fileType) {
    const std::string name = GetJavaString(env, j_name);
    if (name.empty())
        return JNI_FALSE;

    const auto type = static_cast<MemoryCardType>(j_type);
    const auto file_type = static_cast<MemoryCardFileType>(j_fileType);
    const bool created = FileMcd_CreateNewCard(name, type, file_type);
    return created ? JNI_TRUE : JNI_FALSE;
}

extern "C"
JNIEXPORT jbyteArray JNICALL
Java_com_sbro_emucorex_core_NativeApp_getImageSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    jbyteArray retArr = nullptr;

    std::string _filename = VMManager::GetSaveStateFileName(VMManager::GetDiscSerial().c_str(), VMManager::GetDiscCRC(), p_slot);
    if(!_filename.empty())
    {
        zip_error_t ze = {};
        auto zf = zip_open_managed(_filename.c_str(), ZIP_RDONLY, &ze);
        if (zf) {
            auto zff = zip_fopen_managed(zf.get(), "Screenshot.png", 0);
            if(zff) {
                std::optional<std::vector<u8>> optdata(ReadBinaryFileInZip(zff.get()));
                if (optdata.has_value()) {
                    std::vector<u8> vec = std::move(optdata.value());
                    ////
                    auto length = static_cast<jsize>(vec.size());
                    retArr = env->NewByteArray(length);
                    if (retArr != nullptr) {
                        env->SetByteArrayRegion(retArr, 0, length,
                                                reinterpret_cast<const jbyte *>(vec.data()));
                    }
                }
            }
        }
    }

    return retArr;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_getRetroAchievementGameData(JNIEnv* env, jclass, jstring j_path) {
    const std::string path = GetJavaString(env, j_path);
    if (path.empty())
        return nullptr;

    const std::string json = FetchRetroAchievementGameDataJson(path);
    if (json.empty())
        return nullptr;

    return env->NewStringUTF(json.c_str());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeRequestState(JNIEnv* env, jclass)
{
    (void)env;
    NotifyRetroAchievementsState();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeLogin(JNIEnv* env, jclass, jstring j_username, jstring j_password)
{
    std::string username = GetJavaString(env, j_username);
    std::string password = GetJavaString(env, j_password);

    if (username.empty() || password.empty())
        return env->NewStringUTF("Username and password are required.");

    EnsureAchievementsClientInitialized();

    Error error;
    if (!Achievements::Login(username.c_str(), password.c_str(), &error))
    {
        if (error.IsValid())
            return env->NewStringUTF(error.GetDescription().c_str());
        return env->NewStringUTF("Login failed.");
    }

    NotifyRetroAchievementsState();
    return nullptr;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeLogout(JNIEnv* env, jclass)
{
    (void)env;
    Achievements::Logout();
    NotifyRetroAchievementsState();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeSetEnabled(JNIEnv* env, jclass, jboolean enabled)
{
    (void)env;
    const bool enable = (enabled == JNI_TRUE);

    if (EmuConfig.Achievements.Enabled == enable)
    {
        Host::SetBaseBoolSettingValue("Achievements", "Enabled", enable);
        Host::CommitBaseSettingChanges();
        NotifyRetroAchievementsState();
        return;
    }

    Pcsx2Config::AchievementsOptions old_config = EmuConfig.Achievements;
    EmuConfig.Achievements.Enabled = enable;
    Host::SetBaseBoolSettingValue("Achievements", "Enabled", enable);
    Host::CommitBaseSettingChanges();
    Achievements::UpdateSettings(old_config);
    NotifyRetroAchievementsState();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeSetHardcore(JNIEnv* env, jclass, jboolean enabled)
{
    (void)env;
    const bool enable = (enabled == JNI_TRUE);

    if (EmuConfig.Achievements.HardcoreMode == enable)
    {
        Host::SetBaseBoolSettingValue("Achievements", "ChallengeMode", enable);
        Host::CommitBaseSettingChanges();
        NotifyRetroAchievementsState();
        return;
    }

    Pcsx2Config::AchievementsOptions old_config = EmuConfig.Achievements;
    EmuConfig.Achievements.HardcoreMode = enable;
    Host::SetBaseBoolSettingValue("Achievements", "ChallengeMode", enable);
    Host::CommitBaseSettingChanges();
    Achievements::UpdateSettings(old_config);
    NotifyRetroAchievementsState();
}


void Host::CommitBaseSettingChanges()
{
    auto lock = GetSettingsLock();
    if (s_settings_interface)
        s_settings_interface->Save();
}

void Host::LoadSettings(SettingsInterface& si, std::unique_lock<std::mutex>& lock)
{
}

void Host::CheckForSettingsChanges(const Pcsx2Config& old_config)
{
}

bool Host::RequestResetSettings(bool folders, bool core, bool controllers, bool hotkeys, bool ui)
{
    // not running any UI, so no settings requests will come in
    return false;
}

void Host::SetDefaultUISettings(SettingsInterface& si)
{
    // nothing
}

std::unique_ptr<ProgressCallback> Host::CreateHostProgressCallback()
{
    return nullptr;
}

void Host::ReportErrorAsync(const std::string_view title, const std::string_view message)
{
    if (!title.empty() && !message.empty())
        ERROR_LOG("ReportErrorAsync: {}: {}", title, message);
    else if (!message.empty())
        ERROR_LOG("ReportErrorAsync: {}", message);
}

bool Host::ConfirmMessage(const std::string_view title, const std::string_view message)
{
    if (!title.empty() && !message.empty())
        ERROR_LOG("ConfirmMessage: {}: {}", title, message);
    else if (!message.empty())
        ERROR_LOG("ConfirmMessage: {}", message);

    return true;
}

void Host::OpenURL(const std::string_view url)
{
    // noop
}

bool Host::CopyTextToClipboard(const std::string_view text)
{
    return false;
}

void Host::BeginTextInput()
{
    // noop
}

void Host::EndTextInput()
{
    // noop
}

std::optional<WindowInfo> Host::GetTopLevelWindowInfo()
{
    return std::nullopt;
}

void Host::OnInputDeviceConnected(const std::string_view identifier, const std::string_view device_name)
{
}

void Host::OnInputDeviceDisconnected(const InputBindingKey key, const std::string_view identifier)
{
}

void Host::SetMouseMode(bool relative_mode, bool hide_cursor)
{
}

void Host::RequestResizeHostDisplay(s32 width, s32 height)
{
}

void Host::OnVMStarting()
{
}

void Host::OnVMStarted()
{
}

void Host::OnVMDestroyed()
{
}

void Host::OnVMPaused()
{
}

void Host::OnVMResumed()
{
}

void Host::OnPerformanceMetricsUpdated()
{
}

void Host::OnSaveStateLoading(const std::string_view filename)
{
}

void Host::OnSaveStateLoaded(const std::string_view filename, bool was_successful)
{
}

void Host::OnSaveStateSaved(const std::string_view filename)
{
}

void Host::RunOnCPUThread(std::function<void()> function, bool block /* = false */)
{
    pxFailRel("Not implemented");
}

void Host::RefreshGameListAsync(bool invalidate_cache)
{
}

void Host::CancelGameListRefresh()
{
}

bool Host::IsFullscreen()
{
    return false;
}

void Host::SetFullscreen(bool enabled)
{
}

void Host::OnCaptureStarted(const std::string& filename)
{
}

void Host::OnCaptureStopped()
{
}

void Host::RequestExitApplication(bool allow_confirm)
{
}

void Host::RequestExitBigPicture()
{
}

void Host::RequestVMShutdown(bool allow_confirm, bool allow_save_state, bool default_save_state)
{
    VMManager::SetState(VMState::Stopping);
}

void Host::OnAchievementsLoginSuccess(const char* username, u32 points, u32 sc_points, u32 unread_messages)
{
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!EnsureRetroAchievementsBridge(env))
        return;

    jstring j_username = username ? env->NewStringUTF(username) : nullptr;
    env->CallStaticVoidMethod(s_ra_bridge_class, s_ra_notify_login_success, j_username,
                              static_cast<jint>(points), static_cast<jint>(sc_points),
                              static_cast<jint>(unread_messages));
    ClearJNIExceptions(env);
    if (j_username)
        env->DeleteLocalRef(j_username);

    NotifyRetroAchievementsState();
}

void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason reason)
{
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!EnsureRetroAchievementsBridge(env))
        return;

    env->CallStaticVoidMethod(s_ra_bridge_class, s_ra_notify_login_requested,
                              static_cast<jint>(reason));
    ClearJNIExceptions(env);
}

void Host::OnAchievementsHardcoreModeChanged(bool enabled)
{
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!EnsureRetroAchievementsBridge(env))
        return;

    env->CallStaticVoidMethod(s_ra_bridge_class, s_ra_notify_hardcore_changed,
                              enabled ? JNI_TRUE : JNI_FALSE);
    ClearJNIExceptions(env);
    NotifyRetroAchievementsState();

}

void Host::OnAchievementsRefreshed()
{
    NotifyRetroAchievementsState();
}

void Host::OnCoverDownloaderOpenRequested()
{
    // noop
}

void Host::OnCreateMemoryCardOpenRequested()
{
    // noop
}

bool Host::ShouldPreferHostFileSelector()
{
    return false;
}

void Host::OpenHostFileSelectorAsync(std::string_view title, bool select_directory, FileSelectorCallback callback,
                                     FileSelectorFilters filters, std::string_view initial_directory)
{
    callback(std::string());
}

std::optional<u32> InputManager::ConvertHostKeyboardStringToCode(const std::string_view str)
{
    return std::nullopt;
}

std::optional<std::string> InputManager::ConvertHostKeyboardCodeToString(u32 code)
{
    return std::nullopt;
}

const char* InputManager::ConvertHostKeyboardCodeToIcon(u32 code)
{
    return nullptr;
}

s32 Host::Internal::GetTranslatedStringImpl(
        const std::string_view context, const std::string_view msg, char* tbuf, size_t tbuf_space)
{
    if (msg.size() > tbuf_space)
        return -1;
    else if (msg.empty())
        return 0;

    std::memcpy(tbuf, msg.data(), msg.size());
    return static_cast<s32>(msg.size());
}

std::string Host::TranslatePluralToString(const char* context, const char* msg, const char* disambiguation, int count)
{
    TinyString count_str = TinyString::from_format("{}", count);

    std::string ret(msg);
    for (;;)
    {
        std::string::size_type pos = ret.find("%n");
        if (pos == std::string::npos)
            break;

        ret.replace(pos, pos + 2, count_str.view());
    }

    return ret;
}

void Host::ReportInfoAsync(const std::string_view title, const std::string_view message)
{
}

bool Host::LocaleCircleConfirm()
{
    return false;
}

bool Host::InNoGUIMode()
{
    return false;
}
