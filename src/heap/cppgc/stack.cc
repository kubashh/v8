// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/stack.h"

#include <limits>

#include "src/base/platform/platform.h"
#include "src/heap/cppgc/globals.h"

namespace cppgc {
namespace internal {

using IterateStackCallback = void (Stack::*)(StackVisitor*, intptr_t*) const;
extern "C" void PushAllRegistersAndIterateStack(const Stack*, StackVisitor*,
                                                IterateStackCallback);

Stack::Stack(void* stack_start) : stack_start_(stack_start) {}

bool Stack::IsOnStack(void* slot) const {
  void* raw_slot = v8::base::Stack::GetStackSlot(slot);
  return v8::base::Stack::GetCurrentStackPosition() <= raw_slot &&
         raw_slot <= stack_start_;
}

#ifdef V8_TARGET_ARCH_X64
void Stack::IteratePointers(StackVisitor* visitor) const {
  PushAllRegistersAndIterateStack(this, visitor, &Stack::IteratePointersImpl);
}
#endif  // V8_TARGET_ARCH_X64

void Stack::IteratePointersImpl(StackVisitor* visitor,
                                intptr_t* stack_end) const {
  uintptr_t* current = reinterpret_cast<uintptr_t*>(
      reinterpret_cast<uintptr_t>(stack_end) & ~kAddressMask);
  for (; current < stack_start_; ++current) {
    uintptr_t address = *current;
    if (address == 0) continue;
    // TODO(mlippautz): MSAN unpoison |address| which allows working with
    // uninitialized and poisoned stacks.
    visitor->VisitPointer(reinterpret_cast<void*>(address));
  }
}

}  // namespace internal
}  // namespace cppgc
