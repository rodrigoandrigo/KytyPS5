#include "graphics/host_gpu/d3d12/ps4TransferProbe.h"

#include <windows.h>
#include <cstring>
#include <d3d12.h>
#include <stdexcept>
#include <wrl/client.h>

namespace Libs::Graphics::D3D12 {
namespace {
using Microsoft::WRL::ComPtr;
void CheckProbe(HRESULT result, const char* operation) {
	if (FAILED(result))
		throw std::runtime_error(std::string(operation) +
		                         " HRESULT=" + std::to_string(uint32_t(result)));
}
D3D12_RESOURCE_DESC BufferDesc(uint64_t bytes) {
	D3D12_RESOURCE_DESC desc {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width     = bytes;
	desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
	desc.SampleDesc.Count                                = 1;
	desc.Layout                                          = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	return desc;
}
ComPtr<ID3D12Resource> Resource(ID3D12Device* device, const D3D12_RESOURCE_DESC& desc,
                                D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
	D3D12_HEAP_PROPERTIES heap {};
	heap.Type             = type;
	heap.CreationNodeMask = heap.VisibleNodeMask = 1;
	ComPtr<ID3D12Resource> resource;
	CheckProbe(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
	                                           IID_PPV_ARGS(&resource)),
	           "CreateCommittedResource");
	return resource;
}
void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
             D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
	D3D12_RESOURCE_BARRIER barrier {};
	barrier.Type       = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
	list->ResourceBarrier(1, &barrier);
}
} // namespace

bool ProbePs4Transfers(ID3D12Device* device, ID3D12CommandQueue* queue,
                       const Ps4::TransferPlan& plan, std::vector<uint8_t>& pixels,
                       std::string& error) {
	if (!Ps4::ValidatePlan(plan, error)) return false;
	if (!device || !queue || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
		error = "PS4 transfer probe requires a D3D12 direct queue";
		return false;
	}
	try {
		const auto     t             = plan.target;
		const uint64_t bytes         = uint64_t(t.pitch) * t.height;
		uint64_t       staging_bytes = bytes;
		for (const auto& write: plan.writes)
			staging_bytes += write.words.size() * 4;
		auto upload            = Resource(device, BufferDesc(staging_bytes), D3D12_HEAP_TYPE_UPLOAD,
		                                  D3D12_RESOURCE_STATE_GENERIC_READ);
		auto destination       = Resource(device, BufferDesc(bytes), D3D12_HEAP_TYPE_DEFAULT,
		                                  D3D12_RESOURCE_STATE_COPY_DEST);
		auto readback          = Resource(device, BufferDesc(bytes), D3D12_HEAP_TYPE_READBACK,
		                                  D3D12_RESOURCE_STATE_COPY_DEST);
		auto texture_desc      = BufferDesc(t.width);
		texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texture_desc.Height    = t.height;
		texture_desc.Layout    = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		texture_desc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
		auto texture =
		    Resource(device, texture_desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
		void*             mapped = nullptr;
		const D3D12_RANGE no_read {0, 0};
		CheckProbe(upload->Map(0, &no_read, &mapped), "Map upload");
		std::memset(mapped, 0, size_t(bytes));
		uint64_t offset = bytes;
		for (const auto& write: plan.writes) {
			std::memcpy(static_cast<uint8_t*>(mapped) + offset, write.words.data(),
			            write.words.size() * 4);
			offset += write.words.size() * 4;
		}
		upload->Unmap(0, nullptr);
		ComPtr<ID3D12CommandAllocator>    allocator;
		ComPtr<ID3D12GraphicsCommandList> list;
		ComPtr<ID3D12Fence>               fence;
		CheckProbe(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
		                                          IID_PPV_ARGS(&allocator)),
		           "CreateCommandAllocator");
		CheckProbe(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
		                                     nullptr, IID_PPV_ARGS(&list)),
		           "CreateCommandList");
		CheckProbe(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
		           "CreateFence");
		list->CopyBufferRegion(destination.Get(), 0, upload.Get(), 0, bytes);
		offset = bytes;
		for (const auto& write: plan.writes) {
			// Ordered overlapping writes, with an explicit dependency between copies.
			Barrier(list.Get(), destination.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
			        D3D12_RESOURCE_STATE_COPY_SOURCE);
			Barrier(list.Get(), destination.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
			        D3D12_RESOURCE_STATE_COPY_DEST);
			if (!write.words.empty())
				list->CopyBufferRegion(destination.Get(), write.offset, upload.Get(), offset,
				                       write.words.size() * 4);
			offset += write.words.size() * 4;
		}
		Barrier(list.Get(), destination.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
		        D3D12_RESOURCE_STATE_COPY_SOURCE);
		D3D12_TEXTURE_COPY_LOCATION buffer {};
		buffer.pResource                 = destination.Get();
		buffer.Type                      = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		buffer.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, t.width, t.height, 1,
		                                    t.pitch};
		D3D12_TEXTURE_COPY_LOCATION image {};
		image.pResource = texture.Get();
		image.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		list->CopyTextureRegion(&image, 0, 0, 0, &buffer, nullptr);
		Barrier(list.Get(), texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
		        D3D12_RESOURCE_STATE_COPY_SOURCE);
		buffer.pResource = readback.Get();
		list->CopyTextureRegion(&buffer, 0, 0, 0, &image, nullptr);
		CheckProbe(list->Close(), "Close transfer list");
		ID3D12CommandList* lists[] {list.Get()};
		queue->ExecuteCommandLists(1, lists);
		CheckProbe(queue->Signal(fence.Get(), 1), "Signal transfer fence");
		// Polling uses only UWP APIs and retains every resource until completion or device loss.
		while (fence->GetCompletedValue() < 1) {
			CheckProbe(device->GetDeviceRemovedReason(), "Transfer device status");
			SleepEx(1, FALSE);
		}
		CheckProbe(device->GetDeviceRemovedReason(), "Transfer completed device status");
		const D3D12_RANGE range {0, size_t(bytes)};
		CheckProbe(readback->Map(0, &range, &mapped), "Map readback");
		std::vector<uint8_t> result(size_t(t.width) * t.height * 4);
		for (uint32_t y = 0; y < t.height; ++y)
			std::memcpy(result.data() + size_t(y) * t.width * 4,
			            static_cast<uint8_t*>(mapped) + size_t(y) * t.pitch, size_t(t.width) * 4);
		readback->Unmap(0, &no_read);
		pixels = std::move(result);
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}
} // namespace Libs::Graphics::D3D12
