// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PLEASE READ BEFORE CHANGING THIS FILE!
//
// This file implements the out of bounds trap handler for
// WebAssembly. Exception handlers are notoriously difficult to get
// right, and getting it wrong can lead to security
// vulnerabilities. In order to minimize this risk, here are some
// rules to follow.
//
// 1. Do not introduce any new external dependencies. This file needs
//    to be self contained so it is easy to audit everything that a
//    trap handler might do.
//
// 2. Any changes must be reviewed by someone from the crash reporting
//    or security team. See OWNERS for suggested reviewers.
//
// For more information, see https://goo.gl/yMeyUY.
//
// This file contains most of the code that actually runs in an exception
// handler context. Some additional code is used both inside and outside the
// trap handler. This code can be found in handler-shared.cc.

#include "src/trap-handler/handler-inside-win.h"

#include <windows.h>

#include "src/isolate.h"
#include "src/trap-handler/trap-handler-internal.h"
#include "src/trap-handler/trap-handler.h"

namespace v8 {
namespace internal {
namespace trap_handler {

bool TryHandleWasmTrap(EXCEPTION_POINTERS* exception) {
  // Ensure the faulting thread was actually running Wasm code.
  if (!IsThreadInWasm()) {
    return false;
  }

  // Clear g_thread_in_wasm_code, primarily to protect against nested faults.
  g_thread_in_wasm_code = false;


  const EXCEPTION_RECORD* record = exception->ExceptionRecord;

  if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
    return false;
  }

  uintptr_t fault_addr = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
  uintptr_t landing_pad = 0;

  if (TryFindLandingPad(fault_addr, &landing_pad)) {
    exception->ContextRecord->Rip = landing_pad;
    // We will return to wasm code, so restore the g_thread_in_wasm_code flag.
    g_thread_in_wasm_code = true;
    return true;
  }

  // If we get here, it's not a recoverable wasm fault, so we go to the next
  // handler. Leave the g_thread_in_wasm_code flag unset since we do not return
  // to wasm code.
  return false;
}

LONG HandleWasmTrap(EXCEPTION_POINTERS* exception) {
  // VectoredExceptionHandlers need extreme caution. Do as little as
  // possible to determine if the exception should be handled or not.
  // Exceptions can be thrown very early in a threads life, before the
  // thread has even completed initializing. As a demonstrative example,
  // there was a bug where an exception would be raised before the thread
  // local copy of the "__declspec(thread)" variables had been allocated,
  // the handler tried to access the thread-local "g_thread_in_wasm_code",
  // which would then raise another exception, and an infinite loop ensued.

  // First ensure this is an exception type of interest
  if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
    // Then check that an isolate has been entered on this thread. If it hasn't
    // then the exception can't be of interest.
    //
    // Note: The implementation of this uses the Windows Tls* APIs. These
    // are safe to access as soon as the thread is created (i.e. even before
    // thread-local variables are available), and just return NULL if not
    // initialized yet. (As the Tls slots are just a block of zero-initialized
    // memory inside the thread environment block).
    if (Isolate::TryGetCurrent() != nullptr) {
      // If the thread has entered an isolate, then it should be safe to do
      // some of the more non-trivial checks.
      if (TryHandleWasmTrap(exception)) {
        return EXCEPTION_CONTINUE_EXECUTION;
      }
    }
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace trap_handler
}  // namespace internal
}  // namespace v8
