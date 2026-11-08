#pragma once
#include "graphics/shader/recompiler/frontend/decode/GcnDecoder.h"

namespace Libs::Graphics::ShaderRecompiler::Gcn {
struct BufferDescriptor {
	uint64_t address = 0;
	uint32_t size    = 0;
	uint32_t stride  = 0;
};
struct ImageDescriptor {
	uint64_t address = 0;
	uint32_t width = 0, height = 0, pitch = 0, size = 0; // pitch/size in bytes
};
bool DecodeImageDescriptor(std::span<const uint32_t> words, ImageDescriptor& output,
                           std::string& error);
bool DecodeBufferDescriptor(std::span<const uint32_t> words, BufferDescriptor& output,
                            std::string& error, bool indexed = false);
// Validates GCN first, translates the supported semantic instructions to shared
// IR and emits SPIR-V. Indexed MUBUF and bounded 2D RGBA8 IMAGE_LOAD share the
// existing IR. External fetch programs remain unsupported; the caller selects the parameter ABI.
bool Compile(std::span<const uint32_t> code, Stage stage, std::vector<uint32_t>& spirv,
             std::string& error, std::span<const uint32_t> user_data = {},
             uint32_t parameter_count = 0, BufferDescriptor* buffer = nullptr,
             std::span<const uint32_t> parameter_map = {}, bool no_perspective = false,
             std::vector<BufferDescriptor>* resources = nullptr, ImageDescriptor* image = nullptr);
} // namespace Libs::Graphics::ShaderRecompiler::Gcn
