// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_X64

#include "src/deoptimizer.h"
#include "src/macro-assembler.h"
#include "src/objects-inl.h"
#include "src/register-configuration.h"
#include "src/safepoint-table.h"

namespace v8 {
namespace internal {

#define __ masm->

bool Deoptimizer::PadTopOfStackRegister() { return false; }

void FrameDescription::SetCallerPc(unsigned offset, intptr_t value) {
  if (kPCOnStackSize == 2 * kSystemPointerSize) {
    // Zero out the high-32 bit of PC for x32 port.
    SetFrameSlot(offset + kSystemPointerSize, 0);
  }
  SetFrameSlot(offset, value);
}


void FrameDescription::SetCallerFp(unsigned offset, intptr_t value) {
  if (kFPOnStackSize == 2 * kSystemPointerSize) {
    // Zero out the high-32 bit of FP for x32 port.
    SetFrameSlot(offset + kSystemPointerSize, 0);
  }
  SetFrameSlot(offset, value);
}


void FrameDescription::SetCallerConstantPool(unsigned offset, intptr_t value) {
  // No embedded constant pool support.
  UNREACHABLE();
}


#undef __


}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_X64
