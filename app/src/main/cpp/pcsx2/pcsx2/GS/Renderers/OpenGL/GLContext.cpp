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

#include <cctype>
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

// Parses the Mali driver token from GL_VERSION, e.g. "OpenGL ES 3.2 v1.r54p1-01rel0"
// (r-driver) or "OpenGL ES 3.2 v1.g20p0-..." (g-driver).
static bool ParseMaliDriverVersion(
	const char* gl_version, int* gles_major, int* gles_minor, int* release, bool* is_g_driver)
{
	if (!gl_version || std::sscanf(gl_version, "OpenGL ES %d.%d", gles_major, gles_minor) != 2)
		return false;

	for (const char* pos = gl_version; (pos = std::strchr(pos, '.')) != nullptr; pos++)
	{
		const char kind = pos[1];
		if ((kind != 'r' && kind != 'g') || !std::isdigit(static_cast<unsigned char>(pos[2])))
			continue;

		char* end = nullptr;
		const long value = std::strtol(pos + 2, &end, 10);
		if (end == pos + 2)
			continue;

		*release = static_cast<int>(value);
		*is_g_driver = (kind == 'g');
		return true;
	}

	return false;
}

static bool DisableBrokenExtensions(const char* gl_vendor, const char* gl_renderer, const char* gl_version)
{
	if (std::strstr(gl_vendor, "ARM") || std::strstr(gl_renderer, "Mali"))
	{
		// GL_{EXT,OES}_copy_image falls back to CPU paths on old Mali. The driver release in
		// GL_VERSION is the authoritative signal: Bifrost parts (G71/G76) have model >= 57 but
		// old r-drivers, while newer Immortalis parts must not be treated as "old" by name.
		int gles_major = 0;
		int gles_minor = 0;
		int release = 0;
		bool is_g_driver = false;
		const bool parsed = ParseMaliDriverVersion(gl_version, &gles_major, &gles_minor, &release, &is_g_driver);

		const bool usable = parsed &&
			((gles_major >= 3 && is_g_driver && release > 0) ||
				((gles_major > 3 || (gles_major == 3 && gles_minor >= 2)) && !is_g_driver && release > 31));

		if (!usable)
		{
			Console.Warning("Old or unrecognized Mali driver, disabling GL_{EXT,OES}_copy_image (%s).",
				gl_version ? gl_version : "unknown version");
			GLAD_GL_EXT_copy_image = 0;
			GLAD_GL_OES_copy_image = 0;
			return true;
		}

		Console.WriteLn("Modern Mali driver (%s), keeping GL_{EXT,OES}_copy_image enabled.", gl_version);
	}
	return false;
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
	const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
	if (gl_vendor && gl_renderer)
		context->m_copy_image_disabled = DisableBrokenExtensions(gl_vendor, gl_renderer, gl_version);

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
