#include "graphics/shader/recompiler/GcnCompiler.h"

#include "graphics/shader/recompiler/ShaderRecompiler.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstring>
#include <map>
#include <optional>

namespace Libs::Graphics::ShaderRecompiler::Gcn {
bool ValidateParameterMap(std::span<const uint32_t> map, uint32_t count, std::string& error) {
	if (count > 4 || (!map.empty() && map.size() != count)) {
		error = "invalid GCN parameter map size";
		return false;
	}
	for (uint32_t control: map) {
		const bool use_default = (control & 0x20) != 0;
		if ((control & ~0x73fu) || (!use_default && ((control & 31) >= count || (control & 0x300)))) {
			error = "unsupported GCN PARAM offset/default/flat controls";
			return false;
		}
	}
	return true;
}
bool DecodeBufferDescriptor(std::span<const uint32_t> words, BufferDescriptor& output,
                            std::string& error, bool indexed) {
	if (words.size() != 4) {
		error = "buffer descriptor requires four DWORDs";
		return false;
	}
	const uint32_t stride = (words[1] >> 16) & 0x3fff;
	const uint64_t bytes  = uint64_t(words[2]) * (stride ? stride : 1);
	if ((words[1] & ~0x3fff00ffu) || (!indexed && stride) || (indexed && (!stride || stride % 4)) ||
	    words[3] != 0xfac || bytes < 16 || bytes > 65536 || bytes % 4) {
		error = "unsupported PS4 buffer descriptor (raw/stride/identity/size)";
		return false;
	}
	BufferDescriptor value {words[0] | (uint64_t(words[1] & 255) << 32), uint32_t(bytes), stride};
	if (!value.address || value.address % 16 || value.address > ((1ull << 40) - value.size)) {
		error = "invalid PS4 buffer address/alignment/range";
		return false;
	}
	output = value;
	error.clear();
	return true;
}

bool DecodeImageDescriptor(std::span<const uint32_t> w, ImageDescriptor& out, std::string& error) {
	if (w.size() != 8 || w[1] != (10u << 20) || (w[2] & 0xf0000000u) ||
	    w[3] != (0x90000000u | (8u << 20) | 0xfacu) || (w[4] & ~0x07ffe000u) || w[5] || w[6] ||
	    w[7]) {
		error = "unsupported PS4 image descriptor (2D linear RGBA8, one mip/layer)";
		return false;
	}
	ImageDescriptor value {uint64_t(w[0]) << 8, (w[2] & 16383) + 1, ((w[2] >> 14) & 16383) + 1,
	                       (((w[4] >> 13) & 16383) + 1) * 4, 0};
	value.size = value.pitch * value.height;
	if (!value.address || value.address + value.size > (1ull << 40) || value.width > 256 ||
	    value.height > 256 || value.pitch < value.width * 4 || value.pitch > 4096 ||
	    value.pitch % 256) {
		error = "invalid PS4 image address/dimensions/linear pitch";
		return false;
	}
	out = value;
	error.clear();
	return true;
}

namespace {
struct FrozenScalars {
	std::array<std::optional<uint32_t>, 104> values {};
	std::vector<ScalarRead> reads;
	size_t prefix_instructions = 0;
	uint32_t scalar_registers = 0;
	bool Words(uint32_t base, std::span<uint32_t> words, std::string& error) const {
		if (base > values.size() || words.size() > values.size() - base) {
			error = "GCN descriptor group exceeds scalar register file";
			return false;
		}
		for (size_t i = 0; i < words.size(); ++i) {
			if (!values[base + i]) {
				error = "GCN descriptor is not an immutable user/table snapshot";
				return false;
			}
			words[i] = *values[base + i];
		}
		return true;
	}
};
// Only an initial scalar-load prelude may be specialized. No speculation through
// branches, pointer arithmetic, writes or dynamic offsets. The runtime must hold
// its guest memory lease and excludes target aliases before submitting work.
bool FreezeScalarPrelude(std::span<const uint32_t> code, std::span<const uint32_t> users,
                         const ShaderMemory& memory, FrozenScalars& frozen,
                         std::vector<uint32_t>& normalized, std::string& error,
                         Stage stage, bool allow_fetch = true) {
	for (size_t i = 0; i < users.size(); ++i) frozen.values[i] = users[i];
	frozen.scalar_registers = std::max(frozen.scalar_registers, uint32_t(users.size()));
	size_t first_body = 0, read_bytes = 0;
	for (const auto& read: frozen.reads) read_bytes += read.bytes;
	std::vector<uint32_t> fetch_body;
	while (first_body < code.size()) {
		const uint32_t word = code[first_body], op = (word >> 22) & 31;
		if (word == 0xbf8c007fu || word == 0xbf8c0f70u || word == 0xbf8c0070u) {
			++first_body;
			continue;
		}
		if (allow_fetch && stage == Stage::Vertex && (word & 0xff800000u) == 0xbe800000u &&
		    ((word >> 8) & 255) == 33) {
			const uint32_t source = word & 255, link = (word >> 16) & 127;
			std::array<uint32_t, 2> pointer;
			if (source % 2 || link % 2 || link + 2 > 104 || !memory ||
			    !frozen.Words(source, pointer, error)) {
				if (error.empty()) error = "invalid static GCN fetch pointer/link group";
				return false;
			}
			const uint64_t address = pointer[0] | (uint64_t(pointer[1]) << 32);
			if (!address || address % 4 || address >= (1ull << 40)) {
				error = "invalid static GCN fetch address";
				return false;
			}
			std::vector<uint32_t> fetch;
			bool returned = false;
			const auto read_word = [&](uint32_t& out) {
				const size_t offset = fetch.size() * 4;
				if (fetch.size() >= 1024 || offset + 4 > (1ull << 40) - address ||
				    !memory(address + offset, {reinterpret_cast<uint8_t*>(&out), 4})) return false;
				fetch.push_back(out);
				return true;
			};
			while (fetch.size() < 1024) {
				uint32_t instruction;
				if (!read_word(instruction)) break;
				if (instruction == (0xbe802000u | link)) { returned = true; break; }
				if (instruction == 0xbf8c007fu || instruction == 0xbf8c0f70u || instruction == 0xbf8c0070u)
					continue;
				if ((instruction & 0xf8000000u) == 0xc0000000u && ((instruction >> 22) & 31) <= 4)
					continue;
				if ((instruction & 0xfc000000u) == 0xe0000000u ||
				    (instruction & 0xfc000000u) == 0xe8000000u) {
					uint32_t operand;
					if (!read_word(operand)) break;
					continue;
				}
				break; // No ALU/branches/exports/nested calls in this fetch profile.
			}
			if (!returned || read_bytes + fetch.size() * 4 > 4096 || frozen.reads.size() >= 64) {
				error = "unsupported/truncated GCN fetch body or snapshot budget";
				return false;
			}
			// Confirm the discovered function as one complete range. Neither the
			// pointer nor successful word probes alone prove that payload mapped.
			std::vector<uint32_t> snapshot(fetch.size());
			if (!memory(address, {reinterpret_cast<uint8_t*>(snapshot.data()), snapshot.size() * 4}) ||
			    snapshot != fetch) {
				error = "GCN fetch mapping/data changed during snapshot";
				return false;
			}
			FrozenScalars called = frozen;
			called.prefix_instructions = 0;
			called.values[link].reset();
			called.values[link + 1].reset(); // Return PC is opaque and cannot be observed by VS.
			called.scalar_registers = std::max({called.scalar_registers, source + 2, link + 2});
			called.reads.push_back({address, uint32_t(fetch.size() * 4)});
			fetch.pop_back(); // Validated S_SETPC is represented by returning to the VS body.
			std::vector<uint32_t> inlined;
			if (!FreezeScalarPrelude(fetch, {}, memory, called, inlined, error, stage, false)) return false;
			fetch_body.assign(inlined.begin() + called.prefix_instructions * 2, inlined.end());
			// After the initial S_LOAD prelude, only read-only vector loads and
			// waits are legal. Reject interleaved loads instead of speculating
			// descriptor mutations across fetch operations.
			for (size_t i = 0; i < fetch_body.size();) {
				const uint32_t instruction = fetch_body[i];
				if (instruction == 0xbf8c007fu || instruction == 0xbf8c0f70u || instruction == 0xbf8c0070u) { ++i; continue; }
				if (((instruction & 0xfc000000u) != 0xe0000000u &&
				     (instruction & 0xfc000000u) != 0xe8000000u) || i + 1 >= fetch_body.size()) {
					error = "GCN fetch requires a scalar prelude followed by vector loads";
					return false;
				}
				// Detailed controls/opcode/register checks happen in Gcn::Decode.
				i += 2;
			}
			frozen = std::move(called);
			frozen.prefix_instructions = 0;
			++first_body;
			break; // Exactly one initial fetch call, before VS control flow.
		}
		if ((word & 0xf8000000u) != 0xc0000000u || op > 4) break;
		const uint32_t base = ((word >> 9) & 63) * 2, dst = (word >> 15) & 127;
		const uint32_t count = 1u << op;
		std::array<uint32_t, 2> pointer;
		if (!(word & 0x100) || dst + count > 104 || !memory ||
		    !frozen.Words(base, pointer, error)) {
			if (error.empty()) error = "S_LOAD prelude requires immutable pointer and memory reader";
			return false;
		}
		const uint64_t address = pointer[0] | (uint64_t(pointer[1]) << 32);
		const uint64_t offset = uint64_t(word & 255) * 4, bytes = uint64_t(count) * 4;
		if (!address || address % 4 || address >= (1ull << 40) ||
		    offset + bytes > (1ull << 40) - address || read_bytes + bytes > 4096 ||
		    frozen.reads.size() >= 64) {
			error = "S_LOAD prelude pointer/range/snapshot budget exceeded";
			return false;
		}
		std::array<uint32_t, 16> payload {};
		if (!memory(address + offset, {reinterpret_cast<uint8_t*>(payload.data()), size_t(bytes)})) {
			error = "S_LOAD table payload unreadable";
			return false;
		}
		// Load the whole group before publishing any destination, including an
		// overlapping pointer/destination group. Failed reads publish nothing.
		for (uint32_t n = 0; n < count; ++n) frozen.values[dst + n] = payload[n];
		frozen.scalar_registers = std::max({frozen.scalar_registers, base + 2, dst + count});
		frozen.reads.push_back({address + offset, uint32_t(bytes)});
		read_bytes += bytes;
		++first_body;
	}
	for (uint32_t reg = 0; reg < frozen.values.size(); ++reg)
		if (frozen.values[reg]) {
			normalized.push_back(0xbe8003ffu | reg << 16);
			normalized.push_back(*frozen.values[reg]);
			++frozen.prefix_instructions;
		}
	if (fetch_body.size() + code.size() - first_body > 4096 - normalized.size()) {
		error = "GCN normalized code budget exceeded";
		return false;
	}
	normalized.insert(normalized.end(), fetch_body.begin(), fetch_body.end());
	normalized.insert(normalized.end(), code.begin() + first_body, code.end());
	return true;
}
bool CanonicalResources(std::vector<uint32_t>& code, Stage stage, const IR::Program& program,
                        std::string& error) {
	std::map<uint32_t, uint32_t> binding, sets;
	size_t                       types = 0;
	for (size_t i = 5; i < code.size(); i += code[i] >> 16) {
		const uint32_t n = code[i] >> 16, op = code[i] & 65535;
		if (!n || n > code.size() - i) return false;
		if (op == 71 && n == 4) {
			if (code[i + 2] == 33) binding[code[i + 1]] = code[i + 3];
			if (code[i + 2] == 34) sets[code[i + 1]] = code[i + 3];
		}
		if (!types && op >= 19 && op <= 39) types = i;
	}
	const auto     shader        = stage == Stage::Vertex ? ShaderType::Vertex : ShaderType::Pixel;
	const uint32_t native_buffer = IR::NativeBinding(shader, IR::DescriptorBindingKind::Buffers);
	const uint32_t native_sampler = IR::NativeBinding(shader, IR::DescriptorBindingKind::Samplers);
	uint32_t       native_image  = UINT32_MAX;
	if (!program.info.images.empty())
		native_image =
		    IR::NativeBinding(shader, *IR::DescriptorBindingForImage(program.info.images[0]));
	std::map<uint32_t, uint32_t> canonical;
	uint32_t                     buffer_var = 0;
	for (auto [var, old]: binding) {
#ifdef KYTY_GPU_BACKEND_D3D12
		old = sets[var];
#endif
		if (old == native_buffer) {
			canonical[var] = stage == Stage::Vertex ? 60 : 58;
			buffer_var     = var;
		} else if (old == native_image)
			canonical[var] = stage == Stage::Vertex ? 65 : 64;
		else if (old == native_sampler && !program.info.samplers.empty())
			canonical[var] = stage == Stage::Vertex ? 67 : 66;
		else {
			error = "unexpected GCN resource binding";
			return false;
		}
	}
	bool readonly = false;
	for (size_t i = 5; i < code.size(); i += code[i] >> 16) {
		if ((code[i] & 65535) != 71 || !canonical.contains(code[i + 1])) continue;
		if (code[i + 2] == 33) code[i + 3] = canonical[code[i + 1]];
		if (code[i + 2] == 34) code[i + 3] = 0;
		if (code[i + 1] == buffer_var && code[i + 2] == 24) readonly = true;
	}
	if (buffer_var && !readonly)
		code.insert(code.begin() + types, {(3u << 16) | 71u, buffer_var, 24});
	return true;
}
} // namespace

bool Compile(std::span<const uint32_t> code, Stage stage, std::vector<uint32_t>& spirv,
             std::string& error, std::span<const uint32_t> user_data, uint32_t parameter_count,
             BufferDescriptor* buffer, std::span<const uint32_t> parameter_map, bool no_perspective,
             std::vector<BufferDescriptor>* resources, ImageDescriptor* image,
             const ShaderMemory& memory, std::vector<ScalarRead>* scalar_reads,
             std::vector<uint32_t>* normalized_code, uint32_t* scalar_registers) {
	error.clear();
	if (!ValidateParameterMap(parameter_map, parameter_count, error)) return false;
	if (stage != Stage::Vertex && stage != Stage::Pixel) {
		error = "unsupported GCN stage";
		return false;
	}
	if (user_data.size() > 16 || code.empty() || code.size() > 4096) {
		error = "GCN code/user data budget exceeded";
		return false;
	}
	FrozenScalars frozen;
	std::vector<uint32_t> specialized;
	if (!FreezeScalarPrelude(code, user_data, memory, frozen, specialized, error, stage)) return false;
	code = specialized;
	Decoder::Program decoded;
	if (!Decode(code, stage, decoded, error, parameter_count)) return false;
	std::map<uint32_t, BufferDescriptor> by_pc;
	std::bitset<104>                     descriptor_mask;
	ImageDescriptor                      image_descriptor;
	bool                                 uses_image = false, uses_sampler = false;
	for (const auto& inst: decoded.instructions) {
		if (inst.family == Decoder::Family::MIMG) {
			std::array<uint32_t, 8> words;
			ImageDescriptor current;
			if (!image || !frozen.Words(inst.src1.reg, words, error) ||
			    !DecodeImageDescriptor(words, current, error)) {
				if (error.empty()) error = "image instruction requires descriptor output";
				return false;
			}
			if (uses_image && (current.address != image_descriptor.address ||
			                   current.width != image_descriptor.width ||
			                   current.height != image_descriptor.height ||
			                   current.pitch != image_descriptor.pitch)) {
				error = "multiple different GCN images require a resource list ABI";
				return false;
			}
			image_descriptor = current;
			if (inst.opcode == Decoder::Opcode::IMAGE_SAMPLE) {
				std::array<uint32_t, 4> sampler;
				if (!frozen.Words(inst.src2.reg, sampler, error)) return false;
				if (sampler != std::array<uint32_t, 4> {0x92, 0, 0, 0}) {
					error = "unsupported GCN sampler (nearest/clamp, normalized, LOD0 only)";
					return false;
				}
				uses_sampler = true;
				for (uint32_t n = 0; n < 4; ++n) descriptor_mask.set(inst.src2.reg + n);
			}
			uses_image = true;
			for (uint32_t n = 0; n < 8; ++n) descriptor_mask.set(inst.src1.reg + n);
			continue;
		}
		const bool vector_load = inst.family == Decoder::Family::MUBUF ||
		                         inst.family == Decoder::Family::MTBUF;
		if (!vector_load && inst.family != Decoder::Family::SMEM) continue;
		if (inst.family == Decoder::Family::SMEM && inst.opcode_id <= 4) {
			error = "S_LOAD requires an initial immutable scalar prelude";
			return false;
		}
		const uint32_t   base = vector_load ? inst.src1.reg : inst.src0.reg;
		BufferDescriptor descriptor;
		std::array<uint32_t, 4> words;
		if ((!buffer && !resources) || !frozen.Words(base, words, error) ||
		    !DecodeBufferDescriptor(words, descriptor, error, vector_load)) {
			if (error.empty()) error = "load requires immutable direct user descriptor";
			return false;
		}
		if ((vector_load && (inst.offset + inst.data_dwords * 4 > descriptor.stride)) ||
		    (!vector_load && uint64_t(inst.src1.value) + uint64_t(inst.data_dwords) * 4 >
		                         descriptor.size)) {
			error = "buffer load outside descriptor record/range";
			return false;
		}
		for (uint32_t n = 0; n < 4; ++n) descriptor_mask.set(base + n);
		by_pc.emplace(inst.pc, descriptor);
	}
	const bool uses_buffer = !by_pc.empty();
	if (uses_buffer || uses_image)
		for (size_t i = frozen.prefix_instructions; i < decoded.instructions.size(); ++i) {
			const auto& inst = decoded.instructions[i];
			if (inst.dst.kind != Decoder::OperandKind::Sgpr) continue;
			const uint32_t count = (inst.family == Decoder::Family::SMEM || inst.opcode == Decoder::Opcode::S_AND_SAVEEXEC_B64) ? inst.data_dwords : 1;
			for (uint32_t n = 0; n < count; ++n)
				if (descriptor_mask.test(inst.dst.reg + n)) {
					error = "buffer descriptor SGPR mutation unsupported";
					return false;
				}
		}
	ShaderVertexInputInfo vertex;
	ShaderPixelInputInfo  pixel;
	CompileOptions        options;
	pixel.input_num         = parameter_count;
	pixel.ps_no_perspective = no_perspective;
	for (uint32_t i = 0; i < parameter_count; ++i)
		pixel.interpolator_settings[i] = parameter_map.empty() ? i : parameter_map[i];
	// Canonical P1/P2 become a native interpolant (perspective or linear central).
	// Neither instruction reads the pair here: prologue barycentrics are removed by DCE.
	if (parameter_count) pixel.ps_perspective_center_vgpr = 0;
	options.stage             = stage == Stage::Vertex ? ShaderType::Vertex : ShaderType::Pixel;
	options.gcn_vertex_inputs = true;
	options.wave_size         = 64;
	options.dump_ir           = false;
	if (stage == Stage::Vertex)
		options.input_info.vertex = &vertex;
	else
		options.input_info.pixel = &pixel;
	if (stage == Stage::Pixel)
		for (auto& inst: decoded.instructions) {
			if (inst.family != Decoder::Family::VINTRP) continue;
			const uint32_t attr = inst.src1.value, channel = inst.src2.value;
			const uint32_t control = parameter_map.empty() ? attr : parameter_map[attr];
			const bool use_default = (control & 0x20) != 0, flat = (control & 0x400) != 0;
			if (use_default) {
				if (inst.opcode == Decoder::Opcode::V_INTERP_P1_F32) {
					inst.family = Decoder::Family::SOPP;
					inst.opcode = Decoder::Opcode::S_NOP;
					inst.dst = inst.src0 = inst.src1 = inst.src2 = inst.src3 = {};
					inst.src_count = 0;
				} else {
					const uint32_t selector = (control >> 8) & 3;
					const bool one = channel == 3 ? (selector & 1) : (selector & 2);
					inst.family = Decoder::Family::VOP1;
					inst.opcode = Decoder::Opcode::V_MOV_B32;
					inst.src0 = inst.src1 = inst.src2 = inst.src3 = {};
					inst.src0.kind = Decoder::OperandKind::LiteralConstant;
					inst.src0.value = one ? 0x3f800000u : 0;
					inst.src_count = 1;
				}
			} else if (inst.opcode == Decoder::Opcode::V_INTERP_MOV_F32) {
				if (!flat) {
					error = "GCN V_INTERP_MOV P0 requires flat PARAM in this profile";
					return false;
				}
				// First provoking vertex is fixed by the current triangle raster
				// contract. A native flat attribute needs no barycentric extension.
				inst.opcode = Decoder::Opcode::V_INTERP_P2_F32;
			} else if (flat) {
				error = "flat GCN PARAM requires V_INTERP_MOV P0";
				return false;
			}
		}
	auto                          translated = TranslateProgram(decoded, options);
	IR::ResourceSpecialization    specialization;
	std::vector<BufferDescriptor> descriptors;
	if (uses_buffer) {
		for (const auto& resource: translated.program.info.buffers) {
			auto found = by_pc.find(resource.first_use_pc);
			if (found == by_pc.end() || descriptors.size() == 2) {
				error = "unexpected GCN resource origin/count";
				return false;
			}
			descriptors.push_back(found->second);
			IR::ResourceSpecialization::Buffer value;
			value.packed_stride = found->second.stride;
			specialization.buffers.push_back(value);
		}
		if (descriptors.empty() || (descriptors.size() > 1 && !resources)) {
			error = "multiple GCN buffers require resource list output";
			return false;
		}
		// Clamp the original unsigned index before 32-bit stride multiplication.
		// The sentinel record lies just beyond this binding and returns zero for all components.
		for (auto* block: translated.program.blocks)
			for (auto it = block->begin(); it != block->end(); ++it) {
				if (it->GetOpcode() != IR::ValueOpcode::LoadBufferU32 &&
				    it->GetOpcode() != IR::ValueOpcode::LoadBufferU32x2 &&
				    it->GetOpcode() != IR::ValueOpcode::LoadBufferU32x3 &&
				    it->GetOpcode() != IR::ValueOpcode::LoadBufferU32x4)
					continue;
				const auto& memory =
				    translated.program.memory_info[it->Flags<IR::MemoryFlags>().index];
				if (memory.resource >= descriptors.size()) {
					error = "invalid GCN buffer resource index";
					return false;
				}
				const auto& descriptor = descriptors[memory.resource];
				const auto  safe       = block->PrependNewInst(
                    it, IR::ValueOpcode::UMin32,
                    {it->Arg(1), IR::Value(descriptor.size / descriptor.stride)});
				it->SetArg(1, IR::Value(&*safe));
			}
	}
	if (uses_image) {
		if (translated.program.info.images.size() != 1) {
			error = "unexpected GCN image count";
			return false;
		}
		IR::ResourceSpecialization::Image value;
		value.numeric_class = Prospero::TextureNumericClass::Float;
		value.dimension     = Decoder::ImageDimension::Dim2D;
		specialization.images.push_back(value);
		for (auto* block: translated.program.blocks)
			for (auto it = block->begin(); it != block->end(); ++it) {
				if (it->GetOpcode() != IR::ValueOpcode::ImageRead) continue;
				const auto* address = it->Arg(1).ResolveInstruction();
				const auto  x =
				    block->PrependNewInst(it, IR::ValueOpcode::ULessThan32,
				                          {address->Arg(0), IR::Value(image_descriptor.width)});
				const auto y =
				    block->PrependNewInst(it, IR::ValueOpcode::ULessThan32,
				                          {address->Arg(1), IR::Value(image_descriptor.height)});
				const auto xy     = block->PrependNewInst(it, IR::ValueOpcode::LogicalAnd,
				                                          {IR::Value(&*x), IR::Value(&*y)});
				const auto active = block->PrependNewInst(it, IR::ValueOpcode::LogicalAnd,
				                                          {it->Arg(2), IR::Value(&*xy)});
				it->SetArg(2, IR::Value(&*active));
			}
	}
	if (uses_sampler) {
		if (translated.program.info.samplers.size() != 1) {
			error = "unexpected GCN sampler count";
			return false;
		}
		specialization.samplers.resize(1);
		specialization.unnormalized_samplers.push_back(0);
	}
	auto result = CompileProgram(std::move(translated), options, specialization,
	                             stage == Stage::Vertex ? 4 : 0);
	if (uses_buffer || uses_image) {
		uint64_t mask = uses_buffer ? 1 : 0;
		if (uses_sampler) mask |= uint64_t(1) << uint32_t(IR::DescriptorBindingKind::Samplers);
		if (uses_image)
			mask |= uint64_t(1) << uint32_t(
			            *IR::DescriptorBindingForImage(result.program.info.images[0]));
		if (result.program.bindings.descriptor_mask != mask ||
		    (uses_buffer && (result.program.bindings.ShaderDataDwords() != descriptors.size() + 1 ||
		                     !result.program.bindings.UsesPushData()))) {
			error = "unsupported GCN resource ABI";
			return false;
		}
		if (!CanonicalResources(result.spirv, stage, result.program, error)) return false;
	}

	if (scalar_registers) *scalar_registers = frozen.scalar_registers;
	if (scalar_reads) *scalar_reads = std::move(frozen.reads);
	if (normalized_code) *normalized_code = specialized;
	spirv = std::move(result.spirv);
	if (buffer) *buffer = descriptors.empty() ? BufferDescriptor {} : descriptors.front();
	if (resources) *resources = std::move(descriptors);
	if (image) *image = image_descriptor;
	return true;
}
} // namespace Libs::Graphics::ShaderRecompiler::Gcn
