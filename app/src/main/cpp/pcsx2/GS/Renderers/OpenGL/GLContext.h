// SPDX-FileCopyrightText: 2002-2023 PCSX2 Dev Team
// SPDX-License-Identifier: LGPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"
#include "common/WindowInfo.h"

#include <array>
#include <memory>
#include <vector>

class GLContext
{
public:
	GLContext(const WindowInfo& wi);
	virtual ~GLContext();

	enum class Profile
	{
		NoProfile,
		Core,
		ES
	};

	struct Version
	{
		Profile profile;
		int major_version;
		int minor_version;
	};

	struct FullscreenModeInfo
	{
		u32 width;
		u32 height;
		float refresh_rate;
	};

	__fi const WindowInfo& GetWindowInfo() const { return m_wi; }
	__fi bool IsGLES() const { return (m_version.profile == Profile::ES); }
	__fi u32 GetSurfaceWidth() const { return m_wi.surface_width; }
	__fi u32 GetSurfaceHeight() const { return m_wi.surface_height; }

	virtual void* GetProcAddress(const char* name) = 0;
	virtual bool ChangeSurface(const WindowInfo& new_wi) = 0;
	virtual void ResizeSurface(u32 new_surface_width = 0, u32 new_surface_height = 0) = 0;
	virtual bool SwapBuffers() = 0;
	virtual bool MakeCurrent() = 0;
	virtual bool DoneCurrent() = 0;
	virtual bool SetSwapInterval(s32 interval) = 0;
	virtual std::unique_ptr<GLContext> CreateSharedContext(const WindowInfo& wi) = 0;

	virtual std::vector<FullscreenModeInfo> EnumerateFullscreenModes();

	static std::unique_ptr<GLContext> Create(const WindowInfo& wi, const Version* versions_to_try,
										   size_t num_versions_to_try);

	template<size_t N>
	static std::unique_ptr<GLContext> Create(const WindowInfo& wi, const std::array<Version, N>& versions_to_try)
	{
		return Create(wi, versions_to_try.data(), versions_to_try.size());
	}

	static std::unique_ptr<GLContext> Create(const WindowInfo& wi) { return Create(wi, GetAllVersionsList()); }

	static const std::array<Version, 16>& GetAllVersionsList();

protected:
	WindowInfo m_wi;
	Version m_version = {};
};
