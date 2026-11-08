#include "graphics/guest_gpu/ps4GnmRenderer.h"

#include "graphics/host_gpu/d3d12/dxilCompiler.h"
#include "graphics/host_gpu/d3d12/ps4RasterProbe.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace Libs::Graphics::Ps4 {
namespace {
using Microsoft::WRL::ComPtr;
class Renderer final: public GnmRenderer {
public:
	ComPtr<ID3D12Device>                 device;
	ComPtr<ID3D12CommandQueue>           queue;
	std::unique_ptr<D3D12::DxilCompiler> compiler;
	bool                                 Init(bool software, std::string& error) {
        ComPtr<IDXGIAdapter> adapter;
        if (software) {
            ComPtr<IDXGIFactory4> factory;
            if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) ||
                FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) {
                error = "Cannot select GNM WARP device";
                return false;
            }
        }
        if (FAILED(
                D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
            error = "Cannot create GNM D3D12 device";
            return false;
        }
        D3D12_COMMAND_QUEUE_DESC desc {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)))) {
            error = "Cannot create GNM D3D12 queue";
            return false;
        }
        compiler = std::make_unique<D3D12::DxilCompiler>(device.Get());
        return true;
	}
	bool Render(const RasterState& state, std::span<const uint32_t> vertex,
	            std::span<const uint32_t> pixel, std::vector<uint8_t>& output,
	            std::string& error) override {
		const D3D12::LinkedStage stages[] {{vertex, D3D12::LinkedStage::Kind::Vertex, true},
		                                   {pixel, D3D12::LinkedStage::Kind::Pixel}};
		const auto               shaders = compiler->CompileLinked(stages, 0);
		return D3D12::RasterizePs4Probe(device.Get(), queue.Get(), state, shaders[0].bytecode,
		                                shaders[1].bytecode, output, error);
	}
	const char* Name() const override { return "D3D12 GNM"; }
};
} // namespace
std::unique_ptr<GnmRenderer> CreateGnmRenderer(std::string& error, bool software) {
	auto renderer = std::make_unique<Renderer>();
	if (!renderer->Init(software, error)) return nullptr;
	return renderer;
}
} // namespace Libs::Graphics::Ps4
