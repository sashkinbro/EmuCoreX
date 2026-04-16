// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "PrecompiledHeader.h"
#include "GS/Renderers/Common/GSGPUProfile.h"

#include <array>
#include <cctype>
#include <initializer_list>
#include <optional>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace
{
static std::string ToLowerASCII(std::string_view value)
{
	std::string lowered;
	lowered.reserve(value.size());

	for (const char ch : value)
		lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));

	return lowered;
}

static bool Contains(std::string_view haystack, std::string_view needle)
{
	return (haystack.find(needle) != std::string_view::npos);
}

static bool ContainsAny(std::string_view haystack, std::initializer_list<const char*> needles)
{
	for (const char* needle : needles)
	{
		if (Contains(haystack, needle))
			return true;
	}

	return false;
}

static void AppendHint(std::string& hints, std::string_view key, std::string_view value)
{
	if (value.empty())
		return;

	if (!hints.empty())
		hints.append(" | ");

	if (!key.empty())
	{
		hints.append(key);
		hints.push_back('=');
	}

	hints.append(value);
}

#if defined(__ANDROID__)
static std::string GetAndroidProperty(const char* name)
{
	std::array<char, PROP_VALUE_MAX> value = {};
	const int length = __system_property_get(name, value.data());
	return (length > 0) ? std::string(value.data(), static_cast<size_t>(length)) : std::string();
}
#endif

static std::string BuildHints(std::string_view gpu_vendor, std::string_view gpu_renderer_or_name)
{
	std::string hints;
	AppendHint(hints, "gpu_vendor", gpu_vendor);
	AppendHint(hints, "gpu", gpu_renderer_or_name);

#if defined(__ANDROID__)
	static constexpr const char* property_names[] = {
		"ro.soc.manufacturer",
		"ro.soc.model",
		"ro.soc.platform",
		"ro.board.platform",
		"ro.hardware",
		"ro.product.board",
		"ro.product.cpu.abi",
		"ro.vendor.product.cpu.abilist",
	};

	for (const char* property_name : property_names)
		AppendHint(hints, property_name, GetAndroidProperty(property_name));
#endif

	return hints;
}

static bool LooksLikeAdreno(std::string_view lowered_hints)
{
	const bool has_adreno = ContainsAny(lowered_hints, {"adreno"});
	const bool has_qualcomm = ContainsAny(lowered_hints, {"qualcomm", "qcom", "snapdragon"});
	return (has_adreno || has_qualcomm);
}

static std::optional<u32> ParseAdrenoModel(std::string_view value)
{
	const std::string lowered = ToLowerASCII(value);
	const size_t adreno_pos = lowered.find("adreno");
	if (adreno_pos == std::string::npos)
		return std::nullopt;

	size_t digit_pos = adreno_pos + 6;
	while (digit_pos < lowered.size() && !std::isdigit(static_cast<unsigned char>(lowered[digit_pos])))
		digit_pos++;

	if (digit_pos >= lowered.size())
		return std::nullopt;

	u32 model = 0;
	size_t digits = 0;
	while ((digit_pos + digits) < lowered.size() && std::isdigit(static_cast<unsigned char>(lowered[digit_pos + digits])))
	{
		model = (model * 10u) + static_cast<u32>(lowered[digit_pos + digits] - '0');
		digits++;
	}

	return (digits >= 3) ? std::optional<u32>(model) : std::nullopt;
}
} // namespace

GpuProfileOverride GpuProfileDetector::ParseOverride(std::string_view value)
{
	const std::string lowered = ToLowerASCII(value);
	if (lowered == "mali")
		return GpuProfileOverride::Mali;
	if (lowered == "adreno")
		return GpuProfileOverride::Adreno;

	return GpuProfileOverride::Auto;
}

const char* GpuProfileDetector::OverrideToConfigString(GpuProfileOverride value)
{
	switch (value)
	{
		case GpuProfileOverride::Mali:
			return "mali";
		case GpuProfileOverride::Adreno:
			return "adreno";
		case GpuProfileOverride::Auto:
		default:
			return "auto";
	}
}

const char* GpuProfileDetector::OverrideToString(GpuProfileOverride value)
{
	switch (value)
	{
		case GpuProfileOverride::Mali:
			return "Force Mali";
		case GpuProfileOverride::Adreno:
			return "Force Adreno";
		case GpuProfileOverride::Auto:
		default:
			return "Auto";
	}
}

const char* GpuProfileDetector::RuntimeProfileToString(RuntimeGpuProfile value)
{
	switch (value)
	{
		case RuntimeGpuProfile::Mali:
			return "Mali";
		case RuntimeGpuProfile::Adreno:
		default:
			return "Adreno";
	}
}

std::optional<u32> GpuProfileDetector::DetectAdrenoModel(std::string_view gpu_renderer_or_name)
{
	return ParseAdrenoModel(gpu_renderer_or_name);
}

bool GpuProfileDetector::PrefersNativeAdrenoBarrierPath(std::string_view gpu_renderer_or_name)
{
	const std::optional<u32> model = DetectAdrenoModel(gpu_renderer_or_name);
	return model.has_value() && model.value() >= 700u;
}

GpuProfileSelection GpuProfileDetector::Resolve(std::string_view override_value, std::string_view gpu_vendor,
	std::string_view gpu_renderer_or_name)
{
	GpuProfileSelection selection;
	selection.override_mode = ParseOverride(override_value);
	selection.hints = BuildHints(gpu_vendor, gpu_renderer_or_name);

	if (selection.override_mode == GpuProfileOverride::Mali)
	{
		selection.runtime_profile = RuntimeGpuProfile::Mali;
		return selection;
	}

	if (selection.override_mode == GpuProfileOverride::Adreno)
	{
		selection.runtime_profile = RuntimeGpuProfile::Adreno;
		return selection;
	}

	const std::string lowered_hints = ToLowerASCII(selection.hints);

#if defined(__ANDROID__)
	if (LooksLikeAdreno(lowered_hints))
	{
		selection.runtime_profile = RuntimeGpuProfile::Adreno;
	}
	else
	{
		// Per Android policy for this fork: unknown/non-Adreno devices default to Mali profile.
		selection.runtime_profile = RuntimeGpuProfile::Mali;
	}
#else
	selection.runtime_profile = RuntimeGpuProfile::Adreno;
#endif

	return selection;
}
