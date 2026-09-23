// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Common/GSGPUProfile.h"

#include <array>
#include <cctype>
#include <initializer_list>
#include <string>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace
{
std::string ToLowerASCII(std::string_view value)
{
	std::string lowered;
	lowered.reserve(value.size());

	for (const char ch : value)
		lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));

	return lowered;
}

bool ContainsAny(std::string_view haystack, std::initializer_list<const char*> needles)
{
	for (const char* needle : needles)
	{
		if (haystack.find(needle) != std::string_view::npos)
			return true;
	}

	return false;
}

bool LooksLikeAdreno(std::string_view lowered)
{
	return ContainsAny(lowered, {"adreno", "qualcomm", "qcom", "snapdragon"});
}

bool LooksLikeMobileTileGpu(std::string_view lowered)
{
	return ContainsAny(lowered, {"mali", "immortalis", "powervr", "imgtec", "imagination technologies"});
}
} // namespace

const char* GpuProfileDetector::RuntimeProfileToString(RuntimeGpuProfile value)
{
	switch (value)
	{
		case RuntimeGpuProfile::Mobile:
			return "Mobile";
		case RuntimeGpuProfile::Adreno:
			return "Adreno";
		case RuntimeGpuProfile::Unknown:
		default:
			return "Unknown";
	}
}

RuntimeGpuProfile GpuProfileDetector::Detect(std::string_view gpu_vendor, std::string_view gpu_renderer_or_name)
{
	// The renderer/device name is the strongest signal. Check it before the vendor string so a
	// stale or conflicting vendor hint cannot select the wrong path.
	const std::string renderer = ToLowerASCII(gpu_renderer_or_name);
	if (LooksLikeAdreno(renderer))
		return RuntimeGpuProfile::Adreno;
	if (LooksLikeMobileTileGpu(renderer))
		return RuntimeGpuProfile::Mobile;

	const std::string vendor = ToLowerASCII(gpu_vendor);
	if (LooksLikeAdreno(vendor))
		return RuntimeGpuProfile::Adreno;
	if (LooksLikeMobileTileGpu(vendor))
		return RuntimeGpuProfile::Mobile;

	return RuntimeGpuProfile::Unknown;
}

u32 GpuProfileDetector::ParseAdrenoGeneration(std::string_view gpu_renderer_or_name)
{
	const std::string lowered = ToLowerASCII(gpu_renderer_or_name);
	const size_t position = lowered.find("adreno");
	if (position == std::string::npos)
		return 0;

	// Device names look like "Adreno (TM) 740" or "Adreno X1-85". Walk to the first digit,
	// resolving the X-series (9th generation) along the way.
	size_t index = position + 6;
	while (index < lowered.size())
	{
		const char ch = lowered[index];
		if (ch == 'x')
			return 9;
		if (std::isdigit(static_cast<unsigned char>(ch)))
			break;
		index++;
	}

	u32 model = 0;
	for (; index < lowered.size() && std::isdigit(static_cast<unsigned char>(lowered[index])); index++)
		model = model * 10 + static_cast<u32>(lowered[index] - '0');

	return (model >= 200) ? (model / 100) : 0;
}

bool GpuProfileDetector::LooksLikeMediaTekSoC(std::string_view hints)
{
	const std::string lowered = ToLowerASCII(hints);
	if (ContainsAny(lowered, {"mediatek", "dimensity", "helio", "mtk"}))
		return true;

	// MediaTek board/platform properties commonly use compact part numbers such as mt6877 or
	// mt6989z without spelling out the vendor. Require a token boundary and four digits to avoid
	// treating an unrelated occurrence of "mt" as a chipset identifier.
	for (size_t i = 0; i + 6 <= lowered.size(); i++)
	{
		if (lowered[i] != 'm' || lowered[i + 1] != 't' ||
			(i > 0 && std::isalnum(static_cast<unsigned char>(lowered[i - 1]))))
		{
			continue;
		}

		bool has_four_digits = true;
		for (size_t digit = i + 2; digit < i + 6; digit++)
			has_four_digits &= (std::isdigit(static_cast<unsigned char>(lowered[digit])) != 0);

		if (has_four_digits)
			return true;
	}

	return false;
}

bool GpuProfileDetector::DetectMediaTekSoC()
{
#if defined(__ANDROID__)
	static constexpr const char* property_names[] = {
		"ro.soc.manufacturer",
		"ro.soc.model",
		"ro.soc.platform",
		"ro.board.platform",
		"ro.hardware",
		"ro.hardware.chipname",
		"ro.chipname",
		"ro.product.board",
		"ro.mediatek.platform",
		"ro.vendor.mediatek.platform",
	};

	for (const char* property_name : property_names)
	{
		std::array<char, PROP_VALUE_MAX> value = {};
		const int length = __system_property_get(property_name, value.data());
		if (length > 0 &&
			LooksLikeMediaTekSoC(std::string_view(value.data(), static_cast<size_t>(length))))
		{
			return true;
		}
	}
#endif

	return false;
}
