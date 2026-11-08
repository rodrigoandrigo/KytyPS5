#include "graphics/guest_gpu/ps4GnmRenderer.h"

#include "graphics/host_gpu/renderer/ps4RasterProbe.h"
#include "graphics/host_gpu/vulkanCommon.h"

namespace Libs::Graphics::Ps4 {
namespace {
class Renderer final: public GnmRenderer {
	vk::detail::DynamicLoader         loader;
	vk::detail::DispatchLoaderDynamic dispatch;
	VkInstance                        instance {};
	Vulkan::TransferDevice            host {};
	std::string                       name;

public:
	~Renderer() override {
		if (host.device) {
			dispatch.vkDeviceWaitIdle(host.device);
			dispatch.vkDestroyDevice(host.device, nullptr);
		}
		if (instance) dispatch.vkDestroyInstance(instance, nullptr);
	}
	bool Init(bool software, std::string& error) {
		const auto fail = [&](const char* operation) {
			error = std::string("GNM Vulkan: ") + operation;
			return false;
		};
		if (!loader.success()) return fail("loader unavailable");
		const auto get = loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
		if (!get) return fail("loader entry unavailable");
		dispatch.init(get);
		VkApplicationInfo app {VK_STRUCTURE_TYPE_APPLICATION_INFO};
		app.apiVersion       = VK_API_VERSION_1_2;
		app.pApplicationName = "Kyty PS4 GNM";
		VkInstanceCreateInfo ci {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		ci.pApplicationInfo = &app;
		if (dispatch.vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS)
			return fail("create instance");
		dispatch.init(instance, get);
		uint32_t count = 0;
		if (dispatch.vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS || !count)
			return fail("no devices");
		std::vector<VkPhysicalDevice> devices(count);
		if (dispatch.vkEnumeratePhysicalDevices(instance, &count, devices.data()) != VK_SUCCESS)
			return fail("enumerate devices");
		VkPhysicalDevice physical {};
		for (auto candidate: devices) {
			VkPhysicalDeviceProperties props;
			dispatch.vkGetPhysicalDeviceProperties(candidate, &props);
			if (props.apiVersion < VK_API_VERSION_1_2 ||
			    (software && props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU))
				continue;
			dispatch.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
			std::vector<VkQueueFamilyProperties> families(count);
			dispatch.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
			for (uint32_t i = 0; i < count; ++i)
				if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
					physical    = candidate;
					host.family = i;
					name        = props.deviceName;
					break;
				}
			if (physical) break;
		}
		if (!physical) return fail("no compatible graphics device");
		const float             priority = 1;
		VkDeviceQueueCreateInfo qi {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		qi.queueFamilyIndex = host.family;
		qi.queueCount       = 1;
		qi.pQueuePriorities = &priority;
		VkPhysicalDeviceFeatures features {};
		dispatch.vkGetPhysicalDeviceFeatures(physical, &features);
		VkDeviceCreateInfo dc {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		dc.queueCreateInfoCount = 1;
		dc.pQueueCreateInfos    = &qi;
		dc.pEnabledFeatures     = &features;
		if (dispatch.vkCreateDevice(physical, &dc, nullptr, &host.device) != VK_SUCCESS)
			return fail("create device");
		dispatch.init(vk::Device(host.device));
		host.get_proc = dispatch.vkGetDeviceProcAddr;
		dispatch.vkGetDeviceQueue(host.device, host.family, 0, &host.queue);
		dispatch.vkGetPhysicalDeviceMemoryProperties(physical, &host.memory);
		return true;
	}
	bool Render(const RasterState& state, std::span<const uint32_t> vertex,
	            std::span<const uint32_t> pixel, std::vector<uint8_t>& output,
	            std::string& error) override {
		return Vulkan::RasterizePs4Probe(host, state, vertex, pixel, output, error);
	}
	const char* Name() const override { return name.c_str(); }
};
} // namespace
std::unique_ptr<GnmRenderer> CreateGnmRenderer(std::string& error, bool software) {
	auto renderer = std::make_unique<Renderer>();
	if (!renderer->Init(software, error)) return nullptr;
	return renderer;
}
} // namespace Libs::Graphics::Ps4
