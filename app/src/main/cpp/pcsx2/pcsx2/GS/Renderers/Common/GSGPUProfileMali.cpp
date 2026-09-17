// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Common/GSGPUProfilePrivate.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace GpuProfileDetail
{
namespace
{
struct MaliSpec
{
	char series;
	u16 model;
	MobileGpuArchitecture architecture;
};

// Arm's public product families. Performance still depends heavily on MC/MP core count, which is applied below.
static constexpr std::array<MaliSpec, 40> s_mali_specs = {{
	{'U', 200, MobileGpuArchitecture::MaliUtgard},
	{'U', 300, MobileGpuArchitecture::MaliUtgard},
	{'U', 400, MobileGpuArchitecture::MaliUtgard},
	{'U', 450, MobileGpuArchitecture::MaliUtgard},
	{'U', 470, MobileGpuArchitecture::MaliUtgard},
	{'T', 600, MobileGpuArchitecture::MaliMidgard},
	{'T', 604, MobileGpuArchitecture::MaliMidgard},
	{'T', 620, MobileGpuArchitecture::MaliMidgard},
	{'T', 624, MobileGpuArchitecture::MaliMidgard},
	{'T', 628, MobileGpuArchitecture::MaliMidgard},
	{'T', 658, MobileGpuArchitecture::MaliMidgard},
	{'T', 678, MobileGpuArchitecture::MaliMidgard},
	{'T', 720, MobileGpuArchitecture::MaliMidgard},
	{'T', 760, MobileGpuArchitecture::MaliMidgard},
	{'T', 820, MobileGpuArchitecture::MaliMidgard},
	{'T', 830, MobileGpuArchitecture::MaliMidgard},
	{'T', 860, MobileGpuArchitecture::MaliMidgard},
	{'T', 880, MobileGpuArchitecture::MaliMidgard},
	{'G', 31, MobileGpuArchitecture::MaliBifrost},
	{'G', 51, MobileGpuArchitecture::MaliBifrost},
	{'G', 52, MobileGpuArchitecture::MaliBifrost},
	{'G', 71, MobileGpuArchitecture::MaliBifrost},
	{'G', 72, MobileGpuArchitecture::MaliBifrost},
	{'G', 76, MobileGpuArchitecture::MaliBifrost},
	{'G', 57, MobileGpuArchitecture::MaliValhall1},
	{'G', 68, MobileGpuArchitecture::MaliValhall1},
	{'G', 77, MobileGpuArchitecture::MaliValhall1},
	{'G', 78, MobileGpuArchitecture::MaliValhall1}, // G78AE shares the GS policy.
	{'G', 310, MobileGpuArchitecture::MaliValhall2},
	{'G', 510, MobileGpuArchitecture::MaliValhall2},
	{'G', 610, MobileGpuArchitecture::MaliValhall2},
	{'G', 710, MobileGpuArchitecture::MaliValhall2},
	{'G', 615, MobileGpuArchitecture::MaliValhall3},
	{'G', 715, MobileGpuArchitecture::MaliValhall3},
	{'G', 620, MobileGpuArchitecture::MaliFifthGen},
	{'G', 720, MobileGpuArchitecture::MaliFifthGen},
	{'G', 625, MobileGpuArchitecture::MaliFifthGen},
	{'G', 725, MobileGpuArchitecture::MaliFifthGen},
	{'G', 925, MobileGpuArchitecture::MaliFifthGen},
	{'G', 1, MobileGpuArchitecture::MaliG1},
}};

static bool ParseUnsigned(std::string_view text, size_t pos, u16* value, size_t* end)
{
	if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos])))
		return false;

	u32 parsed = 0;
	while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
	{
		parsed = parsed * 10 + static_cast<u32>(text[pos++] - '0');
		if (parsed > 9999)
			return false;
	}
	*value = static_cast<u16>(parsed);
	*end = pos;
	return true;
}

static bool ParseMaliModel(std::string_view hints, char* series, u16* model, bool* immortalis)
{
	const size_t immortalis_pos = hints.find("immortalis");
	*immortalis = (immortalis_pos != std::string_view::npos);

	const auto try_token = [&](size_t name_pos, size_t name_length) {
		size_t pos = name_pos + name_length;
		const size_t token_end = hints.find('|', pos);
		const size_t end_limit = (token_end == std::string_view::npos) ? hints.size() : token_end;

		while (pos < end_limit && hints[pos] != 'g' && hints[pos] != 't' &&
			!std::isdigit(static_cast<unsigned char>(hints[pos])))
		{
			pos++;
		}
		if (pos == end_limit)
			return false;

		char parsed_series = 'U';
		if (hints[pos] == 'g' || hints[pos] == 't')
			parsed_series = static_cast<char>(std::toupper(static_cast<unsigned char>(hints[pos++])));

		u16 parsed_model = 0;
		size_t parsed_end = pos;
		if (!ParseUnsigned(hints.substr(0, end_limit), pos, &parsed_model, &parsed_end))
			return false;

		if (parsed_end < end_limit &&
			std::isalnum(static_cast<unsigned char>(hints[parsed_end])))
		{
			const std::string_view suffix = hints.substr(parsed_end, end_limit - parsed_end);
			const bool valid_suffix =
				(suffix.starts_with("ae") &&
					(suffix.size() == 2 ||
						!std::isalnum(static_cast<unsigned char>(suffix[2])))) ||
				((suffix.starts_with("mc") || suffix.starts_with("mp")) &&
					suffix.size() > 2 &&
					std::isdigit(static_cast<unsigned char>(suffix[2])));
			if (!valid_suffix)
				return false;
		}

		*series = parsed_series;
		*model = parsed_model;
		return true;
	};

	// A vendor hint such as "ARM Mali" can appear before the actual renderer token. Do not
	// scan across the " | " separator, otherwise the 'g' in the next "gpu=" key is mistaken
	// for the model series and Mali-G57 becomes Unknown Mali.
	for (size_t pos = hints.find("mali"); pos != std::string_view::npos; pos = hints.find("mali", pos + 4))
	{
		if (try_token(pos, 4))
			return true;
	}

	return immortalis_pos != std::string_view::npos && try_token(immortalis_pos, 10);
}

static u8 ParseCoreCount(std::string_view hints)
{
	for (size_t pos = 0; pos + 2 < hints.size(); pos++)
	{
		if ((hints[pos] != 'm' || (hints[pos + 1] != 'c' && hints[pos + 1] != 'p')) ||
			(pos > 0 && std::isalnum(static_cast<unsigned char>(hints[pos - 1]))))
		{
			continue;
		}

		u16 value = 0;
		size_t end = pos + 2;
		if (ParseUnsigned(hints, pos + 2, &value, &end) && value <= 255)
			return static_cast<u8>(value);
	}
	return 0;
}

static u8 ParseMaliCoreCount(std::string_view hints)
{
	size_t segment_start = 0;
	while (segment_start < hints.size())
	{
		const size_t separator = hints.find('|', segment_start);
		const size_t segment_end =
			(separator == std::string_view::npos) ? hints.size() : separator;
		const std::string_view segment =
			hints.substr(segment_start, segment_end - segment_start);
		if (segment.find("mali") != std::string_view::npos ||
			segment.find("immortalis") != std::string_view::npos)
		{
			if (const u8 count = ParseCoreCount(segment); count != 0)
				return count;
		}
		if (separator == std::string_view::npos)
			break;
		segment_start = separator + 1;
	}
	return 0;
}

static const MaliSpec* FindMaliSpec(char series, u16 model)
{
	for (const MaliSpec& spec : s_mali_specs)
	{
		if (spec.series == series && spec.model == model)
			return &spec;
	}
	return nullptr;
}

static MobileGpuArchitecture ArchitectureForUnknownMali(char series, u16 model)
{
	if (series == 'U')
		return MobileGpuArchitecture::MaliUtgard;
	if (series == 'T')
		return MobileGpuArchitecture::MaliMidgard;
	if (series != 'G')
		return MobileGpuArchitecture::Unknown;
	if (model < 100)
		return (model == 57 || model == 68 || model == 77 || model == 78) ?
			MobileGpuArchitecture::MaliValhall1 : MobileGpuArchitecture::MaliBifrost;
	if (model < 600)
		return MobileGpuArchitecture::MaliValhall2;
	if (model < 620)
		return (model == 615) ? MobileGpuArchitecture::MaliValhall3 : MobileGpuArchitecture::MaliValhall2;
	if (model >= 900)
		return MobileGpuArchitecture::MaliFifthGen;
	return MobileGpuArchitecture::MaliFifthGen;
}

} // namespace

bool LooksLikeMali(std::string_view lowered_hints)
{
	// Do not equate MediaTek with Mali: older MediaTek parts shipped PowerVR, and the GL/Vulkan renderer
	// string is a more authoritative signal than the SoC vendor.
	return ContainsAny(lowered_hints, {"mali", "immortalis", "arm mali"});
}

ResolvedGpuProfile ResolveMaliProfile(std::string_view lowered_hints)
{
	ResolvedGpuProfile resolved;
	resolved.gpu.name = "Unknown Mali";

	char series = 0;
	u16 model = 0;
	bool immortalis = false;
	if (!ParseMaliModel(lowered_hints, &series, &model, &immortalis))
		return resolved;

	const MaliSpec* spec = FindMaliSpec(series, model);
	resolved.gpu.architecture = spec ? spec->architecture : ArchitectureForUnknownMali(series, model);
	resolved.gpu.model_number = model;
	resolved.gpu.core_count = ParseMaliCoreCount(lowered_hints);
	resolved.gpu.recognized = (spec != nullptr);

	if (immortalis)
		resolved.gpu.name = "Immortalis-G" + std::to_string(model);
	else if (series == 'U')
		resolved.gpu.name = "Mali-" + std::to_string(model);
	else
		resolved.gpu.name = "Mali-" + std::string(1, series) + std::to_string(model);
	if (resolved.gpu.core_count != 0)
		resolved.gpu.name += " MC" + std::to_string(resolved.gpu.core_count);

	return resolved;
}
} // namespace GpuProfileDetail
