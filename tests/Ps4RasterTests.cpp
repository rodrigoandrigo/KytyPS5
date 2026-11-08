#include "Ps4TextureFixture.h"
#include "graphics/guest_gpu/ps4Raster.h"
#include "graphics/guest_gpu/ps4Transfer.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#if defined(PS4_RASTER_D3D12)
#include "graphics/host_gpu/d3d12/ps4RasterProbe.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#else
#include "graphics/host_gpu/renderer/ps4RasterProbe.h"
#endif
using namespace Libs::Graphics;
static void Require(bool ok, const std::string& text) {
	if (!ok) throw std::runtime_error(text);
}
template <typename T>
static std::vector<T> Read(const std::filesystem::path& file) {
	std::ifstream stream(file, std::ios::binary | std::ios::ate);
	const auto    bytes = stream.tellg();
	Require(bytes > 0 && bytes % sizeof(T) == 0 && bytes < 16 * 1024 * 1024,
	        "Shader fixture missing/invalid: " + file.string());
	std::vector<T> data(static_cast<size_t>(bytes) / sizeof(T));
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(data.data()), bytes);
	Require(bool(stream), "Read fixture");
	return data;
}
static void Save(const std::filesystem::path& path, const Ps4::RasterState& s,
                 const std::vector<uint8_t>& pixels) {
	std::ofstream file(path, std::ios::binary);
	file << "P6\n" << s.width << ' ' << s.height << "\n255\n";
	for (size_t i = 0; i < pixels.size(); i += 4)
		file.write(reinterpret_cast<const char*>(pixels.data() + i), 3);
	Require(bool(file), "Write raster image");
}
template <typename Run>
static void RasterTests(Run run, const std::filesystem::path& folder, const char* backend) {
	for (int variant = 0; variant < 59; ++variant) {
		Ps4::RasterState state;
		if (variant == 1) {
			state.left   = 7;
			state.top    = 3;
			state.right  = 59;
			state.bottom = 27;
		}
		if (variant == 2) {
			state.alpha_blend = true;
			state.write_mask  = 5;
			state.clear[0]    = .125f;
			state.clear[1]    = .25f;
			state.clear[2]    = .375f;
		}
		std::vector<uint8_t> initial;
		if (variant == 3) {
			initial.resize(size_t(state.width) * state.height * 4);
			for (uint32_t y = 0; y < state.height; ++y)
				for (uint32_t x = 0; x < state.width; ++x) {
					auto* p = initial.data() + (size_t(y) * state.width + x) * 4;
					p[0]    = uint8_t(x * 3);
					p[1]    = uint8_t(y * 5);
					p[2]    = 32;
					p[3]    = 255;
				}
			state.initial_pixels = initial;
		}
		std::vector<uint8_t> shader_data;
		if (variant == 10 || variant == 11 || variant == 13) {
			shader_data.resize(64);
			const float data[] = {1,
			                      0,
			                      0,
			                      1,
			                      variant == 11 ? .75f : .25f,
			                      variant == 11 ? .25f : .5f,
			                      variant == 11 ? .5f : .75f,
			                      1};
			std::memcpy(shader_data.data(), data, sizeof(data));
			if (variant == 13) {
				const uint32_t signed_input   = uint32_t(-3);
				const float    negative_input = -1.75f;
				std::memcpy(shader_data.data(), &signed_input, 4);
				std::memcpy(shader_data.data() + 4, &negative_input, 4);
				std::memcpy(shader_data.data() + 8, &negative_input, 4);
			}
			state.pixel_buffer = shader_data;
		}
		std::vector<uint8_t> second_data;
		if (variant >= 15) {
			shader_data.assign(variant >= 20 && variant <= 21 ? 64 : 16, 0);
			second_data.assign(64, 0);
			if (variant == 20 || variant == 21) {
				const float positions[] = {-.75f, -.75f, 0,     1,    .75f, -.75f,
				                           0,     1,     -.75f, .75f, 0,    1};
				const float smaller[]   = {-.5f, -.5f, 0, 1, .5f, -.5f, 0, 1, -.5f, .5f, 0, 1};
				const float colors[]    = {0, 0, .25f, 1, 1, 0, .25f, 1, 0, 1, .25f, 1};
				const float changed[]   = {.25f, 0, 0, 1, .25f, 0, 1, 1, .25f, 1, 0, 1};
				std::memcpy(shader_data.data(), variant == 21 ? smaller : positions,
				            sizeof(positions));
				std::memcpy(second_data.data(), variant == 21 ? changed : colors, sizeof(colors));
			} else {
				const uint32_t index = variant == 15                    ? 1
				                       : variant == 16 || variant == 22 ? 2
				                       : variant == 17                  ? 4
				                       : variant == 18                  ? 0x10000000u
				                                                        : UINT32_MAX;
				std::memcpy(shader_data.data(), &index, 4);
				const float records[] = {1,    0,    1,   1, .25f, .5f, .75f, 1,
				                         .75f, .25f, .5f, 1, 0,    1,   0,    1};
				std::memcpy(second_data.data(), records, sizeof(records));
			}
			state.pixel_buffer  = shader_data;
			state.pixel_buffer2 = second_data;
			if (variant == 20 || variant == 21) {
				state.vertex_buffer  = shader_data;
				state.vertex_buffer2 = second_data;
				state.pixel_buffer   = {};
				state.pixel_buffer2  = {};
			}
		}
		Ps4TextureTests::Payload texture_data;
		if (variant >= 23) {
			const uint32_t scene = uint32_t(variant - 23);
			const uint32_t x     = variant == 55   ? 7
			                       : variant == 56 ? 8
			                       : variant == 57 ? 0
			                       : variant == 58 ? UINT32_MAX
			                                       : scene % 8;
			const uint32_t y     = variant == 55   ? 3
			                       : variant >= 56 ? (variant == 57 ? 4u : 0u)
			                                       : scene / 8;
			texture_data         = Ps4TextureTests::Make(x, y, variant == 55);
			state.vertex_buffer  = Ps4TextureTests::Bytes(texture_data.positions);
			state.vertex_buffer2 = Ps4TextureTests::Bytes(texture_data.colors);
			state.pixel_buffer   = Ps4TextureTests::Bytes(texture_data.coordinates);
			state.pixel_buffer2  = Ps4TextureTests::Bytes(texture_data.modulation);
			state.vertex_texture = {8, 4, 256, texture_data.vs_image};
			state.pixel_texture  = {8, 4, 256, texture_data.ps_image};
		}
		std::vector<uint8_t> pixels;
		std::string          error;
		Require(run(variant, state, pixels, error), error);
		Require(pixels.size() == size_t(state.width) * state.height * 4, "Raster output size");
		uint32_t    covered = 0;
		const float color[] {variant == 5 || variant == 11 ? .75f : .25f,
		                     variant == 11 ? .25f : .5f, variant == 11 ? .5f : .75f,
		                     variant == 2 ? .5f : 1.f};
		for (uint32_t y = 0; y < state.height; ++y)
			for (uint32_t x = 0; x < state.width; ++x) {
				const float origin_x = variant == 21 || variant == 55 ? 16.f : 8.f,
				            origin_y = variant == 21 || variant == 55 ? 8.f : 4.f;
				const float extent_x = variant == 21 || variant == 55 ? 32.f : 48.f,
				            extent_y = variant == 21 || variant == 55 ? 16.f : 24.f;
				const bool triangle =
				    variant == 1 || (float(x) >= origin_x && float(y) >= origin_y &&
				                     (float(x) + .5f - origin_x) / extent_x +
				                             (float(y) + .5f - origin_y) / extent_y <
				                         1);

				const bool inside = triangle && x >= state.left && x < state.right &&
				                    y >= state.top && y < state.bottom;
				covered += inside;
				float shade[] {color[0], color[1], color[2], color[3]};
				if ((variant >= 6 && variant < 10) || variant == 12 || variant == 14) {
					const float u = (float(x) + .5f - 8) / 48, v = (float(y) + .5f - 4) / 24;
					const float denominator = variant >= 7 && variant != 14 ? 1 - v * .5f : 1;
					shade[0]                = u / denominator;
					shade[1] = v * (variant >= 7 && variant != 14 ? .5f : 1.f) / denominator;
					shade[2] = .25f;
					if (variant == 9 || variant == 12) std::swap(shade[0], shade[2]);
					if (variant == 8) {
						shade[0] = std::clamp(1 - 2 * std::abs(shade[0]), 0.f, 1.f);
						shade[1] = (shade[1] * .5f + .5f) * .5f;
						shade[2] = std::max(.375f, std::min(.5f, shade[1]));
					}
				}
				if (variant == 13) {
					shade[0] = .5f;
					shade[1] = .5f;
					shade[2] = .75f;
				}
				if (variant == 16 || variant == 22) {
					shade[0] = .75f;
					shade[1] = .25f;
					shade[2] = .5f;
				}
				if (variant >= 17 && variant <= 19)
					for (float& c: shade)
						c = 0;
				if (variant == 20 || variant == 21) {
					const float u = (float(x) + .5f - origin_x) / extent_x,
					            v = (float(y) + .5f - origin_y) / extent_y;
					shade[0]      = variant == 21 ? .25f : u;
					shade[1]      = v;
					shade[2]      = variant == 21 ? u : .25f;
				}
				for (uint32_t channel = 0; channel < 4; ++channel) {
					if (variant >= 23)
						shade[channel] = Ps4TextureTests::Expected(
						    texture_data, (float(x) + .5f - origin_x) / extent_x,
						    (float(y) + .5f - origin_y) / extent_y, channel);
					const auto clear =
					    (state.initial_pixels.empty()
					         ? int(std::lround(state.clear[channel] * 255))
					         : int(initial[(size_t(y) * state.width + x) * 4 + channel]));
					float expected = float(clear);
					if (inside && (state.write_mask & (1u << channel))) {
						expected = shade[channel] * 255;
						if (state.alpha_blend)
							expected = channel == 3 ? expected + clear * (1 - color[3])
							                        : expected * color[3] + clear * (1 - color[3]);
					}
					const int actual = pixels[(size_t(y) * state.width + x) * 4 + channel];
					if (std::abs(actual - int(std::lround(expected))) > 1) {
						std::fprintf(
						    stderr,
						    "Raster %s variant=%d (%u,%u) channel=%u actual=%d expected=%.3f\n",
						    backend, variant, x, y, channel, actual, expected);
						throw std::runtime_error("GCN raster pixel mismatch");
					}
				}
			}
		Require(covered && covered < state.width * state.height,
		        "Test must contain covered and uncovered pixels");
		Save(folder / (std::string(backend) + "-" + std::to_string(variant) + ".ppm"), state,
		     pixels);
		std::printf("PS4_RASTER %s variant=%d covered=%u all=%u scissor/blend/mask PASS\n", backend,
		            variant, covered, state.width * state.height);
		if (variant == 15) {
			const auto saved = pixels;
			auto       bad   = state;
			bad.pixel_buffer = {};
			Require(!run(variant, bad, pixels, error) && pixels == saved,
			        "orphan second buffer changed output");
			bad               = state;
			bad.pixel_buffer2 = std::span<const uint8_t>(second_data).first(12);
			Require(!run(variant, bad, pixels, error) && pixels == saved,
			        "short second buffer changed output");
		}
		const auto saved = pixels;
		state.right      = state.width + 1;
		Require(!run(variant, state, pixels, error) && pixels == saved,
		        "Invalid raster state accepted/modified output");
	}
	// The same staging executor as GNM, with real GPU blending between writes.
	Ps4::RasterState state;
	state.alpha_blend = true;
	const Ps4::LinearTarget target {0x8000, state.width, state.height, state.width * 4};
	std::vector<uint8_t>    pixels(size_t(target.pitch) * target.height);
	std::string             error;
	Ps4::TransferPlan       plan;
	const uint32_t          fill[] = {
        0xc0055000u, 0xc0000000u, 0xff1e140au, 0, 0x8000, 0, uint32_t(pixels.size())};
	Require(Ps4::DecodeTransfers(fill, target, plan, error) &&
	            Ps4::ApplyTransfers(plan, pixels, error),
	        error);
	state.initial_pixels = pixels;
	std::vector<uint8_t> first;
	Require(run(2, state, first, error), error);
	const uint32_t writes[] = {0xc0033700, 0x00100500, 0x8000 + (8 * 64 + 16) * 4,  0, 0xff000000,
	                           0xc0033700, 0x00100500, 0x8000 + (30 * 64 + 60) * 4, 0, 0xff665544};
	Require(Ps4::DecodeTransfers(writes, target, plan, error) &&
	            Ps4::ApplyTransfers(plan, first, error),
	        error);
	const auto saved             = first;
	auto       invalid           = plan;
	invalid.writes.back().offset = uint32_t(first.size());
	Require(!Ps4::ApplyTransfers(invalid, first, error) && first == saved,
	        "invalid transfer modified snapshot");
	Require(!Ps4::ApplyTransfers(plan, std::span<uint8_t>(first).first(first.size() - 1), error) &&
	            first == saved,
	        "invalid transfer span modified snapshot");
	state.initial_pixels = first;
	Require(run(2, state, pixels, error), error);
	for (uint32_t y = 0; y < state.height; ++y)
		for (uint32_t x = 0; x < state.width; ++x) {
			const bool inside =
			    x >= 8 && y >= 4 && (float(x) + .5f - 8) / 48 + (float(y) + .5f - 4) / 24 < 1;
			const float source[]     = {.25f, .5f, .75f, 1};
			const int   background[] = {10, 20, 30, 255};
			for (uint32_t c = 0; c < 4; ++c) {
				float expected = background[c];
				if (inside && c < 3) expected = .75f * source[c] * 255 + .25f * background[c];
				if (x == 16 && y == 8 && c < 3) expected = .5f * source[c] * 255;
				if (x == 60 && y == 30) expected = (0xff665544u >> (c * 8)) & 255;
				Require(std::abs(int(pixels[(y * state.width + x) * 4 + c]) -
				                 int(std::lround(expected))) <= 1,
				        "mixed transfer/draw GPU result");
			}
		}
	Save(folder / (std::string(backend) + "-mixed.ppm"), state, pixels);
	std::printf("PS4_RASTER %s mixed DMA/WRITE_DATA and two GPU draws all=2048 PASS\n", backend);
}
#if !defined(PS4_RASTER_D3D12)
static std::atomic<unsigned>          validation_errors {0};
static VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                 VkDebugUtilsMessageTypeFlagsEXT,
                                                 const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                 void*) {
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		++validation_errors;
		std::fprintf(stderr, "Vulkan validation: %s\n", data->pMessage);
	}
	return VK_FALSE;
}
#endif
int main(int argc, char** argv) {
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	try {
		const std::filesystem::path folder = argc == 2 ? argv[1] : "ps4-gcn-fixture";
#if defined(PS4_RASTER_D3D12)
		using Microsoft::WRL::ComPtr;
		ComPtr<ID3D12Debug> debug;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
			debug->EnableDebugLayer();
			std::puts("PS4_RASTER D3D12 debug enabled");
		}
		ComPtr<IDXGIFactory4> factory;
		Require(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))), "DXGI factory");
		for (bool warp: {false, true}) {
			ComPtr<IDXGIAdapter> adapter;
			if (warp) Require(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))), "WARP");
			ComPtr<ID3D12Device> device;
			Require(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
			                                    IID_PPV_ARGS(&device))),
			        "D3D12 device");
			ComPtr<IDXGIAdapter1> selected;
			Require(SUCCEEDED(factory->EnumAdapterByLuid(device->GetAdapterLuid(),
			                                             IID_PPV_ARGS(&selected))),
			        "Adapter LUID");
			DXGI_ADAPTER_DESC1 desc;
			selected->GetDesc1(&desc);
			std::printf("PS4_RASTER D3D12 device=%ls\n", desc.Description);
			ComPtr<ID3D12CommandQueue> queue;
			D3D12_COMMAND_QUEUE_DESC   q {};
			q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			Require(SUCCEEDED(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue))), "D3D12 queue");
			RasterTests(
			    [&](int variant, const auto& state, auto& image, auto& error) {
				    const auto vs = Read<uint8_t>(
				        folder / (variant >= 23                    ? "gcn-texture.vert.dxil"
				                  : variant == 20 || variant == 21 ? "gcn-fetch.vert.dxil"
				                  : variant >= 15                  ? "gcn-small.vert.dxil"
				                  : variant == 14                  ? "gcn-perspective.vert.dxil"
				                  : variant == 12                  ? "gcn-many.vert.dxil"
				                  : variant >= 10                  ? "gcn-small.vert.dxil"
				                  : variant == 9                   ? "gcn-many.vert.dxil"
				                  : variant == 6                   ? "gcn-interp.vert.dxil"
				                  : variant >= 7                   ? "gcn-perspective.vert.dxil"
				                  : variant == 1                   ? "gcn.vert.dxil"
				                                                   : "gcn-small.vert.dxil"));
				    const auto ps = Read<uint8_t>(
				        folder / (variant >= 23                    ? "gcn-texture.frag.dxil"
				                  : variant == 20 || variant == 21 ? "gcn-interp.frag.dxil"
				                  : variant == 22                  ? "gcn-indexed-dword.frag.dxil"
				                  : variant >= 15                  ? "gcn-indexed.frag.dxil"
				                  : variant == 14                  ? "gcn-linear.frag.dxil"
				                  : variant == 12                  ? "gcn-remap.frag.dxil"
				                  : variant == 13                  ? "gcn-unary.frag.dxil"
				                  : variant >= 10                  ? "gcn-buffer.frag.dxil"
				                  : variant == 9                   ? "gcn-param3.frag.dxil"
				                  : variant == 8                   ? "gcn-alu.frag.dxil"
				                  : variant >= 6                   ? "gcn-interp.frag.dxil"
				                  : variant == 2                   ? "gcn-alpha.frag.dxil"
				                  : variant == 4                   ? "gcn-uniform0.frag.dxil"
				                  : variant == 5                   ? "gcn-uniform1.frag.dxil"
				                                                   : "gcn.frag.dxil"));
				    return D3D12::RasterizePs4Probe(device.Get(), queue.Get(), state, vs, ps, image,
				                                    error);
			    },
			    folder, warp ? "warp" : "d3d12");
			ComPtr<ID3D12InfoQueue> info;
			if (SUCCEEDED(device.As(&info))) {
				for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
					SIZE_T bytes = 0;
					info->GetMessage(i, nullptr, &bytes);
					std::vector<uint8_t> data(bytes);
					auto*                message = reinterpret_cast<D3D12_MESSAGE*>(data.data());
					Require(SUCCEEDED(info->GetMessage(i, message, &bytes)), "Read debug message");
					Require(message->Severity > D3D12_MESSAGE_SEVERITY_ERROR,
					        std::string("D3D12: ") + message->pDescription);
				}
				std::puts("PS4_RASTER D3D12 debug errors=0");
			}
		}
#else
		uint32_t count = 0;
		vkEnumerateInstanceLayerProperties(&count, nullptr);
		std::vector<VkLayerProperties> layers(count);
		vkEnumerateInstanceLayerProperties(&count, layers.data());
		bool validation = false;
		for (const auto& layer: layers)
			if (std::string_view(layer.layerName) == "VK_LAYER_KHRONOS_validation")
				validation = true;
		const char *layer     = "VK_LAYER_KHRONOS_validation",
		           *extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		VkApplicationInfo app {VK_STRUCTURE_TYPE_APPLICATION_INFO};
		app.apiVersion = VK_API_VERSION_1_2;
		VkDebugUtilsMessengerCreateInfoEXT debug {
		    VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
		debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debug.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debug.pfnUserCallback = Validation;
		VkInstanceCreateInfo ci {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		ci.pApplicationInfo = &app;
		if (validation) {
			ci.enabledLayerCount = ci.enabledExtensionCount = 1;
			ci.ppEnabledLayerNames                          = &layer;
			ci.ppEnabledExtensionNames                      = &extension;
			ci.pNext                                        = &debug;
		}
		VkInstance instance;
		Require(vkCreateInstance(&ci, nullptr, &instance) == VK_SUCCESS, "Vulkan instance");
		VkDebugUtilsMessengerEXT messenger {};
		if (validation) {
			auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
			    vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
			Require(create && create(instance, &debug, nullptr, &messenger) == VK_SUCCESS,
			        "Validation messenger");
		}
		Require(vkEnumeratePhysicalDevices(instance, &count, nullptr) == VK_SUCCESS && count,
		        "Vulkan devices");
		std::vector<VkPhysicalDevice> devices(count);
		vkEnumeratePhysicalDevices(instance, &count, devices.data());
		auto                       physical = devices.front();
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(physical, &props);
		std::printf("PS4_RASTER Vulkan device=%s validation=%u\n", props.deviceName,
		            unsigned(validation));
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
		std::vector<VkQueueFamilyProperties> families(count);
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
		uint32_t family = 0;
		while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
			++family;
		Require(family < count, "Vulkan graphics queue");
		const float             priority = 1;
		VkDeviceQueueCreateInfo qi {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		qi.queueFamilyIndex = family;
		qi.queueCount       = 1;
		qi.pQueuePriorities = &priority;
		VkPhysicalDeviceFeatures features {};
		vkGetPhysicalDeviceFeatures(physical, &features);
		VkDeviceCreateInfo dc {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		dc.queueCreateInfoCount = 1;
		dc.pQueueCreateInfos    = &qi;
		dc.pEnabledFeatures     = &features;
		Vulkan::TransferDevice host {};
		host.family   = family;
		host.get_proc = vkGetDeviceProcAddr;
		Require(vkCreateDevice(physical, &dc, nullptr, &host.device) == VK_SUCCESS,
		        "Vulkan device");
		vkGetDeviceQueue(host.device, family, 0, &host.queue);
		vkGetPhysicalDeviceMemoryProperties(physical, &host.memory);
		try {
			RasterTests(
			    [&](int variant, const auto& state, auto& image, auto& error) {
				    const auto vs = Read<uint32_t>(
				        folder / (variant >= 23                    ? "gcn-texture.vert.spv"
				                  : variant == 20 || variant == 21 ? "gcn-fetch.vert.spv"
				                  : variant >= 15                  ? "gcn-small.vert.spv"
				                  : variant == 14                  ? "gcn-perspective.vert.spv"
				                  : variant == 12                  ? "gcn-many.vert.spv"
				                  : variant >= 10                  ? "gcn-small.vert.spv"
				                  : variant == 9                   ? "gcn-many.vert.spv"
				                  : variant == 6                   ? "gcn-interp.vert.spv"
				                  : variant >= 7                   ? "gcn-perspective.vert.spv"
				                  : variant == 1                   ? "gcn.vert.spv"
				                                                   : "gcn-small.vert.spv"));
				    const auto ps = Read<uint32_t>(
				        folder / (variant >= 23                    ? "gcn-texture.frag.spv"
				                  : variant == 20 || variant == 21 ? "gcn-interp.frag.spv"
				                  : variant == 22                  ? "gcn-indexed-dword.frag.spv"
				                  : variant >= 15                  ? "gcn-indexed.frag.spv"
				                  : variant == 14                  ? "gcn-linear.frag.spv"
				                  : variant == 12                  ? "gcn-remap.frag.spv"
				                  : variant == 13                  ? "gcn-unary.frag.spv"
				                  : variant >= 10                  ? "gcn-buffer.frag.spv"
				                  : variant == 9                   ? "gcn-param3.frag.spv"
				                  : variant == 8                   ? "gcn-alu.frag.spv"
				                  : variant >= 6                   ? "gcn-interp.frag.spv"
				                  : variant == 2                   ? "gcn-alpha.frag.spv"
				                  : variant == 4                   ? "gcn-uniform0.frag.spv"
				                  : variant == 5                   ? "gcn-uniform1.frag.spv"
				                                                   : "gcn.frag.spv"));
				    return Vulkan::RasterizePs4Probe(host, state, vs, ps, image, error);
			    },
			    folder, "vulkan");
		} catch (...) {
			vkDestroyDevice(host.device, nullptr);
			throw;
		}
		vkDestroyDevice(host.device, nullptr);
		if (messenger)
			reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(
			    instance, "vkDestroyDebugUtilsMessengerEXT"))(instance, messenger, nullptr);
		vkDestroyInstance(instance, nullptr);
		Require(validation_errors == 0, "Vulkan validation errors");
		std::printf("PS4_RASTER Vulkan validation errors=%u\n", validation_errors.load());
#endif
		return 0;
	} catch (const std::exception& e) {
		std::fprintf(stderr, "PS4_RASTER FAIL: %s\n", e.what());
		return 1;
	}
}
