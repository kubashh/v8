// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDED_FROM_UNWINDER_H
#error This header must be included via unwinder.cc
#endif

// Dummy file to be able to compile
#ifndef V8_DIAGNOSTICS_X64_UNWINDER_X64_H_
#define V8_DIAGNOSTICS_X64_UNWINDER_X64_H_

#include "src/diagnostics/unwinder.h"

namespace v8 {

void RestoreCalleeSavedRegisters(void* fp, RegisterState* register_state) {}

}  // namespace v8

#endif  // V8_DIAGNOSTICS_X64_UNWINDER_X64_H_
