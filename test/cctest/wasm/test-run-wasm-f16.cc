// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/overflowing-math.h"
#include "src/codegen/assembler-inl.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/cctest/wasm/wasm-simd-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "third_party/fp16/src/include/fp16.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_run_wasm_f16 {

TEST(F16Load) {
  i::v8_flags.experimental_wasm_fp16 = true;
  WasmRunner<float> r(TestExecutionTier::kLiftoff);
  uint16_t* memory = r.builder().AddMemoryElems<uint16_t>(4);
  r.Build({WASM_F16_LOAD_MEM(WASM_I32V_1(4))});
  r.builder().WriteMemory(&memory[2], fp16_ieee_from_fp32_value(2.75));
  CHECK_EQ(2.75f, r.Call());
}

TEST(F16Store) {
  i::v8_flags.experimental_wasm_fp16 = true;
  WasmRunner<int32_t> r(TestExecutionTier::kLiftoff);
  uint16_t* memory = r.builder().AddMemoryElems<uint16_t>(4);
  r.Build({WASM_F16_STORE_MEM(WASM_I32V_1(4), WASM_F32(2.75)), WASM_ZERO});
  r.Call();
  CHECK_EQ(r.builder().ReadMemory(&memory[2]), fp16_ieee_from_fp32_value(2.75));
}

TEST(F16x8Splat) {
  i::v8_flags.experimental_wasm_fp16 = true;
  WasmRunner<int32_t, float> r(TestExecutionTier::kLiftoff);
  // Set up a global to hold output vector.
  uint16_t* g = r.builder().AddGlobal<uint16_t>(kWasmS128);
  uint8_t param1 = 0;
  r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_F16x8_SPLAT(WASM_LOCAL_GET(param1))),
           WASM_ONE});

  FOR_FLOAT32_INPUTS(x) {
    r.Call(x);
    uint16_t expected = fp16_ieee_from_fp32_value(x);
    for (int i = 0; i < 8; i++) {
      uint16_t actual = LANE(g, i);
      if (std::isnan(x)) {
        CHECK(isnan(actual));
      } else {
        CHECK_EQ(actual, expected);
      }
    }
  }
}

TEST(F16x8ReplaceLane) {
  i::v8_flags.experimental_wasm_fp16 = true;
  WasmRunner<int32_t> r(TestExecutionTier::kLiftoff);
  // Set up a global to hold output vector.
  uint16_t* g = r.builder().AddGlobal<uint16_t>(kWasmS128);
  // Build function to replace each lane with its (FP) index.
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F16x8_SPLAT(WASM_F32(3.14159f))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_F16x8_REPLACE_LANE(
                                     0, WASM_LOCAL_GET(temp1), WASM_F32(0.0f))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_F16x8_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(temp1), WASM_F32(1.0f))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_F16x8_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(temp1), WASM_F32(2.0f))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_F16x8_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(temp1), WASM_F32(3.0f))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_F16x8_REPLACE_LANE(
                                     4, WASM_LOCAL_GET(temp1), WASM_F32(4.0f))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_F16x8_REPLACE_LANE(
                                     5, WASM_LOCAL_GET(temp1), WASM_F32(5.0f))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_F16x8_REPLACE_LANE(
                                     6, WASM_LOCAL_GET(temp1), WASM_F32(6.0f))),
           WASM_GLOBAL_SET(0, WASM_SIMD_F16x8_REPLACE_LANE(
                                  7, WASM_LOCAL_GET(temp1), WASM_F32(7.0f))),
           WASM_ONE});

  r.Call();
  for (int i = 0; i < 8; i++) {
    CHECK_EQ(fp16_ieee_from_fp32_value(i), LANE(g, i));
  }
}

TEST(F16x8ExtractLane) {
  i::v8_flags.experimental_wasm_fp16 = true;
  WasmRunner<float> r(TestExecutionTier::kLiftoff);
  uint16_t* g = r.builder().AddGlobal<uint16_t>(kWasmS128);
  for (int i = 0; i < 8; i++) {
    LANE(g, i) = fp16_ieee_from_fp32_value(i);
  }
  uint8_t acc = r.AllocateLocal(kWasmF32);
#define ADD_LINE(idx)                                                      \
  WASM_LOCAL_SET(                                                          \
      acc, WASM_F32_ADD(WASM_LOCAL_GET(acc), WASM_SIMD_F16x8_EXTRACT_LANE( \
                                                 idx, WASM_GLOBAL_GET(0))))
  r.Build({WASM_LOCAL_SET(acc, WASM_F32(0.0f)), ADD_LINE(0), ADD_LINE(1),
           ADD_LINE(2), ADD_LINE(3), ADD_LINE(4), ADD_LINE(5), ADD_LINE(6),
           ADD_LINE(7), WASM_LOCAL_GET(acc)});
#undef ADD_LINE
  CHECK_EQ(0 + 1 + 2 + 3 + 4 + 5 + 6 + 7, r.Call());
}

}  // namespace test_run_wasm_f16
}  // namespace wasm
}  // namespace internal
}  // namespace v8
