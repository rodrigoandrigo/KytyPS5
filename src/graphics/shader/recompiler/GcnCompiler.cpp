#include "graphics/shader/recompiler/GcnCompiler.h"

#include "graphics/shader/recompiler/ShaderRecompiler.h"

#include <map>

namespace Libs::Graphics::ShaderRecompiler::Gcn {
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
             std::vector<BufferDescriptor>* resources, ImageDescriptor* image) {
	if ((!parameter_map.empty() && parameter_map.size() != parameter_count) ||
	    parameter_count > 4) {
		error = "invalid GCN parameter map size";
		return false;
	}
	for (uint32_t location: parameter_map) {
		if (location >= parameter_count) {
			error = "GCN parameter map outside exported slots";
			return false;
		}
	}
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
	if (!Decode(code, stage, decoded, error, parameter_count)) return false;
	std::map<uint32_t, BufferDescriptor> by_pc;
	uint32_t                             descriptor_mask = 0;
	ImageDescriptor                      image_descriptor;
	bool                                 uses_image = false;
	for (const auto& inst: decoded.instructions) {
		if (inst.family == Decoder::Family::MIMG) {
			if (!image || user_data.size() != 16 ||
			    !DecodeImageDescriptor(user_data.subspan(8, 8), image_descriptor, error)) {
				if (error.empty())
					error = "IMAGE_LOAD requires descriptor output and 16 user SGPRs";
				return false;
			}
			uses_image = true;
			descriptor_mask |= 0xff00;
			continue;
		}
		const bool vector_load = inst.family == Decoder::Family::MUBUF;
		if (!vector_load && inst.opcode != Decoder::Opcode::S_BUFFER_LOAD_DWORDX4) continue;
		const uint32_t   base = vector_load ? inst.src1.reg : inst.src0.reg;
		BufferDescriptor descriptor;
		if ((!buffer && !resources) || base + 4 > user_data.size() ||
		    !DecodeBufferDescriptor(user_data.subspan(base, 4), descriptor, error, vector_load)) {
			if (error.empty()) error = "load requires immutable direct user descriptor";
			return false;
		}
		if ((vector_load && (inst.offset + inst.data_dwords * 4 > descriptor.stride)) ||
		    (!vector_load && inst.src1.value > descriptor.size - 16)) {
			error = "buffer load outside descriptor record/range";
			return false;
		}
		descriptor_mask |= 15u << base;
		by_pc.emplace(inst.pc, descriptor);
	}
	const bool uses_buffer = !by_pc.empty();
	if (uses_buffer || uses_image)
		for (size_t i = user_data.size(); i < decoded.instructions.size(); ++i) {
			const auto& inst = decoded.instructions[i];
			if (inst.dst.kind != Decoder::OperandKind::Sgpr) continue;
			const uint32_t count = inst.opcode == Decoder::Opcode::S_BUFFER_LOAD_DWORDX4 ? 4 : 1;
			for (uint32_t n = 0; n < count; ++n)
				if (inst.dst.reg + n < 16 && (descriptor_mask & (1u << (inst.dst.reg + n)))) {
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
	auto result = CompileProgram(std::move(translated), options, specialization,
	                             stage == Stage::Vertex ? 4 : 0);
	if (uses_buffer || uses_image) {
		uint64_t mask = uses_buffer ? 1 : 0;
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

	spirv = std::move(result.spirv);
	if (buffer) *buffer = descriptors.empty() ? BufferDescriptor {} : descriptors.front();
	if (resources) *resources = std::move(descriptors);
	if (image) *image = image_descriptor;
	return true;
}
} // namespace Libs::Graphics::ShaderRecompiler::Gcn
