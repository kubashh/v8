// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Inlined in wasm-interpreter.cc.

// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD

#if V8_TARGET_BIG_ENDIAN
#define LANE(i, type) ((sizeof(type.val) / sizeof(type.val[0])) - (i)-1)
#else
#define LANE(i, type) (i)
#endif

#define SPLAT_CASE(format, stype, valType, op_type, num)                       \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##format##Splat(                            \
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime, \
      int64_t r0, double fp0) {                                                \
    valType v = pop<valType>(sp, code, wasm_runtime);                          \
    stype s;                                                                   \
    for (int i = 0; i < num; i++) s.val[i] = v;                                \
    push<Simd128>(sp, code, wasm_runtime, Simd128(s));                         \
    NextOp();                                                                  \
  }
SPLAT_CASE(F64x2, float2, double, F64, 2)
SPLAT_CASE(F32x4, float4, float, F32, 4)
SPLAT_CASE(I64x2, int2, int64_t, I64, 2)
SPLAT_CASE(I32x4, int4, int32_t, I32, 4)
SPLAT_CASE(I16x8, int8, int32_t, I32, 8)
SPLAT_CASE(I8x16, int16, int32_t, I32, 16)
#undef SPLAT_CASE

#define EXTRACT_LANE_CASE(format, stype, op_type, name)                        \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##format##ExtractLane(                      \
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime, \
      int64_t r0, double fp0) {                                                \
    uint16_t lane = ReadI16(code);                                             \
    DCHECK_LT(lane, 4);                                                        \
    Simd128 v = pop<Simd128>(sp, code, wasm_runtime);                          \
    stype s = v.to_##name();                                                   \
    push(sp, code, wasm_runtime, s.val[LANE(lane, s)]);                        \
    NextOp();                                                                  \
  }
EXTRACT_LANE_CASE(F64x2, float2, F64, f64x2)
EXTRACT_LANE_CASE(F32x4, float4, F32, f32x4)
EXTRACT_LANE_CASE(I64x2, int2, I64, i64x2)
EXTRACT_LANE_CASE(I32x4, int4, I32, i32x4)
#undef EXTRACT_LANE_CASE

// Unsigned extracts require a bit more care. The underlying array in Simd128 is
// signed (see wasm-value.h), so when casted to uint32_t it will be signed
// extended, e.g. int8_t -> int32_t -> uint32_t. So for unsigned extracts, we
// will cast it int8_t -> uint8_t -> uint32_t. We add the DCHECK to ensure that
// if the array type changes, we know to change this function.
#define EXTRACT_LANE_EXTEND_CASE(format, stype, name, sign, extended_type)     \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##format##ExtractLane##sign(                \
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime, \
      int64_t r0, double fp0) {                                                \
    uint16_t lane = ReadI16(code);                                             \
    DCHECK_LT(lane, 16);                                                       \
    Simd128 s = pop<Simd128>(sp, code, wasm_runtime);                          \
    stype ss = s.to_##name();                                                  \
    auto res = ss.val[LANE(lane, ss)];                                         \
    DCHECK(std::is_signed<decltype(res)>::value);                              \
    if (std::is_unsigned<extended_type>::value) {                              \
      using unsigned_type = std::make_unsigned<decltype(res)>::type;           \
      push(sp, code, wasm_runtime,                                             \
           static_cast<extended_type>(static_cast<unsigned_type>(res)));       \
    } else {                                                                   \
      push(sp, code, wasm_runtime, static_cast<extended_type>(res));           \
    }                                                                          \
    NextOp();                                                                  \
  }
EXTRACT_LANE_EXTEND_CASE(I16x8, int8, i16x8, S, int32_t)
EXTRACT_LANE_EXTEND_CASE(I16x8, int8, i16x8, U, uint32_t)
EXTRACT_LANE_EXTEND_CASE(I8x16, int16, i8x16, S, int32_t)
EXTRACT_LANE_EXTEND_CASE(I8x16, int16, i8x16, U, uint32_t)
#undef EXTRACT_LANE_EXTEND_CASE

#define BINOP_CASE(op, name, stype, count, expr)                              \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    stype s2 = pop<Simd128>(sp, code, wasm_runtime).to_##name();              \
    stype s1 = pop<Simd128>(sp, code, wasm_runtime).to_##name();              \
    stype res;                                                                \
    for (size_t i = 0; i < count; ++i) {                                      \
      auto a = s1.val[LANE(i, s1)];                                           \
      auto b = s2.val[LANE(i, s2)];                                           \
      res.val[LANE(i, res)] = expr;                                           \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
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

#define UNOP_CASE(op, name, stype, count, expr)                               \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    stype s = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    stype res;                                                                \
    for (size_t i = 0; i < count; ++i) {                                      \
      auto a = s.val[LANE(i, s)];                                             \
      res.val[LANE(i, res)] = expr;                                           \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
  }
UNOP_CASE(F64x2Abs, f64x2, float2, 2, std::abs(a))
UNOP_CASE(F64x2Neg, f64x2, float2, 2, -a)
UNOP_CASE(F64x2Sqrt, f64x2, float2, 2, std::sqrt(a))
UNOP_CASE(F64x2Ceil, f64x2, float2, 2, ceil(a))
UNOP_CASE(F64x2Floor, f64x2, float2, 2, floor(a))
UNOP_CASE(F64x2Trunc, f64x2, float2, 2, trunc(a))
UNOP_CASE(F64x2NearestInt, f64x2, float2, 2, nearbyint(a))
UNOP_CASE(F32x4Abs, f32x4, float4, 4, std::abs(a))
UNOP_CASE(F32x4Neg, f32x4, float4, 4, -a)
UNOP_CASE(F32x4Sqrt, f32x4, float4, 4, std::sqrt(a))
UNOP_CASE(F32x4Ceil, f32x4, float4, 4, ceilf(a))
UNOP_CASE(F32x4Floor, f32x4, float4, 4, floorf(a))
UNOP_CASE(F32x4Trunc, f32x4, float4, 4, truncf(a))
UNOP_CASE(F32x4NearestInt, f32x4, float4, 4, nearbyintf(a))
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

#define BITMASK_CASE(op, name, stype, count)                                  \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    stype s = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    int32_t res = 0;                                                          \
    for (size_t i = 0; i < count; ++i) {                                      \
      bool sign = std::signbit(static_cast<double>(s.val[LANE(i, s)]));       \
      res |= (sign << i);                                                     \
    }                                                                         \
    push<int32_t>(sp, code, wasm_runtime, res);                               \
    NextOp();                                                                 \
  }
BITMASK_CASE(I8x16BitMask, i8x16, int16, 16)
BITMASK_CASE(I16x8BitMask, i16x8, int8, 8)
BITMASK_CASE(I32x4BitMask, i32x4, int4, 4)
BITMASK_CASE(I64x2BitMask, i64x2, int2, 2)
#undef BITMASK_CASE

#define CMPOP_CASE(op, name, stype, out_stype, count, expr)                   \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    stype s2 = pop<Simd128>(sp, code, wasm_runtime).to_##name();              \
    stype s1 = pop<Simd128>(sp, code, wasm_runtime).to_##name();              \
    out_stype res;                                                            \
    for (size_t i = 0; i < count; ++i) {                                      \
      auto a = s1.val[LANE(i, s1)];                                           \
      auto b = s2.val[LANE(i, s2)];                                           \
      auto result = expr;                                                     \
      res.val[LANE(i, res)] = result ? -1 : 0;                                \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
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

#define REPLACE_LANE_CASE(format, name, stype, ctype, op_type)                 \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##format##ReplaceLane(                      \
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime, \
      int64_t r0, double fp0) {                                                \
    uint16_t lane = ReadI16(code);                                             \
    DCHECK_LT(lane, 16);                                                       \
    ctype new_val = pop<ctype>(sp, code, wasm_runtime);                        \
    Simd128 simd_val = pop<Simd128>(sp, code, wasm_runtime);                   \
    stype s = simd_val.to_##name();                                            \
    s.val[LANE(lane, s)] = new_val;                                            \
    push<Simd128>(sp, code, wasm_runtime, Simd128(s));                         \
    NextOp();                                                                  \
  }
REPLACE_LANE_CASE(F64x2, f64x2, float2, double, F64)
REPLACE_LANE_CASE(F32x4, f32x4, float4, float, F32)
REPLACE_LANE_CASE(I64x2, i64x2, int2, int64_t, I64)
REPLACE_LANE_CASE(I32x4, i32x4, int4, int32_t, I32)
REPLACE_LANE_CASE(I16x8, i16x8, int8, int32_t, I32)
REPLACE_LANE_CASE(I8x16, i8x16, int16, int32_t, I32)
#undef REPLACE_LANE_CASE

INSTRUCTION_HANDLER_FUNC s2s_SimdS128LoadMem(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  uint8_t* memory_start = wasm_runtime->GetMemoryStart();
  uint64_t offset = Read<uint64_t>(code);

  uint64_t index = pop<uint32_t>(sp, code, wasm_runtime);
  uint64_t effective_index = offset + index;

  if (V8_UNLIKELY(effective_index < index ||
                  !base::IsInBounds<uint64_t>(effective_index, sizeof(Simd128),
                                              wasm_runtime->GetMemorySize()))) {
    TRAP(TrapReason::kTrapMemOutOfBounds)
  }

  uint8_t* address = memory_start + effective_index;
  Simd128 s =
      base::ReadUnalignedValue<Simd128>(reinterpret_cast<Address>(address));
  push<Simd128>(sp, code, wasm_runtime, Simd128(s));

  NextOp();
}

INSTRUCTION_HANDLER_FUNC s2s_SimdS128StoreMem(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  Simd128 val = pop<Simd128>(sp, code, wasm_runtime);

  uint8_t* memory_start = wasm_runtime->GetMemoryStart();
  uint64_t offset = Read<uint64_t>(code);

  uint64_t index = pop<uint32_t>(sp, code, wasm_runtime);
  uint64_t effective_index = offset + index;

  if (V8_UNLIKELY(effective_index < index ||
                  !base::IsInBounds<uint64_t>(effective_index, sizeof(Simd128),
                                              wasm_runtime->GetMemorySize()))) {
    TRAP(TrapReason::kTrapMemOutOfBounds)
  }

  uint8_t* address = memory_start + effective_index;
  base::WriteUnalignedValue<Simd128>(reinterpret_cast<Address>(address), val);

  NextOp();
}

#define SHIFT_CASE(op, name, stype, count, expr)                              \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    uint32_t shift = pop<uint32_t>(sp, code, wasm_runtime);                   \
    stype s = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    stype res;                                                                \
    for (size_t i = 0; i < count; ++i) {                                      \
      auto a = s.val[LANE(i, s)];                                             \
      res.val[LANE(i, res)] = expr;                                           \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
  }
SHIFT_CASE(I64x2Shl, i64x2, int2, 2, static_cast<uint64_t>(a) << (shift % 64))
SHIFT_CASE(I64x2ShrS, i64x2, int2, 2, a >> (shift % 64))
SHIFT_CASE(I64x2ShrU, i64x2, int2, 2, static_cast<uint64_t>(a) >> (shift % 64))
SHIFT_CASE(I32x4Shl, i32x4, int4, 4, static_cast<uint32_t>(a) << (shift % 32))
SHIFT_CASE(I32x4ShrS, i32x4, int4, 4, a >> (shift % 32))
SHIFT_CASE(I32x4ShrU, i32x4, int4, 4, static_cast<uint32_t>(a) >> (shift % 32))
SHIFT_CASE(I16x8Shl, i16x8, int8, 8, static_cast<uint16_t>(a) << (shift % 16))
SHIFT_CASE(I16x8ShrS, i16x8, int8, 8, a >> (shift % 16))
SHIFT_CASE(I16x8ShrU, i16x8, int8, 8, static_cast<uint16_t>(a) >> (shift % 16))
SHIFT_CASE(I8x16Shl, i8x16, int16, 16, static_cast<uint8_t>(a) << (shift % 8))
SHIFT_CASE(I8x16ShrS, i8x16, int16, 16, a >> (shift % 8))
SHIFT_CASE(I8x16ShrU, i8x16, int16, 16, static_cast<uint8_t>(a) >> (shift % 8))
#undef SHIFT_CASE

template <typename s_type, typename d_type, typename narrow, typename wide,
          uint32_t start>
INSTRUCTION_HANDLER_FUNC s2s_DoSimdExtMul(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
  s_type s2 = pop<Simd128>(sp, code, wasm_runtime).to<s_type>();
  s_type s1 = pop<Simd128>(sp, code, wasm_runtime).to<s_type>();
  auto end = start + (kSimd128Size / sizeof(wide));
  d_type res;
  uint32_t i = start;
  for (size_t dst = 0; i < end; ++i, ++dst) {
    // Need static_cast for unsigned narrow types.
    res.val[LANE(dst, res)] =
        MultiplyLong<wide>(static_cast<narrow>(s1.val[LANE(start, s1)]),
                           static_cast<narrow>(s2.val[LANE(start, s2)]));
  }
  push<Simd128>(sp, code, wasm_runtime, Simd128(res));
  NextOp();
}
static auto s2s_SimdI16x8ExtMulLowI8x16S =
    s2s_DoSimdExtMul<int16, int8, int8_t, int16_t, 0>;
static auto s2s_SimdI16x8ExtMulHighI8x16S =
    s2s_DoSimdExtMul<int16, int8, int8_t, int16_t, 8>;
static auto s2s_SimdI16x8ExtMulLowI8x16U =
    s2s_DoSimdExtMul<int16, int8, uint8_t, uint16_t, 0>;
static auto s2s_SimdI16x8ExtMulHighI8x16U =
    s2s_DoSimdExtMul<int16, int8, uint8_t, uint16_t, 8>;
static auto s2s_SimdI32x4ExtMulLowI16x8S =
    s2s_DoSimdExtMul<int8, int4, int16_t, int32_t, 0>;
static auto s2s_SimdI32x4ExtMulHighI16x8S =
    s2s_DoSimdExtMul<int8, int4, int16_t, int32_t, 4>;
static auto s2s_SimdI32x4ExtMulLowI16x8U =
    s2s_DoSimdExtMul<int8, int4, uint16_t, uint32_t, 0>;
static auto s2s_SimdI32x4ExtMulHighI16x8U =
    s2s_DoSimdExtMul<int8, int4, uint16_t, uint32_t, 4>;
static auto s2s_SimdI64x2ExtMulLowI32x4S =
    s2s_DoSimdExtMul<int4, int2, int32_t, int64_t, 0>;
static auto s2s_SimdI64x2ExtMulHighI32x4S =
    s2s_DoSimdExtMul<int4, int2, int32_t, int64_t, 2>;
static auto s2s_SimdI64x2ExtMulLowI32x4U =
    s2s_DoSimdExtMul<int4, int2, uint32_t, uint64_t, 0>;
static auto s2s_SimdI64x2ExtMulHighI32x4U =
    s2s_DoSimdExtMul<int4, int2, uint32_t, uint64_t, 2>;
#undef EXT_MUL_CASE

#define CONVERT_CASE(op, src_type, name, dst_type, count, start_index, ctype, \
                     expr)                                                    \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    src_type s = pop<Simd128>(sp, code, wasm_runtime).to_##name();            \
    dst_type res = {0};                                                       \
    for (size_t i = 0; i < count; ++i) {                                      \
      ctype a = s.val[LANE(start_index + i, s)];                              \
      res.val[LANE(i, res)] = expr;                                           \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
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

#define PACK_CASE(op, src_type, name, dst_type, count, dst_ctype)             \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    src_type s2 = pop<Simd128>(sp, code, wasm_runtime).to_##name();           \
    src_type s1 = pop<Simd128>(sp, code, wasm_runtime).to_##name();           \
    dst_type res;                                                             \
    for (size_t i = 0; i < count; ++i) {                                      \
      int64_t v = i < count / 2 ? s1.val[LANE(i, s1)]                         \
                                : s2.val[LANE(i - count / 2, s2)];            \
      res.val[LANE(i, res)] = base::saturated_cast<dst_ctype>(v);             \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
  }
PACK_CASE(I16x8SConvertI32x4, int4, i32x4, int8, 8, int16_t)
PACK_CASE(I16x8UConvertI32x4, int4, i32x4, int8, 8, uint16_t)
PACK_CASE(I8x16SConvertI16x8, int8, i16x8, int16, 16, int8_t)
PACK_CASE(I8x16UConvertI16x8, int8, i16x8, int16, 16, uint8_t)
#undef PACK_CASE

INSTRUCTION_HANDLER_FUNC s2s_DoSimdSelect(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
  int4 bool_val = pop<Simd128>(sp, code, wasm_runtime).to_i32x4();
  int4 v2 = pop<Simd128>(sp, code, wasm_runtime).to_i32x4();
  int4 v1 = pop<Simd128>(sp, code, wasm_runtime).to_i32x4();
  int4 res;
  for (size_t i = 0; i < 4; ++i) {
    res.val[LANE(i, res)] =
        v2.val[LANE(i, v2)] ^ ((v1.val[LANE(i, v1)] ^ v2.val[LANE(i, v2)]) &
                               bool_val.val[LANE(i, bool_val)]);
  }
  push<Simd128>(sp, code, wasm_runtime, Simd128(res));
  NextOp();
}
// Do these 5 instructions really have the same implementation?
static auto s2s_SimdI8x16RelaxedLaneSelect = s2s_DoSimdSelect;
static auto s2s_SimdI16x8RelaxedLaneSelect = s2s_DoSimdSelect;
static auto s2s_SimdI32x4RelaxedLaneSelect = s2s_DoSimdSelect;
static auto s2s_SimdI64x2RelaxedLaneSelect = s2s_DoSimdSelect;
static auto s2s_SimdS128Select = s2s_DoSimdSelect;

INSTRUCTION_HANDLER_FUNC s2s_SimdI32x4DotI16x8S(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  int8 v2 = pop<Simd128>(sp, code, wasm_runtime).to_i16x8();
  int8 v1 = pop<Simd128>(sp, code, wasm_runtime).to_i16x8();
  int4 res;
  for (size_t i = 0; i < 4; i++) {
    int32_t lo = (v1.val[LANE(i * 2, v1)] * v2.val[LANE(i * 2, v2)]);
    int32_t hi = (v1.val[LANE(i * 2 + 1, v1)] * v2.val[LANE(i * 2 + 1, v2)]);
    res.val[LANE(i, res)] = base::AddWithWraparound(lo, hi);
  }
  push<Simd128>(sp, code, wasm_runtime, Simd128(res));
  NextOp();
}

INSTRUCTION_HANDLER_FUNC s2s_SimdI16x8DotI8x16I7x16S(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  int16 v2 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
  int16 v1 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
  int8 res;
  for (size_t i = 0; i < 8; i++) {
    int16_t lo = (v1.val[LANE(i * 2, v1)] * v2.val[LANE(i * 2, v2)]);
    int16_t hi = (v1.val[LANE(i * 2 + 1, v1)] * v2.val[LANE(i * 2 + 1, v2)]);
    res.val[LANE(i, res)] = base::AddWithWraparound(lo, hi);
  }
  push<Simd128>(sp, code, wasm_runtime, Simd128(res));
  NextOp();
}

INSTRUCTION_HANDLER_FUNC s2s_SimdI32x4DotI8x16I7x16AddS(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  int4 v3 = pop<Simd128>(sp, code, wasm_runtime).to_i32x4();
  int16 v2 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
  int16 v1 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
  int4 res;
  for (size_t i = 0; i < 4; i++) {
    int32_t a = (v1.val[LANE(i * 4, v1)] * v2.val[LANE(i * 4, v2)]);
    int32_t b = (v1.val[LANE(i * 4 + 1, v1)] * v2.val[LANE(i * 4 + 1, v2)]);
    int32_t c = (v1.val[LANE(i * 4 + 2, v1)] * v2.val[LANE(i * 4 + 2, v2)]);
    int32_t d = (v1.val[LANE(i * 4 + 3, v1)] * v2.val[LANE(i * 4 + 3, v2)]);
    int32_t acc = v3.val[LANE(i, v3)];
    // a + b + c + d should not wrap
    res.val[LANE(i, res)] = base::AddWithWraparound(a + b + c + d, acc);
  }
  push<Simd128>(sp, code, wasm_runtime, Simd128(res));
  NextOp();
}

INSTRUCTION_HANDLER_FUNC s2s_SimdI8x16Swizzle(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  int16 v2 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
  int16 v1 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
  int16 res;
  for (size_t i = 0; i < kSimd128Size; ++i) {
    int lane = v2.val[LANE(i, v2)];
    res.val[LANE(i, res)] =
        lane < kSimd128Size && lane >= 0 ? v1.val[LANE(lane, v1)] : 0;
  }
  push<Simd128>(sp, code, wasm_runtime, Simd128(res));
  NextOp();
}
static auto s2s_SimdI8x16RelaxedSwizzle = s2s_SimdI8x16Swizzle;

INSTRUCTION_HANDLER_FUNC s2s_SimdI8x16Shuffle(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  int16 value = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
  int16 v2 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
  int16 v1 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
  int16 res;
  for (size_t i = 0; i < kSimd128Size; ++i) {
    int lane = value.val[i];
    res.val[LANE(i, res)] = lane < kSimd128Size
                                ? v1.val[LANE(lane, v1)]
                                : v2.val[LANE(lane - kSimd128Size, v2)];
  }
  push<Simd128>(sp, code, wasm_runtime, Simd128(res));
  NextOp();
}

INSTRUCTION_HANDLER_FUNC s2s_SimdV128AnyTrue(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  int4 s = pop<Simd128>(sp, code, wasm_runtime).to_i32x4();
  bool res = s.val[LANE(0, s)] | s.val[LANE(1, s)] | s.val[LANE(2, s)] |
             s.val[LANE(3, s)];
  push<int32_t>(sp, code, wasm_runtime, res);
  NextOp();
}

#define REDUCTION_CASE(op, name, stype, count)                                \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    stype s = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    bool res = true;                                                          \
    for (size_t i = 0; i < count; ++i) {                                      \
      res = res & static_cast<bool>(s.val[LANE(i, s)]);                       \
    }                                                                         \
    push<int32_t>(sp, code, wasm_runtime, res);                               \
    NextOp();                                                                 \
  }
REDUCTION_CASE(I64x2AllTrue, i64x2, int2, 2)
REDUCTION_CASE(I32x4AllTrue, i32x4, int4, 4)
REDUCTION_CASE(I16x8AllTrue, i16x8, int8, 8)
REDUCTION_CASE(I8x16AllTrue, i8x16, int16, 16)
#undef REDUCTION_CASE

#define QFM_CASE(op, name, stype, count, operation)                           \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    stype c = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    stype b = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    stype a = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    stype res;                                                                \
    for (size_t i = 0; i < count; i++) {                                      \
      res.val[LANE(i, res)] =                                                 \
          operation(a.val[LANE(i, a)] * b.val[LANE(i, b)]) +                  \
          c.val[LANE(i, c)];                                                  \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
  }
QFM_CASE(F32x4Qfma, f32x4, float4, 4, +)
QFM_CASE(F32x4Qfms, f32x4, float4, 4, -)
QFM_CASE(F64x2Qfma, f64x2, float2, 2, +)
QFM_CASE(F64x2Qfms, f64x2, float2, 2, -)
#undef QFM_CASE

template <typename s_type, typename load_type>
INSTRUCTION_HANDLER_FUNC s2s_DoSimdLoadSplat(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  uint8_t* memory_start = wasm_runtime->GetMemoryStart();
  uint64_t offset = Read<uint64_t>(code);

  uint64_t index = pop<uint32_t>(sp, code, wasm_runtime);
  uint64_t effective_index = offset + index;

  if (V8_UNLIKELY(effective_index < index ||
                  !base::IsInBounds<uint64_t>(effective_index,
                                              sizeof(load_type),
                                              wasm_runtime->GetMemorySize()))) {
    TRAP(TrapReason::kTrapMemOutOfBounds)
  }

  uint8_t* address = memory_start + effective_index;
  load_type v =
      base::ReadUnalignedValue<load_type>(reinterpret_cast<Address>(address));
  s_type s;
  for (size_t i = 0; i < arraysize(s.val); i++) {
    s.val[LANE(i, s)] = v;
  }
  push<Simd128>(sp, code, wasm_runtime, Simd128(s));

  NextOp();
}
static auto s2s_SimdS128Load8Splat = s2s_DoSimdLoadSplat<int16, int8_t>;
static auto s2s_SimdS128Load16Splat = s2s_DoSimdLoadSplat<int8, int16_t>;
static auto s2s_SimdS128Load32Splat = s2s_DoSimdLoadSplat<int4, int32_t>;
static auto s2s_SimdS128Load64Splat = s2s_DoSimdLoadSplat<int2, int64_t>;

template <typename s_type, typename wide_type, typename narrow_type>
INSTRUCTION_HANDLER_FUNC s2s_DoSimdLoadExtend(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  static_assert(sizeof(wide_type) == sizeof(narrow_type) * 2,
                "size mismatch for wide and narrow types");
  uint8_t* memory_start = wasm_runtime->GetMemoryStart();
  uint64_t offset = Read<uint64_t>(code);

  uint64_t index = pop<uint32_t>(sp, code, wasm_runtime);
  uint64_t effective_index = offset + index;

  if (V8_UNLIKELY(effective_index < index ||
                  !base::IsInBounds<uint64_t>(effective_index, sizeof(uint64_t),
                                              wasm_runtime->GetMemorySize()))) {
    TRAP(TrapReason::kTrapMemOutOfBounds)
  }

  uint8_t* address = memory_start + effective_index;
  uint64_t v =
      base::ReadUnalignedValue<uint64_t>(reinterpret_cast<Address>(address));
  constexpr int lanes = kSimd128Size / sizeof(wide_type);
  s_type s;
  for (int i = 0; i < lanes; i++) {
    uint8_t shift = i * (sizeof(narrow_type) * 8);
    narrow_type el = static_cast<narrow_type>(v >> shift);
    s.val[LANE(i, s)] = static_cast<wide_type>(el);
  }
  push<Simd128>(sp, code, wasm_runtime, Simd128(s));

  NextOp();
}
static auto s2s_SimdS128Load8x8S = s2s_DoSimdLoadExtend<int8, int16_t, int8_t>;
static auto s2s_SimdS128Load8x8U =
    s2s_DoSimdLoadExtend<int8, uint16_t, uint8_t>;
static auto s2s_SimdS128Load16x4S =
    s2s_DoSimdLoadExtend<int4, int32_t, int16_t>;
static auto s2s_SimdS128Load16x4U =
    s2s_DoSimdLoadExtend<int4, uint32_t, uint16_t>;
static auto s2s_SimdS128Load32x2S =
    s2s_DoSimdLoadExtend<int2, int64_t, int32_t>;
static auto s2s_SimdS128Load32x2U =
    s2s_DoSimdLoadExtend<int2, uint64_t, uint32_t>;

template <typename s_type, typename load_type>
INSTRUCTION_HANDLER_FUNC s2s_DoSimdLoadZeroExtend(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  uint8_t* memory_start = wasm_runtime->GetMemoryStart();
  uint64_t offset = Read<uint64_t>(code);

  uint64_t index = pop<uint32_t>(sp, code, wasm_runtime);
  uint64_t effective_index = offset + index;

  if (V8_UNLIKELY(effective_index < index ||
                  !base::IsInBounds<uint64_t>(effective_index,
                                              sizeof(load_type),
                                              wasm_runtime->GetMemorySize()))) {
    TRAP(TrapReason::kTrapMemOutOfBounds)
  }

  uint8_t* address = memory_start + effective_index;
  load_type v =
      base::ReadUnalignedValue<load_type>(reinterpret_cast<Address>(address));
  s_type s;
  // All lanes are 0.
  for (size_t i = 0; i < arraysize(s.val); i++) {
    s.val[LANE(i, s)] = 0;
  }
  // Lane 0 is set to the loaded value.
  s.val[LANE(0, s)] = v;
  push<Simd128>(sp, code, wasm_runtime, Simd128(s));

  NextOp();
}
static auto s2s_SimdS128Load32Zero = s2s_DoSimdLoadZeroExtend<int4, uint32_t>;
static auto s2s_SimdS128Load64Zero = s2s_DoSimdLoadZeroExtend<int2, uint64_t>;

template <typename s_type, typename result_type, typename load_type>
INSTRUCTION_HANDLER_FUNC s2s_DoSimdLoadLane(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  s_type value = pop<Simd128>(sp, code, wasm_runtime).to<s_type>();

  uint8_t* memory_start = wasm_runtime->GetMemoryStart();
  uint64_t offset = Read<uint64_t>(code);

  uint64_t index = pop<uint32_t>(sp, code, wasm_runtime);
  uint64_t effective_index = offset + index;

  if (V8_UNLIKELY(effective_index < index ||
                  !base::IsInBounds<uint64_t>(effective_index,
                                              sizeof(load_type),
                                              wasm_runtime->GetMemorySize()))) {
    TRAP(TrapReason::kTrapMemOutOfBounds)
  }

  uint8_t* address = memory_start + effective_index;
  result_type loaded =
      base::ReadUnalignedValue<load_type>(reinterpret_cast<Address>(address));
  uint16_t lane = Read<uint16_t>(code);
  value.val[LANE(lane, value)] = loaded;
  push<Simd128>(sp, code, wasm_runtime, Simd128(value));

  NextOp();
}
static auto s2s_SimdS128Load8Lane = s2s_DoSimdLoadLane<int16, int32_t, int8_t>;
static auto s2s_SimdS128Load16Lane = s2s_DoSimdLoadLane<int8, int32_t, int16_t>;
static auto s2s_SimdS128Load32Lane = s2s_DoSimdLoadLane<int4, int32_t, int32_t>;
static auto s2s_SimdS128Load64Lane = s2s_DoSimdLoadLane<int2, int64_t, int64_t>;

template <typename s_type, typename result_type, typename load_type>
INSTRUCTION_HANDLER_FUNC s2s_DoSimdStoreLane(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  // Extract a single lane, push it onto the stack, then store the lane.
  s_type value = pop<Simd128>(sp, code, wasm_runtime).to<s_type>();

  uint8_t* memory_start = wasm_runtime->GetMemoryStart();
  uint64_t offset = Read<uint64_t>(code);

  uint64_t index = pop<uint32_t>(sp, code, wasm_runtime);
  uint64_t effective_index = offset + index;

  if (V8_UNLIKELY(effective_index < index ||
                  !base::IsInBounds<uint64_t>(effective_index,
                                              sizeof(load_type),
                                              wasm_runtime->GetMemorySize()))) {
    TRAP(TrapReason::kTrapMemOutOfBounds)
  }
  uint8_t* address = memory_start + effective_index;

  uint16_t lane = Read<uint16_t>(code);
  result_type res = value.val[LANE(lane, value)];
  base::WriteUnalignedValue<result_type>(reinterpret_cast<Address>(address),
                                         res);

  NextOp();
}
static auto s2s_SimdS128Store8Lane =
    s2s_DoSimdStoreLane<int16, int32_t, int8_t>;
static auto s2s_SimdS128Store16Lane =
    s2s_DoSimdStoreLane<int8, int32_t, int16_t>;
static auto s2s_SimdS128Store32Lane =
    s2s_DoSimdStoreLane<int4, int32_t, int32_t>;
static auto s2s_SimdS128Store64Lane =
    s2s_DoSimdStoreLane<int2, int64_t, int64_t>;

template <typename DstSimdType, typename SrcSimdType, typename Wide,
          typename Narrow>
INSTRUCTION_HANDLER_FUNC s2s_DoSimdExtAddPairwise(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0) {
  constexpr int lanes = kSimd128Size / sizeof(DstSimdType::val[0]);
  auto v = pop<Simd128>(sp, code, wasm_runtime).to<SrcSimdType>();
  DstSimdType res;
  for (int i = 0; i < lanes; ++i) {
    res.val[LANE(i, res)] =
        AddLong<Wide>(static_cast<Narrow>(v.val[LANE(i * 2, v)]),
                      static_cast<Narrow>(v.val[LANE(i * 2 + 1, v)]));
  }
  push<Simd128>(sp, code, wasm_runtime, Simd128(res));

  NextOp();
}
static auto s2s_SimdI32x4ExtAddPairwiseI16x8S =
    s2s_DoSimdExtAddPairwise<int4, int8, int32_t, int16_t>;
static auto s2s_SimdI32x4ExtAddPairwiseI16x8U =
    s2s_DoSimdExtAddPairwise<int4, int8, uint32_t, uint16_t>;
static auto s2s_SimdI16x8ExtAddPairwiseI8x16S =
    s2s_DoSimdExtAddPairwise<int8, int16, int16_t, int8_t>;
static auto s2s_SimdI16x8ExtAddPairwiseI8x16U =
    s2s_DoSimdExtAddPairwise<int8, int16, uint16_t, uint8_t>;
