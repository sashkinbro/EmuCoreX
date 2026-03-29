// SPDX-FileCopyrightText: 2002-2023 PCSX2 Dev Team
// SPDX-License-Identifier: LGPL-3.0+

#include "GS/Renderers/OpenGL/GLContext.h"

#if defined(_WIN32)
#include "GS/Renderers/OpenGL/GLContextWGL.h"
#elif defined(__APPLE__)
#include "GS/Renderers/OpenGL/GLContextAGL.h"
#else // Linux
#ifdef X11_API
#include "GS/Renderers/OpenGL/GLContextEGLX11.h"
#endif
#ifdef WAYLAND_API
#include "GS/Renderers/OpenGL/GLContextEGLWayland.h"
#endif
#endif

#include "common/Console.h"

#include "glad.h"
#include "GS/Renderers/OpenGL/GLContextEGLAndroid.h"

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
	if (std::strstr(gl_vendor, "ARM"))
	{
		// GL_{EXT,OES}_copy_image seem to be implemented on the CPU in the Mali drivers...
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

std::vector<GLContext::FullscreenModeInfo> GLContext::EnumerateFullscreenModes()
{
	return {};
}

std::unique_ptr<GLContext> GLContext::Create(const WindowInfo& wi, const Version* versions_to_try,
											 size_t num_versions_to_try)
{
	const bool prefer_es = (wi.type == WindowInfo::Type::Android) || ShouldPreferESContext();
	if (prefer_es)
	{
		// Move ES profiles to the front so EGL negotiates a GLES context first on Android/mobile.
		Version* new_versions_to_try = static_cast<Version*>(alloca(sizeof(Version) * num_versions_to_try));
		size_t count = 0;
		for (size_t i = 0; i < num_versions_to_try; i++)
		{
			if (versions_to_try[i].profile == Profile::ES)
				new_versions_to_try[count++] = versions_to_try[i];
		}

		for (size_t i = 0; i < num_versions_to_try; i++)
		{
			if (versions_to_try[i].profile != Profile::ES)
				new_versions_to_try[count++] = versions_to_try[i];
		}

		versions_to_try = new_versions_to_try;
	}

	std::unique_ptr<GLContext> context;
	if(wi.type == WindowInfo::Type::Android)
		context = GLContextEGLAndroid::Create(wi, versions_to_try, num_versions_to_try);
	if (!context)
		return nullptr;

	// NOTE: Not thread-safe. But this is okay, since we're not going to be creating more than one context at a time.
	static GLContext* context_being_created;
	context_being_created = context.get();

	// load up glad
	if (!context->IsGLES())
	{
		if (!gladLoadGLLoader([](const char* name) { return context_being_created->GetProcAddress(name); }))
		{
			Console.Error("Failed to load GL functions for GLAD");
			return nullptr;
		}
	}
	else
	{
		if (!gladLoadGLES2Loader([](const char* name) { return context_being_created->GetProcAddress(name); }))
		{
			Console.Error("Failed to load GLES functions for GLAD");
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
