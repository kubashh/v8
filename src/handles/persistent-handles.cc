// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/handles/persistent-handles.h"

#include "src/api/api.h"
#include "src/heap/heap-inl.h"
#include "src/heap/safepoint.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

PersistentHandles::PersistentHandles(Isolate* isolate)
    : isolate_(isolate), block_top_(nullptr), prev_(nullptr), next_(nullptr) {
  isolate->persistent_handles_list()->Add(this);
}

PersistentHandles::~PersistentHandles() {
  isolate_->persistent_handles_list()->Remove(this);

  for (Address* block_start : blocks_) {
#if ENABLE_HANDLE_ZAPPING
    HandleScopeUtils::ZapRange(block_start, block_start + kHandleBlockSize);
#endif
    HandleScopeUtils::FreeBlock(block_start);
  }
}

#ifdef DEBUG
void PersistentHandles::Attach(LocalHeap* local_heap) {
  DCHECK_NULL(owner_);
  owner_ = local_heap;
}

void PersistentHandles::Detach() {
  DCHECK_NOT_NULL(owner_);
  owner_ = nullptr;
}

void PersistentHandles::CheckOwnerIsNotParked() {
  if (owner_) DCHECK(!owner_->IsParked());
}

bool PersistentHandles::Contains(Address* location) {
  auto it = ordered_blocks_.upper_bound(location);
  if (it == ordered_blocks_.begin()) return false;
  --it;
  DCHECK_LE(*it, location);
  if (*it == blocks_.back()) {
    // The last block is a special case because it may have
    // less than block_size_ handles.
    return location < block_top_;
  }
  return location < *it + kHandleBlockSize;
}
#endif

void PersistentHandles::AddBlock() {
  DCHECK(HandleScopeUtils::MayNeedExtend(block_top_));

  Address* block_start = HandleScopeUtils::AllocateBlock();
  blocks_.push_back(block_start);

  block_top_ = block_start;

#ifdef DEBUG
  ordered_blocks_.insert(block_start);
#endif
}

Address* PersistentHandles::GetHandle(Address value) {
  if (V8_UNLIKELY(HandleScopeUtils::MayNeedExtend(block_top_))) {
    AddBlock();
  }

  *block_top_ = value;
  return block_top_++;
}

void PersistentHandles::Iterate(RootVisitor* visitor) {
  for (int i = 0; i < static_cast<int>(blocks_.size()) - 1; i++) {
    Address* block_start = blocks_[i];
    Address* block_end = block_start + kHandleBlockSize;
    visitor->VisitRootPointers(Root::kHandleScope, nullptr,
                               FullObjectSlot(block_start),
                               FullObjectSlot(block_end));
  }

  if (!blocks_.empty()) {
    Address* block_start = blocks_.back();
    DCHECK(!HandleScopeUtils::IsSealed(block_top_));
    visitor->VisitRootPointers(Root::kHandleScope, nullptr,
                               FullObjectSlot(block_start),
                               FullObjectSlot(block_top_));
  }
}

void PersistentHandlesList::Add(PersistentHandles* persistent_handles) {
  base::MutexGuard guard(&persistent_handles_mutex_);
  if (persistent_handles_head_)
    persistent_handles_head_->prev_ = persistent_handles;
  persistent_handles->prev_ = nullptr;
  persistent_handles->next_ = persistent_handles_head_;
  persistent_handles_head_ = persistent_handles;
}

void PersistentHandlesList::Remove(PersistentHandles* persistent_handles) {
  base::MutexGuard guard(&persistent_handles_mutex_);
  if (persistent_handles->next_)
    persistent_handles->next_->prev_ = persistent_handles->prev_;
  if (persistent_handles->prev_)
    persistent_handles->prev_->next_ = persistent_handles->next_;
  else
    persistent_handles_head_ = persistent_handles->next_;
}

void PersistentHandlesList::Iterate(RootVisitor* visitor, Isolate* isolate) {
  isolate->heap()->safepoint()->AssertActive();
  base::MutexGuard guard(&persistent_handles_mutex_);
  for (PersistentHandles* current = persistent_handles_head_; current;
       current = current->next_) {
    current->Iterate(visitor);
  }
}

PersistentHandlesScope::PersistentHandlesScope(Isolate* isolate)
    : impl_(isolate->handle_scope_implementer()) {
  impl_->BeginDeferredScope();
  HandleScopeData* data = impl_->isolate()->handle_scope_data();
  DCHECK(!impl_->blocks()->empty());
  prev_top_ = data->top;
  // Set top to limit to force allocation of a new block for the next handle.
  data->top = HandleScopeUtils::BlockLimit(data->top);
}

PersistentHandlesScope::~PersistentHandlesScope() { DCHECK(handles_detached_); }

std::unique_ptr<PersistentHandles> PersistentHandlesScope::Detach() {
  std::unique_ptr<PersistentHandles> ph = impl_->DetachPersistent();
  HandleScopeData* data = impl_->isolate()->handle_scope_data();
  data->top = prev_top_;
#ifdef DEBUG
  handles_detached_ = true;
#endif
  return ph;
}

// static
bool PersistentHandlesScope::IsActive(Isolate* isolate) {
  return isolate->handle_scope_implementer()
             ->last_handle_before_deferred_block_ != nullptr;
}

}  // namespace internal
}  // namespace v8
