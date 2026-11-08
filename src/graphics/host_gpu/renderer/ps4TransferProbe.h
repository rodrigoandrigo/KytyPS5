#pragma once
#include "graphics/guest_gpu/ps4Transfer.h"

#include <vulkan/vulkan.h>

namespace Libs::Graphics::Vulkan {
// Uses the host backend's already selected device, memory properties and queue.
// Caller serializes use of queue. No loader or instance is created by the probe.
struct TransferDevice {
	VkDevice                         device;
	VkQueue                          queue;
	uint32_t                         family;
	VkPhysicalDeviceMemoryProperties memory;
	PFN_vkGetDeviceProcAddr          get_proc;
};
bool ProbePs4Transfers(const TransferDevice& device, const Ps4::TransferPlan& plan,
                       std::vector<uint8_t>& pixels, std::string& error);
} // namespace Libs::Graphics::Vulkan
