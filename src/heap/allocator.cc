// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/allocator.h"

#include "src/base/atomic-utils.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/local-heap.h"
#include "src/heap/marking.h"
#include "src/heap/memory-chunk.h"

namespace v8 {
namespace internal {

void Allocator::Initialize(Heap* heap, ThreadKind thread_kind, Space* space,
                           size_t object_alignment, size_t min_lab_size,
                           size_t max_lab_size) {
  heap_ = heap;
  allocation_counter_.set_object_alignment(object_alignment);
  thread_kind_ = thread_kind;
  space_ = space;
  object_alignment_ = object_alignment;
  min_lab_size_ = min_lab_size;
  max_lab_size_ = max_lab_size;
}

Allocator::~Allocator() { DCHECK(IsLabEmpty()); }

void Allocator::EnsureValidObject(Address start, Address end) {
  if (space_->identity() == CODE_SPACE) {
    heap_->UnprotectAndRegisterMemoryChunk(MemoryChunk::FromAddress(start));
  }
  CreateFiller(start, end);
}

void Allocator::FreeLab() {
  if (allocation_observer_active_) {
    PublishAllocations();
  }
  lab_.limit = lab_.original_limit;
  space_->FreeLab(thread_kind_, &lab_.top, &lab_.limit);
  DCHECK_EQ(lab_.top, kNullAddress);
  DCHECK_EQ(lab_.limit, kNullAddress);
  base::AsAtomicPtr<Address>(&lab_.original_limit)
      ->store(kNullAddress, std::memory_order_relaxed);
  base::AsAtomicPtr<Address>(&lab_.published_top)
      ->store(kNullAddress, std::memory_order_release);
}

void Allocator::MakeLabIterable() {
  if (!IsLabEmpty()) {
    base::Optional<CodePageCollectionMemoryModificationScope> scope;
    if (space_->identity() == CODE_SPACE) {
      scope.emplace(heap_);
    }
    EnsureValidObject(lab_.top, lab_.original_limit);
  }
}

void Allocator::StartBlackAllocation() {
  if (!IsLabEmpty()) {
    space_->StartBlackAllocation(lab_.top, lab_.original_limit);
  }
}

void Allocator::StopBlackAllocation() {
  if (!IsLabEmpty()) {
    space_->StopBlackAllocation(lab_.top, lab_.original_limit);
  }
}

void Allocator::UpdateHighWatermark() {
  if (lab_.top != kNullAddress) {
    BasicMemoryChunk::UpdateHighWaterMark(lab_.top);
  }
}

AllocationResult Allocator::AllocateSlow(
    int object_size, AllocationAlignment alignment, AllocationOrigin origin,
    HeapLimitHandling heap_limit_handling) {
  if (allocation_observer_active_) {
    PublishAllocations();
  }

  // Restore the original limit and see if that is sufficient to fulfil the
  // allocation request.
  lab_.limit = lab_.original_limit;
  AllocationResult allocation = AllocateFast(object_size, alignment);

  if (allocation.IsFailure()) {
    AllocationFailure failure;
    if (!RefillLab(static_cast<size_t>(object_size), alignment, origin,
                   heap_limit_handling, &failure)) {
      DCHECK_EQ(lab_.top, kNullAddress);
      DCHECK_EQ(lab_.limit, kNullAddress);
      DCHECK_EQ(lab_.published_top, kNullAddress);
      DCHECK_EQ(lab_.original_limit, kNullAddress);
      return AllocationResult(failure);
    }
    DCHECK_EQ(lab_.published_top, lab_.top);
    DCHECK_EQ(lab_.original_limit, lab_.limit);

    allocation = AllocateFast(object_size, alignment);
    DCHECK(!allocation.IsFailure());
  }

  if (allocation_observer_active_) {
    InvokeAllocationObservers(allocation.ToAddress(), object_size);
  }
  if (V8_UNLIKELY(inline_allocation_disabled_)) {
    lab_.limit = lab_.top;
  }
  // The invariants of the local allocation buffer.
  DCHECK_LE(lab_.published_top, lab_.top);
  DCHECK_LE(lab_.top, lab_.limit);
  DCHECK_LE(lab_.limit, lab_.original_limit);
  return allocation;
}

void Allocator::UndoAllocation(Address object, int object_size) {
  if (lab_.top == object + object_size) {
    lab_.top = object;
    DCHECK_LE(lab_.published_top, lab_.top);
  } else {
    CreateFiller(object, object + object_size);
  }
}

void Allocator::PublishAllocations() {
  if (allocation_counter_.IsStepInProgress()) {
    return;
  }
  DCHECK_LE(lab_.published_top, lab_.top);
  size_t allocated_bytes = lab_.top - lab_.published_top;
  if (allocated_bytes) {
    base::AsAtomicPtr<Address>(&lab_.published_top)
        ->store(lab_.top, std::memory_order_release);
    allocation_counter_.AdvanceAllocationObservers(allocated_bytes);
  }
}

void Allocator::AddAllocationObserver(AllocationObserver* observer) {
  allocation_observer_active_ = true;
  PublishAllocations();
  allocation_counter_.AddAllocationObserver(observer);
  AdjustLimitForAllocationObservers();
}

void Allocator::RemoveAllocationObserver(AllocationObserver* observer) {
  DCHECK(allocation_observer_active_);
  PublishAllocations();
  allocation_counter_.RemoveAllocationObserver(observer);
  allocation_observer_active_ = allocation_counter_.IsActive();
  AdjustLimitForAllocationObservers();
}

void Allocator::InvokeAllocationObservers(Address soon_object,
                                          int object_size) {
  DCHECK(allocation_observer_active_);
  size_t allocated_bytes = lab_.top - lab_.published_top;
  if (allocated_bytes >= allocation_counter_.NextBytes()) {
    EnsureValidObject(soon_object, soon_object + object_size);
#if DEBUG
    // Ensure that lab isn't modified during one of the AllocationObserver::Step
    // methods.
    Lab saved_lab = lab_;
#endif
    allocation_counter_.InvokeAllocationObservers(
        soon_object, static_cast<size_t>(object_size), allocated_bytes);
    DCHECK_EQ(saved_lab.top, lab_.top);
    DCHECK_EQ(saved_lab.limit, lab_.limit);
    DCHECK_EQ(saved_lab.published_top, lab_.published_top);
    DCHECK_EQ(saved_lab.original_limit, lab_.original_limit);
  }

  AdjustLimitForAllocationObservers();
}

void Allocator::AdjustLimitForAllocationObservers() {
  lab_.limit = lab_.original_limit;
  if (!allocation_observer_active_ || allocation_counter_.IsStepInProgress()) {
    return;
  }
  // Lower the limit if necessary to ensure that we enter the slow path of the
  // allocation before the next scheduled allocation observer step.
  size_t step = allocation_counter_.NextBytes();
  DCHECK_NE(step, 0);
  DCHECK_GE(step, lab_.top - lab_.published_top);
  DCHECK_EQ(step % object_alignment_, 0);
  if (lab_.limit - lab_.published_top > step - object_alignment_) {
    lab_.limit = lab_.published_top + step - object_alignment_;
  }
  DCHECK_LE(lab_.top, lab_.limit);
}

void Allocator::DisableInlineAllocation() {
  FreeLab();
  inline_allocation_disabled_ = true;
}

void Allocator::EnableInlineAllocation() {
  inline_allocation_disabled_ = false;
}

bool Allocator::RefillLab(size_t object_size, AllocationAlignment alignment,
                          AllocationOrigin origin,
                          HeapLimitHandling heap_limit_handling,
                          AllocationFailure* failure) {
  DCHECK_EQ(lab_.limit, lab_.original_limit);
  if (!space_->RefillLab(thread_kind_, std::max(object_size, min_lab_size_),
                         std::max(object_size, max_lab_size_), alignment,
                         origin, heap_limit_handling, &lab_.top, &lab_.limit,
                         failure)) {
    DCHECK_EQ(lab_.top, kNullAddress);
    DCHECK_EQ(lab_.limit, kNullAddress);
    base::AsAtomicPtr<Address>(&lab_.original_limit)
        ->store(kNullAddress, std::memory_order_relaxed);
    base::AsAtomicPtr<Address>(&lab_.published_top)
        ->store(kNullAddress, std::memory_order_release);
    return false;
  }
  base::AsAtomicPtr<Address>(&lab_.original_limit)
      ->store(lab_.limit, std::memory_order_relaxed);
  base::AsAtomicPtr<Address>(&lab_.published_top)
      ->store(lab_.top, std::memory_order_release);
  return true;
}

}  // namespace internal
}  // namespace v8
