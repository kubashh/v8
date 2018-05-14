// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_WASM_ASSEMBLER_H_
#define V8_WASM_WASM_ASSEMBLER_H_

#include "src/macro-assembler.h"

namespace v8 {
namespace internal {
namespace wasm {

class WasmAssembler : public Assembler {
 public:
  explicit WasmAssembler(Isolate* isolate) : Assembler(isolate, nullptr, 0) {}

  inline void EmitJumpTrampoline(Address target) {
#if V8_TARGET_ARCH_X64
    movq(kScratchRegister, static_cast<uint64_t>(target));
    jmp(kScratchRegister);
#elif V8_TARGET_ARCH_S390X
    mov(ip, Operand(bit_cast<intptr_t, Address>(target)));
    b(ip);
#else
    UNIMPLEMENTED();
#endif
  }
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_ASSEMBLER_H_
