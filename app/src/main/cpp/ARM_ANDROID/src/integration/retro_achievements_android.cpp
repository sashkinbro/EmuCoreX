#include "common/Error.h"
#include "common/HostSys.h"
#include "emucorex/android_runtime.h"
#include "emucorex/retro_achievements_android.h"
#include "pcsx2/Achievements.h"
#include "pcsx2/Config.h"
#include "pcsx2/Host.h"
#include "pcsx2/VMManager.h"

#include "fmt/format.h"

#include <algorithm>
#include <cstdlib>
#include <jni.h>
#include <mutex>

using emucorex::android::JStringToString;
using emucorex::android::StringToJString;

namespace
{
JavaVM* s_java_vm = nullptr;
jclass s_achievements_bridge_class = nullptr;
std::mutex s_achievements_mutex;

void InitializeJniIfNeeded(JNIEnv* env, jclass clazz)
{
	std::lock_guard lock(s_achievements_mutex);
	if (!s_java_vm)
	{
		env->GetJavaVM(&s_java_vm);
	}
	if (!s_achievements_bridge_class)
	{
		s_achievements_bridge_class = static_cast<jclass>(env->NewGlobalRef(clazz));
	}
}

// The JNI mutex only guards the cached VM/class pointers. It is never held across a call into Java,
// so it cannot participate in a lock-order inversion with the achievements lock.
bool GetJniTargets(JavaVM*& vm, jclass& bridge_class)
{
	std::lock_guard lock(s_achievements_mutex);
	vm = s_java_vm;
	bridge_class = s_achievements_bridge_class;
	return vm && bridge_class;
}

void NotifyStateChanged(
	bool enabled, bool have_user, const char* username, const char* display_name,
	const char* avatar, u32 points, u32 sc_points, u32 unread_messages,
	bool hardcore_pref, bool hardcore_active, bool have_game,
	const char* game_title, const char* rich_presence, const char* icon_path,
	u32 game_id, u32 num_achievements, u32 earned_achievements, u32 score, u32 sc_score,
	bool hardcore_mode, bool leaderboard_enabled, bool rich_presence_enabled
) {
	JavaVM* java_vm = nullptr;
	jclass bridge_class = nullptr;
	if (!GetJniTargets(java_vm, bridge_class)) return;
	JNIEnv* env = nullptr;
	bool attached = false;
	if (java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
	{
		if (java_vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
		attached = true;
	}
	jmethodID method = env->GetStaticMethodID(bridge_class, "notifyStateChanged",
		"(ZZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;IIIZZZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;IIIIIZZZ)V");
	if (method)
	{
		jstring j_username = username ? env->NewStringUTF(username) : nullptr;
		jstring j_display_name = display_name ? env->NewStringUTF(display_name) : nullptr;
		jstring j_avatar = avatar ? env->NewStringUTF(avatar) : nullptr;
		jstring j_game_title = game_title ? env->NewStringUTF(game_title) : nullptr;
		jstring j_rich_presence = rich_presence ? env->NewStringUTF(rich_presence) : nullptr;
		jstring j_icon_path = icon_path ? env->NewStringUTF(icon_path) : nullptr;

		env->CallStaticVoidMethod(bridge_class, method,
			static_cast<jboolean>(enabled), static_cast<jboolean>(have_user), j_username, j_display_name, j_avatar,
			static_cast<jint>(points), static_cast<jint>(sc_points), static_cast<jint>(unread_messages),
			static_cast<jboolean>(hardcore_pref), static_cast<jboolean>(hardcore_active), static_cast<jboolean>(have_game),
			j_game_title, j_rich_presence, j_icon_path,
			static_cast<jint>(game_id), static_cast<jint>(num_achievements), static_cast<jint>(earned_achievements),
			static_cast<jint>(score), static_cast<jint>(sc_score),
			static_cast<jboolean>(hardcore_mode), static_cast<jboolean>(leaderboard_enabled), static_cast<jboolean>(rich_presence_enabled)
		);

		if (j_username) env->DeleteLocalRef(j_username);
		if (j_display_name) env->DeleteLocalRef(j_display_name);
		if (j_avatar) env->DeleteLocalRef(j_avatar);
		if (j_game_title) env->DeleteLocalRef(j_game_title);
		if (j_rich_presence) env->DeleteLocalRef(j_rich_presence);
		if (j_icon_path) env->DeleteLocalRef(j_icon_path);
	}
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (attached) java_vm->DetachCurrentThread();
}
} // namespace

void NotifyLoginRequested(int reason)
{
	JavaVM* java_vm = nullptr;
	jclass bridge_class = nullptr;
	if (!GetJniTargets(java_vm, bridge_class)) return;
	JNIEnv* env = nullptr;
	bool attached = false;
	if (java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
	{
		if (java_vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
		attached = true;
	}
	jmethodID method = env->GetStaticMethodID(bridge_class, "notifyLoginRequested", "(I)V");
	if (method)
	{
		env->CallStaticVoidMethod(bridge_class, method, static_cast<jint>(reason));
	}
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (attached) java_vm->DetachCurrentThread();
}

void NotifyLoginSuccess(const char* username, u32 points, u32 sc_points, u32 unread_messages)
{
	JavaVM* java_vm = nullptr;
	jclass bridge_class = nullptr;
	if (!GetJniTargets(java_vm, bridge_class)) return;
	JNIEnv* env = nullptr;
	bool attached = false;
	if (java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
	{
		if (java_vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
		attached = true;
	}
	jmethodID method = env->GetStaticMethodID(bridge_class, "notifyLoginSuccess", "(Ljava/lang/String;III)V");
	if (method)
	{
		jstring j_username = username ? env->NewStringUTF(username) : nullptr;
		env->CallStaticVoidMethod(bridge_class, method, j_username, static_cast<jint>(points), static_cast<jint>(sc_points), static_cast<jint>(unread_messages));
		if (j_username) env->DeleteLocalRef(j_username);
	}
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (attached) java_vm->DetachCurrentThread();
}

void NotifyHardcoreModeChanged(bool enabled)
{
	JavaVM* java_vm = nullptr;
	jclass bridge_class = nullptr;
	if (!GetJniTargets(java_vm, bridge_class)) return;
	JNIEnv* env = nullptr;
	bool attached = false;
	if (java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
	{
		if (java_vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
		attached = true;
	}
	jmethodID method = env->GetStaticMethodID(bridge_class, "notifyHardcoreModeChanged", "(Z)V");
	if (method)
	{
		env->CallStaticVoidMethod(bridge_class, method, static_cast<jboolean>(enabled));
	}
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (attached) java_vm->DetachCurrentThread();
}

void NotifySettingsChanged(const char* section, const char* key, const char* value)
{
	JavaVM* java_vm = nullptr;
	jclass bridge_class = nullptr;
	if (!GetJniTargets(java_vm, bridge_class)) return;
	JNIEnv* env = nullptr;
	bool attached = false;
	if (java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
	{
		if (java_vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
		attached = true;
	}
	jmethodID method = env->GetStaticMethodID(bridge_class, "notifySettingsChanged", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
	if (method)
	{
		jstring j_section = section ? env->NewStringUTF(section) : nullptr;
		jstring j_key = key ? env->NewStringUTF(key) : nullptr;
		jstring j_value = value ? env->NewStringUTF(value) : nullptr;
		env->CallStaticVoidMethod(bridge_class, method, j_section, j_key, j_value);
		if (j_section) env->DeleteLocalRef(j_section);
		if (j_key) env->DeleteLocalRef(j_key);
		if (j_value) env->DeleteLocalRef(j_value);
	}
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (attached) java_vm->DetachCurrentThread();
}

void QueryAndNotifyAchievementsState()
{
	// Settings are read before the achievements lock is taken: settings -> achievements is the
	// only order that exists elsewhere (callbacks hold the achievements lock and then write settings).
	bool enabled = EmuConfig.Achievements.Enabled;
	if (Host::Internal::GetBaseSettingsLayer() != nullptr)
		enabled = Host::GetBaseBoolSettingValue("Achievements", "Enabled", enabled);
	const bool hardcore_pref = EmuConfig.Achievements.HardcoreMode;

	std::string username;
	std::string display_name;
	std::string avatar;
	bool have_user = false;
	bool hardcore_active = false;
	bool have_game = false;
	u32 points = 0;
	u32 sc_points = 0;
	u32 unread_messages = 0;

	std::string game_title;
	std::string rich_presence;
	std::string icon_path;
	u32 game_id = 0;
	u32 num_achievements = 0;
	u32 earned_achievements = 0;
	u32 score = 0;
	u32 sc_score = 0;
	bool has_leaderboards = false;
	bool has_rich_presence = false;

	{
		// rc_client, s_game_title, rich presence, icon URL and the badge downloader are all owned by
		// the achievements lock (recursive). JNI IO threads must hold it for the whole snapshot, and it
		// is released before calling into Java so no lock is ever held across the upcall.
		const auto lock = Achievements::GetLock();

		const char* const raw_username = Achievements::GetLoggedInUserName();
		have_user = raw_username != nullptr;
		if (raw_username)
			username = raw_username;
		const char* const raw_display_name = Achievements::GetLoggedInUserDisplayName();
		if (raw_display_name)
			display_name = raw_display_name;
		avatar = Achievements::GetLoggedInUserBadgePath();
		points = Achievements::GetLoggedInUserPoints();
		sc_points = Achievements::GetLoggedInUserSoftcorePoints();
		unread_messages = Achievements::GetLoggedInUserUnreadMessages();

		hardcore_active = Achievements::IsHardcoreModeActive();
		have_game = Achievements::HasActiveGame();

		if (have_game)
		{
			game_title = Achievements::GetGameTitle();
			rich_presence = Achievements::GetRichPresenceString();
			icon_path = Achievements::GetGameIconURL();
			game_id = Achievements::GetGameID();
			num_achievements = Achievements::GetNumAchievements();
			earned_achievements = Achievements::GetEarnedAchievements();
			score = Achievements::GetEarnedPoints();
			sc_score = Achievements::GetGamePoints();
			has_leaderboards = Achievements::HasLeaderboards();
			has_rich_presence = Achievements::HasRichPresence();
		}
	}

	NotifyStateChanged(
		enabled, have_user, have_user ? username.c_str() : nullptr, display_name.empty() ? nullptr : display_name.c_str(),
		avatar.empty() ? nullptr : avatar.c_str(), points, sc_points, unread_messages,
		hardcore_pref, hardcore_active, have_game,
		game_title.empty() ? nullptr : game_title.c_str(),
		rich_presence.empty() ? nullptr : rich_presence.c_str(),
		icon_path.empty() ? nullptr : icon_path.c_str(),
		game_id, num_achievements, earned_achievements, score, sc_score,
		hardcore_active, has_leaderboards, has_rich_presence
	);
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeRequestState(JNIEnv* env, jclass clazz)
{
	InitializeJniIfNeeded(env, clazz);
	QueryAndNotifyAchievementsState();
}

extern "C" JNIEXPORT jstring JNICALL Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeLogin(JNIEnv* env, jclass clazz, jstring username, jstring password)
{
	InitializeJniIfNeeded(env, clazz);
	std::string user_str = JStringToString(env, username);
	std::string pass_str = JStringToString(env, password);
	Error error;
	bool success = Achievements::Login(user_str.c_str(), pass_str.c_str(), &error);
	if (success)
	{
		QueryAndNotifyAchievementsState();
		return nullptr;
	}
	else
	{
		std::string err_msg = error.GetDescription();
		if (err_msg.empty()) err_msg = "Login failed";
		return StringToJString(env, err_msg);
	}
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeLogout(JNIEnv* env, jclass clazz)
{
	InitializeJniIfNeeded(env, clazz);
	Achievements::Logout();
	QueryAndNotifyAchievementsState();
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeSetEnabled(JNIEnv* env, jclass clazz, jboolean enabled)
{
	InitializeJniIfNeeded(env, clazz);
	EmuConfig.Achievements.Enabled = (enabled == JNI_TRUE);
	if (Host::Internal::GetBaseSettingsLayer() != nullptr)
	{
		Host::SetBaseBoolSettingValue("Achievements", "Enabled", enabled == JNI_TRUE);
		Host::CommitBaseSettingChanges();
	}
	if (enabled == JNI_TRUE)
	{
		// The Android hub exists before a VM (and sometimes before a base settings
		// layer). RA account/profile requests still need a persistent rcheevos client.
		if (!Achievements::IsActive())
			Achievements::Initialize();
	}
	else if (Achievements::IsActive())
	{
		Achievements::Shutdown(false);
	}
	QueryAndNotifyAchievementsState();
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeSetHardcore(JNIEnv* env, jclass clazz, jboolean enabled)
{
	InitializeJniIfNeeded(env, clazz);
	const bool requested = (enabled == JNI_TRUE);
	EmuConfig.Achievements.HardcoreMode = requested;
	if (Host::Internal::GetBaseSettingsLayer() != nullptr)
	{
		Host::SetBaseBoolSettingValue("Achievements", "ChallengeMode", requested);
		Host::CommitBaseSettingChanges();
	}

	if (Achievements::IsActive() && !requested)
	{
		// Hardcore -> Casual is allowed immediately. Casual -> Hardcore is only
		// activated by VMManager on a full boot/reset, never by this UI call.
		Achievements::DisableHardcoreMode();
	}
	QueryAndNotifyAchievementsState();
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeSetAchievementIndicators(
	JNIEnv* env, jclass clazz, jboolean enabled)
{
	InitializeJniIfNeeded(env, clazz);
	const bool requested = (enabled == JNI_TRUE);
	EmuConfig.Achievements.Overlays = requested;
	if (Host::Internal::GetBaseSettingsLayer() != nullptr)
	{
		Host::SetBaseBoolSettingValue("Achievements", "Overlays", requested);
		Host::CommitBaseSettingChanges();
	}
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeSetNotifications(
	JNIEnv* env, jclass clazz, jboolean enabled)
{
	InitializeJniIfNeeded(env, clazz);
	const bool requested = (enabled == JNI_TRUE);
	EmuConfig.Achievements.Notifications = requested;
	if (Host::Internal::GetBaseSettingsLayer() != nullptr)
	{
		Host::SetBaseBoolSettingValue("Achievements", "Notifications", requested);
		Host::CommitBaseSettingChanges();
	}
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeSetLeaderboardNotifications(
	JNIEnv* env, jclass clazz, jboolean enabled)
{
	InitializeJniIfNeeded(env, clazz);
	const bool requested = (enabled == JNI_TRUE);
	EmuConfig.Achievements.LeaderboardNotifications = requested;
	if (Host::Internal::GetBaseSettingsLayer() != nullptr)
	{
		Host::SetBaseBoolSettingValue("Achievements", "LeaderboardNotifications", requested);
		Host::CommitBaseSettingChanges();
	}
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeSetLeaderboardTrackers(
	JNIEnv* env, jclass clazz, jboolean enabled)
{
	InitializeJniIfNeeded(env, clazz);
	const bool requested = (enabled == JNI_TRUE);
	EmuConfig.Achievements.LBOverlays = requested;
	if (Host::Internal::GetBaseSettingsLayer() != nullptr)
	{
		Host::SetBaseBoolSettingValue("Achievements", "LBOverlays", requested);
		Host::CommitBaseSettingChanges();
	}
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeSetSoundEffects(
	JNIEnv* env, jclass clazz, jboolean enabled)
{
	InitializeJniIfNeeded(env, clazz);
	const bool requested = (enabled == JNI_TRUE);
	EmuConfig.Achievements.SoundEffects = requested;
	if (Host::Internal::GetBaseSettingsLayer() != nullptr)
	{
		Host::SetBaseBoolSettingValue("Achievements", "SoundEffects", requested);
		Host::CommitBaseSettingChanges();
	}
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativeSetUnlockSound(
	JNIEnv* env, jclass clazz, jstring path)
{
	InitializeJniIfNeeded(env, clazz);
	const std::string requested = JStringToString(env, path);
	EmuConfig.Achievements.UnlockSoundName = requested;
	if (Host::Internal::GetBaseSettingsLayer() != nullptr)
	{
		Host::SetBaseStringSettingValue("Achievements", "UnlockSoundName", requested.c_str());
		Host::CommitBaseSettingChanges();
	}
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_utils_RetroAchievementsBridge_nativePreviewSound(
	JNIEnv* env, jclass clazz, jstring path)
{
	InitializeJniIfNeeded(env, clazz);
	const std::string requested = JStringToString(env, path);
	return Common::PlaySoundAsync(requested.c_str()) ? JNI_TRUE : JNI_FALSE;
}

namespace
{
bool RestartAchievementsClientAfterHostChange()
{
	if (!Achievements::IsActive())
		return true;

	Achievements::Shutdown(false);
	return Achievements::Initialize();
}
} // namespace

extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_setAchievementsHostOverride(
	JNIEnv* env, jclass, jstring host)
{
	if (Host::Internal::GetBaseSettingsLayer() == nullptr)
		return JNI_FALSE;

	const std::string host_string = JStringToString(env, host);
	if (host_string.empty())
		return JNI_FALSE;

	// The Android receiver persists the original preference because the base
	// settings layer itself is reconstructed from DataStore after every cold start.
	Host::SetBaseStringSettingValue("Achievements", "Host", host_string.c_str());
	Host::RemoveBaseSettingValue("Achievements", "HostOverrideSavedHardcore");
	Host::SetBaseBoolSettingValue("Achievements", "ChallengeMode", false);
	EmuConfig.Achievements.HardcoreMode = false;
	if (Achievements::IsHardcoreModeActive())
		Achievements::DisableHardcoreMode();
	Host::CommitBaseSettingChanges();

	const bool restarted = RestartAchievementsClientAfterHostChange();
	QueryAndNotifyAchievementsState();
	return restarted ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_clearAchievementsHostOverride(
	JNIEnv*, jclass, jint hardcore_restore_mode)
{
	if (Host::Internal::GetBaseSettingsLayer() == nullptr)
		return JNI_FALSE;

	Host::RemoveBaseSettingValue("Achievements", "Host");
	if (hardcore_restore_mode >= 0)
	{
		const bool restore_hardcore = (hardcore_restore_mode != 0);
		Host::SetBaseBoolSettingValue("Achievements", "ChallengeMode", restore_hardcore);
		EmuConfig.Achievements.HardcoreMode = restore_hardcore;
	}
	Host::RemoveBaseSettingValue("Achievements", "HostOverrideSavedHardcore");
	Host::CommitBaseSettingChanges();

	const bool restarted = RestartAchievementsClientAfterHostChange();
	QueryAndNotifyAchievementsState();
	return restarted ? JNI_TRUE : JNI_FALSE;
}

// Returns 0 when the user may pause now, otherwise the number of emulated frames still required by
// rc_client_can_pause(). Must only be called when the user actually asks to pause.
extern "C" JNIEXPORT jint JNICALL Java_com_sbro_emucorex_core_NativeApp_achievementsRequestPause(JNIEnv*, jclass)
{
	u32 frames_remaining = 0;
	if (Achievements::CanUserPause(&frames_remaining))
		return 0;
	return static_cast<jint>(std::max<u32>(frames_remaining, 1u));
}

// Hardcore was just requested with a game running: reset the VM so that VMManager::Reset() activates it
// (ResetHardcoreMode) together with rc_client_reset, exactly like a RC_CLIENT_EVENT_RESET.
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_requestVmReset(JNIEnv*, jclass)
{
	Host::RunOnCPUThread([]() {
		if (VMManager::HasValidVM())
			VMManager::RequestReset();
	}, false);
}

bool emucorex::android::IsRetroAchievementsHardcoreActive()
{
	return Achievements::IsHardcoreModeActive();
}

bool emucorex::android::IsRetroAchievementsHardcoreRequested()
{
	return EmuConfig.Achievements.HardcoreMode || Achievements::IsHardcoreModeActive();
}

bool emucorex::android::IsRetroAchievementsFeatureBlocked(HardcoreRestrictedFeature feature)
{
	switch (feature)
	{
		case HardcoreRestrictedFeature::LoadState:
		case HardcoreRestrictedFeature::Cheats:
		case HardcoreRestrictedFeature::SlowMotion:
		case HardcoreRestrictedFeature::FrameAdvance:
		case HardcoreRestrictedFeature::ResumeState:
		case HardcoreRestrictedFeature::MemoryEditing:
		case HardcoreRestrictedFeature::InputPlayback:
			return IsRetroAchievementsHardcoreActive();
	}

	return true;
}

std::string emucorex::android::GetRetroAchievementsUserAgent()
{
	// RetroAchievements requires "<product>/<numeric semver> (<system info>) <extensions>": the product
	// token has no spaces and the version is digits and dots only (no "v" prefix, no build suffix).
	const auto numeric_version = [](const char* text) {
		std::string result;
		if (text)
		{
			const char* p = text;
			if (*p == 'v' || *p == 'V')
				++p;
			while ((*p >= '0' && *p <= '9') || *p == '.')
				result.push_back(*p++);
		}
		while (!result.empty() && result.back() == '.')
			result.pop_back();
		if (result.empty() || result.front() == '.' || result.find("..") != std::string::npos)
			result = "0";
		// Pad to major.minor.patch.
		size_t parts = 1;
		for (const char c : result)
			parts += (c == '.') ? 1 : 0;
		for (; parts < 3; ++parts)
			result += ".0";
		return result;
	};

	const std::string app_version = numeric_version(std::getenv("EMUCOREX_APP_VERSION"));
	return fmt::format("EmuCoreX/{} ({}) pcsx2/{}", app_version, GetOSVersionString(), numeric_version(EMUCOREX_PCSX2_TAG));
}
