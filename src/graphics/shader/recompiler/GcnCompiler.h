#pragma once
#include "graphics/shader/recompiler/frontend/decode/GcnDecoder.h"

namespace Libs::Graphics::ShaderRecompiler::Gcn {
// Validates GCN first, translates the supported semantic instructions to shared
// IR and emits SPIR-V. No textures, fetch shaders, user data or interpolation.
bool Compile(std::span<const uint32_t> code, Stage stage, std::vector<uint32_t>& spirv,
             std::string& error, std::span<const uint32_t> user_data = {});
} // namespace Libs::Graphics::ShaderRecompiler::Gcn
