// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_CODE_SPACE_ACCESS_H_
#define V8_WASM_CODE_SPACE_ACCESS_H_

#include "src/base/build_config.h"
#include "src/base/macros.h"
#include "src/common/globals.h"

#if defined(V8_OS_MACOSX) && defined(V8_HOST_ARCH_ARM64)
// Mac-on-arm64 implies that we have at least MacOS 11.0, so we don't
// need to check for that.

#include <AvailabilityMacros.h>
#include <AvailabilityVersions.h>

// As long as we don't compile with the 11.0 SDK, we need to forward-declare
// the function pthread_jit_write_protect_np. It's guarded by an #if that
// makes sure that the forward declaration is automatically skipped once we
// start compiling with the 11.0 SDK.
#if !defined(MAC_OS_VERSION_11_0)
extern "C" {
void pthread_jit_write_protect_np(int write_protect_enabled);
}
#endif  // MAC_OS_VERSION_11_0

namespace v8 {
namespace internal {

// __builtin_available doesn't work for 11.0 yet; https://crbug.com/1115294
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
inline void SwitchMemoryPermissionsToWritable() {
  printf("Setting rw- permissions\n");
  pthread_jit_write_protect_np(0);
}
inline void SwitchMemoryPermissionsToExecutable() {
  printf("Setting r-x permissions\n");
  pthread_jit_write_protect_np(1);
}
#pragma clang diagnostic pop

}  // namespace internal
}  // namespace v8

#else  // Not Mac-on-arm64.

namespace v8 {
namespace internal {

// Nothing to do, we map code memory with rwx permissions.
inline void SwitchMemoryPermissionsToWritable() {}
inline void SwitchMemoryPermissionsToExecutable() {}

}  // namespace internal
}  // namespace v8

#endif  // V8_OS_MACOSX && V8_HOST_ARCH_ARM64

#endif  // V8_WASM_CODE_SPACE_ACCESS_H_
