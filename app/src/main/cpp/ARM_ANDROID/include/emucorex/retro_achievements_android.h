#pragma once

#include "common/Pcsx2Types.h"

#include <string>

namespace Achievements
{
	// Android-facing data exported by the PCSX2 achievements overlay.
	std::string GetActiveGameDataJSON();
	std::string GetAccountProgressJSON();

	u32 GetLoggedInUserPoints();
	u32 GetLoggedInUserSoftcorePoints();
	u32 GetLoggedInUserUnreadMessages();
	const char* GetLoggedInUserDisplayName();

	u32 GetNumAchievements();
	u32 GetEarnedAchievements();
	u32 GetGamePoints();
	u32 GetEarnedPoints();

	// rc_client_can_pause() wrapper. Call it only when the USER asks to pause (menu, pause hotkey):
	// a successful call records the pause for the compounding rate limit. Returns true when the pause
	// is allowed (always in softcore); otherwise *frames_remaining is set to the delay in frames.
	bool CanUserPause(u32* frames_remaining);
}

// Host callbacks consumed by the upstream PCSX2 achievements implementation.
void NotifyLoginRequested(int reason);
void NotifyLoginSuccess(const char* username, u32 points, u32 sc_points, u32 unread_messages);
void NotifyHardcoreModeChanged(bool enabled);
void NotifySettingsChanged(const char* section, const char* key, const char* value);
void QueryAndNotifyAchievementsState();

namespace emucorex::android
{
enum class HardcoreRestrictedFeature
{
	LoadState,
	Cheats,
	SlowMotion,
	FrameAdvance,
	ResumeState,
	MemoryEditing,
	InputPlayback,
};

// The active rc_client state is authoritative for restrictions during a session.
bool IsRetroAchievementsHardcoreActive();

// The preference is used to sanitize settings before a Hardcore boot/reset.
bool IsRetroAchievementsHardcoreRequested();

bool IsRetroAchievementsFeatureBlocked(HardcoreRestrictedFeature feature);
std::string GetRetroAchievementsUserAgent();
}
