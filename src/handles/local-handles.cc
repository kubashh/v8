// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/handles/local-handles.h"

#include "src/api/api.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/handles/handles.h"
#include "src/heap/heap-inl.h"

namespace v8 {
namespace internal {

Address* LocalHandleScope::GetMainThreadHandle(LocalHeap* local_heap,
                                               Address value) {
  Isolate* isolate = local_heap->heap()->isolate();
  return HandleScope::CreateHandle(isolate, value);
}

void LocalHandleScope::OpenMainThreadScope(LocalHeap* local_heap) {
  Isolate* isolate = local_heap->heap()->isolate();
  HandleScopeData* data = isolate->handle_scope_data();
  local_heap_ = local_heap;
  prev_top_ = data->top;
  data->top = HandleScopeUtils::OpenHandleScope(data->top);
}

void LocalHandleScope::CloseMainThreadScope(LocalHeap* local_heap,
                                            Address* prev_top) {
  Isolate* isolate = local_heap->heap()->isolate();
  HandleScope::CloseScope(isolate, prev_top);
}

LocalHandles::LocalHandles() { AddBlock(); }
LocalHandles::~LocalHandles() {
  RemoveAllBlocks();
  DCHECK(blocks_.empty());
}

void LocalHandles::Iterate(RootVisitor* visitor) {
  for (int i = 0; i < static_cast<int>(blocks_.size()) - 1; i++) {
    Address* block = blocks_[i];
    visitor->VisitRootPointers(Root::kHandleScope, nullptr,
                               FullObjectSlot(block),
                               FullObjectSlot(&block[kHandleBlockSize]));
  }

  if (!blocks_.empty()) {
    Address* block = blocks_.back();
    visitor->VisitRootPointers(
        Root::kHandleScope, nullptr, FullObjectSlot(block),
        FullObjectSlot(HandleScopeUtils::OpenHandleScope(scope_.top)));
  }
}

#ifdef DEBUG
bool LocalHandles::Contains(Address* location) {
  // We have to search in all blocks since they have no guarantee of order.
  for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
    Address* lower_bound = *it;
    // The last block is a special case because it may have less than
    // block_size_ handles.
    Address* upper_bound = lower_bound != blocks_.back()
                               ? lower_bound + kHandleBlockSize
                               : scope_.top;
    if (lower_bound <= location && location < upper_bound) {
      return true;
    }
  }
  return false;
}
#endif

Address* LocalHandles::AddBlock() {
  Address* block = HandleScopeUtils::AllocateBlock();
  blocks_.push_back(block);
  scope_.top = block;
  return block;
}

void LocalHandles::RemoveAllBlocks() {
  while (!blocks_.empty()) {
    Address* block_start = blocks_.back();

    blocks_.pop_back();

#ifdef ENABLE_HANDLE_ZAPPING
    Address* block_limit = block_start + kHandleBlockSize;
    HandleScopeUtils::ZapRange(block_start, block_limit);
#endif

    HandleScopeUtils::FreeBlock(block_start);
  }
}

void LocalHandles::RemoveUnusedBlocks() {
  Address* current_block_start = HandleScopeUtils::BlockStart(scope_.top);
  while (!blocks_.empty()) {
    Address* block_start = blocks_.back();

    if (block_start == current_block_start) {
      break;
    }

    blocks_.pop_back();

#ifdef ENABLE_HANDLE_ZAPPING
    Address* block_limit = block_start + kHandleBlockSize;
    HandleScopeUtils::ZapRange(block_start, block_limit);
#endif

    HandleScopeUtils::FreeBlock(block_start);
  }
}

}  // namespace internal
}  // namespace v8
