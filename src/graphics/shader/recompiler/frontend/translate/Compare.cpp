#include "graphics/shader/recompiler/frontend/translate/Translator.h"

#include <bit>
#include <utility>

namespace Libs::Graphics::ShaderRecompiler::Frontend {

void Translator::EmitCompareResult(const Decoder::Instruction& inst, IR::U1 value, bool scalar) {
	if (scalar) {
		ir.SetScc(value);
		return;
	}
	if (inst.gcn_cmpx_updates_vcc_exec) {
		// The incoming EXEC gates both results. Writing VCC first leaves EXEC
		// intact for the second update; no scalar condition code is modified.
		WriteVectorMask(inst.dst, value);
		WriteVectorMask(ConditionOperand(Decoder::OperandKind::ExecLo), value);
		return;
	}
	WriteVectorMask(inst.dst, value);
}

void Translator::EmitCompareConstant(const Decoder::Instruction& inst, bool value, bool scalar) {
	EmitCompareResult(inst, IR::U1(IR::Value(value)), scalar);
}

void Translator::EmitIntegerCompare(const Decoder::Instruction& inst, IR::ValueOpcode opcode,
                                    IR::Type type, bool scalar, bool swap_operands) {
	// The equality compares share their IR opcode with the unsigned ones.
	const bool signed_64 = opcode == IR::ValueOpcode::SLessThan64 ||
	                       opcode == IR::ValueOpcode::SLessThanEqual64 ||
	                       inst.opcode == Decoder::Opcode::V_CMP_EQ_I64 ||
	                       inst.opcode == Decoder::Opcode::V_CMPX_EQ_I64 ||
	                       inst.opcode == Decoder::Opcode::V_CMP_NE_I64 ||
	                       inst.opcode == Decoder::Opcode::V_CMPX_NE_I64;
	const auto read = [&](const Decoder::Operand& operand) {
		// RDNA2 expands signed 64-bit integer literals by sign extension.
		if (signed_64 && operand.kind == Decoder::OperandKind::LiteralConstant) {
			return IR::Value(static_cast<uint64_t>(
			    static_cast<int64_t>(std::bit_cast<int32_t>(operand.value))));
		}
		return ReadOperand(operand, type);
	};
	auto lhs = read(inst.src0);
	auto rhs = read(inst.src1);
	if (swap_operands) {
		std::swap(lhs, rhs);
	}
	EmitCompareResult(inst, IR::U1(ir.Emit(opcode, {lhs, rhs})), scalar);
}

void Translator::EmitInteger16Compare(const Decoder::Instruction& inst, IR::ValueOpcode opcode,
                                      bool signed_value) {
	const auto lhs = ReadU16AsU32(inst.src0, signed_value);
	const auto rhs = ReadU16AsU32(inst.src1, signed_value);
	EmitCompareResult(inst, IR::U1(ir.Emit(opcode, {lhs, rhs})), false);
}

void Translator::EmitFloatCompare(const Decoder::Instruction& inst, IR::ValueOpcode opcode,
                                  bool half) {
	half |= inst.family == Decoder::Family::VOPC && inst.data_bits == 16;
	const auto type = IR::ArgTypeOf(opcode, 0);
	const auto lhs =
	    half ? IR::Value(ReadF16AsF32(inst.src0)) : ReadOperand(inst.src0, type);
	const auto rhs =
	    half ? IR::Value(ReadF16AsF32(inst.src1)) : ReadOperand(inst.src1, type);
	const IR::FPCompareFlags flags{
	    .flush_input_denorms = !half && type == IR::Type::F32 && flush_f32_inputs};
	EmitCompareResult(inst, IR::U1(ir.Emit(opcode, {lhs, rhs}, flags)), false);
}

void Translator::EmitFloatOrderedCompare(const Decoder::Instruction& inst, bool ordered,
                                         bool is64) {
	const auto type      = is64 ? IR::Type::F64 : IR::Type::F32;
	const auto is_nan_op = is64 ? IR::ValueOpcode::FPIsNan64 : IR::ValueOpcode::FPIsNan32;
	const bool half      = inst.family == Decoder::Family::VOPC && inst.data_bits == 16;
	const auto lhs       = half ? IR::Value(ReadF16AsF32(inst.src0)) : ReadOperand(inst.src0, type);
	const auto rhs       = half ? IR::Value(ReadF16AsF32(inst.src1)) : ReadOperand(inst.src1, type);
	const auto unordered = ir.LogicalOr(IR::U1(ir.Emit(is_nan_op, {lhs})),
	                                    IR::U1(ir.Emit(is_nan_op, {rhs})));
	EmitCompareResult(inst, ordered ? ir.LogicalNot(unordered) : unordered, false);
}

void Translator::EmitFloatClassCompare(const Decoder::Instruction& inst, bool half) {
	if (inst.family == Decoder::Family::VOPC && inst.data_bits == 64) {
		const auto constant = [](uint32_t value) { return IR::U32(IR::Value(value)); };
		const auto [lo, hi] = inst.src0.kind == Decoder::OperandKind::LiteralConstant
		    ? std::array{constant(0), constant(inst.src0.value)} : ReadU32Pair(inst.src0);
		const auto exponent = ir.BitwiseAnd(hi, constant(0x7ff00000));
		const auto fraction = ir.BitwiseOr(lo, ir.BitwiseAnd(hi, constant(0x000fffff)));
		const auto is_negative = ir.INotEqual(ir.BitwiseAnd(hi, constant(0x80000000)), constant(0));
		const auto has_fraction = ir.INotEqual(fraction, constant(0));
		const auto is_special = ir.IEqual(exponent, constant(0x7ff00000));
		const auto has_zero_exp = ir.IEqual(exponent, constant(0));
		const auto is_quiet = ir.INotEqual(ir.BitwiseAnd(hi, constant(0x00080000)), constant(0));

		// AMD class bits 2..9 are ordered from -infinity to +infinity.
		const auto magnitude = ir.Select(is_special, constant(3),
		    ir.Select(has_zero_exp, ir.Select(has_fraction, constant(1), constant(0)), constant(2)));
		const auto signed_class = ir.Select(is_negative, ir.ISub(constant(5), magnitude),
		    ir.IAdd(constant(6), magnitude));
		const auto nan_class = ir.Select(is_quiet, constant(1), constant(0));
		const auto index = ir.Select(ir.LogicalAnd(is_special, has_fraction), nan_class, signed_class);
		const auto mask = IR::U32(ReadOperand(inst.src1, IR::Type::U32));
		EmitCompareResult(inst, ir.INotEqual(ir.BitwiseAnd(mask,
		    ir.ShiftLeftLogical(constant(1), index)), constant(0)), false);
		return;
	}

	const auto value = half ? IR::Value(Read16LaneBits(inst.src0, false))
	                        : ReadOperand(inst.src0, IR::Type::F32);
	const auto mask = half ? IR::Value(Read16LaneBits(inst.src1, false))
	                      : ReadOperand(inst.src1, IR::Type::U32);
	const auto opcode = half ? IR::ValueOpcode::FPCmpClass16 : IR::ValueOpcode::FPCmpClass32;
	EmitCompareResult(inst, IR::U1(ir.Emit(opcode, {value, mask})), false);
}

} // namespace Libs::Graphics::ShaderRecompiler::Frontend
