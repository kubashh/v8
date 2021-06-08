// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/build_config.h"
#include "src/base/debug/stack_trace.h"
#include "src/base/free_deleter.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "starboard/common/log.h"

namespace v8 {
namespace base {
namespace debug {

bool EnableInProcessStackDumping() {
  SB_NOTIMPLEMENTED();
  return false;
}

void DisableSignalStackDump() { SB_NOTIMPLEMENTED(); }

StackTrace::StackTrace() { SB_NOTIMPLEMENTED(); }

void StackTrace::Print() const { SB_NOTIMPLEMENTED(); }

void StackTrace::OutputToStream(std::ostream* os) const { SB_NOTIMPLEMENTED(); }

}  // namespace debug
}  // namespace base
}  // namespace v8
