// Copyright 2010 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/diagnostics/etw-debug-win.h"

#include <windows.h>

namespace v8 {
namespace internal {
/*static*/
EtwDebug EtwDebug::info;

std::ostream& operator<<(std::ostream& os, const EtwDebug&) {
  os << "PID:" << ::GetCurrentProcessId() << "; TID:" << ::GetCurrentThreadId()
     << " ";
  return os;
}
}  // namespace internal
}  // namespace v8
