#pragma once
#include "graphics/guest_gpu/ps4Raster.h"
#include "graphics/host_gpu/renderer/ps4TransferProbe.h"
namespace Libs::Graphics::Vulkan {
bool RasterizePs4Probe(const TransferDevice& host, const Ps4::RasterState& state,
                       std::span<const uint32_t> vertex, std::span<const uint32_t> pixel,
                       std::vector<uint8_t>& image, std::string& error);
}
