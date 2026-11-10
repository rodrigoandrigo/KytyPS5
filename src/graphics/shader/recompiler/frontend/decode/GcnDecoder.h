#pragma once
#include "graphics/shader/recompiler/frontend/decode/ShaderDecoder.h"

#include <string>

namespace Libs::Graphics::ShaderRecompiler::Gcn {
enum class Stage { Vertex, Pixel };
bool ScalarSourceIsPair(const Decoder::Instruction& inst, uint32_t source);
// Bounded forward-CFG GCN subset with definite register assignment on all paths
// and one POS0/MRT0 export per exit. Never calls the RDNA instruction decoder.
bool                  Decode(std::span<const uint32_t> code, Stage stage, Decoder::Program& output,
                             std::string& error, uint32_t parameter_count = 0);
std::vector<uint32_t> TriangleVertexFixture(bool fullscreen = true);
std::vector<uint32_t> SolidPixelFixture(float red = 0.25f, float green = 0.5f, float blue = 0.75f,
                                        float alpha = 1.f);
std::vector<uint32_t> UniformBranchPixelFixture();
std::vector<uint32_t> IndexedBufferPixelFixture(bool dword = false);
std::vector<uint32_t> FetchVertexFixture();
std::vector<uint32_t> TypedFetchFixture();
std::vector<uint32_t> ExternalFetchVertexFixture();
std::vector<uint32_t> ExternalFetchFunctionFixture();
std::vector<uint32_t> TextureVertexFixture();
std::vector<uint32_t> TexturePixelFixture();
std::vector<uint32_t> SampledTextureFixture(Stage stage);
std::vector<uint32_t> TableSampledTextureFixture(Stage stage);
std::vector<uint32_t> BufferPixelFixture(uint32_t dword_offset = 0);
std::vector<uint32_t> InterpolatedVertexFixture(bool perspective = false);
std::vector<uint32_t> InterpolatedPixelFixture(bool arithmetic = false);
std::vector<uint32_t> FlatPixelFixture();
std::vector<uint32_t> IntegerComparePixelFixture(bool unsigned_compare = false);
std::vector<uint32_t> CmpxPixelFixture(uint32_t kind = 0);
std::vector<uint32_t> MaskedPixelFixture(bool nested = false, bool select = false);
std::vector<uint32_t> ElsePixelFixture(bool guarded = true, bool nested = false, uint32_t predicate = 1);
} // namespace Libs::Graphics::ShaderRecompiler::Gcn
