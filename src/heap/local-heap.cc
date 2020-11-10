// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/local-heap.h"

#include <memory>

#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/handles/local-handles.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap-write-barrier.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/marking-barrier.h"
#include "src/heap/safepoint.h"

namespace v8 {
namespace internal {

namespace {
thread_local LocalHeap* current_local_heap = nullptr;
}  // namespace

LocalHeap* LocalHeap::Current() { return current_local_heap; }

LocalHeap::LocalHeap(Heap* heap, ThreadKind kind,
                     std::unique_ptr<PersistentHandles> persistent_handles)
    : heap_(heap),
      is_main_thread_(kind == ThreadKind::kMain),
      state_(ThreadState::Parked),
      safepoint_requested_(false),
      allocation_failed_(false),
      prev_(nullptr),
      next_(nullptr),
      handles_(new LocalHandles),
      persistent_handles_(std::move(persistent_handles)),
      marking_barrier_(new MarkingBarrier(this)),
      old_space_small_allocator_(heap, ThreadKind::kBackground,
                                 heap->old_space(), kTaggedSize, kLabSize,
                                 kMaxLabSize),
      old_space_medium_allocator_(heap, ThreadKind::kBackground,
                                  heap->old_space(), kTaggedSize, 0, 0),
      lo_space_allocator_(heap, ThreadKind::kBackground, heap->lo_space(),
                          kTaggedSize, 0, 0) {
  heap_->safepoint()->AddLocalHeap(this, [this] {
    if (FLAG_local_heaps) {
      WriteBarrier::SetForThread(marking_barrier_.get());
      if (heap_->incremental_marking()->IsMarking()) {
        marking_barrier_->Activate(
            heap_->incremental_marking()->IsCompacting());
      }
    }
  });

  if (persistent_handles_) {
    persistent_handles_->Attach(this);
  }
  DCHECK_NULL(current_local_heap);
  current_local_heap = this;
}

LocalHeap::~LocalHeap() {
  // Park thread since removing the local heap could block.
  EnsureParkedBeforeDestruction();

  heap_->safepoint()->RemoveLocalHeap(this, [this] {
    FreeLabs();
    if (FLAG_local_heaps) {
      marking_barrier_->Publish();
      WriteBarrier::ClearForThread(marking_barrier_.get());
    }
  });

  DCHECK_EQ(current_local_heap, this);
  current_local_heap = nullptr;
}

void LocalHeap::EnsurePersistentHandles() {
  if (!persistent_handles_) {
    persistent_handles_.reset(
        heap_->isolate()->NewPersistentHandles().release());
    persistent_handles_->Attach(this);
  }
}

void LocalHeap::AttachPersistentHandles(
    std::unique_ptr<PersistentHandles> persistent_handles) {
  DCHECK_NULL(persistent_handles_);
  persistent_handles_ = std::move(persistent_handles);
  persistent_handles_->Attach(this);
}

std::unique_ptr<PersistentHandles> LocalHeap::DetachPersistentHandles() {
  if (persistent_handles_) persistent_handles_->Detach();
  return std::move(persistent_handles_);
}

#ifdef DEBUG
bool LocalHeap::ContainsPersistentHandle(Address* location) {
  return persistent_handles_ ? persistent_handles_->Contains(location) : false;
}

bool LocalHeap::ContainsLocalHandle(Address* location) {
  return handles_ ? handles_->Contains(location) : false;
}

bool LocalHeap::IsHandleDereferenceAllowed() {
  DCHECK_EQ(LocalHeap::Current(), this);
  return state_ == ThreadState::Running;
}
#endif

bool LocalHeap::IsParked() {
  DCHECK_EQ(LocalHeap::Current(), this);
  return state_ == ThreadState::Parked;
}

void LocalHeap::Park() {
  base::MutexGuard guard(&state_mutex_);
  CHECK(state_ == ThreadState::Running);
  state_ = ThreadState::Parked;
  state_change_.NotifyAll();
}

void LocalHeap::Unpark() {
  base::MutexGuard guard(&state_mutex_);
  CHECK(state_ == ThreadState::Parked);
  state_ = ThreadState::Running;
}

void LocalHeap::EnsureParkedBeforeDestruction() {
  if (IsParked()) return;
  base::MutexGuard guard(&state_mutex_);
  state_ = ThreadState::Parked;
  state_change_.NotifyAll();
}

void LocalHeap::RequestSafepoint() {
  safepoint_requested_.store(true, std::memory_order_relaxed);
}

void LocalHeap::ClearSafepointRequested() {
  safepoint_requested_.store(false, std::memory_order_relaxed);
}

void LocalHeap::EnterSafepoint() {
  DCHECK_EQ(LocalHeap::Current(), this);
  if (state_ == ThreadState::Running) heap_->safepoint()->EnterFromThread(this);
}

void LocalHeap::FreeLabs() {
  old_space_small_allocator_.FreeLab();
  old_space_medium_allocator_.FreeLab();
  lo_space_allocator_.FreeLab();
}

void LocalHeap::MakeLabsIterable() {
  old_space_small_allocator_.MakeLabIterable();
  old_space_medium_allocator_.MakeLabIterable();
  lo_space_allocator_.MakeLabIterable();
}

bool LocalHeap::AreLabsEmpty() {
  return old_space_small_allocator_.IsLabEmpty() &&
         old_space_medium_allocator_.IsLabEmpty() &&
         lo_space_allocator_.IsLabEmpty();
}

void LocalHeap::StartBlackAllocation() {
  old_space_small_allocator_.StartBlackAllocation();
  old_space_medium_allocator_.StartBlackAllocation();
  lo_space_allocator_.StartBlackAllocation();
}

void LocalHeap::StopBlackAllocation() {
  old_space_small_allocator_.StopBlackAllocation();
  old_space_medium_allocator_.StopBlackAllocation();
  lo_space_allocator_.StopBlackAllocation();
}

void LocalHeap::PerformCollection() {
  ParkedScope scope(this);
  heap_->RequestCollectionBackground(this);
}

Address LocalHeap::PerformCollectionAndAllocateAgain(
    int object_size, AllocationType type, AllocationOrigin origin,
    AllocationAlignment alignment) {
  static const int kMaxNumberOfRetries = 3;

  for (int i = 0; i < kMaxNumberOfRetries; i++) {
    PerformCollection();

    AllocationResult result = AllocateRaw(object_size, type, origin, alignment,
                                          HeapLimitHandling::kIgnoreSoftLimit);
    if (!result.IsFailure()) {
      return result.ToObjectChecked().address();
    }
    // Starting of incremental marking and young GC is not supported yet.
    DCHECK_EQ(result.Failure(), AllocationFailure::kRetryAfterFullGC);
  }

  heap_->FatalProcessOutOfMemory("LocalHeap: allocation failed");
}

}  // namespace internal
}  // namespace v8
