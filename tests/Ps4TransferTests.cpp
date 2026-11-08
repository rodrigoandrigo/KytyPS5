#include "graphics/guest_gpu/ps4Transfer.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

#if defined(PS4_PROBE_D3D12)
#include "graphics/host_gpu/d3d12/ps4TransferProbe.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#elif defined(PS4_PROBE_VULKAN)
#include "graphics/host_gpu/renderer/ps4TransferProbe.h"
#endif

using namespace Libs::Graphics;
static void Require(bool condition, const std::string& text) {
	if (!condition) throw std::runtime_error(text);
}
static void DecodeTests() {
	const Ps4::LinearTarget target {0x100000000, 65, 17, 512};
	const auto              fixture = Ps4::MakeTransferFixture(target);
	Ps4::TransferPlan       plan;
	std::string             error;
	Require(Ps4::DecodeTransfers(fixture, target, plan, error), error);
	const auto expected_count = plan.writes.size();
	const auto reject         = [&](std::vector<uint32_t> commands,
                            Ps4::LinearTarget     t = {0x100000000, 65, 17, 512}) {
        Require(!Ps4::DecodeTransfers(commands, t, plan, error), "Invalid PM4 accepted");
        Require(!error.empty() && plan.writes.size() == expected_count &&
		                    plan.target.address == target.address,
		                "Decoder failure modified output or omitted error");
	};
	for (size_t size = 1; size < 7; ++size)
		reject({fixture.begin(), fixture.begin() + size});
	for (auto bit: {0u, 1u, 2u, 7u}) {
		auto bad = fixture;
		bad[0] |= 1u << bit;
		reject(bad);
	}
	for (auto control: {0u, 0x20000000u, 0x40100000u, 0x40200000u, 0x40000001u}) {
		auto bad = fixture;
		bad[1]   = control;
		reject(bad);
	}
	for (auto count: {0u, 3u, 0x40000004u}) {
		auto bad = fixture;
		bad[6]   = count;
		reject(bad);
	}
	{
		auto bad = fixture;
		bad[4] += target.pitch * target.height;
		reject(bad);
	}
	{
		auto bad = fixture;
		bad[4] += 1;
		reject(bad);
	}
	{
		auto bad = fixture;
		bad[8] |= 1u << 16;
		reject(bad);
	}
	{
		auto bad = fixture;
		bad[8] |= 1u << 30;
		reject(bad);
	}
	{
		auto bad = fixture;
		bad.push_back(0xc0012d00);
		bad.insert(bad.end(), {3, 2});
		reject(bad);
	}
	{
		auto bad = fixture;
		bad.push_back(0xc0001000);
		reject(bad);
	}
	reject(fixture, {UINT64_MAX - 3, 65, 17, 512});
	reject(fixture, {target.address, UINT32_MAX, 17, 512});
	reject(fixture, {target.address, 65, UINT32_MAX, 512});
	reject(fixture, {target.address, 65, 17, 260});
	// PS5-specific NOP subcodes are opaque on the PS4 path.
	auto nop = fixture;
	nop.insert(nop.end(), {0xc000106c, 0});
	reject(nop);
	nop = fixture;
	nop.insert(nop.end(), {0xc0001000, 0xdeadbeef});
	Require(Ps4::DecodeTransfers(nop, target, plan, error), error);
	// No CPU guest pointers, indirect streams, register writes or shaders are
	// accepted.
	for (uint32_t op: {0x3fu, 0x69u, 0x76u, 0x15u, 0x49u})
		reject({0xc0000000u | (op << 8), 0});
	std::puts("PS4_TRANSFER decoder malformed/control/address/transaction tests PASS");
}

template <typename Run>
static void ImageTests(Run run, const char* backend, const char* output) {
	for (const auto target: {Ps4::LinearTarget {0x100000000, 64, 32, 256},
	                         Ps4::LinearTarget {0x100002000, 65, 17, 512},
	                         Ps4::LinearTarget {0x200000000, 1, 1, 256}}) {
		Ps4::TransferPlan plan;
		std::string       error;
		Require(Ps4::DecodeTransfers(Ps4::MakeTransferFixture(target), target, plan, error), error);
		std::vector<uint8_t> pixels;
		Require(run(plan, pixels, error), error);
		const auto expected = Ps4::ExpectedTransferFixture(target);
		Require(pixels == expected, "GPU RGBA8 readback mismatch");
		std::printf("PS4_TRANSFER %s %ux%u pitch=%u pixels=%zu PASS\n", backend, target.width,
		            target.height, target.pitch, pixels.size() / 4);
		if (output && target.width == 64) {
			std::ofstream file(output, std::ios::binary);
			file << "P6\n" << target.width << ' ' << target.height << "\n255\n";
			for (size_t i = 0; i < pixels.size(); i += 4)
				file.write(reinterpret_cast<const char*>(pixels.data() + i), 3);
			Require(bool(file), "Could not write readback image");
		}
		// Public adapter independently validates forged plans before any GPU
		// recording.
		const auto prior           = pixels;
		plan.writes.front().offset = UINT32_MAX;
		Require(!run(plan, pixels, error) && pixels == prior,
		        "Adapter accepted forged plan or modified output");
	}
}

int main(int argc, char** argv) {
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	try {
		DecodeTests();
#if defined(PS4_PROBE_D3D12)
		using Microsoft::WRL::ComPtr;
		ComPtr<ID3D12Debug> debug;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
			debug->EnableDebugLayer();
			std::puts("PS4_TRANSFER D3D12 debug layer enabled");
		} else {
			std::puts("PS4_TRANSFER D3D12 debug layer unavailable (not installed by test)");
		}
		ComPtr<IDXGIFactory4> factory;
		Require(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))), "DXGI factory");
		for (bool warp: {false, true}) {
			ComPtr<ID3D12Device> device;
			ComPtr<IDXGIAdapter> adapter;
			if (warp)
				Require(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))),
				        "WARP adapter");
			Require(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
			                                    IID_PPV_ARGS(&device))),
			        "D3D12 device");
			ComPtr<IDXGIAdapter1> selected;
			Require(SUCCEEDED(factory->EnumAdapterByLuid(device->GetAdapterLuid(),
			                                             IID_PPV_ARGS(&selected))),
			        "Selected D3D12 adapter");
			DXGI_ADAPTER_DESC1 adapter_desc {};
			Require(SUCCEEDED(selected->GetDesc1(&adapter_desc)), "D3D12 adapter description");
			std::printf("PS4_TRANSFER D3D12 device=%ls software=%u\n", adapter_desc.Description,
			            unsigned(bool(adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)));
			D3D12_COMMAND_QUEUE_DESC desc {};
			desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			ComPtr<ID3D12CommandQueue> queue;
			Require(SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))),
			        "D3D12 queue");
			ImageTests(
			    [&](const auto& p, auto& image, auto& error) {
				    return D3D12::ProbePs4Transfers(device.Get(), queue.Get(), p, image, error);
			    },
			    warp ? "D3D12-WARP" : "D3D12-default", !warp && argc > 1 ? argv[1] : nullptr);
			ComPtr<ID3D12InfoQueue> info;
			if (SUCCEEDED(device.As(&info))) {
				for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
					SIZE_T bytes = 0;
					info->GetMessage(i, nullptr, &bytes);
					std::vector<uint8_t> storage(bytes);
					auto*                message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
					Require(SUCCEEDED(info->GetMessage(i, message, &bytes)), "D3D12 debug message");
					Require(message->Severity > D3D12_MESSAGE_SEVERITY_ERROR,
					        std::string("D3D12 debug error: ") + message->pDescription);
				}
				std::puts("PS4_TRANSFER D3D12 debug errors=0");
			}
		}
#elif defined(PS4_PROBE_VULKAN)
		VkInstance           instance {};
		VkInstanceCreateInfo create {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		Require(vkCreateInstance(&create, nullptr, &instance) == VK_SUCCESS, "Vulkan instance");
		uint32_t count = 0;
		Require(vkEnumeratePhysicalDevices(instance, &count, nullptr) == VK_SUCCESS && count,
		        "Vulkan devices");
		std::vector<VkPhysicalDevice> devices(count);
		Require(vkEnumeratePhysicalDevices(instance, &count, devices.data()) == VK_SUCCESS,
		        "Vulkan device enumeration");
		const auto                 physical = devices.front();
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(physical, &properties);
		std::printf("PS4_TRANSFER Vulkan device=%s\n", properties.deviceName);
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
		std::vector<VkQueueFamilyProperties> families(count);
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
		uint32_t family = 0;
		while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
			++family;
		Require(family < count, "Vulkan graphics queue");
		const float             priority = 1;
		VkDeviceQueueCreateInfo q {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		q.queueFamilyIndex = family;
		q.queueCount       = 1;
		q.pQueuePriorities = &priority;
		VkDeviceCreateInfo dc {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		dc.queueCreateInfoCount = 1;
		dc.pQueueCreateInfos    = &q;
		Vulkan::TransferDevice host {};
		host.family   = family;
		host.get_proc = vkGetDeviceProcAddr;
		Require(vkCreateDevice(physical, &dc, nullptr, &host.device) == VK_SUCCESS,
		        "Vulkan device");
		vkGetDeviceQueue(host.device, family, 0, &host.queue);
		vkGetPhysicalDeviceMemoryProperties(physical, &host.memory);
		try {
			ImageTests(
			    [&](const auto& p, auto& image, auto& error) {
				    return Vulkan::ProbePs4Transfers(host, p, image, error);
			    },
			    "Vulkan", argc > 1 ? argv[1] : nullptr);
		} catch (...) {
			vkDestroyDevice(host.device, nullptr);
			vkDestroyInstance(instance, nullptr);
			throw;
		}
		vkDestroyDevice(host.device, nullptr);
		vkDestroyInstance(instance, nullptr);
#endif
		return 0;
	} catch (const std::exception& e) {
		std::fprintf(stderr, "PS4_TRANSFER FAIL: %s\n", e.what());
		return 1;
	}
}
