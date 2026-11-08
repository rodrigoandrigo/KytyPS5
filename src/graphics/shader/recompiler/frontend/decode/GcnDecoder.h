#pragma once
#include "graphics/shader/recompiler/frontend/decode/ShaderDecoder.h"

#include <string>

namespace Libs::Graphics::ShaderRecompiler::Gcn {
enum class Stage { Vertex, Pixel };
// Bounded forward-CFG GCN subset with definite register assignment on all paths
// and one POS0/MRT0 export per exit. Never calls the RDNA instruction decoder.
bool                  Decode(std::span<const uint32_t> code, Stage stage, Decoder::Program& output,
                             std::string& error);
std::vector<uint32_t> TriangleVertexFixture(bool fullscreen = true);
std::vector<uint32_t> SolidPixelFixture(float red = 0.25f, float green = 0.5f, float blue = 0.75f,
                                        float alpha = 1.f);
std::vector<uint32_t> UniformBranchPixelFixture();
} // namespace Libs::Graphics::ShaderRecompiler::Gcn
