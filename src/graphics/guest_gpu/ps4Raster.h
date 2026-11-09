#pragma once
#include <cmath>
#include <cstdint>
#include <span>
#include <string>

namespace Libs::Graphics::Ps4 {
// Explicit offscreen draw contract, independent of PS5 register encodings.
// Triangle list, first vertex/instance 0, one instance, optional read-only shader buffers,
// depth/stencil disabled, no culling, RGBA8 UNorm, one sample.
struct Texture2D {
	uint32_t                 width = 0, height = 0, pitch = 0;
	std::span<const uint8_t> pixels;
};
struct RasterState {
	std::span<const uint8_t>
	    pixel_buffer; // PS read-only raw SSBO/SRV array (canonical binding 58).
	std::span<const uint8_t> pixel_buffer2; // Optional second resource in descriptor order.
	std::span<const uint8_t> vertex_buffer, vertex_buffer2; // VS array, binding 60.
	Texture2D                pixel_texture, vertex_texture;
	uint32_t                 width = 64, height = 32;
	uint32_t                 vertex_count = 3; // count of indices for indexed draws
	std::span<const uint8_t> indices;
	uint32_t                 index_bytes = 0; // 0=auto; 2=uint16; 4=uint32
	uint32_t                 left = 0, top = 0, right = 64, bottom = 32;
	float                    clear[4] {0, 0, 0, 1};
	uint8_t                  write_mask = 15;
	bool alpha_blend = false; // RGB: src alpha / 1-src alpha; alpha: one / 1-src alpha.
	std::span<const uint8_t>
	    initial_pixels; // Optional tightly packed RGBA8 snapshot, retained by caller.
};
inline bool ValidateRasterState(const RasterState& s, std::string& error) {
	error.clear();
	for (auto buffer: {s.pixel_buffer, s.pixel_buffer2, s.vertex_buffer, s.vertex_buffer2})
		if ((!s.pixel_buffer2.empty() && s.pixel_buffer.empty()) ||
		    (!s.vertex_buffer2.empty() && s.vertex_buffer.empty()) ||
		    (!buffer.empty() &&
		     (buffer.size() > 65536 || buffer.size() < 16 || buffer.size() % 4))) {
			error = "Invalid PS4 pixel buffer snapshot";
			return false;
		}
	for (const auto& t: {s.pixel_texture, s.vertex_texture}) {
		if (t.pixels.empty() && !t.width && !t.height && !t.pitch) continue;
		if (!t.width || !t.height || t.width > 256 || t.height > 256 || t.pitch < t.width * 4 ||
		    t.pitch > 4096 || t.pitch % 256 || t.pixels.size() != size_t(t.pitch) * t.height) {
			error = "Invalid PS4 texture snapshot/pitch";
			return false;
		}
	}
	if (!s.width || !s.height || s.width > 1024 || s.height > 1024 || s.left > s.right ||
	    s.top > s.bottom || s.right > s.width || s.bottom > s.height || s.write_mask > 15 ||
	    !s.vertex_count || s.vertex_count > 65535 || s.vertex_count % 3) {
		error = "Unsupported PS4 diagnostic raster dimensions/scissor/write mask";
		return false;
	}
	if (s.indices.empty() ? s.index_bytes != 0 :
	    ((s.index_bytes != 2 && s.index_bytes != 4) ||
	     s.indices.size() != size_t(s.vertex_count) * s.index_bytes)) {
		error = "Invalid PS4 index type/snapshot/count";
		return false;
	}
	for (float c: s.clear)
		if (!std::isfinite(c) || c < 0 || c > 1) {
			error = "Invalid PS4 diagnostic clear color";
			return false;
		}
	if (!s.initial_pixels.empty() && s.initial_pixels.size() != size_t(s.width) * s.height * 4) {
		error = "Invalid PS4 initial framebuffer snapshot size";
		return false;
	}
	return true;
}
} // namespace Libs::Graphics::Ps4
