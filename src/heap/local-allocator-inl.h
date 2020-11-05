// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LOCAL_ALLOCATOR_INL_H_
#define V8_HEAP_LOCAL_ALLOCATOR_INL_H_

#include "src/heap/local-allocator.h"

#include "src/heap/spaces-inl.h"

namespace v8 {
namespace internal {

AllocationResult EvacuationAllocator::Allocate(AllocationSpace space,
                                               int object_size,
                                               AllocationOrigin origin,
                                               AllocationAlignment alignment) {
  switch (space) {
    case NEW_SPACE:
      if (object_size > kMaxLabObjectSize) {
        return new_space_medium_allocator_.Allocate(
            object_size, alignment, origin, HeapLimitHandling::kIgnore);
      }
      return new_space_small_allocator_.Allocate(object_size, alignment, origin,
                                                 HeapLimitHandling::kIgnore);
    case OLD_SPACE:
      return old_space_allocator_.Allocate(object_size, alignment, origin,
                                           HeapLimitHandling::kIgnore);
    case CODE_SPACE:
      return code_space_allocator_.Allocate(object_size, alignment, origin,
                                            HeapLimitHandling::kIgnore);
    default:
      UNREACHABLE();
  }
}

void EvacuationAllocator::FreeLast(AllocationSpace space, HeapObject object,
                                   int object_size) {
  switch (space) {
    case NEW_SPACE:
      if (object_size > kMaxLabObjectSize) {
        new_space_medium_allocator_.UndoAllocation(object.address(),
                                                   object_size);
      } else {
        new_space_small_allocator_.UndoAllocation(object.address(),
                                                  object_size);
      }
      return;
    case OLD_SPACE:
      old_space_allocator_.UndoAllocation(object.address(), object_size);
      return;
    default:
      // Only new and old space supported.
      UNREACHABLE();
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LOCAL_ALLOCATOR_INL_H_
