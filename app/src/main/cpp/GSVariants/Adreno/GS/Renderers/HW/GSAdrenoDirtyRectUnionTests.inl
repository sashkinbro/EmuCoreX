// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+
// Adreno-only actual texture-cache invalidation regression; compiled in Debug self-tests only.

#include <array>
#include <algorithm>
#include <cstddef>
#include <random>
#include <utility>
#include <vector>

namespace GSAdrenoDirtyRectUnionTest
{
GSDirtyRect Rect(int x, int y, int z, int w, u32 psm = PSMCT32, u32 bw = 10, u32 mask = 0xf)
{
	RGBAMask rgba{};
	rgba._u32 = mask;
	GSVector4i rect(x, y, z, w);
	return GSDirtyRect(rect, psm, bw, rgba, false);
}

GSDirtyRectList AsList(const std::vector<GSDirtyRect>& rects)
{
	GSDirtyRectList list;
	for (const GSDirtyRect& rect : rects)
		list.push_back(rect);
	return list;
}

bool CoverModel(const std::vector<GSDirtyRect>& rects, u32 psm, u32 bw, u32 mask, const GSVector4i& area)
{
	for (int y = area.y; y < area.w; y++)
	{
		for (int x = area.x; x < area.z; x++)
		{
			bool hit = false;
			for (const GSDirtyRect& rect : rects)
			{
				if (rect.psm == psm && std::max(rect.bw, 1u) == std::max(bw, 1u) &&
					(rect.rgba._u32 & mask) == mask && x >= rect.r.x && x < rect.r.z && y >= rect.r.y && y < rect.r.w)
				{
					hit = true;
					break;
				}
			}
			if (!hit)
				return false;
		}
	}
	return !area.rempty();
}

bool MatchesPixelModel(const std::vector<GSDirtyRect>& rects, u32 psm, u32 bw, u32 mask,
	const GSVector4i& area, std::mt19937& rng)
{
	const GSDirtyRectList list = AsList(rects);
	const bool expected = CoverModel(rects, psm, bw, mask, area);
	for (int attempt = 0; attempt < 3; attempt++)
	{
		GSVector2i hole(area.x - 2 + static_cast<int>(rng() % (area.z - area.x + 4)),
			area.y - 2 + static_cast<int>(rng() % (area.w - area.y + 4)));
		if (GSTextureCache::DirtyRectsCover(list, psm, bw, mask, area, &hole) != expected)
			return false;
		// Hole is a shortcut hint; early conservative exits need not overwrite it. Reusing it must
		// still produce the model's coverage result, regardless of whether it was a real hole.
		if (GSTextureCache::DirtyRectsCover(list, psm, bw, mask, area, &hole) != expected)
			return false;
	}
	return true;
}

bool RunPixelModel()
{
	std::mt19937 rng(2026);
	int covered = 0, uncovered = 0;
	for (int iter = 0; iter < 6000; iter++)
	{
		const int width = 6 + static_cast<int>(rng() % 26);
		const int height = 6 + static_cast<int>(rng() % 22);
		const GSVector4i area(static_cast<int>(rng() % 3), static_cast<int>(rng() % 3), width, height);
		const u32 mask = (rng() & 1) ? 0xf : 0x7;
		std::vector<GSDirtyRect> rects;
		const int style = static_cast<int>(rng() % 3);
		if (style == 0)
		{
			const int count = 1 + static_cast<int>(rng() % 14);
			for (int i = 0; i < count; i++)
			{
				const int x = static_cast<int>(rng() % width), y = static_cast<int>(rng() % height);
				rects.push_back(Rect(x - static_cast<int>(rng() % 3), y - static_cast<int>(rng() % 3),
					x + 1 + static_cast<int>(rng() % (width / 2 + 1)),
					y + 1 + static_cast<int>(rng() % (height / 2 + 1))));
			}
		}
		else
		{
			std::vector<GSVector4i> tiles{area};
			const int cuts = 1 + static_cast<int>(rng() % 10);
			for (int cut_index = 0; cut_index < cuts; cut_index++)
			{
				const size_t index = rng() % tiles.size();
				const GSVector4i tile = tiles[index];
				if (rng() & 1)
				{
					if (tile.z - tile.x < 2) continue;
					const int cut = tile.x + 1 + static_cast<int>(rng() % (tile.z - tile.x - 1));
					tiles[index] = GSVector4i(tile.x, tile.y, cut, tile.w);
					tiles.push_back(GSVector4i(cut, tile.y, tile.z, tile.w));
				}
				else
				{
					if (tile.w - tile.y < 2) continue;
					const int cut = tile.y + 1 + static_cast<int>(rng() % (tile.w - tile.y - 1));
					tiles[index] = GSVector4i(tile.x, tile.y, tile.z, cut);
					tiles.push_back(GSVector4i(tile.x, cut, tile.z, tile.w));
				}
			}
			for (GSVector4i tile : tiles)
			{
				if (style == 2 && rng() % 5 == 0)
				{
					switch (rng() % 4)
					{
						case 0: tile.x++; break;
						case 1: tile.y++; break;
						case 2: tile.z--; break;
						default: tile.w--; break;
					}
				}
				if (!tile.rempty()) rects.push_back(Rect(tile.x, tile.y, tile.z, tile.w));
			}
			std::shuffle(rects.begin(), rects.end(), rng);
		}

		const int noise = static_cast<int>(rng() % 3);
		for (int i = 0; i < noise; i++)
		{
			GSDirtyRect d = Rect(0, 0, width + 4, height + 4);
			switch (rng() % 3)
			{
				case 0: d.psm = PSMCT16; break;
				case 1: d.bw = 3; break;
				default: d.rgba._u32 = 0x8; break;
			}
			rects.insert(rects.begin() + static_cast<std::ptrdiff_t>(rng() % (rects.size() + 1)), d);
		}
		const bool expected = CoverModel(rects, PSMCT32, 10, mask, area);
		if (!MatchesPixelModel(rects, PSMCT32, 10, mask, area, rng)) return false;
		(expected ? covered : uncovered)++;
	}
	if (covered <= 600 || uncovered <= 600)
		return false;

	std::vector<GSDirtyRect> too_many;
	for (int i = 0; i < 129; i++)
		too_many.push_back(Rect(i, 0, i + 1, 1));
	if (GSTextureCache::DirtyRectsCover(AsList(too_many), PSMCT32, 10, 0xf, GSVector4i(0, 0, 129, 1)))
		return false;
	if (GSTextureCache::DirtyRectsCover(AsList({Rect(0, 0, 16, 16, PSMCT16)}),
			PSMCT32, 10, 0xf, GSVector4i(0, 0, 16, 16)) ||
		GSTextureCache::DirtyRectsCover(AsList({Rect(0, 0, 16, 16, PSMCT32, 11)}),
			PSMCT32, 10, 0xf, GSVector4i(0, 0, 16, 16)))
		return false;

	std::mt19937 grid_rng(8);
	covered = uncovered = 0;
	for (int iter = 0; iter < 700; iter++)
	{
		const int cells_x = 1 + static_cast<int>(grid_rng() % ((iter % 3 == 0) ? 150 : 40));
		const int cells_y = 1 + static_cast<int>(grid_rng() % 6);
		const GSVector4i area(8 * static_cast<int>(grid_rng() % 3), 8 * static_cast<int>(grid_rng() % 3),
			0, 0);
		const GSVector4i full(area.x, area.y, area.x + 8 * cells_x, area.y + 8 * cells_y);
		std::vector<GSVector4i> tiles{full};
		const int cuts = 1 + static_cast<int>(grid_rng() % 12);
		for (int cut_index = 0; cut_index < cuts; cut_index++)
		{
			const size_t index = grid_rng() % tiles.size();
			const GSVector4i tile = tiles[index];
			if (grid_rng() & 1)
			{
				if (tile.z - tile.x < 16) continue;
				const int cut = tile.x + 8 * (1 + static_cast<int>(grid_rng() % ((tile.z - tile.x) / 8 - 1)));
				tiles[index] = GSVector4i(tile.x, tile.y, cut, tile.w);
				tiles.push_back(GSVector4i(cut, tile.y, tile.z, tile.w));
			}
			else
			{
				if (tile.w - tile.y < 16) continue;
				const int cut = tile.y + 8 * (1 + static_cast<int>(grid_rng() % ((tile.w - tile.y) / 8 - 1)));
				tiles[index] = GSVector4i(tile.x, tile.y, tile.z, cut);
				tiles.push_back(GSVector4i(tile.x, cut, tile.z, tile.w));
			}
		}
		std::vector<GSDirtyRect> rects;
		for (GSVector4i tile : tiles)
		{
			const int change = static_cast<int>(grid_rng() % 12);
			if (change == 0) tile.z = std::max(tile.z - 8, tile.x);
			else if (change == 1) tile.y += 8;
			else if (change == 2) tile.x += 1;
			else if (change == 3) tile.w -= 1;
			if (!tile.rempty()) rects.push_back(Rect(tile.x, tile.y, tile.z, tile.w));
		}
		std::shuffle(rects.begin(), rects.end(), grid_rng);
		const bool expected = CoverModel(rects, PSMCT32, 10, 0xf, full);
		if (!MatchesPixelModel(rects, PSMCT32, 10, 0xf, full, grid_rng)) return false;
		(expected ? covered : uncovered)++;
	}
	return covered > 100 && uncovered > 100;
}
} // namespace GSAdrenoDirtyRectUnionTest

void GSRendererHW::TestAdrenoDirtyRectUnion(void (*check)(bool, const char*))
{
	const bool isolated = g_gs_device && !g_gs_renderer && !g_texture_cache;
	check(isolated, "Adreno dirty-rect union fixture owns an isolated HW renderer and cache");
	if (!isolated)
		return;

	const auto saved_settings = GSConfig;
	GSConfig = Pcsx2Config::GSOptions();
	g_gs_renderer = std::make_unique<GSRendererHW>();
	auto& renderer = *static_cast<GSRendererHW*>(g_gs_renderer.get());
	ScopedGuard cleanup([&]() {
		renderer.m_last_rt = nullptr;
		// Normal renderer teardown drains the cache before its unique_ptr is reset. Target::~Target
		// consults the global g_texture_cache while decrementing memory usage, so preserve that order.
		if (g_texture_cache)
			g_texture_cache->RemoveAll(true, true, true);
		g_gs_renderer.reset();
		GSConfig = saved_settings;
	});

	constexpr u32 kBW = 10;
	constexpr int kWidth = 640;
	constexpr int kHeight = 448;
	const auto make_target = [&](u32 bp, u32 psm = PSMCT32, int valid_width = kWidth,
		int valid_height = kHeight, int type = GSTextureCache::RenderTarget) -> GSTextureCache::Target* {
		GIFRegTEX0 tex0{};
		tex0.TBP0 = bp;
		tex0.TBW = kBW;
		tex0.PSM = psm;
		GSTextureCache::Target* target = g_texture_cache->CreateTarget(tex0,
			GSVector2i(kWidth, kHeight), GSVector2i(valid_width, valid_height), 1.0f,
			type, true, 0, false, false, false);
		if (target)
		{
			target->m_valid = GSVector4i(0, 0, valid_width, valid_height);
			target->m_dirty.clear();
			target->DirtyListChanged();
		}
		return target;
	};
	const auto find_target = [&](u32 bp, int type = GSTextureCache::RenderTarget) {
		return g_texture_cache->GetExactTarget(bp, kBW, type, bp);
	};
	const auto write = [&](u32 bp, const GSVector4i& rect, u32 psm = PSMCT32, u32 bw = kBW) {
		g_texture_cache->InvalidateVideoMem(renderer.m_mem.GetOffset(bp, bw, psm), rect);
	};
	const auto pinwheel = []() {
		const int xa = kWidth * 3 / 8;
		const int xb = kWidth * 5 / 8;
		const int ya = kHeight * 4 / 9;
		const int yb = kHeight * 2 / 3;
		return std::array<GSVector4i, 5>{
			GSVector4i(0, 0, xb, ya), GSVector4i(xb, 0, kWidth, yb),
			GSVector4i(xa, yb, kWidth, kHeight), GSVector4i(0, ya, xa, kHeight),
			GSVector4i(xa, ya, xb, yb)};
	};
	check(GSAdrenoDirtyRectUnionTest::RunPixelModel(),
		"Adreno dirty-rectangle union matches pixel models on 6000 random and 700 block-grid lists");

	const auto cover = pinwheel();
	constexpr u32 kColorBP = 0;
	GSTextureCache::Target* target = make_target(kColorBP);
	check(target != nullptr, "Adreno union fixture creates an actual cached color target");
	if (!target)
		return;
	check(target->m_valid.eq(GSVector4i(0, 0, kWidth, kHeight)),
		"Adreno union fixture target valid rectangle matches pinwheel coverage");
	for (size_t i = 0; i + 1 < cover.size(); i++)
	{
		write(kColorBP, cover[i]);
		check(find_target(kColorBP) == target, "Adreno target remains before dirty-rectangle union is complete");
	}
	check(target->m_dirty.size() == cover.size() - 1,
		"Adreno pinwheel pieces remain distinct dirty rectangles before completion");
	write(kColorBP, cover.back());
	check(find_target(kColorBP) == nullptr,
		"Adreno InvalidateVideoMem retires target when distinct dirty rectangles cover every valid pixel");

	constexpr u32 kGapBP = 0x1000;
	GSTextureCache::Target* gap_target = make_target(kGapBP);
	check(gap_target != nullptr, "Adreno union fixture creates a one-pixel-gap control target");
	if (!gap_target)
		return;
	std::array<GSVector4i, 5> gap = cover;
	gap[4].z--;
	for (const GSVector4i& rect : gap)
		write(kGapBP, rect);
	check(find_target(kGapBP) == gap_target,
		"Adreno union does not retire a target with a one-pixel hole in its dirty rectangle union");
	const int gap_x = kWidth * 5 / 8 - 1;
	const int gap_y0 = kHeight * 4 / 9;
	const int gap_y1 = kHeight * 2 / 3;
	write(kGapBP, GSVector4i(gap_x, gap_y0, gap_x + 1, gap_y1));
	check(find_target(kGapBP) == nullptr, "Adreno union retires the one-pixel-gap control after the hole is written");

	constexpr u32 kAlphaBP = 0x2000;
	GSTextureCache::Target* alpha_target = make_target(kAlphaBP);
	check(alpha_target != nullptr, "Adreno union fixture creates a missing-channel control target");
	if (!alpha_target)
		return;
	for (size_t i = 0; i < cover.size(); i++)
		write(kAlphaBP, cover[i], (i == 2) ? PSMCT24 : PSMCT32);
	check(find_target(kAlphaBP) == alpha_target,
		"Adreno union does not retire an RGBA target when one covering write omits alpha");

	constexpr u32 kWidthBP = 0x2800;
	GSTextureCache::Target* width_target = make_target(kWidthBP);
	check(width_target != nullptr, "Adreno union fixture creates a buffer-width control target");
	if (!width_target)
		return;
	for (size_t i = 0; i + 1 < cover.size(); i++)
		write(kWidthBP, cover[i]);
	write(kWidthBP, cover.back(), PSMCT32, kBW + 1);
	check(find_target(kWidthBP) == width_target,
		"Adreno union does not retire an RGBA target when one covering write uses a different buffer width");
	write(kWidthBP, cover.back());
	check(find_target(kWidthBP) == nullptr, "Adreno union retires the buffer-width control after the matching write");

	constexpr u32 kPsmBP = 0x2c00;
	GSTextureCache::Target* psm_target = make_target(kPsmBP);
	check(psm_target != nullptr, "Adreno union fixture creates a pixel-format control target");
	if (!psm_target)
		return;
	for (size_t i = 0; i + 1 < cover.size(); i++)
		write(kPsmBP, cover[i]);
	write(kPsmBP, cover.back(), PSMCT16);
	check(find_target(kPsmBP) == psm_target,
		"Adreno union does not retire an RGBA target when one covering write uses a different pixel format");
	write(kPsmBP, cover.back());
	check(find_target(kPsmBP) == nullptr, "Adreno union retires the pixel-format control after the matching write");

	constexpr u32 kRgbBP = 0x3000;
	GSTextureCache::Target* rgb_target = make_target(kRgbBP, PSMCT24);
	check(rgb_target != nullptr, "Adreno union fixture creates an RGB target");
	if (!rgb_target)
		return;
	for (const GSVector4i& rect : cover)
		write(kRgbBP, rect, PSMCT24);
	check(find_target(kRgbBP) == nullptr, "Adreno union retires an RGB target covered by RGB writes");

	constexpr u32 kValidBP = 0x3800;
	GSTextureCache::Target* valid_target = make_target(kValidBP, PSMCT32, kWidth / 2, kHeight / 2);
	check(valid_target != nullptr, "Adreno union fixture creates a subarea-valid target");
	if (!valid_target)
		return;
	const auto small_cover = [&]() {
		const int width = kWidth / 2, height = kHeight / 2;
		const int xa = width * 3 / 8, xb = width * 5 / 8;
		const int ya = height * 4 / 9, yb = height * 2 / 3;
		return std::array<GSVector4i, 5>{GSVector4i(0, 0, xb, ya), GSVector4i(xb, 0, width, yb),
			GSVector4i(xa, yb, width, height), GSVector4i(0, ya, xa, height), GSVector4i(xa, ya, xb, yb)};
	};
	for (const GSVector4i& rect : small_cover())
		write(kValidBP, rect);
	check(find_target(kValidBP) == nullptr, "Adreno union uses the target valid area rather than texture allocation size");

	constexpr u32 kDepthBP = 0x2000;
	GIFRegTEX0 depth_tex0{};
	depth_tex0.TBP0 = kDepthBP;
	depth_tex0.TBW = kBW;
	depth_tex0.PSM = PSMZ32;
	GSTextureCache::Target* depth_target = g_texture_cache->CreateTarget(depth_tex0,
		GSVector2i(kWidth, kHeight), GSVector2i(kWidth, kHeight), 1.0f,
		GSTextureCache::DepthStencil, true, 0, false, false, false);
	check(depth_target != nullptr, "Adreno union fixture creates a real cached depth target");
	if (!depth_target)
		return;
	depth_target->m_valid = GSVector4i(0, 0, kWidth, kHeight);
	depth_target->m_dirty.clear();
	depth_target->DirtyListChanged();
	for (const GSVector4i& rect : cover)
		write(kDepthBP, rect, PSMZ32);
	check(find_target(kDepthBP, GSTextureCache::DepthStencil) == nullptr,
		"Adreno union retires a depth target when all valid depth pixels are in local memory");

	std::mt19937 rng(11);
	int retired_cases = 0, kept_cases = 0;
	for (int iter = 0; iter < 160; iter++)
	{
		g_texture_cache->RemoveAll(true, true, true);
		const int width = 64 + static_cast<int>(rng() % 5) * 64;
		const int height = 32 + static_cast<int>(rng() % 5) * 32;
		GSTextureCache::Target* random_target = make_target(0, PSMCT32, width, height);
		if (!random_target) { retired_cases = kept_cases = -1000; break; }
		std::vector<u8> model(static_cast<size_t>(width) * height, 0);
		size_t uncovered = model.size();
		const int steps = 2 + static_cast<int>(rng() % 24);
		const int grain = (rng() % 4 == 0) ? 1 : 8;
		bool gone = false, model_full = false;
		for (int step = 0; step < steps && !gone; step++)
		{
			const auto span = [&](int size) {
				const int cells = size / grain;
				const int lo = (rng() % 3 == 0) ? 0 : static_cast<int>(rng() % cells);
				const int hi = (rng() % 3 == 0) ? cells : lo + 1 + static_cast<int>(rng() % (cells - lo));
				return std::pair<int, int>{lo * grain, hi * grain};
			};
			const auto xs = span(width), ys = span(height);
			const GSVector4i rect(xs.first, ys.first, xs.second, ys.second);
			for (int y = rect.y; y < rect.w; y++)
				for (int x = rect.x; x < rect.z; x++)
				{
					u8& pixel = model[static_cast<size_t>(y) * width + x];
					if (pixel == 0)
					{
						pixel = 1;
						uncovered--;
					}
				}
			model_full = (uncovered == 0);
			write(0, rect);
			gone = find_target(0) == nullptr;
			if (gone != model_full)
			{
				retired_cases = kept_cases = -1000;
				break;
			}
		}
		(gone ? retired_cases : kept_cases)++;
	}
	check(retired_cases > 20 && kept_cases > 20,
		"Adreno target retirement matches the pixel model across 160 random write sequences");
}
