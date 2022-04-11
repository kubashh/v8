// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_CODE_MEMORY_ACCESS_H_
#define V8_COMMON_CODE_MEMORY_ACCESS_H_

#include "include/v8config.h"
#include "src/base/build_config.h"

namespace v8 {
namespace internal {

// Within the scope, the code space is writable (and for Apple M1 also not
// executable). After the last (nested) scope is destructed, the code space is
// not writable.
// This uses three different implementations, depending on the platform, flags,
// and runtime support:
// - On MacOS on ARM64 ("Apple M1"/Apple Silicon), it uses APRR/MAP_JIT to
// switch only the calling thread between writable and executable. This achieves
// "real" W^X and is thread-local and fast.
// - When Intel PKU (aka. memory protection keys) are available, it switches
// the protection keys' permission between writable and not writable. The
// executable permission cannot be retracted with PKU. That is, this "only"
// achieves write-protection, but is similarly thread-local and fast.
// -  similar argument for debug time.
// MAP_JIT on Apple M1 cannot switch permissions for smaller ranges of memory,
// and for PKU we would need multiple keys, so both of them also switch
// permissions for all code pages.
class V8_NODISCARD CodeMemoryWriteScope {
 public:
  V8_INLINE explicit CodeMemoryWriteScope();
  V8_INLINE ~CodeMemoryWriteScope();

  // Disable copy constructor and copy-assignment operator, since this manages
  // a resource and implicit copying of the scope can yield surprising errors.
  CodeMemoryWriteScope(const CodeMemoryWriteScope&) = delete;
  CodeMemoryWriteScope& operator=(const CodeMemoryWriteScope&) = delete;

  V8_INLINE static void Enter();
  V8_INLINE static void Exit();

 private:
#if V8_HAS_PTHREAD_JIT_WRITE_PROTECT
  static void SetWritable();
  static void SetExecutable();

  static thread_local int code_space_write_nesting_level_;

#else

#ifdef DEBUG
  static thread_local int code_space_write_nesting_level_;
#endif  // DEBUG

#endif  // V8_HAS_PTHREAD_JIT_WRITE_PROTECT
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_CODE_MEMORY_ACCESS_H_
