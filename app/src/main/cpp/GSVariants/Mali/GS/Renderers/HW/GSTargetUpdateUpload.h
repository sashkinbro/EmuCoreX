// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <algorithm>
#include <cstddef>

// Target updates reuse a fixed scratch allocation. Keep each decode within it,
// including targets whose complete RGBA image is larger than that allocation.
// Upload must consume/copy the bytes before returning; GSTexture::Update does.
// Rect is GSVector4i in production; callbacks inline into the target's update.
template <typename Rect, typename Decode, typename ScanAlpha, typename Upload>
bool GSUploadTargetRect(const Rect& source, const Rect& destination, u8* scratch, size_t scratch_bytes,
    u32 pitch, u32 block_height, Decode&& decode, ScanAlpha&& scan_alpha, Upload&& upload)
{
    const int width = source.width();
    const int height = source.height();
    if (!scratch || width <= 0 || height <= 0 || destination.width() != width || destination.height() != height ||
        static_cast<u64>(width) * sizeof(u32) > pitch || !pitch || scratch_bytes < pitch || !block_height)
    {
        return false;
    }

    u32 rows = static_cast<u32>(std::min<size_t>(scratch_bytes / pitch, static_cast<size_t>(height)));
    if (rows < static_cast<u32>(height) && rows >= block_height)
        rows -= rows % block_height;

    for (int y = 0; y < height;)
    {
        const int count = std::min<int>(static_cast<int>(rows), height - y);
        const Rect read(source.x, source.y + y, source.z, source.y + y + count);
        const Rect write(destination.x, destination.y + y, destination.z, destination.y + y + count);
        decode(read, scratch, pitch);
        scan_alpha(scratch, static_cast<u32>(width), static_cast<u32>(count), pitch);
        if (!upload(write, scratch, pitch))
            return false;
        y += count;
    }
    return true;
}
