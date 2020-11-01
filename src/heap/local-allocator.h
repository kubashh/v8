// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LOCAL_ALLOCATOR_H_
#define V8_HEAP_LOCAL_ALLOCATOR_H_

#include "src/common/globals.h"
#include "src/heap/allocator.h"
#include "src/objects/heap-object.h"
#include "src/objects/objects.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

// Allocator encapsulating thread-local allocation durning collection. Assumes
// that all other allocations also go through EvacuationAllocator.
class EvacuationAllocator {
 public:
  static const int kLabSize = 32 * KB;
  static const int kMaxLabObjectSize = 8 * KB;

  explicit EvacuationAllocator(Heap* heap, LocalSpaceKind local_space_kind)
      : heap_(heap),
        new_space_(heap->new_space()),
        compaction_spaces_(heap, local_space_kind),
        new_space_small_allocator_(new_space_, kLabSize, kLabSize),
        new_space_medium_allocator_(new_space_, 0, 0) {}

  // Needs to be called from the main thread to finalize this
  // EvacuationAllocator.
  void Finalize() {
    heap_->old_space()->MergeLocalSpace(compaction_spaces_.Get(OLD_SPACE));
    heap_->code_space()->MergeLocalSpace(compaction_spaces_.Get(CODE_SPACE));
    new_space_small_allocator_.FreeLab();
    new_space_medium_allocator_.FreeLab();
  }

  inline AllocationResult Allocate(AllocationSpace space, int object_size,
                                   AllocationOrigin origin,
                                   AllocationAlignment alignment);
  inline void FreeLast(AllocationSpace space, HeapObject object,
                       int object_size);

 private:
  inline AllocationResult AllocateInNewSpace(int object_size,
                                             AllocationOrigin origin,
                                             AllocationAlignment alignment);
  inline void FreeLastInNewSpace(HeapObject object, int object_size);
  inline void FreeLastInOldSpace(HeapObject object, int object_size);

  Heap* const heap_;
  NewSpace* const new_space_;
  CompactionSpaceCollection compaction_spaces_;
  Allocator<NewSpace, ThreadKind::kBackground> new_space_small_allocator_;
  Allocator<NewSpace, ThreadKind::kBackground> new_space_medium_allocator_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LOCAL_ALLOCATOR_H_
