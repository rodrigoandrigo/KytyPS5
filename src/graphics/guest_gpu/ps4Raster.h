#pragma once
#include <cmath>
#include <cstdint>
#include <string>

namespace Libs::Graphics::Ps4 {
// Explicit offscreen draw contract, independent of PS5 register encodings.
// Triangle list, 3 vertices, first vertex/instance 0, one instance, no descriptors,
// depth/stencil disabled, no culling, RGBA8 UNorm, one sample.
struct RasterState {
	uint32_t width = 64, height = 32;
	uint32_t left = 0, top = 0, right = 64, bottom = 32;
	float    clear[4] {0, 0, 0, 1};
	uint8_t  write_mask  = 15;
	bool     alpha_blend = false; // RGB: src alpha / 1-src alpha; alpha: one / 1-src alpha.
};
inline bool ValidateRasterState(const RasterState& s, std::string& error) {
	error.clear();
	if (!s.width || !s.height || s.width > 1024 || s.height > 1024 || s.left > s.right ||
	    s.top > s.bottom || s.right > s.width || s.bottom > s.height || s.write_mask > 15) {
		error = "Unsupported PS4 diagnostic raster dimensions/scissor/write mask";
		return false;
	}
	for (float c: s.clear)
		if (!std::isfinite(c) || c < 0 || c > 1) {
			error = "Invalid PS4 diagnostic clear color";
			return false;
		}
	return true;
}
} // namespace Libs::Graphics::Ps4
