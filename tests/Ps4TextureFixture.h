#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
namespace Ps4TextureTests {
template <class T>
inline std::span<const uint8_t> Bytes(const T& values) {
	return {reinterpret_cast<const uint8_t*>(values.data()), values.size() * sizeof(values[0])};
}
struct Payload {
	std::array<float, 16>   positions {}, colors {}, modulation {};
	std::array<uint32_t, 4> coordinates {};
	std::vector<uint8_t>    vs_image, ps_image;
};
inline Payload Make(uint32_t x, uint32_t y, bool changed = false) {
	Payload p;
	p.positions = changed ? std::array<float, 16> {-.5f, -.5f, 0, 1, .5f, -.5f, 0, 1,
	                                               -.5f, .5f,  0, 1, 0,   0,    0, 1}
	                      : std::array<float, 16> {-.75f, -.75f, 0, 1, .75f, -.75f, 0, 1,
	                                               -.75f, .75f,  0, 1, 0,    0,     0, 1};
	p.colors =
	    changed ? std::array<float, 16> {.25f, 0, 0, 1, .25f, 0, 1, 1, .25f, 1, 0, 1, 0, 0, 0, 1}
	            : std::array<float, 16> {0, 0, .25f, 1, 1, 0, .25f, 1, 0, 1, .25f, 1, 0, 0, 0, 1};
	p.modulation  = {1, 0, 1, 1, .75f, .5f, .25f, 1, .5f, .75f, 1, 1, 0, 1, 0, 1};
	p.coordinates = {x, y, changed ? 2u : 1u, 0};
	p.vs_image.assign(256 * 4, 0xee);
	p.ps_image.assign(256 * 4, 0xdd);
	for (uint32_t row = 0; row < 4; ++row)
		for (uint32_t col = 0; col < 8; ++col) {
			auto* vs = p.vs_image.data() + row * 256 + col * 4;
			auto* ps = p.ps_image.data() + row * 256 + col * 4;
			vs[0]    = uint8_t(changed ? 64 + col * 24 : 255 - col * 16);
			vs[1]    = uint8_t(changed ? 255 - col * 24 : 64 + col * 24);
			vs[2]    = uint8_t(changed ? 32 + col * 24 : 192 - col * 16);
			vs[3]    = 255;
			ps[0]    = uint8_t(changed ? 200 - col * 16 : 32 + col * 24);
			ps[1]    = uint8_t(changed ? 200 - row * 40 : 16 + row * 48);
			ps[2]    = uint8_t(changed ? 16 + col * 12 + row * 8 : 200 - col * 12 - row * 8);
			ps[3]    = changed ? 128 : 255;
		}
	return p;
}
inline float Expected(const Payload& p, float u, float v, uint32_t channel) {
	const uint32_t x = p.coordinates[0], y = p.coordinates[1];
	if (x >= 8 || y >= 4) return 0;
	const float weights[]    = {1 - u - v, u, v};
	float       interpolated = 0;
	for (uint32_t vertex = 0; vertex < 3; ++vertex)
		interpolated += weights[vertex] * p.colors[vertex * 4 + channel] *
		                p.vs_image[vertex * 4 + channel] / 255.f;
	return interpolated * p.modulation[p.coordinates[2] * 4 + channel] *
	       p.ps_image[y * 256 + x * 4 + channel] / 255.f;
}
inline std::array<uint32_t, 8> ImageWords(uint64_t address) {
	return {uint32_t(address >> 8), 10u << 20, 7u | (3u << 14), 0x90800facu, 63u << 13, 0, 0, 0};
}
} // namespace Ps4TextureTests
