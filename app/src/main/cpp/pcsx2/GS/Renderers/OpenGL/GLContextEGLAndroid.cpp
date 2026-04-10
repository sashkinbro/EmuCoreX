// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/OpenGL/GLContextEGLAndroid.h"

#include "common/Console.h"
#include "common/Error.h"

#include <android/native_window.h>

GLContextEGLAndroid::GLContextEGLAndroid(const WindowInfo& wi)
	: GLContextEGL(wi)
{
}

GLContextEGLAndroid::~GLContextEGLAndroid() = default;

std::unique_ptr<GLContext> GLContextEGLAndroid::Create(const WindowInfo& wi, std::span<const Version> versions_to_try, Error* error)
{
	std::unique_ptr<GLContextEGLAndroid> context = std::make_unique<GLContextEGLAndroid>(wi);
	if (!context->Initialize(versions_to_try, error))
		return nullptr;

	return context;
}

std::unique_ptr<GLContext> GLContextEGLAndroid::CreateSharedContext(const WindowInfo& wi, Error* error)
{
	std::unique_ptr<GLContextEGLAndroid> context = std::make_unique<GLContextEGLAndroid>(wi);
	context->m_display = m_display;

	if (!context->CreateContextAndSurface(m_version, m_context, false))
	{
		Error::SetStringView(error, "Failed to create Android shared EGL context/surface");
		return nullptr;
	}

	return context;
}

void GLContextEGLAndroid::ResizeSurface(u32 new_surface_width, u32 new_surface_height)
{
	GLContextEGL::ResizeSurface(new_surface_width, new_surface_height);
}

EGLDisplay GLContextEGLAndroid::GetPlatformDisplay(Error* error)
{
	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (dpy == EGL_NO_DISPLAY)
	{
		const EGLint err = eglGetError();
		Error::SetStringFmt(error, "eglGetDisplay(EGL_DEFAULT_DISPLAY) failed: {} (0x{:X})", err, err);
	}

	return dpy;
}

EGLSurface GLContextEGLAndroid::CreatePlatformSurface(EGLConfig config, void* win, Error* error)
{
	ANativeWindow* native_window = static_cast<ANativeWindow*>(win);
	if (!native_window)
	{
		Error::SetStringView(error, "Android native window handle is null");
		return EGL_NO_SURFACE;
	}

	EGLint native_visual_id = 0;
	if (!eglGetConfigAttrib(m_display, config, EGL_NATIVE_VISUAL_ID, &native_visual_id))
	{
		const EGLint err = eglGetError();
		Error::SetStringFmt(error, "eglGetConfigAttrib(EGL_NATIVE_VISUAL_ID) failed: {} (0x{:X})", err, err);
		return EGL_NO_SURFACE;
	}

	ANativeWindow_setBuffersGeometry(native_window, 0, 0, static_cast<int32_t>(native_visual_id));
	m_wi.surface_width = static_cast<u32>(ANativeWindow_getWidth(native_window));
	m_wi.surface_height = static_cast<u32>(ANativeWindow_getHeight(native_window));

	EGLSurface surface = eglCreateWindowSurface(m_display, config, native_window, nullptr);
	if (surface == EGL_NO_SURFACE)
	{
		const EGLint err = eglGetError();
		Error::SetStringFmt(error, "eglCreateWindowSurface() failed: {} (0x{:X})", err, err);
	}

	return surface;
}
