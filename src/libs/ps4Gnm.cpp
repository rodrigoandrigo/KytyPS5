#include "libs/ps4Gnm.h"

#include "graphics/guest_gpu/ps4Transfer.h"
#include "graphics/shader/recompiler/GcnCompiler.h"

#include <algorithm>
#include <array>
#include <bit>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <future>
#include <map>
#include <thread>

namespace Libs::Ps4 {
namespace ShaderRecompiler = Graphics::ShaderRecompiler;
namespace {
constexpr int32_t         Invalid  = int32_t(0x80d11000u);
constexpr size_t          MaxBytes = 4 * 1024 * 1024;
std::mutex                registry_mutex;
std::weak_ptr<GnmSession> current;
struct Registers {
	std::map<uint32_t, uint32_t> values;
	bool                         instances = false;
	uint32_t                     index_bytes = 0;
};
struct Target {
	uint64_t             address;
	uint32_t             width, height;
	std::vector<uint8_t> pixels;
};
struct Operation {
	uint64_t evidence_ordinal = 0, command_address = 0;
	uint32_t command_dword = 0, command_opcode = 0;
	std::vector<ShaderRecompiler::Gcn::ScalarRead> scalar_reads;
	uint64_t index_address = 0;
	std::vector<uint8_t> indices;
	struct BufferSnapshot {
		uint64_t address = 0;
		std::vector<uint8_t> bytes;
		bool writable = false;
	};
	std::array<BufferSnapshot, Graphics::Ps4::BuffersPerStage> pixel_buffers, vertex_buffers;
	struct ImageSnapshot {
		ShaderRecompiler::Gcn::ImageDescriptor descriptor;
		std::vector<uint8_t> pixels;
	};
	std::array<ImageSnapshot, Graphics::Ps4::ImagesPerStage> pixel_images, vertex_images;
	uint64_t                               target;
	Graphics::Ps4::RasterState             raster;
	std::vector<uint32_t>                  vertex, pixel;
	Graphics::Ps4::TransferPlan            transfer;
	bool resource_transfer = false;
	bool observed_by_draw = false;
};
bool Read(const GnmMemory& memory, uint64_t address, std::span<uint8_t> bytes) {
	return address && bytes.size() <= MaxBytes && address <= UINT64_MAX - bytes.size() &&
	       memory.read(address, bytes);
}
template <typename T>
bool ReadObject(const GnmMemory& memory, uint64_t address, T& object) {
	return Read(memory, address, {reinterpret_cast<uint8_t*>(&object), sizeof(T)});
}
bool Writable(const GnmMemory& memory, uint64_t address, size_t size) {
	return address && size <= MaxBytes && address <= UINT64_MAX - size &&
	       memory.writable(address, size);
}
bool WriteWords(const GnmMemory& memory, uint64_t address, const std::vector<uint32_t>& words) {
	return Writable(memory, address, words.size() * 4) &&
	       memory.write(address,
	                    {reinterpret_cast<const uint8_t*>(words.data()), words.size() * 4});
}
void Packet(std::vector<uint32_t>& out, uint32_t op, uint32_t reg,
            std::initializer_list<uint32_t> values) {
	out.push_back(0xc0000000u | (uint32_t(values.size()) << 16) | (op << 8));
	out.push_back(reg);
	out.insert(out.end(), values);
}
void NopTo(std::vector<uint32_t>& out, size_t size) {
	const auto count = size - out.size();
	out.push_back(0xc0001000u | (uint32_t(count - 2) << 16));
	out.resize(size, 0);
}
bool RegisterLimits(std::span<const uint32_t> code, std::span<const uint32_t> users, bool vertex,
                    uint32_t rsrc1, std::string& error, uint32_t parameter_count) {
	std::vector<uint32_t> normalized;
	for (uint32_t i = 0; i < users.size(); ++i) {
		normalized.push_back(0xbe8003ffu | i << 16);
		normalized.push_back(users[i]);
	}
	normalized.insert(normalized.end(), code.begin(), code.end());
	ShaderRecompiler::Decoder::Program program;
	if (!ShaderRecompiler::Gcn::Decode(normalized,
	                                   vertex ? ShaderRecompiler::Gcn::Stage::Vertex
	                                          : ShaderRecompiler::Gcn::Stage::Pixel,
	                                   program, error, parameter_count))
		return false;
	const uint32_t vectors = ((rsrc1 & 63) + 1) * 4, scalars = (((rsrc1 >> 6) & 15) + 1) * 8;
	for (const auto& inst: program.instructions) {
		if (inst.family == ShaderRecompiler::Decoder::Family::MIMG &&
		    (inst.dst.reg + 4 > vectors || inst.src0.reg + inst.image_address_components > vectors ||
		     inst.src1.reg + 8 > scalars ||
		     (inst.opcode == ShaderRecompiler::Decoder::Opcode::IMAGE_SAMPLE &&
		      inst.src2.reg + 4 > scalars))) {
			error = "IMAGE_LOAD register group exceeds declared allocation";
			return false;
		}
		if ((inst.family == ShaderRecompiler::Decoder::Family::MUBUF ||
		     inst.family == ShaderRecompiler::Decoder::Family::MTBUF) &&
		    (inst.dst.reg + inst.data_dwords > vectors || inst.src1.reg + 4 > scalars)) {
			error = "MUBUF register group exceeds declared allocation";
			return false;
		}
		if (inst.family == ShaderRecompiler::Decoder::Family::SMEM &&
		    (inst.dst.reg + inst.data_dwords > scalars ||
		     inst.src0.reg + (inst.opcode_id <= 4 ? 2u : 4u) > scalars)) {
			error = "SMRD descriptor/destination group exceeds declared SGPR allocation";
			return false;
		}
		const ShaderRecompiler::Decoder::Operand operands[] {inst.dst, inst.src0, inst.src1,
		                                                     inst.src2, inst.src3};
		for (const auto& op: operands) {
			const bool paired =
			    (inst.data_dwords == 2 &&
			     (inst.family == ShaderRecompiler::Decoder::Family::SOP2 ||
			      inst.family == ShaderRecompiler::Decoder::Family::SOP1 ||
			      inst.family == ShaderRecompiler::Decoder::Family::VOPC)) ||
			    (inst.family == ShaderRecompiler::Decoder::Family::VOPC &&
			     (op.kind == ShaderRecompiler::Decoder::OperandKind::Sgpr && &op == &operands[0])) ||
			    (inst.family == ShaderRecompiler::Decoder::Family::VOPC &&
			     inst.data_bits == 64 && &op == &operands[1]);
			if (paired &&
			    ((op.kind == ShaderRecompiler::Decoder::OperandKind::Sgpr && op.reg + 2 > scalars) ||
			     (op.kind == ShaderRecompiler::Decoder::OperandKind::Vgpr && op.reg + 2 > vectors))) {
				error = "B64 register pair exceeds declared allocation";
				return false;
			}
			if ((op.kind == ShaderRecompiler::Decoder::OperandKind::Vgpr && op.reg >= vectors) ||
			    (op.kind == ShaderRecompiler::Decoder::OperandKind::Sgpr && op.reg >= scalars)) {
				error = "GCN register use exceeds declared RSRC1 allocation";
				return false;
			}
		}
	}
	return true;
}
bool KnownRegister(uint32_t reg, uint32_t value) {
	if ((reg >= 0x2c08 && reg <= 0x2c1b) || (reg >= 0x2c48 && reg <= 0x2c5b)) return true;
	if (reg >= 0xa318 && reg <= 0xa31d) return true;
	if (reg >= 0xa31e && reg <= 0xa326) return value == 0 || reg == 0xa323 || reg == 0xa324;
	if (reg >= 0xa10f && reg <= 0xa114) return true;
	if (reg >= 0xa191 && reg < 0xa195) return true;
	switch (reg) {
		case 0xa00c:
		case 0xa00d:
		case 0xa080:
		case 0xa081:
		case 0xa082:
		case 0xa08e:
		case 0xa08f:
		case 0xa090:
		case 0xa091:
		case 0xa0b4:
		case 0xa0b5:
		case 0xa1b1:
		case 0xa1b3:
		case 0xa1b4:
		case 0xa1b6:
		case 0xa1b8:
		case 0xa1c3:
		case 0xa1c4:
		case 0xa1c5:
		case 0xa1e0:
		case 0xa200:
		case 0xa202:
		case 0xa203:
		case 0xa204:
		case 0xa205:
		case 0xa206:
		case 0xa207:
		case 0xc242: return true;
		default: return false;
	}
}
} // namespace
struct GnmSession::State {
	mutable std::mutex                          mutex;
	std::mutex                                  queue_mutex, finish_mutex;
	std::condition_variable                     wake;
	std::deque<std::packaged_task<int32_t()>>   jobs;
	std::thread                                 worker;
	bool                                        stopping = false;
	GnmMemory                                   memory;
	Registers                                   registers;
	std::unique_ptr<Graphics::Ps4::GnmRenderer> renderer;
	GnmStats                                    stats;
	uint64_t                                    attempt = 0, next_draw_ordinal = 0;
	std::deque<GnmDrawEvidence>                  evidence;
	std::deque<GnmSubmissionEvidence>            submission_evidence;
	// Only an active Submit may annotate its rows. Builder errors must not
	// retroactively mark an earlier successful batch as rejected.
	bool                                        recording_submit = false;
	bool                                        finished = false;
	std::string                                 error;
	int32_t Reject(const std::string& message, int32_t code = Invalid) {
		error = message;
		if (recording_submit) {
			for (auto& row: evidence)
				if (row.attempt == attempt) row.batch_rejected = true;
			if (!submission_evidence.empty() && submission_evidence.back().attempt == attempt) {
				auto& row = submission_evidence.back();
				row.rejected = true;
				row.error = message.substr(0, 1024);
			}
		}
		++stats.rejected;
		std::fprintf(stderr, "PS4 GNM rejected: %s\n", error.c_str());
		return code;
	}
};
namespace {
bool PrepareDraw(GnmSession::State& session, const Registers& registers, uint32_t count,
                 std::map<uint64_t, Target>& targets, Operation& draw,
                 std::vector<std::pair<uint64_t, size_t>>& shader_ranges,
                 const ShaderRecompiler::Gcn::ShaderMemory& read_resource) {
	const auto fail = [&](const std::string& reason) {
		session.error = reason;
		return false;
	};
	const auto get = [&](uint32_t address, uint32_t& value) {
		auto it = registers.values.find(address);
		if (it == registers.values.end())
			return fail("missing GCN register " + std::to_string(address));
		value = it->second;
		return true;
	};
	const auto exact = [&](uint32_t address, uint32_t expected) {
		uint32_t value;
		return get(address, value) &&
		       (value == expected ||
		        fail("unsupported GCN state at register " + std::to_string(address)));
	};
	uint32_t base, pitch, slice, info, attrib, screen, generic_tl, generic_br, target_mask, blend;
	if (!registers.instances || !exact(0xc242, 4) || !exact(0xa200, 0) ||
	    !exact(0xa202, 0xcc0010) || !exact(0xa203, 0) || !exact(0xa204, 1u << 19) ||
	    !exact(0xa205, 0) || !exact(0xa206, 0x3f) || !exact(0xa207, 0) || !exact(0xa1c3, 4) ||
	    !exact(0xa1c4, 0) || !exact(0xa1c5, 9) || !exact(0xa1b8, 0) || !exact(0xa08f, 15) ||
	    !exact(0xa31b, 0) || !exact(0xa00c, 0) || !exact(0xa080, 0) || !exact(0xa081, 0x80000000) ||
	    !exact(0xa0b4, 0) || !exact(0xa0b5, 0x3f800000))
		return fail(session.error.empty() ? "NUM_INSTANCES must be explicitly set to one"
		                                  : session.error);
	uint32_t parameter_count, input_enable, input_address;
	if (!get(0xa1b6, parameter_count) || parameter_count > 4 ||
	    !exact(0xa1b1, parameter_count ? (parameter_count - 1) << 1 : 0) ||
	    !get(0xa1b3, input_enable) || !get(0xa1b4, input_address) ||
	    input_enable != input_address ||
	    (parameter_count ? (input_enable != 0 && input_enable != 2 && input_enable != 32) : input_enable != 0))
		return fail("unsupported GNM interpolator count/enable/export configuration");
	std::array<uint32_t, 4> parameter_map {};
	for (uint32_t i = 0; i < parameter_count; ++i)
		if (!get(0xa191 + i, parameter_map[i])) return false;
	if (!ShaderRecompiler::Gcn::ValidateParameterMap(
	        std::span<const uint32_t>(parameter_map).first(parameter_count), parameter_count,
	        session.error)) return false;
	if (parameter_count && !input_enable)
		for (uint32_t i = 0; i < parameter_count; ++i)
			if (!(parameter_map[i] & 0x420))
				return fail("smooth GCN PARAM requires enabled barycentrics");
	if (!get(0xa318, base) || !get(0xa319, pitch) || !get(0xa31a, slice) || !get(0xa31c, info) ||
	    !get(0xa31d, attrib) || !get(0xa00d, screen) || !get(0xa090, generic_tl) ||
	    !get(0xa091, generic_br) || !get(0xa08e, target_mask) || !get(0xa1e0, blend))
		return false;
	// Liverpool linear-aligned RGBA8/UNorm only; no CMASK/DCC/MSAA or reinterpretation of Prospero
	// descriptors.
	if ((pitch & ~0x7ffu) || (slice & ~0x3fffffu) || (info != 0x28 && info != 0x8028) ||
	    attrib != 8 || target_mask > 15 || (blend != 0 && blend != 0x65010504) ||
	    !(generic_tl & 0x80000000))
		return fail("unsupported GCN color buffer, blend or scissor mode");
	const uint32_t width    = (pitch + 1) * 8;
	const uint64_t elements = (uint64_t(slice) + 1) * 64;
	if (!base || width < 64 || width > 1024 || width % 64 || elements % width ||
	    elements / width > 1024)
		return fail("invalid linear GCN framebuffer dimensions");
	auto& state        = draw.raster;
	state.width        = width;
	state.height       = uint32_t(elements / width);
	state.vertex_count = count;
	if (screen != (state.height << 16 | state.width) || !exact(0xa082, screen))
		return fail("screen/window scissor must match target");
	const float viewport[] {width * .5f, width * .5f, state.height * .5f, state.height * .5f, 1, 0};
	for (uint32_t i = 0; i < 6; ++i)
		if (!exact(0xa10f + i, std::bit_cast<uint32_t>(viewport[i]))) return false;
	if ((generic_tl & 0x00008000u) || (generic_br & 0x80008000u))
		return fail("unsupported scissor reserved bits");
	state.left        = generic_tl & 0x7fff;
	state.top         = (generic_tl >> 16) & 0x7fff;
	state.right       = generic_br & 0x7fff;
	state.bottom      = (generic_br >> 16) & 0x7fff;
	state.write_mask  = uint8_t(target_mask);
	state.alpha_blend = blend != 0;
	if (!Graphics::Ps4::ValidateRasterState(state, session.error)) return false;
	draw.target        = uint64_t(base) << 8;
	const size_t bytes = size_t(state.width) * state.height * 4;
	if (!Writable(session.memory, draw.target, bytes))
		return fail("framebuffer is not writable guest memory");
	auto it = targets.find(draw.target);
	if (it == targets.end()) {
		size_t total = bytes;
		for (const auto& [address, target]: targets) {
			total += target.pixels.size();
			if (draw.target < address + target.pixels.size() && address < draw.target + bytes)
				return fail("overlapping framebuffer ranges");
		}
		if (total > 16 * 1024 * 1024) return fail("GNM target budget exceeded");
		Target target {draw.target, state.width, state.height, std::vector<uint8_t>(bytes)};
		if (!Read(session.memory, draw.target, target.pixels))
			return fail("framebuffer snapshot unreadable");
		targets.emplace(draw.target, std::move(target));
	} else if (it->second.width != state.width || it->second.height != state.height)
		return fail("framebuffer layout changed within batch");
	const auto shader = [&](bool vertex, std::vector<uint32_t>& spirv) {
		const uint32_t reg = vertex ? 0x2c48 : 0x2c08;
		uint32_t       lo, hi, rsrc1, rsrc2;
		if (!get(reg, lo) || !get(reg + 1, hi) || !get(reg + 2, rsrc1) || !get(reg + 3, rsrc2))
			return false;
		if (!lo || hi || (rsrc1 & ~0x000c03ffu) || (rsrc1 & 0x000f0000u) != 0x000c0000u ||
		    (rsrc2 & ~0x3eu) || ((rsrc2 >> 1) & 31) > 16 || ((rsrc1 >> 6) & 15) > 12 ||
		    ((rsrc2 >> 1) & 31) > (((rsrc1 >> 6) & 15) + 1) * 8)
			return fail("unsupported GCN shader resource/FP configuration");
		const uint32_t        users = (rsrc2 >> 1) & 31;
		std::vector<uint32_t> data(users);
		for (uint32_t i = 0; i < users; ++i)
			if (!get(reg + 4 + i, data[i])) return false;
		const uint64_t        address = uint64_t(lo) << 8;
		std::vector<uint32_t> code;
		std::string           compile_error;
		for (uint32_t i = 0; i < 4096; ++i) {
			uint32_t word;
			if (!ReadObject(session.memory, address + uint64_t(i) * 4, word))
				return fail("GCN shader unreadable: " + compile_error);
			code.push_back(word);
			if (word != 0xbf810000u) continue;
			std::vector<ShaderRecompiler::Gcn::BufferDescriptor> resources;
			std::vector<ShaderRecompiler::Gcn::ImageDescriptor>  textures;
			std::vector<ShaderRecompiler::Gcn::ScalarRead>         scalar_reads;
			std::vector<uint32_t>                               normalized;
			uint32_t                                            scalar_registers = 0;
			uint32_t                                            writable_buffers = 0;
			if (ShaderRecompiler::Gcn::CompileImages(
			        code,
			        vertex ? ShaderRecompiler::Gcn::Stage::Vertex
			               : ShaderRecompiler::Gcn::Stage::Pixel,
			        spirv, compile_error, data, parameter_count,
			        std::span<const uint32_t>(parameter_map).first(parameter_count),
			        input_enable == 32, resources, textures,
			        read_resource, &scalar_reads, &normalized, &scalar_registers, &writable_buffers)) {
				if (scalar_registers > (((rsrc1 >> 6) & 15) + 1) * 8)
					return fail("fetch/scalar prelude exceeds declared SGPR allocation");
				if (!RegisterLimits(normalized, {}, vertex, rsrc1, session.error, parameter_count))
					return false;
				auto& buffers = vertex ? draw.vertex_buffers : draw.pixel_buffers;
				for (size_t index = 0; index < resources.size(); ++index) {
					const auto& resource = resources[index];
					auto& snapshot = buffers[index];
					snapshot.address = resource.address;
					snapshot.writable = (writable_buffers & (1u << index)) != 0;
					snapshot.bytes.resize(resource.size);
					if (!read_resource(resource.address, snapshot.bytes))
						return fail("PS4 shader buffer unreadable");
				}
				for (size_t index = 0; index < textures.size(); ++index) {
					const auto& texture = textures[index];
					auto& snapshot = (vertex ? draw.vertex_images : draw.pixel_images)[index];
					snapshot.descriptor = texture;
					snapshot.pixels.resize(texture.size);
					if (!read_resource(texture.address, snapshot.pixels))
						return fail("PS4 image payload unreadable");
				}
				draw.scalar_reads.insert(draw.scalar_reads.end(), scalar_reads.begin(), scalar_reads.end());
				shader_ranges.emplace_back(address, code.size() * 4);
				return true;
			}
		}
		return fail("GCN shader unsupported/unterminated: " + compile_error);
	};
	return shader(true, draw.vertex) && shader(false, draw.pixel);
}
std::shared_ptr<GnmSession> Active() {
	std::lock_guard lock(registry_mutex);
	return current.lock();
}
int32_t KYTY_SYSV_ABI Submit(uint32_t n, const uint32_t* const* dcbs, const uint32_t* sizes,
                             const uint32_t* const* ccbs, const uint32_t* csizes) {
	auto s = Active();
	return s ? s->Submit(n, uint64_t(dcbs), uint64_t(sizes), uint64_t(ccbs), uint64_t(csizes))
	         : Invalid;
}
int32_t KYTY_SYSV_ABI SetVs(uint32_t* commands, uint32_t size, const uint32_t* regs,
                            uint32_t modifier) {
	auto s = Active();
	return s ? s->SetShader(true, uint64_t(commands), size, uint64_t(regs), modifier) : -1;
}
int32_t KYTY_SYSV_ABI SetPs(uint32_t* commands, uint32_t size, const uint32_t* regs) {
	auto s = Active();
	return s ? s->SetShader(false, uint64_t(commands), size, uint64_t(regs)) : -1;
}
int32_t KYTY_SYSV_ABI DrawAuto(uint32_t* commands, uint32_t size, uint32_t n, uint32_t flags) {
	auto s = Active();
	return s ? s->Draw(uint64_t(commands), size, n, flags) : -1;
}
int32_t KYTY_SYSV_ABI DrawIndex(uint32_t* commands, uint32_t size, uint32_t n,
                                uint64_t indices, uint32_t flags, uint32_t type) {
	auto s = Active();
	return s ? s->DrawIndexed(uint64_t(commands), size, n, indices, flags, type) : -1;
}
int32_t KYTY_SYSV_ABI Done() {
	auto s = Active();
	return s ? s->Done() : Invalid;
}
} // namespace
GnmSession::GnmSession(std::unique_ptr<State> state): m_state(std::move(state)) {}
std::shared_ptr<GnmSession>
GnmSession::Create(GnmMemory memory, std::string& error,
                   std::unique_ptr<Graphics::Ps4::GnmRenderer> renderer) {
	if (!memory.read || !memory.write || !memory.writable) {
		error = "Missing GNM memory callbacks";
		return {};
	}
	std::lock_guard lock(registry_mutex);
	if (!current.expired()) {
		error = "GNM session already active";
		return {};
	}
	auto state           = std::make_unique<State>();
	state->memory        = std::move(memory);
	state->renderer      = std::move(renderer);
	auto  session        = std::shared_ptr<GnmSession>(new GnmSession(std::move(state)));
	auto* worker_state   = session->m_state.get();
	worker_state->worker = std::thread([worker_state] {
		for (;;) {
			std::packaged_task<int32_t()> job;
			{
				std::unique_lock lock(worker_state->queue_mutex);
				worker_state->wake.wait(
				    lock, [&] { return worker_state->stopping || !worker_state->jobs.empty(); });
				if (worker_state->jobs.empty()) return;
				job = std::move(worker_state->jobs.front());
				worker_state->jobs.pop_front();
			}
			job();
		}
	});
	current              = session;
	return session;
}
int32_t GnmSession::Submit(uint32_t count, uint64_t dcbs, uint64_t sizes, uint64_t ccbs,
                           uint64_t cc_sizes) {
	auto&                         s = *m_state;
	std::packaged_task<int32_t()> job(
	    [=, this] { return SubmitOnWorker(count, dcbs, sizes, ccbs, cc_sizes); });
	auto result = job.get_future();
	{
		std::lock_guard lock(s.queue_mutex);
		if (s.stopping) return Invalid;
		s.jobs.push_back(std::move(job));
	}
	s.wake.notify_one();
	return result.get();
}
int32_t GnmSession::SubmitOnWorker(uint32_t count, uint64_t dcbs, uint64_t sizes, uint64_t ccbs,
                                   uint64_t cc_sizes) {
	auto&           s = *m_state;
	std::lock_guard lock(s.mutex);
	s.error.clear();
	++s.attempt;
	if (s.submission_evidence.size() == 256) s.submission_evidence.pop_front();
	s.submission_evidence.push_back({s.attempt});
	s.recording_submit = true;
	struct EndRecording {
		bool& active;
		~EndRecording() { active = false; }
	} recording {s.recording_submit};
	if (s.finished) return s.Reject("GNM session finished");
	const auto lease = s.memory.hold ? s.memory.hold() : std::shared_ptr<void> {};
	if (s.memory.hold && !lease) return s.Reject("cannot pin GNM guest mappings");
	if (!count || count > 16 || dcbs % 8 || sizes % 4)
		return s.Reject("invalid GNM command array/count");
	std::array<uint64_t, 16> addresses {};
	std::array<uint32_t, 16> lengths {};
	if (!Read(s.memory, dcbs, {reinterpret_cast<uint8_t*>(addresses.data()), count * 8}) ||
	    !Read(s.memory, sizes, {reinterpret_cast<uint8_t*>(lengths.data()), count * 4}))
		return s.Reject("unreadable GNM command arrays");
	if (cc_sizes) {
		std::array<uint32_t, 16> sizes_cc {};
		if (!Read(s.memory, cc_sizes, {reinterpret_cast<uint8_t*>(sizes_cc.data()), count * 4}) ||
		    std::any_of(sizes_cc.begin(), sizes_cc.begin() + count,
		                [](uint32_t n) { return n != 0; }))
			return s.Reject("CCB/constant-engine streams unsupported");
	} else if (ccbs)
		return s.Reject("CCB array without sizes");
	std::vector<std::vector<uint32_t>> buffers;
	size_t                             total = 0;
	for (uint32_t i = 0; i < count; ++i) {
		if (!lengths[i] || lengths[i] % 4 || lengths[i] > 0x3ffffc || addresses[i] % 4 ||
		    lengths[i] > MaxBytes - total)
			return s.Reject("invalid/oversized GNM DCB");
		total += lengths[i];
		buffers.emplace_back(lengths[i] / 4);
		if (!Read(s.memory, addresses[i],
		          {reinterpret_cast<uint8_t*>(buffers.back().data()), lengths[i]}))
			return s.Reject("unreadable GNM DCB");
	}
	std::vector<std::pair<uint64_t, size_t>> command_ranges;
	for (uint32_t i = 0; i < count; ++i) command_ranges.emplace_back(addresses[i], lengths[i]);
	std::vector<uint64_t> active_streams;
	uint32_t indirect_count = 0;
	auto                                     registers = s.registers;
	std::vector<Operation>                   operations;
	std::map<uint64_t, Target>               targets;
	std::vector<std::pair<uint64_t, size_t>> shader_ranges;
	uint32_t                                 draw_count = 0, transfer_count = 0;
	uint64_t                                 resource_bytes = 0;
	size_t transfer_bytes = 0;
	std::vector<size_t> transfer_indices;
	const auto read_resource = [&](uint64_t address, std::span<uint8_t> bytes) {
		if (!Read(s.memory, address, bytes)) return false;
		for (const auto index : transfer_indices) {
			auto& operation = operations[index];
			const auto& write = operation.transfer.writes.front();
			const uint64_t destination = operation.transfer.target.address;
			const size_t size = write.words.size() * 4;
			const uint64_t start = std::max(address, destination);
			const uint64_t end = std::min(address + bytes.size(), destination + size);
			if (start >= end) continue;
			std::memcpy(bytes.data() + (start - address),
			            reinterpret_cast<const uint8_t*>(write.words.data()) + (start - destination),
			            size_t(end - start));
		}
		return true;
	};
	std::function<int32_t(std::span<const uint32_t>, uint32_t, uint64_t, uint32_t)> parse;
	parse = [&](std::span<const uint32_t> commands, uint32_t command_index,
	            uint64_t command_address, uint32_t depth) -> int32_t {
		if (depth > 8 || std::find(active_streams.begin(), active_streams.end(), command_address) != active_streams.end())
			return s.Reject("GNM indirect buffer cycle/depth budget exceeded");
		active_streams.push_back(command_address);
		struct PopStream {
			std::vector<uint64_t>& stack;
			~PopStream() { stack.pop_back(); }
		} pop {active_streams};
		for (size_t pc = 0; pc < commands.size();) {
			auto& location = s.submission_evidence.back();
			location.command_address = command_address;
			location.command_dword = uint32_t(pc);
			location.opcode = (commands[pc] >> 8) & 255;
			const uint32_t header = commands[pc];
			const size_t   words  = ((header >> 16) & 0x3fff) + 2;
			if (header >> 30 != 3 || (header & 255) || words > commands.size() - pc)
				return s.Reject("invalid/truncated GNM type-3 packet");
			const uint32_t op = (header >> 8) & 255;
			if (op == 0x10) {
			} else if (op == 0x3f) {
				// Unconditional graphics IB, VMID0; chain replaces this stream's
				// remaining tail. Conditional IBs and other control flags are separate.
				if (words != 4 || (commands[pc + 2] & ~255u) ||
				    (commands[pc + 3] & ~0x001fffffu) || ++indirect_count > 256)
					return s.Reject("unsupported GNM indirect buffer controls/count");
				const uint64_t address = commands[pc + 1] | (uint64_t(commands[pc + 2]) << 32);
				const size_t bytes = size_t(commands[pc + 3] & 0xfffff) * 4;
				if (!address || address % 4 || !bytes || address >= (1ull << 40) ||
				    bytes > (1ull << 40) - address || bytes > MaxBytes - total)
					return s.Reject("invalid/oversized GNM indirect buffer range");
				total += bytes;
				std::vector<uint32_t> nested(bytes / 4);
				if (!Read(s.memory, address, {reinterpret_cast<uint8_t*>(nested.data()), bytes}))
					return s.Reject("unreadable GNM indirect buffer payload");
				command_ranges.emplace_back(address, bytes);
				const auto result = parse(nested, command_index, address, depth + 1);
				if (result || (commands[pc + 3] & 0x100000u)) return result;
			} else if (op == 0x69 || op == 0x76 || op == 0x79) {
				const uint32_t base = op == 0x69 ? 0xa000 : op == 0x76 ? 0x2c00 : 0xc000;
				if (words < 3 || commands[pc + 1] >= 0x400 || words - 2 > 0x400 - commands[pc + 1])
					return s.Reject("invalid SET_*_REG range");
				for (size_t n = 2; n < words; ++n) {
					const uint32_t reg   = base + commands[pc + 1] + uint32_t(n - 2),
					               value = commands[pc + n];
					if (!KnownRegister(reg, value))
						return s.Reject("unsupported GCN register " + std::to_string(reg));
					registers.values[reg] = value;
				}
			} else if (op == 0x2a) {
				if (words != 2 || commands[pc + 1] > 1)
					return s.Reject("unsupported GNM index type/endian controls");
				registers.index_bytes = commands[pc + 1] ? 4 : 2;
			} else if (op == 0x2f) {
				if (words != 2 || commands[pc + 1] != 1)
					return s.Reject("only one GNM instance is supported");
				registers.instances = true;
			} else if (op == 0x37 || op == 0x50) {
				if (++transfer_count > 4096) return s.Reject("GNM transfer count budget exceeded");
				const bool write = op == 0x37;
				if ((write && words < 5) || (!write && words != 7))
					return s.Reject("invalid GNM transfer packet length");
				const uint64_t destination = write
				    ? commands[pc + 2] | (uint64_t(commands[pc + 3]) << 32)
				    : commands[pc + 4] | (uint64_t(commands[pc + 5]) << 32);
				const size_t bytes = write ? (words - 4) * 4 : commands[pc + 6] & 0x1fffffu;
				if (bytes > 16 * 1024 * 1024 - transfer_bytes)
					return s.Reject("GNM transfer byte budget exceeded");
				if (!destination || destination >= (1ull << 40) || bytes > (1ull << 40) - destination)
					return s.Reject("invalid GNM transfer address");
				Operation operation {};
				operation.command_address = command_address;
				operation.command_dword = uint32_t(pc);
				operation.command_opcode = op;
				const uint32_t pitch = uint32_t((bytes + 255) & ~size_t(255));
				if (!Graphics::Ps4::DecodeTransfers(commands.subspan(pc, words),
				                                    {destination, 1, 1, pitch}, operation.transfer, s.error))
					return s.Reject(s.error);
				transfer_bytes += bytes;
				transfer_indices.push_back(operations.size());
				operations.push_back(std::move(operation));
			} else if (op == 0x2d || op == 0x27) {
				const bool indexed = op == 0x27;
				if (draw_count >= 64 ||
				    (!indexed && (words != 3 || commands[pc + 2] != 2)) ||
				    (indexed && (words != 6 || commands[pc + 5] != 0 || !registers.index_bytes ||
				                 commands[pc + 1] < commands[pc + 4] || (commands[pc + 3] & ~255u))))
					return s.Reject("unsupported indexed/auto draw mode/budget");
				const uint32_t vertex_count = commands[pc + (indexed ? 4 : 1)];
				Operation draw;
				draw.command_address = command_address;
				draw.command_dword = uint32_t(pc);
				draw.command_opcode = op;
				if (!PrepareDraw(s, registers, vertex_count, targets, draw, shader_ranges, read_resource))
					return s.Reject(s.error);
				if (indexed) {
					draw.index_address = commands[pc + 2] | (uint64_t(commands[pc + 3]) << 32);
					const size_t bytes = size_t(vertex_count) * registers.index_bytes;
					if (!draw.index_address || draw.index_address % registers.index_bytes ||
					    draw.index_address >= (1ull << 40) || bytes > (1ull << 40) - draw.index_address)
						return s.Reject("invalid GNM index buffer range/alignment");
					draw.indices.resize(bytes);
					if (!read_resource(draw.index_address, draw.indices))
						return s.Reject("unreadable GNM index buffer payload");
					draw.raster.index_bytes = registers.index_bytes;
				}
				for (const auto index : transfer_indices) {
					auto& transfer = operations[index];
					if (transfer.observed_by_draw) continue;
					const auto destination = transfer.transfer.target.address;
					const auto bytes = transfer.transfer.writes.front().words.size() * 4;
					const auto contains = [&](uint64_t base, size_t size) {
						return destination >= base && destination - base <= size &&
						       bytes <= size - (destination - base);
					};
					transfer.observed_by_draw = contains(draw.index_address, draw.indices.size());
					for (const auto& read : draw.scalar_reads)
						transfer.observed_by_draw |= contains(read.address, read.bytes);
					for (const auto* buffers : {&draw.pixel_buffers, &draw.vertex_buffers})
						for (const auto& buffer : *buffers)
							transfer.observed_by_draw |= contains(buffer.address, buffer.bytes.size());
					for (const auto* images : {&draw.pixel_images, &draw.vertex_images})
						for (const auto& image : *images)
							transfer.observed_by_draw |= contains(image.descriptor.address, image.pixels.size());
				}
				auto draw_bytes = draw.indices.size();
				for (const auto* buffers : {&draw.pixel_buffers, &draw.vertex_buffers})
					for (const auto& buffer : *buffers) draw_bytes += buffer.bytes.size();
				for (const auto* images : {&draw.pixel_images, &draw.vertex_images})
					for (const auto& image : *images) draw_bytes += image.pixels.size();
				resource_bytes += draw_bytes;
				for (const auto& read: draw.scalar_reads) resource_bytes += read.bytes;
				if (resource_bytes > 16 * 1024 * 1024)
					return s.Reject("shader resource snapshot byte budget exceeded");
				++draw_count;
				draw.evidence_ordinal = ++s.next_draw_ordinal;
				if (s.evidence.size() == 256) s.evidence.pop_front();
				s.evidence.push_back({s.attempt, draw.evidence_ordinal, draw.target,
				                      draw_bytes,
				                      uint32_t(command_index), uint32_t(pc),
				                      uint32_t(draw.vertex.size()), uint32_t(draw.pixel.size())});
				s.evidence.back().command_address = command_address;
				operations.push_back(std::move(draw));
			} else
				return s.Reject("unsupported PS4 PM4 opcode " + std::to_string(op) + " at dword " +
				                std::to_string(pc));
			pc += words;
		}
		return 0;
	};
	for (uint32_t i = 0; i < buffers.size(); ++i)
		if (const auto result = parse(buffers[i], i, addresses[i], 0)) return result;
	std::vector<Operation::BufferSnapshot*> shader_writes;
	for (auto& operation : operations)
		for (auto* buffers : {&operation.pixel_buffers, &operation.vertex_buffers})
			for (auto& buffer : *buffers)
				if (buffer.writable) shader_writes.push_back(&buffer);
	for (const auto* write : shader_writes) {
		const auto size = write->bytes.size();
		if (!Writable(s.memory, write->address, size))
			return s.Reject("PS4 shader write range is not writable");
		const auto aliases = [&](uint64_t address, size_t bytes) {
			return bytes && (address <= write->address ? write->address - address < bytes
			                                          : address - write->address < size);
		};
		for (const auto& [address, bytes] : command_ranges)
			if (aliases(address, bytes)) return s.Reject("PS4 shader write aliases command memory");
		for (const auto& [address, bytes] : shader_ranges)
			if (aliases(address, bytes)) return s.Reject("PS4 shader write aliases code");
		if (aliases(dcbs, count * 8) || aliases(sizes, count * 4))
			return s.Reject("PS4 shader write aliases command arrays");
		for (const auto index : transfer_indices) {
			const auto& transfer = operations[index].transfer;
			if (aliases(transfer.target.address, transfer.writes.front().words.size() * 4))
				return s.Reject("PS4 shader write aliases a PM4 transfer");
		}
		for (const auto& operation : operations) {
			if (aliases(operation.index_address, operation.indices.size()))
				return s.Reject("PS4 shader write aliases an index snapshot");
			for (const auto& read : operation.scalar_reads)
				if (aliases(read.address, read.bytes))
					return s.Reject("PS4 shader write aliases a scalar prelude snapshot");
			for (const auto* images : {&operation.pixel_images, &operation.vertex_images})
				for (const auto& image : *images)
					if (aliases(image.descriptor.address, image.pixels.size()))
						return s.Reject("PS4 shader write aliases an image snapshot");
			for (const auto* buffers : {&operation.pixel_buffers, &operation.vertex_buffers})
				for (const auto& buffer : *buffers)
					if (&buffer != write && aliases(buffer.address, buffer.bytes.size()))
						return s.Reject("PS4 shader write aliases another buffer snapshot");
		}
	}
	// Capture does not grant permission for output to modify pending command,
	// shader or scalar table bytes. Reject these aliases before any host draw.
	for (const auto& [address, target]: targets) {
		const auto aliases = [&](uint64_t base, size_t bytes) {
			return base <= address ? address - base < bytes : base - address < target.pixels.size();
		};
		for (const auto& [base, bytes]: command_ranges)
			if (aliases(base, bytes)) return s.Reject("GNM target aliases command buffer snapshot");
		for (const auto& [base, bytes]: shader_ranges)
			if (aliases(base, bytes)) return s.Reject("GNM target aliases shader code snapshot");
	}
	for (const auto& operation: operations) {
		// Descriptor/constant table bytes were snapshotted before any host work.
		// Draw targets cannot overlap table reads. Ordered resource transfers are staged separately.
		for (const auto& read: operation.scalar_reads)
			for (const auto& [address, target]: targets)
				if (read.address <= address ? address - read.address < read.bytes
				                            : read.address - address < target.pixels.size())
					return s.Reject("scalar table aliases a draw target in this batch");
		const auto aliases_target = [&](uint64_t base, size_t bytes) {
			if (!bytes) return false;
			for (const auto& [address, target] : targets)
				if (base <= address ? address - base < bytes : base - address < target.pixels.size())
					return true;
			return false;
		};
		for (const auto* images : {&operation.pixel_images, &operation.vertex_images})
			for (const auto& image : *images)
				if (aliases_target(image.descriptor.address, image.pixels.size()))
					return s.Reject("shader resource aliases a draw target in this batch");
		for (const auto* buffers : {&operation.pixel_buffers, &operation.vertex_buffers})
			for (const auto& buffer : *buffers)
				if (aliases_target(buffer.address, buffer.bytes.size()))
					return s.Reject("shader resource aliases a draw target in this batch");
		if (aliases_target(operation.index_address, operation.indices.size()))
			return s.Reject("shader resource aliases a draw target in this batch");
	}
	// Resolve transfers only after every target and draw has been validated. Commands
	// and shaders are snapshots: reject mixed batches whose outputs could alter either.
	if (transfer_count) {
		const auto overlaps = [](uint64_t a, size_t an, uint64_t b, size_t bn) {
			return a <= b ? b - a < an : a - b < bn;
		};
		for (const auto& [address, target]: targets) {
			for (const auto& [shader, bytes]: shader_ranges)
				if (overlaps(address, target.pixels.size(), shader, bytes))
					return s.Reject("GNM mixed-batch target aliases shader code");
			for (uint32_t i = 0; i < count; ++i)
				if (overlaps(address, target.pixels.size(), addresses[i], lengths[i]))
					return s.Reject("GNM mixed-batch target aliases command buffer");
			if (overlaps(address, target.pixels.size(), dcbs, count * 8) ||
			    overlaps(address, target.pixels.size(), sizes, count * 4))
				return s.Reject("GNM mixed-batch target aliases command arrays");
		}
		for (const auto index : transfer_indices) {
			auto& operation = operations[index];
			auto& write = operation.transfer.writes.front();
			const uint64_t destination = operation.transfer.target.address;
			const size_t bytes = write.words.size() * 4;
			for (const auto& [address, target] : targets) {
				if (!overlaps(destination, bytes, address, target.pixels.size())) continue;
				if (destination < address || bytes > target.pixels.size() - (destination - address))
					return s.Reject("GNM transfer crosses a draw target boundary");
				operation.target = address;
				operation.transfer.target = {address, target.width, target.height, target.width * 4};
				write.offset = uint32_t(destination - address);
				break;
			}
			if (operation.target) continue;
			if (!operation.observed_by_draw || !Writable(s.memory, destination, bytes))
				return s.Reject("GNM transfer outside a writable resource read by a later draw");
			const auto aliases = [&](const auto& range) {
				return overlaps(destination, bytes, range.first, range.second);
			};
			if (std::any_of(shader_ranges.begin(), shader_ranges.end(), aliases) ||
			    std::any_of(command_ranges.begin(), command_ranges.end(), aliases) ||
			    overlaps(destination, bytes, dcbs, count * 8) ||
			    overlaps(destination, bytes, sizes, count * 4))
				return s.Reject("GNM resource transfer aliases shader or command memory");
			operation.resource_transfer = true;
		}
	}
	if (draw_count && !s.renderer) {
		s.submission_evidence.back().phase = GnmSubmitPhase::Host;
		s.submission_evidence.back().command_address = 0;
		s.submission_evidence.back().command_dword = 0;
		s.submission_evidence.back().opcode = 0;
		s.renderer = Graphics::Ps4::CreateGnmRenderer(s.error);
		if (!s.renderer) return s.Reject(s.error);
		std::fprintf(stderr, "PS4 GNM renderer: %s\n", s.renderer->Name());
	}
	for (auto& draw: operations) {
		auto& progress = s.submission_evidence.back();
		progress.phase = GnmSubmitPhase::Host;
		progress.command_address = draw.command_address;
		progress.command_dword = draw.command_dword;
		progress.opcode = draw.command_opcode;
		progress.target = draw.target;
		if (draw.resource_transfer) continue;
		auto& target = targets.at(draw.target);
		if (!draw.transfer.writes.empty()) {
			if (!Graphics::Ps4::ApplyTransfers(draw.transfer, target.pixels, s.error))
				return s.Reject(s.error);
			continue;
		}
		auto state           = draw.raster;
		state.initial_pixels = target.pixels;
		state.indices        = draw.indices;
		const auto bind_buffers = [](const auto& buffers, auto& first, auto& second, auto& third, auto& tail) {
			first = buffers[0].bytes;
			second = buffers[1].bytes;
			third = buffers[2].bytes;
			for (size_t i = 0; i < tail.size(); ++i) tail[i] = buffers[i + 3].bytes;
		};
		bind_buffers(draw.pixel_buffers, state.pixel_buffer, state.pixel_buffer2, state.pixel_buffer3, state.pixel_buffer_tail);
		bind_buffers(draw.vertex_buffers, state.vertex_buffer, state.vertex_buffer2, state.vertex_buffer3, state.vertex_buffer_tail);
		for (size_t i = 0; i < Graphics::Ps4::BuffersPerStage; ++i) {
			if (draw.pixel_buffers[i].writable) {
				state.pixel_storage_writes = true;
				state.storage_readback[i] = draw.pixel_buffers[i].bytes;
			}
			if (draw.vertex_buffers[i].writable) {
				state.vertex_storage_writes = true;
				state.storage_readback[Graphics::Ps4::BuffersPerStage + i] = draw.vertex_buffers[i].bytes;
			}
		}
		const auto bind_images = [](const auto& images, auto& first, auto& second, auto& tail) {
			const auto bind = [](const auto& image) {
				const auto& d = image.descriptor;
				return Graphics::Ps4::Texture2D {d.width, d.height, d.pitch, image.pixels};
			};
			first = bind(images[0]);
			second = bind(images[1]);
			for (size_t i = 0; i < tail.size(); ++i) tail[i] = bind(images[i + 2]);
		};
		bind_images(draw.pixel_images, state.pixel_texture, state.pixel_texture2, state.pixel_texture_tail);
		bind_images(draw.vertex_images, state.vertex_texture, state.vertex_texture2, state.vertex_texture_tail);
		std::vector<uint8_t> pixels;
		if (!s.renderer->Render(state, draw.vertex, draw.pixel, pixels, s.error) ||
		    pixels.size() != target.pixels.size())
			return s.Reject("GNM host render failed: " + s.error);
		for (auto& row: s.evidence)
			if (row.ordinal == draw.evidence_ordinal) row.host_completed = true;
		target.pixels = std::move(pixels);
	}
	s.submission_evidence.back().phase = GnmSubmitPhase::Publication;
	s.submission_evidence.back().command_address = 0;
	s.submission_evidence.back().command_dword = 0;
	s.submission_evidence.back().opcode = 0;
	for (const auto& [address, target]: targets) {
		s.submission_evidence.back().target = address;
		if (!Writable(s.memory, address, target.pixels.size()))
			return s.Reject("GNM framebuffer mapping lost before commit");
	}
	for (const auto index : transfer_indices) {
		const auto& operation = operations[index];
		if (!operation.resource_transfer) continue;
		const auto address = operation.transfer.target.address;
		const auto& words = operation.transfer.writes.front().words;
		if (!Writable(s.memory, address, words.size() * 4))
			return s.Reject("GNM shader resource mapping lost before commit");
	}
	for (const auto* write : shader_writes)
		if (!Writable(s.memory, write->address, write->bytes.size()))
			return s.Reject("GNM shader write mapping lost before commit");
	for (const auto index : transfer_indices) {
		const auto& operation = operations[index];
		if (!operation.resource_transfer) continue;
		const auto address = operation.transfer.target.address;
		const auto& words = operation.transfer.writes.front().words;
		s.submission_evidence.back().target = address;
		if (!s.memory.write(address, {reinterpret_cast<const uint8_t*>(words.data()), words.size() * 4}))
			return s.Reject("GNM shader resource commit failed");
	}
	for (const auto& [address, target]: targets) {
		s.submission_evidence.back().target = address;
		if (!s.memory.write(address, target.pixels))
			return s.Reject("GNM framebuffer commit failed");
		for (auto& row: s.evidence)
			if (row.attempt == s.attempt && row.target == address && row.host_completed)
				row.guest_published = true;
	}
	for (const auto* write : shader_writes) {
		s.submission_evidence.back().target = write->address;
		if (!s.memory.write(write->address, write->bytes))
			return s.Reject("GNM shader write commit failed");
	}
	s.registers = std::move(registers);
	++s.stats.submissions;
	s.stats.draws += draw_count;
	s.submission_evidence.back().phase = GnmSubmitPhase::Completed;
	s.submission_evidence.back().success = true;
	return 0;
}
int32_t GnmSession::SetShader(bool vertex, uint64_t commands, uint32_t size, uint64_t registers,
                              uint32_t modifier) {
	auto&           s = *m_state;
	std::lock_guard lock(s.mutex);
	if (s.finished) return s.Reject("GNM session finished", -1);
	const auto lease = s.memory.hold ? s.memory.hold() : std::shared_ptr<void> {};
	if (s.memory.hold && !lease) return s.Reject("cannot pin GNM guest mappings", -1);
	const uint32_t           required = vertex ? 29 : 40;
	std::array<uint32_t, 12> regs {};
	if (size < required || modifier || commands % 4 || registers % 4 ||
	    !Read(s.memory, registers,
	          {reinterpret_cast<uint8_t*>(regs.data()), (vertex ? 7u : 12u) * 4}) ||
	    regs[1])
		return s.Reject("invalid GNM shader-builder parameters", -1);
	std::vector<uint32_t> out;
	Packet(out, 0x76, vertex ? 0x48 : 8, {regs[0], 0});
	Packet(out, 0x76, vertex ? 0x4a : 10, {regs[2], regs[3]});
	if (vertex) {
		Packet(out, 0x69, 0x207, {regs[6]});
		Packet(out, 0x69, 0x1b1, {regs[4]});
		Packet(out, 0x69, 0x1c3, {regs[5]});
	} else {
		Packet(out, 0x69, 0x1c4, {regs[4], regs[5]});
		Packet(out, 0x69, 0x1b3, {regs[6], regs[7]});
		Packet(out, 0x69, 0x1b6, {regs[8]});
		Packet(out, 0x69, 0x1b8, {regs[9]});
		Packet(out, 0x69, 0x203, {regs[10]});
		Packet(out, 0x69, 0x8f, {regs[11]});
	}
	NopTo(out, required);
	if (!WriteWords(s.memory, commands, out))
		return s.Reject("GNM shader-builder output is not writable", -1);
	return 0;
}
int32_t GnmSession::Draw(uint64_t commands, uint32_t size, uint32_t count, uint32_t flags) {
	auto&           s = *m_state;
	std::lock_guard lock(s.mutex);
	if (s.finished) return s.Reject("GNM session finished", -1);
	const auto lease = s.memory.hold ? s.memory.hold() : std::shared_ptr<void> {};
	if (s.memory.hold && !lease) return s.Reject("cannot pin GNM guest mappings", -1);
	if (commands % 4 || size != 7 || flags || !count || count > 65535 || count % 3)
		return s.Reject("unsupported GNM draw-builder parameters", -1);
	const std::vector<uint32_t> out {0xc0012d00, count, 2, 0xc0021000, 0, 0, 0};
	if (!WriteWords(s.memory, commands, out))
		return s.Reject("GNM draw-builder output not writable", -1);
	return 0;
}
int32_t GnmSession::DrawIndexed(uint64_t commands, uint32_t size, uint32_t count,
                                 uint64_t indices, uint32_t flags, uint32_t type) {
	auto& s = *m_state;
	std::lock_guard lock(s.mutex);
	if (s.finished) return s.Reject("GNM session finished", -1);
	const auto lease = s.memory.hold ? s.memory.hold() : std::shared_ptr<void> {};
	if (s.memory.hold && !lease) return s.Reject("cannot pin GNM guest mappings", -1);
	if (commands % 4 || size != 10 || flags || type || !count || count > 65535 || count % 3 ||
	    !indices || indices % 2 || indices >= (1ull << 40))
		return s.Reject("unsupported GNM indexed draw-builder parameters", -1);
	std::vector<uint32_t> out {0xc0042700u, count, uint32_t(indices), uint32_t(indices >> 32), count, 0};
	NopTo(out, 10);
	if (!WriteWords(s.memory, commands, out))
		return s.Reject("GNM indexed draw-builder output not writable", -1);
	return 0;
}
int32_t GnmSession::Done() {
	auto&           s = *m_state;
	std::lock_guard lock(s.mutex);
	return s.finished ? s.Reject("GNM session finished") : 0;
}
void GnmSession::Finish() {
	auto&           s = *m_state;
	std::lock_guard finish_lock(s.finish_mutex);
	{
		std::lock_guard queue_lock(s.queue_mutex);
		s.stopping = true;
	}
	s.wake.notify_one();
	if (s.worker.joinable()) s.worker.join();
	std::lock_guard lock(s.mutex);
	if (s.finished) return;
	s.finished = true;
	{
		std::lock_guard registry_lock(registry_mutex);
		if (current.lock().get() == this) current.reset();
	}
	s.renderer.reset();
	s.memory = {};
}
std::vector<GnmSubmissionEvidence> GnmSession::SubmissionEvidence() const {
	std::lock_guard lock(m_state->mutex);
	return {m_state->submission_evidence.begin(), m_state->submission_evidence.end()};
}
std::vector<GnmDrawEvidence> GnmSession::DrawEvidence() const {
	std::lock_guard lock(m_state->mutex);
	return {m_state->evidence.begin(), m_state->evidence.end()};
}
GnmStats GnmSession::Stats() const {
	std::lock_guard lock(m_state->mutex);
	return m_state->stats;
}
std::string GnmSession::LastError() const {
	std::lock_guard lock(m_state->mutex);
	return m_state->error;
}
GnmSession::~GnmSession() {
	Finish();
}
void RegisterGnmExports(Loader::SymbolDatabase& symbols) {
	const auto add = [&](const char* nid, auto function, const char* name) {
		Loader::SymbolResolve query {nid, "libSceGnmDriver",       1, "libSceGnmDriver", 1,
		                             0,   Loader::SymbolType::Func};
		if (!symbols.Find(query)) symbols.Add(query, reinterpret_cast<uint64_t>(function), name);
	};
	add("gAhCn6UiU4Y", SetVs, "sceGnmSetVsShader");
	add("bQVd5YzCal0", SetPs, "sceGnmSetPsShader");
	add("GGsn7jMTxw4", DrawAuto, "sceGnmDrawIndexAuto");
	add("HlTPoZ-oY7Y", DrawIndex, "sceGnmDrawIndex");
	add("zwY0YV91TTI", Submit, "sceGnmSubmitCommandBuffers");
	add("yvZ73uQUqrk", Done, "sceGnmSubmitDone");
}
} // namespace Libs::Ps4
