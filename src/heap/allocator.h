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

class AllocatorBase {
 public:
  explicit AllocatorBase(ReadOnlyRoots read_only_roots)
      : read_only_roots_(read_only_roots) {}

  virtual ~AllocatorBase();

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

  Address* top_address() { return &lab_.top; }
  Address* limit_address() { return &lab_.limit; }

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
  void InvokeAllocationObservers(Address soon_object, int object_size);

  Lab lab_;
  bool allocation_observer_active_ = false;
  bool inline_allocation_disabled_ = false;
  ReadOnlyRoots read_only_roots_;
  AllocationCounter allocation_counter_;
};

template <typename Space, ThreadKind thread_kind>
class Allocator final : public AllocatorBase {
 public:
  Allocator(Space* space, int min_lab_size, int max_lab_size)
      : space_(space),
        min_lab_size_(min_lab_size),
        max_lab_size_(max_lab_size) {}

  void set_space(Space* space) { space_ = space; }

 protected:
  bool RefillLab(int object_size, AllocationAlignment, AllocationOrigin origin,
                 Lab* lab) final;
  void Free(Lab*) final;
  void EnsureValidObject(Address soon_object, int object_size) final;
  void StartBlackAllocation(Lab*) final;
  void StopBlackAllocation(Lab*) final;
  Space* space_;
  const int min_lab_size_;
  const int max_lab_size_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_ALLOCATOR_H_
