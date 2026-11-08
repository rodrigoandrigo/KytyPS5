#include "graphics/shader/recompiler/frontend/decode/GcnDecoder.h"

#include <array>
#include <bit>
#include <bitset>
#include <map>
#include <optional>
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
bool Source(uint32_t bits, std::span<const uint32_t> code, size_t& next, Operand& op,
            std::optional<uint32_t>& literal) {
	if (bits < 104) {
		op.kind = OperandKind::Sgpr;
		op.reg  = bits;
		return true;
	}
	if (bits >= 256) {
		op = Vector(bits - 256);
		return true;
	}
	if (bits == 255) {
		if (!literal) {
			if (next == code.size()) return false;
			literal = code[next++];
		}
		op.kind  = OperandKind::LiteralConstant;
		op.value = *literal;
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
	struct Effects {
		bool scc_read = false, scc_write = false;
	};
	std::vector<Effects>       effects;
	std::map<uint32_t, size_t> boundaries;
	for (size_t pc = 0; pc < code.size();) {
		const uint32_t          word = code[pc];
		size_t                  next = pc + 1;
		Instruction             inst;
		Effects                 effect;
		std::optional<uint32_t> literal;
		inst.pc           = uint32_t(pc * 4);
		const auto source = [&](uint32_t bits, Operand& operand) {
			return Source(bits, code, next, operand, literal);
		};
		const auto scalar_dst = [&](uint32_t reg) {
			if (reg >= 104) return false;
			inst.dst.kind = OperandKind::Sgpr;
			inst.dst.reg  = reg;
			return true;
		};
		if ((word & 0xff800000u) == 0xbf800000u) {
			inst.family       = Family::SOPP;
			inst.opcode_id    = (word >> 16) & 127;
			const int32_t imm = int16_t(word & 0xffff);
			switch (inst.opcode_id) {
				case 0:
					inst.opcode     = Opcode::S_NOP;
					inst.src_count  = 1;
					inst.src0.kind  = OperandKind::LiteralConstant;
					inst.src0.value = uint32_t(imm);
					break;
				case 1:
					if (imm) return fail(pc, "s_endpgm reserved immediate");
					inst.opcode = Opcode::S_ENDPGM;
					break;
				case 2: inst.opcode = Opcode::S_BRANCH; break;
				case 4:
					inst.opcode     = Opcode::S_CBRANCH_SCC0;
					effect.scc_read = true;
					break;
				case 5:
					inst.opcode     = Opcode::S_CBRANCH_SCC1;
					effect.scc_read = true;
					break;
				default: return fail(pc, "unsupported GCN scalar control");
			}
			if (inst.opcode_id == 2 || inst.opcode_id == 4 || inst.opcode_id == 5) {
				const int64_t target = int64_t(inst.pc) + 4 + int64_t(imm) * 4;
				// Forward control flow is finite; loops require a separate bound proof.
				if (target < int64_t(inst.pc) + 4 || target >= int64_t(code.size_bytes()))
					return fail(pc, "backward or out-of-range branch");
				inst.branch_target = uint32_t(target);
			}
		} else if ((word & 0xff800000u) == 0xbe800000u) {
			inst.family    = Family::SOP1;
			inst.opcode_id = (word >> 8) & 255;
			if (inst.opcode_id == 3)
				inst.opcode = Opcode::S_MOV_B32;
			else if (inst.opcode_id == 7) {
				inst.opcode      = Opcode::S_NOT_B32;
				effect.scc_write = true;
			} else
				return fail(pc, "unsupported GCN SOP1 opcode");
			if (!scalar_dst((word >> 16) & 127) || !source(word & 255, inst.src0))
				return fail(pc, "invalid scalar move operand");
			inst.src_count = 1;
		} else if ((word & 0xff800000u) == 0xbf000000u) {
			inst.family    = Family::SOPC;
			inst.opcode_id = (word >> 16) & 127;
			constexpr Opcode comparisons[] {
			    Opcode::S_CMP_EQ_I32, Opcode::S_CMP_LG_I32, Opcode::S_CMP_GT_I32,
			    Opcode::S_CMP_GE_I32, Opcode::S_CMP_LT_I32, Opcode::S_CMP_LE_I32,
			    Opcode::S_CMP_EQ_U32, Opcode::S_CMP_LG_U32, Opcode::S_CMP_GT_U32,
			    Opcode::S_CMP_GE_U32, Opcode::S_CMP_LT_U32, Opcode::S_CMP_LE_U32};
			if (inst.opcode_id >= std::size(comparisons))
				return fail(pc, "unsupported GCN scalar comparison");
			inst.opcode      = comparisons[inst.opcode_id];
			inst.src_count   = 2;
			effect.scc_write = true;
			if (!source(word & 255, inst.src0) || !source((word >> 8) & 255, inst.src1))
				return fail(pc, "invalid scalar comparison source");
		} else if ((word & 0xf0000000u) == 0xb0000000u) {
			if (((word >> 23) & 31) != 0 || !scalar_dst((word >> 16) & 127))
				return fail(pc, "unsupported SOPK/destination");
			inst.family          = Family::SOPK;
			inst.opcode          = Opcode::S_MOVK_I32;
			inst.src_count       = 1;
			inst.src0.kind       = OperandKind::LiteralConstant;
			inst.src0.signed_val = int16_t(word & 0xffff);
			inst.src0.value      = uint32_t(inst.src0.signed_val);
		} else if ((word & 0xc0000000u) == 0x80000000u) {
			inst.family    = Family::SOP2;
			inst.opcode_id = (word >> 23) & 127;
			switch (inst.opcode_id) {
				case 0: inst.opcode = Opcode::S_ADD_U32; break;
				case 1: inst.opcode = Opcode::S_SUB_U32; break;
				case 2: inst.opcode = Opcode::S_ADD_I32; break;
				case 3: inst.opcode = Opcode::S_SUB_I32; break;
				case 6: inst.opcode = Opcode::S_MIN_I32; break;
				case 7: inst.opcode = Opcode::S_MIN_U32; break;
				case 8: inst.opcode = Opcode::S_MAX_I32; break;
				case 9: inst.opcode = Opcode::S_MAX_U32; break;
				case 10:
					inst.opcode     = Opcode::S_CSELECT_B32;
					effect.scc_read = true;
					break;
				case 14: inst.opcode = Opcode::S_AND_B32; break;
				case 16: inst.opcode = Opcode::S_OR_B32; break;
				case 18: inst.opcode = Opcode::S_XOR_B32; break;
				case 30: inst.opcode = Opcode::S_LSHL_B32; break;
				case 32: inst.opcode = Opcode::S_LSHR_B32; break;
				case 34: inst.opcode = Opcode::S_ASHR_I32; break;
				case 38: inst.opcode = Opcode::S_MUL_I32; break;
				default: return fail(pc, "unsupported GCN SOP2 opcode");
			}
			effect.scc_write = inst.opcode_id != 10 && inst.opcode_id != 38;
			inst.src_count   = 2;
			if (!scalar_dst((word >> 16) & 127) || !source(word & 255, inst.src0) ||
			    !source((word >> 8) & 255, inst.src1))
				return fail(pc, "invalid scalar ALU operand");
		} else if ((word & 0xfc000000u) == 0xf8000000u) {
			if (next == code.size()) return fail(pc, "truncated export");
			const uint32_t target = stage == Stage::Vertex ? 12 : 0;
			const uint32_t expected =
			    0xf800080fu | (target << 4) | (stage == Stage::Pixel ? 0x1000 : 0);
			if (word != expected) return fail(pc, "unsupported export controls/target");
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
		} else {
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
					case 5: inst.opcode = Opcode::V_SUBREV_F32; break;
					case 8: inst.opcode = Opcode::V_MUL_F32; break;
					case 26: inst.opcode = Opcode::V_LSHLREV_B32; break;
					case 27: inst.opcode = Opcode::V_AND_B32; break;
					case 28: inst.opcode = Opcode::V_OR_B32; break;
					case 29: inst.opcode = Opcode::V_XOR_B32; break;
					default: return fail(pc, "unsupported GCN VOP2 opcode");
				}
				inst.src1      = Vector((word >> 9) & 255);
				inst.src_count = 2;
			} else
				return fail(pc, "unsupported GCN family");
			if (!source(word & 511, inst.src0))
				return fail(pc, "invalid or truncated vector source");
		}
		inst.word_count = uint32_t(next - pc);
		for (size_t i = pc; i < next; ++i)
			inst.raw[i - pc] = code[i];
		boundaries.emplace(inst.pc, program.instructions.size());
		program.instructions.push_back(inst);
		effects.push_back(effect);
		pc = next;
	}
	// Definite-assignment intersection over the forward CFG, not lexical order.
	constexpr size_t Scc = 360;
	struct Flow {
		std::bitset<361> defined;
		bool             may_export = false, must_export = false;
	};
	std::vector<std::optional<Flow>> incoming(program.instructions.size());
	incoming[0] = Flow {};
	if (stage == Stage::Vertex) incoming[0]->defined[0] = true;
	bool       exit  = false;
	const auto merge = [&](size_t to, const Flow& flow) {
		if (!incoming[to])
			incoming[to] = flow;
		else {
			incoming[to]->defined &= flow.defined;
			incoming[to]->may_export |= flow.may_export;
			incoming[to]->must_export &= flow.must_export;
		}
	};
	for (size_t i = 0; i < program.instructions.size(); ++i) {
		const auto& inst = program.instructions[i];
		const bool  branch =
		    inst.opcode == Opcode::S_BRANCH || effects[i].scc_read && inst.family == Family::SOPP;
		size_t target = 0;
		if (branch) {
			auto it = boundaries.find(inst.branch_target);
			if (it == boundaries.end())
				return fail(inst.pc / 4, "branch into literal/non-instruction");
			target = it->second;
		}
		if (!incoming[i]) return fail(inst.pc / 4, "unreachable instruction/trailing code");
		auto          flow = *incoming[i];
		const Operand sources[] {inst.src0, inst.src1, inst.src2, inst.src3};
		for (uint32_t n = 0; n < inst.src_count; ++n) {
			const auto&  source = sources[n];
			const size_t reg    = source.kind == OperandKind::Sgpr ? source.reg + 256 : source.reg;
			if ((source.kind == OperandKind::Vgpr || source.kind == OperandKind::Sgpr) &&
			    !flow.defined[reg])
				return fail(inst.pc / 4, "read of undefined register on a control-flow path");
		}
		if (effects[i].scc_read && !flow.defined[Scc]) return fail(inst.pc / 4, "undefined SCC");
		if (inst.dst.kind == OperandKind::Vgpr) flow.defined[inst.dst.reg] = true;
		if (inst.dst.kind == OperandKind::Sgpr) flow.defined[256 + inst.dst.reg] = true;
		if (effects[i].scc_write) flow.defined[Scc] = true;
		if (inst.opcode == Opcode::EXP) {
			if (flow.may_export) return fail(inst.pc / 4, "duplicate export on a path");
			flow.may_export = flow.must_export = true;
		} else if (flow.may_export && inst.opcode != Opcode::S_ENDPGM && !branch &&
		           inst.opcode != Opcode::S_NOP)
			return fail(inst.pc / 4, "ALU after done export");
		if (inst.opcode == Opcode::S_ENDPGM) {
			if (!flow.must_export) return fail(inst.pc / 4, "path ends without required export");
			exit = true;
			continue;
		}
		if (branch) merge(target, flow);
		if (inst.opcode != Opcode::S_BRANCH) {
			if (i + 1 == program.instructions.size())
				return fail(inst.pc / 4, "path falls off shader");
			merge(i + 1, flow);
		}
	}
	if (!exit) return fail(code.size(), "missing reachable s_endpgm");
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
	        Vop1(6, 3, 259),     Vop2(8, 2, 244, 2),  Vop2(8, 3, 244, 3),  Vop1(1, 5, 242),
	        Vop2(4, 2, 258, 5),  Vop2(4, 3, 259, 5),  Vop1(1, 4, 128),     0xf80008cfu,
	        0x05040302u,         0xbf810000u};
}
std::vector<uint32_t> SolidPixelFixture(float red, float green, float blue, float alpha) {
	return {Vop1(1, 0, 255), std::bit_cast<uint32_t>(red),
	        Vop1(1, 1, 255), std::bit_cast<uint32_t>(green),
	        Vop1(1, 2, 255), std::bit_cast<uint32_t>(blue),
	        Vop1(1, 3, 255), std::bit_cast<uint32_t>(alpha),
	        0xf800180fu,     0x03020100u,
	        0xbf810000u};
}
std::vector<uint32_t> UniformBranchPixelFixture() {
	return {0x80018100u,
	        0xbf068101u,
	        0xbf850003u,
	        Vop1(1, 0, 255),
	        std::bit_cast<uint32_t>(.75f),
	        0xbf820002u,
	        Vop1(1, 0, 255),
	        std::bit_cast<uint32_t>(.25f),
	        Vop1(1, 1, 240),
	        Vop1(1, 2, 255),
	        std::bit_cast<uint32_t>(.75f),
	        Vop1(1, 3, 242),
	        0xf800180fu,
	        0x03020100u,
	        0xbf810000u};
}
} // namespace Libs::Graphics::ShaderRecompiler::Gcn
