// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_THREAD_SAFE_DESTRUCTIBLE_DATA_H_
#define INCLUDE_CPPGC_THREAD_SAFE_DESTRUCTIBLE_DATA_H_

#include "cppgc/garbage-collected.h"
#include "cppgc/internal/caged-heap.h"

namespace cppgc::subtle {

// ThreadSafeDestructibleData represents a class allocated on the cppgc heap.
// Descendents of it are required to have no outgoing pointers, i.e. they can
// only represent data. The key feature of such classes is that lifetime of them
// can be controlled externally, the class can specify the trait (TODO) which
// the garbage collector would query to check if the objects can safely be
// destroyed (concurrently with the mutator thread).
template <typename T>
class ThreadSafeDestructibleData : public GarbageCollected<T> {
 public:
  bool IsOnManagedHeap() const {
    return internal::CagedHeapBase::IsWithinCage(this);
  }
};

}  // namespace cppgc::subtle

#endif  // INCLUDE_CPPGC_THREAD_SAFE_DESTRUCTIBLE_DATA_H_
