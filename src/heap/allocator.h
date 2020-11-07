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

enum class AllocationFailure : int {
  kRetryAfterYoungGC,
  kRetryAfterFullGC,
  kRetryAfterIncrementalMarkingStart
};

class AllocationResult {
 public:
  static inline AllocationResult RetryAfterYoungGC() {
    return AllocationResult(AllocationFailure::kRetryAfterYoungGC);
  }
  static inline AllocationResult RetryAfterFullGC() {
    return AllocationResult(AllocationFailure::kRetryAfterFullGC);
  }
  static inline AllocationResult RetryAfterIncrementalMarkingStart() {
    return AllocationResult(
        AllocationFailure::kRetryAfterIncrementalMarkingStart);
  }

  // Implicit constructor from Object.
  AllocationResult(Object object)  // NOLINT
      : object_(object) {
    // AllocationResults can't return Smis, which are used to represent
    // failure and the space to retry in.
    CHECK(!object.IsSmi());
  }

  explicit AllocationResult(AllocationFailure failure)
      : object_(Smi::FromInt(static_cast<int>(failure))) {}

  AllocationResult() = default;

  inline bool IsFailure() { return object_.IsSmi(); }
  inline HeapObject ToObjectChecked();
  inline HeapObject ToObject();
  inline Address ToAddress();
  inline AllocationFailure Failure();
  inline AllocationSpace RetrySpace();

  template <typename T>
  bool To(T* obj) {
    if (IsFailure()) return false;
    *obj = T::cast(object_);
    return true;
  }

 private:
  Object object_;
};

STATIC_ASSERT(sizeof(AllocationResult) == kSystemPointerSize);

enum class HeapLimitHandling { kRespect, kIgnore };

// Local allocation buffer.
struct Lab {
  Address top = kNullAddress;
  Address limit = kNullAddress;
  Address published_top = kNullAddress;
  Address original_limit = kNullAddress;
  bool IsEmpty() { return top == original_limit; }
};

class Allocator {
 public:
  Allocator() = default;

  Allocator(Heap* heap, ThreadKind thread_kind, Space* space,
            size_t object_alignment, size_t min_lab_size, size_t max_lab_size)
      : heap_(heap),
        allocation_counter_(object_alignment),
        space_(space),
        thread_kind_(thread_kind),
        object_alignment_(object_alignment),
        min_lab_size_(min_lab_size),
        max_lab_size_(max_lab_size) {}

  ~Allocator();

  void Initialize(Heap* heap, ThreadKind thread_kind, Space* space,
                  size_t object_alignment, size_t min_lab_size,
                  size_t max_lab_size);

  inline AllocationResult Allocate(int object_size,
                                   AllocationAlignment alignment,
                                   AllocationOrigin origin,
                                   HeapLimitHandling heap_limit_handling);

  inline bool IsPendingAllocation(HeapObject object);

  void UndoAllocation(Address object, int object_size);
  void MakeLabIterable();
  void FreeLab();
  bool IsLabEmpty();
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
                                AllocationOrigin, HeapLimitHandling);
  void InvokeAllocationObservers(Address soon_object, int object_size);
  void AdjustLimitForAllocationObservers();
  inline void CreateFiller(Address start, Address end);
  void EnsureValidObject(Address start, Address end);
  bool RefillLab(size_t object_size, AllocationAlignment alignment,
                 AllocationOrigin origin, HeapLimitHandling,
                 AllocationFailure* failure);

  Lab lab_;
  bool allocation_observer_active_ = false;
  bool inline_allocation_disabled_ = false;
  Heap* heap_;
  AllocationCounter allocation_counter_;
  Space* space_;
  ThreadKind thread_kind_;
  size_t object_alignment_ = 0;
  size_t min_lab_size_;
  size_t max_lab_size_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_ALLOCATOR_H_
