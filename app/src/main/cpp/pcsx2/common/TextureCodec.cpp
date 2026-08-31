// SPDX-License-Identifier: GPL-3.0+
#include "TextureCodec.h"
#include "TextureDecompress.h"
#include "astcenc.h"
#include <png.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <memory>
#include <thread>
#include <condition_variable>
#include <mutex>

namespace TextureCodec
{
	namespace
	{
		constexpr size_t MaxBytes = 128 * 1024 * 1024;
		constexpr uint8_t KtxMagic[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
		uint32_t U32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
		uint64_t U64(const uint8_t* p) { return U32(p) | uint64_t(U32(p + 4)) << 32; }
		void Put(std::vector<uint8_t>& v, size_t offset, uint64_t value, unsigned count = 4)
		{
			for (unsigned i = 0; i < count; i++)
				v[offset + i] = uint8_t(value >> (8 * i));
		}
		bool Dimensions(uint32_t w, uint32_t h)
		{
			return w && h && w <= 16384 && h <= 16384 && uint64_t(w) * h * 4 <= MaxBytes;
		}
		size_t BlockBytes(uint32_t w, uint32_t h, int block)
		{
			return size_t((w + Blocks[block][0] - 1) / Blocks[block][0]) * ((h + Blocks[block][1] - 1) / Blocks[block][1]) * 16;
		}
		unsigned MipCount(uint32_t w, uint32_t h)
		{
			unsigned n = 1;
			while ((w > 1 || h > 1) && n < 15)
			{
				w = std::max(1u, w / 2);
				h = std::max(1u, h / 2);
				n++;
			}
			return n;
		}
		bool Fail(std::string& error, const char* message)
		{
			error = message;
			return false;
		}
		using Context = std::unique_ptr<astcenc_context, decltype(&astcenc_context_free)>;
		constexpr astcenc_swizzle Swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};

		bool ReadDDS(const std::vector<uint8_t>& bytes, Image& image, bool base_only, std::string& error)
		{
			if (bytes.size() < 128 || U32(bytes.data() + 4) != 124 || U32(bytes.data() + 76) != 32)
				return Fail(error, "Invalid DDS header");
			const auto* p = bytes.data();
			uint32_t w = U32(p + 16), h = U32(p + 12), flags = U32(p + 8), fourcc = U32(p + 84);
			if (!Dimensions(w, h) || U32(p + 112) != 0 || (flags & 0x800000))
				return Fail(error, "Unsupported DDS dimensions or surface type");
			uint32_t levels = (flags & 0x20000) ? std::max(1u, U32(p + 28)) : 1;
			if (levels > MipCount(w, h))
				return Fail(error, "Invalid DDS mip count");
			size_t offset = 128;
			int bc = 0;
			uint32_t bits = U32(p + 88);
			std::array<uint32_t, 4> masks{U32(p + 92), U32(p + 96), U32(p + 100), U32(p + 104)};
			if (U32(p + 80) & 4)
			{
				if (fourcc == 0x31545844)
					bc = 1;
				else if (fourcc == 0x32545844 || fourcc == 0x33545844)
					bc = 2;
				else if (fourcc == 0x34545844 || fourcc == 0x35545844)
					bc = 3;
				else if (fourcc == 0x30315844 && bytes.size() >= 148)
				{
					if (U32(p + 132) != 3 || U32(p + 140) != 1 || (U32(p + 136) & 4))
						return Fail(error, "Unsupported DDS array or cube");
					offset = 148;
					switch (U32(p + 128))
					{
						case 71:
							bc = 1;
							break;
						case 74:
							bc = 2;
							break;
						case 77:
							bc = 3;
							break;
						case 98:
							bc = 7;
							break;
						default:
							return Fail(error, "Unsupported DDS format (original preserved)");
					}
				}
				else
					return Fail(error, "Unsupported DDS compression (original preserved)");
			}
			else if ((bits != 24 && bits != 32) || !((masks[0] == 0xff && masks[1] == 0xff00 && masks[2] == 0xff0000) || (masks[0] == 0xff0000 && masks[1] == 0xff00 && masks[2] == 0xff)) ||
					 (masks[3] != 0 && masks[3] != 0xff000000))
				return Fail(error, "Unsupported DDS pixel layout (original preserved)");
			size_t memory = 0;
			for (uint32_t mip = 0; mip < levels; mip++)
			{
				const uint32_t stride = bc ? ((w + 3) / 4) * (bc == 1 ? 8 : 16) : w * (bits / 8);
				const uint32_t pitch = (!bc && mip == 0 && (flags & 8)) ? std::max(stride, U32(p + 20)) : stride;
				const size_t size = size_t(pitch) * (bc ? (h + 3) / 4 : h);
				if (size > bytes.size() - std::min(offset, bytes.size()))
					return Fail(error, "Truncated DDS mip data");
				memory += size_t(w) * h * 4;
				if (memory > MaxBytes)
					return Fail(error, "Texture exceeds conversion memory limit");
				Level level{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
				if (bc)
				{
					for (uint32_t y = 0; y < h; y += 4)
						for (uint32_t x = 0; x < w; x += 4)
						{
							alignas(16) uint8_t tile[64];
							const uint8_t* block = p + offset + (y / 4) * pitch + (x / 4) * (bc == 1 ? 8 : 16);
							if (bc == 1)
								DecompressBlockBC1(0, 0, 16, block, tile);
							else if (bc == 2)
								DecompressBlockBC2(0, 0, 16, block, tile);
							else if (bc == 3)
								DecompressBlockBC3(0, 0, 16, block, tile);
							else if (!bc7decomp::unpack_bc7(block, reinterpret_cast<bc7decomp::color_rgba*>(tile)))
								return Fail(error, "Invalid BC7 block");
							for (uint32_t dy = 0; dy < std::min(4u, h - y); dy++)
								std::memcpy(level.data.data() + (size_t(y + dy) * w + x) * 4, tile + dy * 16, std::min(4u, w - x) * 4);
						}
				}
				else
					for (uint32_t y = 0; y < h; y++)
						for (uint32_t x = 0; x < w; x++)
						{
							const uint8_t* in = p + offset + size_t(y) * pitch + x * (bits / 8);
							uint8_t* out = level.data.data() + (size_t(y) * w + x) * 4;
							out[0] = in[masks[0] == 0xff ? 0 : 2];
							out[1] = in[1];
							out[2] = in[masks[2] == 0xff ? 0 : 2];
							// Match the existing replacement loader's PS2 alpha convention.
							out[3] = masks[3] ? in[3] : (masks[0] == 0xff ? 128 : 255);
						}
				image.levels.push_back(std::move(level));
				offset += size;
				if (base_only)
					break;
				w = std::max(1u, w / 2);
				h = std::max(1u, h / 2);
			}
			return true;
		}

		bool ReadKtx(const std::vector<uint8_t>& bytes, Image& image, bool base_only, std::string& error)
		{
			if (bytes.size() < 104)
				return Fail(error, "Truncated KTX2 header");
			const auto* p = bytes.data();
			uint32_t format = U32(p + 12), w = U32(p + 20), h = U32(p + 24), n = U32(p + 40);
			if (format < 157 || format > 183 || ((format - 157) & 1) || U32(p + 16) != 1 || !Dimensions(w, h) ||
				U32(p + 28) != 0 || U32(p + 32) != 0 || U32(p + 36) != 1 || n == 0 || n > MipCount(w, h) ||
				U32(p + 44) != 0 || U64(p + 64) != 0 || U64(p + 72) != 0 || bytes.size() < 80 + size_t(n) * 24)
				return Fail(error, "Unsupported KTX2 format (requires 2D ASTC LDR linear)");
			const uint32_t dfd = U32(p + 48), dfdSize = U32(p + 52);
			if (dfd < 80 + n * 24 || dfdSize < 44 || uint64_t(dfd) + dfdSize > bytes.size() || U32(p + dfd) != dfdSize)
				return Fail(error, "Invalid KTX2 data format descriptor");
			image.block = int((format - 157) / 2);
			if (p[dfd + 12] != 162 || p[dfd + 14] != 1 || p[dfd + 16] != Blocks[image.block][0] - 1 ||
				p[dfd + 17] != Blocks[image.block][1] - 1 || p[dfd + 20] != 16)
				return Fail(error, "KTX2 descriptor does not match the ASTC format");
			uint64_t previous = bytes.size();
			for (uint32_t i = 0; i < n; i++)
			{
				const auto* index = p + 80 + i * 24;
				uint64_t offset = U64(index), length = U64(index + 8);
				size_t expected = BlockBytes(w, h, image.block);
				if (offset % 16 || offset < uint64_t(dfd) + dfdSize || offset > bytes.size() || length != expected ||
					length > bytes.size() - offset || U64(index + 16) != length || offset + length > previous)
					return Fail(error, "Invalid KTX2 mip range");
				if (!base_only || i == 0)
					image.levels.push_back({w, h, std::vector<uint8_t>(p + offset, p + offset + length)});
				previous = offset;
				w = std::max(1u, w / 2);
				h = std::max(1u, h / 2);
			}
			return true;
		}

		bool WriteKtx(const std::string& path, const Image& image, std::string& error)
		{
			const size_t n = image.levels.size(), dfd = 80 + n * 24;
			std::vector<uint8_t> bytes((dfd + 44 + 15) & ~size_t(15), 0);
			std::copy(std::begin(KtxMagic), std::end(KtxMagic), bytes.begin());
			Put(bytes, 12, 157 + image.block * 2);
			Put(bytes, 16, 1);
			Put(bytes, 20, image.levels[0].width);
			Put(bytes, 24, image.levels[0].height);
			Put(bytes, 36, 1);
			Put(bytes, 40, n);
			Put(bytes, 48, dfd);
			Put(bytes, 52, 44);
			Put(bytes, dfd, 44);
			Put(bytes, dfd + 8, 2 | (40u << 16));
			Put(bytes, dfd + 12, 162 | (1u << 8) | (1u << 16)); // ASTC, BT.709 primaries, linear transfer, straight alpha.
			bytes[dfd + 16] = Blocks[image.block][0] - 1;
			bytes[dfd + 17] = Blocks[image.block][1] - 1;
			bytes[dfd + 20] = 16;
			bytes[dfd + 30] = 127;
			Put(bytes, dfd + 40, 0xffffffffu);
			for (size_t i = n; i-- > 0;)
			{
				bytes.resize((bytes.size() + 15) & ~size_t(15), 0);
				Put(bytes, 80 + i * 24, bytes.size(), 8);
				Put(bytes, 88 + i * 24, image.levels[i].data.size(), 8);
				Put(bytes, 96 + i * 24, image.levels[i].data.size(), 8);
				bytes.insert(bytes.end(), image.levels[i].data.begin(), image.levels[i].data.end());
			}
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			out.close();
			if (!out)
				return Fail(error, "Unable to write optimized texture");
			return true;
		}
	} // namespace

	bool Read(const std::string& path, Image& image, bool base_only, std::string& error)
	{
		image = {};
		error.clear();
		try
		{
			std::ifstream in(path, std::ios::binary | std::ios::ate);
			if (!in || in.tellg() < 16 || in.tellg() > std::streamoff(MaxBytes))
				return Fail(error, "Invalid texture file size");
			std::vector<uint8_t> bytes(static_cast<size_t>(in.tellg()));
			in.seekg(0);
			if (!in.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
				return Fail(error, "Texture read failed");
			if (!std::memcmp(bytes.data(), KtxMagic, 12))
				return ReadKtx(bytes, image, base_only, error);
			if (U32(bytes.data()) == 0x20534444)
				return ReadDDS(bytes, image, base_only, error);
			if (png_sig_cmp(bytes.data(), 0, 8))
				return Fail(error, "Unsupported texture file");
			png_image png{};
			png.version = PNG_IMAGE_VERSION;
			if (!png_image_begin_read_from_memory(&png, bytes.data(), bytes.size()))
				return Fail(error, "Invalid PNG");
			const bool alpha = (png.format & PNG_FORMAT_FLAG_ALPHA) != 0;
			if (!Dimensions(png.width, png.height))
			{
				png_image_free(&png);
				return Fail(error, "PNG exceeds conversion memory limit");
			}
			png.format = PNG_FORMAT_RGBA;
			Level level{png.width, png.height, std::vector<uint8_t>(size_t(png.width) * png.height * 4)};
			bool ok = png_image_finish_read(&png, nullptr, level.data.data(), 0, nullptr) != 0;
			png_image_free(&png);
			if (!ok)
				return Fail(error, "PNG decode failed");
			if (!alpha)
				for (size_t i = 3; i < level.data.size(); i += 4)
					level.data[i] = 128;
			image.levels.push_back(std::move(level));
			return true;
		}
		catch (const std::exception& e)
		{
			error = e.what();
			image = {};
			return false;
		}
	}

	bool Decode(Image& image, std::string& error)
	{
		try
		{
			if (image.block < 0)
				return true;
			if (image.block >= 14)
				return Fail(error, "Invalid ASTC block size");
			size_t memory = 0;
			for (const auto& level : image.levels)
			{
				if (!Dimensions(level.width, level.height) || level.data.size() != BlockBytes(level.width, level.height, image.block))
					return Fail(error, "Invalid ASTC image dimensions or payload");
				memory += size_t(level.width) * level.height * 4;
				if (memory > MaxBytes)
					return Fail(error, "ASTC mip chain exceeds decode memory limit");
			}
			astcenc_config config{};
			astcenc_context* raw = nullptr;
			if (astcenc_config_init(ASTCENC_PRF_LDR, Blocks[image.block][0], Blocks[image.block][1], 1,
					ASTCENC_PRE_FAST, ASTCENC_FLG_DECOMPRESS_ONLY, &config) != ASTCENC_SUCCESS ||
				astcenc_context_alloc(&config, 1, &raw, nullptr) != ASTCENC_SUCCESS)
				return Fail(error, "ASTC decoder allocation failed");
			Context context(raw, astcenc_context_free);
			for (auto& level : image.levels)
			{
				if (!Dimensions(level.width, level.height))
					return Fail(error, "ASTC dimensions exceed memory limit");
				std::vector<uint8_t> pixels(size_t(level.width) * level.height * 4);
				void* slice = pixels.data();
				astcenc_image target{level.width, level.height, 1, ASTCENC_TYPE_U8, &slice};
				auto result = astcenc_decompress_image(raw, level.data.data(), level.data.size(), &target, &Swizzle, 0);
				if (result != ASTCENC_SUCCESS)
					return Fail(error, "ASTC decoding failed");
				astcenc_decompress_reset(raw);
				level.data = std::move(pixels);
			}
			image.block = -1;
			return true;
		}
		catch (const std::exception& e)
		{
			error = e.what();
			image = {};
			return false;
		}
	}

	bool Convert(const std::string& source, const std::string& destination, int block, unsigned threads, std::string& error)
	{
		try
		{
			if (block < 0 || block >= 14)
				return Fail(error, "Invalid ASTC quality selection");
			Image image;
			if (!Read(source, image, false, error))
				return false;
			if (image.block >= 0)
				return Fail(error, "Already compressed; use the original to change quality");
			// The dumper emits sidecars rather than embedding mipmaps. Never invent a
			// missing level or accept a differently scaled sidecar.
			if (image.levels.size() == 1)
			{
				const std::filesystem::path path(source);
				if (path.stem().string().find("-mip") == std::string::npos)
				{
					const auto width = image.levels[0].width, height = image.levels[0].height;
					size_t memory = image.levels[0].data.size();
					for (unsigned mip = 1; mip < MipCount(width, height); mip++)
					{
						const auto sidecar = path.parent_path() / (path.stem().string() + "-mip" + std::to_string(mip) + path.extension().string());
						if (!std::filesystem::is_regular_file(sidecar))
							break;
						Image side;
						if (!Read(sidecar.string(), side, true, error) || side.block >= 0 ||
							side.levels[0].width != std::max(1u, width >> mip) || side.levels[0].height != std::max(1u, height >> mip))
							return Fail(error, "Mip sidecar dimensions do not match the base texture");
						memory += side.levels[0].data.size();
						if (memory > MaxBytes)
							return Fail(error, "Mip chain exceeds conversion memory limit");
						image.levels.push_back(std::move(side.levels[0]));
					}
				}
			}
			threads = std::clamp(threads, 1u, 4u);
			astcenc_config config{};
			astcenc_context* raw = nullptr;
			if (astcenc_config_init(ASTCENC_PRF_LDR, Blocks[block][0], Blocks[block][1], 1, ASTCENC_PRE_FAST, 0, &config) != ASTCENC_SUCCESS ||
				astcenc_context_alloc(&config, threads, &raw, nullptr) != ASTCENC_SUCCESS)
				return Fail(error, "ASTC encoder allocation failed");
			Context context(raw, astcenc_context_free);
			for (auto& level : image.levels)
			{
				std::vector<uint8_t> compressed(BlockBytes(level.width, level.height, block));
				void* slice = level.data.data();
				astcenc_image input{level.width, level.height, 1, ASTCENC_TYPE_U8, &slice};
				std::vector<std::thread> workers;
				std::vector<astcenc_error> results(threads, ASTCENC_SUCCESS);
				auto encode = [&](unsigned i) { results[i] = astcenc_compress_image(raw, &input, &Swizzle, compressed.data(), compressed.size(), i); };
				workers.reserve(threads - 1);
				std::mutex gate;
				std::condition_variable ready;
				bool start = false, abort = false;
				try
				{
					for (unsigned i = 1; i < threads; i++)
						workers.emplace_back([&, i] {
							{
								std::unique_lock lock(gate);
								ready.wait(lock, [&] { return start; });
								if (abort)
									return;
							}
							encode(i);
						});
				}
				catch (...)
				{
					{
						std::lock_guard lock(gate);
						abort = true;
						start = true;
					}
					ready.notify_all();
					for (auto& worker : workers)
						worker.join();
					return Fail(error, "Unable to start ASTC encoder workers");
				}
				{
					std::lock_guard lock(gate);
					start = true;
				}
				ready.notify_all();
				encode(0);
				for (auto& worker : workers)
					worker.join();
				if (std::any_of(results.begin(), results.end(), [](auto r) { return r != ASTCENC_SUCCESS; }))
					return Fail(error, "ASTC encoding failed");
				astcenc_compress_reset(raw);
				level.data = std::move(compressed);
			}
			image.block = block;
			if (!WriteKtx(destination, image, error))
				return false;
			Image verified;
			return Read(destination, verified, false, error) && verified.block == block && verified.levels.size() == image.levels.size();
		}
		catch (const std::exception& e)
		{
			error = e.what();
			return false;
		}
	}
} // namespace TextureCodec
