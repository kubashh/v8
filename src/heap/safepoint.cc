// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/safepoint.h"

#include "src/handles/local-handles.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap.h"

namespace v8 {
namespace internal {

SafepointManager::SafepointManager(Heap* heap)
    : heap_(heap), local_heaps_head_(nullptr), level_(0) {}

void SafepointManager::Start() {
  DCHECK_GE(level_, 0);
  if (++level_ == 1) StopThreads();
}

void SafepointManager::End() {
  DCHECK_GT(level_, 0);
  if (--level_ == 0) ResumeThreads();
}

void SafepointManager::StopThreads() {
  local_heaps_mutex_.Lock();

  barrier_.Arm();

  for (LocalHeap* current = local_heaps_head_; current;
       current = current->next_) {
    current->RequestSafepoint();
  }

  for (LocalHeap* current = local_heaps_head_; current;
       current = current->next_) {
    current->state_mutex_.Lock();

    while (current->state_ == LocalHeap::ThreadState::Running) {
      current->state_change_.Wait(&current->state_mutex_);
    }
  }
}

void SafepointManager::ResumeThreads() {
  for (LocalHeap* current = local_heaps_head_; current;
       current = current->next_) {
    current->state_mutex_.Unlock();
  }

  barrier_.Disarm();

  local_heaps_mutex_.Unlock();
}

void SafepointManager::EnterFromThread(LocalHeap* local_heap) {
  {
    base::MutexGuard guard(&local_heap->state_mutex_);
    local_heap->state_ = LocalHeap::ThreadState::Safepoint;
    local_heap->state_change_.NotifyAll();
  }

  barrier_.Wait();

  {
    base::MutexGuard guard(&local_heap->state_mutex_);
    local_heap->state_ = LocalHeap::ThreadState::Running;
  }
}

void SafepointManager::Barrier::Arm() {
  base::MutexGuard guard(&mutex_);
  CHECK(!armed_);
  armed_ = true;
}

void SafepointManager::Barrier::Disarm() {
  base::MutexGuard guard(&mutex_);
  CHECK(armed_);
  armed_ = false;
  cond_.NotifyAll();
}

void SafepointManager::Barrier::Wait() {
  base::MutexGuard guard(&mutex_);
  while (armed_) {
    cond_.Wait(&mutex_);
  }
}

SafepointScope::SafepointScope(Heap* heap) : safepoint_(heap->safepoint()) {
  safepoint_->Start();
}

SafepointScope::~SafepointScope() { safepoint_->End(); }

void SafepointManager::AddLocalHeap(LocalHeap* local_heap) {
  base::MutexGuard guard(&local_heaps_mutex_);
  if (local_heaps_head_) local_heaps_head_->prev_ = local_heap;
  local_heap->prev_ = nullptr;
  local_heap->next_ = local_heaps_head_;
  local_heaps_head_ = local_heap;
}

void SafepointManager::RemoveLocalHeap(LocalHeap* local_heap) {
  base::MutexGuard guard(&local_heaps_mutex_);
  if (local_heap->next_) local_heap->next_->prev_ = local_heap->prev_;
  if (local_heap->prev_)
    local_heap->prev_->next_ = local_heap->next_;
  else
    local_heaps_head_ = local_heap->next_;
}

bool SafepointManager::ContainsLocalHeap(LocalHeap* local_heap) {
  base::MutexGuard guard(&local_heaps_mutex_);
  LocalHeap* current = local_heaps_head_;

  while (current) {
    if (current == local_heap) return true;
    current = current->next_;
  }

  return false;
}

bool SafepointManager::ContainsAnyLocalHeap() {
  base::MutexGuard guard(&local_heaps_mutex_);
  return local_heaps_head_ != nullptr;
}

void SafepointManager::Iterate(RootVisitor* visitor) {
  DCHECK(IsActive());
  for (LocalHeap* current = local_heaps_head_; current;
       current = current->next_) {
    current->handles()->Iterate(visitor);
  }
}

}  // namespace internal
}  // namespace v8
