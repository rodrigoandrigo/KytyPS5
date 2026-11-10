#include "graphics/host_gpu/renderer/ps4TransferProbe.h"

#include "graphics/host_gpu/renderer/ps4RasterProbe.h"

#include <cstring>
#include <stdexcept>

namespace Libs::Graphics::Vulkan {
namespace {
void CheckProbe(VkResult result, const char* operation) {
	if (result != VK_SUCCESS)
		throw std::runtime_error(std::string(operation) + " VkResult=" + std::to_string(result));
}
// clang-format off
#define PROBE_VK_FUNCTIONS(X) \
	X(CreateSampler) \
	X(DestroySampler) \
	X(CreateBuffer) \
	X(DestroyBuffer) \
	X(GetBufferMemoryRequirements) \
	X(AllocateMemory) \
	X(FreeMemory) \
	X(BindBufferMemory) \
	X(MapMemory) \
	X(UnmapMemory) \
	X(CreateImage) \
	X(DestroyImage) \
	X(GetImageMemoryRequirements) \
	X(BindImageMemory) \
	X(CreateCommandPool) \
	X(DestroyCommandPool) \
	X(AllocateCommandBuffers) \
	X(BeginCommandBuffer) \
	X(EndCommandBuffer) \
	X(CmdCopyBuffer) \
	X(CmdPipelineBarrier) \
	X(CmdCopyBufferToImage) \
	X(CmdCopyImageToBuffer) \
	X(CreateFence) \
	X(DestroyFence) \
	X(QueueSubmit) \
	X(WaitForFences) \
	X(DeviceWaitIdle) \
	X(CreateShaderModule) \
	X(DestroyShaderModule) \
	X(CreateRenderPass) \
	X(DestroyRenderPass) \
	X(CreateImageView) \
	X(DestroyImageView) \
	X(CreateFramebuffer) \
	X(DestroyFramebuffer) \
	X(CreatePipelineLayout) \
	X(DestroyPipelineLayout) \
	X(CreateGraphicsPipelines) \
	X(DestroyPipeline) \
	X(CmdBeginRenderPass) \
	X(CmdEndRenderPass) \
	X(CmdBindPipeline) \
	X(CmdDraw) \
	X(CmdDrawIndexed) \
	X(CmdBindIndexBuffer) \
	X(CreateDescriptorSetLayout) \
	X(DestroyDescriptorSetLayout) \
	X(CreateDescriptorPool) \
	X(DestroyDescriptorPool) \
	X(AllocateDescriptorSets) \
	X(UpdateDescriptorSets) \
	X(CmdBindDescriptorSets) \
	X(CmdPushConstants)
// clang-format on
constexpr uint32_t RasterImageCount = 2 * Ps4::ImagesPerStage;
constexpr uint32_t ImageUploadBase = 3 + 2 * Ps4::BuffersPerStage;
constexpr uint32_t IndexBufferSlot = ImageUploadBase + RasterImageCount;
constexpr uint32_t TargetMemorySlot = IndexBufferSlot + 1;
constexpr uint32_t ImageMemoryBase = TargetMemorySlot + 1;

struct Resources {
	const TransferDevice& host;
#define MEMBER(name) PFN_vk##name name = nullptr;
	PROBE_VK_FUNCTIONS(MEMBER)
#undef MEMBER
	VkBuffer              buffers[IndexBufferSlot + 1] {};
	VkDeviceMemory        memories[ImageMemoryBase + RasterImageCount] {};
	VkImage               image {};
	VkImage               textures[RasterImageCount] {};
	VkSampler             samplers[2] {};
	VkImageView           texture_views[RasterImageCount] {};
	VkCommandPool         pool {};
	VkFence               fence {};
	VkShaderModule        shaders[2] {};
	VkRenderPass          render_pass {};
	VkImageView           view {};
	VkFramebuffer         framebuffer {};
	VkPipelineLayout      layout {};
	VkPipeline            pipeline {};
	VkDescriptorSetLayout descriptor_layout {};
	VkDescriptorPool      descriptor_pool {};
	VkDescriptorSet       descriptor_set {};
	bool                  submitted = false;
	explicit Resources(const TransferDevice& d): host(d) {
#define LOAD(name)                                                                                 \
	name = reinterpret_cast<PFN_vk##name>(d.get_proc(d.device, "vk" #name));                       \
	if (!name) throw std::runtime_error("Missing vk" #name);
		PROBE_VK_FUNCTIONS(LOAD)
#undef LOAD
	}
	~Resources() {
		if (submitted) DeviceWaitIdle(host.device); // Also drains exceptional post-submit paths.
		if (fence) DestroyFence(host.device, fence, nullptr);
		if (pool) DestroyCommandPool(host.device, pool, nullptr);
		if (pipeline) DestroyPipeline(host.device, pipeline, nullptr);
		if (layout) DestroyPipelineLayout(host.device, layout, nullptr);
		if (descriptor_pool) DestroyDescriptorPool(host.device, descriptor_pool, nullptr);
		if (descriptor_layout) DestroyDescriptorSetLayout(host.device, descriptor_layout, nullptr);
		for (auto sampler: samplers)
			if (sampler) DestroySampler(host.device, sampler, nullptr);
		if (framebuffer) DestroyFramebuffer(host.device, framebuffer, nullptr);
		if (view) DestroyImageView(host.device, view, nullptr);
		if (render_pass) DestroyRenderPass(host.device, render_pass, nullptr);
		for (auto shader: shaders)
			if (shader) DestroyShaderModule(host.device, shader, nullptr);
		for (auto view: texture_views)
			if (view) DestroyImageView(host.device, view, nullptr);
		for (auto texture: textures)
			if (texture) DestroyImage(host.device, texture, nullptr);
		if (image) DestroyImage(host.device, image, nullptr);
		for (auto buffer: buffers)
			if (buffer) DestroyBuffer(host.device, buffer, nullptr);
		for (auto memory: memories)
			if (memory) FreeMemory(host.device, memory, nullptr);
	}
	void Allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags flags,
	              VkDeviceMemory& out) {
		for (uint32_t i = 0; i < host.memory.memoryTypeCount; ++i) {
			if ((req.memoryTypeBits & (1u << i)) &&
			    (host.memory.memoryTypes[i].propertyFlags & flags) == flags) {
				VkMemoryAllocateInfo info {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
				info.allocationSize  = req.size;
				info.memoryTypeIndex = i;
				CheckProbe(AllocateMemory(host.device, &info, nullptr, &out), "AllocateMemory");
				return;
			}
		}
		throw std::runtime_error("No compatible Vulkan probe memory type");
	}
	void Buffer(uint32_t index, uint64_t bytes, VkBufferUsageFlags usage,
	            VkMemoryPropertyFlags flags, uint32_t memory_slot = UINT32_MAX) {
		if (memory_slot == UINT32_MAX) memory_slot = index;
		VkBufferCreateInfo info {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		info.size  = bytes;
		info.usage = usage;
		CheckProbe(CreateBuffer(host.device, &info, nullptr, &buffers[index]), "CreateBuffer");
		VkMemoryRequirements req;
		GetBufferMemoryRequirements(host.device, buffers[index], &req);
		Allocate(req, flags, memories[memory_slot]);
		CheckProbe(BindBufferMemory(host.device, buffers[index], memories[memory_slot], 0),
		           "BindBufferMemory");
	}
};
#undef PROBE_VK_FUNCTIONS
} // namespace

bool ProbePs4Transfers(const TransferDevice& host, const Ps4::TransferPlan& plan,
                       std::vector<uint8_t>& pixels, std::string& error) {
	if (!Ps4::ValidatePlan(plan, error)) return false;
	if (!host.device || !host.queue || !host.get_proc) {
		error = "Invalid Vulkan transfer device";
		return false;
	}
	try {
		Resources      r(host);
		const auto     t             = plan.target;
		const uint64_t bytes         = uint64_t(t.pitch) * t.height;
		uint64_t       staging_bytes = bytes;
		for (const auto& w: plan.writes)
			staging_bytes += w.words.size() * 4;
		constexpr auto cpu =
		    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		r.Buffer(0, staging_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, cpu);
		r.Buffer(1, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0);
		r.Buffer(2, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, cpu);
		VkImageCreateInfo image {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format    = VK_FORMAT_R8G8B8A8_UNORM;
		image.extent    = {t.width, t.height, 1};
		image.mipLevels = image.arrayLayers = 1;
		image.samples                       = VK_SAMPLE_COUNT_1_BIT;
		image.tiling                        = VK_IMAGE_TILING_OPTIMAL;
		image.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		CheckProbe(r.CreateImage(host.device, &image, nullptr, &r.image), "CreateImage");
		VkMemoryRequirements req;
		r.GetImageMemoryRequirements(host.device, r.image, &req);
		r.Allocate(req, 0, r.memories[TargetMemorySlot]);
		CheckProbe(r.BindImageMemory(host.device, r.image, r.memories[TargetMemorySlot], 0), "BindImageMemory");
		void* mapped = nullptr;
		CheckProbe(r.MapMemory(host.device, r.memories[0], 0, staging_bytes, 0, &mapped),
		           "Map upload");
		std::memset(mapped, 0, size_t(bytes));
		uint64_t offset = bytes;
		for (const auto& w: plan.writes) {
			std::memcpy(static_cast<uint8_t*>(mapped) + offset, w.words.data(), w.words.size() * 4);
			offset += w.words.size() * 4;
		}
		r.UnmapMemory(host.device, r.memories[0]);
		VkCommandPoolCreateInfo pool {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		pool.queueFamilyIndex = host.family;
		CheckProbe(r.CreateCommandPool(host.device, &pool, nullptr, &r.pool), "CreateCommandPool");
		VkCommandBufferAllocateInfo allocate {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		allocate.commandPool        = r.pool;
		allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate.commandBufferCount = 1;
		VkCommandBuffer cmd;
		CheckProbe(r.AllocateCommandBuffers(host.device, &allocate, &cmd),
		           "AllocateCommandBuffers");
		VkCommandBufferBeginInfo begin {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		CheckProbe(r.BeginCommandBuffer(cmd, &begin), "BeginCommandBuffer");
		const auto transfer_dependency = [&] {
			VkMemoryBarrier barrier {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			r.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0,
			                     nullptr);
		};
		VkBufferCopy copy {0, 0, bytes};
		r.CmdCopyBuffer(cmd, r.buffers[0], r.buffers[1], 1, &copy);
		offset = bytes;
		for (const auto& w: plan.writes) {
			transfer_dependency();
			copy = {offset, w.offset, w.words.size() * 4};
			if (copy.size) r.CmdCopyBuffer(cmd, r.buffers[0], r.buffers[1], 1, &copy);
			offset += copy.size;
		}
		transfer_dependency();
		VkImageMemoryBarrier image_barrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		image_barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
		image_barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_barrier.srcQueueFamilyIndex = image_barrier.dstQueueFamilyIndex =
		    VK_QUEUE_FAMILY_IGNORED;
		image_barrier.image            = r.image;
		image_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		r.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                     0, 0, nullptr, 0, nullptr, 1, &image_barrier);
		VkBufferImageCopy region {};
		region.bufferRowLength  = t.pitch / 4;
		region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		region.imageExtent      = image.extent;
		r.CmdCopyBufferToImage(cmd, r.buffers[1], r.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
		                       &region);
		image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		image_barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		r.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
		                     0, nullptr, 0, nullptr, 1, &image_barrier);
		r.CmdCopyImageToBuffer(cmd, r.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r.buffers[2], 1,
		                       &region);
		VkMemoryBarrier host_barrier {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
		host_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		host_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		r.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
		                     &host_barrier, 0, nullptr, 0, nullptr);
		CheckProbe(r.EndCommandBuffer(cmd), "EndCommandBuffer");
		VkFenceCreateInfo fence {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		CheckProbe(r.CreateFence(host.device, &fence, nullptr, &r.fence), "CreateFence");
		VkSubmitInfo submit {VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submit.commandBufferCount = 1;
		submit.pCommandBuffers    = &cmd;
		CheckProbe(r.QueueSubmit(host.queue, 1, &submit, r.fence), "QueueSubmit");
		r.submitted = true;
		CheckProbe(r.WaitForFences(host.device, 1, &r.fence, VK_TRUE, UINT64_MAX), "WaitForFences");
		r.submitted = false;
		CheckProbe(r.MapMemory(host.device, r.memories[2], 0, bytes, 0, &mapped), "Map readback");
		std::vector<uint8_t> result(size_t(t.width) * t.height * 4);
		for (uint32_t y = 0; y < t.height; ++y)
			std::memcpy(result.data() + size_t(y) * t.width * 4,
			            static_cast<uint8_t*>(mapped) + size_t(y) * t.pitch, size_t(t.width) * 4);
		r.UnmapMemory(host.device, r.memories[2]);
		pixels = std::move(result);
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}
bool RasterizePs4Probe(const TransferDevice& host, const Ps4::RasterState& s,
                       std::span<const uint32_t> vertex, std::span<const uint32_t> pixel,
                       std::vector<uint8_t>& image, std::string& error) {
	if (!Ps4::ValidateRasterState(s, error)) return false;
	if (!host.device || !host.queue || !host.get_proc || vertex.empty() || pixel.empty()) {
		error = "Invalid Vulkan PS4 raster device/shaders";
		return false;
	}
	try {
		Resources                       r(host);
		const std::span<const uint32_t> codes[] {vertex, pixel};
		for (uint32_t i = 0; i < 2; ++i) {
			VkShaderModuleCreateInfo info {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			info.codeSize = codes[i].size_bytes();
			info.pCode    = codes[i].data();
			CheckProbe(r.CreateShaderModule(host.device, &info, nullptr, &r.shaders[i]),
			           "Create GCN shader module");
		}
		VkAttachmentDescription attachment {};
		attachment.format  = VK_FORMAT_R8G8B8A8_UNORM;
		attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment.loadOp =
		    s.initial_pixels.empty() ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout  = s.initial_pixels.empty()
		                                ? VK_IMAGE_LAYOUT_UNDEFINED
		                                : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachment.finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		VkAttachmentReference color {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		VkSubpassDescription  subpass {};
		subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments    = &color;
		VkSubpassDependency dependencies[2] {};
		dependencies[0].srcSubpass   = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass   = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].dstAccessMask =
		    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].srcSubpass    = 0;
		dependencies[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		VkRenderPassCreateInfo rp {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
		rp.attachmentCount = 1;
		rp.pAttachments    = &attachment;
		rp.subpassCount    = 1;
		rp.pSubpasses      = &subpass;
		rp.dependencyCount = 2;
		rp.pDependencies   = dependencies;
		CheckProbe(r.CreateRenderPass(host.device, &rp, nullptr, &r.render_pass),
		           "Create GCN render pass");
		VkImageCreateInfo texture {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		texture.imageType = VK_IMAGE_TYPE_2D;
		texture.format    = attachment.format;
		texture.extent    = {s.width, s.height, 1};
		texture.mipLevels = texture.arrayLayers = 1;
		texture.samples                         = VK_SAMPLE_COUNT_1_BIT;
		texture.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		                VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		CheckProbe(r.CreateImage(host.device, &texture, nullptr, &r.image),
		           "Create GCN render target");
		VkMemoryRequirements req;
		r.GetImageMemoryRequirements(host.device, r.image, &req);
		r.Allocate(req, 0, r.memories[TargetMemorySlot]);
		CheckProbe(r.BindImageMemory(host.device, r.image, r.memories[TargetMemorySlot], 0), "Bind raster target");
		VkImageViewCreateInfo view {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		view.image            = r.image;
		view.viewType         = VK_IMAGE_VIEW_TYPE_2D;
		view.format           = attachment.format;
		view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		CheckProbe(r.CreateImageView(host.device, &view, nullptr, &r.view), "Create raster view");
		VkFramebufferCreateInfo fb {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
		fb.renderPass      = r.render_pass;
		fb.attachmentCount = 1;
		fb.pAttachments    = &r.view;
		fb.width           = s.width;
		fb.height          = s.height;
		fb.layers          = 1;
		CheckProbe(r.CreateFramebuffer(host.device, &fb, nullptr, &r.framebuffer),
		           "Create raster framebuffer");
		VkPipelineLayoutCreateInfo   layout {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		constexpr VkShaderStageFlags buffer_stages =
		    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		VkPushConstantRange            push {buffer_stages, 0, 128};
		const auto payloads = Ps4::RasterBuffers(s);
		const auto image_payloads = Ps4::RasterImages(s);
		VkDescriptorBufferInfo         buffer_infos[2 * Ps4::BuffersPerStage] {};
		VkDescriptorImageInfo          image_infos[RasterImageCount] {};
		std::vector<VkDescriptorSetLayoutBinding> bindings;
		std::vector<VkWriteDescriptorSet>         writes;
		uint32_t                                  buffers_used = 0, images_used = 0;
		for (uint32_t i = 0; i < payloads.size(); ++i) {
			if (payloads[i].empty()) continue;
			r.Buffer(3 + i, payloads[i].size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			void* data;
			CheckProbe(r.MapMemory(host.device, r.memories[3 + i], 0, payloads[i].size(), 0, &data),
			           "Map shader buffer");
			std::memcpy(data, payloads[i].data(), payloads[i].size());
			r.UnmapMemory(host.device, r.memories[3 + i]);
			buffer_infos[i] = {r.buffers[3 + i], 0, payloads[i].size()};
			++buffers_used;
		}
		for (uint32_t stage = 0; stage < 2; ++stage) {
			const uint32_t           first = stage * Ps4::BuffersPerStage;
			uint32_t count = 0;
			while (count < Ps4::BuffersPerStage && !payloads[first + count].empty()) ++count;
			const VkShaderStageFlags visibility =
			    stage ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
			if (count) {
				bindings.push_back({Ps4::BufferBinding + first, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count,
				                    visibility, nullptr});
				VkWriteDescriptorSet write {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
				write.dstBinding      = Ps4::BufferBinding + first;
				write.descriptorCount = count;
				write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				write.pBufferInfo     = &buffer_infos[first];
				writes.push_back(write);
			}
			const uint32_t first_image = stage * Ps4::ImagesPerStage;
			uint32_t image_count = 0;
			for (uint32_t slot = first_image; slot < first_image + Ps4::ImagesPerStage; ++slot) {
				const auto& t = image_payloads[slot];
				if (t.pixels.empty()) break;
				VkImageCreateInfo ci {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
				ci.imageType = VK_IMAGE_TYPE_2D;
				ci.format    = VK_FORMAT_R8G8B8A8_UNORM;
				ci.extent    = {t.width, t.height, 1};
				ci.mipLevels = ci.arrayLayers = 1;
				ci.samples                    = VK_SAMPLE_COUNT_1_BIT;
				ci.tiling                     = VK_IMAGE_TILING_OPTIMAL;
				ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				CheckProbe(r.CreateImage(host.device, &ci, nullptr, &r.textures[slot]),
				           "Create shader image");
				VkMemoryRequirements req;
				r.GetImageMemoryRequirements(host.device, r.textures[slot], &req);
				r.Allocate(req, 0, r.memories[ImageMemoryBase + slot]);
				CheckProbe(r.BindImageMemory(host.device, r.textures[slot], r.memories[ImageMemoryBase + slot], 0),
				           "Bind shader image");
				VkImageViewCreateInfo view {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
				view.image            = r.textures[slot];
				view.viewType         = VK_IMAGE_VIEW_TYPE_2D;
				view.format           = ci.format;
				view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
				CheckProbe(r.CreateImageView(host.device, &view, nullptr, &r.texture_views[slot]),
				           "Create shader image view");
				r.Buffer(ImageUploadBase + slot, t.pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				void* data;
				CheckProbe(
				    r.MapMemory(host.device, r.memories[ImageUploadBase + slot], 0, t.pixels.size(), 0, &data),
				    "Map image upload");
				std::memcpy(data, t.pixels.data(), t.pixels.size());
				r.UnmapMemory(host.device, r.memories[ImageUploadBase + slot]);
				image_infos[slot] = {VK_NULL_HANDLE, r.texture_views[slot],
				                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
				++image_count;
			}
			if (!image_count) continue;
			bindings.push_back(
			    {Ps4::ImageBinding + first_image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, image_count, visibility, nullptr});
			VkWriteDescriptorSet write {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
			write.dstBinding      = Ps4::ImageBinding + first_image;
			write.descriptorCount = image_count;
			write.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			write.pImageInfo      = &image_infos[first_image];
			writes.push_back(write);
			VkSamplerCreateInfo sampler {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
			sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
			sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
			    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sampler.maxAnisotropy = 1;
			sampler.minLod = sampler.maxLod = 0;
			CheckProbe(r.CreateSampler(host.device, &sampler, nullptr, &r.samplers[stage]),
			           "Create PS4 nearest/clamp sampler");
			// Immutable sampler: no extra descriptor payload or dangling local array.
			bindings.push_back({Ps4::SamplerBinding + stage, VK_DESCRIPTOR_TYPE_SAMPLER, 1, visibility,
			                    &r.samplers[stage]});
			images_used += image_count;
		}
		const bool has_resources = !bindings.empty();
		if (has_resources) {
			VkDescriptorSetLayoutCreateInfo ci {
			    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
			ci.bindingCount = uint32_t(bindings.size());
			ci.pBindings    = bindings.data();
			CheckProbe(r.CreateDescriptorSetLayout(host.device, &ci, nullptr, &r.descriptor_layout),
			           "Create resource layout");
			std::vector<VkDescriptorPoolSize> sizes;
			if (buffers_used) sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffers_used});
			if (images_used) {
				sizes.push_back({VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, images_used});
				sizes.push_back({VK_DESCRIPTOR_TYPE_SAMPLER, images_used});
			}
			VkDescriptorPoolCreateInfo pool {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
			pool.maxSets       = 1;
			pool.poolSizeCount = uint32_t(sizes.size());
			pool.pPoolSizes    = sizes.data();
			CheckProbe(r.CreateDescriptorPool(host.device, &pool, nullptr, &r.descriptor_pool),
			           "Create resource pool");
			VkDescriptorSetAllocateInfo alloc {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
			alloc.descriptorPool     = r.descriptor_pool;
			alloc.descriptorSetCount = 1;
			alloc.pSetLayouts        = &r.descriptor_layout;
			CheckProbe(r.AllocateDescriptorSets(host.device, &alloc, &r.descriptor_set),
			           "Allocate resource set");
			for (auto& write: writes)
				write.dstSet = r.descriptor_set;
			r.UpdateDescriptorSets(host.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
			layout.setLayoutCount         = 1;
			layout.pSetLayouts            = &r.descriptor_layout;
			layout.pushConstantRangeCount = 1;
			layout.pPushConstantRanges    = &push;
		}

		CheckProbe(r.CreatePipelineLayout(host.device, &layout, nullptr, &r.layout),
		           "Create raster pipeline layout");
		VkPipelineShaderStageCreateInfo stages[2] {};
		for (uint32_t i = 0; i < 2; ++i) {
			stages[i].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[i].stage  = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[i].module = r.shaders[i];
			stages[i].pName  = "main";
		}
		VkPipelineVertexInputStateCreateInfo vi {
		    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		VkPipelineInputAssemblyStateCreateInfo ia {
		    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VkViewport viewport {0, 0, float(s.width), float(s.height), 0, 1};
		VkRect2D scissor {{int32_t(s.left), int32_t(s.top)}, {s.right - s.left, s.bottom - s.top}};
		VkPipelineViewportStateCreateInfo vp {
		    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vp.viewportCount = vp.scissorCount = 1;
		vp.pViewports                      = &viewport;
		vp.pScissors                       = &scissor;
		VkPipelineRasterizationStateCreateInfo rs {
		    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode    = VK_CULL_MODE_NONE;
		rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth   = 1;
		VkPipelineMultisampleStateCreateInfo ms {
		    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		VkPipelineColorBlendAttachmentState blend {};
		blend.blendEnable         = s.alpha_blend;
		blend.srcColorBlendFactor = s.alpha_blend ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
		blend.dstColorBlendFactor =
		    s.alpha_blend ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO;
		blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
		blend.srcAlphaBlendFactor               = VK_BLEND_FACTOR_ONE;
		blend.dstAlphaBlendFactor =
		    s.alpha_blend ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO;
		blend.colorWriteMask = s.write_mask;
		VkPipelineColorBlendStateCreateInfo cb {
		    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount = 1;
		cb.pAttachments    = &blend;
		VkGraphicsPipelineCreateInfo pipeline {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		pipeline.stageCount          = 2;
		pipeline.pStages             = stages;
		pipeline.pVertexInputState   = &vi;
		pipeline.pInputAssemblyState = &ia;
		pipeline.pViewportState      = &vp;
		pipeline.pRasterizationState = &rs;
		pipeline.pMultisampleState   = &ms;
		pipeline.pColorBlendState    = &cb;
		pipeline.layout              = r.layout;
		pipeline.renderPass          = r.render_pass;
		CheckProbe(r.CreateGraphicsPipelines(host.device, VK_NULL_HANDLE, 1, &pipeline, nullptr,
		                                     &r.pipeline),
		           "Create GCN raster pipeline");
		const uint64_t bytes = uint64_t(s.width) * s.height * 4;
		r.Buffer(2, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (!s.initial_pixels.empty()) {
			r.Buffer(0, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			void* data;
			CheckProbe(r.MapMemory(host.device, r.memories[0], 0, bytes, 0, &data),
			           "Map initial framebuffer");
			std::memcpy(data, s.initial_pixels.data(), size_t(bytes));
			r.UnmapMemory(host.device, r.memories[0]);
		}
		if (!s.indices.empty()) {
			r.Buffer(IndexBufferSlot, s.indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			void* data;
			CheckProbe(r.MapMemory(host.device, r.memories[IndexBufferSlot], 0, s.indices.size(), 0, &data), "Map PS4 indices");
			std::memcpy(data, s.indices.data(), s.indices.size());
			r.UnmapMemory(host.device, r.memories[IndexBufferSlot]);
		}
		VkCommandPoolCreateInfo pool {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		pool.queueFamilyIndex = host.family;
		CheckProbe(r.CreateCommandPool(host.device, &pool, nullptr, &r.pool), "Create raster pool");
		VkCommandBufferAllocateInfo allocate {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		allocate.commandPool        = r.pool;
		allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate.commandBufferCount = 1;
		VkCommandBuffer cmd;
		CheckProbe(r.AllocateCommandBuffers(host.device, &allocate, &cmd),
		           "Allocate raster command buffer");
		VkCommandBufferBeginInfo begin {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		CheckProbe(r.BeginCommandBuffer(cmd, &begin), "Begin raster commands");
		if (!s.indices.empty()) {
			VkBufferMemoryBarrier barrier {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
			barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
			barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer = r.buffers[IndexBufferSlot];
			barrier.size = VK_WHOLE_SIZE;
			r.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
			                     0, 0, nullptr, 1, &barrier, 0, nullptr);
		}
		for (uint32_t i = 0; i < RasterImageCount; ++i) {
			if (!r.textures[i]) continue;
			VkImageMemoryBarrier barrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			barrier.image               = r.textures[i];
			barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.oldLayout                                         = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			r.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
			                     &barrier);
			VkBufferImageCopy copy {};
			copy.bufferRowLength  = image_payloads[i].pitch / 4;
			copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			copy.imageExtent      = {image_payloads[i].width, image_payloads[i].height, 1};
			r.CmdCopyBufferToImage(cmd, r.buffers[ImageUploadBase + i], r.textures[i],
			                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
			barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			r.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                     0, 0, nullptr, 0, nullptr, 1, &barrier);
		}

		if (!s.initial_pixels.empty()) {
			VkImageMemoryBarrier barrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			barrier.image               = r.image;
			barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.oldLayout                                         = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			r.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
			                     &barrier);
			VkBufferImageCopy copy {};
			copy.imageExtent      = texture.extent;
			copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			r.CmdCopyBufferToImage(cmd, r.buffers[0], r.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                       1, &copy);
			barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask =
			    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			r.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
			                     nullptr, 1, &barrier);
		}
		VkClearValue clear {};
		std::memcpy(clear.color.float32, s.clear, sizeof(s.clear));
		VkRenderPassBeginInfo pass {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
		pass.renderPass        = r.render_pass;
		pass.framebuffer       = r.framebuffer;
		pass.renderArea.extent = {s.width, s.height};
		pass.clearValueCount   = 1;
		pass.pClearValues      = &clear;
		r.CmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
		r.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.pipeline);
		if (has_resources) {
			r.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.layout, 0, 1,
			                        &r.descriptor_set, 0, nullptr);
			const auto data = Ps4::RasterBufferMetadata(s);
			r.CmdPushConstants(cmd, r.layout, buffer_stages, 0, sizeof(data), data.data());
		}
		if (!s.indices.empty()) {
			r.CmdBindIndexBuffer(cmd, r.buffers[IndexBufferSlot], 0, s.index_bytes == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
			r.CmdDrawIndexed(cmd, s.vertex_count, 1, 0, 0, 0);
		} else
			r.CmdDraw(cmd, s.vertex_count, 1, 0, 0);
		r.CmdEndRenderPass(cmd);
		std::vector<VkBufferMemoryBarrier> storage_barriers;
		for (uint32_t i = 0; i < payloads.size(); ++i) {
			if (s.storage_readback[i].empty()) continue;
			VkBufferMemoryBarrier barrier {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
			barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer = r.buffers[3 + i];
			barrier.size = VK_WHOLE_SIZE;
			storage_barriers.push_back(barrier);
		}
		if (!storage_barriers.empty())
			r.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                     VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
			                     uint32_t(storage_barriers.size()), storage_barriers.data(), 0, nullptr);
		VkBufferImageCopy region {};
		region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		region.imageExtent      = texture.extent;
		r.CmdCopyImageToBuffer(cmd, r.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r.buffers[2], 1,
		                       &region);
		VkMemoryBarrier barrier {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		r.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
		                     &barrier, 0, nullptr, 0, nullptr);
		CheckProbe(r.EndCommandBuffer(cmd), "End raster commands");
		VkFenceCreateInfo fence {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		CheckProbe(r.CreateFence(host.device, &fence, nullptr, &r.fence), "Create raster fence");
		VkSubmitInfo submit {VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submit.commandBufferCount = 1;
		submit.pCommandBuffers    = &cmd;
		CheckProbe(r.QueueSubmit(host.queue, 1, &submit, r.fence), "Submit GCN raster");
		r.submitted = true;
		CheckProbe(r.WaitForFences(host.device, 1, &r.fence, VK_TRUE, UINT64_MAX),
		           "Wait GCN raster");
		r.submitted = false;
		void* mapped;
		CheckProbe(r.MapMemory(host.device, r.memories[2], 0, bytes, 0, &mapped),
		           "Map raster pixels");
		std::vector<uint8_t> pixels(static_cast<size_t>(bytes));
		std::memcpy(pixels.data(), mapped, size_t(bytes));
		r.UnmapMemory(host.device, r.memories[2]);
		std::array<std::vector<uint8_t>, 2 * Ps4::BuffersPerStage> storage;
		for (uint32_t i = 0; i < payloads.size(); ++i) {
			if (s.storage_readback[i].empty()) continue;
			storage[i].resize(payloads[i].size());
			void* data;
			CheckProbe(r.MapMemory(host.device, r.memories[3 + i], 0, storage[i].size(), 0, &data),
			           "Map PS4 shader readback");
			std::memcpy(storage[i].data(), data, storage[i].size());
			r.UnmapMemory(host.device, r.memories[3 + i]);
		}
		for (uint32_t i = 0; i < payloads.size(); ++i)
			if (!storage[i].empty())
				std::memcpy(s.storage_readback[i].data(), storage[i].data(), storage[i].size());
		image = std::move(pixels);
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}
} // namespace Libs::Graphics::Vulkan
