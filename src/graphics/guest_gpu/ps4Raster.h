#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>

namespace Libs::Graphics::Ps4 {
inline constexpr uint32_t ImagesPerStage = 4;
inline constexpr uint32_t BuffersPerStage = 8;
inline constexpr uint32_t BufferBinding = 58;
inline constexpr uint32_t BufferMetadataDwords = BuffersPerStage + (BuffersPerStage + 3) / 4;
inline constexpr uint32_t ImageBinding = BufferBinding + 2 * BuffersPerStage;
inline constexpr uint32_t SamplerBinding = ImageBinding + 2 * ImagesPerStage;
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
	std::span<const uint8_t> pixel_buffer2, pixel_buffer3; // Optional resources in descriptor order.
	std::span<const uint8_t> vertex_buffer, vertex_buffer2, vertex_buffer3;
	std::array<std::span<const uint8_t>, BuffersPerStage - 3> pixel_buffer_tail, vertex_buffer_tail;
	// Stage-wide storage-buffer binding mode. The backend owns a private copy for the draw.
	bool pixel_storage_writes = false, vertex_storage_writes = false;
	std::array<std::span<uint8_t>, 2 * BuffersPerStage> storage_readback;
	Texture2D                pixel_texture, vertex_texture;
	Texture2D                pixel_texture2, vertex_texture2;
	std::array<Texture2D, ImagesPerStage - 2> pixel_texture_tail, vertex_texture_tail;
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
inline std::array<std::span<const uint8_t>, 2 * BuffersPerStage> RasterBuffers(const RasterState& state) {
	std::array<std::span<const uint8_t>, 2 * BuffersPerStage> buffers;
	buffers[0] = state.pixel_buffer;
	buffers[1] = state.pixel_buffer2;
	buffers[2] = state.pixel_buffer3;
	buffers[BuffersPerStage] = state.vertex_buffer;
	buffers[BuffersPerStage + 1] = state.vertex_buffer2;
	buffers[BuffersPerStage + 2] = state.vertex_buffer3;
	for (size_t i = 0; i < state.pixel_buffer_tail.size(); ++i) {
		buffers[3 + i] = state.pixel_buffer_tail[i];
		buffers[BuffersPerStage + 3 + i] = state.vertex_buffer_tail[i];
	}
	return buffers;
}
inline std::array<uint32_t, 2 * BufferMetadataDwords> RasterBufferMetadata(const RasterState& state) {
	const auto buffers = RasterBuffers(state);
	std::array<uint32_t, 2 * BufferMetadataDwords> words {};
	for (size_t stage = 0; stage < 2; ++stage) {
		const size_t first = stage * BuffersPerStage;
		size_t count = 0;
		while (count < BuffersPerStage && !buffers[first + count].empty()) ++count;
		const size_t sizes = stage * BufferMetadataDwords + (count + 3) / 4;
		for (size_t i = 0; i < count; ++i) words[sizes + i] = uint32_t(buffers[first + i].size());
	}
	return words;
}
inline std::array<Texture2D, 2 * ImagesPerStage> RasterImages(const RasterState& state) {
	return {state.pixel_texture, state.pixel_texture2,
	        state.pixel_texture_tail[0], state.pixel_texture_tail[1],
	        state.vertex_texture, state.vertex_texture2,
	        state.vertex_texture_tail[0], state.vertex_texture_tail[1]};
}
inline bool ValidateRasterState(const RasterState& s, std::string& error) {
	error.clear();
	const auto buffers = RasterBuffers(s);
	if ((s.pixel_storage_writes && s.pixel_buffer.empty()) ||
	    (s.vertex_storage_writes && s.vertex_buffer.empty())) {
		error = "PS4 writable storage stage requires a buffer";
		return false;
	}
	for (size_t i = 0; i < buffers.size(); ++i) {
		const auto buffer = buffers[i];
		const auto readback = s.storage_readback[i];
		if (!readback.empty() && (readback.size() != buffer.size() ||
		    !(i < BuffersPerStage ? s.pixel_storage_writes : s.vertex_storage_writes))) {
			error = "Invalid PS4 storage readback range";
			return false;
		}
		if (!buffer.empty() && (buffer.size() > 65536 || buffer.size() % 4 ||
		    (i % BuffersPerStage && buffers[i - 1].empty()))) {
			error = "Invalid PS4 shader buffer snapshot";
			return false;
		}
	}
	const auto images = RasterImages(s);
	for (size_t i = 0; i < images.size(); ++i) {
		const auto& t = images[i];
		if (t.pixels.empty() && !t.width && !t.height && !t.pitch) continue;
		if (i % ImagesPerStage && images[i - 1].pixels.empty()) {
			error = "Invalid PS4 image binding order";
			return false;
		}
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
