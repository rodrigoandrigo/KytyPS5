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
		return false; // Special operands, SDWA/DPP and extensions require separate
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
            std::string& error, uint32_t parameter_count) {
	error.clear();
	const auto fail = [&](size_t pc, const char* reason) {
		error = "GCN pc=" + std::to_string(pc * 4) + ": " + reason;
		return false;
	};
	if (parameter_count > 4) return fail(0, "too many GCN parameters");
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
				case 12:
					if (uint32_t(imm) != 0x7f && uint32_t(imm) != 0xf70 && uint32_t(imm) != 0x70)
						return fail(pc, "unsupported GCN wait counters");
					inst.opcode = Opcode::S_WAITCNT;
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
		} else if ((word & 0xf8000000u) == 0xc0000000u) {
			const uint32_t op = (word >> 22) & 31, base = ((word >> 9) & 63) * 2,
			               dst = (word >> 15) & 127;
			if (stage != Stage::Pixel || op != 10 || !(word & 0x100) || base != 0 || dst < 4 ||
			    dst > 100)
				return fail(pc, "unsupported SMRD stage/opcode/descriptor/offset/destination");
			inst.family    = Family::SMEM;
			inst.opcode    = Opcode::S_BUFFER_LOAD_DWORDX4;
			inst.opcode_id = op;
			scalar_dst(dst);
			inst.src0.kind   = OperandKind::Sgpr;
			inst.src0.reg    = base;
			inst.src1.kind   = OperandKind::LiteralConstant;
			inst.src1.value  = (word & 255) * 4;
			inst.src_count   = 2;
			inst.data_dwords = 4;
			inst.data_bits   = 32;
		} else if ((word & 0xfc000000u) == 0xf0000000u) {
			if (next == code.size()) return fail(pc, "truncated GCN MIMG");
			const uint32_t operand = code[next++], src = operand & 255, dst = (operand >> 8) & 255;
			// IMAGE_LOAD, full RGBA, unnormalized coordinates, eight-word descriptor s8..15.
			if (word != 0xf0001f00u || (operand & 0xffff0000u) != 0x00020000u || src > 254 ||
			    dst > 252)
				return fail(pc, "unsupported IMAGE_LOAD controls/descriptor/register group");
			inst.family                   = Family::MIMG;
			inst.opcode                   = Opcode::IMAGE_LOAD;
			inst.dst                      = Vector(dst);
			inst.src0                     = Vector(src);
			inst.src1.kind                = OperandKind::Sgpr;
			inst.src1.reg                 = 8;
			inst.src_count                = 2;
			inst.data_dwords              = 4;
			inst.data_bits                = 32;
			inst.dmask                    = 15;
			inst.image_dimension          = ImageDimension::Dim2D;
			inst.image_address_components = 2;
		} else if ((word & 0xfc000000u) == 0xe0000000u) {
			if (next == code.size()) return fail(pc, "truncated GCN MUBUF");
			const uint32_t operand = code[next++], op = (word >> 18) & 127;
			const uint32_t count = op == 12 ? 1 : 4, dst = (operand >> 8) & 255;
			const uint32_t base = ((operand >> 16) & 31) * 4;
			// IDXEN only; no OFFEN/ADDR64/cache/TFE/LDS/swizzle extensions.
			if ((op != 12 && op != 14) || (word & ~0xe1fc2fffu) || !(word & 0x2000) || (word & 3) ||
			    (operand & 0x00e00000u) || (operand >> 24) != 128 || base > 4 || dst + count > 256)
				return fail(pc, "unsupported MUBUF opcode/controls/descriptor/register range");
			inst.family      = Family::MUBUF;
			inst.opcode      = op == 12 ? Opcode::BUFFER_LOAD_DWORD : Opcode::BUFFER_LOAD_DWORDX4;
			inst.opcode_id   = op;
			inst.dst         = Vector(dst);
			inst.src0        = Vector(operand & 255);
			inst.src1.kind   = OperandKind::Sgpr;
			inst.src1.reg    = base;
			inst.src2.kind   = OperandKind::IntegerInlineConstant;
			inst.src2.value  = 0;
			inst.src_count   = 3;
			inst.offset      = word & 4095;
			inst.idxen       = true;
			inst.data_dwords = count;
			inst.data_bits   = 32;
		} else if ((word & 0xfc000000u) == 0xd0000000u) {
			if (next == code.size()) return fail(pc, "truncated GCN VOP3");
			if (word & 0x1f000u) return fail(pc, "unsupported VOP3 op_sel/reserved controls");
			const uint32_t operands = code[next++];
			inst.family             = Family::VOP3;
			inst.opcode_id          = (word >> 17) & 511;
			inst.src_count          = 2;
			switch (inst.opcode_id) {
				case 259: inst.opcode = Opcode::V_ADD_F32; break;
				case 260: inst.opcode = Opcode::V_SUB_F32; break;
				case 261: inst.opcode = Opcode::V_SUBREV_F32; break;
				case 264: inst.opcode = Opcode::V_MUL_F32; break;
				case 271: inst.opcode = Opcode::V_MIN_F32; break;
				case 272: inst.opcode = Opcode::V_MAX_F32; break;
				case 321:
					inst.opcode    = Opcode::V_MAD_F32;
					inst.src_count = 3;
					break;
				case 331:
					inst.opcode    = Opcode::V_FMA_F32;
					inst.src_count = 3;
					break;
				default: return fail(pc, "unsupported GCN VOP3 opcode");
			}
			inst.dst       = Vector(word & 255);
			inst.dst.clamp = (word & 0x800) != 0;
			inst.dst.omod  = (operands >> 27) & 3;
			Operand*                sources[] {&inst.src0, &inst.src1, &inst.src2};
			std::optional<uint32_t> scalar;
			for (uint32_t n = 0; n < 3; ++n) {
				const uint32_t bits     = (operands >> (9 * n)) & 511;
				const bool     absolute = (word & (1u << (8 + n))) != 0;
				const bool     negate   = (operands & (1u << (29 + n))) != 0;
				if (n >= inst.src_count) {
					if (bits || absolute || negate) return fail(pc, "nonzero unused VOP3 operand");
					continue;
				}
				// GCN VOP3 has no RDNA literal extension. Enforce its single SGPR bus source.
				if (bits == 255 || !source(bits, *sources[n]))
					return fail(pc, "invalid GCN VOP3 source");
				if (sources[n]->kind == OperandKind::Sgpr) {
					if (scalar && *scalar != bits)
						return fail(pc, "VOP3 has multiple SGPR bus sources");
					scalar = bits;
				}
				sources[n]->absolute = absolute;
				sources[n]->negate   = negate;
			}
		} else if ((word & 0xfc000000u) == 0xc8000000u) {
			const uint32_t op = (word >> 16) & 3, attr = (word >> 10) & 63, chan = (word >> 8) & 3,
			               dst = (word >> 18) & 255, src = word & 255;
			if (stage != Stage::Pixel || attr >= parameter_count || op > 1 || dst < 2 || src != op)
				return fail(pc, "unsupported interpolation stage/attribute/operand");
			// Only adjacent canonical P1/P2 pairs are lowered to a native interpolant.
			// P1's intermediate value is not interchangeable with that interpolant.
			if (op == 0) {
				if (next == code.size() || code[next] != ((word | (1u << 16)) + 1))
					return fail(pc, "interpolation requires matching adjacent P1/P2");
			} else if (pc == 0 || code[pc - 1] != ((word & ~(1u << 16)) - 1))
				return fail(pc, "interpolation P2 lacks matching P1");
			inst.family    = Family::VINTRP;
			inst.opcode_id = op;
			inst.opcode    = op == 0 ? Opcode::V_INTERP_P1_F32 : Opcode::V_INTERP_P2_F32;
			inst.dst       = Vector(dst);
			inst.src0      = Vector(src);
			inst.src1.kind = inst.src2.kind = OperandKind::IntegerInlineConstant;
			inst.src1.value                 = attr;
			inst.src2.value                 = chan;
			inst.src_count                  = 3;
		} else if ((word & 0xfc000000u) == 0xf8000000u) {
			if (next == code.size()) return fail(pc, "truncated export");
			const uint32_t target = (word >> 4) & 63;
			const bool     parameter =
			    stage == Stage::Vertex && target >= 32 && target < 32 + parameter_count;
			if (!parameter && target != (stage == Stage::Vertex ? 12u : 0u))
				return fail(pc, "unsupported export target");
			const uint32_t expected = 0xf800000fu | (target << 4) | (parameter ? 0 : 0x800) |
			                          (stage == Stage::Pixel ? 0x1000 : 0);
			if (word != expected) return fail(pc, "unsupported export controls/target");
			const uint32_t sources = code[next++];
			inst.family            = Family::EXP;
			inst.opcode            = Opcode::EXP;
			inst.opcode_id         = target;
			inst.exp               = {.target = target,
			                          .en     = 15,
			                          .done   = !parameter,
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
				switch (inst.opcode_id) {
					case 1: inst.opcode = Opcode::V_MOV_B32; break;
					case 5: inst.opcode = Opcode::V_CVT_F32_I32; break;
					case 6: inst.opcode = Opcode::V_CVT_F32_U32; break;
					case 32: inst.opcode = Opcode::V_FRACT_F32; break;
					case 33: inst.opcode = Opcode::V_TRUNC_F32; break;
					case 34: inst.opcode = Opcode::V_CEIL_F32; break;
					case 36: inst.opcode = Opcode::V_FLOOR_F32; break;
					case 42: inst.opcode = Opcode::V_RCP_F32; break;
					case 51: inst.opcode = Opcode::V_SQRT_F32; break;
					default: return fail(pc, "unsupported GCN VOP1 opcode");
				}
				inst.src_count = 1;
			} else if (!(word & 0x80000000u)) {
				inst.family    = Family::VOP2;
				inst.opcode_id = (word >> 25) & 63;
				switch (inst.opcode_id) {
					case 3: inst.opcode = Opcode::V_ADD_F32; break;
					case 4: inst.opcode = Opcode::V_SUB_F32; break;
					case 5: inst.opcode = Opcode::V_SUBREV_F32; break;
					case 8: inst.opcode = Opcode::V_MUL_F32; break;
					case 15: inst.opcode = Opcode::V_MIN_F32; break;
					case 16: inst.opcode = Opcode::V_MAX_F32; break;
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
		uint32_t         may_parameters = 0, must_parameters = 0;
	};
	std::vector<std::optional<Flow>> incoming(program.instructions.size());
	incoming[0] = Flow {};
	if (stage == Stage::Vertex)
		incoming[0]->defined[0] = true;
	else if (parameter_count) {
		incoming[0]->defined[0] = true;
		incoming[0]->defined[1] = true;
	}
	bool       exit  = false;
	const auto merge = [&](size_t to, const Flow& flow) {
		if (!incoming[to])
			incoming[to] = flow;
		else {
			incoming[to]->defined &= flow.defined;
			incoming[to]->may_export |= flow.may_export;
			incoming[to]->must_export &= flow.must_export;
			incoming[to]->may_parameters |= flow.may_parameters;
			incoming[to]->must_parameters &= flow.must_parameters;
		}
	};
	for (size_t i = 0; i < program.instructions.size(); ++i) {
		const auto& inst = program.instructions[i];
		const bool  branch =
		    inst.opcode == Opcode::S_BRANCH || (effects[i].scc_read && inst.family == Family::SOPP);
		size_t target = 0;
		if (branch) {
			auto it = boundaries.find(inst.branch_target);
			if (it == boundaries.end())
				return fail(inst.pc / 4, "branch into literal/non-instruction");
			target = it->second;
			if (program.instructions[target].opcode == Opcode::V_INTERP_P2_F32)
				return fail(inst.pc / 4, "branch into interpolation pair");
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
		if (inst.opcode == Opcode::S_BUFFER_LOAD_DWORDX4 || inst.family == Family::MUBUF ||
		    inst.family == Family::MIMG) {
			const uint32_t base = inst.family == Family::MUBUF || inst.family == Family::MIMG
			                          ? inst.src1.reg
			                          : inst.src0.reg;
			for (uint32_t n = 0; n < (inst.family == Family::MIMG ? 8u : 4u); ++n)
				if (!flow.defined[256 + base + n])
					return fail(inst.pc / 4, "undefined buffer descriptor SGPR");
		}
		if (inst.family == Family::MIMG && !flow.defined[inst.src0.reg + 1])
			return fail(inst.pc / 4, "undefined IMAGE_LOAD coordinate group");
		if (effects[i].scc_read && !flow.defined[Scc]) return fail(inst.pc / 4, "undefined SCC");
		if (inst.dst.kind == OperandKind::Vgpr) {
			if (stage == Stage::Pixel && parameter_count && inst.dst.reg < 2)
				return fail(inst.pc / 4, "overwriting interpolation coordinates is unsupported");
			if (inst.opcode != Opcode::V_INTERP_P1_F32)
				for (uint32_t n = 0;
				     n < ((inst.family == Family::MUBUF || inst.family == Family::MIMG)
				              ? inst.data_dwords
				              : 1u);
				     ++n) {
					if (stage == Stage::Pixel && parameter_count && inst.dst.reg + n < 2)
						return fail(inst.pc / 4, "MUBUF overwrites interpolation coordinates");
					flow.defined[inst.dst.reg + n] = true;
				}
		}
		if (inst.dst.kind == OperandKind::Sgpr) {
			const uint32_t count = inst.opcode == Opcode::S_BUFFER_LOAD_DWORDX4 ? 4 : 1;
			for (uint32_t n = 0; n < count; ++n)
				flow.defined[256 + inst.dst.reg + n] = true;
		}
		if (effects[i].scc_write) flow.defined[Scc] = true;
		if (inst.opcode == Opcode::EXP) {
			if (inst.exp.target >= 32) {
				const uint32_t bit = 1u << (inst.exp.target - 32);
				if (flow.may_parameters & bit)
					return fail(inst.pc / 4, "duplicate parameter export");
				flow.may_parameters |= bit;
				flow.must_parameters |= bit;
			} else {
				if (flow.may_export) return fail(inst.pc / 4, "duplicate export on a path");
				flow.may_export = flow.must_export = true;
			}
		} else if (flow.may_export && inst.opcode != Opcode::S_ENDPGM && !branch &&
		           inst.opcode != Opcode::S_NOP)
			return fail(inst.pc / 4, "ALU after done export");
		if (inst.opcode == Opcode::S_ENDPGM) {
			if (!flow.must_export ||
			    (stage == Stage::Vertex && flow.must_parameters != (1u << parameter_count) - 1))
				return fail(inst.pc / 4, "path ends without required export");
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
std::vector<uint32_t> IndexedBufferPixelFixture(bool dword) {
	// Buffer zero supplies a runtime index; buffer one supplies structured RGBA records.
	std::vector<uint32_t> code {0xc2840100u, 0xbf8c007fu, Vop1(1, 0, 8)};
	if (dword) {
		for (uint32_t i = 0; i < 4; ++i) {
			code.push_back(0xe0302000u | i * 4);
			code.push_back(0x80010000u | ((i + 1) << 8));
		}
	} else {
		code.push_back(0xe0382000u);
		code.push_back(0x80010100u);
	}
	code.insert(code.end(), {0xbf8c0f70u, 0xf800180fu, 0x04030201u, 0xbf810000u});
	return code;
}
std::vector<uint32_t> FetchVertexFixture() {
	return {0xe0382000u, 0x80000200u, 0xe0382000u, 0x80010600u, 0xbf8c0f70u,
	        0xf80008cfu, 0x05040302u, 0xf800020fu, 0x09080706u, 0xbf810000u};
}
std::vector<uint32_t> TextureVertexFixture() {
	auto code = FetchVertexFixture();
	code.resize(4);
	code.insert(code.end(),
	            {Vop1(1, 10, 256), Vop1(1, 11, 128), 0xf0001f00u, 0x00020c0au, 0xbf8c0f70u});
	for (uint32_t c = 0; c < 4; ++c)
		code.push_back(Vop2(8, 6 + c, 256 + 12 + c, 6 + c));
	code.insert(code.end(), {0xf80008cf, 0x05040302, 0xf800020f, 0x09080706, 0xbf810000});
	return code;
}
std::vector<uint32_t> TexturePixelFixture() {
	auto code = InterpolatedPixelFixture();
	code.resize(8);
	code.insert(code.end(),
	            {0xc2880100u, 0xbf8c007fu, Vop1(1, 10, 16), Vop1(1, 11, 17), Vop1(1, 12, 18),
	             0xe0382000u, 0x80010d0cu, 0xf0001f00u, 0x0002060au, 0xbf8c0f70u});
	for (uint32_t c = 0; c < 4; ++c) {
		code.push_back(Vop2(8, 2 + c, 256 + 13 + c, 2 + c));
		code.push_back(Vop2(8, 2 + c, 256 + 6 + c, 2 + c));
	}
	code.insert(code.end(), {0xf800180f, 0x05040302, 0xbf810000});
	return code;
}
std::vector<uint32_t> BufferPixelFixture(uint32_t dword_offset) {
	return {0xc2820100u | dword_offset,
	        0xbf8c007fu,
	        Vop1(1, 0, 4),
	        Vop1(1, 1, 5),
	        Vop1(1, 2, 6),
	        Vop1(1, 3, 7),
	        0xf800180fu,
	        0x03020100u,
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
std::vector<uint32_t> InterpolatedVertexFixture(bool perspective) {
	auto code = TriangleVertexFixture(false);
	code.resize(code.size() - 3);
	const auto append = [&](std::initializer_list<uint32_t> words) {
		code.insert(code.end(), words);
	};
	// The NDC triangle stays fixed; PARAM0 carries (u,v,.25,1).
	append({Vop2(8, 6, 255, 2), std::bit_cast<uint32_t>(2.f / 3), Vop2(3, 6, 240, 6),
	        Vop2(8, 7, 255, 3), std::bit_cast<uint32_t>(2.f / 3), Vop2(3, 7, 240, 7),
	        Vop1(1, 8, 255), std::bit_cast<uint32_t>(.25f), Vop1(1, 9, 242)});
	if (perspective) {
		append({Vop2(27, 10, 130, 0), Vop1(6, 10, 266), Vop2(8, 10, 240, 10), Vop2(3, 10, 242, 10),
		        Vop2(8, 2, 266, 2), Vop2(8, 3, 266, 3), Vop1(1, 5, 266)});
	}
	append({0xf80008cfu, 0x05040302u, 0xf800020fu, 0x09080706u, 0xbf810000u});
	return code;
}
std::vector<uint32_t> InterpolatedPixelFixture(bool arithmetic) {
	std::vector<uint32_t> code;
	for (uint32_t channel = 0; channel < 4; ++channel) {
		const uint32_t p1 = 0xc8000000u | ((channel + 2) << 18) | (channel << 8);
		code.push_back(p1);
		code.push_back(p1 | 0x10001u);
	}
	if (arithmetic) {
		const auto vop3 = [&](uint32_t op, uint32_t dst, uint32_t a, uint32_t b, uint32_t c,
		                      uint32_t absolute, uint32_t negate, uint32_t omod, bool clamp) {
			code.push_back(0xd0000000u | (op << 17) | dst | (absolute << 8) | (clamp ? 0x800 : 0));
			code.push_back(a | (b << 9) | (c << 18) | (omod << 27) | (negate << 29));
		};
		vop3(331, 2, 258, 244, 242, 1, 1, 0, true);  // clamp(1 - 2*abs(red))
		vop3(321, 3, 259, 240, 240, 0, 0, 3, false); // (green*.5 + .5)*.5
		code.push_back(Vop2(15, 4, 240, 3));         // min(.5, green)
		code.push_back(Vop1(1, 6, 255));
		code.push_back(std::bit_cast<uint32_t>(.375f));
		vop3(272, 4, 262, 260, 0, 0, 0, 0, false); // max(.375, blue)
	}
	code.insert(code.end(), {0xf800180fu, 0x05040302u, 0xbf810000u});
	return code;
}
} // namespace Libs::Graphics::ShaderRecompiler::Gcn
