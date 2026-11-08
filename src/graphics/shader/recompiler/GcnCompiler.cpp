#include "graphics/shader/recompiler/GcnCompiler.h"

#include "graphics/shader/recompiler/ShaderRecompiler.h"

namespace Libs::Graphics::ShaderRecompiler::Gcn {
bool Compile(std::span<const uint32_t> code, Stage stage, std::vector<uint32_t>& spirv,
             std::string& error, std::span<const uint32_t> user_data) {
	// Freeze user SGPRs as explicit GCN moves. Relative branches retain their
	// offsets and no unbound host push constants are introduced.
	if (user_data.size() > 16 || code.size() + user_data.size() * 2 > 4096) {
		error = "GCN code/user data budget exceeded";
		return false;
	}
	std::vector<uint32_t> specialized;
	if (!user_data.empty()) {
		for (uint32_t i = 0; i < user_data.size(); ++i) {
			specialized.push_back(0xbe8003ffu | (i << 16));
			specialized.push_back(user_data[i]);
		}
		specialized.insert(specialized.end(), code.begin(), code.end());
		code = specialized;
	}
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
