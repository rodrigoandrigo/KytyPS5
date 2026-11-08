#pragma once
#include "graphics/shader/recompiler/frontend/decode/ShaderDecoder.h"

#include <string>

namespace Libs::Graphics::ShaderRecompiler::Gcn {
enum class Stage { Vertex, Pixel };
// Bounded straight-line GCN subset with defined register reads and a single
// uncompressed POS0/MRT0 export. Never calls the RDNA instruction decoder.
bool                  Decode(std::span<const uint32_t> code, Stage stage, Decoder::Program& output,
                             std::string& error);
std::vector<uint32_t> TriangleVertexFixture(bool fullscreen = true);
std::vector<uint32_t> SolidPixelFixture(float red = 0.25f, float green = 0.5f, float blue = 0.75f,
                                        float alpha = 1.f);
} // namespace Libs::Graphics::ShaderRecompiler::Gcn
