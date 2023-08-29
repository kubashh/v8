// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/stacks.h"

#include "src/base/platform/platform.h"
#include "src/execution/simulator.h"

namespace v8::internal::wasm {

// static
StackMemory* StackMemory::GetCurrentStackView(Isolate* isolate) {
  base::Vector<uint8_t> view = SimulatorStack::GetCurrentStackView(isolate);
  return new StackMemory(isolate, view.begin(), view.size());
}

StackMemory::~StackMemory() {
  if (v8_flags.trace_wasm_stack_switching) {
    PrintF("Delete stack #%d\n", id_);
  }
  PageAllocator* allocator = GetPlatformPageAllocator();
  if (owned_ && !allocator->DecommitPages(limit_, size_)) {
    V8::FatalProcessOutOfMemory(nullptr, "Decommit stack memory");
  }
  // We don't need to handle removing the last stack from the list (next_ ==
  // this). This only happens on isolate tear down, otherwise there is always
  // at least one reachable stack (the active stack).
  isolate_->wasm_stacks() = next_;
  prev_->next_ = next_;
  next_->prev_ = prev_;
  // Deallocate initial_shadow_stack_. For the main continuation, we do not
  // deallocate the GCS. This is denoted by initial_shadow_stack_ being zero.
  if (initial_shadow_stack_) {
    // TODO(arm64): Abstract this on Simulator.
#if defined(V8_ENABLE_SHADOW_STACK) and defined(V8_TARGET_ARCH_ARM64) and \
    defined(USE_SIMULATOR)
    Simulator::current(isolate_)->GetGCSManager().FreeGuardedControlStack(
        initial_shadow_stack_);
#endif
  }
}

void StackMemory::Add(StackMemory* stack) {
  stack->next_ = this->next_;
  stack->prev_ = this;
  this->next_->prev_ = stack;
  this->next_ = stack;
}

StackMemory::StackMemory(Isolate* isolate)
    : isolate_(isolate), owned_(true), initial_shadow_stack_(0) {
  static std::atomic<int> next_id(1);
  id_ = next_id.fetch_add(1);
  PageAllocator* allocator = GetPlatformPageAllocator();
  int kJsStackSizeKB = v8_flags.wasm_stack_switching_stack_size;
  size_ = (kJsStackSizeKB + kJSLimitOffsetKB) * KB;
  size_ = RoundUp(size_, allocator->AllocatePageSize());
  limit_ = static_cast<uint8_t*>(
      allocator->AllocatePages(nullptr, size_, allocator->AllocatePageSize(),
                               PageAllocator::kReadWrite));
  if (v8_flags.trace_wasm_stack_switching) {
    PrintF("Allocate stack #%d (limit: %p, base: %p)\n", id_, limit_,
           limit_ + size_);
  }
// TODO(arm64): Abstract this on Simulator.
#if defined(V8_ENABLE_SHADOW_STACK) and defined(V8_TARGET_ARCH_ARM64) and \
    defined(USE_SIMULATOR)
  uint64_t new_gcs = Simulator::current(isolate)
                         ->GetGCSManager()
                         .AllocateGuardedControlStack();
  initial_shadow_stack_ = reinterpret_cast<Address>(new_gcs);
#endif
}

// Overload to represent a view of the libc stack.
StackMemory::StackMemory(Isolate* isolate, uint8_t* limit, size_t size)
    : isolate_(isolate),
      limit_(limit),
      size_(size),
      owned_(false),
      initial_shadow_stack_(0) {
  id_ = 0;
}

}  // namespace v8::internal::wasm
