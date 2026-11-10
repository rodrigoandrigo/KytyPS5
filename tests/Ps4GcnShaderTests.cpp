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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <source_location>

using namespace Libs::Graphics;
namespace Gcn = ShaderRecompiler::Gcn;
static void Check(bool condition, const std::string& message,
                  const std::source_location where = std::source_location::current()) {
	if (!condition) {
		std::fprintf(stderr, "PS4_GCN FAIL line=%u: %s\n", where.line(), message.c_str());
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
	for (uint32_t branch: {0xbf850001u, 0xbf858000u, 0xbf857fffu}) {
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
		auto code = Gcn::BufferPixelFixture(4);
		code[0] = 0xc2820008u; // S_BUFFER_LOAD_DWORDX4 s[4:7], s[0:3], s8
		code.insert(code.begin(), {0xc2040100u, 0xbf8c007fu}); // Runtime byte offset from buffer DWORD zero.
		Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, descriptor,
		                   0, &resource),
		      error);
		Check(resource.address == 0x200000 && resource.size == 64, "descriptor decode");
		Check(validator.Validate(words), "buffer-load SPIR-V validation");
		std::ofstream file(folder / "gcn-buffer.frag.spv", std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(file), "write buffer SPIR-V");
		std::ofstream source(folder / "gcn-buffer.frag.gcn", std::ios::binary);
		source.write(reinterpret_cast<const char*>(code.data()), code.size() * 4);
		Check(bool(source), "write SMRD GCN fixture");
		std::puts("PS4_GCN s_buffer_load_dwordx4 -> bound GPU buffer SPIR-V PASS");
	}
	{
		const uint32_t pointer[] {0x200004, 0};
		auto code = Gcn::BufferPixelFixture(4);
		code[0] &= ~(8u << 22);
		code.insert(code.begin(), 0xbf820000u);
		std::vector<uint32_t> words;
		Gcn::BufferDescriptor resource;
		Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, pointer, 0, &resource), error);
		Check(resource.address == 0x200014 && resource.size == 16, "body S_LOAD exact range");
		Check(validator.Validate(words), "body S_LOAD SPIR-V");
		std::ofstream file(folder / "gcn-raw-load.frag.spv", std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(file), "write body S_LOAD SPIR-V");
		code.insert(code.begin(), 0xbe800380u); // Mutable base must not use a stale snapshot.
		Check(!Gcn::Compile(code, Gcn::Stage::Pixel, words, error, pointer, 0, &resource),
		      "body S_LOAD accepted a mutable base");
	}
	{
		const uint32_t users[] {0x200004, 0, 0};
		auto code = Gcn::BufferPixelFixture(4);
		code[0] = 0xc0820008u; // Load RGBA using the byte offset in s8.
		code.insert(code.begin(), {0xc0040002u, 0xbf8c007fu}); // Load s8 using s2.
		std::vector<uint32_t> words;
		Gcn::BufferDescriptor resource;
		const auto memory = [&](uint64_t address, std::span<uint8_t> out) {
			return address == users[0] && out.size() <= 32;
		};
		Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, users, 0, &resource,
		                   {}, false, nullptr, nullptr, memory), error);
		Check(resource.address == users[0] && resource.size == 32, "SGPR S_LOAD range");
		Check(validator.Validate(words), "SGPR S_LOAD SPIR-V");
		std::ofstream file(folder / "gcn-raw-sgpr.frag.spv", std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(file), "write SGPR S_LOAD SPIR-V");
		Check(!Gcn::Compile(code, Gcn::Stage::Pixel, words, error, users, 0, &resource),
		      "SGPR S_LOAD accepted without a readable range");
	}
	{
		const uint32_t users[] {0x200000, 0, 0};
		const uint32_t code[] {0xc0040002u, 0xbf8c007fu, 0xc0880008u, 0xbf8c007fu,
		    0x7e000280u, 0xe0382000u, 0x80040100u, 0xbf8c0f70u,
		    0xf800180fu, 0x04030201u, 0xbf810000u};
		const uint32_t table[] {16, 0, 0, 0,
		    0x210000, 16u << 16, 1, 0xfac, 0x220000, 16u << 16, 1, 0xfac,
		    0x230000, 16u << 16, 1, 0xfac, 0x240000, 16u << 16, 1, 0xfac};
		const auto memory = [&](uint64_t address, std::span<uint8_t> out) {
			if (address != users[0] || out.size() > sizeof(table)) return false;
			std::memcpy(out.data(), table, out.size());
			return true;
		};
		std::vector<uint32_t> words;
		std::vector<Gcn::BufferDescriptor> resources;
		Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, users, 0, nullptr,
		                   {}, false, &resources, nullptr, memory), error);
		Check(resources.size() == 5 && resources[0].address == users[0] &&
		          resources[1].address == table[4] && resources[2].address == table[8] &&
		          resources[3].address == table[12] && resources[4].address == table[16],
		      "dynamic MUBUF candidate ordering");
		Check(validator.Validate(words), "dynamic MUBUF SPIR-V");
		std::ofstream file(folder / "gcn-mubuf-table.frag.spv", std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(file), "write dynamic MUBUF shader");
		std::vector<uint32_t> joined {code[0], code[1], 0xbe8903a0u, 0xbf069008u,
		    0xbf840002u, code[2], 0xbf820001u, 0xc0880009u};
		joined.insert(joined.end(), std::begin(code) + 3, std::end(code));
		Check(Gcn::Compile(joined, Gcn::Stage::Pixel, words, error, users, 0, nullptr,
		                   {}, false, &resources, nullptr, memory), error);
		Check(resources.size() == 5 && validator.Validate(words), "MUBUF descriptor CFG join");
		std::ofstream join_file(folder / "gcn-mubuf-join.frag.spv", std::ios::binary);
		join_file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(join_file), "write MUBUF descriptor CFG join shader");
		joined.insert(joined.begin() + 8, 0xbe900380u);
		Check(!Gcn::Compile(joined, Gcn::Stage::Pixel, words, error, users, 0, nullptr,
		                    {}, false, &resources, nullptr, memory) &&
		          error.find("full S_LOAD") != std::string::npos,
		      "MUBUF descriptor join accepted partial overwrite");
		// Accumulate red from both iterations in v5 and export the final GBA.
		const uint32_t loop[] {code[0], code[1], code[2], code[3], code[4], 0x7e0a0280u,
		    code[5], code[6], code[7], 0x060a0b01u, 0xbf06a008u, 0xbf850004u,
		    0xbe8803a0u, code[2], code[3], 0xbf82fff6u, code[8], 0x04030205u, code[10]};
		Check(Gcn::Compile(loop, Gcn::Stage::Pixel, words, error, users, 0, nullptr,
		                   {}, false, &resources, nullptr, memory), error);
		Check(resources.size() == 5 && validator.Validate(words), "MUBUF descriptor loop backedge");
		std::ofstream loop_file(folder / "gcn-mubuf-loop.frag.spv", std::ios::binary);
		loop_file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(loop_file), "write MUBUF descriptor loop shader");
	}
	{
		const uint32_t users[] {0x200000, 0, 0, 0, 0x200000, 4u << 16, 12, 0xfac};
		const uint32_t code[] {0x7e000280u, 0x7e0a02a0u, 0xe0702000u, 0x80010500u,
		    0xbf8c0f70u, 0xc0040002u, 0xbf8c007fu, 0xc0880008u, 0xbf8c007fu,
		    0xe0382000u, 0x80040100u, 0xbf8c0f70u, 0xf800180fu, 0x04030201u, 0xbf810000u};
		const uint32_t table[] {16, 0, 0, 0,
		    0x210000, 16u << 16, 1, 0xfac, 0x220000, 16u << 16, 1, 0xfac};
		const auto memory = [&](uint64_t address, std::span<uint8_t> out) {
			if (address != users[0] || out.size() > sizeof(table)) return false;
			std::memcpy(out.data(), table, out.size());
			return true;
		};
		std::vector<uint32_t> words;
		std::vector<Gcn::BufferDescriptor> buffers;
		std::vector<Gcn::ImageDescriptor> images;
		uint32_t writable = 0;
		Check(Gcn::CompileImages(code, Gcn::Stage::Pixel, words, error, users, 0, {},
		                         false, buffers, images, memory, nullptr, nullptr, nullptr, &writable), error);
		Check(buffers.size() == 3 && buffers[0].address == users[0] && writable == 1 &&
		          validator.Validate(words), "writable SRT shares the store and SMRD binding");
		std::ofstream file(folder / "gcn-srt-store.frag.spv", std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(file), "write GCN SRT store shader");
	}
	for (bool vertex : {false, true}) {
		const uint32_t users[] {0x200000, 0};
		std::array<uint32_t, 32> table {};
		std::vector<uint32_t> prefix {0xc1040100u, 0xc10c0110u, 0xbf8c007fu};
		for (uint32_t i = 0; i < 8; ++i) {
			table[4 * i] = 0x210000 + i * 0x1000;
			table[4 * i + 2] = 16;
			table[4 * i + 3] = 0xfac;
			prefix.push_back(0xc2000100u | ((40u + i) << 15) | ((4u + i * 2) << 9));
		}
		prefix.push_back(0xbf8c007fu);
		auto code = vertex ? Gcn::TriangleVertexFixture(false) : std::vector<uint32_t> {
		    0x7e0202f0u, 0x7e0402ffu, std::bit_cast<uint32_t>(.75f), 0x7e0602f2u,
		    0xf800180fu, 0x03020100u, 0xbf810000u};
		const uint32_t dst = vertex ? 5 : 0;
		std::vector<uint32_t> sum {0x7e000200u | (dst << 17) | 40u};
		for (uint32_t i = 1; i < 8; ++i)
			sum.push_back(0x06000000u | (dst << 17) | (dst << 9) | (40u + i));
		code.insert(code.end() - 3, sum.begin(), sum.end());
		code.insert(code.begin(), prefix.begin(), prefix.end());
		const auto memory = [&](uint64_t address, std::span<uint8_t> out) {
			if (address < users[0] || address - users[0] > sizeof(table) ||
			    out.size() > sizeof(table) - (address - users[0])) return false;
			std::memcpy(out.data(), reinterpret_cast<const uint8_t*>(table.data()) + (address - users[0]), out.size());
			return true;
		};
		std::vector<uint32_t> words;
		std::vector<Gcn::BufferDescriptor> buffers;
		std::vector<Gcn::ImageDescriptor> images;
		Check(Gcn::CompileImages(code, vertex ? Gcn::Stage::Vertex : Gcn::Stage::Pixel,
		                         words, error, users, 0, {}, false, buffers, images, memory), error);
		Check(buffers.size() == 8 && buffers.back().address == table[28] &&
		          validator.Validate(words), "eight buffers per GCN stage");
		std::ofstream file(folder / (vertex ? "gcn-eight.vert.spv" : "gcn-eight.frag.spv"), std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(file), "write eight-buffer shader");
	}
	for (bool sampled : {false, true}) {
		const uint32_t users[] {0x200000, 0, 0, 0, 0x92, 0, 0, 0};
		const uint32_t code[] {0xc0040002u, 0xbf8c007fu, 0xc0c80008u, 0xbf8c007fu,
		    0x7e140280u, 0x7e160280u, 0x7e180280u,
		    sampled ? 0xf0900f00u : 0xf0001f00u, sampled ? 0x0024040au : 0x0004040au,
		    0xbf8c0f70u, 0xf800180fu, 0x07060504u, 0xbf810000u};
		const uint32_t table[] {16, 0, 0, 0,
		    0x2100, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0,
		    0x2200, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0,
		    0x2300, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0,
		    0x2400, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0};
		const auto memory = [&](uint64_t address, std::span<uint8_t> out) {
			if (address != users[0] || out.size() > sizeof(table)) return false;
			std::memcpy(out.data(), table, out.size());
			return true;
		};
		std::vector<uint32_t> words;
		std::vector<Gcn::BufferDescriptor> buffers;
		std::vector<Gcn::ImageDescriptor> images;
		Check(Gcn::CompileImages(code, Gcn::Stage::Pixel, words, error, users, 0, {},
		                         false, buffers, images, memory), error);
		Check(buffers.size() == 1 && images.size() == 4 &&
		          images[0].address == 0x210000 && images[1].address == 0x220000 &&
		          images[2].address == 0x230000 && images[3].address == 0x240000 &&
		          validator.Validate(words), "dynamic MIMG candidate order and SPIR-V");
		std::ofstream file(folder / (sampled ? "gcn-image-srt-sample.frag.spv" : "gcn-image-srt-load.frag.spv"),
		                   std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(file), "write dynamic MIMG shader");
	}
	for (bool vertex : {false, true}) {
		const uint32_t users[] {
		    0x2200, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0,
		    0x2300, 10u << 20, 1, 0x90800facu, 63u << 13, 0, 0, 0};
		auto code = vertex ? Gcn::TriangleVertexFixture(false) : std::vector<uint32_t> {
		    0xf800180fu, 0x07061704u, 0xbf810000u}; // R, B and A from first, G from second.
		code.insert(code.end() - 3, {0x7e140280u, 0x7e160280u, 0xf0001f00u,
		    vertex ? 0x00000c0au : 0x0000040au,
		    vertex ? 0x7e140280u : 0x7e140281u, 0xf0001f00u, 0x0002160au, 0xbf8c0f70u});
		if (vertex)
			code.insert(code.end() - 3, {0x7e0a030cu, 0x060a0b16u});
		std::vector<uint32_t> words;
		std::vector<Gcn::BufferDescriptor> buffers;
		std::vector<Gcn::ImageDescriptor> images;
		Check(Gcn::CompileImages(code, vertex ? Gcn::Stage::Vertex : Gcn::Stage::Pixel,
		                         words, error, users, 0, {}, false, buffers, images), error);
		Check(images.size() == 2 && images[0].width == 1 && images[1].width == 2 &&
		          validator.Validate(words), "two images per GCN stage");
		std::ofstream file(folder / (vertex ? "gcn-two-images.vert.spv" : "gcn-two-images.frag.spv"), std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(file), "write two-image shader");
	}
	for (bool vertex : {false, true}) {
		const uint32_t users[] {
		    0x200000, 0, 16, 0xfac, 0x210000, 0, 16, 0xfac, 0x220000, 0, 16, 0xfac};
		auto code = vertex ? Gcn::TriangleVertexFixture(false) : Gcn::SolidPixelFixture();
		if (vertex) {
			code.insert(code.end() - 3, {0x7e0a0210u, 0x060a0a11u, 0x060a0a12u});
		} else {
			code = {0x7e000210u, 0x7e020211u, 0x7e040212u, 0x7e0602f2u,
			        0xf800180fu, 0x03020100u, 0xbf810000u};
		}
		code.insert(code.begin(), {0xc2080100u, 0xc2088500u, 0xc2090900u, 0xbf8c007fu});
		std::vector<uint32_t> words;
		std::vector<Gcn::BufferDescriptor> resources;
		Check(Gcn::Compile(code, vertex ? Gcn::Stage::Vertex : Gcn::Stage::Pixel, words,
		                   error, users, 0, nullptr, {}, false, &resources), error);
		Check(resources.size() == 3 && resources[0].address == users[0] &&
		          resources[1].address == users[4] && resources[2].address == users[8],
		      "three GCN buffers in descriptor order");
		Check(validator.Validate(words), "three GCN buffers SPIR-V");
		std::ofstream file(folder / (vertex ? "gcn-three.vert.spv" : "gcn-three.frag.spv"), std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(file), "write three-buffer shader");
	}
	for (uint32_t width : {1u, 2u}) {
		const uint32_t pointer[] {0x200004, 0};
		auto code = Gcn::BufferPixelFixture(4);
		code[0] = 0xc0020104u | ((width - 1) << 22);
		code[3] = width == 1 ? 0x7e0202f0u : code[3]; // .5 when only red comes from memory.
		code[4] = 0x7e0402f0u; // Blue = .5.
		code[5] = 0x7e0602f2u; // Alpha = 1.
		code.insert(code.begin(), 0xbf820000u);
		std::vector<uint32_t> words;
		Gcn::BufferDescriptor resource;
		Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, pointer, 0, &resource), error);
		Check(resource.address == 0x200014 && resource.size == width * 4, "short S_LOAD exact range");
		Check(validator.Validate(words), "short S_LOAD SPIR-V");
		std::ofstream file(folder / ("gcn-raw-short" + std::to_string(width) + ".frag.spv"), std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(file), "write short S_LOAD SPIR-V");
	}
	{
		const uint32_t flat[] {0x400};
		std::vector<uint32_t> words;
		Check(Gcn::Compile(Gcn::FlatPixelFixture(), Gcn::Stage::Pixel, words, error, {}, 1,
		                   nullptr, flat), error);
		Check(validator.Validate(words), "flat PARAM MOV P0 SPIR-V validation");
		std::ofstream file(folder / "gcn-flat.frag.spv", std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(file), "write flat module");
		for (uint32_t selector = 0; selector < 4; ++selector) {
			const uint32_t mapping[] {0x20u | (selector << 8)};
			Check(Gcn::Compile(Gcn::InterpolatedPixelFixture(), Gcn::Stage::Pixel, words, error,
			                   {}, 1, nullptr, mapping), error);
			Check(validator.Validate(words), "default PARAM SPIR-V validation");
			std::ofstream out(folder / ("gcn-default" + std::to_string(selector) + ".frag.spv"), std::ios::binary);
			out.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
			Check(bool(out), "write default PARAM module");
		}
		words = {123};
		Check(!Gcn::Compile(Gcn::FlatPixelFixture(), Gcn::Stage::Pixel, words, error, {}, 1) &&
		          words == std::vector<uint32_t> {123}, "MOV P0 accepted without flat control");
		Check(!Gcn::Compile(Gcn::InterpolatedPixelFixture(), Gcn::Stage::Pixel, words, error,
		                    {}, 1, nullptr, flat), "smooth P1/P2 accepted for flat input");
	}
	{
		const std::array<uint32_t, 8> table {0x200000, 16u << 16, 4, 0xfac,
		                                      0x210000, 16u << 16, 4, 0xfac};
		const auto fetch = Gcn::ExternalFetchFunctionFixture();
		const std::array<uint32_t, 6> users {0, 0, 0x300000, 0, 0x310000, 0};
		const auto read = [&](uint64_t address, std::span<uint8_t> out) {
			const auto copy = [&](uint64_t base, const auto& values) {
				const size_t bytes = values.size() * 4;
				if (address < base || address - base > bytes || out.size() > bytes - size_t(address - base)) return false;
				std::memcpy(out.data(), reinterpret_cast<const uint8_t*>(values.data()) + address - base, out.size());
				return true;
			};
			return copy(0x300000, table) || copy(0x310000, fetch);
		};
		std::vector<uint32_t> words;
		std::vector<Gcn::BufferDescriptor> buffers;
		std::vector<Gcn::ScalarRead> reads;
		uint32_t scalars = 0;
		Check(Gcn::Compile(Gcn::ExternalFetchVertexFixture(), Gcn::Stage::Vertex, words, error,
		                   users, 1, nullptr, {}, false, &buffers, nullptr, read, &reads,
		                   nullptr, &scalars), error);
		Check(buffers.size() == 2 && buffers[0].address == 0x200000 && buffers[1].address == 0x210000 &&
		          reads.size() == 2 && reads[0].address == 0x310000 && reads[0].bytes == fetch.size() * 4 &&
		          reads[1].address == 0x300000 && reads[1].bytes == 32 && scalars == 16,
		      "external fetch snapshot/resource/link allocation");
		Check(validator.Validate(words), "external typed fetch SPIR-V validation");
		std::ofstream file(folder / "gcn-external-fetch.vert.spv", std::ios::binary);
		file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
		Check(bool(file), "write external fetch module");
		for (bool pixel: {false, true}) {
			std::vector<uint32_t> kept {123};
			Check(!Gcn::Compile(Gcn::ExternalFetchVertexFixture(), pixel ? Gcn::Stage::Pixel : Gcn::Stage::Vertex,
			                    kept, error, users, 1, nullptr, {}, false, &buffers) &&
			          kept == std::vector<uint32_t> {123}, "fetch accepted without valid memory/stage");
		}
	}
	for (bool typed: {false, true})
		for (uint32_t count = 1; count <= 4; ++count) {
			const uint32_t formats[] {4, 11, 13, 14}, raw_ops[] {12, 13, 15, 14};
			const uint32_t load = typed ? 0xe8002000u | ((count - 1) << 16) | (formats[count - 1] << 19) | (7u << 23)
			                            : 0xe0002000u | (raw_ops[count - 1] << 18);
			for (auto stage: {Gcn::Stage::Vertex, Gcn::Stage::Pixel}) {
				auto code = stage == Gcn::Stage::Vertex ? Gcn::TriangleVertexFixture() : Gcn::SolidPixelFixture();
				// Initialize index in v10; export all loaded lanes by folding them
				// into position X (VS) or red (PS), retaining the other channels.
				const uint32_t dst = stage == Gcn::Stage::Vertex ? 2u : 0u;
				std::vector<uint32_t> body {0x7e140280u, load, 0x8000140au, 0xbf8c0f70u};
				for (uint32_t n = 1; n < count; ++n)
					body.push_back((3u << 25) | (20u << 17) | ((20u + n) << 9) | (256u + 20u));
				body.push_back(0x7e000000u | (dst << 17) | (1u << 9) | (256u + 20u));
				code.insert(code.end() - 3, body.begin(), body.end());
				const uint32_t users[] {0x200000, 16u << 16, 4, 0xfac};
				std::vector<uint32_t> words;
				std::vector<Gcn::BufferDescriptor> buffers;
				Check(Gcn::Compile(code, stage, words, error, users, 0, nullptr, {}, false, &buffers), error);
				Check(validator.Validate(words), "MTBUF/MUBUF group width SPIR-V validation");
				for (uint32_t mask: {1u, 1u << 12, 1u << 14, 1u << 15}) {
					auto bad = code;
					bad[bad.size() - 3 - body.size() + 1] ^= mask;
					std::vector<uint32_t> kept {123};
					Check(!Gcn::Compile(bad, stage, kept, error, users, 0, nullptr, {}, false, &buffers) &&
					          kept == std::vector<uint32_t> {123}, "unsupported vector load controls changed output");
				}
			}
		}
	{
		for (uint32_t mode = 0; mode < 3; ++mode) {
			const auto code = Gcn::MaskedPixelFixture(mode == 1, mode == 2);
			std::vector<uint32_t> words;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "VOPC/VCC/EXEC predication SPIR-V validation");
			const std::string names[] {"gcn-mask.frag", "gcn-mask-nested.frag", "gcn-select.frag"};
			std::ofstream file(folder / (names[mode] + ".spv"), std::ios::binary);
			file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
			Check(bool(file), "write lane mask fixture");
		}
		for (uint32_t op = 0; op < 16; ++op) {
			auto comparison = Gcn::MaskedPixelFixture(false, true);
			comparison[9] = (comparison[9] & ~(255u << 17)) | (op << 17);
			std::vector<uint32_t> words;
			Check(Gcn::Compile(comparison, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "F32 VOPC predicate family SPIR-V validation");
		}
		const auto reject_mask = [&](const std::vector<uint32_t>& bad) {
			std::vector<uint32_t> kept {123};
			Check(!Gcn::Compile(bad, Gcn::Stage::Pixel, kept, error, {}, 1) &&
			          kept == std::vector<uint32_t> {123}, "unsafe mask program accepted/changed output");
		};
		auto bad = Gcn::MaskedPixelFixture();
		for (auto& word: bad) if (word == 0xbefe0410u) word = 0xbf800000u;
		std::vector<uint32_t> masked_export;
		Check(Gcn::Compile(bad, Gcn::Stage::Pixel, masked_export, error, {}, 1), error);
		Check(validator.Validate(masked_export), "export under reduced EXEC SPIR-V");
		bad = Gcn::MaskedPixelFixture();
		for (auto& word: bad) if (word == 0xbefe0410u) word = 0xbefe0412u;
		reject_mask(bad); // undefined source pair
		bad = Gcn::MaskedPixelFixture();
		for (auto& word: bad) if (word == 0xbe90246au) word = 0xbe90246bu;
		reject_mask(bad); // VCC_HI is not a complete wave64 source
		bad = Gcn::MaskedPixelFixture();
		for (size_t i = 0; i < bad.size(); ++i) if (bad[i] == 0xbe90246au) {
			bad.insert(bad.begin() + i + 1, 0xbe910380u); // overwrite high word of active saved pair
			break;
		}
		Check(Gcn::Compile(bad, Gcn::Stage::Pixel, masked_export, error, {}, 1), error);
		Check(validator.Validate(masked_export), "clobbered snapshot remains a legal PS mask");
		bad = Gcn::MaskedPixelFixture();
		for (size_t i = 0; i < bad.size(); ++i) if (bad[i] == 0xbe90246au) {
			bad.insert(bad.begin() + i + 1, 0x7e280280u); // v20 is defined only under reduced EXEC.
			break;
		}
		bad[bad.size() - 2] = 0x05040314u; // Restored EXEC may now read an undefined v20.
		reject_mask(bad);
		bad = Gcn::MaskedPixelFixture();
		for (size_t i = 0; i < bad.size(); ++i) if (bad[i] == 0xbe90246au) {
			bad.insert(bad.begin() + i + 1, 0xbf820003u); // skip the restore and reach the export
			break;
		}
		reject_mask(bad);
		bad = Gcn::MaskedPixelFixture();
		for (size_t i = 0; i < bad.size(); ++i) if (bad[i] == 0xbe90246au) {
			bad.insert(bad.begin() + i, {0xbf068080u, 0xbf850001u}); // branch skips only SAVEEXEC
			break;
		}
		reject_mask(bad); // restore reads undefined SGPRs on the path that skips SAVEEXEC
		bad = Gcn::MaskedPixelFixture();
		for (size_t i = 0; i < bad.size(); ++i) if (bad[i] == 0xbe90246au) {
			bad.erase(bad.begin() + i);
			for (uint32_t n = 0; n < 9; ++n)
				bad.insert(bad.begin() + i + n, 0xbe80246au | ((16u + 2 * n) << 16));
			break;
		}
		Check(Gcn::Compile(bad, Gcn::Stage::Pixel, masked_export, error, {}, 1), error);
		Check(validator.Validate(masked_export), "nine consecutive SAVEEXEC operations");
		bad = Gcn::MaskedPixelFixture(false, true);
		bad.erase(bad.begin() + 9); // remove defining VOPC; V_CNDMASK cannot read undefined VCC
		reject_mask(bad);
	}
	{
		for (uint32_t mode = 0; mode < 5; ++mode) {
			const auto code = Gcn::ElsePixelFixture(mode != 1, mode == 4, mode == 2 ? 0 : mode == 3 ? 15 : 1);
			std::vector<uint32_t> words;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "EXEC else/guard/empty/nested SPIR-V validation");
			std::ofstream file(folder / ("gcn-else" + std::to_string(mode) + ".frag.spv"), std::ios::binary);
			file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
			Check(bool(file), "write EXEC else fixture");
		}
		const auto reject_else = [&](const std::vector<uint32_t>& code) {
			std::vector<uint32_t> words {123};
			Check(!Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1) &&
			          words == std::vector<uint32_t> {123}, "unsafe else/guard accepted");
		};
		auto bad = Gcn::ElsePixelFixture();
		for (auto& word: bad) if (word == 0x89fe7e10u) word = 0x89fe7e12u;
		reject_else(bad); // different saved pair
		bad = Gcn::ElsePixelFixture();
		for (auto& word: bad) if ((word & 0xffff0000u) == 0xbf880000u) { word = 0xbf880001u; break; }
		reject_else(bad); // guard targets the literal of a VALU instruction
		bad = Gcn::ElsePixelFixture(false, true);
		for (auto& word: bad) if (word == 0x89fe7e12u) word = 0x89fe7e10u;
		std::vector<uint32_t> words;
		Check(Gcn::Compile(bad, Gcn::Stage::Pixel, words, error, {}, 1), error);
		Check(validator.Validate(words), "nested XOR can read the parent's saved EXEC");
	}
	{
		for (uint32_t mode = 0; mode < 7; ++mode) {
			auto code = mode < 2 ? Gcn::IntegerComparePixelFixture(mode == 1) : Gcn::CmpxPixelFixture(mode - 2);
			if (mode == 2) {
				const auto save = std::find(code.begin(), code.end(), 0xbe94046au);
				Check(save != code.end() && save != code.begin(), "CMPX explicit destination fixture");
				// Replace CMPX VCC and its following VCC->s20 copy with CMPX s[20:21].
				const uint32_t encoded_cmpx = *(save - 1);
				*(save - 1) = 0xd0000014u | (encoded_cmpx & (255u << 17));
				*save = (encoded_cmpx & 0x1ffffu) | (1u << 17);
				// Keep blue only if every predicate agrees with the CPU oracle.
				// These cases run in the existing D3D12/WARP/Vulkan pixel readback.
				std::vector<uint32_t> checks {0x7e1a0280u}; // v13 = 0
				const auto load = [&](uint32_t reg, uint32_t value) {
					checks.insert(checks.end(), {0x7e0002ffu | (reg << 17), value});
				};
				const auto compare = [&](uint32_t op, uint32_t rhs, bool expected) {
					checks.push_back(0x7c00010au | (op << 17) | (rhs << 9));
					checks.push_back((4u << 17) | ((expected ? 4u : 13u) << 9) |
					                 (expected ? 269u : 260u)); // v_cndmask_b32 v4
				};
				const uint64_t values[] {
				    0x7ff0000000000001ull, 0x7ff8000000000000ull, 0xfff0000000000000ull,
				    0xbff0000000000000ull, 0x8000000000000001ull, 0x8000000000000000ull,
				    0x0000000000000000ull, 0x0000000000000001ull, 0x3ff0000000000000ull,
				    0x7ff0000000000000ull};
				for (uint32_t bit = 0; bit < std::size(values); ++bit) {
					load(10, uint32_t(values[bit]));
					load(11, uint32_t(values[bit] >> 32));
					load(12, 1u << bit);
					compare(168, 12, true);
					load(12, 0x3ffu ^ (1u << bit));
					compare(168, 12, false);
				}
				load(10, 0x8000);
				load(12, 0);
				compare(202, 12, true); // -0 == +0
				load(10, 0x0001);
				compare(204, 12, true); // smallest positive subnormal > +0
				load(10, 0x7e00);
				compare(207, 12, false); // NaN is unordered
				load(10, 0xbf800000u); // -1.0
				checks.push_back(0x7c00190au); // V_CMP_F_F32 -> VCC = 0
				checks.insert(checks.end(), {
				    0xd0080114u, 0x0002190au, // V_CMP_GT_F32 s[20:21], abs(v10), v12
				    0x00081b04u, // Keep blue when VCC is still false.
				    0xbeea0414u, // Copy explicit compare result to VCC.
				    0x0008090du, // Keep blue when the explicit compare is true.
				});
				load(12, 0);
				load(13, 0x3ff40000);
				// The keep-blue predicate needs a dedicated zero VGPR now that v13
				// contains the high word of the right operand.
				load(15, 0);
				const auto check_literal = [&](uint32_t opcode, uint32_t literal) {
					checks.insert(checks.end(), {0x7c0018ffu | (opcode << 17), literal, 0x0008090fu});
				};
				check_literal(34, 0x3ff40000); // F64 literal supplies the high DWORD.
				load(12, 0x87654321);
				load(13, 0xffffffff);
				check_literal(162, 0x87654321); // I64 sign extension.
				load(13, 0);
				check_literal(226, 0x87654321); // U64 zero extension.
				load(12, 512);
				check_literal(168, 0x7ff00000); // F64 class reads the literal as +infinity.
				load(12, 0);
				load(13, 0x3ff00000);
				checks.insert(checks.end(), {0x7c4418f2u, 0x0008090fu}); // F64 inline 1.0.
				load(12, 0xffffffff);
				load(13, 0xffffffff);
				checks.insert(checks.end(), {0x7d4418c1u, 0x0008090fu}); // I64 inline -1.
				for (uint32_t mask_pair : {106u, 126u}) {
					// Compare a mask pair with itself via the VOP3 scalar bus.
					checks.insert(checks.end(), {0xd0000014u | (226u << 17),
					    mask_pair | (mask_pair << 9), 0xbeea0414u, 0x0008090fu});
				}
				const auto check_special = [&](uint32_t source, uint32_t expected) {
					load(12, expected);
					checks.insert(checks.end(), {0x7d841800u | source, 0x0008090fu});
				};
				check_special(252, 0); // EXECZ while the entry mask is restored.
				check_special(251, 0); // The previous compare left VCC nonzero.
				checks.push_back(0x7c00190au); // V_CMP_F_F32 clears VCC.
				check_special(251, 1);
				checks.push_back(0xbf068080u); // S_CMP_EQ_U32 0, 0 sets SCC.
				check_special(253, 1);
				checks.push_back(0xbf068180u); // S_CMP_EQ_U32 0, 1 clears SCC.
				check_special(253, 0);
				load(10, 0x00ff0000u);
				load(12, 0xffffffffu);
				// I32 EQ, sign-extended byte 2 versus the complete DWORD.
				checks.insert(checks.end(), {0x7d0418f9u, 0x060a940au,
				                            0xbeea0414u, 0x0008090fu});
				load(10, 0xabcd0000u);
				load(12, 0x0000abcdu);
				// U32 EQ, high word of v10 versus low word of v12.
				checks.insert(checks.end(), {0x7d8418f9u, 0x0405000au, 0x0008090fu});
				code.insert(code.end() - 3, checks.begin(), checks.end());
			}
			if (mode == 4) {
				const auto save = std::find(code.begin(), code.end(), 0xbe94046au);
				Check(save != code.end() && save != code.begin(), "CMPX SDWA fixture");
				const uint32_t raw = *(save - 1);
				*(save - 1) = (raw & ~511u) | 249u;
				*save = 0x06069400u | (raw & 255); // DWORD selects and explicit s[20:21].
			}
			std::vector<uint32_t> words;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "integer VOPC/CMPX SPIR-V validation");
			std::ofstream file(folder / ("gcn-icmp" + std::to_string(mode) + ".frag.spv"), std::ios::binary);
			file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
			Check(bool(file), "write integer/CMPX fixture");
		}
		for (uint32_t base: {128u, 192u})
			for (uint32_t predicate = 0; predicate < 8; ++predicate) {
				auto code = Gcn::IntegerComparePixelFixture();
				for (auto& word: code)
					if ((word & 0xfe000000u) == 0x7c000000u)
						word = (word & ~(255u << 17)) | ((base + predicate) << 17);
				std::vector<uint32_t> words;
				Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
				Check(validator.Validate(words), "integer predicate family SPIR-V validation");
			}
		for (uint32_t source : {251u, 253u}) {
			auto code = Gcn::IntegerComparePixelFixture();
			for (auto& word : code)
				if ((word & 0xfe000000u) == 0x7c000000u)
					word = (word & ~511u) | source;
			std::vector<uint32_t> words;
			Check(!Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1) &&
			          error.find(source == 251 ? "VCCZ requires" : "undefined SCC") != std::string::npos,
			      "undefined compare condition source");
		}
		auto masked = Gcn::IntegerComparePixelFixture();
		for (auto& word: masked) if ((word & 0xfe000000u) == 0x7c000000u) word |= 16u << 17;
		std::vector<uint32_t> masked_words;
		Check(Gcn::Compile(masked, Gcn::Stage::Pixel, masked_words, error, {}, 1), error);
		Check(validator.Validate(masked_words), "CMPX without saved frame");
		// Explicitly inspect decoded semantics: the shared RDNA path must not
		// acquire the GCN two-mask rule by opcode-name coincidence.
		ShaderRecompiler::Decoder::Program program;
		const auto cmpx = Gcn::CmpxPixelFixture();
		Check(Gcn::Decode(cmpx, Gcn::Stage::Pixel, program, error, 1), error);
		uint32_t marked = 0;
		for (const auto& inst: program.instructions) marked += inst.gcn_cmpx_updates_vcc_exec;
		Check(marked == 1 && !ShaderRecompiler::Decoder::Instruction{}.gcn_cmpx_updates_vcc_exec,
		      "GCN CMPX marker/default semantics");
	}
	{
		// A forward mask branch may skip a VALU while staying in the same EXEC frame.
		for (uint32_t opcode : {6u, 7u, 8u, 9u}) {
			auto code = Gcn::ElsePixelFixture();
			for (auto& word : code) {
				if (word == 0xbf880002u) {
					word = 0xbf800002u | (opcode << 16);
					break;
				}
			}
			std::vector<uint32_t> words;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "forward VCC/EXEC branch SPIR-V");
		}
		auto undefined = Gcn::SolidPixelFixture();
		undefined.insert(undefined.begin(), 0xbf860000u);
		std::vector<uint32_t> words {123};
		Check(!Gcn::Compile(undefined, Gcn::Stage::Pixel, words, error) &&
		          error.find("undefined VCC") != std::string::npos &&
		          words == std::vector<uint32_t> {123}, "branch with undefined VCC");
	}
	for (uint32_t opcode : {15u, 17u, 19u, 21u, 23u}) {
		auto code = Gcn::SolidPixelFixture();
		const uint32_t binary = 0x80000000u | (opcode << 23) | (20u << 16) | (18u << 8) | 16u;
		code.insert(code.begin(), {0xbe900380u, 0xbe910381u, 0xbe920381u, 0xbe930380u, binary});
		code.insert(code.end() - 3, 0x7e000215u); // Consume the upper result word.
		std::vector<uint32_t> words;
		Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error), error);
		Check(validator.Validate(words), "B64 SGPR pair SPIR-V");
		code.erase(code.begin() + 1);
		Check(!Gcn::Compile(code, Gcn::Stage::Pixel, words, error) &&
		          error.find("undefined upper SGPR") != std::string::npos, "undefined B64 input");
	}
	{
		// Select and conditionally replace VCC, then edit and reverse its two words.
		const std::vector<uint32_t> mask_ops {
		    0xbeea04c1u, // S_MOV_B64 VCC, -1
		    0xbf008080u, // S_CMP_EQ_I32 0, 0
		    0xbeea0680u, // S_CMOV_B64 VCC, 0
		    0xbeea1e7fu, // S_BITSET1_B64 VCC, EXEC_HI
		    0xbeea1c80u, // S_BITSET0_B64 VCC, 0
		    0xbeea0c6au, // S_BREV_B64 VCC, VCC
		    0xbeea086au, // S_NOT_B64 VCC, VCC
		    0x85ea806au, // S_CSELECT_B64 VCC, VCC, 0
		};
		auto code = Gcn::SolidPixelFixture();
		code.insert(code.begin(), mask_ops.begin(), mask_ops.end());
		// Consume the final VCC_HI through an ordinary SGPR source.
		code.insert(code.end() - 3, {0xbe94036bu, 0x7e000214u});
		std::vector<uint32_t> words;
		Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error), error);
		Check(validator.Validate(words), "B64 mask operations SPIR-V");
		code.erase(code.begin());
		Check(!Gcn::Compile(code, Gcn::Stage::Pixel, words, error) &&
		          error.find("undefined VCC half") != std::string::npos,
		      "conditional move requires a defined old destination");
		auto halves = Gcn::SolidPixelFixture();
		halves.insert(halves.begin(), {0xbeea0381u, 0xbeeb0380u,
		                              0xbeeb1d9fu, // BITSET1 VCC_HI, 31
		                              0xbeeb0b6bu, // BREV VCC_HI, VCC_HI
		                              0xbeeb1b80u, // BITSET0 VCC_HI, 0
		                              0xbeea096au, // WQM_B32 VCC_LO, VCC_LO
		                              0xbeeb0581u, // CMOV VCC_HI, 1, using WQM's SCC
		                              0xbeea0a6au}); // WQM_B64 VCC, VCC
		halves.insert(halves.end() - 3, {0xbe94036bu, 0x7e000214u});
		Check(Gcn::Compile(halves, Gcn::Stage::Pixel, words, error), error);
		Check(validator.Validate(words), "half-mask operations SPIR-V");
		halves.erase(halves.begin() + 1);
		Check(!Gcn::Compile(halves, Gcn::Stage::Pixel, words, error) &&
		          error.find("undefined VCC half") != std::string::npos, "bitset needs old half");
		for (uint32_t op : {31u, 33u, 35u, 37u, 41u, 42u}) {
			auto mixed = Gcn::SolidPixelFixture();
			// s23 is deliberately undefined. The shift or field control is one word.
			const uint32_t lhs = op == 37 ? 129u : 16u;
			mixed.insert(mixed.begin(), {0xbe9004c1u, 0xbe9603bfu,
			                             0x806a1600u | (op << 23) | lhs});
			mixed.insert(mixed.end() - 3, {0xbe94036bu, 0x7e000214u});
			Check(Gcn::Compile(mixed, Gcn::Stage::Pixel, words, error), error);
			Check(validator.Validate(words), "mixed scalar width B64 SPIR-V");
			if (op == 37) continue;
			mixed[0] = 0xbe9003c1u;
			Check(!Gcn::Compile(mixed, Gcn::Stage::Pixel, words, error) &&
			          error.find("undefined upper SGPR") != std::string::npos, "wide data needs a pair");
		}
	}
	{
		auto code = Gcn::SolidPixelFixture();
		// s0 = 3; do { s0 -= 1; } while (s0 != 0).
		code.insert(code.begin(), {0xbe800383u, 0x80808100u, 0xbf078000u, 0xbf85fffdu});
		std::vector<uint32_t> words;
		Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error), error);
		Check(validator.Validate(words), "GCN scalar loop SPIR-V");
		// A backedge cannot make a value defined at the first loop entry.
		code[0] = 0xbf800000u;
		Check(!Gcn::Compile(code, Gcn::Stage::Pixel, words, error) &&
		          error.find("undefined register") != std::string::npos, "loop entry definition");
	}
	{
		auto code = Gcn::SolidPixelFixture();
		code.insert(code.begin(), {0xbeea0380u, 0xbeeb0381u,
		                          0xbf0ca06au, // BITCMP0_B32 VCC_LO, 32
		                          0xbf0da06bu, // BITCMP1_B32 VCC_HI, 32
		                          0xbf0ea06au, // BITCMP0_B64 VCC, 32
		                          0xbf0fa06au}); // BITCMP1_B64 VCC, 32
		std::vector<uint32_t> words;
		Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error), error);
		Check(validator.Validate(words), "scalar bit compare mask halves");
		code.erase(code.begin() + 1);
		Check(!Gcn::Compile(code, Gcn::Stage::Pixel, words, error) &&
		          error.find("undefined VCC half") != std::string::npos, "undefined bit compare operand");
		for (uint32_t op = 3; op <= 14; ++op) {
			auto immediate = Gcn::SolidPixelFixture();
			immediate.insert(immediate.begin(), {0xbe9003c1u, 0xb010ffffu | (op << 23)});
			ShaderRecompiler::Decoder::Program program;
			Check(Gcn::Decode(immediate, Gcn::Stage::Pixel, program, error), error);
			Check(program.instructions[1].src1.value == (op < 9 ? 0xffffffffu : 0xffffu),
			      "SOPK comparison immediate extension");
			Check(Gcn::Compile(immediate, Gcn::Stage::Pixel, words, error), error);
			Check(validator.Validate(words), "scalar immediate compare SPIR-V");
		}
		auto dpp = Gcn::IntegerComparePixelFixture();
		for (auto& word : dpp)
			if ((word & 0xfe000000u) == 0x7c000000u) word = (word & ~511u) | 250u;
		Check(!Gcn::Compile(dpp, Gcn::Stage::Pixel, words, error, {}, 1) &&
		          error.find("DPP is not supported") != std::string::npos, "Gen4 VOPC DPP rejected");
	}
	{
		for (uint32_t opcode = 64; opcode < 128; ++opcode) {
			for (bool vop3 : {false, true}) {
				auto code = Gcn::SolidPixelFixture();
				// v[8:9] contains a NaN in either precision. v[10:11] is zero.
				std::vector<uint32_t> compare {
				    0xbe90047eu, 0x7e1002ffu, 0x7fc00001u,
				    0x7e1202ffu, 0x7ff80000u, 0x7e140280u, 0x7e160280u};
				if (vop3) {
					compare.push_back(0xd000016au | (opcode << 17)); // abs(src0)
					compare.push_back((266u << 9) | 264u);
				} else {
					compare.push_back(0x7c000000u | (opcode << 17) | (10u << 9) | 264u);
				}
				compare.insert(compare.end(), {0xbefe0410u, 0x00001480u}); // restore, v_cndmask v0
				code.insert(code.end() - 3, compare.begin(), compare.end());
				std::vector<uint32_t> words;
				Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error), error);
				Check(validator.Validate(words), "signaling FP compare SPIR-V");
			}
		}
		for (uint32_t predicate = 0; predicate < 16; ++predicate) {
			auto code = Gcn::CmpxPixelFixture(3);
			for (auto& word : code)
				if ((word & 0xfffe0000u) == (0x7c000000u | (177u << 17)))
					word = (word & ~(255u << 17)) | ((48u + predicate) << 17);
			std::vector<uint32_t> words;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "F64 CMPX SPIR-V");
		}
		for (uint32_t opcode : {136u, 137u, 138u, 139u, 140u, 141u, 142u,
		                        169u, 170u, 171u, 172u, 173u, 174u,
		                        200u, 201u, 202u, 203u, 204u, 205u, 206u, 207u}) {
			auto code = Gcn::IntegerComparePixelFixture();
			for (auto& word : code)
				if ((word & 0xfe000000u) == 0x7c000000u)
					word = (word & ~(255u << 17)) | (opcode << 17);
			std::vector<uint32_t> words;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "VOPC 16-bit or F32 class SPIR-V");
		}
		{
			auto code = Gcn::CmpxPixelFixture(3);
			for (auto& word : code)
				if ((word & 0xfffe0000u) == (0x7c000000u | (177u << 17)))
					word = (word & ~(255u << 17)) | (184u << 17);
			std::vector<uint32_t> words;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "F64 class CMPX SPIR-V");
		}
		for (uint32_t exit = 0; exit < 2; ++exit) {
			auto code = Gcn::MaskedPixelFixture(false, false);
			const auto restore = std::find(code.begin(), code.end(), 0xbefe0410u);
			Check(restore != code.end(), "multi-exit fixture restore");
			code.erase(restore, code.end());
			const auto save = std::find(code.begin(), code.end(), 0xbe90246au);
			Check(save != code.end(), "multi-exit fixture save");
			const auto save_index = save - code.begin();
			*save = exit ? 0xbe90037eu : 0xbe90047eu; // Save EXEC_LO or the pair.
			if (exit) code.insert(code.begin() + save_index + 1, 0xbe91037fu); // Save EXEC_HI.
			code.insert(code.begin() + save_index + (exit ? 2 : 1), 0x87fe6a7eu); // AND EXEC, EXEC, VCC
			// The preceding VALU write is divergent in both scenes. Select a
			// known wave-uniform exit afterward so the oracle is independent of
			// how the host groups pixels into waves.
			// A scalar high bit must count even on a host subgroup of 32 lanes.
			code.insert(code.end(), {0xbe920380u, 0xbe930381u});
			if (exit) {
				code.insert(code.end(), {0xbefe0312u, 0xbeff0313u});
			} else {
				code.push_back(0xbefe0412u); // MOV EXEC, s[18:19]
			}
			code.insert(code.end(), {0xbe940410u, exit ? 0xbf890007u : 0xbf880006u});
			const auto append_exit = [&](float blue) {
				if (exit) code.insert(code.end(), {0xbefe0314u, 0xbeff0315u});
				else code.push_back(0xbefe0414u);
				code.insert(code.end(), {0x7e0802ffu, std::bit_cast<uint32_t>(blue),
				                         0xf800180fu, 0x05040302u, 0xbf810000u});
			};
			append_exit(.375f);
			append_exit(.625f);
			std::vector<uint32_t> words;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "divergent two-exit SPIR-V");
			std::ofstream file(folder / ("gcn-cfg-exit" + std::to_string(exit) + ".frag.spv"), std::ios::binary);
			file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
			Check(bool(file), "write divergent two-exit fixture");
			auto clobbered = code;
			const auto copy = std::find(clobbered.begin(), clobbered.end(), 0xbe940410u);
			Check(copy != clobbered.end(), "EXEC snapshot copy");
			// Only one incoming edge clobbers the upper half.
			clobbered.insert(copy, {0xbf840001u, 0xbe910380u});
			Check(Gcn::Compile(clobbered, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "distinct EXEC provenance at join");
			code[code.size() - 6] = 0xbf800000u;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "direct EXEC write at second exit");
		}
		for (uint32_t exit = 0; exit < 2; ++exit) {
			auto code = Gcn::MaskedPixelFixture(false, false);
			const auto restore = std::find(code.begin(), code.end(), 0xbefe0410u);
			Check(restore != code.end(), "saved-frame fixture restore");
			code.erase(restore, code.end());
			const auto save = std::find(code.begin(), code.end(), 0xbe90246au);
			Check(save != code.end(), "saved-frame fixture save");
			*save = exit ? 0xbe902614u : 0xbe902414u;
			// XOR needs EXEC ^ VCC as its source to select the same lanes as AND.
			code.insert(save, exit ? 0x89947e6au : 0xbe94046au);
			const auto masked_write = code.end() - 2;
			// The fallthrough writes the complementary lanes, then recovers the
			// original active lanes before joining the path that skips both XORs.
			code.insert(masked_write, {0xbe920380u | exit, 0xbf078012u, 0xbf850004u,
			                           0x89fe7e10u, 0x7e0402ffu, std::bit_cast<uint32_t>(.875f),
			                           0x89fe7e10u});
			// Select a wave-uniform path with SCC while the divergent EXEC is saved.
			// The fallthrough arm jumps past the taken arm's exit.
			code.insert(code.end(), {0xbf078012u, 0xbf850001u, 0xbf820006u});
			for (float blue : {.625f, .375f}) {
				code.insert(code.end(), {0xbefe0410u,
				                         0x7e0802ffu, std::bit_cast<uint32_t>(blue),
				                         0xf800180fu, 0x05040302u, 0xbf810000u});
			}
			std::vector<uint32_t> words;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "divergent saved-frame branches SPIR-V");
			// Keep the same pixel oracle and exercise these branches in the GPU runner.
			std::ofstream file(folder / ("gcn-cfg-frame" + std::to_string(exit) + ".frag.spv"), std::ios::binary);
			file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
			Check(bool(file), "write saved-frame branch fixture");
			code[code.size() - 6] = 0xbf800000u;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "unrestored saved-frame exit");
		}
		for (uint32_t exit = 0; exit < 2; ++exit) {
			auto code = Gcn::MaskedPixelFixture(false, false);
			const auto save = std::find(code.begin(), code.end(), 0xbe90246au);
			Check(save != code.end(), "masked export fixture save");
			code.erase(save, code.end());
			// Convert the threshold mask into NaN on the left and zero on the right.
			code.insert(code.end(), {0x7e1802ffu, exit ? 0x7f800001u : 0x7fc00001u,
			                         0x00161880u}); // v_cndmask v11, 0, v12
			if (exit) {
				code.insert(code.end(), {0xbe90047eu, 0xd000006au | (88u << 17),
				                         (266u << 9) | 267u, 0xbefe0410u}); // CMPSX_U_F32, VOP3
			} else {
				code.push_back(0x7c000000u | (72u << 17) | (10u << 9) | 267u); // CMPS_U_F32
			}
			// Drive the complementary exits with BITCMP1_B64 and CMPK_EQ_U32.
			code.insert(code.end(), {exit ? 0xbf800000u : 0xbe960380u,
			                         exit ? 0xb5160001u : 0xbf0fa016u,
			                         0x7e0802ffu, std::bit_cast<uint32_t>(.375f), 0xbf840009u,
			                         0xbefe046au, // S_MOV_B64 EXEC, VCC
			                         0x7e2802ffu, std::bit_cast<uint32_t>(.75f),
			                         0x7c000000u | (20u << 17) | (10u << 9) | 276u, // CMPX_GT v20, v10
			                         0x7c000000u | (17u << 17) | (10u << 9) | 259u, // green < .5
			                         0x87fe6a7eu, // S_AND_B64 EXEC, EXEC, VCC
			                         0xf800180fu, 0x05040314u, 0xbf810000u,
			                         0x8afe6a7eu, // S_ANDN2_B64 EXEC, EXEC, VCC
			                         0x7e2a0302u, // v21 = v2 under the right mask
			                         0x7c000000u | (22u << 17) | (10u << 9) | 277u, // CMPX_GE v21, v10
			                         0x7c000000u | (17u << 17) | (4u << 9) | 259u, // green < .375
			                         0xf800180fu, 0x05040315u, 0xbf810000u});
			// Two callsites share a leaf. Only the first continuation writes
			// s23=1 after the leaf initializes it to zero.
			const uint32_t leaf_delta = uint32_t((code.size() + 11) * 4);
			code.insert(code.begin(), {0xbe901f00u, 0x8010ff10u, leaf_delta,
			                          0x82118011u, 0xbf008080u | exit,
			                          0xbeea0380u, 0xbeeb0381u,
			                          exit ? 0xbf870003u : 0xbf860003u,
			                          0xbe922110u, 0xbe970581u, 0xbf820001u, 0xbe922110u});
			// The second fixture uses the same pair for the target and saved PC.
			if (exit)
				// Set bit 63, extract it with sign extension, then shift -1 down to 1.
				code.insert(code.end(), {0x9296bf81u, 0x9516ff16u, 0x0001003fu, 0x9096bf16u});
			else
				code.insert(code.end(), {0xbe970380u, 0xbe971d80u, 0xbe970b17u, 0xbe971b9fu});
			code.push_back(exit ? 0xbe922112u : 0xbe802012u);
			ShaderRecompiler::Decoder::Program decoded;
			Check(Gcn::Decode(code, Gcn::Stage::Pixel, decoded, error, 1), error);
			const auto& targets = decoded.instructions.back().gcn_indirect;
			Check(targets.count == 2 && targets.pcs[0] == 36 && targets.pcs[1] == 48,
			      "local return has both proven continuations");
			std::vector<uint32_t> words;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "masked MRT export at two exits");
			std::ofstream file(folder / ("gcn-cfg-mrt" + std::to_string(exit) + ".frag.spv"), std::ios::binary);
			file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
			Check(bool(file), "write masked MRT fixture");
			auto bad = code;
			bad[2] = 4; // PC + 4 lands on the literal payload.
			Check(!Gcn::Compile(bad, Gcn::Stage::Pixel, words, error, {}, 1) &&
			          error.find("branch into literal") != std::string::npos, "indirect branch into literal");
			bad = code;
			bad[3] = 0xbe910380u; // Lose the high address word.
			Check(!Gcn::Compile(bad, Gcn::Stage::Pixel, words, error, {}, 1) &&
			          error.find("not a proven code address") != std::string::npos, "clobbered indirect PC");
			bad = code;
			bad.insert(bad.begin() + 1, 0x7e180210u); // Read the code address into a VGPR.
			Check(!Gcn::Compile(bad, Gcn::Stage::Pixel, words, error, {}, 1) &&
			          error.find("PC address cannot be used") != std::string::npos, "PC cannot escape to VGPR");
		}
		{
			auto code = Gcn::TriangleVertexFixture();
			// Route the saved entry EXEC pair through VCC before restoring it.
			code.insert(code.begin(), {0xbe902480u, 0xbeea0410u, 0xbefe046au});
			std::vector<uint32_t> words;
			Check(Gcn::Compile(code, Gcn::Stage::Vertex, words, error), error);
			Check(validator.Validate(words), "VS entry mask restored through VCC");
			code.insert(code.begin() + 2, 0xbeeb0380u); // Clobber VCC_HI.
			Check(!Gcn::Compile(code, Gcn::Stage::Vertex, words, error) &&
			          error.find("requires restored EXEC") != std::string::npos,
			      "VS export requires both entry halves");
		}
		for (uint32_t op = 36; op <= 43; ++op) {
			auto code = Gcn::MaskedPixelFixture(false, false);
			const auto save = std::find(code.begin(), code.end(), 0xbe90246au);
			Check(save != code.end(), "SAVEEXEC predicate fixture");
			*save = 0xbe90006au | (op << 8);
			std::vector<uint32_t> words;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, {}, 1), error);
			Check(validator.Validate(words), "SAVEEXEC predicate SPIR-V");
		}
	}
	// Indirect immutable table snapshots: only two user SGPRs, but two buffers,
	// image and sampler in both stages. Prepared, not executed during integration.
	{
		std::array<uint32_t, 20> table {
		    0x200000, 0, 64, 0xfac, 0x210000, 0, 64, 0xfac,
		    0x2200, 10u << 20, 7u | (3u << 14), 0x90800facu, 63u << 13, 0, 0, 0,
		    0x92, 0, 0, 0};
		const std::array<uint32_t, 2> users {0x300000, 0};
		const auto read = [&](uint64_t address, std::span<uint8_t> out) {
			if (address < users[0] || address - users[0] > sizeof(table) ||
			    out.size() > sizeof(table) - size_t(address - users[0])) return false;
			std::memcpy(out.data(), reinterpret_cast<const uint8_t*>(table.data()) + address - users[0], out.size());
			return true;
		};
		for (auto stage: {Gcn::Stage::Vertex, Gcn::Stage::Pixel}) {
			const auto code = Gcn::TableSampledTextureFixture(stage);
			std::vector<uint32_t> words, normalized;
			std::vector<Gcn::BufferDescriptor> buffers;
			Gcn::ImageDescriptor image;
			std::vector<Gcn::ScalarRead> reads;
			Check(Gcn::Compile(code, stage, words, error, users, 1, nullptr, {}, false,
			                   &buffers, &image, read, &reads, &normalized), error);
			Check(buffers.size() == 2 && buffers[0].address == 0x200000 &&
			          buffers[1].address == 0x210000 && image.address == 0x220000 &&
			          reads.size() == 2 && reads[0].address == 0x300000 && reads[0].bytes == 64 &&
			          reads[1].address == 0x300040 && reads[1].bytes == 16,
			      "S_LOAD full table snapshot/resource origin");
			Check(validator.Validate(words), "table sampled SPIR-V validation");
			const std::string name = stage == Gcn::Stage::Vertex ? "gcn-table-sample.vert" : "gcn-table-sample.frag";
			std::ofstream file(folder / (name + ".spv"), std::ios::binary);
			file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
			Check(bool(file), "write table sampled module");
			const auto reject_table = [&](const Gcn::ShaderMemory& reader,
			                              const std::vector<uint32_t>& bad_code) {
				std::vector<uint32_t> kept {123}, kept_normalized {456};
				std::vector<Gcn::BufferDescriptor> kept_buffers {{123, 456}};
				std::vector<Gcn::ScalarRead> kept_reads {{123, 456}};
				Gcn::ImageDescriptor kept_image {123, 4, 4, 256, 1024};
				Check(!Gcn::Compile(bad_code, stage, kept, error, users, 1, nullptr, {}, false,
				                    &kept_buffers, &kept_image, reader, &kept_reads, &kept_normalized) &&
				          kept == std::vector<uint32_t> {123} && kept_normalized == std::vector<uint32_t> {456} &&
				          kept_buffers[0].address == 123 && kept_image.address == 123 && kept_reads[0].address == 123,
				      "failed table specialization changed outputs");
			};
			reject_table({}, code);
			reject_table([](uint64_t, std::span<uint8_t>) { return false; }, code);
			auto bad = code;
			bad[0] &= ~0x100u;
			reject_table(read, bad);
			bad = code;
			bad[1] = (bad[1] & ~255u) | 20u; // read beyond complete table
			reject_table(read, bad);
		}
	}
	// Newly normalized scalar-result ALU families. Numeric corner/readback
	// validation is a later batch; these checks also ensure operands remain
	// definitely assigned and modules use the existing shared emitter.
	for (uint32_t opcode: {7u, 8u, 35u, 37u, 39u, 46u, 53u, 54u, 55u, 56u}) {
		auto code = Gcn::SolidPixelFixture();
		code.insert(code.end() - 3, 0x7e000000u | (opcode << 9) | 256u);
		std::vector<uint32_t> words;
		Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error), error);
		Check(validator.Validate(words), "extended VOP1 SPIR-V validation");
	}
	for (uint32_t opcode: {17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u, 25u}) {
		auto code = Gcn::SolidPixelFixture();
		code.insert(code.end() - 3, (opcode << 25) | (1u << 9) | 256u);
		std::vector<uint32_t> words;
		Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error), error);
		Check(validator.Validate(words), "extended VOP2 SPIR-V validation");
	}
	// Prepared for the deferred post-integration validation batch.
	{
		const std::array<uint32_t, 16> users {
		    0x200000, 0, 64, 0xfac, 0x92, 0, 0, 0,
		    0x2200, 10u << 20, 7u | (3u << 14), 0x90800facu, 63u << 13, 0, 0, 0};
		for (const auto stage: {Gcn::Stage::Vertex, Gcn::Stage::Pixel}) {
			const auto code = Gcn::SampledTextureFixture(stage);
			std::vector<uint32_t> words;
			std::vector<Gcn::BufferDescriptor> resources;
			Gcn::ImageDescriptor image;
			Check(Gcn::Compile(code, stage, words, error, users, 1, nullptr, {}, false,
			                   &resources, &image), error);
			Check(resources.size() == 1 && resources[0].address == 0x200000 &&
			          image.address == 0x220000 && image.width == 8 && image.height == 4,
			      "sampled image + modulation buffer");
			Check(validator.Validate(words), "IMAGE_SAMPLE_L SPIR-V validation");
			const std::string name = stage == Gcn::Stage::Vertex ? "gcn-sample.vert" : "gcn-sample.frag";
			std::ofstream file(folder / (name + ".spv"), std::ios::binary);
			file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
			Check(bool(file), "write sampled image SPIR-V");
			for (uint32_t n = 4; n < 8; ++n) {
				auto bad = users;
				bad[n] ^= n == 4 ? 1u : 0x10000u;
				std::vector<uint32_t> kept {123};
				std::vector<Gcn::BufferDescriptor> kept_resources {{123, 456}};
				Gcn::ImageDescriptor kept_image {123, 4, 4, 256, 1024};
				Check(!Gcn::Compile(code, stage, kept, error, bad, 1, nullptr, {}, false,
				                    &kept_resources, &kept_image) &&
				          kept == std::vector<uint32_t> {123} && kept_image.address == 123 &&
				          kept_resources[0].address == 123,
				      "unsupported sampler accepted/changed output");
			}
		}
		std::puts("PS4_GCN IMAGE_SAMPLE_L normalized nearest/clamp LOD0 VS/PS PASS");
	}
	// Lumen scalar-memory reference: exercise every GCN width without
	// routing PS4 encodings through the RDNA decoder. Execution is deferred
	// until the requested integration batch is complete.
	{
		const std::array<uint32_t, 8> users {0x200000, 0, 256, 0xfac,
		                                      0x210000, 0, 256, 0xfac};
		for (const auto stage: {Gcn::Stage::Vertex, Gcn::Stage::Pixel})
			for (uint32_t power = 0; power <= 4; ++power)
				for (uint32_t base: {0u, 4u}) {
					const uint32_t width = 1u << power;
					// Destination s16..31 cannot overwrite either descriptor.
					const uint32_t load = 0xc0000100u | ((8u + power) << 22) |
					                      (16u << 15) | ((base / 2) << 9);
					auto code = stage == Gcn::Stage::Vertex ? Gcn::TriangleVertexFixture()
					                                         : Gcn::SolidPixelFixture();
					// Consume the entire group, including the end of DWORDX16.
					// s32 folds the bits independently of floating-point arithmetic.
					std::vector<uint32_t> prefix {load, 0xbf8c007fu, 0xbea00310u};
					for (uint32_t n = 1; n < width; ++n)
						prefix.push_back(0x88000000u | (32u << 16) | ((16u + n) << 8) | 32u);
					code.insert(code.begin(), prefix.begin(), prefix.end());
					// Replace the first literal value with the folded load in PS;
					// VS keeps vertex ID and replaces the final position W source.
					if (stage == Gcn::Stage::Pixel) {
						code.erase(code.begin() + prefix.size() + 1);
						code[prefix.size()] = 0x7e000220u; // v_mov_b32 v0,s32
					} else {
						code.insert(code.end() - 3, 0x7e0a0220u); // v_mov_b32 v5,s32
					}
					std::vector<uint32_t> words;
					std::vector<Gcn::BufferDescriptor> resources;
					Check(Gcn::Compile(code, stage, words, error, users, 0, nullptr, {}, false,
					                   &resources), error);
					Check(resources.size() == 1 && resources[0].address == users[base],
					      "SMRD width/stage/descriptor binding");
					Check(validator.Validate(words), "SMRD width/stage SPIR-V validation");
					const auto reject_load = [&](uint32_t replacement) {
						auto bad = code;
						bad[0] = replacement;
						std::vector<uint32_t> kept {123};
						std::vector<Gcn::BufferDescriptor> kept_resources {{123, 456}};
						Check(!Gcn::Compile(bad, stage, kept, error, users, 0, nullptr, {},
						                    false, &kept_resources) &&
						          kept == std::vector<uint32_t> {123} &&
						          kept_resources[0].address == 123,
						      "invalid SMRD group/range changed output");
					};
					reject_load(load | 255u); // byte offset1020 exceeds256-byte resource
					reject_load((load & ~(127u << 15)) | (103u << 15));
					// One DWORD fits s103; rejection above also follows from
					// undefined s16 in the consumer. Wider loads exceed s103.
					reject_load((load & ~0x1ffu) | 32u); // s32 is undefined before the load
				}
		std::puts("PS4_GCN SMRD five widths, VS/PS, two descriptor slots PASS");
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
		for (uint32_t mask: {0x200u, 15u << 22}) {
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
		{
			auto code = Gcn::IndexedBufferPixelFixture();
			code[2] = 0x7e000280u; // v0 = 0, index stays fixed.
			code[4] = (code[4] & 0xffffffu) | (8u << 24); // s8 supplies SOFFSET.
			std::vector<uint32_t> words;
			std::vector<Gcn::BufferDescriptor> resources;
			Check(Gcn::Compile(code, Gcn::Stage::Pixel, words, error, ps_users, 0,
			                   nullptr, {}, false, &resources), error);
			Check(resources.size() == 2 && validator.Validate(words), "MUBUF SOFFSET SPIR-V");
			std::ofstream file(folder / "gcn-mubuf-soffset.frag.spv", std::ios::binary);
			file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
			Check(bool(file), "write MUBUF SOFFSET shader");
			code[4] = (code[4] & 0xffffffu) | (12u << 24);
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
