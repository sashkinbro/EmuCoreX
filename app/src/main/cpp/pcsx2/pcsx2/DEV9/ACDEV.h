#pragma once

// Base of the ACDEV area
#define ACDEV_BASE SPD_REGBASE

// from ACDEV_BASE up to this point, rom0:ROMDRV will look for a ROMFS bios filesystem, as requested by rom0:ACDEV
#define ACDEV_ROMDIR_POKE_END (ACDEV_BASE + 0x8000)
