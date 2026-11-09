// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <cstdint>

// Pure cadence state shared by the Vulkan draw-entry policy and its host check.
// Counting completed passes is independent of the readback arming window.
struct GSVKReadbackKickPolicy
{
	enum class Mode : std::uint8_t
	{
		Disabled,
		Readback,
		PassBoundary // Debug measurement only; production uses Readback.
	};

	static constexpr std::uint32_t DEFAULT_PASS_INTERVAL = 8;
	static constexpr std::uint32_t READBACK_WINDOW_FRAMES = 3;

	constexpr void ReadbackRequested(std::uint32_t frame)
	{
		m_readback_frame = frame;
		m_has_readback = true;
	}
	constexpr void RenderPassEnded() { ++m_completed_passes; }
	constexpr void Submitted() { m_completed_passes = 0; }
	constexpr std::uint32_t UnsubmittedPasses(bool in_render_pass) const
	{
		return m_completed_passes + (in_render_pass ? 1u : 0u);
	}
	constexpr bool IsArmed(std::uint32_t frame, Mode mode) const
	{
		return mode == Mode::PassBoundary ||
			(mode == Mode::Readback && m_has_readback && (frame - m_readback_frame) <= READBACK_WINDOW_FRAMES);
	}
	constexpr bool AtCadence(bool natural_boundary, bool in_render_pass,
		std::uint32_t interval = DEFAULT_PASS_INTERVAL) const
	{
		return natural_boundary && UnsubmittedPasses(in_render_pass) >= interval;
	}

private:
	std::uint32_t m_completed_passes = 0;
	std::uint32_t m_readback_frame = 0;
	bool m_has_readback = false;
};
