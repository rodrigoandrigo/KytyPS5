#include "graphics/shader/recompiler/frontend/decode/GcnDecoder.h"

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <deque>
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
bool MaskRegister(uint32_t code, Operand& operand) {
	switch (code) {
		case 106: operand.kind = OperandKind::VccLo; return true;
		case 107: operand.kind = OperandKind::VccHi; return true;
		case 126: operand.kind = OperandKind::ExecLo; return true;
		case 127: operand.kind = OperandKind::ExecHi; return true;
		default: return false;
	}
}
bool CompareSpecialSource(uint32_t code, Operand& operand) {
	if (MaskRegister(code, operand)) return true;
	switch (code) {
		case 251: operand.kind = OperandKind::VccZ; return true;
		case 252: operand.kind = OperandKind::ExecZ; return true;
		case 253: operand.kind = OperandKind::Scc; return true;
		default: return false;
	}
}
bool ScalarPair(uint32_t code, Operand& operand) {
	if (code & 1) return false;
	if (MaskRegister(code, operand)) return true;
	if (code > 102) return false;
	operand.kind = OperandKind::Sgpr;
	operand.reg = code;
	return true;
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
uint32_t CompareBaseOpcode(uint32_t op) {
	// FP32/FP64 CMPS and CMPSX have the same predicates as CMP and CMPX.
	// The PS4 raster profile has traps disabled and cannot read TRAPSTS.
	return op & ~(op < 128 ? 0x50u : 0x10u);
}

// Gen4 VOP3 opcodes 0..255 use the same comparison predicate numbers as VOPC.
bool DecodeCompareOpcode(uint32_t op, Instruction& inst) {
	constexpr Opcode compares[] {Opcode::V_CMP_F_F32, Opcode::V_CMP_LT_F32,
	    Opcode::V_CMP_EQ_F32, Opcode::V_CMP_LE_F32, Opcode::V_CMP_GT_F32,
	    Opcode::V_CMP_LG_F32, Opcode::V_CMP_GE_F32, Opcode::V_CMP_O_F32,
	    Opcode::V_CMP_U_F32, Opcode::V_CMP_NGE_F32, Opcode::V_CMP_NLG_F32,
	    Opcode::V_CMP_NGT_F32, Opcode::V_CMP_NLE_F32, Opcode::V_CMP_NEQ_F32,
	    Opcode::V_CMP_NLT_F32, Opcode::V_CMP_TRU_F32};
	constexpr Opcode signed_compares[] {Opcode::V_CMP_F_I32, Opcode::V_CMP_LT_I32,
	    Opcode::V_CMP_EQ_I32, Opcode::V_CMP_LE_I32, Opcode::V_CMP_GT_I32,
	    Opcode::V_CMP_NE_I32, Opcode::V_CMP_GE_I32, Opcode::V_CMP_T_I32};
	constexpr Opcode unsigned_compares[] {Opcode::V_CMP_F_U32, Opcode::V_CMP_LT_U32,
	    Opcode::V_CMP_EQ_U32, Opcode::V_CMP_LE_U32, Opcode::V_CMP_GT_U32,
	    Opcode::V_CMP_NE_U32, Opcode::V_CMP_GE_U32, Opcode::V_CMP_T_U32};
	const bool cmpx = (op & 16) != 0;
	const uint32_t base_op = CompareBaseOpcode(op);
	if (base_op < 16) inst.opcode = compares[base_op];
	else if (base_op >= 32 && base_op < 48) {
		constexpr Opcode fp64[] {
		    Opcode::V_CMP_F_F64, Opcode::V_CMP_LT_F64, Opcode::V_CMP_EQ_F64,
		    Opcode::V_CMP_LE_F64, Opcode::V_CMP_GT_F64, Opcode::V_CMP_LG_F64,
		    Opcode::V_CMP_GE_F64, Opcode::V_CMP_O_F64, Opcode::V_CMP_U_F64,
		    Opcode::V_CMP_NGE_F64, Opcode::V_CMP_NLG_F64, Opcode::V_CMP_NGT_F64,
		    Opcode::V_CMP_NLE_F64, Opcode::V_CMP_NEQ_F64, Opcode::V_CMP_NLT_F64,
		    Opcode::V_CMP_TRU_F64};
		inst.opcode = fp64[base_op - 32];
		inst.data_dwords = 2;
	} else if (base_op >= 128 && base_op < 136) inst.opcode = signed_compares[base_op - 128];
	else if (base_op >= 192 && base_op < 200) inst.opcode = unsigned_compares[base_op - 192];
	else if (!cmpx && base_op >= 200 && base_op <= 207) {
		// The Gen4 F16 VOPC subset shares the first eight F32 predicates.
		inst.opcode = compares[base_op - 200];
		inst.data_bits = 16;
	} else if (base_op == 136 || base_op == 168) {
		inst.opcode = Opcode::V_CMP_CLASS_F32;
		// Only the floating operand is paired. The class mask is one DWORD.
		if (base_op == 168) inst.data_bits = 64;
	} else if (!cmpx && base_op >= 137 && base_op <= 142) {
		constexpr Opcode predicates[] {Opcode::V_CMP_LT_I16, Opcode::V_CMP_EQ_I16,
		    Opcode::V_CMP_LE_I16, Opcode::V_CMP_GT_I16, Opcode::V_CMP_NE_I16,
		    Opcode::V_CMP_GE_I16};
		inst.opcode = predicates[base_op - 137];
	} else if (!cmpx && base_op >= 169 && base_op <= 174) {
		constexpr Opcode predicates[] {Opcode::V_CMP_LT_U16, Opcode::V_CMP_EQ_U16,
		    Opcode::V_CMP_LE_U16, Opcode::V_CMP_GT_U16, Opcode::V_CMP_NE_U16,
		    Opcode::V_CMP_GE_U16};
		inst.opcode = predicates[base_op - 169];
	} else if ((base_op >= 160 && base_op < 168) || (base_op >= 224 && base_op < 232)) {
		const auto first = base_op >= 224 ? Opcode::V_CMP_F_U64 : Opcode::V_CMP_F_I64;
		inst.opcode = static_cast<Opcode>(static_cast<uint32_t>(first) + (base_op & 7));
		inst.data_dwords = 2;
	} else return false;
	inst.family = Family::VOPC;
	inst.gcn_cmpx_updates_vcc_exec = cmpx;
	inst.opcode_id = op;
	inst.src_count = 2;
	return true;
}

bool CompareSourcePair(const Instruction& inst, uint32_t source) {
	return inst.family == Family::VOPC &&
	       (inst.data_dwords == 2 || (inst.data_bits == 64 && source == 0));
}

bool ScalarBitComparePair(const Instruction& inst, uint32_t source) {
	return source == 0 && (inst.opcode == Opcode::S_BITCMP0_B64 ||
	                       inst.opcode == Opcode::S_BITCMP1_B64);
}

bool IsValidComparePair(const Operand& operand) {
	return (operand.kind == OperandKind::Vgpr && operand.reg < 255) ||
	       (operand.kind == OperandKind::Sgpr && operand.reg <= 102 && !(operand.reg & 1)) ||
	       operand.kind == OperandKind::LiteralConstant ||
	       operand.kind == OperandKind::IntegerInlineConstant ||
	       operand.kind == OperandKind::FloatInlineConstant ||
	       operand.kind == OperandKind::ExecLo || operand.kind == OperandKind::VccLo ||
	       operand.kind == OperandKind::ExecZ || operand.kind == OperandKind::VccZ ||
	       operand.kind == OperandKind::Scc;
}

uint32_t Vop1(uint32_t op, uint32_t dst, uint32_t source) {
	return 0x7e000000u | dst << 17 | op << 9 | source;
}
uint32_t Vop2(uint32_t op, uint32_t dst, uint32_t source, uint32_t vector) {
	return op << 25 | dst << 17 | vector << 9 | source;
}
} // namespace

bool ScalarSourceIsPair(const Instruction& inst, uint32_t source) {
	if (ScalarBitComparePair(inst, source)) return true;
	if (inst.data_dwords != 2) return false;
	if (inst.family == Family::SOP1)
		return source != 0 || (inst.opcode != Opcode::S_BITSET0_B64 &&
		                       inst.opcode != Opcode::S_BITSET1_B64);
	if (inst.family != Family::SOP2) return false;
	switch (inst.opcode) {
		case Opcode::S_BFM_B64: return false;
		case Opcode::S_LSHL_B64:
		case Opcode::S_LSHR_B64:
		case Opcode::S_ASHR_I64:
		case Opcode::S_BFE_U64: return source == 0;
		default: return true;
	}
}

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
		bool vcc_read = false, vcc_write = false, mask_save = false;
		bool mask_branch = false;
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
		const auto mask_dst = [&](uint32_t reg) {
			return MaskRegister(reg, inst.dst) || scalar_dst(reg);
		};
		const auto mask_src = [&](uint32_t reg, Operand& operand) {
			return MaskRegister(reg, operand) || source(reg, operand);
		};
		const auto pair_src = [&](uint32_t reg, Operand& operand) {
			return ScalarPair(reg, operand) || (reg >= 128 && source(reg, operand));
		};
		const auto compare_src = [&](uint32_t reg, Operand& operand) {
			return CompareSpecialSource(reg, operand) || source(reg, operand);
		};
		constexpr Opcode scalar_comparisons[] {
		    Opcode::S_CMP_EQ_I32, Opcode::S_CMP_LG_I32, Opcode::S_CMP_GT_I32,
		    Opcode::S_CMP_GE_I32, Opcode::S_CMP_LT_I32, Opcode::S_CMP_LE_I32,
		    Opcode::S_CMP_EQ_U32, Opcode::S_CMP_LG_U32, Opcode::S_CMP_GT_U32,
		    Opcode::S_CMP_GE_U32, Opcode::S_CMP_LT_U32, Opcode::S_CMP_LE_U32};
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
				case 6: inst.opcode = Opcode::S_CBRANCH_VCCZ; effect.mask_branch = effect.vcc_read = true; break;
				case 7: inst.opcode = Opcode::S_CBRANCH_VCCNZ; effect.mask_branch = effect.vcc_read = true; break;
				case 8: inst.opcode = Opcode::S_CBRANCH_EXECZ; effect.mask_branch = true; break;
				case 9: inst.opcode = Opcode::S_CBRANCH_EXECNZ; effect.mask_branch = true; break;
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
			if (inst.opcode_id == 2 || effect.scc_read || effect.mask_branch) {
				const int64_t target = int64_t(inst.pc) + 4 + int64_t(imm) * 4;
				if (target < 0 || target >= int64_t(code.size_bytes()))
					return fail(pc, "out-of-range branch");
				inst.branch_target = uint32_t(target);
			}
		} else if ((word & 0xff800000u) == 0xbe800000u) {
			inst.family    = Family::SOP1;
			inst.opcode_id = (word >> 8) & 255;
			if (inst.opcode_id >= 31 && inst.opcode_id <= 33) {
				const uint32_t dst = (word >> 16) & 127, src = word & 255;
				constexpr Opcode pc_ops[] {Opcode::S_GETPC_B64, Opcode::S_SETPC_B64,
				                            Opcode::S_SWAPPC_B64};
				inst.opcode = pc_ops[inst.opcode_id - 31];
				inst.data_dwords = 2;
				if (inst.opcode != Opcode::S_SETPC_B64 &&
				    (dst % 2 || dst > 102 || !scalar_dst(dst)))
					return fail(pc, "invalid PC destination pair");
				if (inst.opcode != Opcode::S_GETPC_B64) {
					if (src % 2 || src > 102 || !ScalarPair(src, inst.src0))
						return fail(pc, "invalid indirect PC source pair");
					inst.src_count = 1;
				}
			} else if (inst.opcode_id >= 36 && inst.opcode_id <= 43) {
				constexpr Opcode save_exec[] {
				    Opcode::S_AND_SAVEEXEC_B64, Opcode::S_OR_SAVEEXEC_B64,
				    Opcode::S_XOR_SAVEEXEC_B64, Opcode::S_ANDN2_SAVEEXEC_B64,
				    Opcode::S_ORN2_SAVEEXEC_B64, Opcode::S_NAND_SAVEEXEC_B64,
				    Opcode::S_NOR_SAVEEXEC_B64, Opcode::S_XNOR_SAVEEXEC_B64};
				const uint32_t dst = (word >> 16) & 127;
				if (dst % 2 || dst > 102 || !pair_src(word & 255, inst.src0))
					return fail(pc, "SAVEEXEC requires aligned scalar register pairs");
				inst.opcode = save_exec[inst.opcode_id - 36];
				scalar_dst(dst);
				inst.src_count = 1;
				inst.data_dwords = 2;
				effect.mask_save = effect.scc_write = true;
				effect.vcc_read = inst.src0.kind == OperandKind::VccLo;
			} else if (inst.opcode_id == 4 || inst.opcode_id == 6 || inst.opcode_id == 8 || inst.opcode_id == 10 ||
			           inst.opcode_id == 12 || inst.opcode_id == 28 || inst.opcode_id == 30) {
				const uint32_t dst = (word >> 16) & 127, src = word & 255;
				const bool bitset = inst.opcode_id == 28 || inst.opcode_id == 30;
				if (!ScalarPair(dst, inst.dst) ||
				    !(bitset ? mask_src(src, inst.src0) : pair_src(src, inst.src0)))
					return fail(pc, "invalid SOP1 B64 operand");
				switch (inst.opcode_id) {
					case 4: inst.opcode = Opcode::S_MOV_B64; break;
					case 6: inst.opcode = Opcode::S_CMOV_B64; effect.scc_read = true; break;
					case 8: inst.opcode = Opcode::S_NOT_B64; effect.scc_write = true; break;
					case 10: inst.opcode = Opcode::S_WQM_B64; effect.scc_write = true; break;
					case 12: inst.opcode = Opcode::S_BREV_B64; break;
					case 28: inst.opcode = Opcode::S_BITSET0_B64; break;
					case 30: inst.opcode = Opcode::S_BITSET1_B64; break;
				}
				inst.src_count = 1;
				inst.data_dwords = 2;
				if (bitset || inst.opcode_id == 6) {
					inst.src1 = inst.dst; // The old destination is also an input.
					inst.src_count = 2;
				}
				effect.vcc_write = dst == 106;
			} else {
				switch (inst.opcode_id) {
					case 3: inst.opcode = Opcode::S_MOV_B32; break;
					case 5: inst.opcode = Opcode::S_CSELECT_B32; effect.scc_read = true; break;
					case 7: inst.opcode = Opcode::S_NOT_B32; effect.scc_write = true; break;
					case 9: inst.opcode = Opcode::S_WQM_B32; effect.scc_write = true; break;
					case 11: inst.opcode = Opcode::S_BREV_B32; break;
					case 27: inst.opcode = Opcode::S_BITSET0_B32; break;
					case 29: inst.opcode = Opcode::S_BITSET1_B32; break;
					default: return fail(pc, "unsupported GCN SOP1 opcode");
				}
				if (!mask_dst((word >> 16) & 127) || !mask_src(word & 255, inst.src0))
					return fail(pc, "invalid scalar move operand");
				inst.src_count = 1;
				if (inst.opcode_id == 5 || inst.opcode_id == 27 || inst.opcode_id == 29) {
					inst.src1 = inst.dst;
					inst.src_count = 2;
				}
			}
		} else if ((word & 0xff800000u) == 0xbf000000u) {
			inst.family    = Family::SOPC;
			inst.opcode_id = (word >> 16) & 127;
			constexpr Opcode bit_compares[] {Opcode::S_BITCMP0_B32, Opcode::S_BITCMP1_B32,
			                                 Opcode::S_BITCMP0_B64, Opcode::S_BITCMP1_B64};
			if (inst.opcode_id < std::size(scalar_comparisons))
				inst.opcode = scalar_comparisons[inst.opcode_id];
			else if (inst.opcode_id < 16)
				inst.opcode = bit_compares[inst.opcode_id - 12];
			else
				return fail(pc, "unsupported GCN scalar comparison");
			inst.src_count   = 2;
			effect.scc_write = true;
			if (ScalarBitComparePair(inst, 0))
				inst.data_dwords = 2;
			const uint32_t lhs = word & 255;
			if (!(ScalarBitComparePair(inst, 0) ? pair_src(lhs, inst.src0) : compare_src(lhs, inst.src0)) ||
			    !compare_src((word >> 8) & 255, inst.src1))
				return fail(pc, "invalid scalar comparison source");
		} else if ((word & 0xf0000000u) == 0xb0000000u) {
			const uint32_t reg = (word >> 16) & 127, op = (word >> 23) & 31;
			inst.family = Family::SOPK;
			inst.opcode_id = op;
			Operand immediate;
			immediate.kind = OperandKind::LiteralConstant;
			immediate.signed_val = op < 9 ? int16_t(word & 0xffff) : int32_t(word & 0xffff);
			immediate.value = uint32_t(immediate.signed_val);
			if (op == 0 && scalar_dst(reg)) {
				inst.opcode = Opcode::S_MOVK_I32;
				inst.src_count = 1;
				inst.src0 = immediate;
			} else if (op >= 3 && op <= 14 && compare_src(reg, inst.src0)) {
				inst.opcode = scalar_comparisons[op - 3];
				inst.src_count = 2;
				inst.src1 = immediate;
				effect.scc_write = true;
			} else {
				return fail(pc, "unsupported SOPK opcode or operand");
			}
		} else if ((word & 0xc0000000u) == 0x80000000u) {
			inst.family    = Family::SOP2;
			inst.opcode_id = (word >> 23) & 127;
			switch (inst.opcode_id) {
				case 0: inst.opcode = Opcode::S_ADD_U32; break;
				case 1: inst.opcode = Opcode::S_SUB_U32; break;
				case 2: inst.opcode = Opcode::S_ADD_I32; break;
				case 3: inst.opcode = Opcode::S_SUB_I32; break;
				case 4:
					inst.opcode = Opcode::S_ADDC_U32;
					effect.scc_read = true;
					break;
				case 5:
					inst.opcode = Opcode::S_SUBB_U32;
					effect.scc_read = true;
					break;
				case 6: inst.opcode = Opcode::S_MIN_I32; break;
				case 7: inst.opcode = Opcode::S_MIN_U32; break;
				case 8: inst.opcode = Opcode::S_MAX_I32; break;
				case 9: inst.opcode = Opcode::S_MAX_U32; break;
				case 10:
					inst.opcode     = Opcode::S_CSELECT_B32;
					effect.scc_read = true;
					break;
				case 11:
					inst.opcode = Opcode::S_CSELECT_B64;
					inst.data_dwords = 2;
					effect.scc_read = true;
					break;
				case 14: inst.opcode = Opcode::S_AND_B32; break;
				case 16: inst.opcode = Opcode::S_OR_B32; break;
				case 18: inst.opcode = Opcode::S_XOR_B32; break;
				case 15: inst.opcode = Opcode::S_AND_B64; inst.data_dwords = 2; break;
				case 17: inst.opcode = Opcode::S_OR_B64; inst.data_dwords = 2; break;
				case 19: inst.opcode = Opcode::S_XOR_B64; inst.data_dwords = 2; break;
				case 20: inst.opcode = Opcode::S_ANDN2_B32; break;
				case 21: inst.opcode = Opcode::S_ANDN2_B64; inst.data_dwords = 2; break;
				case 22: inst.opcode = Opcode::S_ORN2_B32; break;
				case 23: inst.opcode = Opcode::S_ORN2_B64; inst.data_dwords = 2; break;
				case 24: inst.opcode = Opcode::S_NAND_B32; break;
				case 25: inst.opcode = Opcode::S_NAND_B64; inst.data_dwords = 2; break;
				case 26: inst.opcode = Opcode::S_NOR_B32; break;
				case 27: inst.opcode = Opcode::S_NOR_B64; inst.data_dwords = 2; break;
				case 28: inst.opcode = Opcode::S_XNOR_B32; break;
				case 29: inst.opcode = Opcode::S_XNOR_B64; inst.data_dwords = 2; break;
				case 30: inst.opcode = Opcode::S_LSHL_B32; break;
				case 31: inst.opcode = Opcode::S_LSHL_B64; inst.data_dwords = 2; break;
				case 32: inst.opcode = Opcode::S_LSHR_B32; break;
				case 33: inst.opcode = Opcode::S_LSHR_B64; inst.data_dwords = 2; break;
				case 34: inst.opcode = Opcode::S_ASHR_I32; break;
				case 35: inst.opcode = Opcode::S_ASHR_I64; inst.data_dwords = 2; break;
				case 36: inst.opcode = Opcode::S_BFM_B32; break;
				case 37: inst.opcode = Opcode::S_BFM_B64; inst.data_dwords = 2; break;
				case 38: inst.opcode = Opcode::S_MUL_I32; break;
				case 39: inst.opcode = Opcode::S_BFE_U32; break;
				case 40: inst.opcode = Opcode::S_BFE_I32; break;
				case 41: inst.opcode = Opcode::S_BFE_U64; inst.data_dwords = 2; break;
				case 42:
					inst.opcode = Opcode::S_BFE_U64;
					inst.data_dwords = 2;
					inst.data_signed = true;
					break;
				default: return fail(pc, "unsupported GCN SOP2 opcode");
			}
			effect.scc_write = !effect.scc_read || inst.opcode == Opcode::S_ADDC_U32 ||
			                   inst.opcode == Opcode::S_SUBB_U32;
			effect.scc_write &= inst.opcode_id < 36 || inst.opcode_id > 38;
			inst.src_count   = 2;
			if (inst.data_dwords == 2) {
				const uint32_t dst = (word >> 16) & 127, lhs = word & 255, rhs = (word >> 8) & 255;
				if (!ScalarPair(dst, inst.dst) ||
				    !(ScalarSourceIsPair(inst, 0) ? pair_src(lhs, inst.src0) : mask_src(lhs, inst.src0)) ||
				    !(ScalarSourceIsPair(inst, 1) ? pair_src(rhs, inst.src1) : mask_src(rhs, inst.src1)))
					return fail(pc, "invalid SOP2 B64 operand");
				effect.vcc_write = dst == 106;
			} else if (!mask_dst((word >> 16) & 127) || !mask_src(word & 255, inst.src0) ||
			           !mask_src((word >> 8) & 255, inst.src1))
				return fail(pc, "invalid scalar ALU operand");
		} else if ((word & 0xf8000000u) == 0xc0000000u) {
			const uint32_t op = (word >> 22) & 31, base = ((word >> 9) & 63) * 2,
			               dst = (word >> 15) & 127;
			// GCN SMRD opcodes 8..12 encode DWORD through DWORDX16. The
			// Immediate offsets are in DWORDs. Register offsets are byte addresses.
			const bool raw = op <= 4;
			const bool immediate = (word & 0x100) != 0;
			const uint32_t count = raw ? 1u << op : (op >= 8 && op <= 12 ? 1u << (op - 8) : 0);
			if (!count || (!immediate && (word & 255) >= 104) ||
			    base + (raw ? 2u : 4u) > 104 ||
			    (!raw && (base % 4 || dst < 4)) || dst + count > 104)
				return fail(pc, "unsupported SMRD stage/opcode/descriptor/offset/destination");
			inst.family    = Family::SMEM;
			constexpr Opcode loads[] {Opcode::S_BUFFER_LOAD_DWORD, Opcode::S_BUFFER_LOAD_DWORDX2,
			                          Opcode::S_BUFFER_LOAD_DWORDX4, Opcode::S_BUFFER_LOAD_DWORDX8,
			                          Opcode::S_BUFFER_LOAD_DWORDX16};
			constexpr Opcode raw_loads[] {Opcode::S_LOAD_DWORD, Opcode::S_LOAD_DWORDX2,
			                              Opcode::S_LOAD_DWORDX4, Opcode::S_LOAD_DWORDX8,
			                              Opcode::S_LOAD_DWORDX16};
			inst.opcode    = raw ? raw_loads[op] : loads[op - 8];
			inst.opcode_id = op;
			scalar_dst(dst);
			inst.src0.kind   = OperandKind::Sgpr;
			inst.src0.reg    = base;
			if (immediate) {
				inst.src1.kind = OperandKind::LiteralConstant;
				inst.src1.value = (word & 255) * 4;
			} else {
				inst.src1.kind = OperandKind::Sgpr;
				inst.src1.reg = word & 255;
			}
			inst.src_count   = 2;
			inst.data_dwords = count;
			inst.data_bits   = 32;
		} else if ((word & 0xfc000000u) == 0xf0000000u) {
			if (next == code.size()) return fail(pc, "truncated GCN MIMG");
			const uint32_t operand = code[next++], src = operand & 255, dst = (operand >> 8) & 255;
			// Two explicit GCN encodings, never RDNA opcode reinterpretation.
			// T# s8..15; sampled form also uses S# s4..7 and (u,v,lod).
			const bool sampled = word == 0xf0900f00u; // IMAGE_SAMPLE_L, normalized RGBA
			if ((!sampled && word != 0xf0001f00u) ||
			    (operand & 0xfc000000u) || (!sampled && (operand & 0x03e00000u)) ||
			    ((operand >> 16) & 31) * 4 + 8 > 104 ||
			    (sampled && ((operand >> 21) & 31) * 4 + 4 > 104) ||
			    src > (sampled ? 253u : 254u) || dst > 252)
				return fail(pc, "unsupported image controls/descriptor/register group");
			inst.family                   = Family::MIMG;
			inst.opcode                   = sampled ? Opcode::IMAGE_SAMPLE : Opcode::IMAGE_LOAD;
			inst.dst                      = Vector(dst);
			inst.src0                     = Vector(src);
			inst.src1.kind                = OperandKind::Sgpr;
			inst.src1.reg                 = ((operand >> 16) & 31) * 4;
			inst.src_count                = sampled ? 3 : 2;
			if (sampled) {
				inst.src2.kind = OperandKind::Sgpr;
				inst.src2.reg = ((operand >> 21) & 31) * 4;
				inst.image_sample_flags = ImageSampleFlagLod;
			}
			inst.data_dwords              = 4;
			inst.data_bits                = 32;
			inst.dmask                    = 15;
			inst.image_dimension          = ImageDimension::Dim2D;
			inst.image_address_components = sampled ? 3 : 2;
		} else if ((word & 0xfc000000u) == 0xe8000000u) {
			if (next == code.size()) return fail(pc, "truncated GCN MTBUF");
			const uint32_t operand = code[next++], op = (word >> 16) & 7;
			const uint32_t format = (word >> 19) & 15, number = (word >> 23) & 7;
			const uint32_t count = op + 1, base = ((operand >> 16) & 31) * 4;
			const uint32_t dst = (operand >> 8) & 255;
			constexpr uint32_t formats[] {4, 11, 13, 14}; // 32,32x2,32x3,32x4
			if (op > 3 || format != formats[op] || number != 7 ||
			    word != (0xe8002000u | (op << 16) | (format << 19) | (7u << 23) | (word & 4095)) ||
			    (word & 3) || (operand & 0x00e00000u) || (operand >> 24) != 128 ||
			    base + 4 > 104 || dst + count > 256)
				return fail(pc, "unsupported MTBUF format/controls/descriptor/register range");
			// Each returned FLOAT32 component is bit-identical to a raw DWORD.
			// Normalize this representation-compatible subset, not GCN format numbers cast
			// to Prospero. Packed/normalized/integer conversions are separate.
			constexpr Opcode loads[] {Opcode::BUFFER_LOAD_DWORD, Opcode::BUFFER_LOAD_DWORDX2,
			                          Opcode::BUFFER_LOAD_DWORDX3, Opcode::BUFFER_LOAD_DWORDX4};
			inst.family = Family::MTBUF;
			inst.opcode = loads[op];
			inst.opcode_id = op;
			inst.dst = Vector(dst);
			inst.src0 = Vector(operand & 255);
			inst.src1.kind = OperandKind::Sgpr;
			inst.src1.reg = base;
			inst.src2.kind = OperandKind::IntegerInlineConstant;
			inst.src2.value = 0;
			inst.src_count = 3;
			inst.offset = word & 4095;
			inst.idxen = true;
			inst.data_dwords = count;
			inst.data_bits = 32;
		} else if ((word & 0xfc000000u) == 0xe0000000u) {
			if (next == code.size()) return fail(pc, "truncated GCN MUBUF");
			const uint32_t operand = code[next++], op = (word >> 18) & 127;
			const bool store = op == 28;
			const uint32_t count = store || op == 12 ? 1 : op == 13 ? 2 : op == 15 ? 3 : 4,
			               dst = (operand >> 8) & 255;
			const uint32_t base = ((operand >> 16) & 31) * 4;
			const uint32_t soffset = operand >> 24;
			// IDXEN only; no OFFEN/ADDR64/cache/TFE/LDS/swizzle extensions.
			if ((!store && (op < 12 || op > 15)) || (word & ~0xe1fc2fffu) || !(word & 0x2000) || (word & 3) ||
			    (operand & 0x00e00000u) || (soffset >= 104 && soffset != 128) ||
			    base + 4 > 104 || dst + count > 256)
				return fail(pc, "unsupported MUBUF opcode/controls/descriptor/register range");
			inst.family      = Family::MUBUF;
			constexpr Opcode loads[] {Opcode::BUFFER_LOAD_DWORD, Opcode::BUFFER_LOAD_DWORDX2,
			                          Opcode::BUFFER_LOAD_DWORDX4, Opcode::BUFFER_LOAD_DWORDX3};
			inst.opcode = store ? Opcode::BUFFER_STORE_DWORD : loads[op - 12];
			inst.opcode_id   = op;
			inst.dst         = Vector(dst);
			inst.src0        = Vector(operand & 255);
			inst.src1.kind   = OperandKind::Sgpr;
			inst.src1.reg    = base;
			if (!source(soffset, inst.src2))
				return fail(pc, "invalid MUBUF scalar byte offset");
			inst.src_count   = 3;
			if (store) {
				inst.src3 = inst.dst; // Stores read VDATA rather than defining a VGPR.
				inst.src_count = 4;
			}
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
			const bool compare = inst.opcode_id < 256;
			if (compare) {
				if (!DecodeCompareOpcode(inst.opcode_id, inst) ||
				    !ScalarPair(word & 255, inst.dst) || (word & 0x800) || ((operands >> 27) & 3))
					return fail(pc, "invalid VOP3 compare opcode, mask destination or result modifiers");
				effect.vcc_write = inst.dst.kind == OperandKind::VccLo;
			} else switch (inst.opcode_id) {
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
			if (!compare) {
				inst.dst       = Vector(word & 255);
				inst.dst.clamp = (word & 0x800) != 0;
				inst.dst.omod  = (operands >> 27) & 3;
			}
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
				const bool mask_source = compare && CompareSpecialSource(bits, *sources[n]);
				if (bits == 255 || (!mask_source && !source(bits, *sources[n])))
					return fail(pc, "invalid GCN VOP3 source");
				if (mask_source || sources[n]->kind == OperandKind::Sgpr) {
					if (scalar && *scalar != bits)
						return fail(pc, "VOP3 has multiple SGPR bus sources");
					scalar = bits;
				}
				if (compare && (absolute || negate)) {
					const uint32_t op = CompareBaseOpcode(inst.opcode_id);
					const bool floating = op < 64 || (op >= 200 && op <= 207);
					if (!floating) return fail(pc, "invalid VOP3 compare source modifier");
				}
				sources[n]->absolute = absolute;
				sources[n]->negate   = negate;
			}
		} else if ((word & 0xfe000000u) == 0x7c000000u) {
			const uint32_t op = (word >> 17) & 255;
			if (!DecodeCompareOpcode(op, inst))
				return fail(pc, "unsupported VOPC type/predicate");
			const uint32_t src0 = word & 511, src1 = (word >> 9) & 255;
			inst.dst.kind = OperandKind::VccLo;
			if (src0 == 250)
				return fail(pc, "DPP is not supported by the Gen4 VOPC profile");
			if (src0 == 249) {
				if (next == code.size()) return fail(pc, "truncated GCN VOPC SDWA");
				if (CompareSourcePair(inst, 0) || CompareSourcePair(inst, 1))
					return fail(pc, "SDWA compare does not accept 64-bit operands");
				const uint32_t ext = code[next++];
				if (ext & 0x40400000u) return fail(pc, "reserved VOPC SDWA modifier");
				if ((ext & 0x8000u) && !ScalarPair((ext >> 8) & 127, inst.dst))
					return fail(pc, "invalid VOPC SDWA destination pair");
				if (!(ext & 0x8000u) && (ext & 0x7f00u))
					return fail(pc, "reserved implicit VOPC SDWA destination");
				const auto decode_sdwa = [&](uint32_t bits, uint32_t fields, Operand& operand) {
					if (fields & 128) {
						if (bits == 255 ||
						    (!CompareSpecialSource(bits, operand) && !source(bits, operand)))
							return false;
					} else {
						operand = Vector(bits);
					}
					operand.sdwa_sel = fields & 7;
					operand.sdwa_sext = (fields & 8) != 0;
					operand.negate = (fields & 16) != 0;
					operand.absolute = (fields & 32) != 0;
					return operand.sdwa_sel <= 6;
				};
				if (!decode_sdwa(ext & 255, ext >> 16, inst.src0) ||
				    !decode_sdwa(src1, ext >> 24, inst.src1))
					return fail(pc, "invalid VOPC SDWA source");
				if ((ext & 0x80800000u) == 0x80800000u && (ext & 255) != src1)
					return fail(pc, "SDWA compare has multiple scalar bus sources");
				const bool floating = CompareBaseOpcode(op) < 16 || inst.data_bits == 16;
				if (!floating && (ext & 0x30300000u))
					return fail(pc, "floating-point modifier on integer SDWA compare");
			} else {
				if (!CompareSpecialSource(src0, inst.src0) && !source(src0, inst.src0))
					return fail(pc, "invalid VOPC source");
				inst.src1 = Vector(src1);
			}
			effect.vcc_write = inst.dst.kind == OperandKind::VccLo;
		} else if ((word & 0xfc000000u) == 0xc8000000u) {
			const uint32_t op = (word >> 16) & 3, attr = (word >> 10) & 63, chan = (word >> 8) & 3,
			               dst = (word >> 18) & 255, src = word & 255;
			if (stage != Stage::Pixel || attr >= parameter_count || op > 2 || dst < 2 || src != op)
				return fail(pc, "unsupported interpolation stage/attribute/operand");
			// Only adjacent canonical P1/P2 pairs are lowered to a native interpolant.
			// P1's intermediate value is not interchangeable with that interpolant.
			if (op == 0) {
				if (next == code.size() || code[next] != ((word | (1u << 16)) + 1))
					return fail(pc, "interpolation requires matching adjacent P1/P2");
			} else if (op == 1 && (pc == 0 || code[pc - 1] != ((word & ~(1u << 16)) - 1)))
				return fail(pc, "interpolation P2 lacks matching P1");
			inst.family    = Family::VINTRP;
			inst.opcode_id = op;
			inst.opcode    = op == 0 ? Opcode::V_INTERP_P1_F32 :
			                 op == 1 ? Opcode::V_INTERP_P2_F32 : Opcode::V_INTERP_MOV_F32;
			inst.dst       = Vector(dst);
			inst.src0      = Vector(src);
			if (op == 2) {
				inst.src0.kind = OperandKind::IntegerInlineConstant;
				inst.src0.value = src;
			}
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
					case 7: inst.opcode = Opcode::V_CVT_U32_F32; break;
					case 8: inst.opcode = Opcode::V_CVT_I32_F32; break;
					case 32: inst.opcode = Opcode::V_FRACT_F32; break;
					case 33: inst.opcode = Opcode::V_TRUNC_F32; break;
					case 34: inst.opcode = Opcode::V_CEIL_F32; break;
					case 35: inst.opcode = Opcode::V_RNDNE_F32; break;
					case 36: inst.opcode = Opcode::V_FLOOR_F32; break;
					case 37: inst.opcode = Opcode::V_EXP_F32; break;
					case 39: inst.opcode = Opcode::V_LOG_F32; break;
					case 42: inst.opcode = Opcode::V_RCP_F32; break;
					case 46: inst.opcode = Opcode::V_RSQ_F32; break;
					case 51: inst.opcode = Opcode::V_SQRT_F32; break;
					case 53: inst.opcode = Opcode::V_SIN_F32; break;
					case 54: inst.opcode = Opcode::V_COS_F32; break;
					case 55: inst.opcode = Opcode::V_NOT_B32; break;
					case 56: inst.opcode = Opcode::V_BFREV_B32; break;
					default: return fail(pc, "unsupported GCN VOP1 opcode");
				}
				inst.src_count = 1;
			} else if (!(word & 0x80000000u)) {
				inst.family    = Family::VOP2;
				inst.opcode_id = (word >> 25) & 63;
				switch (inst.opcode_id) {
					case 0: inst.opcode = Opcode::V_CNDMASK_B32; effect.vcc_read = true; break;
					case 3: inst.opcode = Opcode::V_ADD_F32; break;
					case 4: inst.opcode = Opcode::V_SUB_F32; break;
					case 5: inst.opcode = Opcode::V_SUBREV_F32; break;
					case 8: inst.opcode = Opcode::V_MUL_F32; break;
					case 15: inst.opcode = Opcode::V_MIN_F32; break;
					case 16: inst.opcode = Opcode::V_MAX_F32; break;
					case 17: inst.opcode = Opcode::V_MIN_I32; break;
					case 18: inst.opcode = Opcode::V_MAX_I32; break;
					case 19: inst.opcode = Opcode::V_MIN_U32; break;
					case 20: inst.opcode = Opcode::V_MAX_U32; break;
					case 21: inst.opcode = Opcode::V_LSHR_B32; break;
					case 22: inst.opcode = Opcode::V_LSHRREV_B32; break;
					case 23: inst.opcode = Opcode::V_ASHR_I32; break;
					case 24: inst.opcode = Opcode::V_ASHRREV_I32; break;
					case 25: inst.opcode = Opcode::V_LSHL_B32; break;
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
		if ((CompareSourcePair(inst, 0) && !IsValidComparePair(inst.src0)) ||
		    (CompareSourcePair(inst, 1) && !IsValidComparePair(inst.src1)))
			return fail(pc, "invalid 64-bit compare source");
		inst.word_count = uint32_t(next - pc);
		for (size_t i = pc; i < next; ++i)
			inst.raw[i - pc] = code[i];
		boundaries.emplace(inst.pc, program.instructions.size());
		program.instructions.push_back(inst);
		effects.push_back(effect);
		pc = next;
	}
	// Definite-assignment intersection over all incoming CFG edges.
	constexpr size_t Scc = 360, Vcc = 361, VccHi = 362;
	constexpr size_t MaskVcc = 104, MaskExec = 106, MaskWords = 108;
	const auto mask_word = [](const Operand& operand) -> size_t {
		switch (operand.kind) {
			case OperandKind::Sgpr: return operand.reg;
			case OperandKind::VccLo: return MaskVcc;
			case OperandKind::VccHi: return MaskVcc + 1;
			case OperandKind::ExecLo: return MaskExec;
			case OperandKind::ExecHi: return MaskExec + 1;
			default: return MaskWords;
		}
	};
	struct PcWord {
		int64_t offset;
		bool high;
		bool operator==(const PcWord&) const = default;
	};
	struct PcCarry {
		int64_t from, to;
		uint32_t high_adjust;
		bool subtract;
		bool operator==(const PcCarry&) const = default;
	};
	struct Flow {
		std::bitset<363> defined;
		std::bitset<256> active_vgprs; // Defined for every lane in the current EXEC.
		// Each bit identifies a scalar word equal to the corresponding entry EXEC half.
		std::array<std::bitset<MaskWords>, 2> entry_exec_words;
		std::array<std::optional<PcWord>, MaskWords> pc_words;
		std::optional<PcCarry> pc_carry;
		std::bitset<MaskWords> may_hold_pc;
		bool pc_scc = false;
		bool             may_export = false, must_export = false;
		uint32_t         may_parameters = 0, must_parameters = 0;
		bool operator==(const Flow&) const = default;
	};
	struct Incoming {
		Flow flow;
		bool queued = true;
	};
	constexpr size_t MaxPcContexts = 16;
	std::vector<std::vector<Incoming>> incoming(program.instructions.size());
	std::vector<std::vector<uint32_t>> indirect_targets(program.instructions.size());
	auto& initial = incoming[0].emplace_back().flow;
	initial.entry_exec_words[0].set(MaskExec);
	initial.entry_exec_words[1].set(MaskExec + 1);
	if (stage == Stage::Vertex)
		initial.defined[0] = true;
	else if (parameter_count) {
		initial.defined[0] = true;
		initial.defined[1] = true;
	}
	bool exit = false;
	std::deque<std::pair<size_t, size_t>> work {{0, 0}};
	const auto merge = [&](size_t to, const Flow& flow) {
		auto& states = incoming[to];
		for (size_t context = 0; context < states.size(); ++context) {
			auto& state = states[context];
			// Different code addresses must retain their own register facts.
			// Joining them would let a return acquire another caller's state.
			if (state.flow.pc_words != flow.pc_words || state.flow.pc_carry != flow.pc_carry ||
			    state.flow.may_hold_pc != flow.may_hold_pc || state.flow.pc_scc != flow.pc_scc)
				continue;
			Flow joined = state.flow;
			joined.defined &= flow.defined;
			for (size_t reg = 0; reg < joined.active_vgprs.size(); ++reg)
				joined.active_vgprs[reg] = (state.flow.defined[reg] || state.flow.active_vgprs[reg]) &&
				                           (flow.defined[reg] || flow.active_vgprs[reg]);
			for (uint32_t half = 0; half < 2; ++half)
				joined.entry_exec_words[half] &= flow.entry_exec_words[half];
			joined.may_export |= flow.may_export;
			joined.must_export &= flow.must_export;
			joined.may_parameters |= flow.may_parameters;
			joined.must_parameters &= flow.must_parameters;
			if (joined == state.flow) return true;
			state.flow = std::move(joined);
			if (!state.queued) {
				work.emplace_back(to, context);
				state.queued = true;
			}
			return true;
		}
		if (states.size() == MaxPcContexts) return false;
		work.emplace_back(to, states.size());
		states.push_back({flow});
		return true;
	};
	while (!work.empty()) {
		const auto [i, context] = work.front();
		work.pop_front();
		incoming[i][context].queued = false;
		auto& inst = program.instructions[i];
		const bool indirect = inst.opcode == Opcode::S_SETPC_B64 || inst.opcode == Opcode::S_SWAPPC_B64;
		const bool unconditional = indirect || inst.opcode == Opcode::S_BRANCH;
		const bool branch =
		    unconditional || ((effects[i].scc_read || effects[i].mask_branch) && inst.family == Family::SOPP);
		auto flow = incoming[i][context].flow;
		if (indirect) {
			const auto& lo = flow.pc_words[inst.src0.reg];
			const auto& hi = flow.pc_words[inst.src0.reg + 1];
			if (!lo || !hi || lo->high || !hi->high || lo->offset != hi->offset)
				return fail(inst.pc / 4, "indirect PC is not a proven code address");
			if (lo->offset < 0 || lo->offset >= int64_t(code.size_bytes()))
				return fail(inst.pc / 4, "indirect PC outside shader");
			const uint32_t resolved = uint32_t(lo->offset);
			auto& targets = indirect_targets[i];
			if (std::find(targets.begin(), targets.end(), resolved) == targets.end())
				targets.push_back(resolved);
			inst.branch_target = resolved;
		}
		size_t target = 0;
		if (branch) {
			auto it = boundaries.find(inst.branch_target);
			if (it == boundaries.end())
				return fail(inst.pc / 4, "branch into literal/non-instruction");
			target = it->second;
			if (program.instructions[target].opcode == Opcode::V_INTERP_P2_F32)
				return fail(inst.pc / 4, "branch into interpolation pair");
		}
		const auto vector_defined = [&](uint32_t reg) {
			return flow.defined[reg] || flow.active_vgprs[reg];
		};
		const auto& effect = effects[i];
		const bool full_exec = flow.entry_exec_words[0][MaskExec] &&
		                       flow.entry_exec_words[1][MaskExec + 1];
		if (effect.vcc_read && (!flow.defined[Vcc] || !flow.defined[VccHi]))
			return fail(inst.pc / 4, "undefined VCC");
		const bool writes_exec = inst.gcn_cmpx_updates_vcc_exec ||
		    inst.dst.kind == OperandKind::ExecLo || inst.dst.kind == OperandKind::ExecHi;
		// A pixel MRT export already carries EXEC through its VM bit. The
		// backend discards inactive invocations on return. Vertex positions
		// have no equivalent per-invocation discard.
		if (stage == Stage::Vertex &&
		    (inst.opcode == Opcode::EXP || inst.opcode == Opcode::S_ENDPGM) &&
		    !full_exec)
			return fail(inst.pc / 4, "export/end requires restored EXEC");
		const Operand sources[] {inst.src0, inst.src1, inst.src2, inst.src3};
		bool reads_pc = false;
		for (uint32_t n = 0; n < inst.src_count; ++n) {
			const auto&  source = sources[n];
			if (source.kind == OperandKind::Scc && !flow.defined[Scc])
				return fail(inst.pc / 4, "read of undefined SCC");
			if (source.kind == OperandKind::VccZ && (!flow.defined[Vcc] || !flow.defined[VccHi]))
				return fail(inst.pc / 4, "VCCZ requires both halves of VCC");
			const size_t reg    = source.kind == OperandKind::Sgpr ? source.reg + 256 : source.reg;
			if ((source.kind == OperandKind::VccLo && !flow.defined[Vcc]) ||
			    (source.kind == OperandKind::VccHi && !flow.defined[VccHi]))
				return fail(inst.pc / 4, "read of undefined VCC half");
			if ((source.kind == OperandKind::Vgpr && !vector_defined(source.reg)) ||
			    (source.kind == OperandKind::Sgpr && !flow.defined[reg]))
				return fail(inst.pc / 4, "read of undefined register on a control-flow path");
			const bool pair_read = ScalarSourceIsPair(inst, n) || CompareSourcePair(inst, n);
			const size_t word = mask_word(source);
			if (word != MaskWords) {
				for (size_t half = 0; half < (pair_read ? 2u : 1u); ++half) {
					if (!flow.may_hold_pc[word + half]) continue;
					if (!flow.pc_words[word + half])
						return fail(inst.pc / 4, "ambiguous PC value at CFG join");
					reads_pc = true;
				}
			}
			if (source.kind == OperandKind::Scc && flow.pc_scc)
				return fail(inst.pc / 4, "PC carry cannot be used as shader data");
			if (pair_read && source.kind == OperandKind::VccLo && !flow.defined[VccHi])
				return fail(inst.pc / 4, "read of undefined upper VCC in 64-bit operand");
			if (pair_read && ((source.kind == OperandKind::Vgpr && !vector_defined(source.reg + 1)) ||
			                  (source.kind == OperandKind::Sgpr && !flow.defined[reg + 1])))
				return fail(inst.pc / 4, source.kind == OperandKind::Sgpr ?
				    "read of undefined upper SGPR in B64 operand" : "read of undefined upper VGPR in B64 operand");
		}
		if (inst.family == Family::SMEM || inst.family == Family::MUBUF || inst.family == Family::MTBUF ||
		    inst.family == Family::MIMG) {
			const uint32_t base = inst.family == Family::MUBUF || inst.family == Family::MTBUF || inst.family == Family::MIMG
			                          ? inst.src1.reg
			                          : inst.src0.reg;
			const uint32_t descriptor_words = inst.family == Family::MIMG ? 8u :
			    (inst.family == Family::SMEM && inst.opcode_id <= 4 ? 2u : 4u);
			for (uint32_t n = 0; n < descriptor_words; ++n)
				if (flow.may_hold_pc[base + n])
					return fail(inst.pc / 4, "PC address cannot be used as a descriptor");
				else if (!flow.defined[256 + base + n])
					return fail(inst.pc / 4, "undefined buffer descriptor SGPR");
		}
		if (inst.family == Family::MIMG) {
			for (uint32_t n = 1; n < inst.image_address_components; ++n)
				if (!vector_defined(inst.src0.reg + n))
					return fail(inst.pc / 4, "undefined image coordinate group");
			if (inst.opcode == Opcode::IMAGE_SAMPLE) {
				for (uint32_t n = 0; n < 4; ++n) {
					if (flow.may_hold_pc[inst.src2.reg + n])
						return fail(inst.pc / 4, "PC address cannot be used as a sampler descriptor");
					if (!flow.defined[256 + inst.src2.reg + n])
						return fail(inst.pc / 4, "undefined sampler descriptor SGPR");
				}
			}
		}
		if (effect.scc_read && !flow.defined[Scc]) return fail(inst.pc / 4, "undefined SCC");
		const bool with_carry = inst.opcode == Opcode::S_ADDC_U32 || inst.opcode == Opcode::S_SUBB_U32;
		if (effect.scc_read && flow.pc_scc && !with_carry)
			return fail(inst.pc / 4, "PC carry cannot control shader execution");
		const bool buffer_store = inst.opcode == Opcode::BUFFER_STORE_DWORD;
		if (inst.dst.kind == OperandKind::Vgpr && !buffer_store) {
			if (stage == Stage::Pixel && parameter_count && inst.dst.reg < 2)
				return fail(inst.pc / 4, "overwriting interpolation coordinates is unsupported");
			if (inst.opcode != Opcode::V_INTERP_P1_F32)
				for (uint32_t n = 0;
				     n < ((inst.family == Family::MUBUF || inst.family == Family::MTBUF || inst.family == Family::MIMG)
				              ? inst.data_dwords
				              : 1u);
				     ++n) {
					if (stage == Stage::Pixel && parameter_count && inst.dst.reg + n < 2)
						return fail(inst.pc / 4, "MUBUF overwrites interpolation coordinates");
					if (!full_exec)
						flow.active_vgprs[inst.dst.reg + n] = true;
					else
						flow.defined[inst.dst.reg + n] = true;
				}
		}
		const uint32_t dst_words = inst.family == Family::VOPC ? 2 : inst.data_dwords;
		const size_t dst_word = mask_word(inst.dst);
		std::array<std::optional<PcWord>, 2> pc_result;
		const auto pc_word = [&](const Operand& operand, size_t half) -> std::optional<PcWord> {
			const size_t word = mask_word(operand);
			return word == MaskWords ? std::nullopt : flow.pc_words[word + half];
		};
		const auto integer = [](const Operand& operand) -> std::optional<uint32_t> {
			if (operand.kind == OperandKind::IntegerInlineConstant ||
			    operand.kind == OperandKind::LiteralConstant)
				return operand.value;
			return std::nullopt;
		};
		const auto carry = flow.pc_carry;
		const bool reads_pc_carry = effect.scc_read && flow.pc_scc;
		if (effect.scc_write) {
			flow.pc_carry.reset();
			flow.pc_scc = reads_pc || reads_pc_carry;
		}
		if (inst.opcode == Opcode::S_GETPC_B64 || inst.opcode == Opcode::S_SWAPPC_B64) {
			const int64_t next_pc = int64_t(inst.pc) + 4;
			pc_result = {PcWord{next_pc, false}, PcWord{next_pc, true}};
		} else if (inst.opcode == Opcode::S_MOV_B32 || inst.opcode == Opcode::S_MOV_B64) {
			pc_result[0] = pc_word(inst.src0, 0);
			if (dst_words == 2) pc_result[1] = pc_word(inst.src0, 1);
		} else if (inst.opcode == Opcode::S_ADD_U32 || inst.opcode == Opcode::S_SUB_U32 || with_carry) {
			const bool subtract = inst.opcode == Opcode::S_SUB_U32 || inst.opcode == Opcode::S_SUBB_U32;
			const auto address = pc_word(inst.src0, 0);
			const auto delta = integer(inst.src1);
			if (address && delta) {
				if (!with_carry && !address->high) {
					const int64_t signed_delta = std::bit_cast<int32_t>(*delta);
					const int64_t offset = address->offset + (subtract ? -signed_delta : signed_delta);
					pc_result[0] = PcWord{offset, false};
					flow.pc_carry = PcCarry{address->offset, offset,
					                       (*delta >> 31) ? UINT32_MAX : 0u, subtract};
				} else if (with_carry && address->high && carry && carry->subtract == subtract &&
				           carry->from == address->offset && carry->high_adjust == *delta) {
					pc_result[0] = PcWord{carry->to, true};
				}
			}
		}
		if (!indirect && (reads_pc || reads_pc_carry) &&
		    (inst.dst.kind != OperandKind::Sgpr || (!pc_result[0] && !pc_result[1])))
			return fail(inst.pc / 4, "PC address cannot be used as shader data");
		if (dst_word != MaskWords)
			for (uint32_t n = 0; n < dst_words; ++n) {
				flow.pc_words[dst_word + n] = n < pc_result.size() ? pc_result[n] : std::nullopt;
				flow.may_hold_pc.set(dst_word + n, flow.pc_words[dst_word + n].has_value());
			}
		if (dst_word != MaskWords) {
			const bool move = inst.opcode == Opcode::S_MOV_B32 || inst.opcode == Opcode::S_MOV_B64;
			const bool select = inst.opcode == Opcode::S_CSELECT_B32 ||
			                    inst.opcode == Opcode::S_CSELECT_B64 || inst.opcode == Opcode::S_CMOV_B64;
			const size_t src_word = effect.mask_save ? MaskExec : mask_word(inst.src0);
			const size_t other_word = mask_word(inst.src1);
			for (auto& entry_words : flow.entry_exec_words) {
				// Sample all sources before overwriting an aliased destination.
				std::bitset<MaskWords> copied;
				for (uint32_t n = 0; n < dst_words; ++n) {
					const bool copy = (move || effect.mask_save || select) &&
					    src_word != MaskWords && entry_words[src_word + n] &&
					    (!select || (other_word != MaskWords && entry_words[other_word + n]));
					copied.set(dst_word + n, copy);
				}
				for (uint32_t n = 0; n < dst_words; ++n)
					entry_words.set(dst_word + n, copied[dst_word + n]);
			}
		}
		if (inst.dst.kind == OperandKind::Sgpr)
			for (uint32_t n = 0; n < dst_words; ++n)
				flow.defined[256 + inst.dst.reg + n] = true;
		if (effect.mask_save || inst.gcn_cmpx_updates_vcc_exec)
			for (auto& entry_words : flow.entry_exec_words) {
				entry_words.reset(MaskExec);
				entry_words.reset(MaskExec + 1);
			}
		if (effect.mask_save || inst.gcn_cmpx_updates_vcc_exec) {
			flow.pc_words[MaskExec].reset();
			flow.pc_words[MaskExec + 1].reset();
		}
		if (effects[i].scc_write) flow.defined[Scc] = true;
		if (effects[i].vcc_write || inst.dst.kind == OperandKind::VccLo) flow.defined[Vcc] = true;
		if (effects[i].vcc_write || inst.dst.kind == OperandKind::VccHi) flow.defined[VccHi] = true;
		// A restriction cannot expose a lane outside the current EXEC. In
		// particular CMPX gates its result with incoming EXEC in the translator.
		const bool exec_lo = inst.dst.kind == OperandKind::ExecLo;
		const bool exec_hi = inst.dst.kind == OperandKind::ExecHi;
		const auto same_half = [&](const Operand& operand) {
			return (exec_lo && operand.kind == OperandKind::ExecLo) ||
			       (exec_hi && operand.kind == OperandKind::ExecHi);
		};
		const bool narrows_exec = inst.family == Family::VOPC ||
		    inst.opcode == Opcode::S_AND_SAVEEXEC_B64 ||
		    ((inst.opcode == Opcode::S_AND_B32 || inst.opcode == Opcode::S_AND_B64) &&
		     (same_half(inst.src0) || same_half(inst.src1))) ||
		    ((inst.opcode == Opcode::S_ANDN2_B32 || inst.opcode == Opcode::S_ANDN2_B64) &&
		     same_half(inst.src0));
		if ((writes_exec || effect.mask_save) && !narrows_exec)
			flow.active_vgprs.reset();
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
		if (branch && !merge(target, flow)) return fail(inst.pc / 4, "PC context budget exceeded");
		if (!unconditional) {
			if (i + 1 == program.instructions.size())
				return fail(inst.pc / 4, "path falls off shader");
			if (!merge(i + 1, flow)) return fail(inst.pc / 4, "PC context budget exceeded");
		}
	}
	for (size_t i = 0; i < incoming.size(); ++i)
		if (incoming[i].empty()) return fail(program.instructions[i].pc / 4, "unreachable instruction/trailing code");
	if (!exit) return fail(code.size(), "missing reachable s_endpgm");
	for (size_t i = 0; i < indirect_targets.size(); ++i) {
		auto& targets = indirect_targets[i];
		auto& inst = program.instructions[i];
		if (targets.empty()) continue;
		if (targets.size() > 1) {
			if (targets.size() > std::size(inst.gcn_indirect.pcs))
				return fail(inst.pc / 4, "indirect target budget exceeded");
			std::sort(targets.begin(), targets.end());
			inst.branch_target = Decoder::ProvenIndirectTargets;
			inst.gcn_indirect = {};
			inst.gcn_indirect.count = uint32_t(targets.size());
			std::copy(targets.begin(), targets.end(), inst.gcn_indirect.pcs);
		} else if (inst.opcode == Opcode::S_SETPC_B64) {
			inst.opcode = Opcode::S_BRANCH;
		}
	}
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
std::vector<uint32_t> TypedFetchFixture() {
	// GCN MTBUF FLOAT32x4, two indexed V# resources, same semantic fetch ABI.
	const uint32_t load = 0xe8002000u | (3u << 16) | (14u << 19) | (7u << 23);
	return {load, 0x80000200u, load, 0x80010600u, 0xbf8c0f70u,
	        0xf80008cfu, 0x05040302u, 0xf800020fu, 0x09080706u, 0xbf810000u};
}
std::vector<uint32_t> ExternalFetchVertexFixture() {
	// Initial S_SWAPPC s0..1,s4..5. s2..3 points to the V# table.
	return {0xbe802104u, 0xf80008cfu, 0x05040302u, 0xf800020fu, 0x09080706u, 0xbf810000u};
}
std::vector<uint32_t> ExternalFetchFunctionFixture() {
	auto code = TypedFetchFixture();
	code.resize(5); // loads and wait only
	code[1] = 0x80020200u; // descriptor s8..11
	code[3] = 0x80030600u; // descriptor s12..15
	code.insert(code.begin(), {0xc0c40300u, 0xbf8c007fu}); // S_LOAD_DWORDX8 s8..15,s2..3,0
	code.push_back(0xbe802000u); // S_SETPC s0..1
	return code;
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
std::vector<uint32_t> SampledTextureFixture(Stage stage) {
	// One raw buffer s0..3, S# s4..7 and T# s8..15 fit the PS4 user SGPR ABI.
	// Consume dynamic UVs from PARAM0 or generated vertex coordinates.
	auto code = stage == Stage::Vertex ? InterpolatedVertexFixture()
	                                  : InterpolatedPixelFixture();
	if (stage == Stage::Vertex) code.resize(code.size() - 5); // remove POS0/PARAM0/end
	else code.resize(code.size() - 3); // remove MRT0/end
	const uint32_t uv = stage == Stage::Vertex ? 6 : 2;
	const std::initializer_list<uint32_t> load {
	    0xc2880100u, 0xbf8c007fu}; // s_buffer_load_dwordx4 s16..19,s0..3,0
	code.insert(code.end(), load);
	code.push_back(Vop1(1, uv + 2, 128)); // explicit float LOD0
	code.insert(code.end(), {0xf0900f00u, 0x00220c00u | uv}); // sampled RGBA -> v12..15
	for (uint32_t c = 0; c < 4; ++c)
		code.push_back(Vop2(8, 12 + c, 16 + c, 12 + c)); // dynamic buffer modulation
	if (stage == Stage::Vertex)
		code.insert(code.end(), {0xf80008cfu, 0x05040302u, 0xf800020fu, 0x0f0e0d0cu,
		                         0xbf810000u});
	else
		code.insert(code.end(), {0xf800180fu, 0x0f0e0d0cu, 0xbf810000u});
	return code;
}
std::vector<uint32_t> TableSampledTextureFixture(Stage stage) {
	auto code = SampledTextureFixture(stage);
	for (size_t i = 0; i < code.size(); ++i) {
		if (code[i] == 0xc2880100u) {
			code[i] = 0xc0000100u | (10u << 22) | (40u << 15) | (8u << 9);
			code.insert(code.begin() + i + 1,
			            0xc0000100u | (10u << 22) | (44u << 15) | (10u << 9));
			++i;
		} else if (code[i] == 0xf0900f00u) {
			// T# s24..31, S# s32..35, same vaddr/vdata.
			code[i + 1] = (code[i + 1] & 65535) | (6u << 16) | (8u << 21);
			++i;
		} else {
			for (uint32_t c = 0; c < 4; ++c)
				if (code[i] == Vop2(8, 12 + c, 16 + c, 12 + c)) {
					code[i] = Vop2(8, 12 + c, 40 + c, 12 + c);
					code.insert(code.begin() + i + 1, Vop2(8, 12 + c, 44 + c, 12 + c));
					++i;
					break;
				}
		}
	}
	// Initial guest pointer lives in the real two user SGPRs s0..1.
	// Table: two V# (8 words), T# (8 words), S# (4 words).
	code.insert(code.begin(), {0xc0000100u | (4u << 22) | (16u << 15),
	                           0xc0000110u | (2u << 22) | (32u << 15), 0xbf8c007fu});
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
std::vector<uint32_t> MaskedPixelFixture(bool nested, bool select) {
	auto code = InterpolatedPixelFixture();
	code.resize(code.size() - 3);
	code.push_back(Vop1(1, 10, 240)); // .5
	code.push_back(0x7c000000u | (1u << 17) | (10u << 9) | 258u); // red < .5 -> VCC
	if (select) {
		code.insert(code.end(), {Vop1(1, 11, 255), std::bit_cast<uint32_t>(.75f), Vop2(0, 2, 258, 11)});
	} else {
		code.push_back(0xbe90246au); // s_and_saveexec_b64 s16..17,vcc
		if (nested) {
			code.push_back(0x7c000000u | (1u << 17) | (10u << 9) | 259u); // green < .5
			code.push_back(0xbe92246au); // nested save s18..19
		}
		code.insert(code.end(), {Vop1(1, 2, 255), std::bit_cast<uint32_t>(.75f)});
		if (nested) code.push_back(0xbefe0412u);
		code.push_back(0xbefe0410u); // restore original EXEC from s16..17
	}
	code.insert(code.end(), {0xf800180fu, 0x05040302u, 0xbf810000u});
	return code;
}
std::vector<uint32_t> ElsePixelFixture(bool guarded, bool nested, uint32_t predicate) {
	auto code = InterpolatedPixelFixture();
	code.resize(code.size() - 3);
	code.push_back(Vop1(1, 10, 240));
	const auto compare = [&](uint32_t op, uint32_t reg) {
		code.push_back(0x7c000000u | (op << 17) | (10u << 9) | (256u + reg));
	};
	const auto guard = [&]() {
		const size_t pc = code.size();
		if (guarded) code.push_back(0xbf880000u);
		return pc;
	};
	const auto patch = [&](size_t pc) {
		if (guarded) code[pc] |= uint32_t(code.size() - pc - 1);
	};
	const auto color = [&](float value) {
		code.insert(code.end(), {Vop1(1, 2, 255), std::bit_cast<uint32_t>(value)});
	};
	compare(predicate, 2);
	code.push_back(0xbe90246au);
	const auto outer_then = guard();
	if (nested) {
		compare(1, 3);
		code.push_back(0xbe92246au);
		const auto inner_then = guard();
		color(.75f);
		patch(inner_then);
		code.push_back(0x89fe7e12u); // EXEC = saved18 XOR EXEC
		const auto inner_else = guard();
		color(.125f);
		patch(inner_else);
		code.push_back(0xbefe0412u);
	} else color(.75f);
	patch(outer_then);
	code.push_back(0x89fe7e10u); // EXEC = saved16 XOR EXEC
	const auto outer_else = guard();
	color(nested ? .375f : .125f);
	patch(outer_else);
	code.push_back(0xbefe0410u);
	code.insert(code.end(), {0xf800180fu, 0x05040302u, 0xbf810000u});
	return code;
}
namespace {
void IntegerCompareInputs(std::vector<uint32_t>& code) {
	code.insert(code.end(), {Vop2(8, 11, 255, 2), std::bit_cast<uint32_t>(8.f),
	    Vop1(7, 11, 267), Vop2(26, 11, 157, 11), // uint(u*8) <<29: crosses signed boundary
	    Vop1(1, 12, 255), 0x40000000u});
}
}
std::vector<uint32_t> IntegerComparePixelFixture(bool unsigned_compare) {
	auto code = InterpolatedPixelFixture();
	code.resize(code.size() - 3);
	IntegerCompareInputs(code);
	code.insert(code.end(), {Vop1(1, 14, 255), std::bit_cast<uint32_t>(.75f),
	                         Vop1(1, 15, 255), std::bit_cast<uint32_t>(.125f),
	                         0x7c000000u | ((unsigned_compare ? 193u : 129u) << 17) | (12u << 9) | 267u,
	                         Vop2(0, 2, 271, 14), 0xf800180fu, 0x05040302u, 0xbf810000u});
	return code;
}
std::vector<uint32_t> CmpxPixelFixture(uint32_t kind) {
	auto code = InterpolatedPixelFixture();
	code.resize(code.size() - 3);
	code.insert(code.end(), {Vop1(1, 10, 255), std::bit_cast<uint32_t>(.75f),
	                         Vop1(1, 14, 255), std::bit_cast<uint32_t>(.875f), Vop1(1, 15, 240)});
	// Correct false blue is .5, chosen independently of the red branch.
	if (kind >= 3) IntegerCompareInputs(code);
	else code.push_back(Vop1(1, 11, 240));
	const uint32_t op = kind == 1 ? 16 : kind == 2 ? 31 : kind == 3 ? 177 : kind == 4 ? 241 : 17;
	const uint32_t compare = 0x7c000000u | (op << 17) | ((kind >= 3 ? 12u : 11u) << 9) |
	                         (kind >= 3 ? 266u : 259u);
	code.insert(code.end(), {0x7c000000u | (1u << 17) | (10u << 9) | 258u, // outer red < .75
	    Vop1(1, 2, 255), std::bit_cast<uint32_t>(.125f), // initialize lanes outside outer mask
	});
	if (kind >= 3)
		code.insert(code.end(), {Vop1(1, 10, 128), Vop1(1, 13, 268), Vop1(1, 12, 128)});
	code.insert(code.end(), {0xbe90246au, compare,
	    0xbe94046au, 0xbeea0414u, // VCC -> s[20:21] -> VCC
	    0xbeea036au, 0xbeeb036bu, // VCC_LO/VCC_HI through MOV_B32
	    0xbefe037eu, 0xbeff037fu, // EXEC_LO/EXEC_HI through MOV_B32
	    0xbf880006u, 0xbe940382u, // Skip loop when EXEC is empty. s20 = 2.
	    Vop2(3, 2, 255, 2), std::bit_cast<uint32_t>(.3125f),
	    0x80948114u, 0xbf078014u, 0xbf85fffbu, // --s20; branch while nonzero
	    0x89fe7e10u, 0xbf880002u, Vop1(1, 2, 255), std::bit_cast<uint32_t>(.125f), 0xbefe0410u,
	    Vop2(0, 4, 271, 14), // VCC must also hold the CMPX result after restoring EXEC
	    0xf800180fu, 0x05040302u, 0xbf810000u});
	return code;
}
std::vector<uint32_t> FlatPixelFixture() {
	std::vector<uint32_t> code;
	for (uint32_t channel = 0; channel < 4; ++channel)
		code.push_back(0xc8020002u | ((channel + 2) << 18) | (channel << 8));
	code.insert(code.end(), {0xf800180fu, 0x05040302u, 0xbf810000u});
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
