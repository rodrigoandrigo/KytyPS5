#include "Ps4GnmTestFixture.h"
#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "common/singleton.h"
#include "common/subsystems.h"
#include "common/threads.h"
#include "graphics/shader/shader.h"
#include "kernel/fileSystem.h"
#include "kernel/memory.h"
#include "kernel/pthread.h"
#include "libs/network.h"
#include "libs/ps4Gnm.h"
#include "libs/ps4Kernel.h"
#include "loader/runtimeLinker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

using namespace Ps4GnmTests;
using namespace Libs;
static void Check(bool ok, const std::string& text) {
	if (!ok) {
		std::fprintf(stderr, "PS4_GNM FAIL: %s\n", text.c_str());
		std::_Exit(1);
	}
}
struct Memory {
	std::vector<uint8_t> data     = std::vector<uint8_t>(0x100000);
	bool                 readonly = false, pinned = false;
	size_t               writes = 0;
	bool                 Range(uint64_t address, size_t bytes) const {
        return address >= Base && address - Base <= data.size() &&
               bytes <= data.size() - (address - Base);
	}
	void Put(uint64_t address, const auto& values) {
		Check(Range(address, values.size() * sizeof(values[0])), "test range");
		std::memcpy(data.data() + (address - Base), values.data(),
		            values.size() * sizeof(values[0]));
	}
	std::vector<uint32_t> Words(uint64_t address, size_t count) {
		std::vector<uint32_t> words(count);
		std::memcpy(words.data(), data.data() + (address - Base), count * 4);
		return words;
	}
	Ps4::GnmMemory Access() {
		return {.read =
		            [this](uint64_t a, std::span<uint8_t> b) {
			            Check(pinned, "read without mapping lease");
			            if (!Range(a, b.size())) return false;
			            std::memcpy(b.data(), data.data() + (a - Base), b.size());
			            return true;
		            },
		        .write =
		            [this](uint64_t a, std::span<const uint8_t> b) {
			            Check(pinned, "write without mapping lease");
			            if (!Range(a, b.size()) || (readonly && a == Target)) return false;
			            ++writes;
			            std::memcpy(data.data() + (a - Base), b.data(), b.size());
			            return true;
		            },
		        .writable =
		            [this](uint64_t a, size_t n) {
			            Check(pinned, "query without mapping lease");
			            return Range(a, n) && !(readonly && a == Target);
		            },
		        .hold = [this]() -> std::shared_ptr<void> {
			        Check(!pinned, "nested lease");
			        pinned = true;
			        return std::shared_ptr<void>(new int(0), [this](void* p) {
				        delete static_cast<int*>(p);
				        pinned = false;
			        });
		        }};
	}
};
struct Counts {
	uint32_t                                         calls = 0;
	std::vector<uint8_t>                             initial;
	std::vector<std::vector<uint32_t>>               shaders, vertex_shaders;
	std::vector<std::vector<uint8_t>>                buffers;
	std::vector<std::vector<uint8_t>>                buffers2;
	std::vector<std::array<std::vector<uint8_t>, 6>> resources;
	bool                                             fail = false;
};
class RecordingRenderer final: public Graphics::Ps4::GnmRenderer {
	Memory&                 memory;
	std::shared_ptr<Counts> counts;

public:
	RecordingRenderer(Memory& memory, std::shared_ptr<Counts> counts)
	    : memory(memory), counts(std::move(counts)) {}
	bool Render(const Graphics::Ps4::RasterState& state, std::span<const uint32_t> vs,
	            std::span<const uint32_t> ps, std::vector<uint8_t>& output,
	            std::string& error) override {
		Check(memory.pinned && vs.size() > 5 && ps.size() > 5, "invalid renderer call");
		++counts->calls;
		counts->initial.push_back(state.initial_pixels[0]);
		counts->shaders.emplace_back(ps.begin(), ps.end());
		counts->vertex_shaders.emplace_back(vs.begin(), vs.end());
		const auto first  = state.pixel_buffer.empty() ? state.vertex_buffer : state.pixel_buffer;
		const auto second = state.pixel_buffer.empty() ? state.vertex_buffer2 : state.pixel_buffer2;
		counts->buffers.emplace_back(first.begin(), first.end());
		counts->buffers2.emplace_back(second.begin(), second.end());
		const std::span<const uint8_t> spans[] = {
		    state.pixel_buffer,   state.pixel_buffer2,        state.vertex_buffer,
		    state.vertex_buffer2, state.pixel_texture.pixels, state.vertex_texture.pixels};
		std::array<std::vector<uint8_t>, 6> snapshots;
		for (size_t i = 0; i < 6; ++i)
			snapshots[i].assign(spans[i].begin(), spans[i].end());
		counts->resources.push_back(std::move(snapshots));
		if (counts->fail) {
			error = "injected GPU failure";
			return false;
		}
		output.assign(state.initial_pixels.begin(), state.initial_pixels.end());
		output[0] = uint8_t(100 + counts->calls);
		return true;
	}
	const char* Name() const override { return "recording renderer (unit tests only)"; }
};
static void UnitTests() {
	Memory      memory;
	std::string error;
	auto        counts = std::make_shared<Counts>();
	memory.Put(Vs, Graphics::ShaderRecompiler::Gcn::TriangleVertexFixture(false));
	memory.Put(Ps, Graphics::ShaderRecompiler::Gcn::UniformBranchPixelFixture());
	memory.Put(VsRegs, std::vector<uint32_t> {uint32_t(Vs >> 8), 0, 0xc0001, 0, 0, 4, 0});
	memory.Put(PsRegs,
	           std::vector<uint32_t> {uint32_t(Ps >> 8), 0, 0xc0000, 2, 0, 9, 0, 0, 0, 0, 0, 15});
	for (size_t i = 0; i < Bytes; i += 4) {
		memory.data[Target - Base + i]     = 10;
		memory.data[Target - Base + i + 3] = 255;
	}
	auto session = Ps4::GnmSession::Create(memory.Access(), error,
	                                       std::make_unique<RecordingRenderer>(memory, counts));
	Check(bool(session), error);
	Check(!session->SetShader(true, Commands, 29, VsRegs) &&
	          !session->SetShader(false, Commands + 29 * 4, 40, PsRegs),
	      "shader builders");
	auto       commands = memory.Words(Commands, 69);
	const auto state    = StateWords(Target, 1);
	commands.insert(commands.end(), state.begin(), state.end());
	Check(!session->Draw(Commands + (69 + state.size()) * 4, 7, 3, 0), "draw builder");
	const auto draw = memory.Words(Commands + (69 + state.size()) * 4, 7);
	commands.insert(commands.end(), draw.begin(), draw.end());
	commands.insert(commands.end(), draw.begin(), draw.end());
	memory.Put(Commands, commands);
	memory.Put(Arrays, std::vector<uint64_t> {Commands});
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(commands.size() * 4)});
	Check(!session->Submit(1, Arrays, Sizes, 0, 0), session->LastError());
	Check(!memory.pinned, "lease retained after submit");
	Check(counts->calls == 2 && counts->initial == std::vector<uint8_t>({10, 101}) &&
	          memory.data[Target - Base] == 102,
	      "ordered same-target draws preserve previous contents");
	const auto reject = [&]() {
		const auto old    = memory.data;
		const auto calls  = counts->calls;
		const auto writes = memory.writes;
		Check(session->Submit(1, Arrays, Sizes, 0, 0) != 0 && !session->LastError().empty(),
		      "invalid batch accepted");
		Check(memory.data == old && counts->calls == calls && memory.writes == writes &&
		          !memory.pinned,
		      "invalid batch had effects");
	};
	auto bad = commands;
	bad.insert(bad.end(), {0xc0009900, 0});
	memory.Put(Commands, bad);
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(bad.size() * 4)});
	reject();
	memory.Put(Commands, commands);
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(commands.size() * 4)});
	memory.readonly = true;
	reject();
	memory.readonly = false;
	memory.Put(Sizes, std::vector<uint32_t> {3});
	reject();
	memory.Put(Sizes, std::vector<uint32_t> {0x400000});
	reject();
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(commands.size() * 4)});
	const auto previous  = memory.data;
	const auto callbacks = counts->calls;
	counts->fail         = true;
	Check(session->Submit(1, Arrays, Sizes, 0, 0) != 0 && memory.data == previous &&
	          counts->calls == callbacks + 1,
	      "failed renderer committed output");
	counts->fail = false;
	std::vector<uint32_t> next;
	Reg(next, 0x76, 0xc, {0});
	next.insert(next.end(), draw.begin(), draw.end());
	memory.Put(Commands, next);
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(next.size() * 4)});
	Check(!session->Submit(1, Arrays, Sizes, 0, 0), session->LastError());
	Check(counts->shaders.front() != counts->shaders.back(),
	      "user SGPR snapshot did not reach shader");
	Check(session->Submit(17, Arrays, Sizes, 0, 0) != 0, "unbounded DCB count");
	Check(session->SetShader(true, Commands, 28, VsRegs) != 0 &&
	          session->Draw(Commands, 7, 3, 1) != 0,
	      "invalid builder controls");
	Check(!session->Done(), "submit done");
	const auto stats = session->Stats();
	Check(stats.submissions == 2 && stats.draws == 3 && stats.rejected >= 7, "GNM counters");
	session->Finish();
	Check(session->Done() != 0, "finished session accepted work");
	std::puts("PS4_GNM unit validation/transaction/lease/state/user-SGPR checks PASS");
}
static void InterpolationUnitTests() {
	Memory      memory;
	std::string error;
	auto        counts = std::make_shared<Counts>();
	memory.Put(Vs, Graphics::ShaderRecompiler::Gcn::InterpolatedVertexFixture(true));
	const auto pixel = Graphics::ShaderRecompiler::Gcn::InterpolatedPixelFixture(true);
	memory.Put(Ps, pixel);
	memory.Put(VsRegs, std::vector<uint32_t> {uint32_t(Vs >> 8), 0, 0xc0002, 0, 0, 4, 0});
	memory.Put(PsRegs,
	           std::vector<uint32_t> {uint32_t(Ps >> 8), 0, 0xc0001, 0, 0, 9, 2, 2, 1, 0, 0, 15});
	auto session = Ps4::GnmSession::Create(memory.Access(), error,
	                                       std::make_unique<RecordingRenderer>(memory, counts));
	Check(bool(session), error);
	Check(!session->SetShader(true, Commands, 29, VsRegs) &&
	          !session->SetShader(false, Commands + 29 * 4, 40, PsRegs),
	      "interpolation builders");
	auto       commands = memory.Words(Commands, 69);
	const auto state    = StateWords(Target, 0, true);
	commands.insert(commands.end(), state.begin(), state.end());
	Check(!session->Draw(Commands + (69 + state.size()) * 4, 7, 3, 0),
	      "interpolation draw builder");
	const auto draw = memory.Words(Commands + (69 + state.size()) * 4, 7);
	commands.insert(commands.end(), draw.begin(), draw.end());
	memory.Put(Commands, commands);
	memory.Put(Arrays, std::vector<uint64_t> {Commands});
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(commands.size() * 4)});
	Check(!session->Submit(1, Arrays, Sizes, 0, 0), session->LastError());
	const auto reject = [&] {
		const auto old    = memory.data;
		const auto calls  = counts->calls;
		const auto writes = memory.writes;
		Check(session->Submit(1, Arrays, Sizes, 0, 0) != 0 && !session->LastError().empty(),
		      "invalid interpolation state accepted");
		Check(memory.data == old && counts->calls == calls && memory.writes == writes &&
		          !memory.pinned,
		      "invalid interpolation batch had effects");
	};
	for (const auto& [reg, value]: std::vector<std::pair<uint32_t, uint32_t>> {
	         {0x1b3, 4}, {0x1b4, 0}, {0x1b1, 2}, {0x1b6, 5}, {0x191, 1}, {0x191, 1u << 10}}) {
		std::vector<uint32_t> bad;
		Reg(bad, 0x69, reg, {value});
		bad.insert(bad.end(), draw.begin(), draw.end());
		memory.Put(Commands, bad);
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(bad.size() * 4)});
		reject();
	}
	{
		std::vector<uint32_t> bad;
		Reg(bad, 0x76, 0xa, {0xc0000}); // Four VGPRs cannot hold v6.
		bad.insert(bad.end(), draw.begin(), draw.end());
		memory.Put(Commands, bad);
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(bad.size() * 4)});
		reject();
	}
	memory.Put(Commands, draw);
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(draw.size() * 4)});
	auto broken = pixel;
	broken[1] ^= 1u << 18;
	memory.Put(Ps, broken);
	reject();
	memory.Put(Ps, pixel);
	Check(!session->Submit(1, Arrays, Sizes, 0, 0), session->LastError());
	Check(counts->calls == 2, "invalid state leaked across submissions");
	// Four exported slots with only PARAM3 consumed exercise sparse shader input linking.
	auto many = Graphics::ShaderRecompiler::Gcn::InterpolatedVertexFixture(true);
	for (uint32_t attr = 1; attr < 4; ++attr)
		many.insert(many.end() - 1,
		            {0xf800020fu | (attr << 4), attr == 3 ? 0x09060708u : 0x09080706u});
	auto sparse = Graphics::ShaderRecompiler::Gcn::InterpolatedPixelFixture();
	for (uint32_t i = 0; i < 8; ++i)
		sparse[i] |= 3u << 10;
	memory.Put(Vs, many);
	memory.Put(Ps, sparse);
	memory.Put(VsRegs, std::vector<uint32_t> {uint32_t(Vs >> 8), 0, 0xc0002, 0, 6, 4, 0});
	memory.Put(PsRegs,
	           std::vector<uint32_t> {uint32_t(Ps >> 8), 0, 0xc0001, 0, 0, 9, 2, 2, 4, 0, 0, 15});
	Check(!session->SetShader(true, Commands, 29, VsRegs) &&
	          !session->SetShader(false, Commands + 29 * 4, 40, PsRegs),
	      "four-parameter builders");
	commands = memory.Words(Commands, 69);
	commands.insert(commands.end(), state.begin(), state.end());
	Reg(commands, 0x69, 0x192, {1, 2, 3});
	commands.insert(commands.end(), draw.begin(), draw.end());
	memory.Put(Commands, commands);
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(commands.size() * 4)});
	Check(!session->Submit(1, Arrays, Sizes, 0, 0) && counts->calls == 3, session->LastError());

	// Remap PS attribute zero to the distinct PARAM3 export through real GNM state.
	memory.Put(Ps, Graphics::ShaderRecompiler::Gcn::InterpolatedPixelFixture());
	std::vector<uint32_t> remap;
	Reg(remap, 0x69, 0x191, {3, 1, 2, 0});
	remap.insert(remap.end(), draw.begin(), draw.end());
	memory.Put(Commands, remap);
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(remap.size() * 4)});
	Check(!session->Submit(1, Arrays, Sizes, 0, 0) && counts->calls == 4, session->LastError());
	Reg(remap, 0x69, 0x191, {4});
	remap.insert(remap.end(), draw.begin(), draw.end());
	memory.Put(Commands, remap);
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(remap.size() * 4)});
	reject();
	memory.Put(Commands, draw);
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(draw.size() * 4)});
	Check(!session->Submit(1, Arrays, Sizes, 0, 0) && counts->calls == 5, session->LastError());

	std::vector<uint32_t> linear;
	Reg(linear, 0x69, 0x1b3, {32, 32});
	linear.insert(linear.end(), draw.begin(), draw.end());
	memory.Put(Commands, linear);
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(linear.size() * 4)});
	Check(!session->Submit(1, Arrays, Sizes, 0, 0) && counts->calls == 6, session->LastError());
	Check(counts->shaders[4] != counts->shaders[5], "linear interpolation qualifier missing");

	session->Finish();
	std::puts("PS4_GNM interpolation/ALU mapping/register/pair rejects and state rollback PASS");
}
static void TransferUnitTests() {
	Memory      memory;
	std::string error;
	auto        counts = std::make_shared<Counts>();
	memory.Put(Vs, Graphics::ShaderRecompiler::Gcn::TriangleVertexFixture(false));
	memory.Put(Ps, Graphics::ShaderRecompiler::Gcn::UniformBranchPixelFixture());
	memory.Put(VsRegs, std::vector<uint32_t> {uint32_t(Vs >> 8), 0, 0xc0001, 0, 0, 4, 0});
	memory.Put(PsRegs,
	           std::vector<uint32_t> {uint32_t(Ps >> 8), 0, 0xc0000, 2, 0, 9, 0, 0, 0, 0, 0, 15});
	auto session = Ps4::GnmSession::Create(memory.Access(), error,
	                                       std::make_unique<RecordingRenderer>(memory, counts));
	Check(bool(session), error);
	Check(!session->SetShader(true, Commands, 29, VsRegs) &&
	          !session->SetShader(false, Commands + 29 * 4, 40, PsRegs),
	      "transfer shader builders");
	auto       prefix = memory.Words(Commands, 69);
	const auto state  = StateWords(Target, 1);
	prefix.insert(prefix.end(), state.begin(), state.end());
	Check(!session->Draw(Commands + prefix.size() * 4, 7, 3, 0), "transfer draw builder");
	const auto draw     = memory.Words(Commands + prefix.size() * 4, 7);
	auto       commands = prefix;
	Fill(commands, Target, Bytes, 0xff302010);
	Write(commands, Target, 99);
	commands.insert(commands.end(), draw.begin(), draw.end());
	const auto split = commands.size();
	Write(commands, Target, 17);
	commands.insert(commands.end(), draw.begin(), draw.end());
	Write(commands, Target + Bytes - 4, 0xff665544);
	const auto submit = [&](const std::vector<uint32_t>& words) {
		memory.Put(Commands, words);
		memory.Put(Arrays, std::vector<uint64_t> {Commands});
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(words.size() * 4)});
		return session->Submit(1, Arrays, Sizes, 0, 0);
	};
	Check(!submit(commands), session->LastError());
	Check(counts->calls == 2 && counts->initial == std::vector<uint8_t>({99, 17}) &&
	          memory.data[Target - Base] == 102 &&
	          memory.Words(Target + Bytes - 4, 1)[0] == 0xff665544,
	      "mixed transfer/draw order or trailing write");
	memory.Put(Commands, std::vector<uint32_t>(commands.begin(), commands.begin() + split));
	memory.Put(0x409000, std::vector<uint32_t>(commands.begin() + split, commands.end()));
	memory.Put(0x404720, std::vector<uint64_t> {Commands, 0x409000});
	memory.Put(0x404740, std::vector<uint32_t> {uint32_t(split * 4),
	                                            uint32_t((commands.size() - split) * 4)});
	Check(!session->Submit(2, 0x404720, 0x404740, 0, 0) && counts->calls == 4 &&
	          counts->initial == std::vector<uint8_t>({99, 17, 99, 17}),
	      "cross-DCB transfer order");
	const auto reject = [&](const std::vector<uint32_t>& words) {
		const uint64_t command_address = words.size() * 4 > Target - Commands ? 0x480000 : Commands;
		memory.Put(command_address, words);
		memory.Put(Arrays, std::vector<uint64_t> {command_address});
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(words.size() * 4)});
		const auto old    = memory.data;
		const auto calls  = counts->calls;
		const auto writes = memory.writes;
		Check(session->Submit(1, Arrays, Sizes, 0, 0) != 0 && !session->LastError().empty(),
		      "invalid mixed GNM batch accepted");
		Check(memory.data == old && counts->calls == calls && memory.writes == writes &&
		          !memory.pinned,
		      "invalid mixed GNM batch had effects");
	};
	auto bad = commands;
	bad.insert(bad.end(), {0xc0009900, 0});
	reject(bad);
	for (uint64_t address: {Target + Bytes, Target - 4, Target + 1, UINT64_MAX - 1}) {
		bad = prefix;
		Write(bad, address, 0);
		bad.insert(bad.end(), draw.begin(), draw.end());
		reject(bad);
	}
	bad = prefix;
	Fill(bad, Target, Bytes + 4, 0);
	bad.insert(bad.end(), draw.begin(), draw.end());
	reject(bad);
	bad = prefix;
	Fill(bad, Target, Bytes, 0);
	bad[prefix.size() + 1] |= 1;
	bad.insert(bad.end(), draw.begin(), draw.end());
	reject(bad);
	bad = prefix;
	bad.insert(bad.end(), {0xc0003700, 0});
	bad.insert(bad.end(), draw.begin(), draw.end());
	reject(bad);
	bad = prefix;
	for (uint32_t i = 0; i < 4097; ++i)
		Write(bad, Target, 0);
	bad.insert(bad.end(), draw.begin(), draw.end());
	reject(bad);
	Check(session->LastError().find("count budget") != std::string::npos,
	      "wrong count budget rejection");
	bad = prefix;
	for (uint32_t i = 0; i < 2049; ++i)
		Fill(bad, Target, Bytes, 0);
	bad.insert(bad.end(), draw.begin(), draw.end());
	reject(bad);
	Check(session->LastError().find("byte budget") != std::string::npos,
	      "wrong byte budget rejection");
	bad.clear();
	Write(bad, Target, 0);
	reject(bad); // No validated draw target in this batch.
	for (uint64_t address: {Vs, Commands, Arrays & ~255ull}) {
		bad = prefix;
		Reg(bad, 0x69, 0x318, {uint32_t(address >> 8)});
		Fill(bad, address, Bytes, 0);
		bad.insert(bad.end(), draw.begin(), draw.end());
		reject(bad);
	}
	memory.readonly = true;
	reject(commands);
	memory.readonly = false;
	counts->fail    = true;
	memory.Put(Commands, commands);
	memory.Put(Arrays, std::vector<uint64_t> {Commands});
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(commands.size() * 4)});
	const auto old = memory.data;
	Check(session->Submit(1, Arrays, Sizes, 0, 0) != 0 && memory.data == old,
	      "host failure published pending transfers");
	counts->fail = false;
	Check(!submit(commands), session->LastError());
	session->Finish();
	std::puts("PS4_GNM transfer ordering, cross-DCB, budgets, alias and rollback PASS");
}
static void BufferUnitTests() {
	Memory      memory;
	std::string error;
	auto        counts = std::make_shared<Counts>();
	memory.Put(Vs, Graphics::ShaderRecompiler::Gcn::TriangleVertexFixture(false));
	memory.Put(Ps, Graphics::ShaderRecompiler::Gcn::BufferPixelFixture(4));
	memory.Put(VsRegs, std::vector<uint32_t> {uint32_t(Vs >> 8), 0, 0xc0001, 0, 0, 4, 0});
	memory.Put(PsRegs,
	           std::vector<uint32_t> {uint32_t(Ps >> 8), 0, 0xc0000, 8, 0, 9, 0, 0, 0, 0, 0, 15});
	memory.Put(0x404b80, std::vector<uint32_t>(16, 0x11111111));
	auto session = Ps4::GnmSession::Create(memory.Access(), error,
	                                       std::make_unique<RecordingRenderer>(memory, counts));
	Check(bool(session), error);
	Check(!session->SetShader(true, Commands, 29, VsRegs) &&
	          !session->SetShader(false, Commands + 29 * 4, 40, PsRegs),
	      "buffer builders");
	auto       commands = memory.Words(Commands, 69);
	const auto state    = StateWords(Target, 1);
	commands.insert(commands.end(), state.begin(), state.end());
	Reg(commands, 0x76, 0xc, {0x404b80, 0, 64, 0xfac});
	const size_t descriptor_at = commands.size() - 4;
	Check(!session->Draw(Commands + commands.size() * 4, 7, 3, 0), "buffer draw builder");
	const auto draw = memory.Words(Commands + commands.size() * 4, 7);
	commands.insert(commands.end(), draw.begin(), draw.end());
	const auto submit = [&](const auto& words) {
		memory.Put(Commands, words);
		memory.Put(Arrays, std::vector<uint64_t> {Commands});
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(words.size() * 4)});
		return session->Submit(1, Arrays, Sizes, 0, 0);
	};
	Check(!submit(commands), session->LastError());
	memory.Put(0x404b80, std::vector<uint32_t>(16, 0x22222222));
	Check(!submit(commands), session->LastError());
	Check(counts->buffers.size() == 2 && counts->buffers[0].size() == 64 &&
	          counts->buffers[0][16] == 0x11 && counts->buffers[1][16] == 0x22 &&
	          counts->shaders[0] == counts->shaders[1],
	      "buffer snapshot changed shader or failed to update data");
	for (const auto& [field, value]:
	     std::vector<std::pair<size_t, uint32_t>> {{0, 0x600000},
	                                               {0, uint32_t(Target)},
	                                               {0, 0x404b81},
	                                               {1, 0x10000},
	                                               {2, 16},
	                                               {3, 0xfad}}) {
		auto bad                   = commands;
		bad[descriptor_at + field] = value;
		memory.Put(Commands, bad);
		const auto old   = memory.data;
		const auto calls = counts->calls;
		Check(session->Submit(1, Arrays, Sizes, 0, 0) != 0 && !session->LastError().empty() &&
		          memory.data == old && counts->calls == calls && !memory.pinned,
		      "bad buffer descriptor had effects");
	}
	Check(!submit(commands), session->LastError());
	session->Finish();
	std::puts("PS4_GNM buffer snapshot/data update/binding alias and descriptor rejects PASS");
}
static void IndexedUnitTests(bool vertex) {
	Memory      memory;
	std::string error;
	auto        counts = std::make_shared<Counts>();
	memory.Put(Vs, vertex ? Graphics::ShaderRecompiler::Gcn::FetchVertexFixture()
	                      : Graphics::ShaderRecompiler::Gcn::TriangleVertexFixture(false));
	memory.Put(Ps, vertex ? Graphics::ShaderRecompiler::Gcn::InterpolatedPixelFixture()
	                      : Graphics::ShaderRecompiler::Gcn::IndexedBufferPixelFixture());
	memory.Put(VsRegs, std::vector<uint32_t> {uint32_t(Vs >> 8), 0, vertex ? 0xc0002u : 0xc0001u,
	                                          vertex ? 16u : 0u, 0, 4, 0});
	memory.Put(PsRegs, std::vector<uint32_t> {uint32_t(Ps >> 8), 0, vertex ? 0xc0001u : 0xc0041u,
	                                          vertex ? 0u : 16u, 0, 9, vertex ? 2u : 0u,
	                                          vertex ? 2u : 0u, vertex ? 1u : 0u, 0, 0, 15});
	memory.Put(0x404b80, std::vector<uint32_t>(16, 1));
	memory.Put(0x404bc0, std::vector<uint32_t>(16, 0x11111111));
	auto session = Ps4::GnmSession::Create(memory.Access(), error,
	                                       std::make_unique<RecordingRenderer>(memory, counts));
	Check(bool(session), error);
	Check(!session->SetShader(true, Commands, 29, VsRegs) &&
	          !session->SetShader(false, Commands + 29 * 4, 40, PsRegs),
	      "indexed builders");
	auto       commands = memory.Words(Commands, 69);
	const auto state    = StateWords(Target, 0, vertex);
	commands.insert(commands.end(), state.begin(), state.end());
	Reg(commands, 0x76, vertex ? 0x4c : 0xc,
	    {0x404b80, vertex ? 16u << 16 : 0u, vertex ? 4u : 16u, 0xfac, 0x404bc0, 16u << 16, 4,
	     0xfac});
	const size_t descriptor_at = commands.size() - 8;
	Check(!session->Draw(Commands + commands.size() * 4, 7, 3, 0), "indexed draw builder");
	const auto draw = memory.Words(Commands + commands.size() * 4, 7);
	commands.insert(commands.end(), draw.begin(), draw.end());
	const auto submit = [&](const auto& words) {
		memory.Put(Commands, words);
		memory.Put(Arrays, std::vector<uint64_t> {Commands});
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(words.size() * 4)});
		return session->Submit(1, Arrays, Sizes, 0, 0);
	};
	Check(!submit(commands), session->LastError());
	memory.Put(0x404b80, std::vector<uint32_t>(16, 2));
	memory.Put(0x404bc0, std::vector<uint32_t>(16, 0x22222222));
	Check(!submit(commands), session->LastError());
	Check(counts->buffers.size() == 2 && counts->buffers[0][0] == 1 && counts->buffers[1][0] == 2 &&
	          counts->buffers2[0][0] == 0x11 && counts->buffers2[1][0] == 0x22 &&
	          counts->shaders[0] == counts->shaders[1] &&
	          counts->vertex_shaders[0] == counts->vertex_shaders[1],
	      "two buffer snapshots did not change independently");
	for (const auto& field: std::vector<std::pair<size_t, uint32_t>> {{4, 0x600000},
	                                                                  {4, uint32_t(Target)},
	                                                                  {5, 0},
	                                                                  {5, 3u << 16},
	                                                                  {6, 0},
	                                                                  {6, 5000},
	                                                                  {7, 0xfad}}) {
		auto bad                         = commands;
		bad[descriptor_at + field.first] = field.second;
		memory.Put(Commands, bad);
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(bad.size() * 4)});
		const auto old    = memory.data;
		const auto calls  = counts->calls;
		const auto writes = memory.writes;
		Check(session->Submit(1, Arrays, Sizes, 0, 0) != 0 && memory.data == old &&
		          counts->calls == calls && memory.writes == writes,
		      "bad second buffer had effects");
	}
	// An under-declared register group must be rejected, not just its first register.
	auto bad = commands;
	Reg(bad, 0x76, vertex ? 0x4a : 0xa, {vertex ? 0xc0001u : 0xc0040u});
	bad.insert(bad.end(), draw.begin(), draw.end());
	const auto calls = counts->calls;
	Check(submit(bad) != 0 && counts->calls == calls, "underdeclared MUBUF VGPR group accepted");
	if (!vertex) {
		// Independent bindings and metadata now permit buffers in both stages.
		memory.Put(Vs, std::vector<uint32_t> {0xe0382000, 0x80000200, 0xbf8c0f70, 0xf80008cf,
		                                      0x05040302, 0xbf810000});
		auto both = commands;
		both.resize(both.size() - draw.size());
		Reg(both, 0x76, 0x4b, {8});
		Reg(both, 0x76, 0x4c, {0x404b80, 16u << 16, 4, 0xfac});
		both.insert(both.end(), draw.begin(), draw.end());
		memory.Put(Commands, both);
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(both.size() * 4)});
		const auto calls_before = counts->calls;
		Check(!session->Submit(1, Arrays, Sizes, 0, 0) && counts->calls == calls_before + 1,
		      "simultaneous VS/PS buffers rejected");
		memory.Put(Vs, Graphics::ShaderRecompiler::Gcn::TriangleVertexFixture(false));
	}
	Check(!submit(commands), session->LastError());
	session->Finish();
	std::printf("PS4_GNM %s two buffers, data updates, ranges/alias/group rejects PASS\n",
	            vertex ? "vertex fetch" : "indexed MUBUF");
}
static void TextureUnitTests() {
	Memory      memory;
	std::string error;
	auto        counts = std::make_shared<Counts>();
	memory.Put(Vs, Graphics::ShaderRecompiler::Gcn::TextureVertexFixture());
	memory.Put(Ps, Graphics::ShaderRecompiler::Gcn::TexturePixelFixture());
	memory.Put(VsRegs, std::vector<uint32_t> {uint32_t(Vs >> 8), 0, 0xc0043, 32, 0, 4, 0});
	memory.Put(PsRegs,
	           std::vector<uint32_t> {uint32_t(Ps >> 8), 0, 0xc0084, 32, 0, 9, 2, 2, 1, 0, 0, 15});
	const auto store = [&](uint64_t address, std::span<const uint8_t> bytes) {
		memory.Put(address, std::vector<uint8_t>(bytes.begin(), bytes.end()));
	};
	const auto store_payload = [&](const auto& p) {
		store(0x410000, Ps4TextureTests::Bytes(p.positions));
		store(0x410040, Ps4TextureTests::Bytes(p.colors));
		store(0x410100, Ps4TextureTests::Bytes(p.coordinates));
		store(0x410200, Ps4TextureTests::Bytes(p.modulation));
		store(0x410800, p.vs_image);
		store(0x411000, p.ps_image);
	};
	store_payload(Ps4TextureTests::Make(1, 2));
	auto session = Ps4::GnmSession::Create(memory.Access(), error,
	                                       std::make_unique<RecordingRenderer>(memory, counts));
	Check(bool(session), error);
	Check(!session->SetShader(true, Commands, 29, VsRegs) &&
	          !session->SetShader(false, Commands + 29 * 4, 40, PsRegs),
	      "texture shader builders");
	auto commands = memory.Words(Commands, 69);
	auto state    = StateWords(Target, 0, true);
	commands.insert(commands.end(), state.begin(), state.end());
	const auto add_users = [&](uint32_t reg, bool vertex) {
		std::vector<uint32_t> users =
		    vertex ? std::vector<uint32_t> {0x410000, 16u << 16, 4, 0xfac,
		                                    0x410040, 16u << 16, 4, 0xfac}
		           : std::vector<uint32_t> {0x410100, 0, 16, 0xfac, 0x410200, 16u << 16, 4, 0xfac};
		auto image = Ps4TextureTests::ImageWords(vertex ? 0x410800 : 0x411000);
		users.insert(users.end(), image.begin(), image.end());
		commands.push_back(0xc0107600);
		commands.push_back(reg);
		const auto at = commands.size();
		commands.insert(commands.end(), users.begin(), users.end());
		return at;
	};
	const auto vs_at = add_users(0x4c, true), ps_at = add_users(0xc, false);
	Check(!session->Draw(Commands + commands.size() * 4, 7, 3, 0), "texture draw builder");
	auto draw = memory.Words(Commands + commands.size() * 4, 7);
	commands.insert(commands.end(), draw.begin(), draw.end());
	const auto submit = [&](const auto& words) {
		memory.Put(Commands, words);
		memory.Put(Arrays, std::vector<uint64_t> {Commands});
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(words.size() * 4)});
		return session->Submit(1, Arrays, Sizes, 0, 0);
	};
	Check(!submit(commands), session->LastError());
	auto updated          = Ps4TextureTests::Make(7, 3, true);
	updated.modulation[8] = .625f;
	store_payload(updated);
	Check(!submit(commands), session->LastError());
	Check(counts->shaders[0] == counts->shaders[1] &&
	          counts->vertex_shaders[0] == counts->vertex_shaders[1],
	      "resource updates changed shader modules");
	for (size_t resource = 0; resource < 6; ++resource)
		Check(!counts->resources[0][resource].empty() &&
		          counts->resources[0][resource] != counts->resources[1][resource],
		      "independent resource snapshot missing/update lost");
	const auto reject = [&](const auto& words) {
		memory.Put(Commands, words);
		memory.Put(Arrays, std::vector<uint64_t> {Commands});
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(words.size() * 4)});
		const auto old    = memory.data;
		const auto calls  = counts->calls;
		const auto writes = memory.writes;
		Check(session->Submit(1, Arrays, Sizes, 0, 0) != 0 && memory.data == old &&
		          counts->calls == calls && memory.writes == writes,
		      "invalid texture batch had effects");
	};
	for (size_t base: {vs_at, ps_at})
		for (const auto& field:
		     std::vector<std::pair<uint32_t, uint32_t>> {{8, 0},
		                                                 {8, uint32_t(Target >> 8)},
		                                                 {8, 0x6000},
		                                                 {9, 0},
		                                                 {10, 256},
		                                                 {11, 0x90000fac},
		                                                 {12, 0},
		                                                 {13, 1},
		                                                 {14, 1},
		                                                 {15, 1}}) {
			auto bad                = commands;
			bad[base + field.first] = field.second;
			reject(bad);
		}
	auto budget = commands;
	budget.resize(budget.size() - draw.size());
	for (auto base: {vs_at, ps_at}) {
		budget[base + 8]  = base == vs_at ? 0x4300 : 0x4800;
		budget[base + 10] = 255u | (255u << 14);
		budget[base + 12] = 255u << 13;
	}
	for (uint32_t i = 0; i < 33; ++i)
		budget.insert(budget.end(), draw.begin(), draw.end());
	reject(budget);
	Check(session->LastError().find("snapshot byte budget") != std::string::npos,
	      "wrong texture budget rejection");
	Check(!submit(commands), session->LastError());
	session->Finish();
	std::puts("PS4_GNM four buffers/two images, stable modules, six payload updates, "
	          "descriptor/alias/budget rejects PASS");
}
static void RuntimeTest(Common::Subsystems& subsystems, const std::filesystem::path& root,
                        bool interpolation, bool arithmetic, bool transfers, bool buffer_load,
                        bool indexed, bool textured) {
	std::filesystem::create_directories(root / "app0");
	std::filesystem::create_directories(root / "data");
	const auto bytes =
	    Fixture(interpolation, arithmetic, transfers, buffer_load, indexed, textured);
	{
		std::ofstream file(root / "app0/eboot.bin", std::ios::binary);
		file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		Check(bool(file), "write GNM ELF fixture");
	}
	std::puts("PS4_GNM runtime initializing");
	subsystems.Initialize<LibKernel::PthreadLifecycle>();
	subsystems.Initialize<Network::Lifecycle>();
	subsystems.Initialize<LibKernel::Memory::Lifecycle>();
	subsystems.Initialize<LibKernel::FileSystem::Lifecycle>();
	auto*       rt = Common::Singleton<Loader::RuntimeLinker>::Instance();
	std::string error;
	auto*       program = rt->LoadPs4Program(root / "app0/eboot.bin", error);
	Check(program != nullptr, error);
	std::puts("PS4_GNM ELF loaded; entering guest");
	Ps4::RuntimeStats stats;
	Check(rt->ExecutePs4(program, root / "app0", root / "data", error, &stats), error);
	uint32_t status, last;
	std::memcpy(&status, reinterpret_cast<void*>(Status), 4);
	std::memcpy(&last, reinterpret_cast<void*>(Status + 4), 4);
	std::printf("PS4_GNM guest status=0x%x last=0x%x submissions=%llu draws=%llu rejected=%llu\n",
	            status, last, (unsigned long long)stats.gnm_submissions,
	            (unsigned long long)stats.gnm_draws, (unsigned long long)stats.gnm_rejected);
	Check(status == 0x600d474e && stats.gnm_submissions == 2 &&
	          stats.gnm_draws == (transfers ? 4u : 2u) && stats.gnm_rejected == 1,
	      "guest GNM builders/submit/readback");
	if (textured) {
		const auto* pixels = reinterpret_cast<const uint8_t*>(Target);
		for (uint32_t y = 0; y < Height; ++y)
			for (uint32_t x = 0; x < Width; ++x) {
				const bool inside =
				    x >= 16 && y >= 8 && (float(x) + .5f - 16) / 32 + (float(y) + .5f - 8) / 16 < 1;
				const bool earlier =
				    x >= 8 && y >= 4 && (float(x) + .5f - 8) / 48 + (float(y) + .5f - 4) / 24 < 1;
				const int color[] = {24, 32, 16, 255}, previous[] = {32, 64, 96, 255},
				          background[] = {10, 20, 30, 255};
				for (uint32_t c = 0; c < 4; ++c)
					Check(std::abs(int(pixels[(y * Width + x) * 4 + c]) -
					               (inside    ? color[c]
					                : earlier ? previous[c]
					                          : background[c])) <= 1,
					      "guest simultaneous texture/buffer pixel mismatch");
			}
		std::puts("PS4_GNM four buffers + two IMAGE_LOAD textures VS/PS, changed data/geometry, "
		          "all 2048 pixels PASS");
	}
	if (buffer_load) {
		const auto* pixels = reinterpret_cast<const uint8_t*>(Target);
		for (uint32_t y = 0; y < Height; ++y)
			for (uint32_t x = 0; x < Width; ++x) {
				const bool inside =
				    x >= 8 && y >= 4 && (float(x) + .5f - 8) / 48 + (float(y) + .5f - 4) / 24 < 1;
				const float color[]      = {.75f, .25f, .5f, 1};
				const int   background[] = {10, 20, 30, 255};
				for (uint32_t c = 0; c < 4; ++c)
					Check(std::abs(int(pixels[(y * Width + x) * 4 + c]) -
					               (inside ? int(std::lround(color[c] * 255)) : background[c])) <=
					          1,
					      "guest shader buffer pixel mismatch");
			}
		std::puts(indexed ? "PS4_GNM two buffers + indexed MUBUF -> real GPU, changed index/data; "
		                    "all 2048 pixels PASS"
		                  : "PS4_GNM descriptor + SMRD -> real GPU buffer; changed data; all 2048 "
		                    "pixels PASS");
	}
	if (transfers) {
		const auto* rgba = reinterpret_cast<const uint8_t*>(Target);
		for (uint32_t y = 0; y < Height; ++y)
			for (uint32_t x = 0; x < Width; ++x) {
				const bool inside =
				    x >= 8 && y >= 4 && (float(x) + .5f - 8) / 48 + (float(y) + .5f - 4) / 24 < 1;
				const float source[]     = {.25f, .5f, .75f, 1.f};
				const int   background[] = {10, 20, 30, 255};
				for (uint32_t c = 0; c < 4; ++c) {
					float expected = background[c];
					if (inside && c < 3) expected = .75f * source[c] * 255 + .25f * background[c];
					if (x == 16 && y == 8 && c < 3) expected = .5f * source[c] * 255;
					if (x == 60 && y == 30) expected = (0xff665544u >> (c * 8)) & 255;
					Check(std::abs(int(rgba[(y * Width + x) * 4 + c]) -
					               int(std::lround(expected))) <= 1,
					      "mixed GNM transfer/draw pixel mismatch");
				}
			}
		std::puts("PS4_GNM DMA fill -> draw -> WRITE_DATA -> draw all 2048 guest pixels PASS");
	}
	if (interpolation) {
		const auto* rgba    = reinterpret_cast<const uint8_t*>(Target);
		uint32_t    covered = 0;
		for (uint32_t y = 0; y < Height; ++y)
			for (uint32_t x = 0; x < Width; ++x) {
				const float u = (float(x) + .5f - 8) / 48, v = (float(y) + .5f - 4) / 24;
				const bool  inside = u >= 0 && v >= 0 && u + v < 1;
				covered += inside;
				float expected[] {inside ? 255 * u / (1 - v * .5f) : 10,
				                  inside ? 255 * v * .5f / (1 - v * .5f) : 20,
				                  inside ? 255 * .25f : 30, 255};
				if (inside && arithmetic) {
					expected[0] = 255 * std::clamp(1 - 2 * u / (1 - v * .5f), 0.f, 1.f);
					expected[1] = 255 * ((v * .5f / (1 - v * .5f)) * .5f + .5f) * .5f;
					expected[2] = std::max(255 * .375f, std::min(255 * .5f, expected[1]));
				}
				for (uint32_t c = 0; c < 4; ++c)
					Check(std::abs(int(rgba[(y * Width + x) * 4 + c]) -
					               int(std::lround(expected[c]))) <= 1,
					      "guest perspective interpolation pixel mismatch");
			}
		Check(covered == 576, "guest gradient coverage");
		std::puts("PS4_GNM perspective PARAM0 -> VINTRP all 2048 guest pixels PASS");
	}
	std::ofstream image(root / "gnm-guest.ppm", std::ios::binary);
	image << "P6\n64 32\n255\n";
	const auto* pixels = reinterpret_cast<const char*>(Target);
	for (size_t i = 0; i < Bytes; i += 4)
		image.write(pixels + i, 3);
	Check(bool(image), "save guest framebuffer");
	image.close();
	rt->UnloadProgram(program);
	rt->Clear();
	std::puts("PS4_GNM AMD64 ELF -> imported GNM -> PM4 -> GCN -> GPU -> guest framebuffer PASS");
}
int main(int argc, char** argv) {
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	Common::InitializeThreads();
	Common::Subsystems subsystems;
	subsystems.Initialize<Config::Lifecycle>();
	Config::ConfigOptions config;
	config.printf_direction = Config::LogDirection::Silent;
	config.guest_platform   = Common::GuestPlatform::PS4;
	Config::Load(config);
	subsystems.Initialize<Log::Lifecycle>();
	Libs::Graphics::ShaderInit();
	UnitTests();
	InterpolationUnitTests();
	TransferUnitTests();
	BufferUnitTests();
	IndexedUnitTests(false);
	IndexedUnitTests(true);
	TextureUnitTests();
	if (argc != 2 || std::string_view(argv[1]) != "--unit-only")
		RuntimeTest(subsystems,
		            argc >= 2 ? std::filesystem::path(argv[1])
		                      : std::filesystem::path("ps4-gnm-fixture"),
		            argc == 3 && (std::string_view(argv[2]) == "--interpolation" ||
		                          std::string_view(argv[2]) == "--alu"),
		            argc == 3 && std::string_view(argv[2]) == "--alu",
		            argc == 3 && std::string_view(argv[2]) == "--transfers",
		            argc == 3 && (std::string_view(argv[2]) == "--buffer" ||
		                          std::string_view(argv[2]) == "--indexed"),
		            argc == 3 && std::string_view(argv[2]) == "--indexed",
		            argc == 3 && std::string_view(argv[2]) == "--textures");
	subsystems.Destroy();
	return 0;
}
