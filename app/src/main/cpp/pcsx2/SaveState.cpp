// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Achievements.h"
#include "BuildVersion.h"
#include "CDVD/CDVD.h"
#include "COP0.h"
#include "Cache.h"
#include "Config.h"
#include "Counters.h"
#include "DebugTools/Breakpoints.h"
#include "DebugTools/SymbolImporter.h"
#include "Elfheader.h"
#include "GS.h"
#include "GS/GS.h"
#include "Host.h"
#include "MTGS.h"
#include "MTVU.h"
#include "Patch.h"
#include "R3000A.h"
#include "SIO/Multitap/MultitapProtocol.h"
#include "SIO/Pad/Pad.h"
#include "SIO/Sio.h"
#include "SIO/Sio0.h"
#include "SIO/Sio2.h"
#include "SPU2/spu2.h"
#include "SaveState.h"
#include "StateWrapper.h"
#include "USB/USB.h"
#include "VMManager.h"
#include "VUmicro.h"
#include "vtlb.h"
#include "ps2/BiosTools.h"

#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/ScopedGuard.h"
#include "common/StringUtil.h"
#include "common/ZipHelpers.h"

#include "IconsFontAwesome.h"
#include "fmt/format.h"

#include <csetjmp>
#include <array>
#include <png.h>

#ifdef ARCH_ARM64
#include "arm64/cpuRegistersPack.h"
#endif

using namespace R5900;

static tlbs s_tlb_backup[std::size(tlb)];

static void RuntimeStatePreLoadPrep()
{
#ifdef ARCH_ARM64
	Arm64PrepareRuntimeForStateLoad();
#endif
}

static void RuntimeStatePostLoadPrep()
{
#ifdef ARCH_ARM64
	Arm64RepairRuntimeAfterStateLoad();
#endif
}

static void RestoreTlbMappingsAfterStateLoad()
{
	vtlb_RunPostLoadRepair(s_tlb_backup, std::size(tlb), EmuConfig.Gamefixes.GoemonTlbHack);
}

static void RestoreDebuggerStateAfterStateLoad()
{
	CBreakPoints::SetSkipFirst(BREAKPOINT_EE, 0);
	CBreakPoints::SetSkipFirst(BREAKPOINT_IOP, 0);
}

static void RestorePresentationStateAfterStateLoad()
{
	UpdateVSyncRate(true);

	if (VMManager::Internal::HasBootedELF())
		R5900SymbolImporter.OnElfLoadedInMemory();
}

static void WaitForVmSubsystemsBeforeStateLoad()
{
	// ensure everything is in sync before we start overwriting stuff.
	if (THREAD_VU1)
		vu1Thread.WaitVU();
	MTGS::WaitGS(false);
}

static void BackupTlbStateBeforeStateLoad()
{
	vtlb_CaptureStateLoadSnapshot(s_tlb_backup, std::size(s_tlb_backup));
}

static void ResetExecutionTrackingBeforeStateLoad()
{
	vtlb_InvalidateRuntimeBlockTracking();
	VMManager::Internal::ClearCPUExecutionCaches();
}

static void PreLoadPrep()
{
	WaitForVmSubsystemsBeforeStateLoad();
	BackupTlbStateBeforeStateLoad();
	ResetExecutionTrackingBeforeStateLoad();
	RuntimeStatePreLoadPrep();
}

static void PostLoadPrep()
{
	resetCache();
//	WriteCP0Status(cpuRegs.CP0.n.Status.val);
	RestoreTlbMappingsAfterStateLoad();
	RestoreDebuggerStateAfterStateLoad();
	RestorePresentationStateAfterStateLoad();
	RuntimeStatePostLoadPrep();
}

// --------------------------------------------------------------------------------------
//  SaveStateBase  (implementations)
// --------------------------------------------------------------------------------------
SaveStateBase::SaveStateBase(VmStateBuffer& memblock)
	: m_memory(memblock)
	, m_version(g_SaveVersion)
{
}

static bool LoadStateWrapperFromBuffer(const u8* data, size_t size, bool (*do_state_func)(StateWrapper&), size_t* bytes_consumed = nullptr)
{
	StateWrapper::ReadOnlyMemoryStream stream(data, size);
	StateWrapper sw(&stream, StateWrapper::Mode::Read, g_SaveVersion);
	const bool okay = do_state_func(sw) && sw.IsGood();
	if (okay && bytes_consumed)
		*bytes_consumed = stream.GetPosition();
	return okay;
}

static bool SaveStateWrapperToBuffer(std::vector<u8>* out_data, u32 reserve, bool (*do_state_func)(StateWrapper&))
{
	StateWrapper::VectorMemoryStream stream(reserve);
	StateWrapper sw(&stream, StateWrapper::Mode::Write, g_SaveVersion);
	if (!do_state_func(sw) || !sw.IsGood())
		return false;

	*out_data = stream.GetBuffer();
	return true;
}

static std::optional<std::vector<u8>> ReadZipEntryPayload(zip_file_t* zf)
{
	if (!zf)
		return std::vector<u8>{};

	return ReadBinaryFileInZip(zf);
}

static bool WriteBufferToSavestate(SaveStateBase& writer, std::span<const u8> data)
{
	if (!data.empty())
		writer.FreezeMem(const_cast<u8*>(data.data()), static_cast<int>(data.size()));

	return writer.IsOkay();
}

bool SaveStateBase::FreezeStateWrapperBlock(const char* name, u32 reserve, bool (*do_state_func)(StateWrapper&))
{
	(void)name;

	if (IsSaving())
	{
		std::vector<u8> data;
		if (!SaveStateWrapperToBuffer(&data, reserve, do_state_func))
			return false;

		return WriteBufferToSavestate(*this, data);
	}

	const size_t remaining = (m_idx < static_cast<int>(m_memory.size())) ? (m_memory.size() - static_cast<size_t>(m_idx)) : 0;
	size_t bytes_consumed = 0;
	if (!LoadStateWrapperFromBuffer(remaining > 0 ? &m_memory[m_idx] : nullptr, remaining, do_state_func, &bytes_consumed))
		return false;

	const int new_idx = m_idx + static_cast<int>(bytes_consumed);
	if (static_cast<size_t>(new_idx) > m_memory.size())
		return false;

	m_idx = new_idx;
	return true;
}

bool SaveStateBase::FreezeCoreRegisterState()
{
	if (!FreezeTag("cpuRegs"))
		return false;

	Freeze(cpuRegs);		// cpu regs + COP0
	Freeze(psxRegs);		// iop regs
	Freeze(fpuRegs);
	Freeze(tlb);			// tlbs
	Freeze(cachedTlbs);		// cached tlbs
	Freeze(AllowParams1);	//OSDConfig written (Fast Boot)
	Freeze(AllowParams2);
	return IsOkay();
}

bool SaveStateBase::FreezeCycleTimingState()
{
	if (!FreezeTag("Cycles"))
		return false;

	Freeze(EEsCycle);
	Freeze(EEoCycle);
	Freeze(nextDeltaCounter);
	Freeze(nextStartCounter);
	Freeze(psxNextStartCounter);
	Freeze(psxNextDeltaCounter);
	return IsOkay();
}

bool SaveStateBase::FreezeEeSubsystemState(Error* error)
{
	if (!FreezeTag("EE-Subsystems"))
		return false;

	if (!FreezeEeTimingAndMemoryState(error))
		return false;

	if (!FreezeEeVectorUnitState())
		return false;

	return FreezeEeTransferSubsystemState();
}

bool SaveStateBase::FreezeEeTimingAndMemoryState(Error* error)
{
	bool okay = rcntFreeze();
	okay = okay && memFreeze(error);
	return okay;
}

bool SaveStateBase::FreezeEeVectorUnitState()
{
	bool okay = FreezeVuExecutionState();
	okay = okay && FreezeVifTransportState();
	return okay;
}

bool SaveStateBase::FreezeVuExecutionState()
{
	bool okay = vuMicroFreeze();
	okay = okay && vuJITFreeze();
	okay = okay && mtvuFreeze();
	return okay;
}

bool SaveStateBase::FreezeVifTransportState()
{
	bool okay = vif0Freeze();
	okay = okay && vif1Freeze();
	return okay;
}

bool SaveStateBase::FreezeEeTransferSubsystemState()
{
	bool okay = gsFreeze();
	okay = okay && sifFreeze();
	okay = okay && ipuFreeze();
	okay = okay && ipuDmaFreeze();
	okay = okay && gifFreeze();
	okay = okay && gifDmaFreeze();
	okay = okay && sprFreeze();
	return okay;
}

bool SaveStateBase::FreezeIopSubsystemState()
{
	if (!FreezeTag("IOP-Subsystems"))
		return false;

	if (!FreezeIopCounterAndLinkState())
		return false;

	if (!FreezeIopPeripheralState())
		return false;

	return FreezeIopAuxiliaryState();
}

bool SaveStateBase::FreezeIopCounterAndLinkState()
{
	FreezeMem(iopMem->Sif, sizeof(iopMem->Sif));		// iop's sif memory (not really needed, but oh well)

	return psxRcntFreeze();
}

bool SaveStateBase::FreezeIopPeripheralState()
{
	static const auto DoSioPeripheralState = [](StateWrapper& sw) -> bool
	{
		bool ok = g_Sio0.DoState(sw);
		ok = ok && g_Sio2.DoState(sw);
		ok = ok && g_MultitapArr.at(0).DoState(sw);
		ok = ok && g_MultitapArr.at(1).DoState(sw);
		return ok;
	};
	bool okay = FreezeStateWrapperBlock("IOP SIO/Multitap", 16 * 1024, DoSioPeripheralState);
	if (!okay)
		return false;

	okay = okay && cdrFreeze();
	okay = okay && cdvdFreeze();
	return okay;
}

bool SaveStateBase::FreezeIopAuxiliaryState()
{
	bool okay = deci2Freeze();
	okay = okay && InputRecordingFreeze();
	okay = okay && handleFreeze();
	return okay;
}

void SaveStateBase::PrepBlock(int size)
{
	if (m_error)
		return;

	const int end = m_idx + size;
	if (IsSaving())
	{
		if (static_cast<u32>(end) >= m_memory.size())
			m_memory.resize(static_cast<u32>(end));
	}
	else
	{
		if (m_memory.size() < static_cast<u32>(end))
		{
			Console.Error("(SaveStateBase) Buffer overflow in PrepBlock(), expected %d got %zu", end, m_memory.size());
			m_error = true;
		}
	}
}

bool SaveStateBase::FreezePlaceholderBlocks(size_t block_size, size_t block_count)
{
	std::vector<u8> placeholder(block_size);
	for (size_t i = 0; i < block_count; i++)
		FreezeMem(placeholder.data(), static_cast<int>(placeholder.size()));

	return IsOkay();
}

bool SaveStateBase::FreezeTag(const char* src)
{
	if (m_error)
		return false;

	char tagspace[32];
	pxAssertMsg(std::strlen(src) < (sizeof(tagspace) - 1), "Tag name exceeds the allowed length");

	std::memset(tagspace, 0, sizeof(tagspace));
	StringUtil::Strlcpy(tagspace, src, sizeof(tagspace));
	Freeze(tagspace);

	if (std::strcmp(tagspace, src) != 0)
	{
		Console.Error(fmt::format("Savestate data corruption detected while reading tag: {}", src));
		m_error = true;
		return false;
	}

	return true;
}

bool SaveStateBase::FreezeBios()
{
	if (!FreezeTag("BIOS"))
		return false;

	// Check the BIOS, and issue a warning if the bios for this state
	// doesn't match the bios currently being used (chances are it'll still
	// work fine, but some games are very picky).

	u32 bioscheck = BiosChecksum;
	char biosdesc[256];
	std::memset(biosdesc, 0, sizeof(biosdesc));
	StringUtil::Strlcpy(biosdesc, BiosDescription, sizeof(biosdesc));

	Freeze(bioscheck);
	Freeze(biosdesc);

	if (bioscheck != BiosChecksum)
	{
		Console.Error("\n  Warning: BIOS Version Mismatch, savestate may be unstable!");
		Console.Error(
			"    Current BIOS:   %s (crc=0x%08x)\n"
			"    Savestate BIOS: %s (crc=0x%08x)\n",
			BiosDescription.c_str(), BiosChecksum,
			biosdesc, bioscheck
		);
	}

	return IsOkay();
}

bool SaveStateBase::FreezeInternals(Error* error)
{
	// Print this until the MTVU problem in gifPathFreeze is taken care of (rama)
	if (THREAD_VU1)
		Console.Warning("MTVU speedhack is enabled, saved states may not be stable");

	if (!vmFreeze())
		return false;

	if (!FreezeCoreRegisterState())
		return false;

	if (!FreezeCycleTimingState())
		return false;

	if (!FreezeEeSubsystemState(error))
		return false;

	return FreezeIopSubsystemState();
}


// --------------------------------------------------------------------------------------
//  memSavingState (implementations)
// --------------------------------------------------------------------------------------
// uncompressed to/from memory state saves implementation

memSavingState::memSavingState(VmStateBuffer& save_to)
	: SaveStateBase(save_to)
{
}

// Saving of state data
void memSavingState::FreezeMem(void* data, int size)
{
	if (!size) return;

	const int new_size = m_idx + size;
	if (static_cast<u32>(new_size) > m_memory.size())
		m_memory.resize(static_cast<u32>(new_size));

	std::memcpy(&m_memory[m_idx], data, size);
	m_idx += size;
}

// --------------------------------------------------------------------------------------
//  memLoadingState  (implementations)
// --------------------------------------------------------------------------------------
memLoadingState::memLoadingState(const VmStateBuffer& load_from)
	: SaveStateBase(const_cast<VmStateBuffer&>(load_from))
{
}

// Loading of state data from a memory buffer...
void memLoadingState::FreezeMem( void* data, int size )
{
	if (static_cast<u32>(m_idx + size) > m_memory.size())
		m_error = true;

	if (m_error)
	{
		std::memset(data, 0, size);
		return;
	}

	const u8* const src = &m_memory[m_idx];
	m_idx += size;
	std::memcpy(data, src, size);
}

static const char* EntryFilename_StateVersion = "PCSX2 Savestate Version.id";
static const char* EntryFilename_Screenshot = "Screenshot.png";
static const char* EntryFilename_InternalStructures = "PCSX2 Internal Structures.dat";
static constexpr u32 STATE_PCSX2_VERSION_SIZE = 32;

struct SavestateCompressionSettings
{
	u32 method;
	u32 level;
};

struct SavestateVersionIndicator
{
	u32 save_version;
	char version[STATE_PCSX2_VERSION_SIZE];
};

struct SysState_Component
{
	const char* name;
	int (*freeze)(FreezeAction, freezeData*);
};

static int SysState_MTGSFreeze(FreezeAction mode, freezeData* fP)
{
	MTGS::FreezeData sstate = { fP, 0 };
	MTGS::Freeze(mode, sstate);
	return sstate.retval;
}

static constexpr SysState_Component SPU2_{ "SPU2", SPU2freeze };
static constexpr SysState_Component GS{ "GS", SysState_MTGSFreeze };

static bool SaveLegacyFreezeToBuffer(SysState_Component comp, std::vector<u8>* out_data)
{
	freezeData fP = {};
	if (comp.freeze(FreezeAction::Size, &fP) != 0)
	{
		Console.Error(fmt::format("* {}: Failed to get freeze size", comp.name));
		return false;
	}

	if (fP.size <= 0)
	{
		out_data->clear();
		return true;
	}

	out_data->resize(static_cast<size_t>(fP.size));
	fP.data = out_data->data();

	Console.WriteLn("  Saving %s", comp.name);

	if (comp.freeze(FreezeAction::Save, &fP) != 0)
	{
		Console.Error(fmt::format("* {}: Failed to save freeze data", comp.name));
		out_data->clear();
		return false;
	}

	return true;
}

static bool LoadLegacyFreezeFromPayload(SysState_Component comp, std::span<const u8> payload)
{
	freezeData fP = { 0, nullptr };
	if (comp.freeze(FreezeAction::Size, &fP) != 0)
		fP.size = 0;

	Console.WriteLn("  Loading %s", comp.name);

	if (payload.size() != static_cast<size_t>(fP.size))
	{
		Console.Error(fmt::format("* {}: Failed to decompress save data", comp.name));
		return false;
	}

	std::vector<u8> data(payload.begin(), payload.end());
	fP.data = data.empty() ? nullptr : data.data();
	if (comp.freeze(FreezeAction::Load, &fP) != 0)
	{
		Console.Error(fmt::format("* {}: Failed to load freeze data", comp.name));
		return false;
	}

	return true;
}

static bool SysState_ComponentFreezeIn(zip_file_t* zf, SysState_Component comp)
{
	if (!zf)
		return true;

	const std::optional<std::vector<u8>> data = ReadZipEntryPayload(zf);
	if (!data.has_value())
		return false;

	return LoadLegacyFreezeFromPayload(comp, *data);
}

static bool SysState_ComponentFreezeOut(SaveStateBase& writer, SysState_Component comp)
{
	std::vector<u8> data;
	if (!SaveLegacyFreezeToBuffer(comp, &data))
		return false;

	return WriteBufferToSavestate(writer, data);
}

static bool SysState_ComponentFreezeInNew(zip_file_t* zf, const char* name, bool(*do_state_func)(StateWrapper&))
{
	(void)name;
	// TODO: We could decompress on the fly here for a little bit more speed.
	const std::optional<std::vector<u8>> data = ReadZipEntryPayload(zf);
	if (!data.has_value())
		return false;

	return LoadStateWrapperFromBuffer(data->empty() ? nullptr : data->data(), data->size(), do_state_func);
}

static bool SysState_ComponentFreezeOutNew(SaveStateBase& writer, const char* name, u32 reserve, bool (*do_state_func)(StateWrapper&))
{
	(void)name;
	std::vector<u8> data;
	if (!SaveStateWrapperToBuffer(&data, reserve, do_state_func))
		return false;

	return WriteBufferToSavestate(writer, data);
}

// --------------------------------------------------------------------------------------
//  BaseSavestateEntry
// --------------------------------------------------------------------------------------
class BaseSavestateEntry
{
protected:
	BaseSavestateEntry() = default;

public:
	virtual ~BaseSavestateEntry() = default;

	virtual const char* GetFilename() const = 0;
	virtual bool FreezeIn(zip_file_t* zf) const = 0;
	virtual bool FreezeOut(SaveStateBase& writer) const = 0;
	virtual bool IsRequired() const = 0;
};

class StateWrapperSavestateEntry : public BaseSavestateEntry
{
public:
	StateWrapperSavestateEntry(const char* filename, const char* name, u32 reserve,
		bool (*do_state_func)(StateWrapper&), bool required)
		: m_filename(filename)
		, m_name(name)
		, m_reserve(reserve)
		, m_do_state_func(do_state_func)
		, m_required(required)
	{
	}

	const char* GetFilename() const override { return m_filename; }
	bool FreezeIn(zip_file_t* zf) const override { return SysState_ComponentFreezeInNew(zf, m_name, m_do_state_func); }
	bool FreezeOut(SaveStateBase& writer) const override { return SysState_ComponentFreezeOutNew(writer, m_name, m_reserve, m_do_state_func); }
	bool IsRequired() const override { return m_required; }

private:
	const char* m_filename;
	const char* m_name;
	u32 m_reserve;
	bool (*m_do_state_func)(StateWrapper&);
	bool m_required;
};

class LegacyFreezeSavestateEntry : public BaseSavestateEntry
{
public:
	LegacyFreezeSavestateEntry(const char* filename, SysState_Component component, bool required)
		: m_filename(filename)
		, m_component(component)
		, m_required(required)
	{
	}

	const char* GetFilename() const override { return m_filename; }
	bool FreezeIn(zip_file_t* zf) const override { return SysState_ComponentFreezeIn(zf, m_component); }
	bool FreezeOut(SaveStateBase& writer) const override { return SysState_ComponentFreezeOut(writer, m_component); }
	bool IsRequired() const override { return m_required; }

private:
	const char* m_filename;
	SysState_Component m_component;
	bool m_required;
};

class BinaryPayloadSavestateEntry : public BaseSavestateEntry
{
public:
	BinaryPayloadSavestateEntry(const char* filename, bool required, bool (*is_active_func)(),
		void (*load_state_func)(std::span<const u8>), void (*save_state_func)(std::vector<u8>*))
		: m_filename(filename)
		, m_required(required)
		, m_is_active_func(is_active_func)
		, m_load_state_func(load_state_func)
		, m_save_state_func(save_state_func)
	{
	}

	const char* GetFilename() const override { return m_filename; }

	bool FreezeIn(zip_file_t* zf) const override
	{
		if (m_is_active_func && !m_is_active_func())
			return true;

		const std::optional<std::vector<u8>> data = ReadZipEntryPayload(zf);
		if (!data.has_value())
			return false;

		m_load_state_func(std::span<const u8>(*data));
		return true;
	}

	bool FreezeOut(SaveStateBase& writer) const override
	{
		if (m_is_active_func && !m_is_active_func())
			return true;

		std::vector<u8> data;
		m_save_state_func(&data);
		return WriteBufferToSavestate(writer, data);
	}

	bool IsRequired() const override { return m_required; }

private:
	const char* m_filename;
	bool m_required;
	bool (*m_is_active_func)();
	void (*m_load_state_func)(std::span<const u8>);
	void (*m_save_state_func)(std::vector<u8>*);
};

class MemorySavestateEntry : public BaseSavestateEntry
{
protected:
	MemorySavestateEntry() {}
	virtual ~MemorySavestateEntry() = default;

public:
	virtual bool FreezeIn(zip_file_t* zf) const;
	virtual bool FreezeOut(SaveStateBase& writer) const;
	virtual bool IsRequired() const { return true; }

protected:
	virtual u8* GetDataPtr() const = 0;
	virtual u32 GetDataSize() const = 0;
};

class PointerMemorySavestateEntry : public MemorySavestateEntry
{
public:
	PointerMemorySavestateEntry(const char* filename, u8* (*data_ptr_func)(), u32 (*data_size_func)())
		: m_filename(filename)
		, m_data_ptr_func(data_ptr_func)
		, m_data_size_func(data_size_func)
	{
	}

	const char* GetFilename() const override { return m_filename; }
	u8* GetDataPtr() const override { return m_data_ptr_func(); }
	u32 GetDataSize() const override { return m_data_size_func(); }

private:
	const char* m_filename;
	u8* (*m_data_ptr_func)();
	u32 (*m_data_size_func)();
};

bool MemorySavestateEntry::FreezeIn(zip_file_t* zf) const
{
	const u32 expectedSize = GetDataSize();
	const s64 bytesRead = zip_fread(zf, GetDataPtr(), expectedSize);
	if (bytesRead != static_cast<s64>(expectedSize))
	{
		Console.WriteLn(Color_Yellow, " '%s' is incomplete (expected 0x%x bytes, loading only 0x%x bytes)",
			GetFilename(), expectedSize, static_cast<u32>(bytesRead));
	}

	return true;
}

bool MemorySavestateEntry::FreezeOut(SaveStateBase& writer) const
{
	writer.FreezeMem(GetDataPtr(), GetDataSize());
	return writer.IsOkay();
}

// --------------------------------------------------------------------------------------
//  SavestateEntry_* (EmotionMemory, IopMemory, etc)
// --------------------------------------------------------------------------------------
// Implementation Rationale:
//  The address locations of PS2 virtual memory components is fully dynamic, so we need to
//  resolve the pointers at the time they are requested (eeMem, iopMem, etc).  Thusly, we
//  cannot use static struct member initializers -- we need virtual functions that compute
//  and resolve the addresses on-demand instead... --air

static u8* GetEmotionMemoryStatePtr() { return eeMem->Main; }
static u32 GetEmotionMemoryStateSize() { return Ps2MemSize::ExposedRam; }
static u8* GetIopMemoryStatePtr() { return iopMem->Main; }
static u32 GetIopMemoryStateSize() { return Ps2MemSize::ExposedIopRam; }
static u8* GetEeHwRegsStatePtr() { return eeHw; }
static u32 GetEeHwRegsStateSize() { return static_cast<u32>(sizeof(eeHw)); }
static u8* GetIopHwRegsStatePtr() { return iopHw; }
static u32 GetIopHwRegsStateSize() { return static_cast<u32>(sizeof(iopHw)); }
static u8* GetScratchpadStatePtr() { return eeMem->Scratch; }
static u32 GetScratchpadStateSize() { return static_cast<u32>(sizeof(eeMem->Scratch)); }
static u8* GetVu0MemoryStatePtr() { return VU0.Mem; }
static u32 GetVu0MemoryStateSize() { return VU0_MEMSIZE; }
static u8* GetVu1MemoryStatePtr() { return VU1.Mem; }
static u32 GetVu1MemoryStateSize() { return VU1_MEMSIZE; }
static u8* GetVu0MicroStatePtr() { return VU0.Micro; }
static u32 GetVu0MicroStateSize() { return VU0_PROGSIZE; }
static u8* GetVu1MicroStatePtr() { return VU1.Micro; }
static u32 GetVu1MicroStateSize() { return VU1_PROGSIZE; }

// (cpuRegs, iopRegs, VPU/GIF/DMAC structures should all remain as part of a larger unified
//  block, since they're all PCSX2-dependent and having separate files in the archie for them
//  would not be useful).
//

static const std::unique_ptr<BaseSavestateEntry> SavestateEntries[] = {
	std::unique_ptr<BaseSavestateEntry>(new PointerMemorySavestateEntry("eeMemory.bin", &GetEmotionMemoryStatePtr, &GetEmotionMemoryStateSize)),
	std::unique_ptr<BaseSavestateEntry>(new PointerMemorySavestateEntry("iopMemory.bin", &GetIopMemoryStatePtr, &GetIopMemoryStateSize)),
	std::unique_ptr<BaseSavestateEntry>(new PointerMemorySavestateEntry("eeHwRegs.bin", &GetEeHwRegsStatePtr, &GetEeHwRegsStateSize)),
	std::unique_ptr<BaseSavestateEntry>(new PointerMemorySavestateEntry("iopHwRegs.bin", &GetIopHwRegsStatePtr, &GetIopHwRegsStateSize)),
	std::unique_ptr<BaseSavestateEntry>(new PointerMemorySavestateEntry("Scratchpad.bin", &GetScratchpadStatePtr, &GetScratchpadStateSize)),
	std::unique_ptr<BaseSavestateEntry>(new PointerMemorySavestateEntry("vu0Memory.bin", &GetVu0MemoryStatePtr, &GetVu0MemoryStateSize)),
	std::unique_ptr<BaseSavestateEntry>(new PointerMemorySavestateEntry("vu1Memory.bin", &GetVu1MemoryStatePtr, &GetVu1MemoryStateSize)),
	std::unique_ptr<BaseSavestateEntry>(new PointerMemorySavestateEntry("vu0MicroMem.bin", &GetVu0MicroStatePtr, &GetVu0MicroStateSize)),
	std::unique_ptr<BaseSavestateEntry>(new PointerMemorySavestateEntry("vu1MicroMem.bin", &GetVu1MicroStatePtr, &GetVu1MicroStateSize)),
	std::unique_ptr<BaseSavestateEntry>(new LegacyFreezeSavestateEntry("SPU2.bin", SPU2_, true)),
	std::unique_ptr<BaseSavestateEntry>(new StateWrapperSavestateEntry("USB.bin", "USB", 16 * 1024, &USB::DoState, false)),
	std::unique_ptr<BaseSavestateEntry>(new StateWrapperSavestateEntry("PAD.bin", "PAD", 16 * 1024, &Pad::Freeze, true)),
	std::unique_ptr<BaseSavestateEntry>(new LegacyFreezeSavestateEntry("GS.bin", GS, true)),
	std::unique_ptr<BaseSavestateEntry>(new BinaryPayloadSavestateEntry("Achievements.bin", false, &Achievements::IsActive,
		&Achievements::LoadState, &Achievements::SaveStateToBuffer)),
};

static void AddSavestateArchiveEntry(ArchiveEntryList* destlist, const char* filename, uint startpos, uint endpos)
{
	destlist->Add(
		ArchiveEntry(filename)
			.SetDataIndex(startpos)
			.SetDataSize(endpos - startpos));
}

static bool SaveSavestateInternalStructures(memSavingState* saveme, ArchiveEntryList* destlist, Error* error)
{
	const uint internals_start = saveme->GetCurrentPos();

	if (!saveme->FreezeBios())
	{
		Error::SetString(error, "FreezeBios() failed");
		return false;
	}

	if (!saveme->FreezeInternals(error))
	{
		if (!error->IsValid())
			Error::SetString(error, "FreezeInternals() failed");

		return false;
	}

	AddSavestateArchiveEntry(destlist, EntryFilename_InternalStructures, internals_start, saveme->GetCurrentPos());
	return true;
}

static bool SaveSavestateComponentEntries(memSavingState* saveme, ArchiveEntryList* destlist, Error* error)
{
	for (const std::unique_ptr<BaseSavestateEntry>& entry : SavestateEntries)
	{
		const uint startpos = saveme->GetCurrentPos();
		if (!entry->FreezeOut(*saveme))
		{
			Error::SetString(error, fmt::format("FreezeOut() failed for {}.", entry->GetFilename()));
			return false;
		}

		AddSavestateArchiveEntry(destlist, entry->GetFilename(), startpos, saveme->GetCurrentPos());
	}

	return true;
}

static std::unique_ptr<ArchiveEntryList> CreateSavestateArchiveBuffer(Error* error)
{
	std::unique_ptr<ArchiveEntryList> destlist = std::make_unique<ArchiveEntryList>();
	destlist->GetBuffer().resize(1024 * 1024 * 64);

	memSavingState saveme(destlist->GetBuffer());
	if (!SaveSavestateInternalStructures(&saveme, destlist.get(), error))
		return nullptr;

	if (!SaveSavestateComponentEntries(&saveme, destlist.get(), error))
		return nullptr;

	return destlist;
}

std::unique_ptr<ArchiveEntryList> SaveState_DownloadState(Error* error)
{
	return CreateSavestateArchiveBuffer(error);
}

static std::optional<SaveStateScreenshotData> CaptureSavestateScreenshotData(u32 width, u32 height)
{
	SaveStateScreenshotData data = {};
	if (!MTGS::SaveMemorySnapshot(width, height, true, false, &data.width, &data.height, &data.pixels))
		return std::nullopt;

	return data;
}

std::unique_ptr<SaveStateScreenshotData> SaveState_SaveScreenshot()
{
	static constexpr u32 SCREENSHOT_WIDTH = 640;
	static constexpr u32 SCREENSHOT_HEIGHT = 480;

	const std::optional<SaveStateScreenshotData> capture =
		CaptureSavestateScreenshotData(SCREENSHOT_WIDTH, SCREENSHOT_HEIGHT);
	if (!capture.has_value())
		return nullptr;

	return std::make_unique<SaveStateScreenshotData>(std::move(*capture));
}

static void NormalizeSavestateScreenshotAlpha(SaveStateScreenshotData* data, u32 row)
{
	u32* pixels = &data->pixels[row * data->width];
	for (u32 x = 0; x < data->width; x++)
		pixels[x] |= 0xFF000000u;
}

static bool CommitSavestateScreenshot(zip_t* zf, zip_source_t* zs)
{
	if (zip_source_commit_write(zs) != 0)
		return false;

	const s64 file_index = zip_file_add(zf, EntryFilename_Screenshot, zs, 0);
	if (file_index < 0)
		return false;

	// png is already compressed, no point doing it twice
	zip_set_file_compression(zf, file_index, ZIP_CM_STORE, 0);
	return true;
}

static void InitializeSavestateScreenshotWriter(png_structp png_ptr, png_infop info_ptr,
	SaveStateScreenshotData* data, zip_source_t* zs)
{
	png_set_write_fn(png_ptr, zs, [](png_structp png_ptr, png_bytep data_ptr, png_size_t size) {
		zip_source_write(static_cast<zip_source_t*>(png_get_io_ptr(png_ptr)), data_ptr, size);
	}, [](png_structp png_ptr) {});
	png_set_compression_level(png_ptr, 5);
	png_set_IHDR(png_ptr, info_ptr, data->width, data->height, 8, PNG_COLOR_TYPE_RGBA,
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png_ptr, info_ptr);
}

static void WriteSavestateScreenshotRows(SaveStateScreenshotData* data, png_structp png_ptr)
{
	for (u32 y = 0; y < data->height; ++y)
	{
		// ensure the alpha channel is set to opaque
		NormalizeSavestateScreenshotAlpha(data, y);
		u32* row = &data->pixels[y * data->width];
		png_write_row(png_ptr, reinterpret_cast<png_bytep>(row));
	}
}

static bool SaveState_CompressScreenshot(SaveStateScreenshotData* data, zip_t* zf)
{
	zip_error_t ze = {};
	zip_source_t* const zs = zip_source_buffer_create(nullptr, 0, 0, &ze);
	if (!zs)
		return false;

	if (zip_source_begin_write(zs) != 0)
	{
		zip_source_free(zs);
		return false;
	}

	ScopedGuard zs_free([zs]() { zip_source_free(zs); });

	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	png_infop info_ptr = nullptr;
	if (!png_ptr)
		return false;

	ScopedGuard cleanup([&png_ptr, &info_ptr]() {
		if (png_ptr)
			png_destroy_write_struct(&png_ptr, info_ptr ? &info_ptr : nullptr);
	});

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
		return false;

	if (setjmp(png_jmpbuf(png_ptr)))
		return false;

	InitializeSavestateScreenshotWriter(png_ptr, info_ptr, data, zs);
	WriteSavestateScreenshotRows(data, png_ptr);

	png_write_end(png_ptr, nullptr);

	if (!CommitSavestateScreenshot(zf, zs))
		return false;

	// source is now owned by the zip file for later compression
	zs_free.Cancel();
	return true;
}

static SavestateCompressionSettings GetSavestateCompressionSettings()
{
	if (EmuConfig.Savestate.CompressionType == SavestateCompressionMethod::Zstandard)
	{
		if (EmuConfig.Savestate.CompressionRatio == SavestateCompressionLevel::Low)
			return {ZIP_CM_ZSTD, 1};
		if (EmuConfig.Savestate.CompressionRatio == SavestateCompressionLevel::Medium)
			return {ZIP_CM_ZSTD, 3};
		if (EmuConfig.Savestate.CompressionRatio == SavestateCompressionLevel::High)
			return {ZIP_CM_ZSTD, 10};
		return {ZIP_CM_ZSTD, 22};
	}

	if (EmuConfig.Savestate.CompressionType == SavestateCompressionMethod::Deflate64)
	{
		if (EmuConfig.Savestate.CompressionRatio == SavestateCompressionLevel::Low)
			return {ZIP_CM_DEFLATE64, 1};
		if (EmuConfig.Savestate.CompressionRatio == SavestateCompressionLevel::Medium)
			return {ZIP_CM_DEFLATE64, 3};
		if (EmuConfig.Savestate.CompressionRatio == SavestateCompressionLevel::High)
			return {ZIP_CM_DEFLATE64, 7};
		return {ZIP_CM_DEFLATE64, 9};
	}

	if (EmuConfig.Savestate.CompressionType == SavestateCompressionMethod::LZMA2)
	{
		if (EmuConfig.Savestate.CompressionRatio == SavestateCompressionLevel::Low)
			return {ZIP_CM_LZMA2, 1};
		if (EmuConfig.Savestate.CompressionRatio == SavestateCompressionLevel::Medium)
			return {ZIP_CM_LZMA2, 3};
		if (EmuConfig.Savestate.CompressionRatio == SavestateCompressionLevel::High)
			return {ZIP_CM_LZMA2, 7};
		return {ZIP_CM_LZMA2, 9};
	}

	return {ZIP_CM_STORE, 0};
}

static SavestateVersionIndicator MakeSavestateVersionIndicator()
{
	SavestateVersionIndicator version = {};
	version.save_version = g_SaveVersion;
	if (BuildVersion::GitTaggedCommit)
		StringUtil::Strlcpy(version.version, BuildVersion::GitTag, std::size(version.version));
	else
		StringUtil::Strlcpy(version.version, "Unknown", std::size(version.version));

	return version;
}

static void SetSavestateZipWriteOpenError(Error* error, const char* filename, zip_error_t* ze)
{
	Error::SetStringFmt(error,
		TRANSLATE_FS("SaveState", "Failed to open zip file '{}' for save state: {}."),
		filename, zip_error_strerror(ze));
}

static zip_source_t* CreateSavestateZipWriteSource(const char* filename, zip_error_t* ze, Error* error)
{
	zip_source_t* const zs = zip_source_file_create(filename, 0, 0, ze);
	if (!zs)
		SetSavestateZipWriteOpenError(error, filename, ze);

	return zs;
}

static zip_t* OpenSavestateZipFromWriteSource(zip_source_t* zs, zip_error_t* ze, const char* filename, Error* error)
{
	zip_t* const zf = zip_open_from_source(zs, ZIP_CREATE | ZIP_TRUNCATE, ze);
	if (!zf)
	{
		SetSavestateZipWriteOpenError(error, filename, ze);
		zip_source_free(zs);
	}

	return zf;
}

static zip_t* OpenSavestateZipForWrite(const char* filename, Error* error)
{
	zip_error_t ze = {};
	zip_source_t* const zs = CreateSavestateZipWriteSource(filename, &ze, error);
	if (!zs)
		return nullptr;

	return OpenSavestateZipFromWriteSource(zs, &ze, filename, error);
}

static std::unique_ptr<zip_t, void (*)(zip_t*)> OpenSavestateZipForRead(const char* filename, const char* purpose, Error* error = nullptr)
{
	zip_error_t ze = {};
	auto zf = zip_open_managed(filename, ZIP_RDONLY, &ze);
	if (!zf)
	{
		Console.Error("Failed to open zip file '%s' for %s: %s", filename, purpose, zip_error_strerror(&ze));
		if (error)
		{
			if (zip_error_code_zip(&ze) == ZIP_ER_NOENT)
				Error::SetString(error, "Savestate file does not exist.");
			else
				Error::SetString(error, fmt::format("Savestate zip error: {}", zip_error_strerror(&ze)));
		}
	}

	return zf;
}

static bool AddCompressedBufferToZip(zip_t* zf, const char* filename, const void* data, size_t size,
	const SavestateCompressionSettings& compression, bool free_on_close)
{
	zip_source_t* const zs = zip_source_buffer(zf, data, size, free_on_close ? 1 : 0);
	if (!zs)
	{
		if (free_on_close)
			std::free(const_cast<void*>(data));
		return false;
	}

	const s64 fi = zip_file_add(zf, filename, zs, ZIP_FL_ENC_UTF_8);
	if (fi < 0)
	{
		zip_source_free(zs);
		return false;
	}

	zip_set_file_compression(zf, fi, compression.method, compression.level);
	return true;
}

static std::unique_ptr<zip_file_t, int (*)(zip_file_t*)> OpenSavestateZipEntryByName(zip_t* zf, const char* name)
{
	return zip_fopen_managed(zf, name, 0);
}

static std::unique_ptr<zip_file_t, int (*)(zip_file_t*)> OpenSavestateZipEntryByIndex(zip_t* zf, s64 index)
{
	return zip_fopen_index_managed(zf, index, 0);
}

static bool AddSavestateVersionEntry(zip_t* zf, const SavestateCompressionSettings& compression)
{
	SavestateVersionIndicator* const version =
		static_cast<SavestateVersionIndicator*>(std::malloc(sizeof(SavestateVersionIndicator)));
	if (!version)
		return false;

	*version = MakeSavestateVersionIndicator();
	return AddCompressedBufferToZip(zf, EntryFilename_StateVersion, version, sizeof(*version), compression, true);
}

static std::optional<SavestateVersionIndicator> ReadSavestateVersionIndicator(zip_t* zf)
{
	SavestateVersionIndicator version = {};
	auto zff = OpenSavestateZipEntryByName(zf, EntryFilename_StateVersion);
	if (!zff || zip_fread(zff.get(), &version, sizeof(version)) != sizeof(version))
		return std::nullopt;

	version.version[STATE_PCSX2_VERSION_SIZE - 1] = 0;
	return version;
}

static bool IsSavestateVersionCompatible(const SavestateVersionIndicator& version, Error* error)
{
	if (version.save_version <= g_SaveVersion && (version.save_version >> 16) == (g_SaveVersion >> 16))
		return true;

	std::string current_emulator_version = BuildVersion::GitTag;
	if (current_emulator_version.empty())
		current_emulator_version = "Unknown";

	Error::SetString(error, fmt::format(TRANSLATE_FS("SaveState","This save state was created with PCSX2 version {0}. It is no longer compatible "
										"with your current PCSX2 version {1}.\n\n"
										"If you have any unsaved progress on this save state, you can download the compatible PCSX2 version {0} "
										"from pcsx2.net, load the save state, and save your progress to the memory card."),
										version.version, current_emulator_version));
	return false;
}

static std::optional<std::vector<u8>> ReadZipEntryPayloadByIndex(zip_t* zf, s64 index)
{
	zip_stat_t zst;
	if (zip_stat_index(zf, index, 0, &zst) != 0 || zst.size > std::numeric_limits<int>::max())
		return std::nullopt;

	auto zff = OpenSavestateZipEntryByIndex(zf, index);
	if (!zff)
		return std::nullopt;

	std::vector<u8> data(zst.size);
	if (zip_fread(zff.get(), data.data(), data.size()) != static_cast<zip_int64_t>(data.size()))
		return std::nullopt;

	return data;
}

static bool LoadInternalStateFromPayload(const std::vector<u8>& payload, Error* error)
{
	memLoadingState state(payload);
	if (!state.FreezeBios())
		return false;

	return state.FreezeInternals(error);
}

static void DecodeSavestateScreenshotRgbRow(const u8* row_ptr, u32 width, u32* out_ptr)
{
	for (u32 x = 0; x < width; x++)
	{
		u32 pixel = static_cast<u32>(*(row_ptr)++);
		pixel |= static_cast<u32>(*(row_ptr)++) << 8;
		pixel |= static_cast<u32>(*(row_ptr)++) << 16;
		pixel |= static_cast<u32>(*(row_ptr)++) << 24;
		*(out_ptr++) = pixel | 0xFF000000u;
	}
}

static void DecodeSavestateScreenshotRgbaRow(const u8* row_ptr, u32 width, u32* out_ptr)
{
	for (u32 x = 0; x < width; x++)
	{
		u32 pixel;
		std::memcpy(&pixel, row_ptr, sizeof(u32));
		row_ptr += sizeof(u32);
		*(out_ptr++) = pixel | 0xFF000000u;
	}
}

static void InitializeSavestateScreenshotReader(png_structp png_ptr, zip_file_t* zff)
{
	png_set_read_fn(png_ptr, zff, [](png_structp png_ptr, png_bytep data_ptr, png_size_t size) {
		zip_fread(static_cast<zip_file_t*>(png_get_io_ptr(png_ptr)), data_ptr, size);
	});
}

static bool ReadSavestateScreenshotHeader(png_structp png_ptr, png_infop info_ptr,
	png_uint_32* width, png_uint_32* height, int* colorType)
{
	int bitDepth = 0;
	return (png_get_IHDR(png_ptr, info_ptr, width, height, &bitDepth, colorType, nullptr, nullptr, nullptr) == 1 &&
		*width != 0 && *height != 0);
}

static void DecodeSavestateScreenshotRows(png_structp png_ptr, png_infop info_ptr, u32 width, u32 height,
	int colorType, std::vector<u32>* out_pixels)
{
	const png_uint_32 bytesPerRow = png_get_rowbytes(png_ptr, info_ptr);
	std::vector<u8> rowData(bytesPerRow);
	out_pixels->resize(width * height);

	for (u32 y = 0; y < height; y++)
	{
		png_read_row(png_ptr, static_cast<png_bytep>(rowData.data()), nullptr);

		const u8* row_ptr = rowData.data();
		u32* out_ptr = &out_pixels->at(y * width);
		if (colorType == PNG_COLOR_TYPE_RGB)
		{
			DecodeSavestateScreenshotRgbRow(row_ptr, width, out_ptr);
		}
		else if (colorType == PNG_COLOR_TYPE_RGBA)
		{
			DecodeSavestateScreenshotRgbaRow(row_ptr, width, out_ptr);
		}
	}
}

static bool SaveState_ReadScreenshot(zip_t* zf, u32* out_width, u32* out_height, std::vector<u32>* out_pixels)
{
	auto zff = OpenSavestateZipEntryByName(zf, EntryFilename_Screenshot);
	if (!zff)
		return false;

	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (!png_ptr)
		return false;

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		png_destroy_read_struct(&png_ptr, nullptr, nullptr);
		return false;
	}

	ScopedGuard cleanup([&png_ptr, &info_ptr]() {
		png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
	});

	if (setjmp(png_jmpbuf(png_ptr)))
		return false;

	InitializeSavestateScreenshotReader(png_ptr, zff.get());
	png_read_info(png_ptr, info_ptr);

	png_uint_32 width = 0;
	png_uint_32 height = 0;
	int colorType = -1;
	if (!ReadSavestateScreenshotHeader(png_ptr, info_ptr, &width, &height, &colorType))
	{
		return false;
	}

	*out_width = width;
	*out_height = height;
	DecodeSavestateScreenshotRows(png_ptr, info_ptr, width, height, colorType, out_pixels);
	return true;
}

static bool ReadSavestateScreenshotData(zip_t* zf, SaveStateScreenshotData* out_data)
{
	return SaveState_ReadScreenshot(zf, &out_data->width, &out_data->height, &out_data->pixels);
}

// --------------------------------------------------------------------------------------
//  CompressThread_VmState
// --------------------------------------------------------------------------------------
static bool AddSavestateArchiveEntriesToZip(zip_t* zf, ArchiveEntryList* srclist,
	const SavestateCompressionSettings& compression)
{
	const uint listlen = srclist->GetLength();
	for (uint i = 0; i < listlen; ++i)
	{
		const ArchiveEntry& entry = (*srclist)[i];
		if (!entry.GetDataSize())
			continue;

		if (!AddCompressedBufferToZip(zf, entry.GetFilename().c_str(), srclist->GetPtr(entry.GetDataIndex()),
			entry.GetDataSize(), compression, false))
		{
			return false;
		}
	}
	return true;
}

static bool SaveSavestateScreenshotToZip(zip_t* zf, SaveStateScreenshotData* screenshot)
{
	return !screenshot || SaveState_CompressScreenshot(screenshot, zf);
}

static bool SaveState_AddToZip(zip_t* zf, ArchiveEntryList* srclist, SaveStateScreenshotData* screenshot)
{
	const SavestateCompressionSettings compression = GetSavestateCompressionSettings();
	if (!AddSavestateVersionEntry(zf, compression))
		return false;

	if (!AddSavestateArchiveEntriesToZip(zf, srclist, compression))
		return false;

	return SaveSavestateScreenshotToZip(zf, screenshot);
}

static bool FinalizeSavestateZipWrite(zip_t* zf, const char* filename, Error* error)
{
	if (zip_close(zf) == 0)
		return true;

	Error::SetStringFmt(error,
		TRANSLATE_FS("SaveState", "Failed to save state to zip file '{}': {}."), filename, zip_strerror(zf));
	zip_discard(zf);
	return false;
}

static bool WriteSavestateArchiveToDisk(zip_t* zf, ArchiveEntryList* srclist, SaveStateScreenshotData* screenshot,
	const char* filename, Error* error)
{
	if (!SaveState_AddToZip(zf, srclist, screenshot))
	{
		Error::SetStringFmt(error,
			TRANSLATE_FS("SaveState", "Failed to save state to zip file '{}'."), filename);
		zip_discard(zf);
		return false;
	}

	return FinalizeSavestateZipWrite(zf, filename, error);
}

static bool SaveSavestateArchiveFile(std::unique_ptr<ArchiveEntryList>& srclist,
	std::unique_ptr<SaveStateScreenshotData>& screenshot, const char* filename, Error* error)
{
	zip_t* const zf = OpenSavestateZipForWrite(filename, error);
	if (!zf)
		return false;

	return WriteSavestateArchiveToDisk(zf, srclist.get(), screenshot.get(), filename, error);
}

bool SaveState_ZipToDisk(
	std::unique_ptr<ArchiveEntryList> srclist, std::unique_ptr<SaveStateScreenshotData> screenshot,
	const char* filename, Error* error)
{
	return SaveSavestateArchiveFile(srclist, screenshot, filename, error);
}

static bool ReadSavestateScreenshotArchive(const std::string& filename, SaveStateScreenshotData* out_data)
{
	auto zf = OpenSavestateZipForRead(filename.c_str(), "save state screenshot");
	if (!zf)
		return false;

	return ReadSavestateScreenshotData(zf.get(), out_data);
}

bool SaveState_ReadScreenshot(const std::string& filename, u32* out_width, u32* out_height, std::vector<u32>* out_pixels)
{
	SaveStateScreenshotData data;
	if (!ReadSavestateScreenshotArchive(filename, &data))
		return false;

	*out_width = data.width;
	*out_height = data.height;
	*out_pixels = std::move(data.pixels);
	return true;
}

static bool ValidateSavestateVersion(zip_t* zf, Error* error)
{
	const std::optional<SavestateVersionIndicator> version = ReadSavestateVersionIndicator(zf);
	if (!version.has_value())
	{
		Error::SetString(error, "Savestate file does not contain version indicator.");
		return false;
	}
	return IsSavestateVersionCompatible(*version, error);
}

static zip_int64_t FindSavestateArchiveEntryIndex(zip_t* zf, const char* name, bool required)
{
	zip_int64_t index = zip_name_locate(zf, name, /*ZIP_FL_NOCASE*/ 0);
	if (index >= 0)
	{
		DevCon.WriteLn(Color_Green, " ... found '%s'", name);
		return index;
	}

	if (required)
		Console.WriteLn(Color_Red, " ... not found '%s'!", name);
	else
		DevCon.WriteLn(Color_Red, " ... not found '%s'!", name);

	return index;
}

static bool LoadInternalStructuresState(zip_t* zf, s64 index, Error* error)
{
	const std::optional<std::vector<u8>> buffer = ReadZipEntryPayloadByIndex(zf, index);
	if (!buffer.has_value())
		return false;

	return LoadInternalStateFromPayload(*buffer, error);
}

static bool LookupSavestateArchiveEntryIndices(zip_t* zf, s64* out_internal_index,
	std::array<s64, std::size(SavestateEntries)>* out_entry_indices)
{
	*out_internal_index = FindSavestateArchiveEntryIndex(zf, EntryFilename_InternalStructures, true);
	bool all_present = (*out_internal_index >= 0);

	for (u32 i = 0; i < std::size(SavestateEntries); i++)
	{
		const bool required = SavestateEntries[i]->IsRequired();
		(*out_entry_indices)[i] = FindSavestateArchiveEntryIndex(zf, SavestateEntries[i]->GetFilename(), required);
		if ((*out_entry_indices)[i] < 0 && required)
			all_present = false;
	}

	return all_present;
}

static bool ValidateSavestateArchive(zip_t* zf, s64* out_internal_index, std::array<s64, std::size(SavestateEntries)>* out_entry_indices,
	Error* error)
{
	if (!LookupSavestateArchiveEntryIndices(zf, out_internal_index, out_entry_indices))
	{
		Error::SetString(error, "Some required components were not found or are incomplete.");
		return false;
	}

	return true;
}

static bool LoadSavestateArchiveEntry(zip_t* zf, const BaseSavestateEntry& entry, s64 index, Error* error)
{
	if (index < 0)
		return entry.FreezeIn(nullptr);

	auto zff = OpenSavestateZipEntryByIndex(zf, index);
	if (zff && entry.FreezeIn(zff.get()))
		return true;

	Error::SetString(error, fmt::format("Save state corruption in {}.", entry.GetFilename()));
	return false;
}

static bool LoadSavestateArchiveEntries(zip_t* zf, const std::array<s64, std::size(SavestateEntries)>& entry_indices, Error* error)
{
	for (u32 i = 0; i < std::size(SavestateEntries); ++i)
	{
		if (!LoadSavestateArchiveEntry(zf, *SavestateEntries[i], entry_indices[i], error))
			return false;
	}

	return true;
}

static bool FailSavestateLoad(Error* error, const char* default_message);

static bool RunSavestatePayloadLoadPhase(zip_t* zf, s64 internal_index,
	const std::array<s64, std::size(SavestateEntries)>& entry_indices, Error* error)
{
	if (!LoadInternalStructuresState(zf, internal_index, error))
		return FailSavestateLoad(error, "Save state corruption in internal structures.");

	if (!LoadSavestateArchiveEntries(zf, entry_indices, error))
		return FailSavestateLoad(error, "Save state corruption in component entries.");

	return true;
}

static bool FinalizeSavestateLoad()
{
	PostLoadPrep();
	return true;
}

static void ResetVmAfterFailedStateLoad()
{
	VMManager::Reset();
}

static bool FailSavestateLoad(Error* error, const char* default_message)
{
	if (!error->IsValid())
		Error::SetString(error, default_message);

	ResetVmAfterFailedStateLoad();
	return false;
}

static bool RunSavestateLoadLifecycle(zip_t* zf, s64 internal_index,
	const std::array<s64, std::size(SavestateEntries)>& entry_indices, Error* error)
{
	PreLoadPrep();

	if (!RunSavestatePayloadLoadPhase(zf, internal_index, entry_indices, error))
		return false;

	return FinalizeSavestateLoad();
}

static bool ValidateAndIndexSavestateArchive(zip_t* zf, s64* out_internal_index,
	std::array<s64, std::size(SavestateEntries)>* out_entry_indices, Error* error)
{
	if (!ValidateSavestateVersion(zf, error))
		return false;

	return ValidateSavestateArchive(zf, out_internal_index, out_entry_indices, error);
}

bool SaveState_UnzipFromDisk(const std::string& filename, Error* error)
{
	auto zf = OpenSavestateZipForRead(filename.c_str(), "save state load", error);
	if (!zf)
		return false;

	s64 internal_index = -1;
	std::array<s64, std::size(SavestateEntries)> entry_indices = {};
	if (!ValidateAndIndexSavestateArchive(zf.get(), &internal_index, &entry_indices, error))
		return false;

	return RunSavestateLoadLifecycle(zf.get(), internal_index, entry_indices, error);
}

static std::string FormatSavestateLoadErrorMessage(const std::string& message, std::optional<s32> slot, bool backup)
{
	if (slot.has_value())
	{
		if (backup)
			return fmt::format(
				TRANSLATE_FS("SaveState", "Failed to load state from backup slot {}: {}"), *slot, message);

		return fmt::format(
			TRANSLATE_FS("SaveState", "Failed to load state from slot {}: {}"), *slot, message);
	}

	return fmt::format(TRANSLATE_FS("SaveState", "Failed to load state: {}"), message);
}

static std::string FormatSavestateSaveErrorMessage(const std::string& message, std::optional<s32> slot)
{
	if (slot.has_value())
	{
		return fmt::format(
			TRANSLATE_FS("SaveState", "Failed to save state to slot {}: {}"), *slot, message);
	}

	return fmt::format(TRANSLATE_FS("SaveState", "Failed to save state: {}"), message);
}

static void PostSavestateOsdMessage(const char* key, const std::string& message)
{
	Host::AddIconOSDMessage(key, ICON_FA_TRIANGLE_EXCLAMATION, message, Host::OSD_WARNING_DURATION);
}

void SaveState_ReportLoadErrorOSD(const std::string& message, std::optional<s32> slot, bool backup)
{
	const std::string full_message = FormatSavestateLoadErrorMessage(message, slot, backup);

	PostSavestateOsdMessage("LoadState", full_message);
}

void SaveState_ReportSaveErrorOSD(const std::string& message, std::optional<s32> slot)
{
	const std::string full_message = FormatSavestateSaveErrorMessage(message, slot);

	PostSavestateOsdMessage("SaveState", full_message);
}
