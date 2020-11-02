// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LOCAL_ALLOCATOR_H_
#define V8_HEAP_LOCAL_ALLOCATOR_H_

#include "src/common/globals.h"
#include "src/heap/allocator.h"
#include "src/heap/heap.h"
#include "src/heap/new-spaces.h"
#include "src/heap/paged-spaces.h"
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
        new_space_small_allocator_(ThreadKind::kBackground, new_space_,
                                   kTaggedSize, kLabSize, kLabSize,
                                   ReadOnlyRoots(heap)),
        new_space_medium_allocator_(ThreadKind::kBackground, new_space_,
                                    kTaggedSize, 0, 0, ReadOnlyRoots(heap)),
        old_space_allocator_(ThreadKind::kBackground,
                             compaction_spaces_.Get(OLD_SPACE), kTaggedSize, 0,
                             0, ReadOnlyRoots(heap)),
        code_space_allocator_(ThreadKind::kBackground,
                              compaction_spaces_.Get(CODE_SPACE),
                              kCodeAlignment, 0, 0, ReadOnlyRoots(heap)) {}

  // Needs to be called from the main thread to finalize this
  // EvacuationAllocator.
  void Finalize() {
    new_space_small_allocator_.FreeLab();
    new_space_medium_allocator_.FreeLab();
    old_space_allocator_.FreeLab();
    code_space_allocator_.FreeLab();
    heap_->old_space()->MergeLocalSpace(compaction_spaces_.Get(OLD_SPACE));
    heap_->code_space()->MergeLocalSpace(compaction_spaces_.Get(CODE_SPACE));
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
  Allocator new_space_small_allocator_;
  Allocator new_space_medium_allocator_;
  Allocator old_space_allocator_;
  Allocator code_space_allocator_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LOCAL_ALLOCATOR_H_
