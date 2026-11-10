#include "graphics/host_gpu/d3d12/ps4RasterProbe.h"

#include <windows.h>
#include <cstring>
#include <d3d12.h>
#include <stdexcept>
#include <wrl/client.h>

namespace Libs::Graphics::D3D12 {
namespace {
using Microsoft::WRL::ComPtr;
void CheckRaster(HRESULT hr, const char* op) {
	if (FAILED(hr))
		throw std::runtime_error(std::string(op) + " HRESULT=" + std::to_string(uint32_t(hr)));
}
} // namespace
bool RasterizePs4Probe(ID3D12Device* device, ID3D12CommandQueue* queue, const Ps4::RasterState& s,
                       std::span<const uint8_t> vertex, std::span<const uint8_t> pixel,
                       std::vector<uint8_t>& image, std::string& error) {
	if (!Ps4::ValidateRasterState(s, error)) return false;
	if (!device || !queue || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
	    vertex.empty() || pixel.empty()) {
		error = "Invalid PS4 raster device/queue/shaders";
		return false;
	}
	try {
		const bool has_resources = !s.pixel_buffer.empty() || !s.vertex_buffer.empty() ||
		                           !s.pixel_texture.pixels.empty() ||
		                           !s.vertex_texture.pixels.empty();
		D3D12_ROOT_PARAMETER parameters[4] {};
		parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[0].Constants     = {0, 1001, 12};
		const auto writable_buffer = [&](UINT slot) {
			return slot < Ps4::BuffersPerStage ? s.pixel_storage_writes : s.vertex_storage_writes;
		};
		D3D12_DESCRIPTOR_RANGE ranges[3] {};
		for (UINT stage = 0; stage < 2; ++stage) {
			const UINT first = stage * Ps4::BuffersPerStage;
			ranges[stage] = {writable_buffer(first) ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
			                                       : D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			                 Ps4::BuffersPerStage, Ps4::BufferBinding + first, 0, first};
		}
		ranges[2] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2 * Ps4::ImagesPerStage, Ps4::ImageBinding, 0, 0};
		parameters[1].ParameterType   = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[1].DescriptorTable = {2, &ranges[0]};
		parameters[2].ParameterType   = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[2].Constants       = {0, 1000, 2 * Ps4::BufferMetadataDwords};
		parameters[3].ParameterType   = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[3].DescriptorTable = {1, &ranges[2]};
		D3D12_ROOT_SIGNATURE_DESC root_desc {};
		root_desc.NumParameters = has_resources ? 4 : 1;
		root_desc.pParameters   = parameters;
		root_desc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		// The strict PS4 sampler profile has no dynamic host state. Keep separate
		// canonical registers per stage, matching GcnCompiler's SPIR-V bindings.
		D3D12_STATIC_SAMPLER_DESC samplers[2] {};
		for (uint32_t stage = 0; stage < 2; ++stage) {
			auto& sampler = samplers[stage];
			sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.MaxAnisotropy = 1;
			sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			sampler.MinLOD = sampler.MaxLOD = 0;
			sampler.ShaderRegister = Ps4::SamplerBinding + stage;
			sampler.ShaderVisibility = stage ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_PIXEL;
		}
		root_desc.NumStaticSamplers = 2;
		root_desc.pStaticSamplers = samplers;

		ComPtr<ID3DBlob> blob, errors;
		CheckRaster(
		    D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors),
		    "Serialize raster root");
		ComPtr<ID3D12RootSignature> root;
		CheckRaster(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
		                                        IID_PPV_ARGS(&root)),
		            "Create raster root");
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline {};
		pipeline.pRootSignature                   = root.Get();
		pipeline.VS                               = {vertex.data(), vertex.size()};
		pipeline.PS                               = {pixel.data(), pixel.size()};
		pipeline.SampleMask                       = UINT_MAX;
		pipeline.RasterizerState.FillMode         = D3D12_FILL_MODE_SOLID;
		pipeline.RasterizerState.CullMode         = D3D12_CULL_MODE_NONE;
		pipeline.RasterizerState.DepthClipEnable  = TRUE;
		pipeline.DepthStencilState.DepthEnable    = FALSE;
		pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		pipeline.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_ALWAYS;
		pipeline.DepthStencilState.StencilEnable  = FALSE;
		auto& blend                               = pipeline.BlendState.RenderTarget[0];
		blend.BlendEnable                         = s.alpha_blend;
		blend.SrcBlend  = s.alpha_blend ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_ONE;
		blend.DestBlend = s.alpha_blend ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_ZERO;
		blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blend.SrcBlendAlpha                = D3D12_BLEND_ONE;
		blend.DestBlendAlpha        = s.alpha_blend ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_ZERO;
		blend.LogicOp               = D3D12_LOGIC_OP_NOOP;
		blend.RenderTargetWriteMask = s.write_mask;
		pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pipeline.NumRenderTargets      = 1;
		pipeline.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
		pipeline.SampleDesc.Count      = 1;
		ComPtr<ID3D12PipelineState> pso;
		CheckRaster(device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&pso)),
		            "Create GCN raster PSO");
		D3D12_HEAP_PROPERTIES heap {};
		heap.Type             = D3D12_HEAP_TYPE_DEFAULT;
		heap.CreationNodeMask = heap.VisibleNodeMask = 1;
		D3D12_RESOURCE_DESC texture_desc {};
		texture_desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texture_desc.Width            = s.width;
		texture_desc.Height           = s.height;
		texture_desc.DepthOrArraySize = texture_desc.MipLevels = texture_desc.SampleDesc.Count = 1;
		texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texture_desc.Flags  = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		D3D12_CLEAR_VALUE clear {};
		clear.Format = texture_desc.Format;
		std::memcpy(clear.Color, s.clear, sizeof(s.clear));
		ComPtr<ID3D12Resource> target, readback, upload;
		CheckRaster(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
		                                            s.initial_pixels.empty()
		                                                ? D3D12_RESOURCE_STATE_RENDER_TARGET
		                                                : D3D12_RESOURCE_STATE_COPY_DEST,
		                                            &clear, IID_PPV_ARGS(&target)),
		            "Create GCN raster target");
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
		UINT64                             bytes = 0;
		device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
		D3D12_RESOURCE_DESC buffer_desc {};
		buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		buffer_desc.Width     = bytes;
		buffer_desc.Height = buffer_desc.DepthOrArraySize = buffer_desc.MipLevels =
		    buffer_desc.SampleDesc.Count                  = 1;
		buffer_desc.Layout                                = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		heap.Type                                         = D3D12_HEAP_TYPE_READBACK;
		CheckRaster(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
		                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		                                            IID_PPV_ARGS(&readback)),
		            "Create raster readback");
		if (!s.initial_pixels.empty()) {
			heap.Type = D3D12_HEAP_TYPE_UPLOAD;
			CheckRaster(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
			                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			                                            IID_PPV_ARGS(&upload)),
			            "Create initial framebuffer upload");
			void*             data;
			const D3D12_RANGE empty {0, 0};
			CheckRaster(upload->Map(0, &empty, &data), "Map initial framebuffer");
			for (uint32_t y = 0; y < s.height; ++y)
				std::memcpy(static_cast<uint8_t*>(data) + footprint.Offset +
				                size_t(y) * footprint.Footprint.RowPitch,
				            s.initial_pixels.data() + size_t(y) * s.width * 4, size_t(s.width) * 4);
			upload->Unmap(0, nullptr);
		}
		D3D12_DESCRIPTOR_HEAP_DESC descriptors {};
		descriptors.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		descriptors.NumDescriptors = 1;
		ComPtr<ID3D12DescriptorHeap> rtv_heap;
		CheckRaster(device->CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(&rtv_heap)),
		            "Create raster RTV heap");
		const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
		device->CreateRenderTargetView(target.Get(), nullptr, rtv);
		ComPtr<ID3D12Resource>         shader_buffer[2 * Ps4::BuffersPerStage], shader_upload[2 * Ps4::BuffersPerStage];
		ComPtr<ID3D12Resource> shader_readback[2 * Ps4::BuffersPerStage];
		ComPtr<ID3D12DescriptorHeap>   shader_heap;
		const auto payloads = Ps4::RasterBuffers(s);
		const UINT buffer_count = UINT(payloads.size());
		const UINT                     increment =
		    device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		const auto make_uav = [&](ID3D12Resource* resource, UINT slot, UINT elements) {
			D3D12_UNORDERED_ACCESS_VIEW_DESC uav {};
			uav.Format = DXGI_FORMAT_R32_TYPELESS;
			uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uav.Buffer.NumElements = elements;
			uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
			auto handle = shader_heap->GetCPUDescriptorHandleForHeapStart();
			handle.ptr += SIZE_T(slot) * increment;
			device->CreateUnorderedAccessView(resource, nullptr, &uav, handle);
		};
		if (has_resources) {
			D3D12_DESCRIPTOR_HEAP_DESC views {};
			views.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			views.NumDescriptors = buffer_count + 2 * Ps4::ImagesPerStage;
			views.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			CheckRaster(device->CreateDescriptorHeap(&views, IID_PPV_ARGS(&shader_heap)),
			            "Create resource SRV heap");
			for (UINT i = 0; i < views.NumDescriptors; ++i) {
				if (i < buffer_count && writable_buffer(i)) {
					make_uav(nullptr, i, 4);
					continue;
				}
				D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
				srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				if (i < buffer_count) {
					srv.Format             = DXGI_FORMAT_R32_TYPELESS;
					srv.ViewDimension      = D3D12_SRV_DIMENSION_BUFFER;
					srv.Buffer.NumElements = 4;
					srv.Buffer.Flags       = D3D12_BUFFER_SRV_FLAG_RAW;
				} else {
					srv.Format              = DXGI_FORMAT_R8G8B8A8_UNORM;
					srv.ViewDimension       = D3D12_SRV_DIMENSION_TEXTURE2D;
					srv.Texture2D.MipLevels = 1;
				}
				auto handle = shader_heap->GetCPUDescriptorHandleForHeapStart();
				handle.ptr += SIZE_T(i) * increment;
				device->CreateShaderResourceView(nullptr, &srv, handle);
			}
		}
		for (UINT i = 0; i < buffer_count; ++i) {
			if (payloads[i].empty()) continue;
			D3D12_HEAP_PROPERTIES gpu_heap {}, cpu_heap {};
			gpu_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
			cpu_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
			D3D12_RESOURCE_DESC desc {};
			desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width            = payloads[i].size();
			desc.Height           = 1;
			desc.DepthOrArraySize = desc.MipLevels = 1;
			desc.SampleDesc.Count                  = 1;
			desc.Layout                            = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			if (writable_buffer(i)) desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			CheckRaster(device->CreateCommittedResource(&gpu_heap, D3D12_HEAP_FLAG_NONE, &desc,
			                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
			                                            IID_PPV_ARGS(&shader_buffer[i])),
			            "Create shader buffer");
			desc.Flags = D3D12_RESOURCE_FLAG_NONE;
			CheckRaster(device->CreateCommittedResource(&cpu_heap, D3D12_HEAP_FLAG_NONE, &desc,
			                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			                                            IID_PPV_ARGS(&shader_upload[i])),
			            "Create shader upload");
			void*       data;
			D3D12_RANGE empty {};
			CheckRaster(shader_upload[i]->Map(0, &empty, &data), "Map shader buffer");
			std::memcpy(data, payloads[i].data(), payloads[i].size());
			shader_upload[i]->Unmap(0, nullptr);
			if (writable_buffer(i)) {
				make_uav(shader_buffer[i].Get(), i, UINT(payloads[i].size() / 4));
				if (!s.storage_readback[i].empty()) {
					cpu_heap.Type = D3D12_HEAP_TYPE_READBACK;
					CheckRaster(device->CreateCommittedResource(&cpu_heap, D3D12_HEAP_FLAG_NONE, &desc,
					                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
					                                            IID_PPV_ARGS(&shader_readback[i])),
					            "Create PS4 shader readback");
				}
				continue;
			}
			D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
			srv.Format                  = DXGI_FORMAT_R32_TYPELESS;
			srv.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
			srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv.Buffer.NumElements      = UINT(payloads[i].size() / 4);
			srv.Buffer.Flags            = D3D12_BUFFER_SRV_FLAG_RAW;
			auto handle                 = shader_heap->GetCPUDescriptorHandleForHeapStart();
			handle.ptr += SIZE_T(i) * device->GetDescriptorHandleIncrementSize(
			                              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			device->CreateShaderResourceView(shader_buffer[i].Get(), &srv, handle);
		}

		ComPtr<ID3D12Resource>             textures[2 * Ps4::ImagesPerStage], texture_uploads[2 * Ps4::ImagesPerStage];
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT texture_footprints[2 * Ps4::ImagesPerStage] {};
		const auto image_payloads = Ps4::RasterImages(s);
		for (UINT i = 0; i < image_payloads.size(); ++i) {
			const auto& t = image_payloads[i];
			if (t.pixels.empty()) continue;
			D3D12_RESOURCE_DESC desc {};
			desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			desc.Width            = t.width;
			desc.Height           = t.height;
			desc.DepthOrArraySize = desc.MipLevels = 1;
			desc.Format                            = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count                  = 1;
			D3D12_HEAP_PROPERTIES heap {};
			heap.Type = D3D12_HEAP_TYPE_DEFAULT;
			CheckRaster(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
			                                            IID_PPV_ARGS(&textures[i])),
			            "Create shader image");
			UINT64 bytes = 0;
			device->GetCopyableFootprints(&desc, 0, 1, 0, &texture_footprints[i], nullptr, nullptr,
			                              &bytes);
			desc                  = {};
			desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width            = bytes;
			desc.Height           = 1;
			desc.DepthOrArraySize = desc.MipLevels = 1;
			desc.SampleDesc.Count                  = 1;
			desc.Layout                            = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			heap.Type                              = D3D12_HEAP_TYPE_UPLOAD;
			CheckRaster(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			                                            IID_PPV_ARGS(&texture_uploads[i])),
			            "Create image upload");
			void*       data;
			D3D12_RANGE empty {};
			CheckRaster(texture_uploads[i]->Map(0, &empty, &data), "Map image upload");
			for (UINT y = 0; y < t.height; ++y)
				std::memcpy(static_cast<uint8_t*>(data) + texture_footprints[i].Offset +
				                size_t(y) * texture_footprints[i].Footprint.RowPitch,
				            t.pixels.data() + size_t(y) * t.pitch, size_t(t.width) * 4);
			texture_uploads[i]->Unmap(0, nullptr);
			D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
			srv.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
			srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
			srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv.Texture2D.MipLevels     = 1;
			auto handle                 = shader_heap->GetCPUDescriptorHandleForHeapStart();
			handle.ptr += SIZE_T(buffer_count + i) * increment;
			device->CreateShaderResourceView(textures[i].Get(), &srv, handle);
		}
		ComPtr<ID3D12CommandAllocator>    allocator;
		ComPtr<ID3D12GraphicsCommandList> list;
		ComPtr<ID3D12Fence>               fence;
		CheckRaster(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
		                                           IID_PPV_ARGS(&allocator)),
		            "Create raster allocator");
		CheckRaster(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
		                                      pso.Get(), IID_PPV_ARGS(&list)),
		            "Create raster list");
		CheckRaster(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
		            "Create raster fence");
		list->SetGraphicsRootSignature(root.Get());
		uint32_t runtime[12] {};
		runtime[3] = 1; // Y flip, matching Vulkan's positive-height viewport.
		list->SetGraphicsRoot32BitConstants(0, 12, runtime, 0);
		if (has_resources) {
			for (UINT i = 0; i < buffer_count; ++i) {
				if (payloads[i].empty()) continue;
				list->CopyBufferRegion(shader_buffer[i].Get(), 0, shader_upload[i].Get(), 0,
				                       payloads[i].size());
				D3D12_RESOURCE_BARRIER barrier {};
				barrier.Type       = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition = {shader_buffer[i].Get(), 0, D3D12_RESOURCE_STATE_COPY_DEST,
				                      writable_buffer(i) ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS :
				                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
				list->ResourceBarrier(1, &barrier);
			}
			ID3D12DescriptorHeap* heaps[] = {shader_heap.Get()};
			list->SetDescriptorHeaps(1, heaps);
			list->SetGraphicsRootDescriptorTable(1,
			                                     shader_heap->GetGPUDescriptorHandleForHeapStart());
			auto image_table = shader_heap->GetGPUDescriptorHandleForHeapStart();
			image_table.ptr += UINT64(buffer_count) * increment;
			list->SetGraphicsRootDescriptorTable(3, image_table);
			const auto metadata = Ps4::RasterBufferMetadata(s);
			list->SetGraphicsRoot32BitConstants(2, UINT(metadata.size()), metadata.data(), 0);
			for (UINT i = 0; i < image_payloads.size(); ++i) {
				if (!textures[i]) continue;
				D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
				src.pResource       = texture_uploads[i].Get();
				src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				src.PlacedFootprint = texture_footprints[i];
				dst.pResource       = textures[i].Get();
				dst.Type            = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
				D3D12_RESOURCE_BARRIER barrier {};
				barrier.Type       = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition = {textures[i].Get(), 0, D3D12_RESOURCE_STATE_COPY_DEST,
				                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
				list->ResourceBarrier(1, &barrier);
			}
		}
		const D3D12_VIEWPORT viewport {0, 0, float(s.width), float(s.height), 0, 1};
		const D3D12_RECT     scissor {LONG(s.left), LONG(s.top), LONG(s.right), LONG(s.bottom)};
		list->RSSetViewports(1, &viewport);
		list->RSSetScissorRects(1, &scissor);
		list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		if (s.initial_pixels.empty())
			list->ClearRenderTargetView(rtv, s.clear, 0, nullptr);
		else {
			D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
			src.pResource       = upload.Get();
			src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			src.PlacedFootprint = footprint;
			dst.pResource       = target.Get();
			dst.Type            = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			D3D12_RESOURCE_BARRIER initial_barrier {};
			initial_barrier.Type       = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			initial_barrier.Transition = {target.Get(), 0, D3D12_RESOURCE_STATE_COPY_DEST,
			                              D3D12_RESOURCE_STATE_RENDER_TARGET};
			list->ResourceBarrier(1, &initial_barrier);
		}
		list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ComPtr<ID3D12Resource> index_buffer;
		if (!s.indices.empty()) {
			D3D12_HEAP_PROPERTIES cpu_heap {};
			cpu_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
			cpu_heap.CreationNodeMask = cpu_heap.VisibleNodeMask = 1;
			D3D12_RESOURCE_DESC desc {};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width = s.indices.size();
			desc.Height = 1;
			desc.DepthOrArraySize = desc.MipLevels = 1;
			desc.SampleDesc.Count = 1;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			CheckRaster(device->CreateCommittedResource(&cpu_heap, D3D12_HEAP_FLAG_NONE, &desc,
			                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			                                            IID_PPV_ARGS(&index_buffer)), "Create PS4 index upload");
			void* mapped;
			const D3D12_RANGE no_reads {0, 0};
			CheckRaster(index_buffer->Map(0, &no_reads, &mapped), "Map PS4 index upload");
			std::memcpy(mapped, s.indices.data(), s.indices.size());
			index_buffer->Unmap(0, nullptr);
			const D3D12_INDEX_BUFFER_VIEW view {index_buffer->GetGPUVirtualAddress(), UINT(s.indices.size()),
			    s.index_bytes == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT};
			list->IASetIndexBuffer(&view);
			list->DrawIndexedInstanced(s.vertex_count, 1, 0, 0, 0);
		} else
			list->DrawInstanced(s.vertex_count, 1, 0, 0);
		D3D12_RESOURCE_BARRIER barrier {};
		barrier.Type       = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition = {target.Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET,
		                      D3D12_RESOURCE_STATE_COPY_SOURCE};
		list->ResourceBarrier(1, &barrier);
		D3D12_TEXTURE_COPY_LOCATION source {}, dest {};
		source.pResource     = target.Get();
		source.Type          = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dest.pResource       = readback.Get();
		dest.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dest.PlacedFootprint = footprint;
		list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
		for (UINT i = 0; i < buffer_count; ++i) {
			if (!shader_readback[i]) continue;
			D3D12_RESOURCE_BARRIER barrier {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition = {shader_buffer[i].Get(), 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			                      D3D12_RESOURCE_STATE_COPY_SOURCE};
			list->ResourceBarrier(1, &barrier);
			list->CopyBufferRegion(shader_readback[i].Get(), 0, shader_buffer[i].Get(), 0, payloads[i].size());
		}
		CheckRaster(list->Close(), "Close raster list");
		ID3D12CommandList* lists[] {list.Get()};
		queue->ExecuteCommandLists(1, lists);
		HRESULT signal;
		while (FAILED(signal = queue->Signal(fence.Get(), 1)) &&
		       SUCCEEDED(device->GetDeviceRemovedReason())) {
			// Retain the submitted resources if a healthy queue cannot signal yet.
			SleepEx(1, FALSE);
		}
		CheckRaster(signal, "Signal raster fence");
		while (fence->GetCompletedValue() < 1) {
			CheckRaster(device->GetDeviceRemovedReason(), "Raster device status");
			SleepEx(1, FALSE);
		}
		CheckRaster(device->GetDeviceRemovedReason(), "Raster completed device status");
		void*             mapped = nullptr;
		const D3D12_RANGE range {0, SIZE_T(bytes)}, no_write {0, 0};
		CheckRaster(readback->Map(0, &range, &mapped), "Map raster readback");
		std::vector<uint8_t> pixels(size_t(s.width) * s.height * 4);
		for (uint32_t y = 0; y < s.height; ++y)
			std::memcpy(pixels.data() + size_t(y) * s.width * 4,
			            static_cast<uint8_t*>(mapped) + footprint.Offset +
			                size_t(y) * footprint.Footprint.RowPitch,
			            size_t(s.width) * 4);
		readback->Unmap(0, &no_write);
		std::array<std::vector<uint8_t>, 2 * Ps4::BuffersPerStage> storage;
		for (UINT i = 0; i < buffer_count; ++i) {
			if (!shader_readback[i]) continue;
			storage[i].resize(payloads[i].size());
			D3D12_RANGE read_range {0, storage[i].size()};
			void* data;
			CheckRaster(shader_readback[i]->Map(0, &read_range, &data), "Map PS4 shader readback");
			std::memcpy(storage[i].data(), data, storage[i].size());
			const D3D12_RANGE empty {};
			shader_readback[i]->Unmap(0, &empty);
		}
		for (UINT i = 0; i < buffer_count; ++i)
			if (!storage[i].empty())
				std::memcpy(s.storage_readback[i].data(), storage[i].data(), storage[i].size());
		image = std::move(pixels);
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}
} // namespace Libs::Graphics::D3D12
