// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "common/ScopedGuard.h"
#include "GS/Renderers/Common/GSPresentationPolicy.h"
extern "C" void EmuCoreXOracleGSOverlayContent(void(*check)(bool,const char*))
{
#ifdef GS_PRESENTABLE_OVERLAY_SUPPORTED
    const bool isolated=!g_gs_device&&!FullscreenUI::HasActiveWindow()&&!ImGuiFullscreen::HasActiveNotifications();
    check(isolated,"GS overlay query fixture requires isolated host state");if(!isolated)return;
    std::deque<OSDMessage> posted,active;
    {std::unique_lock lock(s_osd_messages_lock);posted.swap(s_osd_posted_messages);}active.swap(s_osd_active_messages);
    ScopedGuard restore=[&](){std::unique_lock lock(s_osd_messages_lock);posted.swap(s_osd_posted_messages);active.swap(s_osd_active_messages);ImGuiFullscreen::ClearNotifications();};
    check(!ImGuiManager::HasPresentableOverlayContent(),"GS actual empty message and notification queues permit startup blank suppression");
    {std::unique_lock lock(s_osd_messages_lock);s_osd_posted_messages.emplace_back();}
    check(ImGuiManager::HasPresentableOverlayContent(),"GS actual cross-thread posted message queue prevents hiding startup overlay");
    check(!(ShouldSkipAndroidBlankFrame(true,false,false,1)&&!ImGuiManager::HasPresentableOverlayContent()),"GS actual startup blank policy with posted OSD requires presentation");
    {std::unique_lock lock(s_osd_messages_lock);s_osd_posted_messages.clear();}s_osd_active_messages.emplace_back();
    check(ImGuiManager::HasPresentableOverlayContent(),"GS actual GS-thread active OSD messages remain presentable");s_osd_active_messages.clear();
    ImGuiFullscreen::AddNotification("gs-overlay-fixture",60.0f,"fixture","visible notification","");
    check(ImGuiManager::HasPresentableOverlayContent(),"GS actual fullscreen notification producer keeps startup frames presentable");
    ImGuiFullscreen::ClearNotifications();check(!ImGuiManager::HasPresentableOverlayContent(),"GS actual notification retirement restores empty overlay query");
#else
    check(false,"GS production overlay-content query and notification consumer are available");
#endif
}
