// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Inlined in WasmBytecodeGenerator::EncodeInstruction.

// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD

#define SPLAT_CASE(format, stype, valType, op_type, num) \
  case kExpr##format##Splat: {                           \
    EMIT_INSTR_HANDLER(s2s_Simd##format##Splat);         \
    op_type##Pop();                                      \
    S128Push();                                          \
    return RegMode::kNoReg;                              \
  }
SPLAT_CASE(F64x2, float2, double, F64, 2)
SPLAT_CASE(F32x4, float4, float, F32, 4)
SPLAT_CASE(I64x2, int2, int64_t, I64, 2)
SPLAT_CASE(I32x4, int4, int32_t, I32, 4)
SPLAT_CASE(I16x8, int8, int32_t, I32, 8)
SPLAT_CASE(I8x16, int16, int32_t, I32, 16)
#undef SPLAT_CASE

#define EXTRACT_LANE_CASE(format, stype, op_type, name) \
  case kExpr##format##ExtractLane: {                    \
    EMIT_INSTR_HANDLER(s2s_Simd##format##ExtractLane);  \
    /* emit 8 bits ? */                                 \
    EmitI16Const(instr.optional.simd_lane);             \
    S128Pop();                                          \
    op_type##Push();                                    \
    return RegMode::kNoReg;                             \
  }
EXTRACT_LANE_CASE(F64x2, float2, F64, f64x2)
EXTRACT_LANE_CASE(F32x4, float4, F32, f32x4)
EXTRACT_LANE_CASE(I64x2, int2, I64, i64x2)
EXTRACT_LANE_CASE(I32x4, int4, I32, i32x4)
#undef EXTRACT_LANE_CASE

#define EXTRACT_LANE_EXTEND_CASE(format, stype, name, sign, extended_type) \
  case kExpr##format##ExtractLane##sign: {                                 \
    EMIT_INSTR_HANDLER(s2s_Simd##format##ExtractLane##sign);               \
    /* emit 8 bits ? */                                                    \
    EmitI16Const(instr.optional.simd_lane);                                \
    S128Pop();                                                             \
    I32Push();                                                             \
    return RegMode::kNoReg;                                                \
  }
EXTRACT_LANE_EXTEND_CASE(I16x8, int8, i16x8, S, int32_t)
EXTRACT_LANE_EXTEND_CASE(I16x8, int8, i16x8, U, uint32_t)
EXTRACT_LANE_EXTEND_CASE(I8x16, int16, i8x16, S, int32_t)
EXTRACT_LANE_EXTEND_CASE(I8x16, int16, i8x16, U, uint32_t)
#undef EXTRACT_LANE_EXTEND_CASE

#define BINOP_CASE(op, name, stype, count, expr) \
  case kExpr##op: {                              \
    EMIT_INSTR_HANDLER(s2s_Simd##op);            \
    S128Pop();                                   \
    S128Pop();                                   \
    S128Push();                                  \
    return RegMode::kNoReg;                      \
  }
BINOP_CASE(F64x2Add, f64x2, float2, 2, a + b)
BINOP_CASE(F64x2Sub, f64x2, float2, 2, a - b)
BINOP_CASE(F64x2Mul, f64x2, float2, 2, a* b)
BINOP_CASE(F64x2Div, f64x2, float2, 2, base::Divide(a, b))
BINOP_CASE(F64x2Min, f64x2, float2, 2, JSMin(a, b))
BINOP_CASE(F64x2Max, f64x2, float2, 2, JSMax(a, b))
BINOP_CASE(F64x2Pmin, f64x2, float2, 2, std::min(a, b))
BINOP_CASE(F64x2Pmax, f64x2, float2, 2, std::max(a, b))
BINOP_CASE(F32x4RelaxedMin, f32x4, float4, 4, std::min(a, b))
BINOP_CASE(F32x4RelaxedMax, f32x4, float4, 4, std::max(a, b))
BINOP_CASE(F64x2RelaxedMin, f64x2, float2, 2, std::min(a, b))
BINOP_CASE(F64x2RelaxedMax, f64x2, float2, 2, std::max(a, b))
BINOP_CASE(F32x4Add, f32x4, float4, 4, a + b)
BINOP_CASE(F32x4Sub, f32x4, float4, 4, a - b)
BINOP_CASE(F32x4Mul, f32x4, float4, 4, a* b)
BINOP_CASE(F32x4Div, f32x4, float4, 4, a / b)
BINOP_CASE(F32x4Min, f32x4, float4, 4, JSMin(a, b))
BINOP_CASE(F32x4Max, f32x4, float4, 4, JSMax(a, b))
BINOP_CASE(F32x4Pmin, f32x4, float4, 4, std::min(a, b))
BINOP_CASE(F32x4Pmax, f32x4, float4, 4, std::max(a, b))
BINOP_CASE(I64x2Add, i64x2, int2, 2, base::AddWithWraparound(a, b))
BINOP_CASE(I64x2Sub, i64x2, int2, 2, base::SubWithWraparound(a, b))
BINOP_CASE(I64x2Mul, i64x2, int2, 2, base::MulWithWraparound(a, b))
BINOP_CASE(I32x4Add, i32x4, int4, 4, base::AddWithWraparound(a, b))
BINOP_CASE(I32x4Sub, i32x4, int4, 4, base::SubWithWraparound(a, b))
BINOP_CASE(I32x4Mul, i32x4, int4, 4, base::MulWithWraparound(a, b))
BINOP_CASE(I32x4MinS, i32x4, int4, 4, a < b ? a : b)
BINOP_CASE(I32x4MinU, i32x4, int4, 4,
           static_cast<uint32_t>(a) < static_cast<uint32_t>(b) ? a : b)
BINOP_CASE(I32x4MaxS, i32x4, int4, 4, a > b ? a : b)
BINOP_CASE(I32x4MaxU, i32x4, int4, 4,
           static_cast<uint32_t>(a) > static_cast<uint32_t>(b) ? a : b)
BINOP_CASE(S128And, i32x4, int4, 4, a& b)
BINOP_CASE(S128Or, i32x4, int4, 4, a | b)
BINOP_CASE(S128Xor, i32x4, int4, 4, a ^ b)
BINOP_CASE(S128AndNot, i32x4, int4, 4, a & ~b)
BINOP_CASE(I16x8Add, i16x8, int8, 8, base::AddWithWraparound(a, b))
BINOP_CASE(I16x8Sub, i16x8, int8, 8, base::SubWithWraparound(a, b))
BINOP_CASE(I16x8Mul, i16x8, int8, 8, base::MulWithWraparound(a, b))
BINOP_CASE(I16x8MinS, i16x8, int8, 8, a < b ? a : b)
BINOP_CASE(I16x8MinU, i16x8, int8, 8,
           static_cast<uint16_t>(a) < static_cast<uint16_t>(b) ? a : b)
BINOP_CASE(I16x8MaxS, i16x8, int8, 8, a > b ? a : b)
BINOP_CASE(I16x8MaxU, i16x8, int8, 8,
           static_cast<uint16_t>(a) > static_cast<uint16_t>(b) ? a : b)
BINOP_CASE(I16x8AddSatS, i16x8, int8, 8, SaturateAdd<int16_t>(a, b))
BINOP_CASE(I16x8AddSatU, i16x8, int8, 8, SaturateAdd<uint16_t>(a, b))
BINOP_CASE(I16x8SubSatS, i16x8, int8, 8, SaturateSub<int16_t>(a, b))
BINOP_CASE(I16x8SubSatU, i16x8, int8, 8, SaturateSub<uint16_t>(a, b))
BINOP_CASE(I16x8RoundingAverageU, i16x8, int8, 8,
           RoundingAverageUnsigned<uint16_t>(a, b))
BINOP_CASE(I16x8Q15MulRSatS, i16x8, int8, 8,
           SaturateRoundingQMul<int16_t>(a, b))
BINOP_CASE(I16x8RelaxedQ15MulRS, i16x8, int8, 8,
           SaturateRoundingQMul<int16_t>(a, b))
BINOP_CASE(I8x16Add, i8x16, int16, 16, base::AddWithWraparound(a, b))
BINOP_CASE(I8x16Sub, i8x16, int16, 16, base::SubWithWraparound(a, b))
BINOP_CASE(I8x16MinS, i8x16, int16, 16, a < b ? a : b)
BINOP_CASE(I8x16MinU, i8x16, int16, 16,
           static_cast<uint8_t>(a) < static_cast<uint8_t>(b) ? a : b)
BINOP_CASE(I8x16MaxS, i8x16, int16, 16, a > b ? a : b)
BINOP_CASE(I8x16MaxU, i8x16, int16, 16,
           static_cast<uint8_t>(a) > static_cast<uint8_t>(b) ? a : b)
BINOP_CASE(I8x16AddSatS, i8x16, int16, 16, SaturateAdd<int8_t>(a, b))
BINOP_CASE(I8x16AddSatU, i8x16, int16, 16, SaturateAdd<uint8_t>(a, b))
BINOP_CASE(I8x16SubSatS, i8x16, int16, 16, SaturateSub<int8_t>(a, b))
BINOP_CASE(I8x16SubSatU, i8x16, int16, 16, SaturateSub<uint8_t>(a, b))
BINOP_CASE(I8x16RoundingAverageU, i8x16, int16, 16,
           RoundingAverageUnsigned<uint8_t>(a, b))
#undef BINOP_CASE

#define UNOP_CASE(op, name, stype, count, expr) \
  case kExpr##op: {                             \
    EMIT_INSTR_HANDLER(s2s_Simd##op);           \
    S128Pop();                                  \
    S128Push();                                 \
    return RegMode::kNoReg;                     \
  }
UNOP_CASE(F64x2Abs, f64x2, float2, 2, std::abs(a))
UNOP_CASE(F64x2Neg, f64x2, float2, 2, -a)
UNOP_CASE(F64x2Sqrt, f64x2, float2, 2, std::sqrt(a))
UNOP_CASE(F64x2Ceil, f64x2, float2, 2, (AixFpOpWorkaround<double, &ceil>(a)))
UNOP_CASE(F64x2Floor, f64x2, float2, 2, (AixFpOpWorkaround<double, &floor>(a)))
UNOP_CASE(F64x2Trunc, f64x2, float2, 2, (AixFpOpWorkaround<double, &trunc>(a)))
UNOP_CASE(F64x2NearestInt, f64x2, float2, 2,
          (AixFpOpWorkaround<double, &nearbyint>(a)))
UNOP_CASE(F32x4Abs, f32x4, float4, 4, std::abs(a))
UNOP_CASE(F32x4Neg, f32x4, float4, 4, -a)
UNOP_CASE(F32x4Sqrt, f32x4, float4, 4, std::sqrt(a))
UNOP_CASE(F32x4Ceil, f32x4, float4, 4, (AixFpOpWorkaround<float, &ceilf>(a)))
UNOP_CASE(F32x4Floor, f32x4, float4, 4, (AixFpOpWorkaround<float, &floorf>(a)))
UNOP_CASE(F32x4Trunc, f32x4, float4, 4, (AixFpOpWorkaround<float, &truncf>(a)))
UNOP_CASE(F32x4NearestInt, f32x4, float4, 4,
          (AixFpOpWorkaround<float, &nearbyintf>(a)))
UNOP_CASE(I64x2Neg, i64x2, int2, 2, base::NegateWithWraparound(a))
UNOP_CASE(I32x4Neg, i32x4, int4, 4, base::NegateWithWraparound(a))
// Use llabs which will work correctly on both 64-bit and 32-bit.
UNOP_CASE(I64x2Abs, i64x2, int2, 2, std::llabs(a))
UNOP_CASE(I32x4Abs, i32x4, int4, 4, std::abs(a))
UNOP_CASE(S128Not, i32x4, int4, 4, ~a)
UNOP_CASE(I16x8Neg, i16x8, int8, 8, base::NegateWithWraparound(a))
UNOP_CASE(I16x8Abs, i16x8, int8, 8, std::abs(a))
UNOP_CASE(I8x16Neg, i8x16, int16, 16, base::NegateWithWraparound(a))
UNOP_CASE(I8x16Abs, i8x16, int16, 16, std::abs(a))
UNOP_CASE(I8x16Popcnt, i8x16, int16, 16,
          base::bits::CountPopulation<uint8_t>(a))
#undef UNOP_CASE

#define BITMASK_CASE(op, name, stype, count) \
  case kExpr##op: {                          \
    EMIT_INSTR_HANDLER(s2s_Simd##op);        \
    S128Pop();                               \
    I32Push();                               \
    return RegMode::kNoReg;                  \
  }
BITMASK_CASE(I8x16BitMask, i8x16, int16, 16)
BITMASK_CASE(I16x8BitMask, i16x8, int8, 8)
BITMASK_CASE(I32x4BitMask, i32x4, int4, 4)
BITMASK_CASE(I64x2BitMask, i64x2, int2, 2)
#undef BITMASK_CASE

#define CMPOP_CASE(op, name, stype, out_stype, count, expr) \
  case kExpr##op: {                                         \
    EMIT_INSTR_HANDLER(s2s_Simd##op);                       \
    S128Pop();                                              \
    S128Pop();                                              \
    S128Push();                                             \
    return RegMode::kNoReg;                                 \
  }
CMPOP_CASE(F64x2Eq, f64x2, float2, int2, 2, a == b)
CMPOP_CASE(F64x2Ne, f64x2, float2, int2, 2, a != b)
CMPOP_CASE(F64x2Gt, f64x2, float2, int2, 2, a > b)
CMPOP_CASE(F64x2Ge, f64x2, float2, int2, 2, a >= b)
CMPOP_CASE(F64x2Lt, f64x2, float2, int2, 2, a < b)
CMPOP_CASE(F64x2Le, f64x2, float2, int2, 2, a <= b)
CMPOP_CASE(F32x4Eq, f32x4, float4, int4, 4, a == b)
CMPOP_CASE(F32x4Ne, f32x4, float4, int4, 4, a != b)
CMPOP_CASE(F32x4Gt, f32x4, float4, int4, 4, a > b)
CMPOP_CASE(F32x4Ge, f32x4, float4, int4, 4, a >= b)
CMPOP_CASE(F32x4Lt, f32x4, float4, int4, 4, a < b)
CMPOP_CASE(F32x4Le, f32x4, float4, int4, 4, a <= b)
CMPOP_CASE(I64x2Eq, i64x2, int2, int2, 2, a == b)
CMPOP_CASE(I64x2Ne, i64x2, int2, int2, 2, a != b)
CMPOP_CASE(I64x2LtS, i64x2, int2, int2, 2, a < b)
CMPOP_CASE(I64x2GtS, i64x2, int2, int2, 2, a > b)
CMPOP_CASE(I64x2LeS, i64x2, int2, int2, 2, a <= b)
CMPOP_CASE(I64x2GeS, i64x2, int2, int2, 2, a >= b)
CMPOP_CASE(I32x4Eq, i32x4, int4, int4, 4, a == b)
CMPOP_CASE(I32x4Ne, i32x4, int4, int4, 4, a != b)
CMPOP_CASE(I32x4GtS, i32x4, int4, int4, 4, a > b)
CMPOP_CASE(I32x4GeS, i32x4, int4, int4, 4, a >= b)
CMPOP_CASE(I32x4LtS, i32x4, int4, int4, 4, a < b)
CMPOP_CASE(I32x4LeS, i32x4, int4, int4, 4, a <= b)
CMPOP_CASE(I32x4GtU, i32x4, int4, int4, 4,
           static_cast<uint32_t>(a) > static_cast<uint32_t>(b))
CMPOP_CASE(I32x4GeU, i32x4, int4, int4, 4,
           static_cast<uint32_t>(a) >= static_cast<uint32_t>(b))
CMPOP_CASE(I32x4LtU, i32x4, int4, int4, 4,
           static_cast<uint32_t>(a) < static_cast<uint32_t>(b))
CMPOP_CASE(I32x4LeU, i32x4, int4, int4, 4,
           static_cast<uint32_t>(a) <= static_cast<uint32_t>(b))
CMPOP_CASE(I16x8Eq, i16x8, int8, int8, 8, a == b)
CMPOP_CASE(I16x8Ne, i16x8, int8, int8, 8, a != b)
CMPOP_CASE(I16x8GtS, i16x8, int8, int8, 8, a > b)
CMPOP_CASE(I16x8GeS, i16x8, int8, int8, 8, a >= b)
CMPOP_CASE(I16x8LtS, i16x8, int8, int8, 8, a < b)
CMPOP_CASE(I16x8LeS, i16x8, int8, int8, 8, a <= b)
CMPOP_CASE(I16x8GtU, i16x8, int8, int8, 8,
           static_cast<uint16_t>(a) > static_cast<uint16_t>(b))
CMPOP_CASE(I16x8GeU, i16x8, int8, int8, 8,
           static_cast<uint16_t>(a) >= static_cast<uint16_t>(b))
CMPOP_CASE(I16x8LtU, i16x8, int8, int8, 8,
           static_cast<uint16_t>(a) < static_cast<uint16_t>(b))
CMPOP_CASE(I16x8LeU, i16x8, int8, int8, 8,
           static_cast<uint16_t>(a) <= static_cast<uint16_t>(b))
CMPOP_CASE(I8x16Eq, i8x16, int16, int16, 16, a == b)
CMPOP_CASE(I8x16Ne, i8x16, int16, int16, 16, a != b)
CMPOP_CASE(I8x16GtS, i8x16, int16, int16, 16, a > b)
CMPOP_CASE(I8x16GeS, i8x16, int16, int16, 16, a >= b)
CMPOP_CASE(I8x16LtS, i8x16, int16, int16, 16, a < b)
CMPOP_CASE(I8x16LeS, i8x16, int16, int16, 16, a <= b)
CMPOP_CASE(I8x16GtU, i8x16, int16, int16, 16,
           static_cast<uint8_t>(a) > static_cast<uint8_t>(b))
CMPOP_CASE(I8x16GeU, i8x16, int16, int16, 16,
           static_cast<uint8_t>(a) >= static_cast<uint8_t>(b))
CMPOP_CASE(I8x16LtU, i8x16, int16, int16, 16,
           static_cast<uint8_t>(a) < static_cast<uint8_t>(b))
CMPOP_CASE(I8x16LeU, i8x16, int16, int16, 16,
           static_cast<uint8_t>(a) <= static_cast<uint8_t>(b))
#undef CMPOP_CASE

#define REPLACE_LANE_CASE(format, name, stype, ctype, op_type) \
  case kExpr##format##ReplaceLane: {                           \
    EMIT_INSTR_HANDLER(s2s_Simd##format##ReplaceLane);         \
    /* emit 8 bits ? */                                        \
    EmitI16Const(instr.optional.simd_lane);                    \
    op_type##Pop();                                            \
    S128Pop();                                                 \
    S128Push();                                                \
    return RegMode::kNoReg;                                    \
  }
REPLACE_LANE_CASE(F64x2, f64x2, float2, double, F64)
REPLACE_LANE_CASE(F32x4, f32x4, float4, float, F32)
REPLACE_LANE_CASE(I64x2, i64x2, int2, int64_t, I64)
REPLACE_LANE_CASE(I32x4, i32x4, int4, int32_t, I32)
REPLACE_LANE_CASE(I16x8, i16x8, int8, int32_t, I32)
REPLACE_LANE_CASE(I8x16, i8x16, int16, int32_t, I32)
#undef REPLACE_LANE_CASE

case kExprS128LoadMem: {
  EMIT_INSTR_HANDLER(s2s_SimdS128LoadMem, instr.pc);
  EmitI64Const(instr.optional.offset);
  I32Pop();
  S128Push();
  return RegMode::kNoReg;
}

case kExprS128StoreMem: {
  EMIT_INSTR_HANDLER(s2s_SimdS128StoreMem, instr.pc);
  S128Pop();
  EmitI64Const(instr.optional.offset);
  I32Pop();
  return RegMode::kNoReg;
}

#define SHIFT_CASE(op, name, stype, count, expr) \
  case kExpr##op: {                              \
    EMIT_INSTR_HANDLER(s2s_Simd##op);            \
    I32Pop();                                    \
    S128Pop();                                   \
    S128Push();                                  \
    return RegMode::kNoReg;                      \
  }
  SHIFT_CASE(I64x2Shl, i64x2, int2, 2, static_cast<uint64_t>(a) << (shift % 64))
  SHIFT_CASE(I64x2ShrS, i64x2, int2, 2, a >> (shift % 64))
  SHIFT_CASE(I64x2ShrU, i64x2, int2, 2,
             static_cast<uint64_t>(a) >> (shift % 64))
  SHIFT_CASE(I32x4Shl, i32x4, int4, 4, static_cast<uint32_t>(a) << (shift % 32))
  SHIFT_CASE(I32x4ShrS, i32x4, int4, 4, a >> (shift % 32))
  SHIFT_CASE(I32x4ShrU, i32x4, int4, 4,
             static_cast<uint32_t>(a) >> (shift % 32))
  SHIFT_CASE(I16x8Shl, i16x8, int8, 8, static_cast<uint16_t>(a) << (shift % 16))
  SHIFT_CASE(I16x8ShrS, i16x8, int8, 8, a >> (shift % 16))
  SHIFT_CASE(I16x8ShrU, i16x8, int8, 8,
             static_cast<uint16_t>(a) >> (shift % 16))
  SHIFT_CASE(I8x16Shl, i8x16, int16, 16, static_cast<uint8_t>(a) << (shift % 8))
  SHIFT_CASE(I8x16ShrS, i8x16, int16, 16, a >> (shift % 8))
  SHIFT_CASE(I8x16ShrU, i8x16, int16, 16,
             static_cast<uint8_t>(a) >> (shift % 8))
#undef SHIFT_CASE

#define EXT_MUL_CASE(op)              \
  case kExpr##op: {                   \
    EMIT_INSTR_HANDLER(s2s_Simd##op); \
    S128Pop();                        \
    S128Pop();                        \
    S128Push();                       \
    return RegMode::kNoReg;           \
  }
  EXT_MUL_CASE(I16x8ExtMulLowI8x16S)
  EXT_MUL_CASE(I16x8ExtMulHighI8x16S)
  EXT_MUL_CASE(I16x8ExtMulLowI8x16U)
  EXT_MUL_CASE(I16x8ExtMulHighI8x16U)
  EXT_MUL_CASE(I32x4ExtMulLowI16x8S)
  EXT_MUL_CASE(I32x4ExtMulHighI16x8S)
  EXT_MUL_CASE(I32x4ExtMulLowI16x8U)
  EXT_MUL_CASE(I32x4ExtMulHighI16x8U)
  EXT_MUL_CASE(I64x2ExtMulLowI32x4S)
  EXT_MUL_CASE(I64x2ExtMulHighI32x4S)
  EXT_MUL_CASE(I64x2ExtMulLowI32x4U)
  EXT_MUL_CASE(I64x2ExtMulHighI32x4U)
#undef EXT_MUL_CASE

#define CONVERT_CASE(op, src_type, name, dst_type, count, start_index, ctype, \
                     expr)                                                    \
  case kExpr##op: {                                                           \
    EMIT_INSTR_HANDLER(s2s_Simd##op);                                         \
    S128Pop();                                                                \
    S128Push();                                                               \
    return RegMode::kNoReg;                                                   \
  }
  CONVERT_CASE(F32x4SConvertI32x4, int4, i32x4, float4, 4, 0, int32_t,
               static_cast<float>(a))
  CONVERT_CASE(F32x4UConvertI32x4, int4, i32x4, float4, 4, 0, uint32_t,
               static_cast<float>(a))
  CONVERT_CASE(I32x4SConvertF32x4, float4, f32x4, int4, 4, 0, float,
               base::saturated_cast<int32_t>(a))
  CONVERT_CASE(I32x4UConvertF32x4, float4, f32x4, int4, 4, 0, float,
               base::saturated_cast<uint32_t>(a))
  CONVERT_CASE(I32x4RelaxedTruncF32x4S, float4, f32x4, int4, 4, 0, float,
               base::saturated_cast<int32_t>(a))
  CONVERT_CASE(I32x4RelaxedTruncF32x4U, float4, f32x4, int4, 4, 0, float,
               base::saturated_cast<uint32_t>(a))
  CONVERT_CASE(I64x2SConvertI32x4Low, int4, i32x4, int2, 2, 0, int32_t, a)
  CONVERT_CASE(I64x2SConvertI32x4High, int4, i32x4, int2, 2, 2, int32_t, a)
  CONVERT_CASE(I64x2UConvertI32x4Low, int4, i32x4, int2, 2, 0, uint32_t, a)
  CONVERT_CASE(I64x2UConvertI32x4High, int4, i32x4, int2, 2, 2, uint32_t, a)
  CONVERT_CASE(I32x4SConvertI16x8High, int8, i16x8, int4, 4, 4, int16_t, a)
  CONVERT_CASE(I32x4UConvertI16x8High, int8, i16x8, int4, 4, 4, uint16_t, a)
  CONVERT_CASE(I32x4SConvertI16x8Low, int8, i16x8, int4, 4, 0, int16_t, a)
  CONVERT_CASE(I32x4UConvertI16x8Low, int8, i16x8, int4, 4, 0, uint16_t, a)
  CONVERT_CASE(I16x8SConvertI8x16High, int16, i8x16, int8, 8, 8, int8_t, a)
  CONVERT_CASE(I16x8UConvertI8x16High, int16, i8x16, int8, 8, 8, uint8_t, a)
  CONVERT_CASE(I16x8SConvertI8x16Low, int16, i8x16, int8, 8, 0, int8_t, a)
  CONVERT_CASE(I16x8UConvertI8x16Low, int16, i8x16, int8, 8, 0, uint8_t, a)
  CONVERT_CASE(F64x2ConvertLowI32x4S, int4, i32x4, float2, 2, 0, int32_t,
               static_cast<double>(a))
  CONVERT_CASE(F64x2ConvertLowI32x4U, int4, i32x4, float2, 2, 0, uint32_t,
               static_cast<double>(a))
  CONVERT_CASE(I32x4TruncSatF64x2SZero, float2, f64x2, int4, 2, 0, double,
               base::saturated_cast<int32_t>(a))
  CONVERT_CASE(I32x4TruncSatF64x2UZero, float2, f64x2, int4, 2, 0, double,
               base::saturated_cast<uint32_t>(a))
  CONVERT_CASE(I32x4RelaxedTruncF64x2SZero, float2, f64x2, int4, 2, 0, double,
               base::saturated_cast<int32_t>(a))
  CONVERT_CASE(I32x4RelaxedTruncF64x2UZero, float2, f64x2, int4, 2, 0, double,
               base::saturated_cast<uint32_t>(a))
  CONVERT_CASE(F32x4DemoteF64x2Zero, float2, f64x2, float4, 2, 0, float,
               DoubleToFloat32(a))
  CONVERT_CASE(F64x2PromoteLowF32x4, float4, f32x4, float2, 2, 0, float,
               static_cast<double>(a))
#undef CONVERT_CASE

#define PACK_CASE(op, src_type, name, dst_type, count, dst_ctype) \
  case kExpr##op: {                                               \
    EMIT_INSTR_HANDLER(s2s_Simd##op);                             \
    S128Pop();                                                    \
    S128Pop();                                                    \
    S128Push();                                                   \
    return RegMode::kNoReg;                                       \
  }
  PACK_CASE(I16x8SConvertI32x4, int4, i32x4, int8, 8, int16_t)
  PACK_CASE(I16x8UConvertI32x4, int4, i32x4, int8, 8, uint16_t)
  PACK_CASE(I8x16SConvertI16x8, int8, i16x8, int16, 16, int8_t)
  PACK_CASE(I8x16UConvertI16x8, int8, i16x8, int16, 16, uint8_t)
#undef PACK_CASE

#define SELECT_CASE(op)               \
  case kExpr##op: {                   \
    EMIT_INSTR_HANDLER(s2s_Simd##op); \
    S128Pop();                        \
    S128Pop();                        \
    S128Pop();                        \
    S128Push();                       \
    return RegMode::kNoReg;           \
  }
  SELECT_CASE(I8x16RelaxedLaneSelect)
  SELECT_CASE(I16x8RelaxedLaneSelect)
  SELECT_CASE(I32x4RelaxedLaneSelect)
  SELECT_CASE(I64x2RelaxedLaneSelect)
  SELECT_CASE(S128Select)
#undef SELECT_CASE

case kExprI32x4DotI16x8S: {
  EMIT_INSTR_HANDLER(s2s_SimdI32x4DotI16x8S);
  S128Pop();
  S128Pop();
  S128Push();
  return RegMode::kNoReg;
}

case kExprS128Const: {
  PushConstSlot<Simd128>(simd_immediates_[instr.optional.simd_immediate_index]);
  return RegMode::kNoReg;
}

case kExprI16x8DotI8x16I7x16S: {
  EMIT_INSTR_HANDLER(s2s_SimdI16x8DotI8x16I7x16S);
  S128Pop();
  S128Pop();
  S128Push();
  return RegMode::kNoReg;
}

case kExprI32x4DotI8x16I7x16AddS: {
  EMIT_INSTR_HANDLER(s2s_SimdI32x4DotI8x16I7x16AddS);
  S128Pop();
  S128Pop();
  S128Pop();
  S128Push();
  return RegMode::kNoReg;
}

case kExprI8x16RelaxedSwizzle: {
  EMIT_INSTR_HANDLER(s2s_SimdI8x16RelaxedSwizzle);
  S128Pop();
  S128Pop();
  S128Push();
  return RegMode::kNoReg;
}

case kExprI8x16Swizzle: {
  EMIT_INSTR_HANDLER(s2s_SimdI8x16Swizzle);
  S128Pop();
  S128Pop();
  S128Push();
  return RegMode::kNoReg;
}

case kExprI8x16Shuffle: {
  uint32_t slot_index =
      CreateConstSlot(simd_immediates_[instr.optional.simd_immediate_index]);
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  TracePushConstSlot(slot_index);
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
  EMIT_INSTR_HANDLER(s2s_SimdI8x16Shuffle);
  PushSlot(slot_index);
  S128Pop();
  S128Pop();
  S128Pop();
  S128Push();
  return RegMode::kNoReg;
}

case kExprV128AnyTrue: {
  EMIT_INSTR_HANDLER(s2s_SimdV128AnyTrue);
  S128Pop();
  I32Push();
  return RegMode::kNoReg;
}

#define REDUCTION_CASE(op, name, stype, count, operation) \
  case kExpr##op: {                                       \
    EMIT_INSTR_HANDLER(s2s_Simd##op);                     \
    S128Pop();                                            \
    I32Push();                                            \
    return RegMode::kNoReg;                               \
  }
  REDUCTION_CASE(I64x2AllTrue, i64x2, int2, 2, &)
  REDUCTION_CASE(I32x4AllTrue, i32x4, int4, 4, &)
  REDUCTION_CASE(I16x8AllTrue, i16x8, int8, 8, &)
  REDUCTION_CASE(I8x16AllTrue, i8x16, int16, 16, &)
#undef REDUCTION_CASE

#define QFM_CASE(op, name, stype, count, operation) \
  case kExpr##op: {                                 \
    EMIT_INSTR_HANDLER(s2s_Simd##op);               \
    S128Pop();                                      \
    S128Pop();                                      \
    S128Pop();                                      \
    S128Push();                                     \
    return RegMode::kNoReg;                         \
  }
  QFM_CASE(F32x4Qfma, f32x4, float4, 4, +)
  QFM_CASE(F32x4Qfms, f32x4, float4, 4, -)
  QFM_CASE(F64x2Qfma, f64x2, float2, 2, +)
  QFM_CASE(F64x2Qfms, f64x2, float2, 2, -)
#undef QFM_CASE

#define LOAD_SPLAT_CASE(op)                         \
  case kExprS128##op: {                             \
    EMIT_INSTR_HANDLER(s2s_SimdS128##op, instr.pc); \
    EmitI64Const(instr.optional.offset);            \
    I32Pop();                                       \
    S128Push();                                     \
    return RegMode::kNoReg;                         \
  }
  LOAD_SPLAT_CASE(Load8Splat)
  LOAD_SPLAT_CASE(Load16Splat)
  LOAD_SPLAT_CASE(Load32Splat)
  LOAD_SPLAT_CASE(Load64Splat)
#undef LOAD_SPLAT_CASE

#define LOAD_EXTEND_CASE(op)                        \
  case kExprS128##op: {                             \
    EMIT_INSTR_HANDLER(s2s_SimdS128##op, instr.pc); \
    EmitI64Const(instr.optional.offset);            \
    I32Pop();                                       \
    S128Push();                                     \
    return RegMode::kNoReg;                         \
  }
  LOAD_EXTEND_CASE(Load8x8S)
  LOAD_EXTEND_CASE(Load8x8U)
  LOAD_EXTEND_CASE(Load16x4S)
  LOAD_EXTEND_CASE(Load16x4U)
  LOAD_EXTEND_CASE(Load32x2S)
  LOAD_EXTEND_CASE(Load32x2U)
#undef LOAD_EXTEND_CASE

#define LOAD_ZERO_EXTEND_CASE(op, load_type)        \
  case kExprS128##op: {                             \
    EMIT_INSTR_HANDLER(s2s_SimdS128##op, instr.pc); \
    EmitI64Const(instr.optional.offset);            \
    I32Pop();                                       \
    S128Push();                                     \
    return RegMode::kNoReg;                         \
  }
  LOAD_ZERO_EXTEND_CASE(Load32Zero, I32)
  LOAD_ZERO_EXTEND_CASE(Load64Zero, I64)
#undef LOAD_ZERO_EXTEND_CASE

#define LOAD_LANE_CASE(op)                                   \
  case kExprS128##op: {                                      \
    EMIT_INSTR_HANDLER(s2s_SimdS128##op, instr.pc);          \
    S128Pop();                                               \
    EmitI64Const(instr.optional.simd_loadstore_lane.offset); \
    I32Pop();                                                \
    /* emit 8 bits ? */                                      \
    EmitI16Const(instr.optional.simd_loadstore_lane.lane);   \
    S128Push();                                              \
    return RegMode::kNoReg;                                  \
  }
  LOAD_LANE_CASE(Load8Lane)
  LOAD_LANE_CASE(Load16Lane)
  LOAD_LANE_CASE(Load32Lane)
  LOAD_LANE_CASE(Load64Lane)
#undef LOAD_LANE_CASE

#define STORE_LANE_CASE(op)                                  \
  case kExprS128##op: {                                      \
    EMIT_INSTR_HANDLER(s2s_SimdS128##op, instr.pc);          \
    S128Pop();                                               \
    EmitI64Const(instr.optional.simd_loadstore_lane.offset); \
    I32Pop();                                                \
    /* emit 8 bits ? */                                      \
    EmitI16Const(instr.optional.simd_loadstore_lane.lane);   \
    return RegMode::kNoReg;                                  \
  }
  STORE_LANE_CASE(Store8Lane)
  STORE_LANE_CASE(Store16Lane)
  STORE_LANE_CASE(Store32Lane)
  STORE_LANE_CASE(Store64Lane)
#undef STORE_LANE_CASE

#define EXT_ADD_PAIRWISE_CASE(op)     \
  case kExpr##op: {                   \
    EMIT_INSTR_HANDLER(s2s_Simd##op); \
    S128Pop();                        \
    S128Push();                       \
    return RegMode::kNoReg;           \
  }
  EXT_ADD_PAIRWISE_CASE(I32x4ExtAddPairwiseI16x8S)
  EXT_ADD_PAIRWISE_CASE(I32x4ExtAddPairwiseI16x8U)
  EXT_ADD_PAIRWISE_CASE(I16x8ExtAddPairwiseI8x16S)
  EXT_ADD_PAIRWISE_CASE(I16x8ExtAddPairwiseI8x16U)
#undef EXT_ADD_PAIRWISE_CASE
