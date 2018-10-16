// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRAP_HANDLER_HANDLER_INSIDE_WIN_H_
#define V8_TRAP_HANDLER_HANDLER_INSIDE_WIN_H_

#include <Windows.h>
#undef CONST  // windows.h defines CONST which conflicts with the one in
              // src/flags.h

namespace v8 {
namespace internal {
namespace trap_handler {

LONG HandleWasmTrap(LPEXCEPTION_POINTERS exception);

bool TryHandleWasmTrap(LPEXCEPTION_POINTERS exception);

}  // namespace trap_handler
}  // namespace internal
}  // namespace v8

#endif  // V8_TRAP_HANDLER_HANDLER_INSIDE_WIN_H_
