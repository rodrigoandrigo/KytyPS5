#include "graphics/shader/recompiler/GcnCompiler.h"

#include "graphics/shader/recompiler/ShaderRecompiler.h"

namespace Libs::Graphics::ShaderRecompiler::Gcn {
bool Compile(std::span<const uint32_t> code, Stage stage, std::vector<uint32_t>& spirv,
             std::string& error) {
	Decoder::Program decoded;
	if (!Decode(code, stage, decoded, error)) return false;
	ShaderVertexInputInfo vertex;
	ShaderPixelInputInfo  pixel;
	CompileOptions        options;
	options.stage             = stage == Stage::Vertex ? ShaderType::Vertex : ShaderType::Pixel;
	options.gcn_vertex_inputs = true;
	options.wave_size         = 64;
	options.dump_ir           = false;
	if (stage == Stage::Vertex)
		options.input_info.vertex = &vertex;
	else
		options.input_info.pixel = &pixel;
	auto                       translated = TranslateProgram(decoded, options);
	IR::ResourceSpecialization specialization;
	auto result = CompileProgram(std::move(translated), options, specialization);
	spirv       = std::move(result.spirv);
	return true;
}
} // namespace Libs::Graphics::ShaderRecompiler::Gcn
