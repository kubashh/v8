// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/code-memory-access-inl.h"

namespace v8 {
namespace internal {

#if V8_HAS_PTHREAD_JIT_WRITE_PROTECT || V8_HAS_PKU_JIT_WRITE_PROTECT
thread_local int RwxMemoryWriteScope::code_space_write_nesting_level_ = 0;
#endif  // V8_HAS_PTHREAD_JIT_WRITE_PROTECT || V8_HAS_PKU_JIT_WRITE_PROTECT

#if V8_HAS_PKU_JIT_WRITE_PROTECT
int RwxMemoryWriteScope::memory_protection_key_ =
    base::MemoryProtectionKey::kNoMemoryProtectionKey;

bool RwxMemoryWriteScope::is_PKU_supported_ = false;

void RwxMemoryWriteScope::InitializeMemoryProtectionKey() {
  memory_protection_key_ = base::MemoryProtectionKey::AllocateKey();
  is_PKU_supported_ = memory_protection_key_ !=
                      base::MemoryProtectionKey::kNoMemoryProtectionKey;
}

bool RwxMemoryWriteScope::IsPKUWritable() {
  return base::MemoryProtectionKey::GetKeyPermission(memory_protection_key_) ==
         base::MemoryProtectionKey::kNoRestrictions;
}
#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT

}  // namespace internal
}  // namespace v8
