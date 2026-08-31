// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace TextureCodec
{
	inline constexpr uint8_t Blocks[14][2] = {
		{4, 4}, {5, 4}, {5, 5}, {6, 5}, {6, 6}, {8, 5}, {8, 6},
		{8, 8}, {10, 5}, {10, 6}, {10, 8}, {10, 10}, {12, 10}, {12, 12}};
	struct Level
	{
		uint32_t width = 0, height = 0;
		std::vector<uint8_t> data;
	};
	struct Image
	{
		int block = -1; // -1: RGBA8; otherwise index into Blocks (ASTC LDR linear).
		std::vector<Level> levels;
	};
	bool Read(const std::string& path, Image& image, bool base_only, std::string& error);
	bool Decode(Image& image, std::string& error);
	bool Convert(const std::string& source, const std::string& destination, int block,
		unsigned threads, std::string& error);
} // namespace TextureCodec
