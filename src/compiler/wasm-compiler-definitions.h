// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_WASM_COMPILER_DEFINITIONS_H_
#define V8_COMPILER_WASM_COMPILER_DEFINITIONS_H_

#include <cstdint>

namespace v8 {
namespace internal {
namespace compiler {

struct WasmTypeCheckConfig {
  bool object_can_be_null;
  uint8_t rtt_depth;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_WASM_COMPILER_DEFINITIONS_H_
