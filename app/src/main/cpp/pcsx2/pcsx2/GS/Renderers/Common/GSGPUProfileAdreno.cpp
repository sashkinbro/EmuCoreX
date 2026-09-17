// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Common/GSGPUProfilePrivate.h"

#include <array>
#include <cctype>

namespace GpuProfileDetail
{
namespace
{
struct AdrenoSpec
{
	u16 model;
	char suffix;
	MobileGpuArchitecture architecture;
};

// Renderer names are stable (for example "Adreno (TM) 740"), while Snapdragon product names are not.
// Keep one entry per shipping renderer model so adjacent parts are not silently treated as equivalent.
static constexpr std::array<AdrenoSpec, 57> s_adreno_specs = {{
	{200, 0, MobileGpuArchitecture::Adreno2xx},
	{203, 0, MobileGpuArchitecture::Adreno2xx},
	{205, 0, MobileGpuArchitecture::Adreno2xx},
	{220, 0, MobileGpuArchitecture::Adreno2xx},
	{225, 0, MobileGpuArchitecture::Adreno2xx},
	{302, 0, MobileGpuArchitecture::Adreno3xx},
	{303, 0, MobileGpuArchitecture::Adreno3xx},
	{304, 0, MobileGpuArchitecture::Adreno3xx},
	{305, 0, MobileGpuArchitecture::Adreno3xx},
	{306, 0, MobileGpuArchitecture::Adreno3xx},
	{308, 0, MobileGpuArchitecture::Adreno3xx},
	{320, 0, MobileGpuArchitecture::Adreno3xx},
	{330, 0, MobileGpuArchitecture::Adreno3xx},
	{405, 0, MobileGpuArchitecture::Adreno4xx},
	{418, 0, MobileGpuArchitecture::Adreno4xx},
	{420, 0, MobileGpuArchitecture::Adreno4xx},
	{430, 0, MobileGpuArchitecture::Adreno4xx},
	{504, 0, MobileGpuArchitecture::Adreno5xx},
	{505, 0, MobileGpuArchitecture::Adreno5xx},
	{506, 0, MobileGpuArchitecture::Adreno5xx},
	{507, 0, MobileGpuArchitecture::Adreno5xx},
	{508, 0, MobileGpuArchitecture::Adreno5xx},
	{509, 0, MobileGpuArchitecture::Adreno5xx},
	{510, 0, MobileGpuArchitecture::Adreno5xx},
	{512, 0, MobileGpuArchitecture::Adreno5xx},
	{530, 0, MobileGpuArchitecture::Adreno5xx},
	{540, 0, MobileGpuArchitecture::Adreno5xx},
	{605, 0, MobileGpuArchitecture::Adreno6xx},
	{608, 0, MobileGpuArchitecture::Adreno6xx},
	{609, 0, MobileGpuArchitecture::Adreno6xx},
	{610, 0, MobileGpuArchitecture::Adreno6xx},
	{610, 'l', MobileGpuArchitecture::Adreno6xx},
	{612, 0, MobileGpuArchitecture::Adreno6xx},
	{613, 0, MobileGpuArchitecture::Adreno6xx},
	{615, 0, MobileGpuArchitecture::Adreno6xx},
	{616, 0, MobileGpuArchitecture::Adreno6xx},
	{618, 0, MobileGpuArchitecture::Adreno6xx},
	{619, 'l', MobileGpuArchitecture::Adreno6xx},
	{619, 0, MobileGpuArchitecture::Adreno6xx},
	{620, 0, MobileGpuArchitecture::Adreno6xx},
	{630, 0, MobileGpuArchitecture::Adreno6xx},
	{640, 0, MobileGpuArchitecture::Adreno6xx},
	{642, 'l', MobileGpuArchitecture::Adreno6xx},
	{642, 0, MobileGpuArchitecture::Adreno6xx},
	{643, 0, MobileGpuArchitecture::Adreno6xx},
	{644, 0, MobileGpuArchitecture::Adreno6xx},
	{650, 0, MobileGpuArchitecture::Adreno6xx},
	{660, 0, MobileGpuArchitecture::Adreno6xx},
	{663, 0, MobileGpuArchitecture::Adreno6xx},
	{675, 0, MobileGpuArchitecture::Adreno6xx},
	{680, 0, MobileGpuArchitecture::Adreno6xx},
	{685, 0, MobileGpuArchitecture::Adreno6xx},
	{690, 0, MobileGpuArchitecture::Adreno6xx},
	{695, 0, MobileGpuArchitecture::Adreno6xx},
	{702, 0, MobileGpuArchitecture::Adreno7xx},
	{710, 0, MobileGpuArchitecture::Adreno7xx},
	{720, 0, MobileGpuArchitecture::Adreno7xx},
}};

// Later 7xx/8xx models are kept separate because they use materially different renderer generations,
// even though their current GS pool ceiling is the same.
static constexpr std::array<AdrenoSpec, 20> s_recent_adreno_specs = {{
	{722, 0, MobileGpuArchitecture::Adreno7xx},
	{725, 0, MobileGpuArchitecture::Adreno7xx},
	{730, 0, MobileGpuArchitecture::Adreno7xx},
	{732, 0, MobileGpuArchitecture::Adreno7xx},
	{735, 0, MobileGpuArchitecture::Adreno7xx},
	{740, 0, MobileGpuArchitecture::Adreno7xx},
	{750, 0, MobileGpuArchitecture::Adreno7xx},
	{760, 0, MobileGpuArchitecture::Adreno7xx},
	{765, 0, MobileGpuArchitecture::Adreno7xx},
	{775, 0, MobileGpuArchitecture::Adreno7xx},
	{810, 0, MobileGpuArchitecture::Adreno8xx},
	{820, 0, MobileGpuArchitecture::Adreno8xx},
	{825, 0, MobileGpuArchitecture::Adreno8xx},
	{829, 0, MobileGpuArchitecture::Adreno8xx},
	{830, 0, MobileGpuArchitecture::Adreno8xx},
	{840, 0, MobileGpuArchitecture::Adreno8xx},
	{845, 0, MobileGpuArchitecture::Adreno8xx},
	{850, 0, MobileGpuArchitecture::Adreno8xx},
	{860, 0, MobileGpuArchitecture::Adreno8xx},
	{870, 0, MobileGpuArchitecture::Adreno8xx},
}};

static bool ParseAdrenoModel(std::string_view hints, u16* model, char* suffix)
{
	const size_t adreno = hints.find("adreno");
	if (adreno == std::string_view::npos)
		return false;

	size_t pos = adreno + 6;
	while (pos < hints.size() && !std::isdigit(static_cast<unsigned char>(hints[pos])))
		pos++;
	if (pos == hints.size())
		return false;

	u32 value = 0;
	while (pos < hints.size() && std::isdigit(static_cast<unsigned char>(hints[pos])))
	{
		value = value * 10 + static_cast<u32>(hints[pos++] - '0');
		if (value > 9999)
			return false;
	}

	const char parsed_suffix = (pos < hints.size() && hints[pos] == 'l') ? 'l' : 0;
	if (parsed_suffix)
		pos++;
	if (pos < hints.size() && std::isalnum(static_cast<unsigned char>(hints[pos])))
		return false;

	*model = static_cast<u16>(value);
	*suffix = parsed_suffix;
	return true;
}

static bool ContainsAdrenoXToken(std::string_view hints, std::string_view token)
{
	for (size_t pos = hints.find(token); pos != std::string_view::npos;
		pos = hints.find(token, pos + token.size()))
	{
		const size_t end = pos + token.size();
		if ((pos == 0 || !std::isalnum(static_cast<unsigned char>(hints[pos - 1]))) &&
			(end == hints.size() || !std::isalnum(static_cast<unsigned char>(hints[end]))))
		{
			return true;
		}
	}
	return false;
}

static MobileGpuArchitecture ArchitectureForUnknownAdreno(u16 model)
{
	switch (model / 100)
	{
		case 2: return MobileGpuArchitecture::Adreno2xx;
		case 3: return MobileGpuArchitecture::Adreno3xx;
		case 4: return MobileGpuArchitecture::Adreno4xx;
		case 5: return MobileGpuArchitecture::Adreno5xx;
		case 6: return MobileGpuArchitecture::Adreno6xx;
		case 7: return MobileGpuArchitecture::Adreno7xx;
		case 8: return MobileGpuArchitecture::Adreno8xx;
		default: return MobileGpuArchitecture::Unknown;
	}
}

template <size_t N>
static const AdrenoSpec* FindAdrenoSpec(const std::array<AdrenoSpec, N>& specs, u16 model, char suffix)
{
	for (const AdrenoSpec& spec : specs)
	{
		if (spec.model == model && spec.suffix == suffix)
			return &spec;
	}
	return nullptr;
}
} // namespace

bool LooksLikeAdreno(std::string_view lowered_hints)
{
	return ContainsAny(lowered_hints, {"adreno", "qualcomm", "qcom", "snapdragon"});
}

ResolvedGpuProfile ResolveAdrenoProfile(std::string_view lowered_hints)
{
	ResolvedGpuProfile resolved;
	resolved.gpu.name = "Unknown Adreno";

	// Snapdragon X laptop parts occasionally appear through shared Android/ANGLE code paths.
	if (ContainsAdrenoXToken(lowered_hints, "adreno x2-85") ||
		ContainsAdrenoXToken(lowered_hints, "adreno x2 85"))
	{
		resolved.gpu = {MobileGpuArchitecture::AdrenoX, 285, 0, true, "Adreno X2-85"};
		return resolved;
	}
	if (ContainsAdrenoXToken(lowered_hints, "adreno x2-45") ||
		ContainsAdrenoXToken(lowered_hints, "adreno x2 45"))
	{
		resolved.gpu = {MobileGpuArchitecture::AdrenoX, 245, 0, true, "Adreno X2-45"};
		return resolved;
	}
	if (ContainsAdrenoXToken(lowered_hints, "adreno x1-85") ||
		ContainsAdrenoXToken(lowered_hints, "adreno x1 85"))
	{
		resolved.gpu = {MobileGpuArchitecture::AdrenoX, 185, 0, true, "Adreno X1-85"};
		return resolved;
	}
	if (ContainsAdrenoXToken(lowered_hints, "adreno x1-45") ||
		ContainsAdrenoXToken(lowered_hints, "adreno x1 45"))
	{
		resolved.gpu = {MobileGpuArchitecture::AdrenoX, 145, 0, true, "Adreno X1-45"};
		return resolved;
	}

	u16 model = 0;
	char suffix = 0;
	if (!ParseAdrenoModel(lowered_hints, &model, &suffix))
		return resolved;

	const AdrenoSpec* spec = FindAdrenoSpec(s_adreno_specs, model, suffix);
	if (!spec)
		spec = FindAdrenoSpec(s_recent_adreno_specs, model, suffix);

	resolved.gpu.model_number = model;
	resolved.gpu.architecture = spec ? spec->architecture : ArchitectureForUnknownAdreno(model);
	resolved.gpu.recognized = (spec != nullptr);
	resolved.gpu.name = "Adreno " + std::to_string(model);
	if (suffix)
		resolved.gpu.name.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(suffix))));
	return resolved;
}
} // namespace GpuProfileDetail
