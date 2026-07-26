// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Common/GSGPUProfilePrivate.h"

#include <array>
#include <cctype>

namespace GpuProfileDetail
{
namespace
{
struct PowerVRSpec
{
	const char* token;
	MobileGpuArchitecture architecture;
	u16 model;
	MobileGsTuning tuning;
};

static const std::array<PowerVRSpec, 37> s_powervr_specs = {{
	{"sgx530", MobileGpuArchitecture::PowerVRSeries5, 530, MakeMobileGsTuning(40, 4, 40, 4)},
	{"sgx531", MobileGpuArchitecture::PowerVRSeries5, 531, MakeMobileGsTuning(40, 4, 40, 4)},
	{"sgx535", MobileGpuArchitecture::PowerVRSeries5, 535, MakeMobileGsTuning(44, 4, 44, 4)},
	{"sgx540", MobileGpuArchitecture::PowerVRSeries5, 540, MakeMobileGsTuning(44, 4, 44, 4)},
	{"sgx543", MobileGpuArchitecture::PowerVRSeries5, 543, MakeMobileGsTuning(48, 4, 48, 4)},
	{"sgx544", MobileGpuArchitecture::PowerVRSeries5, 544, MakeMobileGsTuning(52, 4, 52, 4)},
	{"sgx545", MobileGpuArchitecture::PowerVRSeries5, 545, MakeMobileGsTuning(52, 4, 52, 4)},
	{"g6200", MobileGpuArchitecture::PowerVRRogue, 6200, MakeMobileGsTuning(72, 6, 72, 5)},
	{"g6230", MobileGpuArchitecture::PowerVRRogue, 6230, MakeMobileGsTuning(76, 6, 76, 5)},
	{"g6400", MobileGpuArchitecture::PowerVRRogue, 6400, MakeMobileGsTuning(80, 6, 80, 6)},
	{"g6430", MobileGpuArchitecture::PowerVRRogue, 6430, MakeMobileGsTuning(88, 7, 88, 6)},
	{"g6630", MobileGpuArchitecture::PowerVRRogue, 6630, MakeMobileGsTuning(96, 7, 96, 6)},
	{"g6110", MobileGpuArchitecture::PowerVRRogue, 6110, MakeMobileGsTuning(64, 5, 64, 5)},
	{"ge6250", MobileGpuArchitecture::PowerVRRogue, 6250, MakeMobileGsTuning(80, 6, 80, 6)},
	{"gx6250", MobileGpuArchitecture::PowerVRRogue, 6250, MakeMobileGsTuning(84, 7, 84, 6)},
	{"gx6450", MobileGpuArchitecture::PowerVRRogue, 6450, MakeMobileGsTuning(96, 7, 96, 6)},
	{"gx6650", MobileGpuArchitecture::PowerVRRogue, 6650, MakeMobileGsTuning(104, 8, 104, 7)},
	{"gr6500", MobileGpuArchitecture::PowerVRRogue, 6500, MakeMobileGsTuning(100, 8, 100, 7)},
	{"gt7200", MobileGpuArchitecture::PowerVRRogue, 7200, MakeMobileGsTuning(80, 6, 80, 6)},
	{"gt7400", MobileGpuArchitecture::PowerVRRogue, 7400, MakeMobileGsTuning(88, 7, 88, 6)},
	{"gt7600", MobileGpuArchitecture::PowerVRRogue, 7600, MakeMobileGsTuning(104, 8, 104, 7)},
	{"gt7800", MobileGpuArchitecture::PowerVRRogue, 7800, MakeMobileGsTuning(120, 9, 120, 7)},
	{"ge8100", MobileGpuArchitecture::PowerVRRogue, 8100, MakeMobileGsTuning(56, 5, 56, 4)},
	{"ge8300", MobileGpuArchitecture::PowerVRRogue, 8300, MakeMobileGsTuning(60, 5, 60, 5)},
	{"ge8310", MobileGpuArchitecture::PowerVRRogue, 8310, MakeMobileGsTuning(62, 5, 62, 5)},
	{"ge8320", MobileGpuArchitecture::PowerVRRogue, 8320, MakeMobileGsTuning(64, 5, 64, 5)},
	{"ge8322", MobileGpuArchitecture::PowerVRRogue, 8322, MakeMobileGsTuning(66, 5, 66, 5)},
	{"ge9215", MobileGpuArchitecture::PowerVRRogue, 9215, MakeMobileGsTuning(68, 5, 68, 5)},
	{"ge9216", MobileGpuArchitecture::PowerVRRogue, 9216, MakeMobileGsTuning(72, 6, 72, 5)},
	{"ge9230", MobileGpuArchitecture::PowerVRRogue, 9230, MakeMobileGsTuning(76, 6, 76, 5)},
	{"ge9300", MobileGpuArchitecture::PowerVRRogue, 9300, MakeMobileGsTuning(80, 6, 80, 6)},
	{"ge9310", MobileGpuArchitecture::PowerVRRogue, 9310, MakeMobileGsTuning(84, 7, 84, 6)},
	{"ge9420", MobileGpuArchitecture::PowerVRRogue, 9420, MakeMobileGsTuning(96, 7, 96, 6)},
	{"gm9246", MobileGpuArchitecture::PowerVRRogue, 9246, MakeMobileGsTuning(104, 8, 104, 7)},
	{"gm9445", MobileGpuArchitecture::PowerVRRogue, 9445, MakeMobileGsTuning(110, 8, 110, 7)},
	{"gm9446", MobileGpuArchitecture::PowerVRRogue, 9446, MakeMobileGsTuning(112, 8, 112, 7)},
	{"gm9624", MobileGpuArchitecture::PowerVRRogue, 9624, MakeMobileGsTuning(120, 9, 120, 7)},
}};

static const PowerVRSpec* FindPowerVRSpec(std::string_view lowered_hints)
{
	for (const PowerVRSpec& spec : s_powervr_specs)
	{
		if (lowered_hints.find(spec.token) != std::string_view::npos)
			return &spec;
	}
	return nullptr;
}

static bool ContainsPowerVRToken(std::string_view hints, std::string_view token)
{
	const size_t pos = hints.find(token);
	if (pos == std::string_view::npos)
		return false;
	const size_t end = pos + token.size();
	return (pos == 0 || !std::isalnum(static_cast<unsigned char>(hints[pos - 1]))) &&
		(end == hints.size() || !std::isalnum(static_cast<unsigned char>(hints[end])));
}
} // namespace

bool LooksLikePowerVR(std::string_view lowered_hints)
{
	// "img" is far too broad for a substring search (it appears in unrelated model/property values).
	return ContainsAny(lowered_hints, {"imagination technologies", "imgtec", "powervr"});
}

ResolvedGpuProfile ResolvePowerVRProfile(std::string_view lowered_hints)
{
	ResolvedGpuProfile resolved;
	resolved.gpu.architecture = MobileGpuArchitecture::PowerVR;
	resolved.gpu.name = "PowerVR";
	resolved.tuning = MakeMobileGsTuning(72, 6, 72, 5);

	if (const PowerVRSpec* spec = FindPowerVRSpec(lowered_hints))
	{
		resolved.gpu.architecture = spec->architecture;
		resolved.gpu.model_number = spec->model;
		resolved.gpu.recognized = true;
		resolved.gpu.name = "PowerVR " + std::string(spec->token);
		for (char& ch : resolved.gpu.name)
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		resolved.gpu.name.replace(0, 7, "PowerVR");
		resolved.tuning = spec->tuning;
	}
	else if (ContainsAny(lowered_hints, {"d-series", "dxt", "dmtp", "dxs", "dxd"}))
	{
		resolved.gpu.architecture = MobileGpuArchitecture::PowerVRVolcanic;
		resolved.gpu.recognized = true;
		resolved.gpu.name = "PowerVR D-Series";
		resolved.tuning = MakeMobileGsTuning(144, 10, 144, 8, true);
	}
	else if (ContainsAny(lowered_hints, {"c-series", "cxt", "cxtp", "cxm"}))
	{
		resolved.gpu.architecture = MobileGpuArchitecture::PowerVRVolcanic;
		resolved.gpu.recognized = true;
		resolved.gpu.name = "PowerVR C-Series";
		resolved.tuning = MakeMobileGsTuning(128, 9, 128, 7, true);
	}
	else if (ContainsAny(lowered_hints, {"b-series", "bxs", "bxe", "bxm", "bxt"}))
	{
		// Series B product names can be licensed from different architecture generations.
		// Do not invent Rogue/Volcanic without a capability signal.
		resolved.gpu.architecture = MobileGpuArchitecture::PowerVR;
		resolved.gpu.recognized = true;
		resolved.gpu.name = "PowerVR B-Series";
		resolved.tuning = MakeMobileGsTuning(112, 8, 112, 7);
	}
	else if (ContainsAny(lowered_hints, {"a-series", "axs", "axe", "axm", "axt"}))
	{
		resolved.gpu.architecture = MobileGpuArchitecture::PowerVR;
		resolved.gpu.recognized = true;
		resolved.gpu.name = "PowerVR A-Series";
		resolved.tuning = MakeMobileGsTuning(96, 7, 96, 6);
	}
	else if (ContainsAny(lowered_hints, {"rogue", "series6", "series7", "series8", "series9"}) ||
		ContainsPowerVRToken(lowered_hints, "sgx"))
	{
		resolved.gpu.architecture = ContainsPowerVRToken(lowered_hints, "sgx") ?
			MobileGpuArchitecture::PowerVRSeries5 : MobileGpuArchitecture::PowerVRRogue;
		resolved.gpu.name = ContainsPowerVRToken(lowered_hints, "sgx") ?
			"PowerVR SGX" : "PowerVR Rogue";
	}
	return resolved;
}
} // namespace GpuProfileDetail
