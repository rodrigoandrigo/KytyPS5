#include "graphics/shader/recompiler/frontend/decode/GcnDecoder.h"

#include <array>
#include <bit>
#include <string>

namespace Libs::Graphics::ShaderRecompiler::Gcn {
namespace {
using namespace Decoder;
Operand Vector(uint32_t reg) {
	Operand op;
	op.kind = OperandKind::Vgpr;
	op.reg  = reg;
	return op;
}
bool Source(uint32_t bits, std::span<const uint32_t> code, size_t& next, Operand& op) {
	if (bits >= 256) {
		op = Vector(bits - 256);
		return true;
	}
	if (bits == 255) {
		if (next == code.size()) return false;
		op.kind  = OperandKind::LiteralConstant;
		op.value = code[next++];
	} else if (bits >= 128 && bits <= 208) {
		op.kind       = OperandKind::IntegerInlineConstant;
		op.signed_val = bits <= 192 ? int32_t(bits - 128) : 192 - int32_t(bits);
		op.value      = uint32_t(op.signed_val);
	} else if (bits >= 240 && bits <= 247) {
		constexpr float values[] {0.5f, -0.5f, 1.f, -1.f, 2.f, -2.f, 4.f, -4.f};
		op.kind  = OperandKind::FloatInlineConstant;
		op.value = std::bit_cast<uint32_t>(values[bits - 240]);
	} else
		return false; // SGPRs, special operands, SDWA/DPP and extensions require separate
		              // validation.
	return true;
}
uint32_t Vop1(uint32_t op, uint32_t dst, uint32_t source) {
	return 0x7e000000u | dst << 17 | op << 9 | source;
}
uint32_t Vop2(uint32_t op, uint32_t dst, uint32_t source, uint32_t vector) {
	return op << 25 | dst << 17 | vector << 9 | source;
}
} // namespace

bool Decode(std::span<const uint32_t> code, Stage stage, Decoder::Program& output,
            std::string& error) {
	error.clear();
	const auto fail = [&](size_t pc, const char* reason) {
		error = "GCN pc=" + std::to_string(pc * 4) + ": " + reason;
		return false;
	};
	if (stage != Stage::Vertex && stage != Stage::Pixel) return fail(0, "unsupported stage");
	if (code.empty() || code.size() > 4096) return fail(0, "empty or oversized shader");
	Decoder::Program program;
	program.code = code;
	std::array<bool, 256> defined {};
	if (stage == Stage::Vertex) defined[0] = true;
	bool exported = false, ended = false;
	for (size_t pc = 0; pc < code.size();) {
		const uint32_t word = code[pc];
		size_t         next = pc + 1;
		Instruction    inst;
		inst.pc = uint32_t(pc * 4);
		if (word == 0xbf810000u) {
			if (!exported || next != code.size())
				return fail(pc, "end requires one export and no trailing code");
			inst.family    = Family::SOPP;
			inst.opcode    = Opcode::S_ENDPGM;
			inst.opcode_id = 1;
			ended          = true;
		} else if ((word & 0xfc000000u) == 0xf8000000u) {
			if (exported || next == code.size()) return fail(pc, "duplicate or truncated export");
			const uint32_t target = stage == Stage::Vertex ? 12 : 0;
			const uint32_t expected =
			    0xf800080fu | (target << 4) | (stage == Stage::Pixel ? 0x1000 : 0);
			if (word != expected)
				return fail(pc, "only full uncompressed done POS0/MRT0 export is supported");
			const uint32_t sources = code[next++];
			inst.family            = Family::EXP;
			inst.opcode            = Opcode::EXP;
			inst.opcode_id         = target;
			inst.exp               = {.target = target,
			                          .en     = 15,
			                          .done   = true,
			                          .compr  = false,
			                          .vm     = stage == Stage::Pixel};
			inst.src_count         = 4;
			inst.src0              = Vector(sources & 255);
			inst.src1              = Vector((sources >> 8) & 255);
			inst.src2              = Vector((sources >> 16) & 255);
			inst.src3              = Vector(sources >> 24);
			exported               = true;
		} else {
			if (exported) return fail(pc, "instructions after done export are not supported");
			inst.dst = Vector((word >> 17) & 255);
			if ((word & 0xfe000000u) == 0x7e000000u) {
				inst.family    = Family::VOP1;
				inst.opcode_id = (word >> 9) & 255;
				if (inst.opcode_id == 1)
					inst.opcode = Opcode::V_MOV_B32;
				else if (inst.opcode_id == 6)
					inst.opcode = Opcode::V_CVT_F32_U32;
				else
					return fail(pc, "unsupported GCN VOP1 opcode");
				inst.src_count = 1;
			} else if (!(word & 0x80000000u)) {
				inst.family    = Family::VOP2;
				inst.opcode_id = (word >> 25) & 63;
				switch (inst.opcode_id) {
					case 3: inst.opcode = Opcode::V_ADD_F32; break;
					case 4: inst.opcode = Opcode::V_SUB_F32; break;
					case 8: inst.opcode = Opcode::V_MUL_F32; break;
					case 26: inst.opcode = Opcode::V_LSHLREV_B32; break;
					case 27: inst.opcode = Opcode::V_AND_B32; break;
					default: return fail(pc, "unsupported GCN VOP2 opcode");
				}
				inst.src1      = Vector((word >> 9) & 255);
				inst.src_count = 2;
			} else
				return fail(pc, "unsupported GCN family/control flow");
			if (!Source(word & 511, code, next, inst.src0))
				return fail(pc, "unsupported or truncated source");
		}
		const Operand sources[] {inst.src0, inst.src1, inst.src2, inst.src3};
		for (uint32_t i = 0; i < inst.src_count; ++i)
			if (sources[i].kind == OperandKind::Vgpr && !defined[sources[i].reg])
				return fail(pc, "read of undefined vector register");
		if (inst.dst.kind == OperandKind::Vgpr) defined[inst.dst.reg] = true;
		inst.word_count = uint32_t(next - pc);
		for (size_t i = pc; i < next; ++i)
			inst.raw[i - pc] = code[i];
		program.instructions.push_back(inst);
		pc = next;
	}
	if (!ended) return fail(code.size(), "missing s_endpgm");
	output = std::move(program);
	return true;
}

std::vector<uint32_t> TriangleVertexFixture(bool fullscreen) {
	if (!fullscreen)
		return {Vop2(26, 2, 129, 0),
		        Vop2(27, 2, 130, 2),
		        Vop2(27, 3, 130, 0),
		        Vop1(6, 2, 258),
		        Vop1(6, 3, 259),
		        Vop2(8, 2, 255, 2),
		        std::bit_cast<uint32_t>(0.75f),
		        Vop2(8, 3, 255, 3),
		        std::bit_cast<uint32_t>(0.75f),
		        Vop2(3, 2, 255, 2),
		        std::bit_cast<uint32_t>(-0.75f),
		        Vop2(3, 3, 255, 3),
		        std::bit_cast<uint32_t>(-0.75f),
		        Vop1(1, 4, 128),
		        Vop1(1, 5, 242),
		        0xf80008cfu,
		        0x05040302u,
		        0xbf810000u};
	return {Vop2(26, 2, 129, 0), Vop2(27, 2, 130, 2), Vop2(27, 3, 130, 0), Vop1(6, 2, 258),
	        Vop1(6, 3, 259),     Vop2(8, 2, 244, 2),  Vop2(8, 3, 244, 3), Vop1(1, 5, 242),
	        Vop2(4, 2, 258, 5), Vop2(4, 3, 259, 5), Vop1(1, 4, 128), 0xf80008cfu,
	        0x05040302u, 0xbf810000u};
}
std::vector<uint32_t> SolidPixelFixture(float red, float green, float blue, float alpha) {
	return {Vop1(1, 0, 255), std::bit_cast<uint32_t>(red),
	        Vop1(1, 1, 255), std::bit_cast<uint32_t>(green),
	        Vop1(1, 2, 255), std::bit_cast<uint32_t>(blue),
	        Vop1(1, 3, 255), std::bit_cast<uint32_t>(alpha),
	        0xf800180fu,     0x03020100u,
	        0xbf810000u};
}
} // namespace Libs::Graphics::ShaderRecompiler::Gcn
