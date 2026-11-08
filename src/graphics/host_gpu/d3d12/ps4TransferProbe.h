#pragma once
#include "graphics/guest_gpu/ps4Transfer.h"

struct ID3D12Device;
struct ID3D12CommandQueue;

namespace Libs::Graphics::D3D12 {
// Synchronous offscreen diagnostic on the existing backend device/queue.
// Private resources only; returns tightly packed RGBA8 after fence completion.
bool ProbePs4Transfers(ID3D12Device* device, ID3D12CommandQueue* queue,
                       const Ps4::TransferPlan& plan, std::vector<uint8_t>& pixels,
                       std::string& error);
} // namespace Libs::Graphics::D3D12
