#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "common/subsystems.h"
#include "common/threads.h"
#include "graphics/shader/recompiler/GcnCompiler.h"
#include "graphics/shader/recompiler/ShaderRecompiler.h"
#include "spirv-tools/libspirv.hpp"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace Libs::Graphics;
namespace Gcn = ShaderRecompiler::Gcn;
static void Check(bool condition, const std::string& message) {
	if (!condition) {
		std::fprintf(stderr, "PS4_GCN FAIL: %s\n", message.c_str());
		std::_Exit(1);
	}
}
int main(int argc, char** argv) {
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	Common::Subsystems subsystems;
	Common::InitializeThreads();
	subsystems.Initialize<Config::Lifecycle>();
	Config::ConfigOptions config;
	config.printf_direction = Config::LogDirection::Silent;
	Config::Load(config);
	subsystems.Initialize<Log::Lifecycle>();
	ShaderInit();
	const std::filesystem::path folder = argc == 2 ? argv[1] : "ps4-gcn-fixture";
	std::filesystem::create_directories(folder);
	std::string error;
	const auto  vertex = Gcn::TriangleVertexFixture(), pixel = Gcn::SolidPixelFixture();
	ShaderRecompiler::Decoder::Program decoded;
	Check(Gcn::Decode(vertex, Gcn::Stage::Vertex, decoded, error), error);
	const auto count  = decoded.instructions.size();
	const auto reject = [&](std::vector<uint32_t> words, Gcn::Stage stage) {
		Check(!Gcn::Decode(words, stage, decoded, error) && !error.empty(),
		      "unsupported shader accepted");
		Check(decoded.instructions.size() == count && decoded.code.data() == vertex.data(),
		      "failure changed output");
	};
	for (size_t i = 0; i < vertex.size(); ++i)
		reject({vertex.begin(), vertex.begin() + i}, Gcn::Stage::Vertex);
	for (size_t i = 0; i < pixel.size(); ++i)
		reject({pixel.begin(), pixel.begin() + i}, Gcn::Stage::Pixel);
	{
		auto bad = vertex;
		bad.push_back(0xbf810000);
		reject(bad, Gcn::Stage::Vertex);
	}
	{
		auto bad = vertex;
		bad[0]   = 0xd0000000;
		reject(bad, Gcn::Stage::Vertex);
	}
	{
		auto bad = vertex;
		bad[0]   = 0xd4000000;
		reject(bad, Gcn::Stage::Vertex);
	}
	{
		auto bad = vertex;
		bad[0]   = 0xc0000000;
		reject(bad, Gcn::Stage::Vertex);
	}
	{
		auto bad = vertex;
		bad[0]   = 0x7e0400f9;
		reject(bad, Gcn::Stage::Vertex);
	}
	{
		auto bad = vertex;
		bad[0] &= ~0x1ffu;
		reject(bad, Gcn::Stage::Vertex);
	}
	{
		auto bad = vertex;
		bad[0] |= 255u << 9;
		reject(bad, Gcn::Stage::Vertex);
	}
	{
		auto bad = pixel;
		bad[bad.size() - 3] |= 1u << 10;
		reject(bad, Gcn::Stage::Pixel);
	}
	reject(vertex, Gcn::Stage::Pixel);
	reject(pixel, Gcn::Stage::Vertex);
	auto                  uniform = Gcn::UniformBranchPixelFixture();
	std::vector<uint32_t> normalized {0xbe8003ff, 1};
	normalized.insert(normalized.end(), uniform.begin(), uniform.end());
	ShaderRecompiler::Decoder::Program flow;
	Check(Gcn::Decode(normalized, Gcn::Stage::Pixel, flow, error), error);
	const auto flow_count = flow.instructions.size();
	for (uint32_t branch: {0xbf850001u, 0xbf85ffffu, 0xbf857fffu}) {
		auto bad = normalized;
		bad[4]   = branch;
		Check(!Gcn::Decode(bad, Gcn::Stage::Pixel, flow, error) &&
		          flow.instructions.size() == flow_count,
		      "invalid branch boundary/range accepted or output changed");
	}
	{
		auto bad = normalized;
		bad[5]   = (bad[5] & ~(255u << 17)) | (4u << 17);
		Check(!Gcn::Decode(bad, Gcn::Stage::Pixel, flow, error),
		      "undefined register on one branch path accepted");
	}
	const auto interp_vs = Gcn::InterpolatedVertexFixture(),
	           interp_ps = Gcn::InterpolatedPixelFixture();
	ShaderRecompiler::Decoder::Program interpolation;
	Check(Gcn::Decode(interp_vs, Gcn::Stage::Vertex, interpolation, error, 1), error);
	Check(Gcn::Decode(interp_ps, Gcn::Stage::Pixel, interpolation, error, 1), error);
	const auto reject_interp = [&](std::vector<uint32_t> words, Gcn::Stage stage,
	                               uint32_t parameters) {
		const auto before = interpolation.instructions.size();
		Check(!Gcn::Decode(words, stage, interpolation, error, parameters) && !error.empty() &&
		          interpolation.instructions.size() == before,
		      "invalid interpolation accepted or output changed");
	};
	reject_interp(interp_vs, Gcn::Stage::Vertex, 0);
	reject_interp(interp_vs, Gcn::Stage::Vertex, 2); // Missing PARAM1 on the exit path.
	reject_interp(interp_ps, Gcn::Stage::Pixel, 0);
	reject_interp(interp_ps, Gcn::Stage::Vertex, 1);
	reject_interp(interp_ps, Gcn::Stage::Pixel, 5);
	for (uint32_t word: {interp_ps[0] | (1u << 10), interp_ps[0] | 2u, interp_ps[0] | (2u << 16)}) {
		auto bad = interp_ps;
		bad[0]   = word;
		reject_interp(bad, Gcn::Stage::Pixel, 1);
	}
	{
		auto bad = interp_ps;
		bad.erase(bad.begin());
		reject_interp(bad, Gcn::Stage::Pixel, 1);
		bad = interp_ps;
		bad.insert(bad.begin(), 0xbf820001u);
		reject_interp(bad, Gcn::Stage::Pixel, 1);
		bad = interp_ps;
		bad[1] ^= 1u << 18;
		reject_interp(bad, Gcn::Stage::Pixel, 1);
		bad = interp_vs;
		bad.insert(bad.end() - 1, {0xf800020fu, 0x09080706u});
		reject_interp(bad, Gcn::Stage::Vertex, 1);
	}
	{
		auto bad = interp_vs;
		bad.insert(bad.end() - 5, 0xbf068080u);
		bad.insert(bad.end() - 3, 0xbf850002u);
		reject_interp(bad, Gcn::Stage::Vertex, 1); // A reachable exit skips PARAM0.
	}
	// Four parameters must all be exported, and PS attributes remain bounded by the ABI.
	{
		auto many = interp_vs;
		for (uint32_t attr = 1; attr < 4; ++attr)
			many.insert(many.end() - 1, {0xf800020fu | (attr << 4), 0x09080706u});
		Check(Gcn::Decode(many, Gcn::Stage::Vertex, interpolation, error, 4), error);
		auto ps = interp_ps;
		for (uint32_t i = 0; i < 8; ++i)
			ps[i] |= 3u << 10;
		Check(Gcn::Decode(ps, Gcn::Stage::Pixel, interpolation, error, 4), error);
	}
	const auto                         alu = Gcn::InterpolatedPixelFixture(true);
	ShaderRecompiler::Decoder::Program alu_decoded;
	Check(Gcn::Decode(alu, Gcn::Stage::Pixel, alu_decoded, error, 1), error);
	const auto bad_alu = [&](std::vector<uint32_t> words) {
		Check(!Gcn::Decode(words, Gcn::Stage::Pixel, alu_decoded, error, 1),
		      "invalid VOP3 accepted");
	};
	for (uint32_t bits: {1u << 12, 1u << 16}) {
		auto bad = alu;
		bad[8] |= bits;
		bad_alu(bad);
	}
	for (uint32_t bits: {255u, 104u, 256u + 200}) {
		auto bad = alu;
		bad[9]   = (bad[9] & ~511u) | bits;
		bad_alu(bad);
	}
	{
		auto bad = alu;
		bad.resize(9);
		bad_alu(bad);
	}
	{
		auto bad = alu;
		bad[9]   = 0 | (1u << 9) | (242u << 18);
		bad_alu(bad);
	}
	std::puts("PS4_GCN bounded decoding, stage, undefined input and transaction checks PASS");
	spvtools::SpirvTools validator(SPV_ENV_VULKAN_1_1);
	validator.SetMessageConsumer(
	    [](spv_message_level_t, const char*, const spv_position_t& position, const char* message) {
		    std::fprintf(stderr, "SPIR-V %zu: %s\n", position.index, message);
	    });
	{
		auto many = interp_vs;
		for (uint32_t attr = 1; attr < 4; ++attr)
			many.insert(many.end() - 1, {0xf800020fu | (attr << 4), 0x09080706u});
		auto ps = interp_ps;
		for (uint32_t i = 0; i < 8; ++i)
			ps[i] |= 3u << 10;
		std::vector<uint32_t> words;
		Check(Gcn::Compile(many, Gcn::Stage::Vertex, words, error, {}, 4), error);
		Check(validator.Validate(words), "four-parameter VS SPIR-V");
		Check(Gcn::Compile(ps, Gcn::Stage::Pixel, words, error, {}, 4), error);
		Check(validator.Validate(words), "sparse PARAM3 PS SPIR-V");
		std::puts("PS4_GCN four PARAM exports and sparse PARAM3 input SPIR-V PASS");
	}
	for (uint32_t variant = 0; variant < 12; ++variant) {
		const bool vs =
		    variant == 0 || variant == 2 || variant == 6 || variant == 8 || variant == 10;
		auto code = variant == 6 || variant == 8 || variant == 10
		                ? Gcn::InterpolatedVertexFixture(variant != 6)
		            : variant == 7 || variant == 9 || variant == 11
		                ? Gcn::InterpolatedPixelFixture(variant == 9)
		            : vs ? Gcn::TriangleVertexFixture(variant == 0)
		            : variant >= 4
		                ? Gcn::UniformBranchPixelFixture()
		                : Gcn::SolidPixelFixture(0.25f, 0.5f, 0.75f, variant == 3 ? 0.5f : 1.f);
		if (variant == 10)
			for (uint32_t attr = 1; attr < 4; ++attr)
				code.insert(code.end() - 1,
				            {0xf800020fu | (attr << 4), attr == 3 ? 0x09060708u : 0x09080706u});
		if (variant == 11)
			for (uint32_t i = 0; i < 8; ++i)
				code[i] |= 3u << 10;
		const char*                   name = variant == 0    ? "gcn.vert"
		                                     : variant == 1  ? "gcn.frag"
		                                     : variant == 2  ? "gcn-small.vert"
		                                     : variant == 3  ? "gcn-alpha.frag"
		                                     : variant == 4  ? "gcn-uniform0.frag"
		                                     : variant == 5  ? "gcn-uniform1.frag"
		                                     : variant == 6  ? "gcn-interp.vert"
		                                     : variant == 7  ? "gcn-interp.frag"
		                                     : variant == 8  ? "gcn-perspective.vert"
		                                     : variant == 9  ? "gcn-alu.frag"
		                                     : variant == 10 ? "gcn-many.vert"
		                                                     : "gcn-param3.frag";
		std::vector<uint32_t>         words;
		const std::array<uint32_t, 1> users {variant >= 4 ? variant - 4 : 0};
		Check(Gcn::Compile(code, vs ? Gcn::Stage::Vertex : Gcn::Stage::Pixel, words, error,
		                   variant >= 4 && variant < 6 ? std::span<const uint32_t>(users)
		                                               : std::span<const uint32_t> {},
		                   variant >= 10  ? 4
		                   : variant >= 6 ? 1
		                                  : 0),
		      error);
		Check(validator.Validate(words), "SPIR-V validation");
		std::ofstream source(folder / (std::string(name) + ".gcn"), std::ios::binary);
		source.write(reinterpret_cast<const char*>(code.data()), std::streamsize(code.size() * 4));
		Check(bool(source), "write GCN fixture");
		std::ofstream file(folder / (std::string(name) + ".spv"), std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), std::streamsize(words.size() * 4));
		Check(bool(file), "write SPIR-V");
		std::printf("PS4_GCN %s GCN -> shared IR -> SPIR-V words=%zu validator=PASS\n",
		            vs ? "VS" : "PS", words.size());
	}
	{
		const uint32_t        descriptor[] = {0x200000, 0, 64, 0xfac};
		std::vector<uint32_t> words;
		Gcn::BufferDescriptor resource;
		Check(Gcn::Compile(Gcn::BufferPixelFixture(4), Gcn::Stage::Pixel, words, error, descriptor,
		                   0, &resource),
		      error);
		Check(resource.address == 0x200000 && resource.size == 64, "descriptor decode");
		Check(validator.Validate(words), "buffer-load SPIR-V validation");
		std::ofstream file(folder / "gcn-buffer.frag.spv", std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(file), "write buffer SPIR-V");
		const auto    code = Gcn::BufferPixelFixture(4);
		std::ofstream source(folder / "gcn-buffer.frag.gcn", std::ios::binary);
		source.write(reinterpret_cast<const char*>(code.data()), code.size() * 4);
		Check(bool(source), "write SMRD GCN fixture");
		std::puts("PS4_GCN s_buffer_load_dwordx4 -> bound GPU buffer SPIR-V PASS");
	}
	{
		const std::array<uint32_t, 4> descriptor {0x200000, 0, 64, 0xfac};
		Gcn::BufferDescriptor         resource {123, 456};
		for (const auto& bad: std::vector<std::array<uint32_t, 4>> {{0, 0, 64, 0xfac},
		                                                            {0x200004, 0, 64, 0xfac},
		                                                            {0x200000, 0x10000, 64, 0xfac},
		                                                            {0x200000, 0, 0, 0xfac},
		                                                            {0x200000, 0, 65540, 0xfac},
		                                                            {0x200000, 0, 18, 0xfac},
		                                                            {0x200000, 0, 64, 0xfad},
		                                                            {0xfffffff0, 255, 64, 0xfac}}) {
			Check(!Gcn::DecodeBufferDescriptor(bad, resource, error) && resource.address == 123 &&
			          resource.size == 456,
			      "bad descriptor accepted/changed output");
		}
		std::vector<uint32_t> words {123};
		const auto reject_buffer = [&](std::vector<uint32_t> code, std::span<const uint32_t> data) {
			Check(!Gcn::Compile(code, Gcn::Stage::Pixel, words, error, data, 0, &resource) &&
			          words == std::vector<uint32_t> {123},
			      "invalid SMRD accepted/changed output");
		};
		reject_buffer(Gcn::BufferPixelFixture(16), descriptor);
		reject_buffer(Gcn::BufferPixelFixture(), std::span(descriptor).first(3));
		for (uint32_t mask: {0x100u, 0x200u, 1u << 22}) {
			auto code = Gcn::BufferPixelFixture();
			code[0] ^= mask;
			reject_buffer(code, descriptor);
		}
		{
			auto code = Gcn::BufferPixelFixture();
			code[1]   = 0xbf8cffff;
			reject_buffer(code, descriptor);
		}
		{
			auto code = Gcn::BufferPixelFixture();
			code.insert(code.begin(), {0xbe8003ff, 0});
			reject_buffer(code, descriptor);
		}
		std::puts("PS4_GCN descriptor bounds/modes/transaction and SMRD rejects PASS");
	}
	{
		const uint32_t        map[] = {3, 1, 2, 0};
		std::vector<uint32_t> words;
		Check(Gcn::Compile(Gcn::InterpolatedPixelFixture(), Gcn::Stage::Pixel, words, error, {}, 4,
		                   nullptr, map),
		      error);
		Check(validator.Validate(words), "remapped interpolation SPIR-V");
		std::ofstream file(folder / "gcn-remap.frag.spv", std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(Gcn::Compile(Gcn::InterpolatedPixelFixture(), Gcn::Stage::Pixel, words, error, {}, 1,
		                   nullptr, {}, true),
		      error);
		Check(validator.Validate(words), "linear central interpolation SPIR-V");
		std::ofstream linear_file(folder / "gcn-linear.frag.spv", std::ios::binary);
		linear_file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		for (const auto& bad:
		     std::vector<std::vector<uint32_t>> {{0}, {4, 1, 2, 0}, {1024, 1, 2, 0}}) {
			words = {123};
			Check(!Gcn::Compile(Gcn::InterpolatedPixelFixture(), Gcn::Stage::Pixel, words, error,
			                    {}, 4, nullptr, bad) &&
			          words == std::vector<uint32_t> {123},
			      "invalid map modified output");
		}
	}
	{
		// Independent chains exercise signed conversion, rounding, reciprocal and sqrt.
		const auto v1 = [](uint32_t op, uint32_t dst, uint32_t src) {
			return 0x7e000000u | (dst << 17) | (op << 9) | src;
		};
		const auto add  = [](uint32_t dst) { return (3u << 25) | (dst << 17) | (dst << 9) | 255u; };
		const auto bits = [](float x) { return std::bit_cast<uint32_t>(x); };
		std::vector<uint32_t> code = {0xc2820100u,    0xbf8c007fu,
		                              v1(5, 0, 4),    add(0),
		                              bits(3.25f),    v1(51, 0, 256),
		                              v1(33, 1, 5),   add(1),
		                              bits(1.25f),    v1(42, 1, 257),
		                              add(1),         bits(-3.5f),
		                              v1(34, 2, 6),   (8u << 25) | (2u << 17) | (2u << 9) | 240u,
		                              add(2),         bits(1.75f),
		                              v1(36, 2, 258), (8u << 25) | (2u << 17) | (2u << 9) | 240u,
		                              add(2),         bits(-.75f),
		                              v1(32, 2, 258), v1(1, 3, 242),
		                              0xf800180f,     0x03020100,
		                              0xbf810000};
		std::vector<uint32_t> words;
		const uint32_t        descriptor[] = {0x200000, 0, 64, 0xfac};
		Gcn::BufferDescriptor resource;
		Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, descriptor, 0, &resource), error);
		Check(validator.Validate(words), "VOP1 ALU SPIR-V");
		std::ofstream file(folder / "gcn-unary.frag.spv", std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		std::ofstream source(folder / "gcn-unary.frag.gcn", std::ios::binary);
		source.write(reinterpret_cast<const char*>(code.data()), code.size() * 4);
		std::puts("PS4_GCN remapped interpolation and seven VOP1 operations SPIR-V PASS");
	}
	{
		const uint32_t ps_users[] = {0x200000, 0, 16, 0xfac, 0x210000, 16u << 16, 4, 0xfac};
		const uint32_t vs_users[] = {0x200000, 16u << 16, 4, 0xfac, 0x210000, 16u << 16, 4, 0xfac};
		for (uint32_t variant = 0; variant < 3; ++variant) {
			const bool vertex = variant == 2;
			const auto code =
			    vertex ? Gcn::FetchVertexFixture() : Gcn::IndexedBufferPixelFixture(variant == 1);
			std::vector<uint32_t>              words;
			std::vector<Gcn::BufferDescriptor> resources;
			Check(Gcn::Compile(code, vertex ? Gcn::Stage::Vertex : Gcn::Stage::Pixel, words, error,
			                   vertex ? std::span(vs_users) : std::span(ps_users), vertex ? 1 : 0,
			                   nullptr, {}, false, &resources),
			      error);
			Check(resources.size() == 2 && resources[0].address == 0x200000 &&
			          resources[1].address == 0x210000 && resources[1].size == 64 &&
			          resources[1].stride == 16,
			      "MUBUF resource order/size/stride");
			Check(validator.Validate(words), "indexed MUBUF SPIR-V");
			const std::string name = vertex    ? "gcn-fetch.vert"
			                         : variant ? "gcn-indexed-dword.frag"
			                                   : "gcn-indexed.frag";
			std::ofstream     file(folder / (name + ".spv"), std::ios::binary);
			file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
			std::ofstream source(folder / (name + ".gcn"), std::ios::binary);
			source.write(reinterpret_cast<const char*>(code.data()), code.size() * 4);
		}
		const auto reject_indexed = [&](std::vector<uint32_t> code, std::vector<uint32_t> users) {
			std::vector<uint32_t>              words {123};
			std::vector<Gcn::BufferDescriptor> resources {{123, 456}};
			Check(!Gcn::Compile(code, Gcn::Stage::Pixel, words, error, users, 0, nullptr, {}, false,
			                    &resources) &&
			          words == std::vector<uint32_t> {123} && resources.size() == 1 &&
			          resources[0].address == 123,
			      "invalid MUBUF changed outputs");
		};
		for (uint32_t mask: {1u, 1u << 12, 1u << 14, 1u << 16, 1u << 17, 1u << 25}) {
			auto code = Gcn::IndexedBufferPixelFixture();
			code[3] ^= mask;
			reject_indexed(code, {std::begin(ps_users), std::end(ps_users)});
		}
		for (uint32_t mask: {1u << 21, 1u << 22, 1u << 23, 1u << 24, 2u << 16}) {
			auto code = Gcn::IndexedBufferPixelFixture();
			code[4] ^= mask;
			reject_indexed(code, {std::begin(ps_users), std::end(ps_users)});
		}
		for (const auto& change: std::vector<std::pair<uint32_t, uint32_t>> {{5, 0},
		                                                                     {5, 3u << 16},
		                                                                     {5, 4u << 16},
		                                                                     {5, 1u << 31},
		                                                                     {6, 0},
		                                                                     {6, 5000},
		                                                                     {7, 0xfad}}) {
			std::vector<uint32_t> users(std::begin(ps_users), std::end(ps_users));
			users[change.first] = change.second;
			reject_indexed(Gcn::IndexedBufferPixelFixture(), users);
		}
		for (uint32_t operand: {0x8001fd00u, 0x800101ffu}) {
			auto code = Gcn::IndexedBufferPixelFixture();
			code[4]   = operand;
			reject_indexed(code, {std::begin(ps_users), std::end(ps_users)});
		}
		{
			auto code = Gcn::IndexedBufferPixelFixture();
			code.insert(code.begin(), {0xbe8703ffu, 0xfac});
			reject_indexed(code, {std::begin(ps_users), std::end(ps_users)});
		}
		std::puts("PS4_GCN indexed MUBUF DWORD/X4, two buffers, vertex fetch and negative "
		          "transaction PASS");
	}
	{
		for (bool vertex: {false, true}) {
			std::vector<uint32_t> users = {0x200000,
			                               vertex ? 16u << 16 : 0u,
			                               vertex ? 4u : 16u,
			                               0xfac,
			                               0x210000,
			                               16u << 16,
			                               4,
			                               0xfac,
			                               0x2200,
			                               10u << 20,
			                               7u | (3u << 14),
			                               0x90800facu,
			                               63u << 13,
			                               0,
			                               0,
			                               0};
			const auto code = vertex ? Gcn::TextureVertexFixture() : Gcn::TexturePixelFixture();
			std::vector<uint32_t>              words;
			std::vector<Gcn::BufferDescriptor> buffers;
			Gcn::ImageDescriptor               image;
			Check(Gcn::Compile(code, vertex ? Gcn::Stage::Vertex : Gcn::Stage::Pixel, words, error,
			                   users, 1, nullptr, {}, false, &buffers, &image),
			      error);
			Check(buffers.size() == 2 && image.address == 0x220000 && image.width == 8 &&
			          image.height == 4 && image.pitch == 256 && image.size == 1024,
			      "image descriptor contract");
			Check(validator.Validate(words), "IMAGE_LOAD SPIR-V");
			const auto reject_image = [&](std::vector<uint32_t> bad_code,
			                              std::vector<uint32_t> bad_users) {
				std::vector<uint32_t>              out {123};
				std::vector<Gcn::BufferDescriptor> kept {{123, 456}};
				Gcn::ImageDescriptor               kept_image {123, 1, 2, 3, 4};
				Check(!Gcn::Compile(bad_code, vertex ? Gcn::Stage::Vertex : Gcn::Stage::Pixel, out,
				                    error, bad_users, 1, nullptr, {}, false, &kept, &kept_image) &&
				          out == std::vector<uint32_t> {123} && kept.size() == 1 &&
				          kept[0].address == 123 && kept_image.address == 123,
				      "invalid IMAGE_LOAD changed outputs");
			};
			for (const auto& field: std::vector<std::pair<uint32_t, uint32_t>> {{8, 0},
			                                                                    {8, 0xffffffff},
			                                                                    {9, 11u << 20},
			                                                                    {10, 256},
			                                                                    {10, 256u << 14},
			                                                                    {11, 0x90000fac},
			                                                                    {12, 7u << 13},
			                                                                    {13, 1},
			                                                                    {14, 1},
			                                                                    {15, 1}}) {
				auto bad_users         = users;
				bad_users[field.first] = field.second;
				reject_image(code, bad_users);
			}
			for (uint32_t mask: {1u << 8, 1u << 12, 1u << 16, 1u << 18}) {
				auto bad = code;
				auto pos = std::find(bad.begin(), bad.end(), 0xf0001f00u);
				*pos ^= mask;
				reject_image(bad, users);
			}
			for (uint32_t operand: {0x0002fe0au, 0x000206ffu, 0x0003060au}) {
				auto bad   = code;
				auto pos   = std::find(bad.begin(), bad.end(), 0xf0001f00u);
				*(pos + 1) = operand;
				reject_image(bad, users);
			}
			{
				auto bad = code;
				bad.insert(bad.begin(), {0xbe8f03ffu, 0});
				reject_image(bad, users);
			}
			const std::string name = vertex ? "gcn-texture.vert" : "gcn-texture.frag";
			std::ofstream     file(folder / (name + ".spv"), std::ios::binary);
			file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
			std::ofstream source(folder / (name + ".gcn"), std::ios::binary);
			source.write(reinterpret_cast<const char*>(code.data()), code.size() * 4);
		}
		std::puts("PS4_GCN IMAGE_LOAD + two buffers in VS/PS SPIR-V PASS");
	}
	// RDNA2's pre-existing vertex ABI continues to be selected by default.
	Check(!ShaderRecompiler::CompileOptions {}.gcn_vertex_inputs, "PS5 ABI default changed");
	subsystems.Destroy();
	return 0;
}
