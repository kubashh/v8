// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "src/assembler-inl.h"
#include "src/objects-inl.h"
#include "src/v8.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/c-signature.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_run_wasm_relocation {

WASM_COMPILED_EXEC_TEST(RunPatchWasmDirectCall) {
  WasmRunner<uint32_t, uint32_t> r(execution_mode);
  Isolate* isolate = CcTest::i_isolate();
  USE(isolate);

  r.builder().AddGlobal<uint32_t>();
  r.builder().AddGlobal<uint32_t>();

  BUILD(r, WASM_SET_GLOBAL(0, WASM_GET_LOCAL(0)), WASM_GET_GLOBAL(0));
  CHECK_EQ(1, r.builder().CodeTableLength());

  UNREACHABLE();  // TODO XXX run the test with patching a direct call.
}

}  // namespace test_run_wasm_relocation
}  // namespace wasm
}  // namespace internal
}  // namespace v8
