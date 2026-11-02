// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "glad/gl.h"

struct GLColorClipStorage
{
    GLenum format;
    GLenum upload_type;
    unsigned char pixel_shift;
};

// Preserve the same65535 encoding when normalized16 storage is unavailable.
constexpr GLColorClipStorage ResolveOGLColorClipStorage(bool rgba16_unorm)
{
    return rgba16_unorm ? GLColorClipStorage{GL_RGBA16, GL_UNSIGNED_SHORT, 3} :
        GLColorClipStorage{GL_RGBA32F, GL_FLOAT, 4};
}

constexpr bool OGLStorageSupportsARMColorFetch(GLenum format)
{
    return format == GL_RGBA8 || format == GL_RGBA16 || format == GL_RGB10_A2;
}
