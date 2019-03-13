// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRAP_HANDLER_HANDLER_INSIDE_WIN_H_
#define V8_TRAP_HANDLER_HANDLER_INSIDE_WIN_H_

#include <windows.h>

#include "src/base/macros.h"

namespace v8 {
namespace internal {
namespace trap_handler {

LONG WINAPI HandleWasmTrap(EXCEPTION_POINTERS* exception);

// On Windows, asan installs its own exception handler which maps shadow
// memory. Since our exception handler may be executed before the asan exception
// handler, we have to make sure that asan shadow memory is not accessed here.
DISABLE_ASAN bool TryHandleWasmTrap(EXCEPTION_POINTERS* exception);

// The below struct needed to access the offset in the Thread Environment Block
// to see if the thread local storage for the thread has been allocated yet.
//
// The ThreadLocalStorage pointer is located 12 pointers into the TEB (i.e. at
// offset 0x58 for 64-bit platforms, and 0x2c for 32-bit platforms). This is
// true for x64, x86, ARM, and ARM64 platforms (see the header files in the SDK
// named ksamd64.inc, ks386.inc, ksarm.h, and ksarm64.h respectively).
//
// These offsets are baked into compiled binaries, so can never be changed for
// backwards compatibility reasons.
struct TEB {
  PVOID reserved[11];
  PVOID ThreadLocalStoragePointer;
};

}  // namespace trap_handler
}  // namespace internal
}  // namespace v8

#endif  // V8_TRAP_HANDLER_HANDLER_INSIDE_WIN_H_
