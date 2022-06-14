// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_CODE_MEMORY_ACCESS_INL_H_
#define V8_COMMON_CODE_MEMORY_ACCESS_INL_H_

#include "src/common/code-memory-access.h"
#include "src/flags/flags.h"

namespace v8 {
namespace internal {

RwxMemoryWriteScope::RwxMemoryWriteScope(const char* comment) {
  if (!FLAG_jitless) {
    SetWritable();
  }
}

RwxMemoryWriteScope::~RwxMemoryWriteScope() {
  if (!FLAG_jitless) {
    SetExecutable();
  }
}

#if V8_HAS_PTHREAD_JIT_WRITE_PROTECT

// Ignoring this warning is considered better than relying on
// __builtin_available.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"

// static
bool RwxMemoryWriteScope::IsAllowed() {
  return pthread_jit_write_protect_supported_np();
}

// static
void RwxMemoryWriteScope::SetWritable() {
  if (code_space_write_nesting_level_ == 0) {
    pthread_jit_write_protect_np(0);
  }
  code_space_write_nesting_level_++;
}

// static
void RwxMemoryWriteScope::SetExecutable() {
  code_space_write_nesting_level_--;
  if (code_space_write_nesting_level_ == 0) {
    thread_jit_write_protect_np(1);
  }
}
#pragma clang diagnostic pop

#elif V8_TRY_USE_PKU_JIT_WRITE_PROTECT

// static
bool RwxMemoryWriteScope::IsAllowed() {
  return v8::base::OS::GetPermissionsProtectionKey() != -1;
}

// static
void RwxMemoryWriteScope::SetWritable() {
  if (v8::base::OS::GetPermissionsProtectionKey() == -1) return;
  if (code_space_write_nesting_level_ == 0) {
    v8::base::OS::SetPermissionsForMemoryProtectionKey(true);
    // PrintF("pku %d set writeable!\n", pkey);
  }
  code_space_write_nesting_level_++;
}

// static
void RwxMemoryWriteScope::SetExecutable() {
  if (v8::base::OS::GetPermissionsProtectionKey() == -1) return;
  code_space_write_nesting_level_--;
  if (code_space_write_nesting_level_ == 0) {
    // int pkey = v8::internal::pku::getPKU();
    v8::base::OS::SetPermissionsForMemoryProtectionKey(false);
    // PrintF("pku %d set executable!\n", pkey);
  }
}

#else  // !V8_HAS_PTHREAD_JIT_WRITE_PROTECT && !V8_TRY_USE_PKU_JIT_WRITE_PROTECT

// static
bool RwxMemoryWriteScope::IsAllowed() { return true; }

// static
void RwxMemoryWriteScope::SetWritable() {}

// static
void RwxMemoryWriteScope::SetExecutable() {}

#endif  // V8_HAS_PTHREAD_JIT_WRITE_PROTECT

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_CODE_MEMORY_ACCESS_INL_H_
