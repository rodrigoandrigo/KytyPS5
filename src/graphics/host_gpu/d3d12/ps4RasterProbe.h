#pragma once
#include "graphics/guest_gpu/ps4Raster.h"

#include <span>
#include <vector>
struct ID3D12Device;
struct ID3D12CommandQueue;
namespace Libs::Graphics::D3D12 {
// DXIL is produced from the GCN frontend's SPIR-V through DxilCompiler/Mesa.
// Vertex translation enables conditional Y flip; the runtime mask is set here.
bool RasterizePs4Probe(ID3D12Device* device, ID3D12CommandQueue* queue,
                       const Ps4::RasterState& state, std::span<const uint8_t> vertex,
                       std::span<const uint8_t> pixel, std::vector<uint8_t>& image,
                       std::string& error);
} // namespace Libs::Graphics::D3D12
