// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Counters.h"
#include "Common.h"
#include "Config.h"
#include "Gif_Unit.h"
#include "MTGS.h"
#include "MTVU.h"
#include "VMManager.h"
#include "DEV9/ACJV.h"

#include <list>

alignas(16) u8 g_RealGSMem[Ps2MemSize::GSregs];
static bool s_GSRegistersWritten = false;

static __fi void gsSyncMTVUReadback()
{
	if (!THREAD_VU1)
		return;

	// Do not synchronously wait for VU1 here. Pulling pending MTVU changes is
	// enough to keep GS register reads coherent without stalling hot polling loops.
	vu1Thread.Get_MTVUChanges();
}

void gsSetVideoMode(GS_VideoMode mode)
{
	gsVideoMode = mode;
	UpdateVSyncRate(false);
}

// Make sure framelimiter options are in sync with GS capabilities.
void gsReset()
{
	MTGS::ResetGS(true);
	gsVideoMode = GS_VideoMode::Uninitialized;
	std::memset(g_RealGSMem, 0, sizeof(g_RealGSMem));
	UpdateVSyncRate(true);
}

static __fi void gsCSRwrite( const tGS_CSR& csr )
{
	if (csr.RESET) {
		//Console.Warning( "csr.RESET" );
		//gifUnit.Reset(true); // Don't think gif should be reset...
		gifUnit.gsSIGNAL.queued = false;
		gifUnit.gsFINISH.gsFINISHFired = true;
		gifUnit.gsFINISH.gsFINISHPending = false;
		// Privilage registers also reset.
		std::memset(g_RealGSMem, 0, sizeof(g_RealGSMem));
		GSIMR.reset();
		CSRreg.Reset();
		MTGS::ResetGS(false);
	}

	if(csr.FLUSH)
	{
		// Our emulated GS has no FIFO, but if it did, it would flush it here...
		//Console.WriteLn("GS_CSR FLUSH GS fifo: %x (CSRr=%x)", value, GSCSRr);
	}

	if(csr.SIGNAL)
	{
		const bool resume = CSRreg.SIGNAL;
		// SIGNAL : What's not known here is whether or not the SIGID register should be updated
		//  here or when the IMR is cleared (below).
		if (gifUnit.gsSIGNAL.queued) {
			//DevCon.Warning("Firing pending signal");
			GSSIGLBLID.SIGID = (GSSIGLBLID.SIGID & ~gifUnit.gsSIGNAL.data[1])
				        | (gifUnit.gsSIGNAL.data[0]&gifUnit.gsSIGNAL.data[1]);

			if (!GSIMR.SIGMSK) gsIrq();
			CSRreg.SIGNAL  = true; // Just to be sure :p
		}
		else CSRreg.SIGNAL = false;
		gifUnit.gsSIGNAL.queued = false;

		if (resume)
			gifUnit.Execute(false, true); // Resume paused transfers
	}

	if (csr.FINISH)	{
		CSRreg.FINISH = false;
		gifUnit.gsFINISH.gsFINISHFired = false; //Clear the previously fired FINISH (YS, Indiecar 2005, MGS3)
		// Arcade boards need the queued FINISH to survive a CSR acknowledgement until
		// their delayed GS->EE readback completes. Retail games keep PCSX2's original behavior.
		if (ACJV::GetGameId().empty())
			gifUnit.gsFINISH.gsFINISHPending = false;
	}
	if(csr.HSINT)	CSRreg.HSINT	= false;
	if(csr.VSINT)	CSRreg.VSINT	= false;
	if(csr.EDWINT)	CSRreg.EDWINT	= false;
}

static __fi void IMRwrite(u32 value)
{
	if (CSRreg.GetInterruptMask() & (~value & GSIMR._u32) >> 8)
		gsIrq();

	GSIMR._u32 = (value & 0x1f00)|0x6000;
}

__fi void gsWrite8(u32 mem, u8 value)
{
	switch (mem)
	{
		// CSR 8-bit write handlers.
		// I'm quite sure these would just write the CSR portion with the other
		// bits set to 0 (no action).  The previous implementation masked the 8-bit
		// write value against the previous CSR write value, but that really doesn't
		// make any sense, given that the real hardware's CSR circuit probably has no
		// real "memory" where it saves anything.  (for example, you can't write to
		// and change the GS revision or ID portions -- they're all hard wired.) --air

		case GS_CSR: // GS_CSR
			gsCSRwrite( tGS_CSR((u32)value) );			break;
		case GS_CSR + 1: // GS_CSR
			gsCSRwrite( tGS_CSR(((u32)value) <<  8) );	break;
		case GS_CSR + 2: // GS_CSR
			gsCSRwrite( tGS_CSR(((u32)value) << 16) );	break;
		case GS_CSR + 3: // GS_CSR
			gsCSRwrite( tGS_CSR(((u32)value) << 24) );	break;

		default:
			*PS2GS_BASE(mem) = value;
		break;
	}
}

//////////////////////////////////////////////////////////////////////////
// GS Write 16 bit

__fi void gsWrite16(u32 mem, u16 value)
{
	switch (mem)
	{
		// See note above about CSR 8 bit writes, and handling them as zero'd bits
		// for all but the written parts.

		case GS_CSR:
			gsCSRwrite( tGS_CSR((u32)value) );
		return; // do not write to MTGS memory

		case GS_CSR+2:
			gsCSRwrite( tGS_CSR(((u32)value) << 16) );
		return; // do not write to MTGS memory

		case GS_IMR:
			IMRwrite(value);
		return; // do not write to MTGS memory
	}

	*(u16*)PS2GS_BASE(mem) = value;
}

//////////////////////////////////////////////////////////////////////////
// GS Write 32 bit

__fi void gsWrite32(u32 mem, u32 value)
{
	pxAssume( (mem & 3) == 0 );

	switch (mem)
	{
		case GS_CSR:
			gsCSRwrite(tGS_CSR(value));
		return;

		case GS_IMR:
			IMRwrite(value);
		return;
	}

	*(u32*)PS2GS_BASE(mem) = value;
}

//////////////////////////////////////////////////////////////////////////
// GS Write 64 bit

void gsWrite64_generic( u32 mem, u64 value )
{
	std::memcpy(PS2GS_BASE(mem), &value, sizeof(value));
}

void gsWrite64_page_00( u32 mem, u64 value )
{
	s_GSRegistersWritten |= (mem == GS_DISPFB1 || mem == GS_DISPFB2 || mem == GS_PMODE);
	bool reqUpdate = false;
	if (mem == GS_SMODE1 || mem == GS_SMODE2)
	{
		if (value != *(u64*)PS2GS_BASE(mem))
			reqUpdate = true;
	}

	gsWrite64_generic( mem, value );

	if (reqUpdate)
		UpdateVSyncRate(false);
}

void gsWrite64_page_01( u32 mem, u64 value )
{
	switch( mem )
	{
		case GS_BUSDIR:

			gifUnit.stat.DIR = static_cast<u32>(value) & 1;
			if (gifUnit.stat.DIR) {      // Assume will do local->host transfer
				gifUnit.stat.OPH = true; // Should we set OPH here?
				gifUnit.FlushToMTGS();   // Send any pending GS Primitives to the GS
			}

			gsWrite64_generic( mem, value );
		return;

		case GS_CSR:
			gsCSRwrite(tGS_CSR(value));
		return;

		case GS_IMR:
			IMRwrite(static_cast<u32>(value));
		return;
	}

	gsWrite64_generic( mem, value );
}

//////////////////////////////////////////////////////////////////////////
// GS Write 128 bit

void TAKES_R128 gsWrite128_page_00( u32 mem, r128 value )
{
	gsWrite128_generic( mem, value );
}

void TAKES_R128 gsWrite128_page_01( u32 mem, r128 value )
{
	switch( mem )
	{
		case GS_CSR:
			gsCSRwrite(r128_to_u32(value));
		return;

		case GS_IMR:
			IMRwrite(r128_to_u32(value));
		return;
	}

	gsWrite128_generic( mem, value );
}

void TAKES_R128 gsWrite128_generic( u32 mem, r128 value )
{
	r128_store(PS2GS_BASE(mem), value);
}

__fi u8 gsRead8(u32 mem)
{
	gsSyncMTVUReadback();

	switch (mem & ~0xF)
	{
		case GS_SIGLBLID:
			return *(u8*)PS2GS_BASE(mem);
		default: // Only SIGLBLID and CSR are readable, everything else mirrors CSR
			return *(u8*)PS2GS_BASE(GS_CSR + (mem & 0xF));
	}
}

__fi u16 gsRead16(u32 mem)
{
	gsSyncMTVUReadback();

	switch (mem & ~0xF)
	{
		case GS_SIGLBLID:
			return *(u16*)PS2GS_BASE(mem);
		default: // Only SIGLBLID and CSR are readable, everything else mirrors CSR
			return *(u16*)PS2GS_BASE(GS_CSR + (mem & 0x7));
	}
}

__fi u32 gsRead32(u32 mem)
{
	gsSyncMTVUReadback();

	switch (mem & ~0xF)
	{
		case GS_SIGLBLID:
			return *(u32*)PS2GS_BASE(mem);
		default: // Only SIGLBLID and CSR are readable, everything else mirrors CSR
			return *(u32*)PS2GS_BASE(GS_CSR + (mem & 0xC));
	}
}

__fi u64 gsRead64(u32 mem)
{
	gsSyncMTVUReadback();

	// fixme - PS2GS_BASE(mem+4) = (g_RealGSMem+(mem + 4 & 0x13ff))
	switch (mem & ~0xF)
	{
		case GS_SIGLBLID:
			return *(u64*)PS2GS_BASE(mem);
		default: // Only SIGLBLID and CSR are readable, everything else mirrors CSR
			return *(u64*)PS2GS_BASE(GS_CSR + (mem & 0x8));
	}
}

__fi u128 gsNonMirroredRead(u32 mem)
{
	gsSyncMTVUReadback();

	return *(u128*)PS2GS_BASE(mem);
}

void gsIrq() {
	hwIntcIrq(INTC_GS);
}

//These are done at VSync Start.  Drawing is done when VSync is off, then output the screen when Vsync is on
//The GS needs to be told at the start of a vsync else it loses half of its picture (could be responsible for some halfscreen issues)
//We got away with it before i think due to our awful GS timing, but now we have it right (ish)
void gsPostVsyncStart()
{
	//gifUnit.FlushToMTGS();  // Needed for some (broken?) homebrew game loaders

	const bool registers_written = s_GSRegistersWritten;
	s_GSRegistersWritten = false;
	MTGS::PostVsyncStart(registers_written);
}

bool SaveStateBase::gsFreeze()
{
	FreezeMem(PS2MEM_GS, 0x2000);
	Freeze(gsVideoMode);
	return IsOkay();
}

// Manual frameskip target (Android). Set from the UI thread via the JNI
// setFrameSkip, read on the GS thread in GSRenderer::VSync. Relaxed atomic — a
// stale read at most mis-skips a single frame, which is harmless.
static std::atomic<u32> s_manual_frameskip{0};
void GSSetManualFrameSkip(u32 frames)
{
	s_manual_frameskip.store(frames, std::memory_order_relaxed);
}
u32 GSGetManualFrameSkip()
{
	return s_manual_frameskip.load(std::memory_order_relaxed);
}

// Max presented-FPS cap (Android). Caps the DISPLAY frame rate without slowing
// emulation — read on the GS thread in GSRenderer::VSync, which drops a present
// only when ahead of the target interval (adaptive, no over-skip). 0 = off.
// s_max_present_fps is the cap value (for the OSD label); s_max_present_interval
// is the vsync-aligned minimum present spacing in CPU ticks, computed in
// native-lib setFpsCap where the native refresh is known, so display rates snap
// to whole vsync multiples (60/30/20/15…) and hold steady at the boundary.
static std::atomic<u32> s_max_present_fps{0};
static std::atomic<u64> s_max_present_interval{0};
// Fast-forward (Turbo) bypasses the present cap so the speed-up is visible. Set
// from the limiter-mode JNI (Turbo → true, anything else → false) and read on
// the GS thread in GSRenderer::VSync. Unlimited (frame-limit-off steady state)
// deliberately does NOT set this — there the present cap is still wanted.
static std::atomic<bool> s_present_cap_suspended{false};
void GSSetMaxPresentFps(u32 fps, u64 present_interval)
{
	s_max_present_fps.store(fps, std::memory_order_relaxed);
	s_max_present_interval.store(present_interval, std::memory_order_relaxed);
}
u32 GSGetMaxPresentFps()
{
	return s_max_present_fps.load(std::memory_order_relaxed);
}
u64 GSGetMaxPresentInterval()
{
	return s_max_present_interval.load(std::memory_order_relaxed);
}
void GSSetPresentCapSuspended(bool suspended)
{
	s_present_cap_suspended.store(suspended, std::memory_order_relaxed);
}
bool GSGetPresentCapSuspended()
{
	return s_present_cap_suspended.load(std::memory_order_relaxed);
}

