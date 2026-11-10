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
	std::vector<std::vector<uint8_t>>                buffers3;
	std::vector<std::vector<uint8_t>>                buffers5;
	std::vector<std::vector<uint8_t>>                pixel_images2;
	std::vector<std::vector<uint8_t>>                pixel_images4;
	std::vector<std::array<std::vector<uint8_t>, 6>> resources;
	std::vector<std::vector<uint8_t>>                indices;
	std::vector<uint32_t>                            index_bytes;
	bool                                             fail = false;
	std::unique_ptr<Graphics::Ps4::GnmRenderer>       gpu;
	std::vector<std::array<uint8_t, 4>>              gpu_pixels;
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
		counts->indices.emplace_back(state.indices.begin(), state.indices.end());
		counts->index_bytes.push_back(state.index_bytes);
		counts->initial.push_back(state.initial_pixels[0]);
		counts->shaders.emplace_back(ps.begin(), ps.end());
		counts->vertex_shaders.emplace_back(vs.begin(), vs.end());
		const auto first  = state.pixel_buffer.empty() ? state.vertex_buffer : state.pixel_buffer;
		const auto second = state.pixel_buffer.empty() ? state.vertex_buffer2 : state.pixel_buffer2;
		counts->buffers.emplace_back(first.begin(), first.end());
		counts->buffers2.emplace_back(second.begin(), second.end());
		counts->buffers3.emplace_back(state.pixel_buffer3.begin(), state.pixel_buffer3.end());
		const auto fifth = state.pixel_buffer_tail[1];
		counts->buffers5.emplace_back(fifth.begin(), fifth.end());
		counts->pixel_images2.emplace_back(state.pixel_texture2.pixels.begin(),
		                                  state.pixel_texture2.pixels.end());
		const auto fourth = state.pixel_texture_tail.back().pixels;
		counts->pixel_images4.emplace_back(fourth.begin(), fourth.end());
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
		if (counts->gpu) {
			if (!counts->gpu->Render(state, vs, ps, output, error)) return false;
			const size_t pixel = (size_t(8) * state.width + 16) * 4;
			Check(pixel + 4 <= output.size(), "ordered SRT GPU output size");
			std::array<uint8_t, 4> rgba;
			std::copy_n(output.begin() + pixel, 4, rgba.begin());
			counts->gpu_pixels.push_back(rgba);
			return true;
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
	{
		const auto evidence = session->DrawEvidence();
		Check(evidence.size() == 2 && evidence[0].attempt == evidence[1].attempt &&
		          evidence[0].ordinal < evidence[1].ordinal &&
		          evidence[0].command_buffer == 0 && evidence[0].target == Target &&
		          evidence[0].vertex_words && evidence[0].pixel_words &&
		          evidence[0].host_completed && evidence[0].guest_published &&
		          !evidence[0].batch_rejected && evidence[1].guest_published,
		      "draw evidence separates preparation, host completion and publication");
		const auto submits = session->SubmissionEvidence();
		Check(submits.size() == 1 && submits[0].success && !submits[0].rejected &&
		          submits[0].phase == Ps4::GnmSubmitPhase::Completed,
		      "successful submission diagnostics");
	}
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
	{
		const auto evidence = session->DrawEvidence();
		Check(evidence.size() == 4 && evidence[2].batch_rejected &&
		          evidence[3].batch_rejected && !evidence[2].host_completed &&
		          !evidence[2].guest_published && !evidence[0].batch_rejected &&
		          evidence[0].guest_published,
		      "invalid tail is diagnosed without claiming GPU execution");
		const auto submits = session->SubmissionEvidence();
		Check(submits.back().rejected && !submits.back().success &&
		          submits.back().phase == Ps4::GnmSubmitPhase::Validation &&
		          submits.back().opcode == 0x99 && !submits.back().error.empty(),
		      "submission rejection location/phase diagnostics");
	}
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
	{
		const auto evidence = session->DrawEvidence();
		Check(evidence.size() >= 2 && evidence.back().batch_rejected &&
		          !evidence.back().host_completed && !evidence.back().guest_published,
		      "host failure must not claim completion/publication");
		Check(session->SubmissionEvidence().back().phase == Ps4::GnmSubmitPhase::Host &&
		          session->SubmissionEvidence().back().rejected,
		      "host failure phase diagnostics");
	}
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
	{
		constexpr uint64_t root = 0x418000, child = 0x420000;
		memory.Put(child, commands);
		const auto submit_ib = [&](const std::vector<uint32_t>& stream) {
			memory.Put(root, stream);
			memory.Put(Arrays, std::vector<uint64_t> {root});
			memory.Put(Sizes, std::vector<uint32_t> {uint32_t(stream.size() * 4)});
			return session->Submit(1, Arrays, Sizes, 0, 0);
		};
		const std::vector<uint32_t> ib {0xc0023f00u, uint32_t(child), 0, uint32_t(commands.size())};
		auto calls = counts->calls;
		Check(!submit_ib(ib) && counts->calls == calls + 2, "nested graphics IB ordered draws");
		Check(session->DrawEvidence().back().command_address == child &&
		          session->DrawEvidence().back().guest_published,
		      "IB draw diagnostics must identify the nested stream");
		auto chain = ib;
		chain[3] |= 0x100000;
		chain.insert(chain.end(), {0xc0009900, 0}); // unreachable tail replaced by chain
		calls = counts->calls;
		Check(!submit_ib(chain) && counts->calls == calls + 2, "IB chain did not replace parent tail");
		const auto reject_ib = [&](const std::vector<uint32_t>& stream) {
			memory.Put(root, stream);
			memory.Put(Arrays, std::vector<uint64_t> {root});
			memory.Put(Sizes, std::vector<uint32_t> {uint32_t(stream.size() * 4)});
			const auto saved = memory.data;
			const auto old_calls = counts->calls;
			Check(session->Submit(1, Arrays, Sizes, 0, 0) != 0 && memory.data == saved &&
			          counts->calls == old_calls && !memory.pinned,
			      "invalid IB had host/guest effects");
		};
		for (auto bad: std::vector<std::vector<uint32_t>> {
		         {0xc0023f00u, 0, 0, 4}, {0xc0023f00u, uint32_t(child) + 1, 0, 4},
		         {0xc0023f00u, uint32_t(child), 256, 4},
		         {0xc0023f00u, uint32_t(child), 0, 0},
		         {0xc0023f00u, uint32_t(child), 0, 0x1000004},
		         {0xc0023f00u, uint32_t(root), 0, 4},
		         {0xc0023f00u, uint32_t(child), 0, 0xfffff}})
			reject_ib(bad);
		// A valid nested draw followed by an invalid parent packet remains
		// metadata-only: no partial host execution before complete validation.
		auto tail = ib;
		tail.insert(tail.end(), {0xc0009900, 0});
		reject_ib(tail);
		memory.Put(Arrays, std::vector<uint64_t> {Commands});
	}
	// Retain only the newest 256 rows; no shader/payload bytes are retained.
	// All submissions here use the recording renderer, not real GPU evidence.
	memory.Put(Commands, commands);
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(commands.size() * 4)});
	for (uint32_t i = 0; i < 130; ++i)
		Check(!session->Submit(1, Arrays, Sizes, 0, 0), session->LastError());
	const auto bounded = session->DrawEvidence();
	Check(bounded.size() == 256 && bounded.front().ordinal > 1 &&
	          bounded.back().ordinal - bounded.front().ordinal == 255 &&
	          bounded.back().guest_published && !bounded.back().batch_rejected,
	      "bounded draw evidence retirement/order");
	Check(session->SubmissionEvidence().size() <= 256 &&
	          session->SubmissionEvidence().back().success,
	      "bounded submission diagnostics");
	Check(session->Draw(Commands, 7, 3, 1) != 0 &&
	          !session->DrawEvidence().back().batch_rejected,
	      "builder failure must not relabel a committed submit");
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

	// Default attributes require no mapped PARAM read or barycentric input.
	for (uint32_t selector = 0; selector < 4; ++selector) {
		std::vector<uint32_t> defaults;
		Reg(defaults, 0x69, 0x1b3, {0, 0});
		const uint32_t control = 0x20u | (selector << 8);
		Reg(defaults, 0x69, 0x191, {control, control, control, control});
		defaults.insert(defaults.end(), draw.begin(), draw.end());
		memory.Put(Commands, defaults);
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(defaults.size() * 4)});
		Check(!session->Submit(1, Arrays, Sizes, 0, 0), session->LastError());
	}
	memory.Put(Ps, Graphics::ShaderRecompiler::Gcn::FlatPixelFixture());
	std::vector<uint32_t> flat;
	Reg(flat, 0x69, 0x191, {0x400});
	flat.insert(flat.end(), draw.begin(), draw.end());
	memory.Put(Commands, flat);
	memory.Put(Sizes, std::vector<uint32_t> {uint32_t(flat.size() * 4)});
	Check(!session->Submit(1, Arrays, Sizes, 0, 0), session->LastError());
	for (uint32_t mode = 0; mode < 15; ++mode) {
		const auto masked = mode < 3 ? Graphics::ShaderRecompiler::Gcn::MaskedPixelFixture(mode == 1, mode == 2)
		    : mode < 8 ? Graphics::ShaderRecompiler::Gcn::ElsePixelFixture(mode != 4, mode == 7, mode == 5 ? 0 : mode == 6 ? 15 : 1)
		    : mode < 10 ? Graphics::ShaderRecompiler::Gcn::IntegerComparePixelFixture(mode == 9)
		                : Graphics::ShaderRecompiler::Gcn::CmpxPixelFixture(mode - 10);
		memory.Put(Ps, masked);
		std::vector<uint32_t> mask_state;
		Reg(mask_state, 0x69, 0x1b3, {2, 2});
		Reg(mask_state, 0x69, 0x191, {0, 0x20, 0x20, 0x20});
		Reg(mask_state, 0x76, 0xa, {0xc0083}); // sixteen VGPRs and24 SGPRs
		mask_state.insert(mask_state.end(), draw.begin(), draw.end());
		memory.Put(Commands, mask_state);
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(mask_state.size() * 4)});
		Check(!session->Submit(1, Arrays, Sizes, 0, 0), session->LastError());
		if (mode != 2 && mode != 8 && mode != 9) {
			auto insufficient = mask_state;
			insufficient.resize(insufficient.size() - draw.size());
			Reg(insufficient, 0x76, 0xa, {0xc0043}); // only16 SGPRs; saved pair starts at16
			insufficient.insert(insufficient.end(), draw.begin(), draw.end());
			memory.Put(Commands, insufficient);
			memory.Put(Sizes, std::vector<uint32_t> {uint32_t(insufficient.size() * 4)});
			reject();
		}
	}
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
static void BufferUnitTests(bool gpu = false) {
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
	auto raw_code = Graphics::ShaderRecompiler::Gcn::BufferPixelFixture(4);
	raw_code[0] &= ~(8u << 22); // S_LOAD_DWORDX4, with a two-word address.
	raw_code.insert(raw_code.begin(), 0xbf820000u); // Enter the body before loading.
	memory.Put(Ps, raw_code);
	Check(!submit(commands), session->LastError());
	Check(counts->buffers.back().size() == 16 && counts->buffers.back()[0] == 0x22,
	      "body S_LOAD did not bind the exact guest range");
	auto unreadable = commands;
	unreadable[descriptor_at] = 0x600000;
	const auto calls = counts->calls;
	Check(submit(unreadable) != 0 && counts->calls == calls,
	      "body S_LOAD accepted unreadable guest memory");
	raw_code[0] = 0xbe880391u; // s8 = 17, aligned down to byte 16 by SMRD.
	raw_code[1] = 0xc0820008u;
	memory.Put(Ps, raw_code);
	auto dynamic = commands;
	const uint32_t buffer_end = uint32_t(Base + memory.data.size());
	dynamic[descriptor_at] = buffer_end - 32;
	memory.Put(buffer_end - 32, std::vector<uint32_t>(8, 0x33333333));
	// Allocate s8 in the PS program resource register.
	auto rsrc1 = std::find(dynamic.begin(), dynamic.end(), 0xc0000u);
	Check(rsrc1 != dynamic.end(), "PS resource register fixture");
	*rsrc1 |= 0x40;
	Check(!submit(dynamic), session->LastError());
	Check(counts->buffers.back().size() == 32 && counts->buffers.back()[16] == 0x33,
	      "SGPR S_LOAD did not stop at the mapping boundary");
	memory.Put(Ps, std::vector<uint32_t> {
	    0xc2080100u, 0xc2088500u, 0xc2090900u, 0xbf8c007fu,
	    0x7e000210u, 0x7e020211u, 0x7e040212u, 0x7e0602f2u,
	    0xf800180fu, 0x03020100u, 0xbf810000u});
	auto three = commands;
	three.resize(three.size() - draw.size());
	Reg(three, 0x76, 0xa, {0xc0080, 24});
	Reg(three, 0x76, 0xc, {0x404b80, 0, 16, 0xfac, 0x404b90, 0, 16, 0xfac,
	                       0x404ba0, 0, 16, 0xfac});
	const size_t third_base = three.size() - 4;
	three.insert(three.end(), draw.begin(), draw.end());
	memory.Put(0x404ba0, std::vector<uint32_t>(4, 0x44444444));
	Check(!submit(three), session->LastError());
	Check(counts->buffers3.back().size() == 16 && counts->buffers3.back()[0] == 0x44,
	      "third buffer missing from GNM draw snapshot");
	three[third_base] = uint32_t(Target);
	const auto calls_before_alias = counts->calls;
	Check(submit(three) != 0 && counts->calls == calls_before_alias &&
	          session->LastError().find("aliases a draw target") != std::string::npos,
	      "GNM accepted a third buffer aliasing the draw target");
	memory.Put(Ps, std::vector<uint32_t> {
	    0xc0040002u, 0xbf8c007fu, 0xc0880008u, 0xbf8c007fu,
	    0x7e000280u, 0xe0382000u, 0x80040100u, 0xbf8c0f70u,
	    0xf800180fu, 0x04030201u, 0xbf810000u});
	auto table_draw = commands;
	table_draw.resize(table_draw.size() - draw.size());
	Reg(table_draw, 0x76, 0xa, {0xc0081, 6});
	Reg(table_draw, 0x76, 0xc, {buffer_end - 80, 0, 0});
	table_draw.insert(table_draw.end(), draw.begin(), draw.end());
	memory.Put(buffer_end - 80, std::vector<uint32_t> {
	    16, 0, 0, 0, 0x404b80, 16u << 16, 1, 0xfac, 0x404ba0, 16u << 16, 1, 0xfac,
	    0x404bb0, 16u << 16, 1, 0xfac, 0x404bc0, 16u << 16, 1, 0xfac});
	memory.Put(0x404bc0, std::vector<uint32_t>(4, 0x67676767));
	Check(!submit(table_draw), session->LastError());
	const auto table_shader = counts->shaders.back();
	Check(counts->buffers.back().size() == 80 && counts->buffers2.back()[0] == 0x22 &&
	          counts->buffers3.back()[0] == 0x44 && counts->buffers5.back().size() == 16 &&
	          counts->buffers5.back()[0] == 0x67, "dynamic SRT snapshots missing");
	memory.Put(buffer_end - 80, std::vector<uint32_t> {64});
	Check(!submit(table_draw), session->LastError());
	Check(counts->buffers.back()[0] == 64 && counts->shaders.back() == table_shader,
	      "SRT selector update must reach the GPU without changing the shader");
	auto ordered = table_draw;
	Write(ordered, buffer_end - 80, 16);
	Fill(ordered, buffer_end - 80, 4, 32);
	ordered.insert(ordered.end(), draw.begin(), draw.end());
	const auto first_ordered = counts->buffers.size();
	if (gpu) {
		memory.Put(0x404bc0, std::array<float, 4> {1, 0, 0, 1});
		memory.Put(0x404ba0, std::array<float, 4> {0, 1, 0, 1});
		counts->gpu = Graphics::Ps4::CreateGnmRenderer(error);
		Check(bool(counts->gpu), error);
	}
	Check(!submit(ordered), session->LastError());
	Check(counts->buffers[first_ordered][0] == 64 &&
	          counts->buffers[first_ordered + 1][0] == 32 &&
	          counts->shaders[first_ordered] == counts->shaders[first_ordered + 1] &&
	          memory.Words(buffer_end - 80, 1)[0] == 32,
	      "ordered PM4 table writes were not visible to the second draw");
	if (gpu) {
		Check(counts->gpu_pixels == std::vector<std::array<uint8_t, 4>> {
		          {255, 0, 0, 255}, {0, 255, 0, 255}}, "ordered SRT draw GPU colors");
		std::printf("PS4_GNM ordered SRT WRITE_DATA/DMA_DATA two draws GPU PASS (%s)\n",
		            counts->gpu->Name());
		counts->gpu.reset();
	}
	auto invalid_ordered = ordered;
	invalid_ordered.insert(invalid_ordered.end(), {0xc0009900u, 0});
	memory.Put(buffer_end - 80, std::vector<uint32_t> {64});
	const auto calls_before_bad_table = counts->calls;
	Check(submit(invalid_ordered) != 0 && counts->calls == calls_before_bad_table &&
	          memory.Words(buffer_end - 80, 1)[0] == 64,
	      "invalid batch published a staged table write");
	counts->fail = true;
	Check(submit(ordered) != 0 && memory.Words(buffer_end - 80, 1)[0] == 64,
	      "failed host draw published a staged table write");
	counts->fail = false;
	if (gpu) {
		const uint32_t table_address = buffer_end - 48;
		memory.Put(table_address, std::vector<uint32_t> {16, 0, 0, 0,
		    0x404bc0, 16u << 16, 1, 0xfac, 0x404ba0, 16u << 16, 1, 0xfac});
		memory.Put(Ps, std::vector<uint32_t> {
		    0x7e000280u, 0x7e0a02a0u, 0xe0702000u, 0x80010500u, 0xbf8c0f70u,
		    0xc0040002u, 0xbf8c007fu, 0xc0880008u, 0xbf8c007fu,
		    0xe0382000u, 0x80040100u, 0xbf8c0f70u, 0xf800180fu, 0x04030201u, 0xbf810000u});
		auto store_draw = commands;
		store_draw.resize(store_draw.size() - draw.size());
		Reg(store_draw, 0x69, 0x90, {0x80080010u, 0x00090011u});
		Reg(store_draw, 0x76, 0xa, {0xc0081, 16});
		Reg(store_draw, 0x76, 0xc, {table_address, 0, 0, 0, table_address, 4u << 16, 12, 0xfac});
		store_draw.insert(store_draw.end(), draw.begin(), draw.end());
		counts->fail = true;
		Check(submit(store_draw) != 0 && memory.Words(table_address, 1)[0] == 16,
		      "failed raster published a GCN store");
		counts->fail = false;
		counts->gpu = Graphics::Ps4::CreateGnmRenderer(error);
		Check(bool(counts->gpu), error);
		counts->gpu_pixels.clear();
		Check(!submit(store_draw), session->LastError());
		Check(counts->gpu_pixels == std::vector<std::array<uint8_t, 4>> {{0, 255, 0, 255}} &&
		          memory.Words(table_address, 1)[0] == 32, "GCN store SRT color or guest publication");
		std::printf("PS4_GNM GCN store -> SRT -> MUBUF and guest publication PASS (%s)\n",
		            counts->gpu->Name());
		counts->gpu.reset();
	}
	memory.Put(Ps, std::vector<uint32_t> {
	    0x7e140280u, 0x7e160280u, 0xf0001f00u, 0x0000040au, 0x7e140281u,
	    0xf0001f00u, 0x0002160au, 0xbf8c0f70u, 0xf800180fu, 0x07061704u, 0xbf810000u});
	auto image_draw = commands;
	image_draw.resize(image_draw.size() - draw.size());
	Reg(image_draw, 0x76, 0xa, {0xc0046, 32});
	Reg(image_draw, 0x76, 0xc, {0x4100, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0,
	                           0x4110, 10u << 20, 1, 0x90800facu, 63u << 13, 0, 0, 0});
	const size_t second_image_base = image_draw.size() - 8;
	image_draw.insert(image_draw.end(), draw.begin(), draw.end());
	memory.Put(0x411000, std::vector<uint8_t>(256, 0x56));
	Check(!submit(image_draw), session->LastError());
	Check(counts->pixel_images2.back().size() == 256 && counts->pixel_images2.back()[0] == 0x56,
	      "second image missing from GNM draw snapshot");
	image_draw[second_image_base] = uint32_t(Target >> 8);
	const auto calls_before_image_alias = counts->calls;
	Check(submit(image_draw) != 0 && counts->calls == calls_before_image_alias &&
	          session->LastError().find("aliases a draw target") != std::string::npos,
	      "GNM accepted a second image aliasing the draw target");
	memory.Put(Ps, std::vector<uint32_t> {
	    0xc0040002u, 0xbf8c007fu, 0xc0c80008u, 0xbf8c007fu,
	    0x7e140280u, 0x7e160280u, 0xf0001f00u, 0x0004040au,
	    0xbf8c0f70u, 0xf800180fu, 0x07060504u, 0xbf810000u});
	auto image_srt = commands;
	image_srt.resize(image_srt.size() - draw.size());
	Reg(image_srt, 0x76, 0xa, {0xc0086, 6});
	Reg(image_srt, 0x76, 0xc, {buffer_end - 144, 0, 0});
	image_srt.insert(image_srt.end(), draw.begin(), draw.end());
	auto image_table = std::vector<uint32_t> {16, 0, 0, 0,
	    0x4100, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0,
	    0x4110, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0,
	    0x4120, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0,
	    0x4130, 10u << 20, 0, 0x90800facu, 63u << 13, 0, 0, 0};
	memory.Put(buffer_end - 144, image_table);
	memory.Put(0x413000, std::vector<uint8_t>(256, 0x78));
	Check(!submit(image_srt), session->LastError());
	const auto image_shader = counts->shaders.back();
	Check(counts->buffers.back().size() == 144 && counts->pixel_images2.back().size() == 256 &&
	          counts->pixel_images2.back()[0] == 0x56 && counts->pixel_images4.back().size() == 256 &&
	          counts->pixel_images4.back()[0] == 0x78, "GNM image SRT snapshots");
	memory.Put(buffer_end - 144, std::vector<uint32_t> {112});
	Check(!submit(image_srt), session->LastError());
	Check(counts->buffers.back()[0] == 112 && counts->shaders.back() == image_shader,
	      "GNM image SRT selector refresh");
	image_table[28] = uint32_t(Target >> 8);
	memory.Put(buffer_end - 144, image_table);
	const auto calls_before_fourth_alias = counts->calls;
	Check(submit(image_srt) != 0 && counts->calls == calls_before_fourth_alias &&
	          session->LastError().find("aliases a draw target") != std::string::npos,
	      "GNM accepted a fourth image aliasing the draw target");
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
static void ExternalFetchUnitTests() {
	Memory memory;
	std::string error;
	auto counts = std::make_shared<Counts>();
	constexpr uint64_t table_address = 0x414000, fetch_address = 0x415000;
	const auto fetch = Graphics::ShaderRecompiler::Gcn::ExternalFetchFunctionFixture();
	memory.Put(Vs, Graphics::ShaderRecompiler::Gcn::ExternalFetchVertexFixture());
	memory.Put(Ps, Graphics::ShaderRecompiler::Gcn::InterpolatedPixelFixture());
	memory.Put(fetch_address, fetch);
	memory.Put(table_address, std::vector<uint32_t> {0x410000, 16u << 16, 4, 0xfac,
	                                                0x410100, 16u << 16, 4, 0xfac});
	memory.Put(VsRegs, std::vector<uint32_t> {uint32_t(Vs >> 8), 0, 0xc0042, 12, 0, 4, 0});
	memory.Put(PsRegs, std::vector<uint32_t> {uint32_t(Ps >> 8), 0, 0xc0001, 0, 0, 9, 2, 2, 1, 0, 0, 15});
	const auto update = [&](bool changed) {
		const auto data = Ps4TextureTests::Make(0, 0, changed);
		memory.Put(0x410000, data.positions);
		memory.Put(0x410100, data.colors);
	};
	update(false);
	auto session = Ps4::GnmSession::Create(memory.Access(), error,
	    std::make_unique<RecordingRenderer>(memory, counts));
	Check(bool(session), error);
	Check(!session->SetShader(true, Commands, 29, VsRegs) &&
	          !session->SetShader(false, Commands + 29 * 4, 40, PsRegs), "external fetch builders");
	auto commands = memory.Words(Commands, 69);
	const auto state = StateWords(Target, 0, true);
	commands.insert(commands.end(), state.begin(), state.end());
	Reg(commands, 0x76, 0x4c, {0, 0, uint32_t(table_address), 0, uint32_t(fetch_address), 0});
	Check(!session->Draw(Commands + commands.size() * 4, 7, 3, 0), "external fetch draw builder");
	const auto draw = memory.Words(Commands + commands.size() * 4, 7);
	commands.insert(commands.end(), draw.begin(), draw.end());
	const auto submit = [&](const std::vector<uint32_t>& words) {
		memory.Put(Commands, words);
		memory.Put(Arrays, std::vector<uint64_t> {Commands});
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(words.size() * 4)});
		return session->Submit(1, Arrays, Sizes, 0, 0);
	};
	Check(!submit(commands), session->LastError());
	update(true);
	Check(!submit(commands), session->LastError());
	Check(counts->calls == 2 && counts->vertex_shaders[0] == counts->vertex_shaders[1] &&
	          counts->resources[0][2] != counts->resources[1][2] &&
	          counts->resources[0][3] != counts->resources[1][3],
	      "external fetch updates must reach both GPU resource snapshots without changing code");
	const auto reject = [&](const std::vector<uint32_t>& words) {
		memory.Put(Commands, words);
		memory.Put(Arrays, std::vector<uint64_t> {Commands});
		memory.Put(Sizes, std::vector<uint32_t> {uint32_t(words.size() * 4)});
		const auto saved = memory.data;
		const auto calls = counts->calls;
		Check(session->Submit(1, Arrays, Sizes, 0, 0) != 0 && memory.data == saved &&
		          counts->calls == calls && !memory.pinned, "invalid external fetch had effects");
	};
	for (uint32_t width: {2u, 4u}) {
		constexpr uint64_t index_address = 0x414100;
		if (width == 2) memory.Put(index_address, std::vector<uint16_t> {1, 3, 2});
		else memory.Put(index_address, std::vector<uint32_t> {1, 3, 2});
		auto indexed = commands;
		indexed.resize(indexed.size() - draw.size());
		indexed.insert(indexed.end(), {0xc0002a00u, width == 4 ? 1u : 0u});
		Check(!session->DrawIndexed(Commands + indexed.size() * 4, 10, 3, index_address, 0, 0),
		      "indexed draw builder");
		const auto packet = memory.Words(Commands + indexed.size() * 4, 10);
		indexed.insert(indexed.end(), packet.begin(), packet.end());
		Check(!submit(indexed) && counts->indices.back().size() == width * 3 &&
		          counts->index_bytes.back() == width, session->LastError());
		auto bad_indices = indexed;
		bad_indices[bad_indices.size() - 10 + 2] = uint32_t(Target);
		reject(bad_indices);
		bad_indices = indexed;
		bad_indices[bad_indices.size() - 10 + 2] = 0x600000;
		reject(bad_indices);
		bad_indices = indexed;
		bad_indices[bad_indices.size() - 10 + 2] = uint32_t(index_address + 1);
		reject(bad_indices);
		bad_indices = indexed;
		bad_indices[bad_indices.size() - 10 + 1] = 2; // max_size smaller than draw count
		reject(bad_indices);
		const auto saved = memory.data;
		Check(session->DrawIndexed(Commands, 10, 3, index_address, 1, 0) != 0 && memory.data == saved,
		      "unsupported indexed builder had effects");
	}
	auto bad_fetch = fetch;
	bad_fetch.back() = 0xbe802002; // wrong return link pair
	memory.Put(fetch_address, bad_fetch);
	reject(commands);
	memory.Put(fetch_address, fetch);
	auto bad = commands;
	bad.resize(bad.size() - draw.size());
	Reg(bad, 0x76, 0x4a, {0xc0002}); // eight SGPRs cannot hold descriptors s8..15
	bad.insert(bad.end(), draw.begin(), draw.end());
	reject(bad);
	memory.Put(Target, fetch);
	bad = commands;
	bad.resize(bad.size() - draw.size());
	Reg(bad, 0x76, 0x50, {uint32_t(Target), 0});
	bad.insert(bad.end(), draw.begin(), draw.end());
	reject(bad);
	Check(session->LastError().find("scalar table aliases") != std::string::npos,
	      "external fetch code/target alias was not rejected");
	session->Finish();
	std::puts("PS4_GNM external typed fetch -> two GPU buffer snapshots, updates/link/alias/RSRC rejects PASS");
}
static void TextureUnitTests(bool sampled = false, bool indirect = false) {
	Memory      memory;
	std::string error;
	auto        counts = std::make_shared<Counts>();
	memory.Put(Vs, indirect ? Graphics::ShaderRecompiler::Gcn::TableSampledTextureFixture(
	                            Graphics::ShaderRecompiler::Gcn::Stage::Vertex)
	                       : sampled ? Graphics::ShaderRecompiler::Gcn::SampledTextureFixture(
	                            Graphics::ShaderRecompiler::Gcn::Stage::Vertex)
	                       : Graphics::ShaderRecompiler::Gcn::TextureVertexFixture());
	memory.Put(Ps, indirect ? Graphics::ShaderRecompiler::Gcn::TableSampledTextureFixture(
	                            Graphics::ShaderRecompiler::Gcn::Stage::Pixel)
	                       : sampled ? Graphics::ShaderRecompiler::Gcn::SampledTextureFixture(
	                            Graphics::ShaderRecompiler::Gcn::Stage::Pixel)
	                       : Graphics::ShaderRecompiler::Gcn::TexturePixelFixture());
	memory.Put(VsRegs, std::vector<uint32_t> {uint32_t(Vs >> 8), 0,
	                                         indirect ? 0xc0143u : sampled ? 0xc0083u : 0xc0043u, indirect ? 4u : 32u, 0, 4, 0});
	memory.Put(PsRegs,
	           std::vector<uint32_t> {uint32_t(Ps >> 8), 0, indirect ? 0xc0143u : 0xc0084u,
	                                    indirect ? 4u : 32u, 0, 9, 2, 2, 1, 0, 0, 15});
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
	std::array<std::vector<uint32_t>, 2> tables;
	const auto add_users = [&](uint32_t reg, bool vertex) {
		std::vector<uint32_t> users =
		    vertex ? std::vector<uint32_t> {0x410000, 16u << 16, 4, 0xfac,
		                                    0x410040, 16u << 16, 4, 0xfac}
		           : std::vector<uint32_t> {0x410100, 0, 16, 0xfac, 0x410200, 16u << 16, 4, 0xfac};
		if (sampled) users = {vertex ? 0x410000u : 0x410200u, 0, 64, 0xfac, 0x92, 0, 0, 0};
		auto image = Ps4TextureTests::ImageWords(vertex ? 0x410800 : 0x411000);
		users.insert(users.end(), image.begin(), image.end());
		if (indirect) {
			users = vertex ? std::vector<uint32_t> {0x410000, 0, 64, 0xfac, 0x410040, 0, 64, 0xfac}
			               : std::vector<uint32_t> {0x410100, 0, 16, 0xfac, 0x410200, 0, 64, 0xfac};
			users.insert(users.end(), image.begin(), image.end());
			users.insert(users.end(), {0x92, 0, 0, 0});
			tables[vertex ? 1 : 0] = users;
			memory.Put(vertex ? 0x412000 : 0x413000, users);
			users = {vertex ? 0x412000u : 0x413000u, 0};
		}
		commands.push_back(0xc0007600u | (uint32_t(users.size()) << 16));
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
	for (size_t resource = 0; resource < 6; ++resource) {
		if (sampled && !indirect && (resource == 1 || resource == 3)) {
			Check(counts->resources[0][resource].empty(), "sampler treated as buffer payload");
			continue;
		}
		Check(!counts->resources[0][resource].empty() &&
		          counts->resources[0][resource] != counts->resources[1][resource],
		      "independent resource snapshot missing/update lost");
	}
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
	if (indirect) {
		for (size_t at: {vs_at, ps_at}) {
			auto bad = commands;
			bad[at] = 0x600000; // absent complete table mapping
			reject(bad);
		}
		memory.Put(Target, tables[1]);
		auto bad = commands;
		bad[vs_at] = uint32_t(Target);
		reject(bad);
		Check(session->LastError().find("scalar table aliases") != std::string::npos,
		      "scalar table/target alias must be rejected before renderer");
		session->Finish();
		std::puts("PS4_GNM S_LOAD table -> two buffers + texture + sampler VS/PS snapshots PASS");
		return;
	}
	if (sampled) {
		for (size_t base: {vs_at, ps_at})
			for (size_t n = 4; n < 8; ++n) {
				auto bad = commands;
				bad[base + n] ^= 1;
				reject(bad);
			}
		auto bad = commands;
		bad.resize(bad.size() - draw.size());
		Reg(bad, 0x76, 0x4a, {0xc0043}); // sixteen SGPRs cannot contain s16..19
		bad.insert(bad.end(), draw.begin(), draw.end());
		reject(bad);
	}
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
	const bool srt_gpu = argc == 2 && std::string_view(argv[1]) == "--srt-write-gpu";
	BufferUnitTests(srt_gpu);
	IndexedUnitTests(false);
	IndexedUnitTests(true);
	ExternalFetchUnitTests();
	TextureUnitTests();
	TextureUnitTests(true);
	TextureUnitTests(true, true);
	if (!srt_gpu && (argc != 2 || std::string_view(argv[1]) != "--unit-only"))
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
