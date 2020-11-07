// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_ALLOCATOR_INL_H_
#define V8_HEAP_ALLOCATOR_INL_H_

#include "src/base/atomic-utils.h"
#include "src/heap/allocator.h"
#include "src/heap/heap.h"
#include "src/roots/roots-inl.h"
#include "src/sanitizer/msan.h"

namespace v8 {
namespace internal {

AllocationFailure AllocationResult::Failure() {
  DCHECK(IsFailure());
  return static_cast<AllocationFailure>(Smi::ToInt(object_));
}

AllocationSpace AllocationResult::RetrySpace() {
  switch (Failure()) {
    case AllocationFailure::kRetryAfterYoungGC:
      return NEW_SPACE;
    case AllocationFailure::kRetryAfterFullGC:
      return OLD_SPACE;
    case AllocationFailure::kRetryAfterIncrementalMarkingStart:
      UNREACHABLE();
      return OLD_SPACE;
  }
}

HeapObject AllocationResult::ToObjectChecked() {
  CHECK(!IsFailure());
  return HeapObject::cast(object_);
}

HeapObject AllocationResult::ToObject() {
  DCHECK(!IsFailure());
  return HeapObject::cast(object_);
}

Address AllocationResult::ToAddress() {
  DCHECK(!IsFailure());
  return HeapObject::cast(object_).address();
}

AllocationResult Allocator::Allocate(int object_size,
                                     AllocationAlignment alignment,
                                     AllocationOrigin origin,
                                     HeapLimitHandling heap_limit_handling) {
  AllocationResult allocation = AllocateFast(object_size, alignment);
  return allocation.IsFailure()
             ? AllocateSlow(object_size, alignment, origin, heap_limit_handling)
             : allocation;
}

AllocationResult Allocator::AllocateFast(int object_size,
                                         AllocationAlignment alignment) {
  Address old_top = lab_.top;
  int filler_size = Heap::GetFillToAlign(old_top, alignment);

  Address new_top = old_top + filler_size + object_size;
  if (new_top > lab_.limit) return AllocationResult::RetryAfterFullGC();

  lab_.top = new_top;
  HeapObject result;

  if (filler_size > 0) {
    result = Heap::PrecedeWithFiller(
        ReadOnlyRoots(heap_), HeapObject::FromAddress(old_top), filler_size);
  } else {
    result = HeapObject::FromAddress(old_top);
  }
  MSAN_ALLOCATED_UNINITIALIZED_MEMORY(result.address(), object_size);
  return AllocationResult(result);
}

bool Allocator::IsPendingAllocation(HeapObject object) {
  Address published_top = base::AsAtomicPtr<Address>(&lab_.published_top)
                              ->load(std::memory_order_acquire);
  Address original_limit = base::AsAtomicPtr<Address>(&lab_.original_limit)
                               ->load(std::memory_order_relaxed);
  Address addr = object.address();
  return published_top <= addr && addr < original_limit;
}

void Allocator::CreateFiller(Address start, Address end) {
  heap_->CreateFillerObjectAtBackground(
      start, static_cast<int>(end - start),
      ClearFreedMemoryMode::kDontClearFreedMemory);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_ALLOCATOR_INL_H_
