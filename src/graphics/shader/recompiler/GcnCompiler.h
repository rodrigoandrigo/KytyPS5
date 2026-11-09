#pragma once
#include "graphics/shader/recompiler/frontend/decode/GcnDecoder.h"
#include <functional>

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
bool ValidateParameterMap(std::span<const uint32_t> map, uint32_t count, std::string& error);
bool DecodeImageDescriptor(std::span<const uint32_t> words, ImageDescriptor& output,
                           std::string& error);
bool DecodeBufferDescriptor(std::span<const uint32_t> words, BufferDescriptor& output,
                            std::string& error, bool indexed = false);
// Validates GCN first, translates the supported semantic instructions to shared
// IR and emits SPIR-V. Scalar buffer widths 1/2/4/8/16, indexed MUBUF and
// bounded 2D RGBA8 IMAGE_LOAD / nearest-clamp IMAGE_SAMPLE_L share the
// existing IR. An initial static VS fetch call may contain a scalar table prelude,
// bounded read-only vector loads and a matching return; the caller selects the parameter ABI.
// Memory access is supplied by the leased PS4 runtime, never by dereferencing
// guest addresses in the compiler. Reads are complete bounded snapshots.
using ShaderMemory = std::function<bool(uint64_t, std::span<uint8_t>)>;
struct ScalarRead {
	uint64_t address = 0;
	uint32_t bytes = 0;
};
bool Compile(std::span<const uint32_t> code, Stage stage, std::vector<uint32_t>& spirv,
             std::string& error, std::span<const uint32_t> user_data = {},
             uint32_t parameter_count = 0, BufferDescriptor* buffer = nullptr,
             std::span<const uint32_t> parameter_map = {}, bool no_perspective = false,
             std::vector<BufferDescriptor>* resources = nullptr, ImageDescriptor* image = nullptr,
             const ShaderMemory& memory = {}, std::vector<ScalarRead>* scalar_reads = nullptr,
             std::vector<uint32_t>* normalized_code = nullptr,
             uint32_t* scalar_registers = nullptr);
} // namespace Libs::Graphics::ShaderRecompiler::Gcn
