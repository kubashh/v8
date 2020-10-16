// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDED_FROM_UNWINDER_H
#error This header must be included via unwinder.cc
#endif

#ifndef V8_DIAGNOSTICS_ARM_UNWINDER_ARM_H_
#define V8_DIAGNOSTICS_ARM_UNWINDER_ARM_H_

#include "src/diagnostics/unwinder.h"

namespace v8 {

void RestoreCalleeSavedRegisters(void* fp, RegisterState* register_state) {
  // Restore r4, ..., r10
  for (int i = 0; i < 7; ++i) {
    i::Address addr =
        reinterpret_cast<i::Address>(fp) +
        i::EntryFrameConstants::kDirectCallerCalleeSavedRegistersROffset +
        i * i::kSystemPointerSize;
    register_state->callee_saved.r[i] = reinterpret_cast<void*>(Load(addr));
  }
}

}  // namespace v8

#endif  // V8_DIAGNOSTICS_ARM_UNWINDER_ARM_H_
