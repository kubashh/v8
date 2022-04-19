// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_ASSEMBLER_BUFFER_CACHE_H_
#define V8_WASM_ASSEMBLER_BUFFER_CACHE_H_

#include "src/wasm/wasm-code-manager.h"

namespace v8::internal {
class AssemblerBuffer;
}

namespace v8::internal::wasm {

class CachedAssemblerBuffer;

// Creating assembler buffers can be expensive, in particular if PKU is used,
// which requires an {mmap} and {pkey_protect} system call for each new buffer.
// Hence pool-allocate a larger memory region and reuse it if assembler buffers
// are freed.
// For now, this class only implements the interface without actually caching
// anything.
// TODO(12809): Actually cache the assembler buffers.
class AssemblerBufferCache final {
 public:
  std::unique_ptr<AssemblerBuffer> GetAssemblerBuffer(int size);

 private:
  friend class CachedAssemblerBuffer;

  // Called when CachedAssemblerBuffers get destroyed, to return memory to the
  // cache.
  void Return(base::AddressRegion);
};

}  // namespace v8::internal::wasm

#endif  // V8_WASM_ASSEMBLER_BUFFER_CACHE_H_
