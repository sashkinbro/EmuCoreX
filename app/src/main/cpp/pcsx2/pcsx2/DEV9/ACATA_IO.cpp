
///////////////// I/O THREAD CODE BELOW ONLY
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/FileSystem.h"

#include "ACATA.h"
#include "ACATA_IO_CHD.h"

#if __POSIX__
#define INVALID_HANDLE_VALUE -1
#include <unistd.h>
#include <fcntl.h>
#endif

std::mutex ACATA::TH::ioMutex;
bool ACATA::TH::b_isIdle,
    ACATA::TH::ioWrite,
    ACATA::TH::ioRead,
    ACATA::TH::isCHD;
std::condition_variable ACATA::TH::Idle_cv, ACATA::TH::ioReady;
FILE* ACATA::TH::IMAGE;
s64 ACATA::TH::IMAGESIZE;
u32 ACATA::TH::sectorsize = ACATAPI::CONSTANTS::DVD_SECTORSIZE; //TODO: remove hardcode before testing HDD/CD games !
u32 ACATA::TH::nsector;
s64 ACATA::TH::LBA;

ChdImage CHD;


void ACATA::TH::IO_Read(u32* addr, u32 size) {
	const s64 lba = LBA;
	const u64 pos = lba * sectorsize;
	u64 size2 = sectorsize*nsector;
	if (size != (size2)) Console.Error("ACATA:IO_Read> mismatch on request and read...\n%ld vs %ld (sec:%d,lba:%d)",
			 size, (size2), sectorsize, nsector);

	if (isCHD) {
		if (!CHD.ReadLogicalSectors(LBA, nsector, sectorsize, (void*)addr)) {
			Console.ErrorFmt("ACATA:IO_ReadCHD: lba:{} nsector:{} sectorsize:{} failed", LBA, nsector, sectorsize);
			pxAssert(false);
			abort();
		}

	} else {
		//Console.WriteLn("%s: from %08X, len %08x", __FUNCTION__, pos, (size2));
		if (FileSystem::FSeek64(IMAGE, pos, SEEK_SET) != 0) {
			Console.ErrorFmt("ACATA:IO_Read: failed to seek pos:{}", pos);
			pxAssert(false);
			abort();
		}

		if (std::fread(addr, sectorsize, nsector, IMAGE) != static_cast<size_t>(nsector)) {
			Console.ErrorFmt("ACATA:IO_Read: size:{} at:{} failed", size2, pos);
			pxAssert(false);
			abort();
		}
	}
	{
		std::lock_guard ioSignallock(ioMutex);
		ioRead = false;
	}
}

void ACATA::TH::IO_Write(u32* addr, u32 size) {
	if (!isCHD) {
		const s64 lba = ACATA::TH::LBA;
		const u64 pos = lba * ACATA::TH::sectorsize;
		u64 size2 = (u64)sectorsize * nsector;
		if (size != size2)
			Console.Error("%s> mismatch %ld vs %ld", __FUNCTION__, size, size2);
		if (FileSystem::FSeek64(IMAGE, pos, SEEK_SET) != 0) {
			Console.ErrorFmt("ACATA:IO_Write: failed to seek pos:{}", pos);
			return;
		}
		if (std::fwrite(addr, sectorsize, nsector, IMAGE) != static_cast<size_t>(nsector)) {
			Console.ErrorFmt("ACATA:IO_Write: size:{} at:{} failed", size2, pos);
			return;
		}
		std::fflush(IMAGE);
	} else Console.ErrorFmt("{}: skipping write due to CHD media", __FUNCTION__);
}

int ACATA::TH::IO_OpenImage() {
	isCHD = ChdImage::IsChdFileName(ACATA::imgpath);
	if (isCHD) {
		if (CHD.Open(ACATA::imgpath)) {
			const u32 chdSectorSize = CHD.GetSectorSize();
			if (chdSectorSize == 0 || sectorsize < chdSectorSize || (sectorsize % chdSectorSize) != 0)
			{
				Console.ErrorFmt("ACATA: cannot adapt {}-byte CHD units to declared {}-byte sectors",
				                 chdSectorSize, sectorsize);
				CHD.Close();
				isCHD = false;
				return EINVAL;
			}
			if (chdSectorSize != sectorsize)
				Console.WarningFmt("ACATA: adapting {}-byte CHD units to declared {}-byte sectors",
				                   chdSectorSize, sectorsize);
			ACATA::TH::IMAGESIZE = (CHD.GetSectorCount() * chdSectorSize);
		} else return EIO;
		Console.WriteLn("%s: CHD image opened ok", __FUNCTION__);
	} else {
		ACATA::TH::IMAGE = FileSystem::OpenCFile(ACATA::imgpath.c_str(), "r+b");

		if (!ACATA::TH::IMAGE)
			ACATA::TH::IMAGE = FileSystem::OpenCFile(ACATA::imgpath.c_str(), "rb");

		if (!ACATA::TH::IMAGE) {
			Console.ErrorFmt("{}> fail to fopen '{}' w/ error {} '{}'", __FUNCTION__, ACATA::imgpath, errno, strerror(errno));
			return errno;
		}
		s64 t;
		if ((t = FileSystem::FSize64(ACATA::TH::IMAGE)) > 0)
			ACATA::TH::IMAGESIZE = t;
		else {
			Console.ErrorFmt("{}> fail to get filesize: {}", __FUNCTION__, t);
			return EINVAL;
		}
		Console.WriteLn("%s: image opened ok", __FUNCTION__);
	}
	return 0;
}

int ACATA::TH::IO_CloseImage() {
	if (isCHD) {
		Console.WriteLn("%s CHD", __FUNCTION__);
		CHD.Close();
		isCHD = false;
		return 0;
	} else if (ACATA::TH::IMAGE) {
		Console.WriteLn("%s", __FUNCTION__);
		const int result = std::fclose(ACATA::TH::IMAGE);
		ACATA::TH::IMAGE = nullptr;
		return result;
	}
	return EINVAL;
}
