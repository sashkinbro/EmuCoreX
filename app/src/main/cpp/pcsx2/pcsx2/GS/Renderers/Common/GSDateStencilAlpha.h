// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstdint>

enum class GSDateStencilAlpha : std::uint8_t
{
	Unknown,
	Clear,
	Set,
};

// The range describes alpha which can actually be written after the alpha test.
// Callers must reject shader/output transformations which do not preserve bit 7.
constexpr GSDateStencilAlpha GSClassifyDateStencilAlpha(int minimum, int maximum, bool fba, bool exact_output)
{
	if (!exact_output || minimum < 0 || maximum > 255 || minimum > maximum)
		return GSDateStencilAlpha::Unknown;
	if (fba || minimum >= 128)
		return GSDateStencilAlpha::Set;
	return maximum < 128 ? GSDateStencilAlpha::Clear : GSDateStencilAlpha::Unknown;
}
