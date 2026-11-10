#include "Ps4TextureFixture.h"
#include "graphics/guest_gpu/ps4Raster.h"
#include "graphics/guest_gpu/ps4Transfer.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#if defined(PS4_RASTER_D3D12)
#include "graphics/host_gpu/d3d12/ps4RasterProbe.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#else
#include "graphics/host_gpu/renderer/ps4RasterProbe.h"
#endif
using namespace Libs::Graphics;
static void Require(bool ok, const std::string& text) {
	if (!ok) throw std::runtime_error(text);
}
template <typename T>
static std::vector<T> Read(const std::filesystem::path& file) {
	std::ifstream stream(file, std::ios::binary | std::ios::ate);
	const auto    bytes = stream.tellg();
	Require(bytes > 0 && bytes % sizeof(T) == 0 && bytes < 16 * 1024 * 1024,
	        "Shader fixture missing/invalid: " + file.string());
	std::vector<T> data(static_cast<size_t>(bytes) / sizeof(T));
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(data.data()), bytes);
	Require(bool(stream), "Read fixture");
	return data;
}
static void Save(const std::filesystem::path& path, const Ps4::RasterState& s,
                 const std::vector<uint8_t>& pixels) {
	std::ofstream file(path, std::ios::binary);
	file << "P6\n" << s.width << ' ' << s.height << "\n255\n";
	for (size_t i = 0; i < pixels.size(); i += 4)
		file.write(reinterpret_cast<const char*>(pixels.data() + i), 3);
	Require(bool(file), "Write raster image");
}
template <typename Run>
static void SampledRasterTests(Run run, const std::filesystem::path& folder, const char* backend) {
	for (int variant = 59; variant <= 64; ++variant) {
		const bool indirect = variant >= 62;
		const int scene = indirect ? variant - 3 : variant;
		const bool changed = scene == 60;
		auto payload = Ps4TextureTests::Make(0, 0, changed);
		std::array<float, 4> vertex_mod = changed ? std::array<float, 4> {.5f, .75f, 1, 1}
		                                             : std::array<float, 4> {.75f, .5f, .25f, 1};
		if (scene == 61) vertex_mod = {-1, 2, 1, 1}; // UV outside [0,1], clamp at both edges
		const std::array<float, 4> pixel_mod = changed ? std::array<float, 4> {.75f, .5f, .25f, 1}
		                                                  : std::array<float, 4> {1, .75f, .5f, 1};
		const std::array<float, 4> vertex_second {.5f, 1, 1, 1};
		const std::array<float, 4> pixel_second {.5f, 1, .75f, 1};
		Ps4::RasterState state;
		if (indirect) {
			state.vertex_buffer2 = Ps4TextureTests::Bytes(vertex_second);
			state.pixel_buffer2 = Ps4TextureTests::Bytes(pixel_second);
		}
		state.vertex_buffer = Ps4TextureTests::Bytes(vertex_mod);
		state.pixel_buffer = Ps4TextureTests::Bytes(pixel_mod);
		state.vertex_texture = {8, 4, 256, payload.vs_image};
		state.pixel_texture = {8, 4, 256, payload.ps_image};
		std::string error;
		std::vector<uint8_t> pixels;
		Require(run(variant, state, pixels, error), error);
		Require(pixels.size() == 64 * 32 * 4, "sampled raster output size");
		for (uint32_t y = 0; y < 32; ++y)
			for (uint32_t x = 0; x < 64; ++x) {
				const float u = (float(x) + .5f - 8) / 48, v = (float(y) + .5f - 4) / 24;
				const bool inside = u >= 0 && v >= 0 && u + v < 1;
				const float weights[] {1 - u - v, u, v};
				const size_t vertex_texels[] {0, 7 * 4, 3 * 256};
				float uv[2] {};
				for (uint32_t c = 0; c < 2; ++c)
					for (uint32_t n = 0; n < 3; ++n)
						uv[c] += weights[n] * payload.vs_image[vertex_texels[n] + c] / 255.f * vertex_mod[c] *
						         (indirect ? vertex_second[c] : 1.f);
				const auto col = uint32_t(std::clamp(std::floor(uv[0] * 8), 0.f, 7.f));
				const auto row = uint32_t(std::clamp(std::floor(uv[1] * 4), 0.f, 3.f));
				for (uint32_t c = 0; c < 4; ++c) {
					const float expected = inside ? payload.ps_image[row * 256 + col * 4 + c] * pixel_mod[c] *
					                                   (indirect ? pixel_second[c] : 1.f)
					                              : (c == 3 ? 255.f : 0.f);
					Require(std::abs(int(pixels[(y * 64 + x) * 4 + c]) - int(std::lround(expected))) <= 1,
					        "VS+PS IMAGE_SAMPLE_L nearest/clamp/pitch/update readback");
				}
			}
		Save(folder / (std::string(backend) + "-sample-" + std::to_string(variant) + ".ppm"), state, pixels);
		std::printf("PS4_RASTER %s sample variant=%d all=2048 PASS\n", backend, variant);
	}
}

template <typename Run>
static void ParameterRasterTests(Run run, const std::filesystem::path& folder, const char* backend) {
	for (int variant = 65; variant <= 69; ++variant) {
		Ps4::RasterState state;
		std::vector<uint8_t> pixels;
		std::string error;
		Require(run(variant, state, pixels, error), error);
		Require(pixels.size() == 64 * 32 * 4, "PARAM raster output size");
		for (uint32_t y = 0; y < 32; ++y)
			for (uint32_t x = 0; x < 64; ++x) {
				const float u = (float(x) + .5f - 8) / 48, v = (float(y) + .5f - 4) / 24;
				const bool inside = u >= 0 && v >= 0 && u + v < 1;
				for (uint32_t c = 0; c < 4; ++c) {
					float expected = c == 3 ? 255.f : 0.f;
					if (inside && variant == 65) expected = c == 3 ? 255.f : c == 2 ? 63.75f : 0.f;
					if (inside && variant >= 66) {
						const uint32_t selector = uint32_t(variant - 66);
						expected = (c == 3 ? (selector & 1) : (selector & 2)) ? 255.f : 0.f;
					}
					Require(std::abs(int(pixels[(y * 64 + x) * 4 + c]) - int(std::lround(expected))) <= 1,
					        "flat first provoking vertex/default PARAM readback");
				}
			}
		Save(folder / (std::string(backend) + "-param-" + std::to_string(variant) + ".ppm"), state, pixels);
		std::printf("PS4_RASTER %s PARAM variant=%d all=2048 PASS\n", backend, variant);
	}
}

template <typename Run>
static void ExternalFetchRasterTests(Run run, const std::filesystem::path& folder, const char* backend) {
	for (int variant = 70; variant <= 73; ++variant) {
		const bool indexed = variant >= 72;
		const bool changed = variant == 71 || variant == 73;
		auto data = Ps4TextureTests::Make(0, 0, changed);
		if (indexed) {
			data.positions[12] = data.positions[13] = changed ? .5f : .75f;
		}
		const std::array<uint16_t, 3> indices16 {1, 3, 2};
		const std::array<uint32_t, 3> indices32 {1, 3, 2};
		Ps4::RasterState state;
		if (indexed) {
			state.index_bytes = variant == 72 ? 2 : 4;
			state.indices = variant == 72 ? Ps4TextureTests::Bytes(indices16) : Ps4TextureTests::Bytes(indices32);
		}
		state.vertex_buffer = Ps4TextureTests::Bytes(data.positions);
		state.vertex_buffer2 = Ps4TextureTests::Bytes(data.colors);
		std::vector<uint8_t> pixels;
		std::string error;
		Require(run(variant, state, pixels, error), error);
		Require(pixels.size() == 64 * 32 * 4, "external fetch raster output size");
		for (uint32_t y = 0; y < 32; ++y)
			for (uint32_t x = 0; x < 64; ++x) {
				const float u = (float(x) + .5f - (changed ? 16 : 8)) / (changed ? 32 : 48);
				const float v = (float(y) + .5f - (changed ? 8 : 4)) / (changed ? 16 : 24);
				const bool inside = u >= 0 && v >= 0 && u <= 1 && v <= 1 &&
				                    (indexed ? u + v > 1 : u + v < 1);
				const float weights[] {indexed ? 1 - v : 1 - u - v,
				                       indexed ? u + v - 1 : u, indexed ? 1 - u : v};
				for (uint32_t c = 0; c < 4; ++c) {
					float expected = c == 3 ? 255.f : 0.f;
					if (inside) {
						expected = 0;
						for (uint32_t n = 0; n < 3; ++n) expected += weights[n] * data.colors[(indexed ? indices32[n] : n) * 4 + c] * 255;
					}
					Require(std::abs(int(pixels[(y * 64 + x) * 4 + c]) - int(std::lround(expected))) <= 1,
					        "external MTBUF fetch position/color update readback");
				}
			}
		Save(folder / (std::string(backend) + "-external-fetch-" + std::to_string(variant) + ".ppm"), state, pixels);
		std::printf("PS4_RASTER %s external fetch variant=%d all=2048 PASS\n", backend, variant);
	}
}

template <typename Run>
static void MaskRasterTests(Run run, const std::filesystem::path& folder, const char* backend) {
	for (int variant = 74; variant <= 76; ++variant) {
		Ps4::RasterState state;
		std::vector<uint8_t> pixels;
		std::string error;
		Require(run(variant, state, pixels, error), error);
		Require(pixels.size() == 64 * 32 * 4, "masked raster output size");
		for (uint32_t y = 0; y < 32; ++y)
			for (uint32_t x = 0; x < 64; ++x) {
				const float u = (float(x) + .5f - 8) / 48, v = (float(y) + .5f - 4) / 24;
				const bool inside = u >= 0 && v >= 0 && u + v < 1;
				const bool replaced = u < .5f && (variant != 75 || v < .5f);
				const float shade[] {replaced ? .75f : u, v, .25f, 1};
				for (uint32_t c = 0; c < 4; ++c) {
					const float expected = inside ? shade[c] * 255 : (c == 3 ? 255.f : 0.f);
					Require(std::abs(int(pixels[(y * 64 + x) * 4 + c]) - int(std::lround(expected))) <= 1,
					        "VCC/EXEC inactive lane preservation/nesting/select readback");
				}
			}
		Save(folder / (std::string(backend) + "-mask-" + std::to_string(variant) + ".ppm"), state, pixels);
		std::printf("PS4_RASTER %s lane mask variant=%d all=2048 PASS\n", backend, variant);
	}
}

template <typename Run>
static void ElseRasterTests(Run run, const std::filesystem::path& folder, const char* backend) {
	for (int variant = 77; variant <= 81; ++variant) {
		Ps4::RasterState state;
		std::vector<uint8_t> pixels;
		std::string error;
		Require(run(variant, state, pixels, error), error);
		Require(pixels.size() == 64 * 32 * 4, "else raster output size");
		for (uint32_t y = 0; y < 32; ++y)
			for (uint32_t x = 0; x < 64; ++x) {
				const float u = (float(x) + .5f - 8) / 48, v = (float(y) + .5f - 4) / 24;
				const bool inside = u >= 0 && v >= 0 && u + v < 1;
				float red = u < .5f ? .75f : .125f;
				if (variant == 79) red = .125f; // then empty
				if (variant == 80) red = .75f; // else empty
				if (variant == 81) red = u < .5f ? (v < .5f ? .75f : .125f) : .375f;
				const float shade[] {red, v, .25f, 1};
				for (uint32_t c = 0; c < 4; ++c) {
					const float expected = inside ? shade[c] * 255 : (c == 3 ? 255.f : 0.f);
					Require(std::abs(int(pixels[(y * 64 + x) * 4 + c]) - int(std::lround(expected))) <= 1,
					        "EXEC else/guard/empty/nested reconvergence readback");
				}
			}
		Save(folder / (std::string(backend) + "-else-" + std::to_string(variant) + ".ppm"), state, pixels);
		std::printf("PS4_RASTER %s else variant=%d all=2048 PASS\n", backend, variant);
	}
}

template <typename Run>
static void IntegerCmpxRasterTests(Run run, const std::filesystem::path& folder, const char* backend) {
	for (int variant = 82; variant <= 88; ++variant) {
		const uint32_t mode = uint32_t(variant - 82);
		Ps4::RasterState state;
		std::vector<uint8_t> pixels;
		std::string error;
		Require(run(variant, state, pixels, error), error);
		Require(pixels.size() == 64 * 32 * 4, "integer CMPX raster output size");
		for (uint32_t y = 0; y < 32; ++y)
			for (uint32_t x = 0; x < 64; ++x) {
				const float u = (float(x) + .5f - 8) / 48, v = (float(y) + .5f - 4) / 24;
				const bool inside = u >= 0 && v >= 0 && u + v < 1;
				const uint32_t raw = (inside ? uint32_t(std::floor(u * 8)) : 0u) << 29;
				const int64_t signed_value = raw & 0x80000000u ? int64_t(raw) - 0x100000000ll : int64_t(raw);
				const bool signed_predicate = signed_value < 0x40000000ll;
				const bool unsigned_predicate = raw < 0x40000000u;
				bool active = mode == 1 ? unsigned_predicate : signed_predicate;
				if (mode >= 2) {
					const uint32_t kind = mode - 2;
					const bool predicate = kind == 1 ? false : kind == 2 ? true :
					    kind == 3 ? signed_predicate : kind == 4 ? unsigned_predicate : v < .5f;
					active = u < .75f && predicate;
				}
				const float shade[] {active ? .75f : .125f, v,
				    mode < 2 ? .25f : active ? .875f : .5f, 1};
				for (uint32_t c = 0; c < 4; ++c) {
					const float expected = inside ? shade[c] * 255 : (c == 3 ? 255.f : 0.f);
					Require(std::abs(int(pixels[(y * 64 + x) * 4 + c]) - int(std::lround(expected))) <= 1,
					        "I32/U32 signedness or CMPX VCC+EXEC/inactive-lane readback");
				}
			}
		Save(folder / (std::string(backend) + "-icmp-" + std::to_string(variant) + ".ppm"), state, pixels);
		std::printf("PS4_RASTER %s integer/CMPX variant=%d all=2048 PASS\n", backend, variant);
	}
}

template <typename Run>
static void DivergentExitRasterTests(Run run, const std::filesystem::path& folder, const char* backend) {
	for (int variant = 89; variant <= 92; ++variant) {
		Ps4::RasterState state;
		std::vector<uint8_t> pixels;
		std::string error;
		Require(run(variant, state, pixels, error), error);
		Require(pixels.size() == 64 * 32 * 4, "divergent exit raster output size");
		for (uint32_t y = 0; y < 32; ++y)
			for (uint32_t x = 0; x < 64; ++x) {
				const float u = (float(x) + .5f - 8) / 48, v = (float(y) + .5f - 4) / 24;
				const bool inside = u >= 0 && v >= 0 && u + v < 1;
				const float red = u < .5f ? .75f : (variant == 91 ? .875f : u);
				const float shade[] {red, v, variant % 2 ? .375f : .625f, 1.f};
				for (uint32_t c = 0; c < 4; ++c) {
					const float expected = inside ? shade[c] * 255 : (c == 3 ? 255.f : 0.f);
					Require(std::abs(int(pixels[(y * 64 + x) * 4 + c]) - int(std::lround(expected))) <= 1,
					        "divergent exit selection, EXEC restoration or inactive-lane preservation");
				}
			}
		Save(folder / (std::string(backend) + "-cfg-exit-" + std::to_string(variant - 89) + ".ppm"), state, pixels);
		std::printf("PS4_RASTER %s divergent CFG exit=%d all=2048 PASS\n", backend, variant - 89);
	}
}

template <typename Run>
static void RasterTests(Run run, const std::filesystem::path& folder, const char* backend) {
	for (int variant = 0; variant < 59; ++variant) {
		Ps4::RasterState state;
		if (variant == 1) {
			state.left   = 7;
			state.top    = 3;
			state.right  = 59;
			state.bottom = 27;
		}
		if (variant == 2) {
			state.alpha_blend = true;
			state.write_mask  = 5;
			state.clear[0]    = .125f;
			state.clear[1]    = .25f;
			state.clear[2]    = .375f;
		}
		std::vector<uint8_t> initial;
		if (variant == 3) {
			initial.resize(size_t(state.width) * state.height * 4);
			for (uint32_t y = 0; y < state.height; ++y)
				for (uint32_t x = 0; x < state.width; ++x) {
					auto* p = initial.data() + (size_t(y) * state.width + x) * 4;
					p[0]    = uint8_t(x * 3);
					p[1]    = uint8_t(y * 5);
					p[2]    = 32;
					p[3]    = 255;
				}
			state.initial_pixels = initial;
		}
		std::vector<uint8_t> shader_data;
		if (variant == 10 || variant == 11 || variant == 13) {
			shader_data.resize(64);
			const float data[] = {1,
			                      0,
			                      0,
			                      1,
			                      variant == 11 ? .75f : .25f,
			                      variant == 11 ? .25f : .5f,
			                      variant == 11 ? .5f : .75f,
			                      1};
			std::memcpy(shader_data.data(), data, sizeof(data));
			if (variant == 10 || variant == 11) {
				const uint32_t byte_offset = variant == 11 ? 19u : 17u;
				std::memcpy(shader_data.data(), &byte_offset, sizeof(byte_offset));
			}
			if (variant == 13) {
				const uint32_t signed_input   = uint32_t(-3);
				const float    negative_input = -1.75f;
				std::memcpy(shader_data.data(), &signed_input, 4);
				std::memcpy(shader_data.data() + 4, &negative_input, 4);
				std::memcpy(shader_data.data() + 8, &negative_input, 4);
			}
			state.pixel_buffer = shader_data;
		}
		std::vector<uint8_t> second_data;
		if (variant >= 15) {
			shader_data.assign(variant >= 20 && variant <= 21 ? 64 : 16, 0);
			second_data.assign(64, 0);
			if (variant == 20 || variant == 21) {
				const float positions[] = {-.75f, -.75f, 0,     1,    .75f, -.75f,
				                           0,     1,     -.75f, .75f, 0,    1};
				const float smaller[]   = {-.5f, -.5f, 0, 1, .5f, -.5f, 0, 1, -.5f, .5f, 0, 1};
				const float colors[]    = {0, 0, .25f, 1, 1, 0, .25f, 1, 0, 1, .25f, 1};
				const float changed[]   = {.25f, 0, 0, 1, .25f, 0, 1, 1, .25f, 1, 0, 1};
				std::memcpy(shader_data.data(), variant == 21 ? smaller : positions,
				            sizeof(positions));
				std::memcpy(second_data.data(), variant == 21 ? changed : colors, sizeof(colors));
			} else {
				const uint32_t index = variant == 15                    ? 1
				                       : variant == 16 || variant == 22 ? 2
				                       : variant == 17                  ? 4
				                       : variant == 18                  ? 0x10000000u
				                                                        : UINT32_MAX;
				std::memcpy(shader_data.data(), &index, 4);
				const float records[] = {1,    0,    1,   1, .25f, .5f, .75f, 1,
				                         .75f, .25f, .5f, 1, 0,    1,   0,    1};
				std::memcpy(second_data.data(), records, sizeof(records));
			}
			state.pixel_buffer  = shader_data;
			state.pixel_buffer2 = second_data;
			if (variant == 20 || variant == 21) {
				state.vertex_buffer  = shader_data;
				state.vertex_buffer2 = second_data;
				state.pixel_buffer   = {};
				state.pixel_buffer2  = {};
			}
		}
		Ps4TextureTests::Payload texture_data;
		if (variant >= 23) {
			const uint32_t scene = uint32_t(variant - 23);
			const uint32_t x     = variant == 55   ? 7
			                       : variant == 56 ? 8
			                       : variant == 57 ? 0
			                       : variant == 58 ? UINT32_MAX
			                                       : scene % 8;
			const uint32_t y     = variant == 55   ? 3
			                       : variant >= 56 ? (variant == 57 ? 4u : 0u)
			                                       : scene / 8;
			texture_data         = Ps4TextureTests::Make(x, y, variant == 55);
			state.vertex_buffer  = Ps4TextureTests::Bytes(texture_data.positions);
			state.vertex_buffer2 = Ps4TextureTests::Bytes(texture_data.colors);
			state.pixel_buffer   = Ps4TextureTests::Bytes(texture_data.coordinates);
			state.pixel_buffer2  = Ps4TextureTests::Bytes(texture_data.modulation);
			state.vertex_texture = {8, 4, 256, texture_data.vs_image};
			state.pixel_texture  = {8, 4, 256, texture_data.ps_image};
		}
		std::vector<uint8_t> pixels;
		std::string          error;
		Require(run(variant, state, pixels, error), error);
		Require(pixels.size() == size_t(state.width) * state.height * 4, "Raster output size");
		uint32_t    covered = 0;
		const float color[] {variant == 5 || variant == 11 ? .75f : .25f,
		                     variant == 11 ? .25f : .5f, variant == 11 ? .5f : .75f,
		                     variant == 2 ? .5f : 1.f};
		for (uint32_t y = 0; y < state.height; ++y)
			for (uint32_t x = 0; x < state.width; ++x) {
				const float origin_x = variant == 21 || variant == 55 ? 16.f : 8.f,
				            origin_y = variant == 21 || variant == 55 ? 8.f : 4.f;
				const float extent_x = variant == 21 || variant == 55 ? 32.f : 48.f,
				            extent_y = variant == 21 || variant == 55 ? 16.f : 24.f;
				const bool triangle =
				    variant == 1 || (float(x) >= origin_x && float(y) >= origin_y &&
				                     (float(x) + .5f - origin_x) / extent_x +
				                             (float(y) + .5f - origin_y) / extent_y <
				                         1);

				const bool inside = triangle && x >= state.left && x < state.right &&
				                    y >= state.top && y < state.bottom;
				covered += inside;
				float shade[] {color[0], color[1], color[2], color[3]};
				if ((variant >= 6 && variant < 10) || variant == 12 || variant == 14) {
					const float u = (float(x) + .5f - 8) / 48, v = (float(y) + .5f - 4) / 24;
					const float denominator = variant >= 7 && variant != 14 ? 1 - v * .5f : 1;
					shade[0]                = u / denominator;
					shade[1] = v * (variant >= 7 && variant != 14 ? .5f : 1.f) / denominator;
					shade[2] = .25f;
					if (variant == 9 || variant == 12) std::swap(shade[0], shade[2]);
					if (variant == 8) {
						shade[0] = std::clamp(1 - 2 * std::abs(shade[0]), 0.f, 1.f);
						shade[1] = (shade[1] * .5f + .5f) * .5f;
						shade[2] = std::max(.375f, std::min(.5f, shade[1]));
					}
				}
				if (variant == 13) {
					shade[0] = .5f;
					shade[1] = .5f;
					shade[2] = .75f;
				}
				if (variant == 16 || variant == 22) {
					shade[0] = .75f;
					shade[1] = .25f;
					shade[2] = .5f;
				}
				if (variant >= 17 && variant <= 19)
					for (float& c: shade)
						c = 0;
				if (variant == 20 || variant == 21) {
					const float u = (float(x) + .5f - origin_x) / extent_x,
					            v = (float(y) + .5f - origin_y) / extent_y;
					shade[0]      = variant == 21 ? .25f : u;
					shade[1]      = v;
					shade[2]      = variant == 21 ? u : .25f;
				}
				for (uint32_t channel = 0; channel < 4; ++channel) {
					if (variant >= 23)
						shade[channel] = Ps4TextureTests::Expected(
						    texture_data, (float(x) + .5f - origin_x) / extent_x,
						    (float(y) + .5f - origin_y) / extent_y, channel);
					const auto clear =
					    (state.initial_pixels.empty()
					         ? int(std::lround(state.clear[channel] * 255))
					         : int(initial[(size_t(y) * state.width + x) * 4 + channel]));
					float expected = float(clear);
					if (inside && (state.write_mask & (1u << channel))) {
						expected = shade[channel] * 255;
						if (state.alpha_blend)
							expected = channel == 3 ? expected + clear * (1 - color[3])
							                        : expected * color[3] + clear * (1 - color[3]);
					}
					const int actual = pixels[(size_t(y) * state.width + x) * 4 + channel];
					if (std::abs(actual - int(std::lround(expected))) > 1) {
						std::fprintf(
						    stderr,
						    "Raster %s variant=%d (%u,%u) channel=%u actual=%d expected=%.3f\n",
						    backend, variant, x, y, channel, actual, expected);
						throw std::runtime_error("GCN raster pixel mismatch");
					}
				}
			}
		Require(covered && covered < state.width * state.height,
		        "Test must contain covered and uncovered pixels");
		Save(folder / (std::string(backend) + "-" + std::to_string(variant) + ".ppm"), state,
		     pixels);
		std::printf("PS4_RASTER %s variant=%d covered=%u all=%u scissor/blend/mask PASS\n", backend,
		            variant, covered, state.width * state.height);
		if (variant == 15) {
			const auto saved = pixels;
			auto       bad   = state;
			bad.pixel_buffer = {};
			Require(!run(variant, bad, pixels, error) && pixels == saved,
			        "orphan second buffer changed output");
			bad               = state;
			bad.pixel_buffer2 = std::span<const uint8_t>(second_data).first(10);
			Require(!run(variant, bad, pixels, error) && pixels == saved,
			        "unaligned second buffer changed output");
		}
		const auto saved = pixels;
		state.right      = state.width + 1;
		Require(!run(variant, state, pixels, error) && pixels == saved,
		        "Invalid raster state accepted/modified output");
	}
	// The same staging executor as GNM, with real GPU blending between writes.
	SampledRasterTests(run, folder, backend);
	ParameterRasterTests(run, folder, backend);
	ExternalFetchRasterTests(run, folder, backend);
	MaskRasterTests(run, folder, backend);
	ElseRasterTests(run, folder, backend);
	IntegerCmpxRasterTests(run, folder, backend);
	DivergentExitRasterTests(run, folder, backend);
	for (int variant = 93; variant <= 122; ++variant) {
		const std::array<float, 4> color {.25f, .5f, .75f, 1.f};
		const std::array<uint32_t, 8> data {
		    variant == 95 || variant == 100 ? UINT32_MAX : variant == 99 ? 16u : 17u,
		    0, 0, 0, std::bit_cast<uint32_t>(color[0]),
		    std::bit_cast<uint32_t>(color[1]), std::bit_cast<uint32_t>(color[2]),
		    std::bit_cast<uint32_t>(color[3])};
		Ps4::RasterState state;
		state.pixel_buffer = variant == 93
		    ? std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(color.data()), sizeof(color))
		    : std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data.data()), sizeof(data));
		if (variant >= 96 && variant <= 97)
			state.pixel_buffer = {reinterpret_cast<const uint8_t*>(color.data()), size_t(variant - 95) * 4};
		const std::array<float, 3> weights {.125f, .25f, .625f};
		if (variant == 98) {
			const auto word = [](const float& value) {
				return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&value), 4);
			};
			state.pixel_buffer = word(color[0]);
			state.pixel_buffer2 = word(color[1]);
			state.pixel_buffer3 = word(color[2]);
			state.vertex_buffer = word(weights[0]);
			state.vertex_buffer2 = word(weights[1]);
			state.vertex_buffer3 = word(weights[2]);
		}
		if (variant >= 99)
			state.pixel_buffer2 = {reinterpret_cast<const uint8_t*>(data.data()), sizeof(data)};
		const std::array<uint32_t, 20> table {
		    variant == 119 ? 48u : variant == 120 ? 64u :
		        variant == 101 || variant == 104 || variant == 106 ? 16u :
		        variant == 102 || variant == 105 ? 32u : 0u,
		    0, 0, 0,
		    0x210000, 16u << 16, 1, 0xfac, 0x220000, 16u << 16, 1, 0xfac,
		    0x230000, 16u << 16, 1, 0xfac, 0x240000, 16u << 16, 1, 0xfac};
		const std::array<float, 4> other {.75f, .25f, .5f, 1.f};
		const std::array<float, 4> third_buffer {.5f, .75f, .25f, 1.f}, fourth_buffer {1.f, .5f, .25f, 1.f};
		if ((variant >= 101 && variant <= 106) || variant == 119 || variant == 120) {
			state.pixel_buffer = {reinterpret_cast<const uint8_t*>(table.data()), sizeof(table)};
			state.pixel_buffer2 = {reinterpret_cast<const uint8_t*>(color.data()), sizeof(color)};
			state.pixel_buffer3 = {reinterpret_cast<const uint8_t*>(other.data()), sizeof(other)};
			state.pixel_buffer_tail[0] = {reinterpret_cast<const uint8_t*>(third_buffer.data()), sizeof(third_buffer)};
			state.pixel_buffer_tail[1] = {reinterpret_cast<const uint8_t*>(fourth_buffer.data()), sizeof(fourth_buffer)};
		}
		const float ps_weight = .03125f, vs_weight = .125f;
		std::vector<uint8_t> writable;
		std::vector<uint8_t> storage_readback;
		const uint32_t store_table[] {16, 0, 0, 0,
		    0x210000, 16u << 16, 1, 0xfac, 0x220000, 16u << 16, 1, 0xfac};
		if (variant == 121) {
			writable.resize(64 * 32 * 4);
			storage_readback.resize(writable.size(), 0xcd);
			state.pixel_buffer = writable;
			state.pixel_buffer2 = {};
			state.pixel_storage_writes = true;
			state.storage_readback[0] = storage_readback;
		}
		if (variant == 122) {
			storage_readback.resize(sizeof(store_table), 0xcd);
			state.pixel_buffer = {reinterpret_cast<const uint8_t*>(store_table), sizeof(store_table)};
			state.pixel_buffer2 = {reinterpret_cast<const uint8_t*>(color.data()), sizeof(color)};
			state.pixel_buffer3 = {reinterpret_cast<const uint8_t*>(other.data()), sizeof(other)};
			state.pixel_storage_writes = true;
			state.storage_readback[0] = storage_readback;
			state.left = 16;
			state.top = 8;
			state.right = 17;
			state.bottom = 9;
		}
		if (variant == 118) {
			const std::span<const uint8_t> ps(reinterpret_cast<const uint8_t*>(&ps_weight), 4);
			const std::span<const uint8_t> vs(reinterpret_cast<const uint8_t*>(&vs_weight), 4);
			state.pixel_buffer = state.pixel_buffer2 = state.pixel_buffer3 = ps;
			state.vertex_buffer = state.vertex_buffer2 = state.vertex_buffer3 = vs;
			state.pixel_buffer_tail.fill(ps);
			state.vertex_buffer_tail.fill(vs);
		}
		std::array<std::array<uint8_t, 256>, 4> texels {};
		const uint32_t image_choice = variant >= 114 ? 3 + uint32_t(variant - 114) % 2 :
		    variant >= 108 ? uint32_t(variant - 108) % 3 : 0;
		const uint32_t image_offsets[] {16, 48, 0, 80, 112};
		const uint32_t image_table[] {image_offsets[image_choice], 0, 0, 0,
		    0x2100, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0,
		    0x2200, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0,
		    0x2300, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0,
		    0x2400, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0};
		const std::array<float, 4> third {.5f, .75f, .25f, 1.f}, fourth {1.f, .5f, .25f, 1.f};
		if (variant == 107) {
			texels[0] = {64, 0, 191, 255};
			texels[1] = {0, 0, 0, 0, 0, 128, 0, 0};
			texels[2] = {64, 0, 0, 0};
			texels[3] = {191, 0, 0, 0};
			state.pixel_buffer = state.pixel_buffer2 = state.pixel_buffer3 = {};
			state.pixel_texture = {1, 1, 256, texels[0]};
			state.pixel_texture2 = {2, 1, 256, texels[1]};
			state.vertex_texture = {1, 1, 256, texels[2]};
			state.vertex_texture2 = {2, 1, 256, texels[3]};
		}
		if (variant >= 108 && variant <= 117) {
			texels[0] = {64, 128, 191, 255};
			texels[1] = {191, 64, 128, 255};
			texels[2] = {128, 191, 64, 255};
			texels[3] = {255, 128, 64, 255};
			state.pixel_buffer = {reinterpret_cast<const uint8_t*>(image_table), sizeof(image_table)};
			state.pixel_buffer2 = state.pixel_buffer3 = {};
			state.pixel_texture = {1, 1, 256, texels[0]};
			state.pixel_texture2 = {1, 1, 256, texels[1]};
			state.pixel_texture_tail = {Ps4::Texture2D {1, 1, 256, texels[2]},
			                            Ps4::Texture2D {1, 1, 256, texels[3]}};
		}
		std::vector<uint8_t> pixels;
		std::string error;
		Require(run(variant, state, pixels, error), error);
		Require(pixels.size() == 64 * 32 * 4, "body S_LOAD output size");
		for (uint32_t y = 0; y < 32; ++y)
			for (uint32_t x = 0; x < 64; ++x) {
				const bool inside = x >= state.left && x < state.right && y >= state.top && y < state.bottom &&
				    x >= 8 && y >= 4 && (float(x) + .5f - 8) / 48 + (float(y) + .5f - 4) / 24 < 1;
				for (uint32_t c = 0; c < 4; ++c) {
					const float shade = variant == 122 ? other[c] :
					    variant == 119 ? third_buffer[c] : variant == 120 ? fourth_buffer[c] :
					    variant >= 108 && variant <= 117 ? (image_choice == 4 ? fourth[c] :
					    image_choice == 3 ? third[c] : image_choice == 2 ? 0.f :
					    image_choice == 1 ? other[c] : color[c]) :
					    variant == 95 || variant == 100 || variant == 103 ? 0.f :
					    variant == 106 && c == 0 ? color[0] + other[0] :
					    variant == 102 || variant == 105 || variant == 106 ? other[c] :
					        (variant >= 96 && variant <= 97 && c == 2 ? .5f : color[c]);
					const float expected = inside ? shade : (c == 3 ? 1.f : 0.f);
					Require(std::abs(int(pixels[(y * 64 + x) * 4 + c]) -
					                 int(std::lround(expected * 255))) <= 1, "body S_LOAD pixel");
					if (variant == 121) {
						const uint8_t rgba[] {64, 128, 191, 255};
						Require(storage_readback[(y * 64 + x) * 4 + c] == (inside ? rgba[c] : 0),
						        "PS4 shader storage readback");
					}
				}
			}
		std::printf("PS4_RASTER %s body S_LOAD variant=%d all=2048 PASS\n", backend, variant);
		if (variant == 122) {
			auto expected = std::array<uint32_t, std::size(store_table)> {};
			std::copy(std::begin(store_table), std::end(store_table), expected.begin());
			expected[0] = 32;
			Require(std::memcmp(storage_readback.data(), expected.data(), sizeof(expected)) == 0,
			        "GCN SRT selector readback");
		}
	}
	Ps4::RasterState state;
	state.alpha_blend = true;
	const Ps4::LinearTarget target {0x8000, state.width, state.height, state.width * 4};
	std::vector<uint8_t>    pixels(size_t(target.pitch) * target.height);
	std::string             error;
	Ps4::TransferPlan       plan;
	const uint32_t          fill[] = {
        0xc0055000u, 0xc0000000u, 0xff1e140au, 0, 0x8000, 0, uint32_t(pixels.size())};
	Require(Ps4::DecodeTransfers(fill, target, plan, error) &&
	            Ps4::ApplyTransfers(plan, pixels, error),
	        error);
	state.initial_pixels = pixels;
	std::vector<uint8_t> first;
	Require(run(2, state, first, error), error);
	const uint32_t writes[] = {0xc0033700, 0x00100500, 0x8000 + (8 * 64 + 16) * 4,  0, 0xff000000,
	                           0xc0033700, 0x00100500, 0x8000 + (30 * 64 + 60) * 4, 0, 0xff665544};
	Require(Ps4::DecodeTransfers(writes, target, plan, error) &&
	            Ps4::ApplyTransfers(plan, first, error),
	        error);
	const auto saved             = first;
	auto       invalid           = plan;
	invalid.writes.back().offset = uint32_t(first.size());
	Require(!Ps4::ApplyTransfers(invalid, first, error) && first == saved,
	        "invalid transfer modified snapshot");
	Require(!Ps4::ApplyTransfers(plan, std::span<uint8_t>(first).first(first.size() - 1), error) &&
	            first == saved,
	        "invalid transfer span modified snapshot");
	state.initial_pixels = first;
	Require(run(2, state, pixels, error), error);
	for (uint32_t y = 0; y < state.height; ++y)
		for (uint32_t x = 0; x < state.width; ++x) {
			const bool inside =
			    x >= 8 && y >= 4 && (float(x) + .5f - 8) / 48 + (float(y) + .5f - 4) / 24 < 1;
			const float source[]     = {.25f, .5f, .75f, 1};
			const int   background[] = {10, 20, 30, 255};
			for (uint32_t c = 0; c < 4; ++c) {
				float expected = background[c];
				if (inside && c < 3) expected = .75f * source[c] * 255 + .25f * background[c];
				if (x == 16 && y == 8 && c < 3) expected = .5f * source[c] * 255;
				if (x == 60 && y == 30) expected = (0xff665544u >> (c * 8)) & 255;
				Require(std::abs(int(pixels[(y * state.width + x) * 4 + c]) -
				                 int(std::lround(expected))) <= 1,
				        "mixed transfer/draw GPU result");
			}
		}
	Save(folder / (std::string(backend) + "-mixed.ppm"), state, pixels);
	std::printf("PS4_RASTER %s mixed DMA/WRITE_DATA and two GPU draws all=2048 PASS\n", backend);
}
#if !defined(PS4_RASTER_D3D12)
static std::atomic<unsigned>          validation_errors {0};
static VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                 VkDebugUtilsMessageTypeFlagsEXT,
                                                 const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                 void*) {
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		++validation_errors;
		std::fprintf(stderr, "Vulkan validation: %s\n", data->pMessage);
	}
	return VK_FALSE;
}
#endif
int main(int argc, char** argv) {
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	try {
		const std::filesystem::path folder = argc == 2 ? argv[1] : "ps4-gcn-fixture";
#if defined(PS4_RASTER_D3D12)
		using Microsoft::WRL::ComPtr;
		ComPtr<ID3D12Debug> debug;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
			debug->EnableDebugLayer();
			std::puts("PS4_RASTER D3D12 debug enabled");
		}
		ComPtr<IDXGIFactory4> factory;
		Require(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))), "DXGI factory");
		for (bool warp: {false, true}) {
			ComPtr<IDXGIAdapter> adapter;
			if (warp) Require(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))), "WARP");
			ComPtr<ID3D12Device> device;
			Require(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
			                                    IID_PPV_ARGS(&device))),
			        "D3D12 device");
			ComPtr<IDXGIAdapter1> selected;
			Require(SUCCEEDED(factory->EnumAdapterByLuid(device->GetAdapterLuid(),
			                                             IID_PPV_ARGS(&selected))),
			        "Adapter LUID");
			DXGI_ADAPTER_DESC1 desc;
			selected->GetDesc1(&desc);
			std::printf("PS4_RASTER D3D12 device=%ls\n", desc.Description);
			ComPtr<ID3D12CommandQueue> queue;
			D3D12_COMMAND_QUEUE_DESC   q {};
			q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			Require(SUCCEEDED(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue))), "D3D12 queue");
			RasterTests(
			    [&](int variant, const auto& state, auto& image, auto& error) {
				    const auto vs = Read<uint8_t>(
				        folder / (variant == 118                   ? "gcn-eight.vert.dxil"
				                  : variant == 107                   ? "gcn-two-images.vert.dxil"
				                  : variant == 98                    ? "gcn-three.vert.dxil"
				                  : variant >= 93                    ? "gcn-small.vert.dxil"
				                  : variant >= 74                    ? "gcn-interp.vert.dxil"
				                  : variant >= 70                    ? "gcn-external-fetch.vert.dxil"
				                  : variant >= 65                    ? "gcn-interp.vert.dxil"
				                  : variant >= 62                    ? "gcn-table-sample.vert.dxil"
				                  : variant >= 59                    ? "gcn-sample.vert.dxil"
				                  : variant >= 23                    ? "gcn-texture.vert.dxil"
				                  : variant == 20 || variant == 21 ? "gcn-fetch.vert.dxil"
				                  : variant >= 15                  ? "gcn-small.vert.dxil"
				                  : variant == 14                  ? "gcn-perspective.vert.dxil"
				                  : variant == 12                  ? "gcn-many.vert.dxil"
				                  : variant >= 10                  ? "gcn-small.vert.dxil"
				                  : variant == 9                   ? "gcn-many.vert.dxil"
				                  : variant == 6                   ? "gcn-interp.vert.dxil"
				                  : variant >= 7                   ? "gcn-perspective.vert.dxil"
				                  : variant == 1                   ? "gcn.vert.dxil"
				                                                   : "gcn-small.vert.dxil"));
				    const auto ps = Read<uint8_t>(
				        folder / (variant == 122                   ? "gcn-srt-store.frag.dxil"
				                  : variant == 121                   ? "gcn-raster-storage.frag.dxil"
				                  : variant >= 119                   ? "gcn-mubuf-table.frag.dxil"
				                  : variant == 118                   ? "gcn-eight.frag.dxil"
				                  : variant >= 116 || (variant >= 111 && variant <= 113) ? "gcn-image-srt-sample.frag.dxil"
				                  : variant >= 108                   ? "gcn-image-srt-load.frag.dxil"
				                  : variant == 107                   ? "gcn-two-images.frag.dxil"
				                  : variant == 106                   ? "gcn-mubuf-loop.frag.dxil"
				                  : variant >= 104                   ? "gcn-mubuf-join.frag.dxil"
				                  : variant >= 101                   ? "gcn-mubuf-table.frag.dxil"
				                  : variant >= 99                    ? "gcn-mubuf-soffset.frag.dxil"
				                  : variant == 98                    ? "gcn-three.frag.dxil"
				                  : variant == 96                    ? "gcn-raw-short1.frag.dxil"
				                  : variant == 97                    ? "gcn-raw-short2.frag.dxil"
				                  : variant >= 94                    ? "gcn-raw-sgpr.frag.dxil"
				                  : variant == 93                    ? "gcn-raw-load.frag.dxil"
				                  : variant == 89                    ? "gcn-cfg-exit0.frag.dxil"
				                  : variant == 90                    ? "gcn-cfg-exit1.frag.dxil"
				                  : variant == 91                    ? "gcn-cfg-frame0.frag.dxil"
				                  : variant == 92                    ? "gcn-cfg-frame1.frag.dxil"
				                  : variant == 82                    ? "gcn-icmp0.frag.dxil"
				                  : variant == 83                    ? "gcn-icmp1.frag.dxil"
				                  : variant == 84                    ? "gcn-icmp2.frag.dxil"
				                  : variant == 85                    ? "gcn-icmp3.frag.dxil"
				                  : variant == 86                    ? "gcn-icmp4.frag.dxil"
				                  : variant == 87                    ? "gcn-icmp5.frag.dxil"
				                  : variant == 88                    ? "gcn-icmp6.frag.dxil"
				                  : variant == 77                    ? "gcn-else0.frag.dxil"
				                  : variant == 78                    ? "gcn-else1.frag.dxil"
				                  : variant == 79                    ? "gcn-else2.frag.dxil"
				                  : variant == 80                    ? "gcn-else3.frag.dxil"
				                  : variant == 81                    ? "gcn-else4.frag.dxil"
				                  : variant == 74                    ? "gcn-mask.frag.dxil"
				                  : variant == 75                    ? "gcn-mask-nested.frag.dxil"
				                  : variant == 76                    ? "gcn-select.frag.dxil"
				                  : variant >= 70                    ? "gcn-interp.frag.dxil"
				                  : variant == 65                    ? "gcn-flat.frag.dxil"
				                  : variant == 66                    ? "gcn-default0.frag.dxil"
				                  : variant == 67                    ? "gcn-default1.frag.dxil"
				                  : variant == 68                    ? "gcn-default2.frag.dxil"
				                  : variant == 69                    ? "gcn-default3.frag.dxil"
				                  : variant >= 62                    ? "gcn-table-sample.frag.dxil"
				                  : variant >= 59                    ? "gcn-sample.frag.dxil"
				                  : variant >= 23                    ? "gcn-texture.frag.dxil"
				                  : variant == 20 || variant == 21 ? "gcn-interp.frag.dxil"
				                  : variant == 22                  ? "gcn-indexed-dword.frag.dxil"
				                  : variant >= 15                  ? "gcn-indexed.frag.dxil"
				                  : variant == 14                  ? "gcn-linear.frag.dxil"
				                  : variant == 12                  ? "gcn-remap.frag.dxil"
				                  : variant == 13                  ? "gcn-unary.frag.dxil"
				                  : variant >= 10                  ? "gcn-buffer.frag.dxil"
				                  : variant == 9                   ? "gcn-param3.frag.dxil"
				                  : variant == 8                   ? "gcn-alu.frag.dxil"
				                  : variant >= 6                   ? "gcn-interp.frag.dxil"
				                  : variant == 2                   ? "gcn-alpha.frag.dxil"
				                  : variant == 4                   ? "gcn-uniform0.frag.dxil"
				                  : variant == 5                   ? "gcn-uniform1.frag.dxil"
				                                                   : "gcn.frag.dxil"));
				    return D3D12::RasterizePs4Probe(device.Get(), queue.Get(), state, vs, ps, image,
				                                    error);
			    },
			    folder, warp ? "warp" : "d3d12");
			ComPtr<ID3D12InfoQueue> info;
			if (SUCCEEDED(device.As(&info))) {
				for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
					SIZE_T bytes = 0;
					info->GetMessage(i, nullptr, &bytes);
					std::vector<uint8_t> data(bytes);
					auto*                message = reinterpret_cast<D3D12_MESSAGE*>(data.data());
					Require(SUCCEEDED(info->GetMessage(i, message, &bytes)), "Read debug message");
					Require(message->Severity > D3D12_MESSAGE_SEVERITY_ERROR,
					        std::string("D3D12: ") + message->pDescription);
				}
				std::puts("PS4_RASTER D3D12 debug errors=0");
			}
		}
#else
		uint32_t count = 0;
		vkEnumerateInstanceLayerProperties(&count, nullptr);
		std::vector<VkLayerProperties> layers(count);
		vkEnumerateInstanceLayerProperties(&count, layers.data());
		bool validation = false;
		for (const auto& layer: layers)
			if (std::string_view(layer.layerName) == "VK_LAYER_KHRONOS_validation")
				validation = true;
		const char *layer     = "VK_LAYER_KHRONOS_validation",
		           *extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		VkApplicationInfo app {VK_STRUCTURE_TYPE_APPLICATION_INFO};
		app.apiVersion = VK_API_VERSION_1_2;
		VkDebugUtilsMessengerCreateInfoEXT debug {
		    VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
		debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debug.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debug.pfnUserCallback = Validation;
		VkInstanceCreateInfo ci {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		ci.pApplicationInfo = &app;
		if (validation) {
			ci.enabledLayerCount = ci.enabledExtensionCount = 1;
			ci.ppEnabledLayerNames                          = &layer;
			ci.ppEnabledExtensionNames                      = &extension;
			ci.pNext                                        = &debug;
		}
		VkInstance instance;
		Require(vkCreateInstance(&ci, nullptr, &instance) == VK_SUCCESS, "Vulkan instance");
		VkDebugUtilsMessengerEXT messenger {};
		if (validation) {
			auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
			    vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
			Require(create && create(instance, &debug, nullptr, &messenger) == VK_SUCCESS,
			        "Validation messenger");
		}
		Require(vkEnumeratePhysicalDevices(instance, &count, nullptr) == VK_SUCCESS && count,
		        "Vulkan devices");
		std::vector<VkPhysicalDevice> devices(count);
		vkEnumeratePhysicalDevices(instance, &count, devices.data());
		auto                       physical = devices.front();
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(physical, &props);
		std::printf("PS4_RASTER Vulkan device=%s validation=%u\n", props.deviceName,
		            unsigned(validation));
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
		std::vector<VkQueueFamilyProperties> families(count);
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
		uint32_t family = 0;
		while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
			++family;
		Require(family < count, "Vulkan graphics queue");
		const float             priority = 1;
		VkDeviceQueueCreateInfo qi {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		qi.queueFamilyIndex = family;
		qi.queueCount       = 1;
		qi.pQueuePriorities = &priority;
		VkPhysicalDeviceFeatures features {};
		vkGetPhysicalDeviceFeatures(physical, &features);
		VkDeviceCreateInfo dc {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		dc.queueCreateInfoCount = 1;
		dc.pQueueCreateInfos    = &qi;
		dc.pEnabledFeatures     = &features;
		Vulkan::TransferDevice host {};
		host.family   = family;
		host.get_proc = vkGetDeviceProcAddr;
		Require(vkCreateDevice(physical, &dc, nullptr, &host.device) == VK_SUCCESS,
		        "Vulkan device");
		vkGetDeviceQueue(host.device, family, 0, &host.queue);
		vkGetPhysicalDeviceMemoryProperties(physical, &host.memory);
		try {
			RasterTests(
			    [&](int variant, const auto& state, auto& image, auto& error) {
				    const auto vs = Read<uint32_t>(
				        folder / (variant == 118                   ? "gcn-eight.vert.spv"
				                  : variant == 107                   ? "gcn-two-images.vert.spv"
				                  : variant == 98                    ? "gcn-three.vert.spv"
				                  : variant >= 93                    ? "gcn-small.vert.spv"
				                  : variant >= 74                    ? "gcn-interp.vert.spv"
				                  : variant >= 70                    ? "gcn-external-fetch.vert.spv"
				                  : variant >= 65                    ? "gcn-interp.vert.spv"
				                  : variant >= 62                    ? "gcn-table-sample.vert.spv"
				                  : variant >= 59                    ? "gcn-sample.vert.spv"
				                  : variant >= 23                    ? "gcn-texture.vert.spv"
				                  : variant == 20 || variant == 21 ? "gcn-fetch.vert.spv"
				                  : variant >= 15                  ? "gcn-small.vert.spv"
				                  : variant == 14                  ? "gcn-perspective.vert.spv"
				                  : variant == 12                  ? "gcn-many.vert.spv"
				                  : variant >= 10                  ? "gcn-small.vert.spv"
				                  : variant == 9                   ? "gcn-many.vert.spv"
				                  : variant == 6                   ? "gcn-interp.vert.spv"
				                  : variant >= 7                   ? "gcn-perspective.vert.spv"
				                  : variant == 1                   ? "gcn.vert.spv"
				                                                   : "gcn-small.vert.spv"));
				    const auto ps = Read<uint32_t>(
				        folder / (variant == 122                   ? "gcn-srt-store.frag.spv"
				                  : variant == 121                   ? "gcn-raster-storage.frag.spv"
				                  : variant >= 119                   ? "gcn-mubuf-table.frag.spv"
				                  : variant == 118                   ? "gcn-eight.frag.spv"
				                  : variant >= 116 || (variant >= 111 && variant <= 113) ? "gcn-image-srt-sample.frag.spv"
				                  : variant >= 108                   ? "gcn-image-srt-load.frag.spv"
				                  : variant == 107                   ? "gcn-two-images.frag.spv"
				                  : variant == 106                   ? "gcn-mubuf-loop.frag.spv"
				                  : variant >= 104                   ? "gcn-mubuf-join.frag.spv"
				                  : variant >= 101                   ? "gcn-mubuf-table.frag.spv"
				                  : variant >= 99                    ? "gcn-mubuf-soffset.frag.spv"
				                  : variant == 98                    ? "gcn-three.frag.spv"
				                  : variant == 96                    ? "gcn-raw-short1.frag.spv"
				                  : variant == 97                    ? "gcn-raw-short2.frag.spv"
				                  : variant >= 94                    ? "gcn-raw-sgpr.frag.spv"
				                  : variant == 93                    ? "gcn-raw-load.frag.spv"
				                  : variant == 89                    ? "gcn-cfg-exit0.frag.spv"
				                  : variant == 90                    ? "gcn-cfg-exit1.frag.spv"
				                  : variant == 91                    ? "gcn-cfg-frame0.frag.spv"
				                  : variant == 92                    ? "gcn-cfg-frame1.frag.spv"
				                  : variant == 82                    ? "gcn-icmp0.frag.spv"
				                  : variant == 83                    ? "gcn-icmp1.frag.spv"
				                  : variant == 84                    ? "gcn-icmp2.frag.spv"
				                  : variant == 85                    ? "gcn-icmp3.frag.spv"
				                  : variant == 86                    ? "gcn-icmp4.frag.spv"
				                  : variant == 87                    ? "gcn-icmp5.frag.spv"
				                  : variant == 88                    ? "gcn-icmp6.frag.spv"
				                  : variant == 77                    ? "gcn-else0.frag.spv"
				                  : variant == 78                    ? "gcn-else1.frag.spv"
				                  : variant == 79                    ? "gcn-else2.frag.spv"
				                  : variant == 80                    ? "gcn-else3.frag.spv"
				                  : variant == 81                    ? "gcn-else4.frag.spv"
				                  : variant == 74                    ? "gcn-mask.frag.spv"
				                  : variant == 75                    ? "gcn-mask-nested.frag.spv"
				                  : variant == 76                    ? "gcn-select.frag.spv"
				                  : variant >= 70                    ? "gcn-interp.frag.spv"
				                  : variant == 65                    ? "gcn-flat.frag.spv"
				                  : variant == 66                    ? "gcn-default0.frag.spv"
				                  : variant == 67                    ? "gcn-default1.frag.spv"
				                  : variant == 68                    ? "gcn-default2.frag.spv"
				                  : variant == 69                    ? "gcn-default3.frag.spv"
				                  : variant >= 62                    ? "gcn-table-sample.frag.spv"
				                  : variant >= 59                    ? "gcn-sample.frag.spv"
				                  : variant >= 23                    ? "gcn-texture.frag.spv"
				                  : variant == 20 || variant == 21 ? "gcn-interp.frag.spv"
				                  : variant == 22                  ? "gcn-indexed-dword.frag.spv"
				                  : variant >= 15                  ? "gcn-indexed.frag.spv"
				                  : variant == 14                  ? "gcn-linear.frag.spv"
				                  : variant == 12                  ? "gcn-remap.frag.spv"
				                  : variant == 13                  ? "gcn-unary.frag.spv"
				                  : variant >= 10                  ? "gcn-buffer.frag.spv"
				                  : variant == 9                   ? "gcn-param3.frag.spv"
				                  : variant == 8                   ? "gcn-alu.frag.spv"
				                  : variant >= 6                   ? "gcn-interp.frag.spv"
				                  : variant == 2                   ? "gcn-alpha.frag.spv"
				                  : variant == 4                   ? "gcn-uniform0.frag.spv"
				                  : variant == 5                   ? "gcn-uniform1.frag.spv"
				                                                   : "gcn.frag.spv"));
				    return Vulkan::RasterizePs4Probe(host, state, vs, ps, image, error);
			    },
			    folder, "vulkan");
		} catch (...) {
			vkDestroyDevice(host.device, nullptr);
			throw;
		}
		vkDestroyDevice(host.device, nullptr);
		if (messenger)
			reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(
			    instance, "vkDestroyDebugUtilsMessengerEXT"))(instance, messenger, nullptr);
		vkDestroyInstance(instance, nullptr);
		Require(validation_errors == 0, "Vulkan validation errors");
		std::printf("PS4_RASTER Vulkan validation errors=%u\n", validation_errors.load());
#endif
		return 0;
	} catch (const std::exception& e) {
		std::fprintf(stderr, "PS4_RASTER FAIL: %s\n", e.what());
		return 1;
	}
}
