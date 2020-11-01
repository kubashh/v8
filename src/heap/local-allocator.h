// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LOCAL_ALLOCATOR_H_
#define V8_HEAP_LOCAL_ALLOCATOR_H_

#include "src/common/globals.h"
#include "src/heap/heap.h"
#include "src/heap/new-spaces.h"
#include "src/heap/paged-spaces.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

// Local allocation buffer.
struct Lab {
  Address top = kNullAddress;
  Address limit = kNullAddress;
  Address published_top = kNullAddress;
  Address original_limit = kNullAddress;
  bool IsValid() { return top != kNullAddress; }
};

class AllocatorBase {
 public:
  explicit AllocatorBase(ReadOnlyRoots read_only_roots)
      : read_only_roots_(read_only_roots) {}

  inline AllocationResult Allocate(int object_size,
                                   AllocationAlignment alignment,
                                   AllocationOrigin origin);
  void UndoAllocation(Address object, int object_size);
  void MakeLabIterable();
  void FreeLab();
  void StartBlackAllocation();
  void StopBlackAllocation();

  void AddAllocationObserver(AllocationObserver* observer);
  void RemoveAllocationObserver(AllocationObserver* observer);
  void PublishAllocations();

 protected:
  inline void CreateFiller(Address start, Address end);
  virtual bool RefillLab(int object_size, AllocationAlignment, AllocationOrigin,
                         Lab*) = 0;
  virtual void Free(Lab*) = 0;
  virtual void EnsureValidObject(Address soon_object, int object_size) = 0;
  virtual void StartBlackAllocation(Lab*) = 0;
  virtual void StopBlackAllocation(Lab*) = 0;

 private:
  inline AllocationResult AllocateFast(int object_size, AllocationAlignment);
  AllocationResult AllocateSlow(int object_size, AllocationAlignment,
                                AllocationOrigin);
  void InvokeAllocationObservers(int object_size, AllocationAlignment);

  Lab lab_;
  bool allocation_observer_active_ = false;
  ReadOnlyRoots read_only_roots_;
  AllocationCounter allocation_counter_;
  bool inline_allocation_disabled_ = false;
};

template <typename Space, ThreadKind thread_kind>
class Allocator final : public AllocatorBase {
 public:
  Allocator(Space* space, int min_lab_size, int max_lab_size)
      : space_(space),
        min_lab_size_(min_lab_size),
        max_lab_size_(max_lab_size) {}

 protected:
  bool RefillLab(int object_size, AllocationAlignment, AllocationOrigin origin,
                 Lab* lab) final;
  void Free(Lab*) final;
  void EnsureValidObject(Address soon_object, int object_size) final;
  void StartBlackAllocation(Lab*) final;
  void StopBlackAllocation(Lab*) final;
  Space* const space_;
  const int min_lab_size_;
  const int max_lab_size_;
};

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
        new_space_medium_allocator_(new_space_, 0, 0),
        lab_allocation_will_fail_(false) {}

  // Needs to be called from the main thread to finalize this
  // EvacuationAllocator.
  void Finalize() {
    heap_->old_space()->MergeLocalSpace(compaction_spaces_.Get(OLD_SPACE));
    heap_->code_space()->MergeLocalSpace(compaction_spaces_.Get(CODE_SPACE));
    new_space_small_allocator_.Free();
    new_space_medium_allocator_.Free();
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
