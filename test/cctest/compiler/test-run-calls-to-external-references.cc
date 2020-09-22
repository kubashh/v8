// Copyright 2014 the V8 project authors. All rights reserved. Use of this
// source code is governed by a BSD-style license that can be found in the
// LICENSE file.

#include "src/codegen/external-reference.h"
#include "src/objects/objects-inl.h"
#include "src/wasm/wasm-external-refs.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/cctest/compiler/value-helper.h"

namespace v8 {
namespace internal {
namespace compiler {

template <typename InType, typename OutType, typename Iterable>
void TestExternalReference_ConvertOp(
    BufferedRawMachineAssemblerTester<int32_t>* m, ExternalReference ref,
    void (*wrapper)(Address), Iterable inputs) {
  constexpr size_t kBufferSize = Max(sizeof(InType), sizeof(OutType));
  uint8_t buffer[kBufferSize] = {0};
  Address buffer_addr = reinterpret_cast<Address>(buffer);

  Node* function = m->ExternalConstant(ref);
  m->CallCFunction(
      function, MachineType::Pointer(),
      std::make_pair(MachineType::Pointer(), m->PointerConstant(buffer)));
  m->Return(m->Int32Constant(4356));

  for (InType input : inputs) {
    WriteUnalignedValue<InType>(buffer_addr, input);

    CHECK_EQ(4356, m->Call());
    OutType output = ReadUnalignedValue<OutType>(buffer_addr);

    WriteUnalignedValue<InType>(buffer_addr, input);
    wrapper(buffer_addr);
    OutType expected_output = ReadUnalignedValue<OutType>(buffer_addr);

    CHECK_EQ(expected_output, output);
  }
}

template <typename InType, typename OutType, typename Iterable>
void TestExternalReference_ConvertOpWithOutputAndReturn(
    BufferedRawMachineAssemblerTester<int32_t>* m, ExternalReference ref,
    int32_t (*wrapper)(Address), Iterable inputs) {
  constexpr size_t kBufferSize = Max(sizeof(InType), sizeof(OutType));
  uint8_t buffer[kBufferSize] = {0};
  Address buffer_addr = reinterpret_cast<Address>(buffer);

  Node* function = m->ExternalConstant(ref);
  m->Return(m->CallCFunction(
      function, MachineType::Int32(),
      std::make_pair(MachineType::Pointer(), m->PointerConstant(buffer))));

  for (InType input : inputs) {
    WriteUnalignedValue<InType>(buffer_addr, input);

    int32_t ret = m->Call();
    OutType output = ReadUnalignedValue<OutType>(buffer_addr);

    WriteUnalignedValue<InType>(buffer_addr, input);
    int32_t expected_ret = wrapper(buffer_addr);
    OutType expected_output = ReadUnalignedValue<OutType>(buffer_addr);

    CHECK_EQ(expected_ret, ret);
    CHECK_EQ(expected_output, output);
  }
}

template <typename InType, typename OutType, typename Iterable>
void TestExternalReference_ConvertOpWithReturn(
    BufferedRawMachineAssemblerTester<OutType>* m, ExternalReference ref,
    OutType (*wrapper)(Address), Iterable inputs) {
  constexpr size_t kBufferSize = sizeof(InType);
  uint8_t buffer[kBufferSize] = {0};
  Address buffer_addr = reinterpret_cast<Address>(buffer);

  Node* function = m->ExternalConstant(ref);
  m->Return(m->CallCFunction(
      function, MachineType::Int32(),
      std::make_pair(MachineType::Pointer(), m->PointerConstant(buffer))));

  for (InType input : inputs) {
    WriteUnalignedValue<InType>(buffer_addr, input);

    OutType ret = m->Call();

    WriteUnalignedValue<InType>(buffer_addr, input);
    OutType expected_ret = wrapper(buffer_addr);

    CHECK_EQ(expected_ret, ret);
  }
}

template <typename Type>
bool isnan(Type value) {
  return false;
}
template <>
bool isnan<float>(float value) {
  return std::isnan(value);
}
template <>
bool isnan<double>(double value) {
  return std::isnan(value);
}

template <typename Type, typename Iterable>
void TestExternalReference_UnOp(BufferedRawMachineAssemblerTester<int32_t>* m,
                                ExternalReference ref, void (*wrapper)(Address),
                                Iterable inputs) {
  constexpr size_t kBufferSize = sizeof(Type);
  uint8_t buffer[kBufferSize] = {0};
  Address buffer_addr = reinterpret_cast<Address>(buffer);

  Node* function = m->ExternalConstant(ref);
  m->CallCFunction(
      function, MachineType::Int32(),
      std::make_pair(MachineType::Pointer(), m->PointerConstant(buffer)));
  m->Return(m->Int32Constant(4356));

  for (Type input : inputs) {
    WriteUnalignedValue<Type>(buffer_addr, input);
    CHECK_EQ(4356, m->Call());
    Type output = ReadUnalignedValue<Type>(buffer_addr);

    WriteUnalignedValue<Type>(buffer_addr, input);
    wrapper(buffer_addr);
    Type expected_output = ReadUnalignedValue<Type>(buffer_addr);

    if (isnan(expected_output) && isnan(output)) continue;
    CHECK_EQ(expected_output, output);
  }
}

template <typename Type, typename Iterable>
void TestExternalReference_BinOp(BufferedRawMachineAssemblerTester<int32_t>* m,
                                 ExternalReference ref,
                                 void (*wrapper)(Address), Iterable inputs) {
  constexpr size_t kBufferSize = 2 * sizeof(Type);
  uint8_t buffer[kBufferSize] = {0};
  Address buffer_addr = reinterpret_cast<Address>(buffer);

  Node* function = m->ExternalConstant(ref);
  m->CallCFunction(
      function, MachineType::Int32(),
      std::make_pair(MachineType::Pointer(), m->PointerConstant(buffer)));
  m->Return(m->Int32Constant(4356));

  for (Type input1 : inputs) {
    for (Type input2 : inputs) {
      WriteUnalignedValue<Type>(buffer_addr, input1);
      WriteUnalignedValue<Type>(buffer_addr + sizeof(Type), input2);
      CHECK_EQ(4356, m->Call());
      Type output = ReadUnalignedValue<Type>(buffer_addr);

      WriteUnalignedValue<Type>(buffer_addr, input1);
      WriteUnalignedValue<Type>(buffer_addr + sizeof(Type), input2);
      wrapper(buffer_addr);
      Type expected_output = ReadUnalignedValue<Type>(buffer_addr);

      if (isnan(expected_output) && isnan(output)) continue;
      CHECK_EQ(expected_output, output);
    }
  }
}

template <typename Type, typename Iterable>
void TestExternalReference_BinOpWithReturn(
    BufferedRawMachineAssemblerTester<int32_t>* m, ExternalReference ref,
    int32_t (*wrapper)(Address), Iterable inputs) {
  constexpr size_t kBufferSize = 2 * sizeof(Type);
  uint8_t buffer[kBufferSize] = {0};
  Address buffer_addr = reinterpret_cast<Address>(buffer);

  Node* function = m->ExternalConstant(ref);
  m->Return(m->CallCFunction(
      function, MachineType::Int32(),
      std::make_pair(MachineType::Pointer(), m->PointerConstant(buffer))));

  for (Type input1 : inputs) {
    for (Type input2 : inputs) {
      WriteUnalignedValue<Type>(buffer_addr, input1);
      WriteUnalignedValue<Type>(buffer_addr + sizeof(Type), input2);
      int32_t ret = m->Call();
      Type output = ReadUnalignedValue<Type>(buffer_addr);

      WriteUnalignedValue<Type>(buffer_addr, input1);
      WriteUnalignedValue<Type>(buffer_addr + sizeof(Type), input2);
      int32_t expected_ret = wrapper(buffer_addr);
      Type expected_output = ReadUnalignedValue<Type>(buffer_addr);

      CHECK_EQ(expected_ret, ret);
      if (isnan(expected_output) && isnan(output)) continue;
      CHECK_EQ(expected_output, output);
    }
  }
}

TEST(RunCallF32Trunc) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_f32_trunc();
  TestExternalReference_UnOp<float>(&m, ref, wasm::f32_trunc_wrapper,
                                    ValueHelper::float32_vector());
}

TEST(RunCallF32Floor) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_f32_floor();
  TestExternalReference_UnOp<float>(&m, ref, wasm::f32_floor_wrapper,
                                    ValueHelper::float32_vector());
}

TEST(RunCallF32Ceil) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_f32_ceil();
  TestExternalReference_UnOp<float>(&m, ref, wasm::f32_ceil_wrapper,
                                    ValueHelper::float32_vector());
}

TEST(RunCallF32RoundTiesEven) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_f32_nearest_int();
  TestExternalReference_UnOp<float>(&m, ref, wasm::f32_nearest_int_wrapper,
                                    ValueHelper::float32_vector());
}

TEST(RunCallF64Trunc) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_f64_trunc();
  TestExternalReference_UnOp<double>(&m, ref, wasm::f64_trunc_wrapper,
                                     ValueHelper::float64_vector());
}

TEST(RunCallF64Floor) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_f64_floor();
  TestExternalReference_UnOp<double>(&m, ref, wasm::f64_floor_wrapper,
                                     ValueHelper::float64_vector());
}

TEST(RunCallF64Ceil) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_f64_ceil();
  TestExternalReference_UnOp<double>(&m, ref, wasm::f64_ceil_wrapper,
                                     ValueHelper::float64_vector());
}

TEST(RunCallF64RoundTiesEven) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_f64_nearest_int();
  TestExternalReference_UnOp<double>(&m, ref, wasm::f64_nearest_int_wrapper,
                                     ValueHelper::float64_vector());
}

TEST(RunCallInt64ToFloat32) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_int64_to_float32();
  TestExternalReference_ConvertOp<int64_t, float>(
      &m, ref, wasm::int64_to_float32_wrapper, ValueHelper::int64_vector());
}

TEST(RunCallUint64ToFloat32) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_uint64_to_float32();
  TestExternalReference_ConvertOp<uint64_t, float>(
      &m, ref, wasm::uint64_to_float32_wrapper, ValueHelper::uint64_vector());
}

TEST(RunCallInt64ToFloat64) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_int64_to_float64();
  TestExternalReference_ConvertOp<int64_t, double>(
      &m, ref, wasm::int64_to_float64_wrapper, ValueHelper::int64_vector());
}

TEST(RunCallUint64ToFloat64) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_uint64_to_float64();
  TestExternalReference_ConvertOp<uint64_t, double>(
      &m, ref, wasm::uint64_to_float64_wrapper, ValueHelper::uint64_vector());
}

TEST(RunCallFloat32ToInt64) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_float32_to_int64();
  TestExternalReference_ConvertOpWithOutputAndReturn<float, int64_t>(
      &m, ref, wasm::float32_to_int64_wrapper, ValueHelper::float32_vector());
}

TEST(RunCallFloat32ToUint64) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_float32_to_uint64();
  TestExternalReference_ConvertOpWithOutputAndReturn<float, uint64_t>(
      &m, ref, wasm::float32_to_uint64_wrapper, ValueHelper::float32_vector());
}

TEST(RunCallFloat64ToInt64) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_float64_to_int64();
  TestExternalReference_ConvertOpWithOutputAndReturn<double, int64_t>(
      &m, ref, wasm::float64_to_int64_wrapper, ValueHelper::float64_vector());
}

TEST(RunCallFloat64ToUint64) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_float64_to_uint64();
  TestExternalReference_ConvertOpWithOutputAndReturn<double, uint64_t>(
      &m, ref, wasm::float64_to_uint64_wrapper, ValueHelper::float64_vector());
}

TEST(RunCallInt64Div) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_int64_div();
  TestExternalReference_BinOpWithReturn<int64_t>(
      &m, ref, wasm::int64_div_wrapper, ValueHelper::int64_vector());
}

TEST(RunCallInt64Mod) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_int64_mod();
  TestExternalReference_BinOpWithReturn<int64_t>(
      &m, ref, wasm::int64_mod_wrapper, ValueHelper::int64_vector());
}

TEST(RunCallUint64Div) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_uint64_div();
  TestExternalReference_BinOpWithReturn<uint64_t>(
      &m, ref, wasm::uint64_div_wrapper, ValueHelper::uint64_vector());
}

TEST(RunCallUint64Mod) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_uint64_mod();
  TestExternalReference_BinOpWithReturn<uint64_t>(
      &m, ref, wasm::uint64_mod_wrapper, ValueHelper::uint64_vector());
}

TEST(RunCallWord32Ctz) {
  BufferedRawMachineAssemblerTester<uint32_t> m;
  ExternalReference ref = ExternalReference::wasm_word32_ctz();
  TestExternalReference_ConvertOpWithReturn<int32_t, uint32_t>(
      &m, ref, wasm::word32_ctz_wrapper, ValueHelper::int32_vector());
}

TEST(RunCallWord64Ctz) {
  BufferedRawMachineAssemblerTester<uint32_t> m;
  ExternalReference ref = ExternalReference::wasm_word64_ctz();
  TestExternalReference_ConvertOpWithReturn<int64_t, uint32_t>(
      &m, ref, wasm::word64_ctz_wrapper, ValueHelper::int64_vector());
}

TEST(RunCallWord32Popcnt) {
  BufferedRawMachineAssemblerTester<uint32_t> m;
  ExternalReference ref = ExternalReference::wasm_word32_popcnt();
  TestExternalReference_ConvertOpWithReturn<uint32_t, uint32_t>(
      &m, ref, wasm::word32_popcnt_wrapper, ValueHelper::int32_vector());
}

TEST(RunCallWord64Popcnt) {
  BufferedRawMachineAssemblerTester<uint32_t> m;
  ExternalReference ref = ExternalReference::wasm_word64_popcnt();
  TestExternalReference_ConvertOpWithReturn<int64_t, uint32_t>(
      &m, ref, wasm::word64_popcnt_wrapper, ValueHelper::int64_vector());
}

TEST(RunCallFloat64Pow) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  ExternalReference ref = ExternalReference::wasm_float64_pow();
  TestExternalReference_BinOp<double>(&m, ref, wasm::float64_pow_wrapper,
                                      ValueHelper::float64_vector());
}

#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
template <typename T>
MachineType MachineTypeForCType() {
  return MachineType::AnyTagged();
}

template <>
MachineType MachineTypeForCType<int64_t>() {
  return MachineType::Int64();
}

template <>
MachineType MachineTypeForCType<int32_t>() {
  return MachineType::Int32();
}

template <>
MachineType MachineTypeForCType<double>() {
  return MachineType::Float64();
}

#define SIGNATURE_TYPES(TYPE, IDX, VALUE) MachineTypeForCType<TYPE>()

#define PARAM_PAIRS(TYPE, IDX, VALUE) \
  std::make_pair(MachineTypeForCType<TYPE>(), m.Parameter(IDX))

#define CALL_ARGS(TYPE, IDX, VALUE) static_cast<TYPE>(VALUE)

#define CHECK_ARG_I(TYPE, IDX, VALUE) (result = result && (arg##IDX == VALUE))

#define SIGNATURE_TEST(NAME, SIGNATURE, FUNC)                            \
  TEST(NAME) {                                                           \
    RawMachineAssemblerTester<int64_t> m(SIGNATURE(SIGNATURE_TYPES));    \
                                                                         \
    Address func_address = FUNCTION_ADDR(&FUNC);                         \
    ExternalReference::Type func_type = ExternalReference::BUILTIN_CALL; \
    ApiFunction func(func_address);                                      \
    ExternalReference ref = ExternalReference::Create(&func, func_type); \
                                                                         \
    Node* function = m.ExternalConstant(ref);                            \
    m.Return(m.CallCFunction(function, MachineType::Int64(),             \
                             SIGNATURE(PARAM_PAIRS)));                   \
                                                                         \
    int64_t c = m.Call(SIGNATURE(CALL_ARGS));                            \
    CHECK_EQ(c, 42);                                                     \
  }

#define MIXED_SIGNATURE_SIMPLE(V) V(int, 0, 0), V(double, 1, 1.5), V(int, 2, 2)

int64_t test_api_func_simple(int arg0, double arg1, int arg2) {
  bool result = true;
  MIXED_SIGNATURE_SIMPLE(CHECK_ARG_I);
  CHECK(result);

  return 42;
}

SIGNATURE_TEST(RunCallWithMixedSignatureSimple, MIXED_SIGNATURE_SIMPLE,
               test_api_func_simple)

#define MIXED_SIGNATURE(V)                                              \
  V(int, 0, 0), V(double, 1, 1.5), V(int, 2, 2), V(double, 3, 3.5),     \
      V(int, 4, 4), V(double, 5, 5.5), V(int, 6, 6), V(double, 7, 7.5), \
      V(int, 8, 8), V(double, 9, 9.5), V(int, 10, 10)

int64_t test_api_func(int arg0, double arg1, int arg2, double arg3, int arg4,
                      double arg5, int arg6, double arg7, int arg8, double arg9,
                      int arg10) {
  bool result = true;
  MIXED_SIGNATURE(CHECK_ARG_I);
  CHECK(result);

  return 42;
}

SIGNATURE_TEST(RunCallWithMixedSignature, MIXED_SIGNATURE, test_api_func)

#define MIXED_SIGNATURE_DOUBLE_INT(V)                                          \
  V(double, 0, 0.5), V(double, 1, 1.5), V(double, 2, 2.5), V(double, 3, 3.5),  \
      V(double, 4, 4.5), V(double, 5, 5.5), V(double, 6, 6.5),                 \
      V(double, 7, 7.5), V(double, 8, 8.5), V(double, 9, 9.5), V(int, 10, 10), \
      V(int, 11, 11), V(int, 12, 12), V(int, 13, 13), V(int, 14, 14),          \
      V(int, 15, 15), V(int, 16, 16), V(int, 17, 17), V(int, 18, 18),          \
      V(int, 19, 19)

int64_t func_mixed_double_int(double arg0, double arg1, double arg2,
                              double arg3, double arg4, double arg5,
                              double arg6, double arg7, double arg8,
                              double arg9, int arg10, int arg11, int arg12,
                              int arg13, int arg14, int arg15, int arg16,
                              int arg17, int arg18, int arg19) {
  bool result = true;
  MIXED_SIGNATURE_DOUBLE_INT(CHECK_ARG_I);
  CHECK(result);

  return 42;
}

SIGNATURE_TEST(RunCallWithMixedSignatureDoubleInt, MIXED_SIGNATURE_DOUBLE_INT,
               func_mixed_double_int)

#define MIXED_SIGNATURE_INT_DOUBLE(V)                                       \
  V(int, 0, 0), V(int, 1, 1), V(int, 2, 2), V(int, 3, 3), V(int, 4, 4),     \
      V(int, 5, 5), V(int, 6, 6), V(int, 7, 7), V(int, 8, 8), V(int, 9, 9), \
      V(double, 10, 10.5), V(double, 11, 11.5), V(double, 12, 12.5),        \
      V(double, 13, 13.5), V(double, 14, 14.5), V(double, 15, 15.5),        \
      V(double, 16, 16.5), V(double, 17, 17.5), V(double, 18, 18.5),        \
      V(double, 19, 19.5)

int64_t func_mixed_int_double(int arg0, int arg1, int arg2, int arg3, int arg4,
                              int arg5, int arg6, int arg7, int arg8, int arg9,
                              double arg10, double arg11, double arg12,
                              double arg13, double arg14, double arg15,
                              double arg16, double arg17, double arg18,
                              double arg19) {
  bool result = true;
  MIXED_SIGNATURE_INT_DOUBLE(CHECK_ARG_I);
  CHECK(result);

  return 42;
}

SIGNATURE_TEST(RunCallWithMixedSignatureIntDouble, MIXED_SIGNATURE_INT_DOUBLE,
               func_mixed_int_double)

#define MIXED_SIGNATURE_INT_DOUBLE_ALT(V)                                   \
  V(int, 0, 0), V(double, 1, 1.5), V(int, 2, 2), V(double, 3, 3.5),         \
      V(int, 4, 4), V(double, 5, 5.5), V(int, 6, 6), V(double, 7, 7.5),     \
      V(int, 8, 8), V(double, 9, 9.5), V(int, 10, 10), V(double, 11, 11.5), \
      V(int, 12, 12), V(double, 13, 13.5), V(int, 14, 14),                  \
      V(double, 15, 15.5), V(int, 16, 16), V(double, 17, 17.5),             \
      V(int, 18, 18), V(double, 19, 19.5)

int64_t func_mixed_int_double_alt(int arg0, double arg1, int arg2, double arg3,
                                  int arg4, double arg5, int arg6, double arg7,
                                  int arg8, double arg9, int arg10,
                                  double arg11, int arg12, double arg13,
                                  int arg14, double arg15, int arg16,
                                  double arg17, int arg18, double arg19) {
  bool result = true;
  MIXED_SIGNATURE_INT_DOUBLE_ALT(CHECK_ARG_I);
  CHECK(result);

  return 42;
}

SIGNATURE_TEST(RunCallWithMixedSignatureIntDoubleAlt,
               MIXED_SIGNATURE_INT_DOUBLE_ALT, func_mixed_int_double_alt)

#define SIGNATURE_ONLY_DOUBLE(V)                                              \
  V(double, 0, 0.5), V(double, 1, 1.5), V(double, 2, 2.5), V(double, 3, 3.5), \
      V(double, 4, 4.5), V(double, 5, 5.5), V(double, 6, 6.5),                \
      V(double, 7, 7.5), V(double, 8, 8.5), V(double, 9, 9.5)

int64_t func_only_double(double arg0, double arg1, double arg2, double arg3,
                         double arg4, double arg5, double arg6, double arg7,
                         double arg8, double arg9) {
  bool result = true;
  SIGNATURE_ONLY_DOUBLE(CHECK_ARG_I);
  CHECK(result);

  return 42;
}

SIGNATURE_TEST(RunCallWithSignatureOnlyDouble, SIGNATURE_ONLY_DOUBLE,
               func_only_double)

#define SIGNATURE_ONLY_INT(V)                                           \
  V(int, 0, 0), V(int, 1, 1), V(int, 2, 2), V(int, 3, 3), V(int, 4, 4), \
      V(int, 5, 5), V(int, 6, 6), V(int, 7, 7), V(int, 8, 8), V(int, 9, 9)

int64_t func_only_int(int arg0, int arg1, int arg2, int arg3, int arg4,
                      int arg5, int arg6, int arg7, int arg8, int arg9) {
  bool result = true;
  SIGNATURE_ONLY_INT(CHECK_ARG_I);
  CHECK(result);

  return 42;
}

SIGNATURE_TEST(RunCallWithSignatureOnlyInt, SIGNATURE_ONLY_INT, func_only_int)

#define SIGNATURE_ONLY_DOUBLE_20(V)                                           \
  V(double, 0, 0.5), V(double, 1, 1.5), V(double, 2, 2.5), V(double, 3, 3.5), \
      V(double, 4, 4.5), V(double, 5, 5.5), V(double, 6, 6.5),                \
      V(double, 7, 7.5), V(double, 8, 8.5), V(double, 9, 9.5),                \
      V(double, 10, 10.5), V(double, 11, 11.5), V(double, 12, 12.5),          \
      V(double, 13, 13.5), V(double, 14, 14.5), V(double, 15, 15.5),          \
      V(double, 16, 16.5), V(double, 17, 17.5), V(double, 18, 18.5),          \
      V(double, 19, 19.5)

int64_t func_only_double_20(double arg0, double arg1, double arg2, double arg3,
                            double arg4, double arg5, double arg6, double arg7,
                            double arg8, double arg9, double arg10,
                            double arg11, double arg12, double arg13,
                            double arg14, double arg15, double arg16,
                            double arg17, double arg18, double arg19) {
  bool result = true;
  SIGNATURE_ONLY_DOUBLE_20(CHECK_ARG_I);
  CHECK(result);

  return 42;
}

SIGNATURE_TEST(RunCallWithSignatureOnlyDouble20, SIGNATURE_ONLY_DOUBLE_20,
               func_only_double_20)

#define SIGNATURE_ONLY_INT_20(V)                                            \
  V(int, 0, 0), V(int, 1, 1), V(int, 2, 2), V(int, 3, 3), V(int, 4, 4),     \
      V(int, 5, 5), V(int, 6, 6), V(int, 7, 7), V(int, 8, 8), V(int, 9, 9), \
      V(int, 10, 10), V(int, 11, 11), V(int, 12, 12), V(int, 13, 13),       \
      V(int, 14, 14), V(int, 15, 15), V(int, 16, 16), V(int, 17, 17),       \
      V(int, 18, 18), V(int, 19, 19)

int64_t func_only_int_20(int arg0, int arg1, int arg2, int arg3, int arg4,
                         int arg5, int arg6, int arg7, int arg8, int arg9,
                         int arg10, int arg11, int arg12, int arg13, int arg14,
                         int arg15, int arg16, int arg17, int arg18,
                         int arg19) {
  bool result = true;
  SIGNATURE_ONLY_INT_20(CHECK_ARG_I);
  CHECK(result);

  return 42;
}

SIGNATURE_TEST(RunCallWithSignatureOnlyInt20, SIGNATURE_ONLY_INT_20,
               func_only_int_20)
#endif  // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE

}  // namespace compiler
}  // namespace internal
}  // namespace v8
