// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/OpenGL/GLContext.h"

#if defined(_WIN32)
#include "GS/Renderers/OpenGL/GLContextWGL.h"
#else // Linux
#if defined(__ANDROID__)
#include "GS/Renderers/OpenGL/GLContextEGLAndroid.h"
#endif
#ifdef X11_API
#include "GS/Renderers/OpenGL/GLContextEGLX11.h"
#endif
#ifdef WAYLAND_API
#include "GS/Renderers/OpenGL/GLContextEGLWayland.h"
#endif
#endif

#include "common/Console.h"
#include "common/Error.h"

#include "glad/gl.h"

#include <cstdlib>
#include <cstring>

static bool ShouldPreferESContext()
{
#ifndef _MSC_VER
	const char* value = std::getenv("PREFER_GLES_CONTEXT");
	return (value && std::strcmp(value, "1") == 0);
#else
	char buffer[2] = {};
	size_t buffer_size = sizeof(buffer);
	getenv_s(&buffer_size, buffer, "PREFER_GLES_CONTEXT");
	return (std::strcmp(buffer, "1") == 0);
#endif
}

static void DisableBrokenExtensions(const char* gl_vendor, const char* gl_renderer)
{
	if (std::strstr(gl_vendor, "ARM") || std::strstr(gl_renderer, "Mali"))
	{
		// GL_{EXT,OES}_copy_image appears to fall back to CPU paths on Mali.
		Console.Warning("Mali driver detected, disabling GL_{EXT,OES}_copy_image");
		GLAD_GL_EXT_copy_image = 0;
		GLAD_GL_OES_copy_image = 0;
	}
}

GLContext::GLContext(const WindowInfo& wi)
	: m_wi(wi)
{
}

GLContext::~GLContext() = default;

std::unique_ptr<GLContext> GLContext::Create(const WindowInfo& wi, Error* error)
{
	return Create(wi, std::span<const Version>(GetAllVersionsList().data(), GetAllVersionsList().size()), error);
}

std::unique_ptr<GLContext> GLContext::Create(const WindowInfo& wi, std::span<const Version> versions_to_try, Error* error)
{
	std::array<Version, 16> reordered_versions = {};
	if (wi.type == WindowInfo::Type::Android || ShouldPreferESContext())
	{
		size_t count = 0;
		for (const Version& version : versions_to_try)
		{
			if (version.profile == Profile::ES)
				reordered_versions[count++] = version;
		}
		for (const Version& version : versions_to_try)
		{
			if (version.profile != Profile::ES)
				reordered_versions[count++] = version;
		}

		versions_to_try = std::span<const Version>(reordered_versions.data(), versions_to_try.size());
	}

	std::unique_ptr<GLContext> context;
#if defined(_WIN32)
	context = GLContextWGL::Create(wi, versions_to_try, error);
#else // Linux
#if defined(__ANDROID__)
	if (wi.type == WindowInfo::Type::Android)
		context = GLContextEGLAndroid::Create(wi, versions_to_try, error);
#endif
#if defined(X11_API)
	if (!context && wi.type == WindowInfo::Type::X11)
		context = GLContextEGLX11::Create(wi, versions_to_try, error);
#endif

#if defined(WAYLAND_API)
	if (!context && wi.type == WindowInfo::Type::Wayland)
		context = GLContextEGLWayland::Create(wi, versions_to_try, error);
#endif
#endif

	if (!context)
		return nullptr;

	// NOTE: Not thread-safe. But this is okay, since we're not going to be creating more than one context at a time.
	static GLContext* context_being_created;
	context_being_created = context.get();

	const auto load_proc = [](const char* name) { return reinterpret_cast<GLADapiproc>(context_being_created->GetProcAddress(name)); };
	if (!context->IsGLES())
	{
		if (!gladLoadGL(load_proc))
		{
			Error::SetStringView(error, "Failed to load GL functions for GLAD");
			return nullptr;
		}
	}
	else
	{
		if (!gladLoadGLES2(load_proc))
		{
			Error::SetStringView(error, "Failed to load GLES functions for GLAD");
			return nullptr;
		}
	}

	context_being_created = nullptr;

	const char* gl_vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
	const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
	if (gl_vendor && gl_renderer)
		DisableBrokenExtensions(gl_vendor, gl_renderer);

	return context;
}

const std::array<GLContext::Version, 16>& GLContext::GetAllVersionsList()
{
	static constexpr std::array<Version, 16> vlist = {{{Profile::Core, 4, 6},
		{Profile::Core, 4, 5},
		{Profile::Core, 4, 4},
		{Profile::Core, 4, 3},
		{Profile::Core, 4, 2},
		{Profile::Core, 4, 1},
		{Profile::Core, 4, 0},
		{Profile::Core, 3, 3},
		{Profile::Core, 3, 2},
		{Profile::Core, 3, 1},
		{Profile::Core, 3, 0},
		{Profile::ES, 3, 2},
		{Profile::ES, 3, 1},
		{Profile::ES, 3, 0},
		{Profile::ES, 2, 0},
		{Profile::NoProfile, 0, 0}}};
	return vlist;
}
