#include <jni.h>
#include <android/log.h>
#include <android/keycodes.h>
#include <android/native_window_jni.h>
#include <unistd.h>
#include <cstdlib>
#include "PrecompiledHeader.h"
#include "common/StringUtil.h"
#include "common/FileSystem.h"
#include "common/ZipHelpers.h"
#include "common/Error.h"
#include "pcsx2/GS.h"
#include "pcsx2/VU1Trace.h"
#include "pcsx2/core/runtime/BuildVersion.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/Config.h"
#include "pcsx2/Counters.h"
#include "pcsx2/SIO/Memcard/MemoryCardFile.h"
#include "pcsx2/SIO/Pad/Pad.h" // For GenericInputBinding

#include "pcsx2/Patch.h"
#include "pcsx2/core/runtime/PerformanceMetrics.h"
#include "pcsx2/core/runtime/GameList.h"
#include "GS/GSPerfMon.h"
#include "GS/GSCapture.h"
#include "GSDumpReplayer.h"
#include "platform/host/audio/AudioStream.h"
#include "ImGui/ImGuiManager.h"
#include "common/Path.h"
#include "common/HTTPDownloader.h"
#include "pcsx2/core/runtime/settings/INISettingsInterface.h"
#include "pcsx2/CDVD/CDVD.h"
#include "pcsx2/CDVD/IsoReader.h"
#include "3rdparty/rcheevos/include/rc_api_request.h"
#include "3rdparty/rcheevos/include/rc_api_runtime.h"
#include "3rdparty/rcheevos/include/rc_api_user.h"
#include "SIO/Pad/Pad.h"
#include "Input/InputManager.h"
#include "ImGui/ImGuiFullscreen.h"
#include "Achievements.h"
#include "platform/host/Host.h"
#include "ImGui/FullscreenUI.h"
#include "SIO/Pad/PadDualshock2.h"
#include "MTGS.h"
#include "SDL3/SDL.h"
#include <future>
#include <memory>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>

namespace
{
	static std::vector<u8> s_imgui_default_font_data;

	static void ConfigureAndroidImGuiFonts()
	{
		const std::string font_path =
			Path::Combine(EmuFolders::Resources, "fonts" FS_OSPATH_SEPARATOR_STR "Roboto-Regular.ttf");
		std::optional<std::vector<u8>> font_data = FileSystem::ReadBinaryFile(font_path.c_str());
		if (!font_data.has_value())
		{
			Console.ErrorFmt("Failed to load default ImGui font '{}'", font_path);
			return;
		}

		s_imgui_default_font_data = std::move(font_data.value());

		ImGuiManager::FontInfo font_info = {};
		font_info.data = s_imgui_default_font_data;
		font_info.exclude_ranges = {};
		font_info.face_name = nullptr;
		font_info.is_emoji_font = false;
		ImGuiManager::SetFonts({font_info});
	}

	struct NativeAppBridgeCache
	{
		jclass app_class = nullptr;
		jmethodID on_pad_vibration = nullptr;
		jmethodID ensure_resource_dir = nullptr;
		jmethodID open_content_uri = nullptr;
		jmethodID native_log = nullptr;
		jmethodID on_performance_metrics = nullptr;
		std::chrono::steady_clock::time_point last_fps_sample_time{};
	};

	struct RetroAchievementsBridgeCache
	{
		jclass bridge_class = nullptr;
		jmethodID notify_login_requested = nullptr;
		jmethodID notify_login_success = nullptr;
		jmethodID notify_state_changed = nullptr;
		jmethodID notify_hardcore_changed = nullptr;
		std::mutex mutex;
	};

	static NativeAppBridgeCache& GetNativeAppBridgeCache()
	{
		static NativeAppBridgeCache s_native_app_bridge_cache;
		return s_native_app_bridge_cache;
	}

	static RetroAchievementsBridgeCache& GetRetroAchievementsBridgeCache()
	{
		static RetroAchievementsBridgeCache s_ra_bridge_cache;
		return s_ra_bridge_cache;
	}

	struct AndroidSurfaceState
	{
		int window_width = 0;
		int window_height = 0;
		ANativeWindow* window = nullptr;
		jobject surface_object = nullptr;
		std::mutex mutex;
	};

	struct CpuThreadBridgeState
	{
		bool execute_exit = false;
		std::mutex queue_mutex;
		std::deque<std::function<void()>> queue;
		std::thread::id thread_id;
		bool active = false;
	};

	struct AndroidPerformanceOverlayBridgeState
	{
		bool visible = false;
		bool detailed = true;
	};

	static AndroidSurfaceState& GetAndroidSurfaceState()
	{
		static AndroidSurfaceState s_android_surface_state;
		return s_android_surface_state;
	}

	static CpuThreadBridgeState& GetCpuThreadBridgeState()
	{
		static CpuThreadBridgeState s_cpu_thread_bridge_state;
		return s_cpu_thread_bridge_state;
	}

	static AndroidPerformanceOverlayBridgeState& GetAndroidPerformanceOverlayBridgeState()
	{
		static AndroidPerformanceOverlayBridgeState s_android_performance_overlay_state;
		return s_android_performance_overlay_state;
	}

	static void ClearJNIExceptions(JNIEnv* env)
	{
		if (env && env->ExceptionCheck())
		{
			env->ExceptionDescribe();
			env->ExceptionClear();
		}
	}

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

    static void AppendProcessorStat(std::string& text, std::string_view label, double usage, double time_ms)
    {
        if (!text.empty())
            text.push_back('\n');

        if (usage >= 99.95)
            text += fmt::format("{}: 100% ({:.2f}ms)", label, time_ms);
        else
            text += fmt::format("{}: {:.1f}% ({:.2f}ms)", label, usage, time_ms);
    }

    static std::string BuildAndroidPerformanceOverlayText()
    {
        std::string text;

        switch (PerformanceMetrics::GetInternalFPSMethod())
        {
            case PerformanceMetrics::InternalFPSMethod::GSPrivilegedRegister:
                text += fmt::format("FPS: {:.2f} [P]", PerformanceMetrics::GetInternalFPS());
                break;

            case PerformanceMetrics::InternalFPSMethod::DISPFBBlit:
                text += fmt::format("FPS: {:.2f} [B]", PerformanceMetrics::GetInternalFPS());
                break;

            case PerformanceMetrics::InternalFPSMethod::None:
            default:
                text += "FPS: N/A";
                break;
        }

        text += fmt::format(" | VPS: {:.2f}", PerformanceMetrics::GetFPS());

        const float speed_percent = PerformanceMetrics::GetSpeed() * 100.0f;
        text += fmt::format(" | Speed: {}%", static_cast<u32>(std::round(speed_percent)));

        const float target_speed = VMManager::GetTargetSpeed();
        if (target_speed == 0.0f)
            text += " (T: Max)";
        else
            text += fmt::format(" (T: {:.0f}%)", target_speed * 100.0f);

        SmallString gs_stats_line;
        SmallString gs_memory_stats_line;
        GSgetStats(gs_stats_line);
        GSgetMemoryStats(gs_memory_stats_line);
        if (!gs_stats_line.empty())
            text += fmt::format("\n{}", gs_stats_line.view());
        if (!gs_memory_stats_line.empty())
            text += fmt::format("\n{}", gs_memory_stats_line.view());

        int iwidth = 0;
        int iheight = 0;
        GSgetInternalResolution(&iwidth, &iheight);
        text += fmt::format(
            "\n{}x{} {} {} | QF: {} | Frame: {:.2f}/{:.2f}/{:.2f} ms",
            iwidth,
            iheight,
            ReportVideoMode(),
            ReportInterlaceMode(),
            MTGS::GetCurrentVsyncQueueSize() > 0 ? (MTGS::GetCurrentVsyncQueueSize() - 1) : 0,
            PerformanceMetrics::GetMinimumFrameTime(),
            PerformanceMetrics::GetAverageFrameTime(),
            PerformanceMetrics::GetMaximumFrameTime());

        if (EmuConfig.Speedhacks.EECycleRate != 0 || EmuConfig.Speedhacks.EECycleSkip != 0)
        {
            AppendProcessorStat(
                text,
                fmt::format("EE[{}/{}]", EmuConfig.Speedhacks.EECycleRate, EmuConfig.Speedhacks.EECycleSkip),
                PerformanceMetrics::GetCPUThreadUsage(),
                PerformanceMetrics::GetCPUThreadAverageTime());
        }
        else
        {
            AppendProcessorStat(
                text,
                "EE",
                PerformanceMetrics::GetCPUThreadUsage(),
                PerformanceMetrics::GetCPUThreadAverageTime());
        }

        AppendProcessorStat(text, "GS", PerformanceMetrics::GetGSThreadUsage(), PerformanceMetrics::GetGSThreadAverageTime());

        if (THREAD_VU1)
            AppendProcessorStat(text, "VU", PerformanceMetrics::GetVUThreadUsage(), PerformanceMetrics::GetVUThreadAverageTime());

        const u32 gs_sw_threads = PerformanceMetrics::GetGSSWThreadCount();
        for (u32 thread = 0; thread < gs_sw_threads; thread++)
        {
            AppendProcessorStat(
                text,
                fmt::format("SW-{}", thread),
                PerformanceMetrics::GetGSSWThreadUsage(thread),
                PerformanceMetrics::GetGSSWThreadAverageTime(thread));
        }

        if (GSCapture::IsCapturing())
            AppendProcessorStat(text, "CAP", PerformanceMetrics::GetCaptureThreadUsage(), PerformanceMetrics::GetCaptureThreadAverageTime());

        const float gpu_usage = PerformanceMetrics::GetGPUUsage();
        const float gpu_time = PerformanceMetrics::GetGPUAverageTime();
        if (gpu_usage > 0.05f || gpu_time > 0.05f)
            AppendProcessorStat(text, "GPU", gpu_usage, gpu_time);

        return text;
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

        auto callback = [out_result](s32 status_code, const std::string&, HTTPDownloader::Request::Data data) {
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
            game_hash = Achievements::GetGameHash(path);
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
                game_hash = Achievements::GetGameHash(elf_path);

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
        const std::string token = Host::GetStringSettingValue("Achievements", "Token");

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
                if (rc_api_process_fetch_user_unlocks_server_response(&unlocks_response, &unlocks_server_response) ==
                        RC_OK &&
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
            json += fmt::format("\"description\":\"{}\",",
                                EscapeJsonString(achievement.description ? achievement.description : ""));
            json += fmt::format("\"points\":{},", achievement.points);
            json += fmt::format("\"category\":{},", achievement.category);
            json += fmt::format("\"type\":{},", achievement.type);
            json += fmt::format("\"rarity\":{:.2f},", achievement.rarity);
            json += fmt::format("\"rarityHardcore\":{:.2f},", achievement.rarity_hardcore);
            json += fmt::format("\"earnedSoftcore\":{},", earnedSoftcore ? "true" : "false");
            json += fmt::format("\"earnedHardcore\":{},", earnedHardcore ? "true" : "false");
            json += fmt::format("\"badgeUrl\":\"{}\",",
                                EscapeJsonString(BuildRcImageUrl(achievement.badge_name, RC_IMAGE_TYPE_ACHIEVEMENT)));
            json += fmt::format(
                "\"badgeLockedUrl\":\"{}\"",
                EscapeJsonString(BuildRcImageUrl(achievement.badge_name, RC_IMAGE_TYPE_ACHIEVEMENT_LOCKED)));
            json += "}";
        }

        json += "]}";

        rc_api_destroy_fetch_game_data_response(&game_response);
        rc_api_destroy_resolve_hash_response(&resolve_response);
        return json;
    }

    static bool EnsureNativeAppMethods(JNIEnv* env)
    {
        NativeAppBridgeCache& native_app_bridge = GetNativeAppBridgeCache();

        if (!env)
            return false;

        if (!native_app_bridge.app_class)
        {
            jclass local = env->FindClass("com/sbro/emucorex/core/NativeApp");
            if (!local)
            {
                ClearJNIExceptions(env);
                return false;
            }

            native_app_bridge.app_class = reinterpret_cast<jclass>(env->NewGlobalRef(local));
            env->DeleteLocalRef(local);
            if (!native_app_bridge.app_class)
            {
                ClearJNIExceptions(env);
                return false;
            }
        }

        if (!native_app_bridge.on_pad_vibration)
            native_app_bridge.on_pad_vibration =
                env->GetStaticMethodID(native_app_bridge.app_class, "onPadVibration", "(IFF)V");

        if (!native_app_bridge.ensure_resource_dir)
            native_app_bridge.ensure_resource_dir = env->GetStaticMethodID(
                native_app_bridge.app_class, "ensureResourceSubdirectoryCopied", "(Ljava/lang/String;)V");

        if (!native_app_bridge.native_log)
            native_app_bridge.native_log =
                env->GetStaticMethodID(native_app_bridge.app_class, "nativeLog", "(Ljava/lang/String;)V");

        if (!native_app_bridge.open_content_uri)
            native_app_bridge.open_content_uri =
                env->GetStaticMethodID(native_app_bridge.app_class, "openContentUri", "(Ljava/lang/String;)I");

        if (!native_app_bridge.on_performance_metrics)
            native_app_bridge.on_performance_metrics =
                env->GetStaticMethodID(native_app_bridge.app_class, "onPerformanceMetrics", "(Ljava/lang/String;FF)V");

        ClearJNIExceptions(env);
        return native_app_bridge.app_class != nullptr;
    }

    static void NativeLogCallback(LOGLEVEL level, ConsoleColors color, std::string_view message)
    {
        NativeAppBridgeCache& native_app_bridge = GetNativeAppBridgeCache();

        int android_level = ANDROID_LOG_DEBUG;
        switch (level)
        {
            case LOGLEVEL_ERROR:
                android_level = ANDROID_LOG_ERROR;
                break;
            case LOGLEVEL_WARNING:
                android_level = ANDROID_LOG_WARN;
                break;
            case LOGLEVEL_INFO:
            case LOGLEVEL_DEV:
                android_level = ANDROID_LOG_INFO;
                break;
            case LOGLEVEL_DEBUG:
            case LOGLEVEL_TRACE:
                android_level = ANDROID_LOG_DEBUG;
                break;
            case LOGLEVEL_NONE:
            case LOGLEVEL_COUNT:
                android_level = ANDROID_LOG_DEBUG;
                break;
        }

        __android_log_print(android_level, "NativeCore", "%.*s",
            static_cast<int>(message.size()), message.data());

        auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (!env || !native_app_bridge.app_class || !native_app_bridge.native_log)
            return;

        jstring j_msg = env->NewStringUTF(std::string(message).c_str());
        env->CallStaticVoidMethod(native_app_bridge.app_class, native_app_bridge.native_log, j_msg);
        env->DeleteLocalRef(j_msg);
        ClearJNIExceptions(env);
    }

	static bool EnsureRetroAchievementsBridge(JNIEnv* env)
	{
		RetroAchievementsBridgeCache& ra_bridge = GetRetroAchievementsBridgeCache();

		if (!env)
			return false;

        std::lock_guard<std::mutex> lock(ra_bridge.mutex);
        if (!ra_bridge.bridge_class)
		{
			jclass local = env->FindClass("com/sbro/emucorex/core/utils/RetroAchievementsBridge");
			if (!local)
				return false;

			ra_bridge.bridge_class = reinterpret_cast<jclass>(env->NewGlobalRef(local));
			env->DeleteLocalRef(local);
			if (!ra_bridge.bridge_class)
				return false;

			ra_bridge.notify_login_requested =
				env->GetStaticMethodID(ra_bridge.bridge_class, "notifyLoginRequested", "(I)V");
			ra_bridge.notify_login_success =
				env->GetStaticMethodID(ra_bridge.bridge_class, "notifyLoginSuccess", "(Ljava/lang/String;III)V");
			ra_bridge.notify_state_changed = env->GetStaticMethodID(
				ra_bridge.bridge_class, "notifyStateChanged",
				"(ZZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;IIIZZZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;IIIIIZZZ)V");
			ra_bridge.notify_hardcore_changed =
				env->GetStaticMethodID(ra_bridge.bridge_class, "notifyHardcoreModeChanged", "(Z)V");

			if (!ra_bridge.notify_login_requested || !ra_bridge.notify_login_success ||
				!ra_bridge.notify_state_changed || !ra_bridge.notify_hardcore_changed)
			{
				return false;
			}
		}

		return true;
	}

	static void NotifyRetroAchievementsState()
	{
		RetroAchievementsBridgeCache& ra_bridge = GetRetroAchievementsBridgeCache();
        auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (!env)
            return;
		if (!EnsureRetroAchievementsBridge(env))
			return;

		const bool achievements_enabled = EmuConfig.Achievements.Enabled;
		const bool hardcore_preference = EmuConfig.Achievements.HardcoreMode;
		const bool hardcore_active = Achievements::IsHardcoreModeActive();
		auto lock = Achievements::GetLock();

		const char* logged_in_user = Achievements::GetLoggedInUserName();
		const bool have_user = (logged_in_user && logged_in_user[0] != '\0');
		const bool have_game = Achievements::HasActiveGame();
		const std::string username = have_user ? std::string(logged_in_user) : std::string();
		const std::string display_name = username;
		const std::string avatar_path = have_user ? Achievements::GetLoggedInUserBadgePath() : std::string();
		const jint points = static_cast<jint>(have_user ? Achievements::GetLoggedInUserScore() : 0);
		const jint softcore_points = static_cast<jint>(have_user ? Achievements::GetLoggedInUserSoftcoreScore() : 0);
		const jint unread_messages = static_cast<jint>(have_user ? Achievements::GetLoggedInUserUnreadMessages() : 0);
		const std::string game_title = have_game ? Achievements::GetGameTitle() : std::string();
		const std::string rich_presence =
			(have_game && Achievements::HasRichPresence()) ? Achievements::GetRichPresenceString() : std::string();
		const std::string icon_path = (have_game && Achievements::HasAchievementsOrLeaderboards()) ?
			Achievements::GetGameIconURL() :
			std::string();
		const jint game_id = static_cast<jint>(have_game ? Achievements::GetGameID() : 0);
		const jint total_achievements = static_cast<jint>(have_game ? Achievements::GetGameAchievementCount() : 0);
		const jint unlocked_achievements = static_cast<jint>(have_game ? Achievements::GetUnlockedGameAchievementCount() : 0);
		const jint total_points = static_cast<jint>(have_game ? Achievements::GetGamePoints() : 0);
		const jint unlocked_points = static_cast<jint>(have_game ? Achievements::GetUnlockedGamePoints() : 0);

		jstring j_username = have_user ? env->NewStringUTF(username.c_str()) : nullptr;
		jstring j_display_name = have_user ? env->NewStringUTF(display_name.c_str()) : nullptr;
		jstring j_avatar = !avatar_path.empty() ?
			env->NewStringUTF(avatar_path.c_str()) :
			nullptr;
		jstring j_game_title = have_game ? env->NewStringUTF(game_title.c_str()) : nullptr;
		jstring j_rich_presence = !rich_presence.empty() ?
			env->NewStringUTF(rich_presence.c_str()) :
			nullptr;
		jstring j_icon_path = !icon_path.empty() ?
			env->NewStringUTF(icon_path.c_str()) :
			nullptr;

		env->CallStaticVoidMethod(
			ra_bridge.bridge_class, ra_bridge.notify_state_changed,
			achievements_enabled ? JNI_TRUE : JNI_FALSE,
			have_user ? JNI_TRUE : JNI_FALSE,
			j_username,
			j_display_name,
			j_avatar,
			points,
			softcore_points,
			unread_messages,
			hardcore_preference ? JNI_TRUE : JNI_FALSE,
			hardcore_active ? JNI_TRUE : JNI_FALSE,
			have_game ? JNI_TRUE : JNI_FALSE,
			j_game_title,
			j_rich_presence,
			j_icon_path,
			game_id,
			total_achievements,
			unlocked_achievements,
			unlocked_points,
			total_points,
			hardcore_active ? JNI_TRUE : JNI_FALSE,
			(have_game && Achievements::HasLeaderboards()) ? JNI_TRUE : JNI_FALSE,
			(have_game && Achievements::HasRichPresence()) ? JNI_TRUE : JNI_FALSE);

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
    NativeAppBridgeCache& native_app_bridge = GetNativeAppBridgeCache();
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env)
        return;

    if (!EnsureNativeAppMethods(env) || !native_app_bridge.ensure_resource_dir)
        return;

    const char* safe_path = relative_path ? relative_path : "";
    jstring j_path = env->NewStringUTF(safe_path);
    env->CallStaticVoidMethod(native_app_bridge.app_class, native_app_bridge.ensure_resource_dir, j_path);
    if (j_path)
        env->DeleteLocalRef(j_path);
    ClearJNIExceptions(env);
}
} // namespace Host::Internal

void AndroidUpdatePadVibration(u32 pad_index, float large_intensity, float small_intensity)
{
    NativeAppBridgeCache& native_app_bridge = GetNativeAppBridgeCache();
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env)
        return;

    if (!EnsureNativeAppMethods(env) || !native_app_bridge.on_pad_vibration)
        return;

    env->CallStaticVoidMethod(native_app_bridge.app_class, native_app_bridge.on_pad_vibration, static_cast<jint>(pad_index),
                              static_cast<jfloat>(large_intensity), static_cast<jfloat>(small_intensity));
}

struct AndroidSettingsBridgeState
{
    std::unique_ptr<INISettingsInterface> interface;
    u32 batch_depth = 0;
    bool batch_dirty = false;
    bool batch_folders_dirty = false;
};

static AndroidSettingsBridgeState& GetAndroidSettingsBridgeState()
{
    static AndroidSettingsBridgeState s_android_settings_bridge_state;
    return s_android_settings_bridge_state;
}

static std::unique_ptr<INISettingsInterface>& GetSettingsInterfaceStorage()
{
    return GetAndroidSettingsBridgeState().interface;
}

static u32& GetSettingsBatchDepth()
{
    return GetAndroidSettingsBridgeState().batch_depth;
}

static bool& GetSettingsBatchDirty()
{
    return GetAndroidSettingsBridgeState().batch_dirty;
}

static bool& GetSettingsBatchFoldersDirty()
{
    return GetAndroidSettingsBridgeState().batch_folders_dirty;
}

static bool IsOnCPUThread()
{
    CpuThreadBridgeState& cpu_thread_state = GetCpuThreadBridgeState();
    std::lock_guard<std::mutex> lock(cpu_thread_state.queue_mutex);
    return cpu_thread_state.active && cpu_thread_state.thread_id == std::this_thread::get_id();
}

static void SetCPUThreadActive(bool active)
{
    CpuThreadBridgeState& cpu_thread_state = GetCpuThreadBridgeState();
    std::lock_guard<std::mutex> lock(cpu_thread_state.queue_mutex);
    cpu_thread_state.active = active;
    cpu_thread_state.thread_id = active ? std::this_thread::get_id() : std::thread::id();
}

static void QueueCPUThreadWork(std::function<void()> function)
{
    CpuThreadBridgeState& cpu_thread_state = GetCpuThreadBridgeState();
    std::lock_guard<std::mutex> lock(cpu_thread_state.queue_mutex);
    cpu_thread_state.queue.emplace_back(std::move(function));
}

static void DrainCPUThreadQueue()
{
    CpuThreadBridgeState& cpu_thread_state = GetCpuThreadBridgeState();
    std::deque<std::function<void()>> pending;
    {
        std::lock_guard<std::mutex> lock(cpu_thread_state.queue_mutex);
        if (cpu_thread_state.queue.empty())
            return;

        pending.swap(cpu_thread_state.queue);
    }

    while (!pending.empty())
    {
        std::function<void()> fn = std::move(pending.front());
        pending.pop_front();
        fn();
    }
}

static float GetAndroidSurfaceScale(int window_width, int window_height)
{
    if (window_width <= 0 || window_height <= 0)
        return 1.0f;

    const int max_dimension = std::max(window_width, window_height);
    return static_cast<float>(max_dimension) / 800.0f;
}

static void ApplySettingsForRuntimeChange()
{
    if (VMManager::HasValidVM())
    {
        Host::RunOnCPUThread([]() {
            VMManager::ApplySettings();
            if (MTGS::IsOpen())
                MTGS::ApplySettings();
        }, true);
        return;
    }

    VMManager::ApplySettings();
    if (MTGS::IsOpen())
        MTGS::ApplySettings();
}

static void FlushPendingSettingsChanges()
{
    if (!GetSettingsInterfaceStorage())
        return;

    if (GetSettingsBatchDirty())
    {
        // Cold-boot startup writes can touch dozens of settings at once. Deferring the actual
        // ApplySettings() until the VM boot path avoids an extra pre-boot reconfigure pass
        // that can briefly disturb timing-sensitive GS/SPU2 startup behavior.
        if (VMManager::HasValidVM())
            ApplySettingsForRuntimeChange();

        if (GetSettingsBatchFoldersDirty())
        {
            if (VMManager::HasValidVM())
                Host::RunOnCPUThread(&VMManager::Internal::UpdateEmuFolders);
            else
                VMManager::Internal::UpdateEmuFolders();
        }

        GetSettingsInterfaceStorage()->Save();
    }

    GetSettingsBatchDirty() = false;
    GetSettingsBatchFoldersDirty() = false;
}

static void RunGSRuntimeChange(std::function<void()> function, bool apply_settings = true)
{
    if (!function)
        return;

    if (VMManager::HasValidVM())
    {
        Host::RunOnCPUThread([fn = std::move(function), apply_settings]() mutable {
            fn();
            if (apply_settings)
            {
                VMManager::ApplySettings();
                if (MTGS::IsOpen())
                    MTGS::ApplySettings();
            }
        }, true);
        return;
    }

    function();
    if (apply_settings)
    {
        VMManager::ApplySettings();
        if (MTGS::IsOpen())
            MTGS::ApplySettings();
    }
}

static bool NormalizeMandatoryFixSettings(INISettingsInterface& si)
{
    static_cast<void>(si);
    return false;
}

static bool ApplyAndroidGsBootstrapDefaults(INISettingsInterface& si, bool only_if_missing)
{
    bool changed = false;

    const auto set_int = [&](const char* key, s32 value) {
        if (only_if_missing && si.ContainsValue("EmuCore/GS", key))
            return;
        si.SetIntValue("EmuCore/GS", key, value);
        changed = true;
    };
    const auto set_bool = [&](const char* key, bool value) {
        if (only_if_missing && si.ContainsValue("EmuCore/GS", key))
            return;
        si.SetBoolValue("EmuCore/GS", key, value);
        changed = true;
    };

    // Keep Android bootstrap aligned with the legacy UI defaults rather than
    // the heavier upstream GS defaults, which were regressing OpenGL.
    set_int("accurate_blending_unit", 0);
    set_int("filter", 2);
    set_int("TriFilter", -1);
    set_int("texture_preloading", 1);
    set_int("linear_present_mode", 1);
    set_int("dithering_ps2", 2);
    set_int("MaxAnisotropy", 0);
    set_bool("fxaa", false);
    set_int("CASMode", 0);
    set_bool("hw_mipmap", false);

    return changed;
}

static bool ApplyAndroidInputBootstrapDefaults(INISettingsInterface& si, bool only_if_missing)
{
    bool changed = false;

    const auto set_bool = [&](const char* key, bool value, bool force_existing_value) {
        if (only_if_missing && !force_existing_value && si.ContainsValue("InputSources", key))
            return;

        bool current_value = value;
        if (si.GetBoolValue("InputSources", key, &current_value) && current_value == value)
            return;

        si.SetBoolValue("InputSources", key, value);
        changed = true;
    };

    // Android routes controller input through MainActivity/GamepadManager -> NativeApp.setPadButton().
    // Keep the SDL source disabled here so startup does not keep probing a backend which fails to init.
    set_bool("SDL", false, true);
    set_bool("XInput", false, false);

    return changed;
}

static bool SyncManualGsHackToggle(INISettingsInterface& si)
{
    auto read_bool = [&](const char* key, bool default_value) {
        bool value = default_value;
        si.GetBoolValue("EmuCore/GS", key, &value);
        return value;
    };
    auto read_int = [&](const char* key, s32 default_value) {
        s32 value = default_value;
        si.GetIntValue("EmuCore/GS", key, &value);
        return value;
    };

    const bool has_manual_fix =
        read_int("UserHacks_CPUSpriteRenderBW", 0) != 0 ||
        read_int("UserHacks_CPUSpriteRenderLevel", 0) != 0 ||
        read_int("UserHacks_CPUCLUTRender", 0) != 0 ||
        read_int("UserHacks_GPUTargetCLUTMode", 0) != 0 ||
        read_int("UserHacks_SkipDraw_Start", 0) != 0 ||
        read_int("UserHacks_SkipDraw_End", 0) != 0 ||
        read_int("UserHacks_AutoFlushLevel", 0) != 0 ||
        read_bool("UserHacks_CPU_FB_Conversion", false) ||
        read_bool("UserHacks_DisableDepthSupport", false) ||
        read_bool("UserHacks_Disable_Safe_Features", false) ||
        read_bool("UserHacks_DisableRenderFixes", false) ||
        read_bool("preload_frame_with_gs_data", false) ||
        read_bool("UserHacks_DisablePartialInvalidation", false) ||
        read_int("UserHacks_TextureInsideRt", 0) != 0 ||
        read_bool("UserHacks_ReadTCOnClose", false) ||
        read_bool("UserHacks_EstimateTextureRegion", false) ||
        read_bool("paltex", false) ||
        read_int("UserHacks_HalfPixelOffset", 0) != 0 ||
        read_int("UserHacks_native_scaling", 0) != 0 ||
        read_int("UserHacks_round_sprite_offset", 0) != 0 ||
        read_int("UserHacks_BilinearHack", 0) != 0 ||
        read_int("UserHacks_TCOffsetX", 0) != 0 ||
        read_int("UserHacks_TCOffsetY", 0) != 0 ||
        read_bool("UserHacks_align_sprite_X", false) ||
        read_bool("UserHacks_merge_pp_sprite", false) ||
        read_bool("UserHacks_ForceEvenSpritePosition", false) ||
        read_bool("UserHacks_NativePaletteDraw", false);

    bool manual_user_hacks = false;
    const bool had_value = si.GetBoolValue("EmuCore/GS", "UserHacks", &manual_user_hacks);
    if (!has_manual_fix || (had_value && manual_user_hacks))
        return false;

    si.SetBoolValue("EmuCore/GS", "UserHacks", true);
    return true;
}

template <typename T>
static T RunOnCPUThreadBlocking(T fallback_value, std::function<T()> function)
{
    if (!VMManager::HasValidVM())
        return fallback_value;

    T result = fallback_value;
    Host::RunOnCPUThread([&result, fn = std::move(function)]() mutable {
        result = fn();
    }, true);
    return result;
}

static bool IsFullscreenUIEnabled()
{
    if (!GetSettingsInterfaceStorage())
        return false;
    return GetSettingsInterfaceStorage()->GetBoolValue("UI", "EnableFullscreenUI", false);
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

static std::string GetCoreVersionString()
{
    const char* primary_version = BuildVersion::GitTaggedCommit ? BuildVersion::GitTag : BuildVersion::GitRev;
    const char* hash = BuildVersion::GitHash;
    const char* date = BuildVersion::GitDate;

    const auto is_placeholder = [](const char* value) {
        return (!value || value[0] == '\0' || StringUtil::Strcasecmp(value, "Unknown") == 0 ||
                StringUtil::Strcasecmp(value, "Android") == 0);
    };

    // Android app builds in this repo can intentionally skip Git metadata during CMake
    // generation, which leaves BuildVersion::GitRev as the unhelpful placeholder "Android".
    // Keep a manual core-base fallback here so the About screen still shows the emulator core,
    // not the target platform label.
    if (is_placeholder(primary_version))
    {
        primary_version = "PCSX2 v2.7.217";
        hash = "724155ba";
        date = nullptr;
    }

    std::string version = (primary_version && primary_version[0] != '\0') ? primary_version : "unknown";
    if (hash && hash[0] != '\0')
    {
        version.append(" (");
        version.append(hash);
        version.push_back(')');
    }
    if (date && date[0] != '\0')
    {
        version.append(" - ");
        version.append(date);
    }

    return version;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_initialize(JNIEnv *env, jclass clazz,
                                                jstring p_szpath, jint p_apiVer) {
    std::string _szPath = GetJavaString(env, p_szpath);
    EmuFolders::AppRoot = _szPath;
    EmuFolders::DataRoot = _szPath;
    EmuFolders::SetResourcesDirectory();
    ConfigureAndroidImGuiFonts();

    Log::SetConsoleOutputLevel(LOGLEVEL_DEBUG);
    if (!GetSettingsInterfaceStorage())
    {
        const std::string ini_path = EmuFolders::DataRoot + "/PCSX2-Android.ini";
        GetSettingsInterfaceStorage() = std::make_unique<INISettingsInterface>(ini_path);
        Host::Internal::SetBaseSettingsLayer(GetSettingsInterfaceStorage().get());
        GetSettingsInterfaceStorage()->Load();
        if (GetSettingsInterfaceStorage()->IsEmpty())
        {
            VMManager::SetDefaultSettings(*GetSettingsInterfaceStorage(), true, true, true, true, true);
            GetSettingsInterfaceStorage()->SetBoolValue("EmuCore", "EnableDiscordPresence", true);
            GetSettingsInterfaceStorage()->SetIntValue("EmuCore/GS", "VsyncEnable", false);
            GetSettingsInterfaceStorage()->SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(GSRendererType::OGL));
            ApplyAndroidInputBootstrapDefaults(*GetSettingsInterfaceStorage(), false);
            GetSettingsInterfaceStorage()->SetBoolValue("Logging", "EnableSystemConsole", true);
            GetSettingsInterfaceStorage()->SetBoolValue("Logging", "EnableTimestamps", true);
            GetSettingsInterfaceStorage()->SetBoolValue("Logging", "EnableVerbose", false);
            GetSettingsInterfaceStorage()->SetBoolValue("EmuCore/GS", "OsdShowFPS", false);
            GetSettingsInterfaceStorage()->SetBoolValue("EmuCore/GS", "OsdShowResolution", false);
            GetSettingsInterfaceStorage()->SetBoolValue("EmuCore/GS", "OsdShowGSStats", false);
            GetSettingsInterfaceStorage()->SetBoolValue("EmuCore/GS", "OsdShowIndicators", false);
            GetSettingsInterfaceStorage()->SetIntValue("EmuCore/GS", "OsdPerformancePos", 0); 
            ApplyAndroidGsBootstrapDefaults(*GetSettingsInterfaceStorage(), false);
            GetSettingsInterfaceStorage()->SetBoolValue("UI", "EnableFullscreenUI", false);
            GetSettingsInterfaceStorage()->SetBoolValue("UI", "ExpandIntoDisplayCutout", true);
            GetSettingsInterfaceStorage()->SetBoolValue("Achievements", "Enabled", false);
            GetSettingsInterfaceStorage()->SetBoolValue("Achievements", "ChallengeMode", false);
            GetSettingsInterfaceStorage()->SetBoolValue("Achievements", "AndroidMigrationV1", true);
            NormalizeMandatoryFixSettings(*GetSettingsInterfaceStorage());
            GetSettingsInterfaceStorage()->Save();
        }
        else
        {
            bool needs_save = false;
            if (!GetSettingsInterfaceStorage()->GetBoolValue("Achievements", "AndroidMigrationV1", false))
            {
                if (!GetSettingsInterfaceStorage()->ContainsValue("Achievements", "Enabled"))
                    GetSettingsInterfaceStorage()->SetBoolValue("Achievements", "Enabled", false);
                GetSettingsInterfaceStorage()->SetBoolValue("Achievements", "AndroidMigrationV1", true);
                needs_save = true;
            }
            if (!GetSettingsInterfaceStorage()->ContainsValue("Achievements", "ChallengeMode"))
            {
                GetSettingsInterfaceStorage()->SetBoolValue("Achievements", "ChallengeMode", false);
                needs_save = true;
            }
            if (!GetSettingsInterfaceStorage()->ContainsValue("UI", "ExpandIntoDisplayCutout"))
            {
                GetSettingsInterfaceStorage()->SetBoolValue("UI", "ExpandIntoDisplayCutout", true);
                needs_save = true;
            }
            if (!GetSettingsInterfaceStorage()->ContainsValue("SPU2/Output", "Backend"))
            {
                GetSettingsInterfaceStorage()->SetStringValue(
                    "SPU2/Output", "Backend", AudioStream::GetBackendName(Pcsx2Config::SPU2Options::DEFAULT_BACKEND));
                needs_save = true;
            }
            if (!GetSettingsInterfaceStorage()->ContainsValue("SPU2/Output", "SyncMode"))
            {
                GetSettingsInterfaceStorage()->SetStringValue(
                    "SPU2/Output", "SyncMode", Pcsx2Config::SPU2Options::GetSyncModeName(Pcsx2Config::SPU2Options::DEFAULT_SYNC_MODE));
                needs_save = true;
            }
            if (!GetSettingsInterfaceStorage()->ContainsValue("SPU2/Output", "BufferMS"))
            {
                GetSettingsInterfaceStorage()->SetUIntValue(
                    "SPU2/Output", "BufferMS", AudioStreamParameters::DEFAULT_BUFFER_MS);
                needs_save = true;
            }
            if (!GetSettingsInterfaceStorage()->ContainsValue("SPU2/Output", "OutputLatencyMS"))
            {
                GetSettingsInterfaceStorage()->SetUIntValue(
                    "SPU2/Output", "OutputLatencyMS", AudioStreamParameters::DEFAULT_OUTPUT_LATENCY_MS);
                needs_save = true;
            }
            if (!GetSettingsInterfaceStorage()->ContainsValue("SPU2/Output", "OutputLatencyMinimal"))
            {
                GetSettingsInterfaceStorage()->SetBoolValue(
                    "SPU2/Output", "OutputLatencyMinimal", AudioStreamParameters::DEFAULT_OUTPUT_LATENCY_MINIMAL);
                needs_save = true;
            }
            if (!GetSettingsInterfaceStorage()->ContainsValue("UI", "PreferEnglishGameTitles"))
            {
                GetSettingsInterfaceStorage()->SetBoolValue("UI", "PreferEnglishGameTitles", false);
                needs_save = true;
            }
            needs_save |= ApplyAndroidInputBootstrapDefaults(*GetSettingsInterfaceStorage(), true);
            needs_save |= ApplyAndroidGsBootstrapDefaults(*GetSettingsInterfaceStorage(), true);
            needs_save |= SyncManualGsHackToggle(*GetSettingsInterfaceStorage());
            needs_save |= NormalizeMandatoryFixSettings(*GetSettingsInterfaceStorage());
            if (needs_save)
                GetSettingsInterfaceStorage()->Save();
        }
    }
    VMManager::Internal::LoadStartupSettings();
    if (GetSettingsInterfaceStorage())
        GetSettingsInterfaceStorage()->SetBoolValue("EmuCore/GS", "OsdShowFPS", false);
    VMManager::ApplySettings();
    GSConfig.OsdPerformancePos = EmuConfig.GS.OsdPerformancePos;
    if (MTGS::IsOpen()) MTGS::ApplySettings();
    VMManager::ReloadInputSources();
    VMManager::ReloadInputBindings(true);
    NotifyRetroAchievementsState();

    if (EnsureNativeAppMethods(env))
    {
        Log::SetHostOutputLevel(LOGLEVEL_INFO, NativeLogCallback);
    }
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_getCoreVersion(JNIEnv* env, jclass clazz)
{
    (void)clazz;
    const std::string version = GetCoreVersionString();
    return env->NewStringUTF(version.c_str());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_setPerformanceOverlayMode(JNIEnv*, jclass, jboolean visible, jboolean detailed)
{
    AndroidPerformanceOverlayBridgeState& overlay_state = GetAndroidPerformanceOverlayBridgeState();
    overlay_state.visible = (visible == JNI_TRUE);
    overlay_state.detailed = (detailed == JNI_TRUE);
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
    ConfigureAndroidImGuiFonts();

    Log::SetConsoleOutputLevel(LOGLEVEL_DEBUG);
    if (GetSettingsInterfaceStorage())
        GetSettingsInterfaceStorage()->Save();
    GetSettingsInterfaceStorage().reset();

    const std::string ini_path = EmuFolders::DataRoot + "/PCSX2-Android.ini";
    GetSettingsInterfaceStorage() = std::make_unique<INISettingsInterface>(ini_path);
    Host::Internal::SetBaseSettingsLayer(GetSettingsInterfaceStorage().get());
    GetSettingsInterfaceStorage()->Load();
    if (GetSettingsInterfaceStorage()->IsEmpty())
    {
            VMManager::SetDefaultSettings(*GetSettingsInterfaceStorage(), true, true, true, true, true);
        GetSettingsInterfaceStorage()->SetBoolValue("EmuCore", "EnableDiscordPresence", true);
        GetSettingsInterfaceStorage()->SetIntValue("EmuCore/GS", "VsyncEnable", false);
        GetSettingsInterfaceStorage()->SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(GSRendererType::OGL));
        ApplyAndroidInputBootstrapDefaults(*GetSettingsInterfaceStorage(), false);
        GetSettingsInterfaceStorage()->SetBoolValue("Logging", "EnableSystemConsole", true);
        GetSettingsInterfaceStorage()->SetBoolValue("Logging", "EnableTimestamps", true);
        GetSettingsInterfaceStorage()->SetBoolValue("Logging", "EnableVerbose", false);
        GetSettingsInterfaceStorage()->SetBoolValue("EmuCore/GS", "OsdShowFPS", false);
        GetSettingsInterfaceStorage()->SetBoolValue("EmuCore/GS", "OsdShowResolution", false);
        GetSettingsInterfaceStorage()->SetBoolValue("EmuCore/GS", "OsdShowGSStats", false);
        GetSettingsInterfaceStorage()->SetBoolValue("EmuCore/GS", "OsdShowIndicators", false);
        GetSettingsInterfaceStorage()->SetIntValue("EmuCore/GS", "OsdPerformancePos", 0);
        ApplyAndroidGsBootstrapDefaults(*GetSettingsInterfaceStorage(), false);
        GetSettingsInterfaceStorage()->SetBoolValue("UI", "EnableFullscreenUI", false);
        GetSettingsInterfaceStorage()->SetBoolValue("UI", "ExpandIntoDisplayCutout", true);
        GetSettingsInterfaceStorage()->SetBoolValue("Achievements", "Enabled", false);
        GetSettingsInterfaceStorage()->SetBoolValue("Achievements", "ChallengeMode", false);
        GetSettingsInterfaceStorage()->SetBoolValue("Achievements", "AndroidMigrationV1", true);
        NormalizeMandatoryFixSettings(*GetSettingsInterfaceStorage());
        GetSettingsInterfaceStorage()->Save();
    }
    else
    {
        bool needs_save = false;
        if (!GetSettingsInterfaceStorage()->GetBoolValue("Achievements", "AndroidMigrationV1", false))
        {
            if (!GetSettingsInterfaceStorage()->ContainsValue("Achievements", "Enabled"))
                GetSettingsInterfaceStorage()->SetBoolValue("Achievements", "Enabled", false);
            GetSettingsInterfaceStorage()->SetBoolValue("Achievements", "AndroidMigrationV1", true);
            needs_save = true;
        }
        if (!GetSettingsInterfaceStorage()->ContainsValue("Achievements", "ChallengeMode"))
        {
            GetSettingsInterfaceStorage()->SetBoolValue("Achievements", "ChallengeMode", false);
            needs_save = true;
        }
        if (!GetSettingsInterfaceStorage()->ContainsValue("UI", "ExpandIntoDisplayCutout"))
        {
            GetSettingsInterfaceStorage()->SetBoolValue("UI", "ExpandIntoDisplayCutout", true);
            needs_save = true;
        }
        if (!GetSettingsInterfaceStorage()->ContainsValue("SPU2/Output", "Backend"))
        {
            GetSettingsInterfaceStorage()->SetStringValue(
                "SPU2/Output", "Backend", AudioStream::GetBackendName(Pcsx2Config::SPU2Options::DEFAULT_BACKEND));
            needs_save = true;
        }
        if (!GetSettingsInterfaceStorage()->ContainsValue("SPU2/Output", "SyncMode"))
        {
            GetSettingsInterfaceStorage()->SetStringValue(
                "SPU2/Output", "SyncMode", Pcsx2Config::SPU2Options::GetSyncModeName(Pcsx2Config::SPU2Options::DEFAULT_SYNC_MODE));
            needs_save = true;
        }
        if (!GetSettingsInterfaceStorage()->ContainsValue("SPU2/Output", "BufferMS"))
        {
            GetSettingsInterfaceStorage()->SetUIntValue(
                "SPU2/Output", "BufferMS", AudioStreamParameters::DEFAULT_BUFFER_MS);
            needs_save = true;
        }
        if (!GetSettingsInterfaceStorage()->ContainsValue("SPU2/Output", "OutputLatencyMS"))
        {
            GetSettingsInterfaceStorage()->SetUIntValue(
                "SPU2/Output", "OutputLatencyMS", AudioStreamParameters::DEFAULT_OUTPUT_LATENCY_MS);
            needs_save = true;
        }
        if (!GetSettingsInterfaceStorage()->ContainsValue("SPU2/Output", "OutputLatencyMinimal"))
        {
            GetSettingsInterfaceStorage()->SetBoolValue(
                "SPU2/Output", "OutputLatencyMinimal", AudioStreamParameters::DEFAULT_OUTPUT_LATENCY_MINIMAL);
            needs_save = true;
        }
        if (!GetSettingsInterfaceStorage()->ContainsValue("UI", "PreferEnglishGameTitles"))
        {
            GetSettingsInterfaceStorage()->SetBoolValue("UI", "PreferEnglishGameTitles", false);
            needs_save = true;
        }
        needs_save |= ApplyAndroidInputBootstrapDefaults(*GetSettingsInterfaceStorage(), true);
        needs_save |= ApplyAndroidGsBootstrapDefaults(*GetSettingsInterfaceStorage(), true);
        needs_save |= SyncManualGsHackToggle(*GetSettingsInterfaceStorage());
        needs_save |= NormalizeMandatoryFixSettings(*GetSettingsInterfaceStorage());
        if (needs_save)
            GetSettingsInterfaceStorage()->Save();
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

static void ResetPadState(u32 pad_index)
{
    PadBase* pad = Pad::GetPad(pad_index);
    if (!pad)
        return;

    for (u32 i = 0; i < static_cast<u32>(pad->GetInfo().bindings.size()); i++)
        pad->Set(i, 0.0f);
}

static constexpr u32 NUM_HOST_POINTER_BUTTONS = 3;

static u32 AndroidMouseButtonMaskToPointerIndex(const jint button)
{
    switch (button)
    {
        case 1:
            return 0;
        case 2:
            return 1;
        case 4:
            return 2;
        default:
            return NUM_HOST_POINTER_BUTTONS;
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_setPadButton(JNIEnv *env, jclass clazz,
                                                  jint p_padIndex, jint p_index, jint p_range, jboolean p_pressed) {
    if (VMManager::HasValidVM()) {
        const u32 pad_index = std::min<u32>(static_cast<u32>(std::max(p_padIndex, 0)), 1u);
        const GenericInputBinding generic_key = AndroidKeyToGeneric(p_index);
        float value = (p_range > 0) ? (static_cast<float>(p_range) / 255.0f) : (p_pressed ? 1.0f : 0.0f);
        Host::RunOnCPUThread([pad_index, generic_key, value]() {
            SetPadButtonState(pad_index, generic_key, value);
        });
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
            std::vector<std::pair<GenericInputBinding, float>> updates;
            updates.reserve(static_cast<size_t>(len));
            for (jsize i = 0; i < len; i++) {
                const GenericInputBinding generic_key = AndroidKeyToGeneric(indices[i]);
                if (generic_key != GenericInputBinding::Unknown)
                    updates.emplace_back(generic_key, values[i]);
            }
            Host::RunOnCPUThread([updates = std::move(updates)]() {
                for (const auto& [generic_key, value] : updates)
                    SetPadButtonState(0, generic_key, value);
            });
        }

        if (indices) env->ReleaseIntArrayElements(p_indices, indices, JNI_ABORT);
        if (values) env->ReleaseFloatArrayElements(p_values, values, JNI_ABORT);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_resetKeyStatus(JNIEnv *env, jclass clazz) {
    if (VMManager::HasValidVM()) {
        Host::RunOnCPUThread([]() {
            ResetPadState(0);
            ResetPadState(1);
            InputManager::PauseVibration();
        });
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_resetPadState(JNIEnv *env, jclass clazz, jint p_padIndex) {
    if (VMManager::HasValidVM()) {
        const u32 pad_index = std::min<u32>(static_cast<u32>(std::max(p_padIndex, 0)), 1u);
        Host::RunOnCPUThread([pad_index]() {
            ResetPadState(pad_index);
        });
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_onHostKeyEvent(JNIEnv* env, jclass clazz, jint key_code, jboolean pressed)
{
    if (!VMManager::HasValidVM())
        return;

    const u32 host_key_code = static_cast<u32>(key_code);
    const float value = (pressed == JNI_TRUE) ? 1.0f : 0.0f;
    Host::RunOnCPUThread([host_key_code, value]() {
        InputManager::InvokeEvents(InputManager::MakeHostKeyboardKey(host_key_code), value);
    });
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_onHostMousePosition(JNIEnv* env, jclass clazz, jfloat x, jfloat y)
{
    if (!VMManager::HasValidVM())
        return;

    InputManager::UpdatePointerAbsolutePosition(0, x, y);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_onHostMouseButton(JNIEnv* env, jclass clazz, jint button, jboolean pressed)
{
    if (!VMManager::HasValidVM())
        return;

    const u32 pointer_index = AndroidMouseButtonMaskToPointerIndex(button);
    if (pointer_index >= NUM_HOST_POINTER_BUTTONS)
        return;

    const float value = (pressed == JNI_TRUE) ? 1.0f : 0.0f;
    Host::RunOnCPUThread([pointer_index, value]() {
        InputManager::InvokeEvents(InputManager::MakePointerButtonKey(0, pointer_index), value);
    });
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_onHostMouseWheel(JNIEnv* env, jclass clazz, jfloat delta_x, jfloat delta_y)
{
    if (!VMManager::HasValidVM())
        return;

    if (delta_x != 0.0f)
        InputManager::UpdatePointerRelativeDelta(0, InputPointerAxis::WheelX, delta_x);
    if (delta_y != 0.0f)
        InputManager::UpdatePointerRelativeDelta(0, InputPointerAxis::WheelY, delta_y);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_setPadVibration(JNIEnv *env, jclass clazz,
                                                     jboolean p_enabled) {
    if (GetSettingsInterfaceStorage()) {
        GetSettingsInterfaceStorage()->SetBoolValue("InputSources", "PadVibration", p_enabled == JNI_TRUE);
        GetSettingsInterfaceStorage()->Save();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_setAspectRatio(JNIEnv *env, jclass clazz,
                                                    jint p_type) {
    RunGSRuntimeChange([p_type]() {
        EmuConfig.GS.AspectRatio = static_cast<AspectRatioType>(p_type);
        GSConfig.AspectRatio = static_cast<AspectRatioType>(p_type);

        // Crucial for Android: we need to update the runtime state as well
        // and reset any custom ratio set by widescreen patches
        EmuConfig.CurrentAspectRatio = static_cast<AspectRatioType>(p_type);
        EmuConfig.CurrentCustomAspectRatio = 0.0f;
    });
    if (GetSettingsInterfaceStorage())
    {
        const auto aspect_ratio_index = static_cast<size_t>(p_type);
        const char* aspect_ratio_name =
            (aspect_ratio_index < static_cast<size_t>(AspectRatioType::MaxCount))
                ? Pcsx2Config::GSOptions::AspectRatioNames[aspect_ratio_index]
                : Pcsx2Config::GSOptions::AspectRatioNames[static_cast<size_t>(AspectRatioType::RAuto4_3_3_2)];
        GetSettingsInterfaceStorage()->SetStringValue("EmuCore/GS", "AspectRatio", aspect_ratio_name);
        GetSettingsInterfaceStorage()->Save();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_renderUpscalemultiplier(JNIEnv *env, jclass clazz,
                                                              jfloat p_value) {
    const float normalized_value = std::clamp(std::round(p_value), 1.0f, 6.0f);
    RunGSRuntimeChange([normalized_value]() {
        EmuConfig.GS.UpscaleMultiplier = normalized_value;
        GSConfig.UpscaleMultiplier = normalized_value;
    });
    if (GetSettingsInterfaceStorage())
    {
        GetSettingsInterfaceStorage()->SetFloatValue("EmuCore/GS", "upscale_multiplier", normalized_value);
        GetSettingsInterfaceStorage()->Save();
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
Java_com_sbro_emucorex_core_NativeApp_getGameMetadata(JNIEnv *env, jclass clazz,
                                                      jstring p_szpath) {
    std::string path = GetJavaString(env, p_szpath);
    if (path.empty())
        return env->NewStringUTF("");

    std::string serial;
    u32 crc = 0;
    GameList::Entry temp_entry;

    Error cdvd_lock_error;
    const bool has_cdvd_lock = cdvdLock(&cdvd_lock_error);

    bool have_entry = false;
    {
        auto game_list_lock = GameList::GetLock();
        if (const GameList::Entry* entry = GameList::GetEntryForPath(path.c_str()); entry != nullptr)
        {
            temp_entry = *entry;
            have_entry = true;
        }
    }

    if (!have_entry && has_cdvd_lock && GameList::PopulateEntryFromPath(path, &temp_entry))
        have_entry = true;

    if (have_entry)
    {
        serial = temp_entry.serial;
        crc = temp_entry.crc;
    }
    else if (has_cdvd_lock)
    {
        GameList::GetSerialAndCRCForFilename(path.c_str(), &serial, &crc);
    }
    // If the disc subsystem is busy during active emulation, quietly fall back to
    // cached/native filename-derived metadata instead of spamming warnings.

    std::string title;
    if (!serial.empty())
    {
        if (const GameDatabaseSchema::GameEntry* db_entry = GameDatabase::findGame(serial))
            title = !db_entry->name_en.empty() ? db_entry->name_en : db_entry->name;
    }

    if (title.empty() && have_entry)
        title = temp_entry.title;

    std::string ret;
    ret.append(title);
    ret.append("|");
    ret.append(serial);
    ret.append("|");
    if (!serial.empty())
        ret.append(StringUtil::StdStringFromFormat("%s (%08X)", serial.c_str(), crc));

    if (has_cdvd_lock)
        cdvdUnlock();

    return env->NewStringUTF(ret.c_str());
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_getGameTitle(JNIEnv *env, jclass clazz,
                                                  jstring p_szpath) {
    return Java_com_sbro_emucorex_core_NativeApp_getGameMetadata(env, clazz, p_szpath);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_getGameSerial(JNIEnv *env, jclass clazz) {
    std::string ret = RunOnCPUThreadBlocking<std::string>(std::string(), []() {
        return VMManager::GetDiscSerial();
    });
    return env->NewStringUTF(ret.c_str());
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_hasWidescreenPatch(JNIEnv* env, jclass clazz)
{
	(void)env;
	(void)clazz;
    const bool has_patch = RunOnCPUThreadBlocking<bool>(false, []() {
        const std::string serial = VMManager::GetDiscSerial();
        if (serial.empty())
            return false;

        const u32 crc = VMManager::GetDiscCRC();
        const std::vector<Patch::PatchInfo> patches = Patch::GetPatchInfo(serial, crc, false, false, nullptr);
        for (const Patch::PatchInfo& info : patches)
        {
            if (info.name == "Widescreen 16:9")
                return true;
        }
        return false;
    });
    return has_patch ? JNI_TRUE : JNI_FALSE;
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

    RunGSRuntimeChange([level]() {
        EmuConfig.GS.TexturePreloading = level;
        GSConfig.TexturePreloading = level;
    });

    if (GetSettingsInterfaceStorage())
    {
        GetSettingsInterfaceStorage()->SetIntValue("EmuCore/GS", "texture_preloading", clamped);
        GetSettingsInterfaceStorage()->Save();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_renderGpu(JNIEnv *env, jclass clazz,
                                               jint p_value) {
    RunGSRuntimeChange([p_value]() {
        EmuConfig.GS.Renderer = static_cast<GSRendererType>(p_value);
        GSConfig.Renderer = static_cast<GSRendererType>(p_value);
    });
    if (GetSettingsInterfaceStorage())
    {
        GetSettingsInterfaceStorage()->SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(p_value));
        GetSettingsInterfaceStorage()->Save();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_setCustomDriverPath(JNIEnv *env, jclass clazz,
                                                          jstring p_path) {
    std::string driver_path = GetJavaString(env, p_path);

    const std::string old_emu_config_path = EmuConfig.GS.CustomDriverPath;
    const std::string old_gs_config_path = GSConfig.CustomDriverPath;

    RunGSRuntimeChange([driver_path]() {
        EmuConfig.GS.CustomDriverPath = driver_path;
        GSConfig.CustomDriverPath = driver_path;
    }, false);

    if (GetSettingsInterfaceStorage())
    {
        if (driver_path.empty())
            GetSettingsInterfaceStorage()->DeleteValue("EmuCore/GS", "CustomDriverPath");
        else
            GetSettingsInterfaceStorage()->SetStringValue("EmuCore/GS", "CustomDriverPath", driver_path.c_str());
        GetSettingsInterfaceStorage()->Save();
    }

    if (old_emu_config_path != driver_path || old_gs_config_path != driver_path)
    {
        if (VMManager::HasValidVM())
        {
            Host::RunOnCPUThread([]() {
                if (MTGS::IsOpen())
                    MTGS::ApplySettings();
            }, true);
        }
        else if (MTGS::IsOpen())
        {
            MTGS::ApplySettings();
        }
    }
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_getCustomDriverPath(JNIEnv *env, jclass clazz) {
    std::string driver_path;
    if (GetSettingsInterfaceStorage())
    {
        GetSettingsInterfaceStorage()->GetStringValue("EmuCore/GS", "CustomDriverPath", &driver_path);
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
        // Preserve the app native library directory for direct Vulkan library lookup.
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
    std::string value = GetJavaString(env, j_value);

    if (!GetSettingsInterfaceStorage())
        return; 
    INISettingsInterface& si = *GetSettingsInterfaceStorage();

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

    GetSettingsBatchDirty() = true;
    GetSettingsBatchFoldersDirty() |= (section == "Folders");

    if (GetSettingsBatchDepth() == 0)
        FlushPendingSettingsChanges();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_beginSettingsBatch(JNIEnv*, jclass)
{
    GetSettingsBatchDepth()++;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_endSettingsBatch(JNIEnv*, jclass)
{
    if (GetSettingsBatchDepth() == 0)
        return;

    GetSettingsBatchDepth()--;
    if (GetSettingsBatchDepth() == 0)
        FlushPendingSettingsChanges();
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
        if (GetSettingsInterfaceStorage())
            GetSettingsInterfaceStorage()->GetBoolValue(section.c_str(), key.c_str(), &v);
        return env->NewStringUTF(v ? "true" : "false");
    }
    else if (type == "int")
    {
        s32 v = 0;
        if (GetSettingsInterfaceStorage())
            GetSettingsInterfaceStorage()->GetIntValue(section.c_str(), key.c_str(), &v);
        return env->NewStringUTF(StringUtil::StdStringFromFormat("%d", v).c_str());
    }
    else if (type == "uint")
    {
        u32 v = 0;
        if (GetSettingsInterfaceStorage())
            GetSettingsInterfaceStorage()->GetUIntValue(section.c_str(), key.c_str(), &v);
        return env->NewStringUTF(StringUtil::StdStringFromFormat("%u", v).c_str());
    }
    else if (type == "float")
    {
        float v = 0.0f;
        if (GetSettingsInterfaceStorage())
            GetSettingsInterfaceStorage()->GetFloatValue(section.c_str(), key.c_str(), &v);
        return env->NewStringUTF(StringUtil::StdStringFromFormat("%g", v).c_str());
    }
    else if (type == "double")
    {
        double v = 0.0;
        if (GetSettingsInterfaceStorage())
            GetSettingsInterfaceStorage()->GetDoubleValue(section.c_str(), key.c_str(), &v);
        return env->NewStringUTF(StringUtil::StdStringFromFormat("%g", v).c_str());
    }
    else 
    {
        std::string v;
        if (GetSettingsInterfaceStorage())
            GetSettingsInterfaceStorage()->GetStringValue(section.c_str(), key.c_str(), &v);
        return env->NewStringUTF(v.c_str());
    }
}



extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_onNativeSurfaceCreated(JNIEnv *env, jclass clazz) {
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_onNativeSurfaceChanged(JNIEnv *env, jclass clazz,
                                                            jobject p_surface, jint p_width, jint p_height) {
    AndroidSurfaceState& surface_state = GetAndroidSurfaceState();
    ANativeWindow* new_window = nullptr;
    jobject new_surface = nullptr;
    if (p_surface != nullptr)
    {
        new_window = ANativeWindow_fromSurface(env, p_surface);
        new_surface = env->NewGlobalRef(p_surface);
    }

    {
        std::lock_guard<std::mutex> lock(surface_state.mutex);
        if(surface_state.window) {
            ANativeWindow_release(surface_state.window);
            surface_state.window = nullptr;
        }
        if (surface_state.surface_object)
        {
            env->DeleteGlobalRef(surface_state.surface_object);
            surface_state.surface_object = nullptr;
        }

        surface_state.window = new_window;
        surface_state.surface_object = new_surface;
        if (p_width > 0)
            surface_state.window_width = p_width;
        if (p_height > 0)
            surface_state.window_height = p_height;
    }

    if(p_width > 0 && p_height > 0) {
        if(MTGS::IsOpen()) {
            Host::RunOnGSThread([]() {
                MTGS::UpdateDisplayWindow();
            });
        }
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_onNativeSurfaceDestroyed(JNIEnv *env, jclass clazz) {
    AndroidSurfaceState& surface_state = GetAndroidSurfaceState();
    {
        std::lock_guard<std::mutex> lock(surface_state.mutex);
        if(surface_state.window) {
            ANativeWindow_release(surface_state.window);
            surface_state.window = nullptr;
        }
        if (surface_state.surface_object)
        {
            env->DeleteGlobalRef(surface_state.surface_object);
            surface_state.surface_object = nullptr;
        }
        surface_state.window_width = 0;
        surface_state.window_height = 0;
    }
}

std::optional<WindowInfo> Host::AcquireRenderWindow(bool recreate_window)
{
    AndroidSurfaceState& surface_state = GetAndroidSurfaceState();
    static void* s_last_logged_window = nullptr;
    static u32 s_acquire_log_count = 0;
    ANativeWindow* window = nullptr;
    int window_width = 0;
    int window_height = 0;

    {
        std::lock_guard<std::mutex> lock(surface_state.mutex);
        if (recreate_window && surface_state.surface_object)
        {
            JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            if (env)
            {
                ANativeWindow* recreated_window = ANativeWindow_fromSurface(env, surface_state.surface_object);
                if (recreated_window)
                {
                    if (surface_state.window && surface_state.window != recreated_window)
                        ANativeWindow_release(surface_state.window);
                    surface_state.window = recreated_window;
                }
                else
                {
                    Console.Warning("AcquireRenderWindow: failed to recreate ANativeWindow from stored Surface");
                }
            }
            else
            {
                Console.Warning("AcquireRenderWindow: SDL_GetAndroidJNIEnv() returned null during recreate");
            }
        }

        window = surface_state.window;
        window_width = surface_state.window_width;
        window_height = surface_state.window_height;
    }

    float _fScale = 1.0f;
    if (window_width > 0 && window_height > 0) {
        int _nSize = window_width;
        if (window_width <= window_height) {
            _nSize = window_height;
        }
        _fScale = (float)_nSize / 800.0f;
    }
    ////
    WindowInfo _windowInfo;
    memset(&_windowInfo, 0, sizeof(_windowInfo));
    _windowInfo.type = (window != nullptr) ?
        WindowInfo::Type::Android :
        WindowInfo::Type::Surfaceless;
    _windowInfo.surface_width = static_cast<u32>(std::max(window_width, 0));
    _windowInfo.surface_height = static_cast<u32>(std::max(window_height, 0));
    _windowInfo.surface_scale = _fScale;
    _windowInfo.window_handle = window;

    if ((window != s_last_logged_window || s_acquire_log_count < 8) && s_acquire_log_count < 16)
    {
        Console.WriteLn("AcquireRenderWindow: recreate=%d type=%s handle=%p size=%dx%d scale=%.3f",
            recreate_window ? 1 : 0, _windowInfo.type == WindowInfo::Type::Android ? "Android" : "Surfaceless",
            window, window_width, window_height, _fScale);
        s_last_logged_window = window;
        s_acquire_log_count++;
    }

    return _windowInfo;
}

void Host::ReleaseRenderWindow() {
}

// Owned by the GS thread.
static double s_last_internal_draws = 0;
static double s_last_draws = 0;
static u64 s_total_internal_draws = 0;
static u64 s_total_draws = 0;
static u32 s_total_frames = 0;

void Host::BeginPresentFrame() {
    NativeAppBridgeCache& native_app_bridge = GetNativeAppBridgeCache();
    const AndroidPerformanceOverlayBridgeState& overlay_state = GetAndroidPerformanceOverlayBridgeState();
    if (GSIsHardwareRenderer())
    {
        static constexpr auto update_stat = [](GSPerfMon::counter_t counter, u64& dst, double& last) {
            // perfmon resets every 30 frames to zero
            const double val = g_perfmon.GetCounter(counter);
            dst += static_cast<u64>((val < last) ? val : (val - last));
            last = val;
        };

        update_stat(GSPerfMon::Draw, s_total_internal_draws, s_last_internal_draws);
        update_stat(GSPerfMon::DrawCalls, s_total_draws, s_last_draws);
        
        s_total_frames++;
        std::atomic_thread_fence(std::memory_order_release);
    }

    // Push metrics to Java every 500ms for both HW and SW renderers.
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - native_app_bridge.last_fps_sample_time).count();
    if (elapsed >= 500) {
        native_app_bridge.last_fps_sample_time = now;

        if (!overlay_state.visible)
            return;

        auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (env && native_app_bridge.app_class && native_app_bridge.on_performance_metrics) {
            float fps = PerformanceMetrics::GetFPS();
            if (fps <= 0.0f) fps = PerformanceMetrics::GetInternalFPS();
            if (fps <= 0.0f && VMManager::HasValidVM())
                fps = VMManager::GetFrameRate();
            const float speed_percent = PerformanceMetrics::GetSpeed() * 100.0f;

            const std::string overlay_text_utf8 =
                overlay_state.detailed ? BuildAndroidPerformanceOverlayText() : std::string();
            jstring overlay_text = env->NewStringUTF(overlay_text_utf8.c_str());

            env->CallStaticVoidMethod(
                native_app_bridge.app_class,
                native_app_bridge.on_performance_metrics,
                overlay_text,
                fps,
                speed_percent
            );
            env->DeleteLocalRef(overlay_text);
        }
    }
}

void Host::OnGameChanged(const std::string& title, const std::string& elf_override, const std::string& disc_path,
                         const std::string& disc_serial, u32 disc_crc, u32 current_crc) {
}

void Host::PumpMessagesOnCPUThread() {
    DrainCPUThreadQueue();
}

int FileSystem::OpenFDFileContent(const char* filename)
{
    NativeAppBridgeCache& native_app_bridge = GetNativeAppBridgeCache();
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    if(!env || !EnsureNativeAppMethods(env) || !native_app_bridge.open_content_uri) {
        return -1;
    }

    jstring j_filename = env->NewStringUTF(filename);
    int fd = env->CallStaticIntMethod(native_app_bridge.app_class, native_app_bridge.open_content_uri, j_filename);
    env->DeleteLocalRef(j_filename);
    return fd;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeConfigure(JNIEnv* env, jclass, jlong application_id,
                                                         jstring scheme, jstring display_name, jstring image_key)
{
    (void)env;
    (void)application_id;
    (void)scheme;
    (void)display_name;
    (void)image_key;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeProvideStoredToken(JNIEnv* env, jclass,
                                                                  jstring access_token, jstring refresh_token,
                                                                  jstring token_type, jlong expires_at,
                                                                  jstring scope)
{
    (void)env;
    (void)access_token;
    (void)refresh_token;
    (void)token_type;
    (void)expires_at;
    (void)scope;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeBeginAuthorize(JNIEnv*, jclass)
{
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeSetAppForeground(JNIEnv*, jclass, jboolean is_foreground)
{
    (void)is_foreground;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativePollCallbacks(JNIEnv*, jclass)
{
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeClearTokens(JNIEnv*, jclass)
{
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeIsLoggedIn(JNIEnv*, jclass)
{
    return JNI_FALSE;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeIsClientReady(JNIEnv*, jclass)
{
    return JNI_FALSE;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_utils_DiscordBridge_nativeConsumeLastError(JNIEnv* env, jclass)
{
    (void)env;
    return nullptr;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_runVMThread(JNIEnv *env, jclass clazz,
                                                 jstring p_szpath) {
    CpuThreadBridgeState& cpu_thread_state = GetCpuThreadBridgeState();
    std::string _szPath = GetJavaString(env, p_szpath);

    /////////////////////////////

    {
    }

    cpu_thread_state.execute_exit = false;
    SetCPUThreadActive(true);

//    const char* error;
//    if (!VMManager::PerformEarlyHardwareChecks(&error)) {
//        return false;
//    }

    // fast_boot : (false: bios->game, true: direct-to-game)
    VMBootParameters boot_params;
    boot_params.filename = _szPath;

    if (!VMManager::Internal::CPUThreadInitialize()) {
        VMManager::Internal::CPUThreadShutdown();
        SetCPUThreadActive(false);
        return JNI_FALSE;
    }

    VMManager::ApplySettings();
    GSDumpReplayer::SetIsDumpRunner(false);

    Error error;
    const VMBootResult boot_result = VMManager::Initialize(boot_params, &error);
    if (boot_result == VMBootResult::StartupSuccess)
    {
        ResetPadState(0);
        ResetPadState(1);
        InputManager::PauseVibration();

        Console.WriteLn("runVMThread: VMManager::Initialize() returned StartupSuccess.");
        VMState _vmState = VMState::Running;
        Console.WriteLn("runVMThread: calling VMManager::SetState(Running).");
        VMManager::SetState(_vmState);
        Console.WriteLn("runVMThread: VMManager::SetState(Running) returned.");
        ////
        bool logged_first_execute = false;
        while (true) {
            Host::PumpMessagesOnCPUThread();
            _vmState = VMManager::GetState();
            if (_vmState == VMState::Stopping || _vmState == VMState::Shutdown) {
                Console.WriteLn("runVMThread: leaving VM loop because state is %s.",
                    (_vmState == VMState::Stopping) ? "Stopping" : "Shutdown");
                break;
            } else if (_vmState == VMState::Running) {
                if (!logged_first_execute) {
                    logged_first_execute = true;
                    Console.WriteLn("runVMThread: first VMManager::Execute() call.");
                }
                cpu_thread_state.execute_exit = false;
                VMManager::Execute();
                cpu_thread_state.execute_exit = true;
            } else {
                usleep(250000);
            }
        }
        ////
        Console.WriteLn("runVMThread: calling VMManager::Shutdown(false).");
        VMManager::Shutdown(false);
    }
    else if (error.IsValid())
    {
        Host::ReportErrorAsync("VM Boot Failed", error.GetDescription());
    }
    ////
    VMManager::Internal::CPUThreadShutdown();
    SetCPUThreadActive(false);

    return (boot_result == VMBootResult::StartupSuccess) ? JNI_TRUE : JNI_FALSE;
}

static std::atomic<u32> s_vu1_trace_resume_duration_ms{0};

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_pause(JNIEnv *env, jclass clazz) {
    Host::RunOnCPUThread([] {
        VMManager::SetPaused(true);
    });
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_resume(JNIEnv *env, jclass clazz) {
    Host::RunOnCPUThread([] {
        VMManager::SetPaused(false);
        const u32 pending_duration = s_vu1_trace_resume_duration_ms.exchange(0, std::memory_order_acq_rel);
        if (pending_duration > 0 && VMManager::HasValidVM())
            VU1Trace::BeginCapture(pending_duration);
    });
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_shutdown(JNIEnv *env, jclass clazz) {
    Host::RunOnCPUThread([] {
        VMManager::SetState(VMState::Stopping);
    });
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_NativeApp_refreshBIOS(JNIEnv* env, jclass clazz)
{
    if (!GetSettingsInterfaceStorage())
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
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_captureVu1Trace(JNIEnv* env, jclass, jint duration_ms)
{
    if (!VMManager::HasValidVM())
        return nullptr;

    const std::string path = RunOnCPUThreadBlocking<std::string>(std::string(), [duration_ms]() {
        if (!VMManager::HasValidVM())
            return std::string();

        return VU1Trace::BeginCapture(static_cast<u32>(std::max(duration_ms, 1)));
    });

    if (path.empty())
        return nullptr;

    return env->NewStringUTF(path.c_str());
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_armVu1TraceOnNextResume(JNIEnv*, jclass, jint duration_ms)
{
    if (!VMManager::HasValidVM())
        return JNI_FALSE;

    const u32 clamped_duration = static_cast<u32>(std::max(duration_ms, 1));
    s_vu1_trace_resume_duration_ms.store(clamped_duration, std::memory_order_release);

    Host::AddOSDMessage(
        fmt::format("VU1 trace armed for next resume ({} ms)", clamped_duration),
        Host::OSD_INFO_DURATION);

    return JNI_TRUE;
}


extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_saveStateToSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    if (!VMManager::HasValidVM()) {
        return false;
    }

    bool result = false;
    Host::RunOnCPUThread([p_slot, &result]() {
        if (!VMManager::HasValidVM() || VMManager::GetDiscCRC() == 0)
            return;

        if (VMManager::GetState() != VMState::Paused)
            VMManager::SetPaused(true);

        if (VMManager::GetState() != VMState::Paused)
            return;

        VMManager::SaveStateToSlot(p_slot, false, [](const std::string& error) {
            if (!error.empty())
                Host::ReportErrorAsync("Save State Failed", error);
        });
        result = true;
    }, true);

    return result ? JNI_TRUE : JNI_FALSE;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_loadStateFromSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    if (!VMManager::HasValidVM()) {
        Console.Warning(fmt::format("(NativeApp::loadStateFromSlot) Ignoring load for slot {} because VM is not valid.", p_slot));
        return false;
    }

    bool result = false;
    Host::RunOnCPUThread([p_slot, &result]() {
        if (!VMManager::HasValidVM())
        {
            Console.Warning(fmt::format("(NativeApp::loadStateFromSlot) VM became invalid before loading slot {}.", p_slot));
            return;
        }

        const u32 crc = VMManager::GetDiscCRC();
        if (crc == 0)
        {
            Console.Warning(fmt::format("(NativeApp::loadStateFromSlot) Disc CRC is not ready yet for slot {}.", p_slot));
            return;
        }

        const std::string serial = VMManager::GetDiscSerial();
        if (serial.empty())
        {
            Console.Warning(fmt::format("(NativeApp::loadStateFromSlot) Disc serial is empty for slot {}.", p_slot));
            return;
        }

        if (!VMManager::HasSaveStateInSlot(serial.c_str(), crc, p_slot))
        {
            Console.Warning(fmt::format(
                "(NativeApp::loadStateFromSlot) Save state not found for serial {} crc {:08X} slot {}.",
                serial,
                crc,
                p_slot));
            return;
        }

        if (VMManager::GetState() != VMState::Paused)
            VMManager::SetPaused(true);

        if (VMManager::GetState() != VMState::Paused)
        {
            Console.Warning(fmt::format("(NativeApp::loadStateFromSlot) Failed to pause VM before loading slot {}.", p_slot));
            return;
        }

        result = VMManager::LoadStateFromSlot(p_slot);
    }, true);

    return result ? JNI_TRUE : JNI_FALSE;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_getGamePathSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    std::string _filename = RunOnCPUThreadBlocking<std::string>(std::string(), [p_slot]() {
        return VMManager::GetSaveStateFileName(VMManager::GetDiscSerial().c_str(), VMManager::GetDiscCRC(), p_slot);
    });
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
Java_com_sbro_emucorex_core_NativeApp_getSaveStateScreenshot(JNIEnv* env, jclass, jstring j_path)
{
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

    env->SetByteArrayRegion(retArr, 0, length, reinterpret_cast<const jbyte*>(optdata->data()));
    return retArr;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_listMemoryCards(JNIEnv* env, jclass)
{
    const std::vector<AvailableMcdInfo> cards = FileMcd_GetAvailableCards(true);

    std::string json = "[";
    bool first = true;
    for (const AvailableMcdInfo& card : cards)
    {
        if (!first)
            json += ",";
        first = false;
        json += fmt::format(
            "{{\"name\":\"{}\",\"path\":\"{}\",\"modifiedTime\":{},\"type\":{},\"fileType\":{},\"sizeBytes\":{},\"formatted\":{}}}",
            EscapeJsonString(card.name),
            EscapeJsonString(card.path),
            static_cast<long long>(card.modified_time),
            static_cast<int>(card.type),
            static_cast<int>(card.file_type),
            static_cast<long long>(card.size),
            card.formatted ? "true" : "false");
    }
    json += "]";
    return env->NewStringUTF(json.c_str());
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_createMemoryCard(JNIEnv* env, jclass, jstring j_name, jint j_type, jint j_fileType)
{
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

    std::string _filename = RunOnCPUThreadBlocking<std::string>(std::string(), [p_slot]() {
        return VMManager::GetSaveStateFileName(VMManager::GetDiscSerial().c_str(), VMManager::GetDiscCRC(), p_slot);
    });
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
Java_com_sbro_emucorex_core_NativeApp_getRetroAchievementGameData(JNIEnv* env, jclass, jstring j_path)
{
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

    Error error;
    if (!Achievements::Login(username.c_str(), password.c_str(), &error))
    {
        if (error.IsValid())
            return env->NewStringUTF(error.GetDescription().c_str());
        return env->NewStringUTF("Login failed.");
    }

    return nullptr;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeLogout(JNIEnv* env, jclass)
{
    (void)env;
    Host::RunOnCPUThread([]() {
        Achievements::Logout();
        NotifyRetroAchievementsState();
    }, true);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeSetEnabled(JNIEnv* env, jclass, jboolean enabled)
{
    (void)env;
    CpuThreadBridgeState& cpu_thread_state = GetCpuThreadBridgeState();
    const bool enable = (enabled == JNI_TRUE);
    const bool cpu_thread_active = [&cpu_thread_state]() {
        std::lock_guard<std::mutex> lock(cpu_thread_state.queue_mutex);
        return cpu_thread_state.active;
    }();
    if (!cpu_thread_active)
    {
        EmuConfig.Achievements.Enabled = enable;
        Host::SetBaseBoolSettingValue("Achievements", "Enabled", enable);
        Host::CommitBaseSettingChanges();
        NotifyRetroAchievementsState();
        return;
    }

    if (!VMManager::HasValidVM())
    {
        Host::SetBaseBoolSettingValue("Achievements", "Enabled", enable);
        Host::CommitBaseSettingChanges();
        NotifyRetroAchievementsState();
        return;
    }

    Host::RunOnCPUThread([enable]() {
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
    }, false);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeSetHardcore(JNIEnv* env, jclass, jboolean enabled)
{
    (void)env;
    CpuThreadBridgeState& cpu_thread_state = GetCpuThreadBridgeState();
    const bool enable = (enabled == JNI_TRUE);
    const bool cpu_thread_active = [&cpu_thread_state]() {
        std::lock_guard<std::mutex> lock(cpu_thread_state.queue_mutex);
        return cpu_thread_state.active;
    }();
    if (!cpu_thread_active)
    {
        EmuConfig.Achievements.HardcoreMode = enable;
        Host::SetBaseBoolSettingValue("Achievements", "ChallengeMode", enable);
        Host::CommitBaseSettingChanges();
        NotifyRetroAchievementsState();
        return;
    }

    if (!VMManager::HasValidVM())
    {
        Host::SetBaseBoolSettingValue("Achievements", "ChallengeMode", enable);
        Host::CommitBaseSettingChanges();
        NotifyRetroAchievementsState();
        return;
    }

    Host::RunOnCPUThread([enable]() {
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
    }, false);
}


void Host::CommitBaseSettingChanges()
{
    auto lock = GetSettingsLock();
    if (GetSettingsInterfaceStorage())
        GetSettingsInterfaceStorage()->Save();
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

void Host::OpenURL(const std::string_view url)
{
    // noop
}

int Host::LocaleSensitiveCompare(std::string_view lhs, std::string_view rhs)
{
    return lhs.compare(rhs);
}

void Host::SetMouseLock(bool state)
{
    Host::SetBaseBoolSettingValue("EmuCore", "EnableMouseLock", state);
    Host::CommitBaseSettingChanges();
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
    AndroidSurfaceState& surface_state = GetAndroidSurfaceState();
    int surface_width = 0;
    int surface_height = 0;
    {
        std::lock_guard<std::mutex> lock(surface_state.mutex);
        surface_width = surface_state.window_width;
        surface_height = surface_state.window_height;
    }

    if (surface_width <= 0 || surface_height <= 0 || !MTGS::IsOpen())
        return;

    const float surface_scale = GetAndroidSurfaceScale(surface_width, surface_height);
    MTGS::ResizeDisplayWindow(static_cast<u32>(surface_width), static_cast<u32>(surface_height), surface_scale);
}

std::vector<std::string> GetOpticalDriveList()
{
    return {};
}

void GetValidDrive(std::string& drive)
{
    drive.clear();
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

bool PageFaultHandler::InstallSecondaryThread()
{
    return true;
}

bool Common::InhibitScreensaver(bool inhibit)
{
    (void)inhibit;
    return true;
}

bool Common::PlaySoundAsync(const char* path)
{
    (void)path;
    return false;
}

BEGIN_HOTKEY_LIST(g_host_hotkeys)
END_HOTKEY_LIST()

void Host::RunOnCPUThread(std::function<void()> function, bool block /* = false */)
{
    if (!function)
        return;

    CpuThreadBridgeState& cpu_thread_state = GetCpuThreadBridgeState();

    if (IsOnCPUThread())
    {
        function();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(cpu_thread_state.queue_mutex);
        if (!cpu_thread_state.active)
        {
            function();
            return;
        }
    }

    if (!block)
    {
        QueueCPUThreadWork(std::move(function));
        return;
    }

    auto completion = std::make_shared<std::promise<void>>();
    std::future<void> completion_future = completion->get_future();
    QueueCPUThreadWork([fn = std::move(function), completion]() mutable {
        fn();
        completion->set_value();
    });
    completion_future.get();
}

void Host::RunOnGSThread(std::function<void()> function)
{
    if (!function)
        return;

    Host::RunOnCPUThread([fn = std::move(function)]() mutable {
        MTGS::RunOnGSThread(std::move(fn));
    });
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
    Host::RunOnCPUThread([]() {
        VMManager::SetState(VMState::Stopping);
    });
}

void Host::OnAchievementsLoginSuccess(const char* username, u32 points, u32 sc_points, u32 unread_messages)
{
    RetroAchievementsBridgeCache& ra_bridge = GetRetroAchievementsBridgeCache();
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!EnsureRetroAchievementsBridge(env))
        return;

    jstring j_username = username ? env->NewStringUTF(username) : nullptr;
    env->CallStaticVoidMethod(ra_bridge.bridge_class, ra_bridge.notify_login_success, j_username,
                              static_cast<jint>(points), static_cast<jint>(sc_points),
                              static_cast<jint>(unread_messages));
    ClearJNIExceptions(env);
    if (j_username)
        env->DeleteLocalRef(j_username);

    // The login callback payload can be partial. Push a full bridge state update
    // immediately afterwards so Compose reflects the real native RA session.
    NotifyRetroAchievementsState();
}

void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason reason)
{
    RetroAchievementsBridgeCache& ra_bridge = GetRetroAchievementsBridgeCache();
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!EnsureRetroAchievementsBridge(env))
        return;

    env->CallStaticVoidMethod(ra_bridge.bridge_class, ra_bridge.notify_login_requested,
                              static_cast<jint>(reason));
    ClearJNIExceptions(env);
}

void Host::OnAchievementsHardcoreModeChanged(bool enabled)
{
    RetroAchievementsBridgeCache& ra_bridge = GetRetroAchievementsBridgeCache();
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!EnsureRetroAchievementsBridge(env))
        return;

    env->CallStaticVoidMethod(ra_bridge.bridge_class, ra_bridge.notify_hardcore_changed,
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
    struct HostKeyMapping
    {
        std::string_view name;
        u32 code;
    };

    static constexpr HostKeyMapping mappings[] = {
        {"Left", AKEYCODE_DPAD_LEFT}, {"Right", AKEYCODE_DPAD_RIGHT}, {"Up", AKEYCODE_DPAD_UP}, {"Down", AKEYCODE_DPAD_DOWN},
        {"PageUp", AKEYCODE_PAGE_UP}, {"PageDown", AKEYCODE_PAGE_DOWN}, {"Home", AKEYCODE_MOVE_HOME}, {"End", AKEYCODE_MOVE_END},
        {"Insert", AKEYCODE_INSERT}, {"Delete", AKEYCODE_FORWARD_DEL}, {"Backspace", AKEYCODE_DEL}, {"Space", AKEYCODE_SPACE},
        {"Return", AKEYCODE_ENTER}, {"Escape", AKEYCODE_ESCAPE}, {"LeftCtrl", AKEYCODE_CTRL_LEFT}, {"Ctrl", AKEYCODE_CTRL_LEFT},
        {"RightCtrl", AKEYCODE_CTRL_RIGHT}, {"LeftShift", AKEYCODE_SHIFT_LEFT}, {"Shift", AKEYCODE_SHIFT_LEFT},
        {"RightShift", AKEYCODE_SHIFT_RIGHT}, {"LeftAlt", AKEYCODE_ALT_LEFT}, {"Alt", AKEYCODE_ALT_LEFT},
        {"RightAlt", AKEYCODE_ALT_RIGHT}, {"LeftSuper", AKEYCODE_META_LEFT}, {"Super", AKEYCODE_META_LEFT},
        {"RightSuper", AKEYCODE_META_RIGHT}, {"Menu", AKEYCODE_MENU},
        {"0", AKEYCODE_0}, {"1", AKEYCODE_1}, {"2", AKEYCODE_2}, {"3", AKEYCODE_3}, {"4", AKEYCODE_4},
        {"5", AKEYCODE_5}, {"6", AKEYCODE_6}, {"7", AKEYCODE_7}, {"8", AKEYCODE_8}, {"9", AKEYCODE_9},
        {"A", AKEYCODE_A}, {"B", AKEYCODE_B}, {"C", AKEYCODE_C}, {"D", AKEYCODE_D}, {"E", AKEYCODE_E},
        {"F", AKEYCODE_F}, {"G", AKEYCODE_G}, {"H", AKEYCODE_H}, {"I", AKEYCODE_I}, {"J", AKEYCODE_J},
        {"K", AKEYCODE_K}, {"L", AKEYCODE_L}, {"M", AKEYCODE_M}, {"N", AKEYCODE_N}, {"O", AKEYCODE_O},
        {"P", AKEYCODE_P}, {"Q", AKEYCODE_Q}, {"R", AKEYCODE_R}, {"S", AKEYCODE_S}, {"T", AKEYCODE_T},
        {"U", AKEYCODE_U}, {"V", AKEYCODE_V}, {"W", AKEYCODE_W}, {"X", AKEYCODE_X}, {"Y", AKEYCODE_Y},
        {"Z", AKEYCODE_Z},
        {"F1", AKEYCODE_F1}, {"F2", AKEYCODE_F2}, {"F3", AKEYCODE_F3}, {"F4", AKEYCODE_F4}, {"F5", AKEYCODE_F5},
        {"F6", AKEYCODE_F6}, {"F7", AKEYCODE_F7}, {"F8", AKEYCODE_F8}, {"F9", AKEYCODE_F9}, {"F10", AKEYCODE_F10},
        {"F11", AKEYCODE_F11}, {"F12", AKEYCODE_F12},
        {"Apostrophe", AKEYCODE_APOSTROPHE}, {"Comma", AKEYCODE_COMMA}, {"Minus", AKEYCODE_MINUS}, {"Period", AKEYCODE_PERIOD},
        {"Slash", AKEYCODE_SLASH}, {"Semicolon", AKEYCODE_SEMICOLON}, {"Equal", AKEYCODE_EQUALS},
        {"BracketLeft", AKEYCODE_LEFT_BRACKET}, {"Backslash", AKEYCODE_BACKSLASH}, {"BracketRight", AKEYCODE_RIGHT_BRACKET},
        {"QuoteLeft", AKEYCODE_GRAVE}, {"CapsLock", AKEYCODE_CAPS_LOCK}, {"ScrollLock", AKEYCODE_SCROLL_LOCK},
        {"NumLock", AKEYCODE_NUM_LOCK}, {"PrintScreen", AKEYCODE_SYSRQ}, {"Pause", AKEYCODE_BREAK},
        {"Keypad0", AKEYCODE_NUMPAD_0}, {"Keypad1", AKEYCODE_NUMPAD_1}, {"Keypad2", AKEYCODE_NUMPAD_2},
        {"Keypad3", AKEYCODE_NUMPAD_3}, {"Keypad4", AKEYCODE_NUMPAD_4}, {"Keypad5", AKEYCODE_NUMPAD_5},
        {"Keypad6", AKEYCODE_NUMPAD_6}, {"Keypad7", AKEYCODE_NUMPAD_7}, {"Keypad8", AKEYCODE_NUMPAD_8},
        {"Keypad9", AKEYCODE_NUMPAD_9}, {"KeypadPeriod", AKEYCODE_NUMPAD_DOT}, {"KeypadDivide", AKEYCODE_NUMPAD_DIVIDE},
        {"KeypadMultiply", AKEYCODE_NUMPAD_MULTIPLY}, {"KeypadMinus", AKEYCODE_NUMPAD_SUBTRACT},
        {"KeypadPlus", AKEYCODE_NUMPAD_ADD}, {"KeypadReturn", AKEYCODE_NUMPAD_ENTER}, {"KeypadEqual", AKEYCODE_NUMPAD_EQUALS},
        {"Plus", AKEYCODE_EQUALS}, {"Asterisk", AKEYCODE_NUMPAD_MULTIPLY},
    };

    for (const HostKeyMapping& mapping : mappings)
    {
        if (mapping.name == str)
            return mapping.code;
    }

    return std::nullopt;
}

std::optional<std::string> InputManager::ConvertHostKeyboardCodeToString(u32 code)
{
    struct HostKeyReverseMapping
    {
        u32 code;
        const char* name;
    };

    static constexpr HostKeyReverseMapping mappings[] = {
        {AKEYCODE_DPAD_LEFT, "Left"}, {AKEYCODE_DPAD_RIGHT, "Right"}, {AKEYCODE_DPAD_UP, "Up"}, {AKEYCODE_DPAD_DOWN, "Down"},
        {AKEYCODE_PAGE_UP, "PageUp"}, {AKEYCODE_PAGE_DOWN, "PageDown"}, {AKEYCODE_MOVE_HOME, "Home"}, {AKEYCODE_MOVE_END, "End"},
        {AKEYCODE_INSERT, "Insert"}, {AKEYCODE_FORWARD_DEL, "Delete"}, {AKEYCODE_DEL, "Backspace"}, {AKEYCODE_SPACE, "Space"},
        {AKEYCODE_ENTER, "Return"}, {AKEYCODE_ESCAPE, "Escape"}, {AKEYCODE_CTRL_LEFT, "LeftCtrl"}, {AKEYCODE_CTRL_RIGHT, "RightCtrl"},
        {AKEYCODE_SHIFT_LEFT, "LeftShift"}, {AKEYCODE_SHIFT_RIGHT, "RightShift"}, {AKEYCODE_ALT_LEFT, "LeftAlt"},
        {AKEYCODE_ALT_RIGHT, "RightAlt"}, {AKEYCODE_META_LEFT, "LeftSuper"}, {AKEYCODE_META_RIGHT, "RightSuper"},
        {AKEYCODE_MENU, "Menu"},
        {AKEYCODE_0, "0"}, {AKEYCODE_1, "1"}, {AKEYCODE_2, "2"}, {AKEYCODE_3, "3"}, {AKEYCODE_4, "4"},
        {AKEYCODE_5, "5"}, {AKEYCODE_6, "6"}, {AKEYCODE_7, "7"}, {AKEYCODE_8, "8"}, {AKEYCODE_9, "9"},
        {AKEYCODE_A, "A"}, {AKEYCODE_B, "B"}, {AKEYCODE_C, "C"}, {AKEYCODE_D, "D"}, {AKEYCODE_E, "E"},
        {AKEYCODE_F, "F"}, {AKEYCODE_G, "G"}, {AKEYCODE_H, "H"}, {AKEYCODE_I, "I"}, {AKEYCODE_J, "J"},
        {AKEYCODE_K, "K"}, {AKEYCODE_L, "L"}, {AKEYCODE_M, "M"}, {AKEYCODE_N, "N"}, {AKEYCODE_O, "O"},
        {AKEYCODE_P, "P"}, {AKEYCODE_Q, "Q"}, {AKEYCODE_R, "R"}, {AKEYCODE_S, "S"}, {AKEYCODE_T, "T"},
        {AKEYCODE_U, "U"}, {AKEYCODE_V, "V"}, {AKEYCODE_W, "W"}, {AKEYCODE_X, "X"}, {AKEYCODE_Y, "Y"},
        {AKEYCODE_Z, "Z"},
        {AKEYCODE_F1, "F1"}, {AKEYCODE_F2, "F2"}, {AKEYCODE_F3, "F3"}, {AKEYCODE_F4, "F4"}, {AKEYCODE_F5, "F5"},
        {AKEYCODE_F6, "F6"}, {AKEYCODE_F7, "F7"}, {AKEYCODE_F8, "F8"}, {AKEYCODE_F9, "F9"}, {AKEYCODE_F10, "F10"},
        {AKEYCODE_F11, "F11"}, {AKEYCODE_F12, "F12"},
        {AKEYCODE_APOSTROPHE, "Apostrophe"}, {AKEYCODE_COMMA, "Comma"}, {AKEYCODE_MINUS, "Minus"}, {AKEYCODE_PERIOD, "Period"},
        {AKEYCODE_SLASH, "Slash"}, {AKEYCODE_SEMICOLON, "Semicolon"}, {AKEYCODE_EQUALS, "Equal"},
        {AKEYCODE_LEFT_BRACKET, "BracketLeft"}, {AKEYCODE_BACKSLASH, "Backslash"}, {AKEYCODE_RIGHT_BRACKET, "BracketRight"},
        {AKEYCODE_GRAVE, "QuoteLeft"}, {AKEYCODE_CAPS_LOCK, "CapsLock"}, {AKEYCODE_SCROLL_LOCK, "ScrollLock"},
        {AKEYCODE_NUM_LOCK, "NumLock"}, {AKEYCODE_SYSRQ, "PrintScreen"}, {AKEYCODE_BREAK, "Pause"},
        {AKEYCODE_NUMPAD_0, "Keypad0"}, {AKEYCODE_NUMPAD_1, "Keypad1"}, {AKEYCODE_NUMPAD_2, "Keypad2"},
        {AKEYCODE_NUMPAD_3, "Keypad3"}, {AKEYCODE_NUMPAD_4, "Keypad4"}, {AKEYCODE_NUMPAD_5, "Keypad5"},
        {AKEYCODE_NUMPAD_6, "Keypad6"}, {AKEYCODE_NUMPAD_7, "Keypad7"}, {AKEYCODE_NUMPAD_8, "Keypad8"},
        {AKEYCODE_NUMPAD_9, "Keypad9"}, {AKEYCODE_NUMPAD_DOT, "KeypadPeriod"}, {AKEYCODE_NUMPAD_DIVIDE, "KeypadDivide"},
        {AKEYCODE_NUMPAD_MULTIPLY, "KeypadMultiply"}, {AKEYCODE_NUMPAD_SUBTRACT, "KeypadMinus"},
        {AKEYCODE_NUMPAD_ADD, "KeypadPlus"}, {AKEYCODE_NUMPAD_ENTER, "KeypadReturn"}, {AKEYCODE_NUMPAD_EQUALS, "KeypadEqual"},
    };

    for (const HostKeyReverseMapping& mapping : mappings)
    {
        if (mapping.code == code)
            return std::string(mapping.name);
    }

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

