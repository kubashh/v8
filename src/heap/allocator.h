// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_ALLOCATOR_H_
#define V8_HEAP_ALLOCATOR_H_

#include "src/common/globals.h"
#include "src/heap/allocation-observer.h"
#include "src/objects/heap-object.h"
#include "src/objects/objects.h"
#include "src/objects/smi.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

class Space;

enum class AllocationOrigin {
  kGeneratedCode = 0,
  kRuntime = 1,
  kGC = 2,
  kFirstAllocationOrigin = kGeneratedCode,
  kLastAllocationOrigin = kGC,
  kNumberOfAllocationOrigins = kLastAllocationOrigin + 1
};

class AllocationResult {
 public:
  static inline AllocationResult Retry(AllocationSpace space = NEW_SPACE) {
    return AllocationResult(space);
  }

  // Implicit constructor from Object.
  AllocationResult(Object object)  // NOLINT
      : object_(object) {
    // AllocationResults can't return Smis, which are used to represent
    // failure and the space to retry in.
    CHECK(!object.IsSmi());
  }

  AllocationResult() : object_(Smi::FromInt(NEW_SPACE)) {}

  inline bool IsRetry() { return object_.IsSmi(); }
  inline HeapObject ToObjectChecked();
  inline HeapObject ToObject();
  inline Address ToAddress();
  inline AllocationSpace RetrySpace();

  template <typename T>
  bool To(T* obj) {
    if (IsRetry()) return false;
    *obj = T::cast(object_);
    return true;
  }

 private:
  explicit AllocationResult(AllocationSpace space)
      : object_(Smi::FromInt(static_cast<int>(space))) {}

  Object object_;
};

STATIC_ASSERT(sizeof(AllocationResult) == kSystemPointerSize);

// Local allocation buffer.
struct Lab {
  Address top = kNullAddress;
  Address limit = kNullAddress;
  Address published_top = kNullAddress;
  Address original_limit = kNullAddress;
  bool IsValid() { return top != kNullAddress; }
};

class Allocator {
 public:
  Allocator(ThreadKind thread_kind, Space* space, size_t object_alignment,
            int min_lab_size, int max_lab_size, ReadOnlyRoots read_only_roots)
      : read_only_roots_(read_only_roots),
        space_(space),
        thread_kind_(thread_kind),
        object_alignment_(object_alignment),
        min_lab_size_(min_lab_size),
        max_lab_size_(max_lab_size) {}

  ~Allocator();

  inline AllocationResult Allocate(int object_size,
                                   AllocationAlignment alignment,
                                   AllocationOrigin origin);

  inline bool IsPendingAllocation(HeapObject object);

  void UndoAllocation(Address object, int object_size);
  void MakeLabIterable();
  void FreeLab();
  void StartBlackAllocation();
  void StopBlackAllocation();

  void AddAllocationObserver(AllocationObserver* observer);
  void RemoveAllocationObserver(AllocationObserver* observer);
  void PublishAllocations();

  void DisableInlineAllocation();
  void EnableInlineAllocation();

  void set_space(Space* space) { space_ = space; }
  Address* top_address() { return &lab_.top; }
  Address* limit_address() { return &lab_.limit; }

 private:
  inline AllocationResult AllocateFast(int object_size, AllocationAlignment);
  AllocationResult AllocateSlow(int object_size, AllocationAlignment,
                                AllocationOrigin);
  void InvokeAllocationObservers(Address soon_object, int object_size);
  inline void CreateFiller(Address start, Address end);
  void EnsureValidObject(Address start, Address end);
  bool RefillLab(int object_size, AllocationAlignment alignment,
                 AllocationOrigin origin);

  Lab lab_;
  bool allocation_observer_active_ = false;
  bool inline_allocation_disabled_ = false;
  ReadOnlyRoots read_only_roots_;
  AllocationCounter allocation_counter_;
  Space* space_;
  ThreadKind thread_kind_;
  size_t object_alignment_ = 0;
  const int min_lab_size_;
  const int max_lab_size_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_ALLOCATOR_H_
