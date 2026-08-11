#include "ACATA_IO_CHD.h"
#include "common/FileSystem.h"
#include "common/StringUtil.h"
#include "common/Console.h"
#include <cstring>
#include <limits>

ChdImage::ChdImage() = default;

ChdImage::~ChdImage()
{
    Close();
}

bool ChdImage::Open(const std::string& path)
{
    Close();

    m_source = FileSystem::OpenCFile(path.c_str(), "rb");
    if (!m_source)
    {
        Console.ErrorFmt("{} failed to open CHD source '{}'", __FUNCTION__, path);
        return false;
    }

    chd_error err = chd_open_file(m_source, CHD_OPEN_READ, nullptr, &m_chd);

    if (err != CHDERR_NONE) {
        Console.ErrorFmt("{} failed to open CHD: {}", __FUNCTION__, (int)err);
        std::fclose(m_source);
        m_source = nullptr;
        return false;
    }

    const chd_header* hdr = chd_get_header(m_chd);

    m_hunkSize = hdr->hunkbytes;

    switch (hdr->unitbytes)
    {
        case 2048:
            m_type = ACMEDIATYPE::ACDVD;
            break;

        case 512:
            m_type = ACMEDIATYPE::ACHDD;
            break;

        default:
            m_type = ACMEDIATYPE::ACUNK;
            break;
    }

    m_unitBytes = hdr->unitbytes;
    m_totalUnits = hdr->logicalbytes / hdr->unitbytes;

    // CD units are raw frames; find where the 2048-byte payload sits (DVD/HDD keep it at 0).
    if (m_unitBytes == 2352 || m_unitBytes == 2448)
        m_frameDataOffset = DetectCdDataOffset();

    m_hunkBuffer.resize(m_hunkSize);

    DevCon.WriteLnFmt("{}: opened ok (unit {}, data offset {})", __FUNCTION__, m_unitBytes, m_frameDataOffset);
    return true;
}

void ChdImage::Close()
{
    if (m_chd)
    {
        chd_close(m_chd);
        m_chd = nullptr;
    }
    if (m_source)
    {
        std::fclose(m_source);
        m_source = nullptr;
    }

    m_hunkBuffer.clear();

    m_cachedHunk = UINT32_MAX;

    m_hunkSize = 0;
    m_unitBytes = 0;
    m_frameDataOffset = 0;
    m_totalUnits = 0;

    m_type = ACMEDIATYPE::ACUNK;
}

bool ChdImage::IsOpen() const
{
    return m_chd != nullptr;
}

ACMEDIATYPE ChdImage::GetType() const
{
    return m_type;
}

u32 ChdImage::GetSectorSize() const
{
    // MAME CD CHDs: libchdr CD codecs already extract user data (2048) from raw sectors
    if (m_unitBytes == 2448 || m_unitBytes == 2352)
        return 2048;
    return m_unitBytes;
}

u64 ChdImage::GetSectorCount() const
{
    return m_totalUnits;
}

// CHD metadata gives the CD track format. Raw sectors (MODE1_RAW/MODE2_RAW) keep the
// sync + header before the 2048-byte data (offset 16/24); plain MODE1/MODE2 store just
// the 2048 data at offset 0 (like an ISO).
u32 ChdImage::DetectCdDataOffset()
{
    char meta[256] = {};
    u32 len = 0;
    if (chd_get_metadata(m_chd, CDROM_TRACK_METADATA2_TAG, 0,
                         meta, sizeof(meta), &len, nullptr, nullptr) != CHDERR_NONE &&
        chd_get_metadata(m_chd, CDROM_TRACK_METADATA_TAG, 0,
                         meta, sizeof(meta), &len, nullptr, nullptr) != CHDERR_NONE)
        return 0;

    // Match the track type (" TYPE:"), not the pregap type ("PGTYPE:").
    if (std::strstr(meta, " TYPE:MODE2_RAW"))
        return 24;
    if (std::strstr(meta, " TYPE:MODE1_RAW"))
        return 16;
    return 0;
}

bool ChdImage::ReadHunk(u32 hunk)
{
    if (hunk == m_cachedHunk)
        return true;

    chd_error err =
        chd_read(m_chd,
                 hunk,
                 m_hunkBuffer.data());

    if (err != CHDERR_NONE)
        return false;

    m_cachedHunk = hunk;
    return true;
}

bool ChdImage::ReadSector(u64 lba, void* buffer)
{
    if (!m_chd)
        return false;

    if (lba >= m_totalUnits)
        return false;

    const u64 byteOffset = lba * m_unitBytes;

    const u32 hunk =
        static_cast<u32>(byteOffset / m_hunkSize);

    const u32 offset =
        static_cast<u32>(byteOffset % m_hunkSize);

    if ((offset + m_unitBytes) > m_hunkSize)
        return false;

    if (!ReadHunk(hunk))
        return false;

    if (m_unitBytes == 2448 || m_unitBytes == 2352)
    {
        std::memcpy(buffer, m_hunkBuffer.data() + offset + m_frameDataOffset, 2048);
    }
    else
    {
        std::memcpy(buffer, m_hunkBuffer.data() + offset, m_unitBytes);
    }

    return true;
}

bool ChdImage::ReadSectors(u64 lba,
                           u32 count,
                           void* buffer)
{
    u8* dst = static_cast<u8*>(buffer);
    const u32 outBytes = (m_unitBytes == 2448 || m_unitBytes == 2352) ? 2048 : m_unitBytes;

    for (u32 i = 0; i < count; i++)
    {
        if (!ReadSector(lba + i,
                        dst + (i * outBytes)))
        {
            return false;
        }
    }

    return true;
}

bool ChdImage::ReadLogicalSectors(u64 lba, u32 count, u32 logicalSectorSize, void* buffer)
{
    const u32 chdSectorSize = GetSectorSize();
    if (chdSectorSize == 0 || logicalSectorSize < chdSectorSize ||
        (logicalSectorSize % chdSectorSize) != 0)
    {
        Console.ErrorFmt("{}: cannot adapt {}-byte CHD units to {}-byte logical sectors",
                         __FUNCTION__, chdSectorSize, logicalSectorSize);
        return false;
    }

    const u32 scale = logicalSectorSize / chdSectorSize;
    if (lba > (std::numeric_limits<u64>::max() / scale) ||
        count > (std::numeric_limits<u32>::max() / scale))
    {
        Console.ErrorFmt("{}: sector range is too large (lba {}, count {}, scale {})",
                         __FUNCTION__, lba, count, scale);
        return false;
    }

    return ReadSectors(lba * scale, count * scale, buffer);
}

bool ChdImage::IsChdFileName(const std::string& path)
{
	return StringUtil::compareNoCase(Path::GetExtension(path), "chd");
}
