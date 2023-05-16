// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_PARKED_SCOPE_INL_H_
#define V8_HEAP_PARKED_SCOPE_INL_H_

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/semaphore.h"
#include "src/base/witness.h"
#include "src/execution/local-isolate.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/parked-scope.h"

namespace v8 {
namespace internal {

V8_INLINE ParkedMutexGuard::ParkedMutexGuard(LocalIsolate* local_isolate,
                                             base::Mutex* mutex)
    : ParkedMutexGuard(local_isolate->heap(), mutex) {}

V8_INLINE ParkedMutexGuard::ParkedMutexGuard(LocalHeap* local_heap,
                                             base::Mutex* mutex)
    : mutex_(mutex) {
  DCHECK(AllowGarbageCollection::IsAllowed());
  if (!mutex_->TryLock()) {
    local_heap->ExecuteWhileParked([this]() { mutex_->Lock(); });
  }
}

V8_INLINE ParkedRecursiveMutexGuard::ParkedRecursiveMutexGuard(
    LocalIsolate* local_isolate, base::RecursiveMutex* mutex)
    : ParkedRecursiveMutexGuard(local_isolate->heap(), mutex) {}

V8_INLINE ParkedRecursiveMutexGuard::ParkedRecursiveMutexGuard(
    LocalHeap* local_heap, base::RecursiveMutex* mutex)
    : mutex_(mutex) {
  DCHECK(AllowGarbageCollection::IsAllowed());
  if (!mutex_->TryLock()) {
    local_heap->ExecuteWhileParked([this]() { mutex_->Lock(); });
  }
}

template <base::MutexSharedType kIsShared, base::NullBehavior Behavior>
V8_INLINE
ParkedSharedMutexGuardIf<kIsShared, Behavior>::ParkedSharedMutexGuardIf(
    LocalHeap* local_heap, base::SharedMutex* mutex, bool enable_mutex) {
  DCHECK(AllowGarbageCollection::IsAllowed());
  DCHECK_IMPLIES(Behavior == base::NullBehavior::kRequireNotNull,
                 mutex != nullptr);
  if (!enable_mutex) return;
  mutex_ = mutex;

  if (kIsShared) {
    if (!mutex_->TryLockShared()) {
      local_heap->ExecuteWhileParked([this]() { mutex_->LockShared(); });
    }
  } else {
    if (!mutex_->TryLockExclusive()) {
      local_heap->ExecuteWhileParked([this]() { mutex_->LockExclusive(); });
    }
  }
}

V8_INLINE void ParkingSemaphore::ParkedWait(LocalIsolate* local_isolate,
                                            bool with_trampoline) {
  ParkedWait(local_isolate->heap(), with_trampoline);
}

V8_INLINE void ParkingSemaphore::ParkedWait(LocalHeap* local_heap,
                                            bool with_trampoline) {
  if (with_trampoline) {
    local_heap->ExecuteWhileParked(
        [this](const ParkedWitness& parked) { ParkedWait(parked); });
  } else {
    ParkedScope parked(local_heap);
    ParkedWait(parked);
  }
}

V8_INLINE void ParkingThread::ParkedJoin(LocalIsolate* local_isolate) {
  ParkedJoin(local_isolate->heap());
}

V8_INLINE void ParkingThread::ParkedJoin(LocalHeap* local_heap) {
  local_heap->ExecuteWhileParked(
      [this](const ParkedWitness& parked) { ParkedJoin(parked); });
}

template <typename Thread>
// static
V8_INLINE void ParkingThread::ParkedJoinAll(
    LocalIsolate* local_isolate, const std::vector<Thread>& threads) {
  ParkedJoinAll(local_isolate->heap(), threads);
}

template <typename Thread>
// static
V8_INLINE void ParkingThread::ParkedJoinAll(
    LocalHeap* local_heap, const std::vector<Thread>& threads) {
  local_heap->ExecuteWhileParked([&threads](const ParkedWitness& parked) {
    ParkedJoinAll(parked, threads);
  });
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_PARKED_SCOPE_INL_H_
