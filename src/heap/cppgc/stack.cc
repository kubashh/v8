// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/stack.h"

#include <limits>

#include "src/base/platform/platform.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/sanitizers.h"

namespace cppgc {
namespace internal {

using IterateStackCallback = void (Stack::*)(StackVisitor*, intptr_t*) const;
extern "C" void PushAllRegistersAndIterateStack(const Stack*, StackVisitor*,
                                                IterateStackCallback);

Stack::Stack(void* stack_start) : stack_start_(stack_start) {
#ifdef V8_USE_ADDRESS_SANITIZER
  asan_fake_stack_ = __asan_get_current_fake_stack();
#endif  // V8_USE_ADDRESS_SANITIZER
}

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

// No ASAN support as method accesses redzones while walking the stack.
NO_SANITIZE_ADDRESS
void Stack::IteratePointersImpl(StackVisitor* visitor,
                                intptr_t* stack_end) const {
  constexpr size_t kRedZoneBytes = 128;
  void** current = reinterpret_cast<void**>(
      reinterpret_cast<uintptr_t>(stack_end - kRedZoneBytes) & ~sizeof(void*));
  for (; current < stack_start_; ++current) {
    // MSAN: Instead of unpoisoning the whole stack, the slot's value is copied
    // into a local which is the unpoisoned.
    void* address = *current;
    MSAN_UNPOISON(address, sizeof(address));
    if (address == nullptr) continue;
    visitor->VisitPointer(address);
    IterateAsanFakeFrameIfNecessary(visitor, stack_end, address);
  }
}

// No ASAN support as accessing fake frames otherwise results in
// "stack-use-after-scope" warnings.
NO_SANITIZE_ADDRESS
void Stack::IterateAsanFakeFrameIfNecessary(StackVisitor* visitor,
                                            intptr_t* stack_end,
                                            void* address) const {
#ifdef V8_USE_ADDRESS_SANITIZER
  // When using ASAN fake stack a pointer to the fake frame is kept on the
  // native frame. In case |addr| points to a fake frame of the current stack
  // iterate the fake frame. Frame layout see
  // https://github.com/google/sanitizers/wiki/AddressSanitizerUseAfterReturn
  if (asan_fake_stack_) {
    void* fake_frame_begin;
    void* fake_frame_end;
    void* real_stack = __asan_addr_is_in_fake_stack(
        asan_fake_stack_, address, &fake_frame_begin, &fake_frame_end);
    if (real_stack) {
      // |address| points to a fake frame. Check that the fake frame is part
      // of this stack.
      if (stack_start_ >= real_stack && real_stack >= stack_end) {
        // Iterate the fake frame.
        for (void** current = reinterpret_cast<void**>(fake_frame_begin);
             current <= fake_frame_end; ++current) {
          void* addr = *current;
          if (addr == nullptr) continue;
          visitor->VisitPointer(addr);
        }
      }
    }
  }
#endif  // V8_USE_ADDRESS_SANITIZER
}

}  // namespace internal
}  // namespace cppgc
