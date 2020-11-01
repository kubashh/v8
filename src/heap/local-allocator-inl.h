// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LOCAL_ALLOCATOR_INL_H_
#define V8_HEAP_LOCAL_ALLOCATOR_INL_H_

#include "src/heap/local-allocator.h"

#include "src/heap/spaces-inl.h"

namespace v8 {
namespace internal {

AllocationResult AllocatorBase::Allocate(int object_size,
                                         AllocationAlignment alignment,
                                         AllocationOrigin origin) {
  AllocationResult allocation = AllocateFast(object_size, alignment);
  return allocation.IsRetry() ? AllocateSlow(object_size, alignment, origin)
                              : allocation;
}

AllocationResult AllocatorBase::AllocateFast(int object_size,
                                             AllocationAlignment alignment) {
  Address old_top = lab_.top;
  int filler_size = Heap::GetFillToAlign(old_top, alignment);

  Address new_top = old_top + filler_size + size_in_bytes;
  if (new_top > lab_.limit) return AllocationResult::Retry();

  lab_.top = new_top;
  if (filler_size > 0) {
    return Heap::PrecedeWithFiller(
        read_only_roots_, HeapObject::FromAddress(old_top), filler_size);
  }
  MSAN_ALLOCATED_UNINITIALIZED_MEMORY(old_top, size_in_bytes);
  return AllocationResult(HeapObject::FromAddress(old_top));
}

void AllocatorBase::CreateFiller(Address start, Address end) {
  Heap::CreateFillerObjectAt(read_only_roots_, start, end - start,
                             ClearFreedMemoryMode::kDontClearFreedMemory);
}

AllocationResult EvacuationAllocator::Allocate(AllocationSpace space,
                                               int object_size,
                                               AllocationOrigin origin,
                                               AllocationAlignment alignment) {
  switch (space) {
    case NEW_SPACE:
      return AllocateInNewSpace(object_size, origin, alignment);
    case OLD_SPACE:
      return compaction_spaces_.Get(OLD_SPACE)->AllocateRaw(object_size,
                                                            alignment, origin);
    case CODE_SPACE:
      return compaction_spaces_.Get(CODE_SPACE)
          ->AllocateRaw(object_size, alignment, origin);
    default:
      UNREACHABLE();
  }
}

void EvacuationAllocator::FreeLast(AllocationSpace space, HeapObject object,
                                   int object_size) {
  switch (space) {
    case NEW_SPACE:
      FreeLastInNewSpace(object, object_size);
      return;
    case OLD_SPACE:
      FreeLastInOldSpace(object, object_size);
      return;
    default:
      // Only new and old space supported.
      UNREACHABLE();
  }
}

void EvacuationAllocator::FreeLastInNewSpace(HeapObject object,
                                             int object_size) {
  if (object_size > kMaxLabObjectSize) {
    return new_space_medium_allocator_->UndoAllocation(object.address(),
                                                       object_size);
  }
  return new_space_small_allocator->UndoAllocation(object.address(),
                                                   object_size);
}

void EvacuationAllocator::FreeLastInOldSpace(HeapObject object,
                                             int object_size) {
  if (!compaction_spaces_.Get(OLD_SPACE)->TryFreeLast(object, object_size)) {
    // We couldn't free the last object so we have to write a proper filler.
    heap_->CreateFillerObjectAt(object.address(), object_size,
                                ClearRecordedSlots::kNo);
  }
}

AllocationResult EvacuationAllocator::AllocateInNewSpace(
    int object_size, AllocationOrigin origin, AllocationAlignment alignment) {
  if (object_size > kMaxLabObjectSize) {
    return new_space_medium_allocator_->Allocate(object_size, alignment,
                                                 origin);
  }
  return new_space_small_allocator->Allocate(object_size, alignment, origin);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LOCAL_ALLOCATOR_INL_H_
