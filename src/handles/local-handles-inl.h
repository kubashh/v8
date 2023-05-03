// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_LOCAL_HANDLES_INL_H_
#define V8_HANDLES_LOCAL_HANDLES_INL_H_

#include "src/base/sanitizer/msan.h"
#include "src/execution/isolate.h"
#include "src/execution/local-isolate.h"
#include "src/handles/local-handles.h"

namespace v8 {
namespace internal {

// static
V8_INLINE Address* LocalHandleScope::GetHandle(LocalHeap* local_heap,
                                               Address value) {
  DCHECK(local_heap->IsRunning());
  if (local_heap->is_main_thread())
    return LocalHandleScope::GetMainThreadHandle(local_heap, value);

  LocalHandles* handles = local_heap->handles();
  static_assert(sizeof(Address*) == sizeof(Address));
  Address* result = handles->scope_.top++;
  *result = value;
  if (V8_UNLIKELY(HandleScopeUtils::MayNeedExtend(handles->scope_.top))) {
    handles->scope_.top = handles->AddBlock();
  }
  return result;
}

LocalHandleScope::LocalHandleScope(LocalIsolate* local_isolate)
    : LocalHandleScope(local_isolate->heap()) {}

LocalHandleScope::LocalHandleScope(LocalHeap* local_heap) {
  DCHECK(local_heap->IsRunning());

  if (local_heap->is_main_thread()) {
    OpenMainThreadScope(local_heap);
  } else {
    LocalHandles* handles = local_heap->handles();
    local_heap_ = local_heap;
    prev_top_ = handles->scope_.top;
    handles->scope_.top =
        HandleScopeUtils::OpenHandleScope(handles->scope_.top);
  }
}

LocalHandleScope::~LocalHandleScope() {
  if (local_heap_->is_main_thread()) {
    CloseMainThreadScope(local_heap_, prev_top_);
  } else {
    CloseScope(local_heap_, prev_top_);
  }
}

template <typename T>
Handle<T> LocalHandleScope::CloseAndEscape(Handle<T> handle_value) {
  HandleScopeData* current;
  T value = *handle_value;
  // Throw away all handles in the current scope.
  if (local_heap_->is_main_thread()) {
    current = local_heap_->heap()->isolate()->handle_scope_data();
    CloseMainThreadScope(local_heap_, prev_top_);
  } else {
    current = &local_heap_->handles()->scope_;
    CloseScope(local_heap_, prev_top_);
  }
  // Allocate one handle in the parent scope.
  DCHECK(!HandleScopeUtils::IsSealed(current->top));
  Handle<T> result(value, local_heap_);
  // Reinitialize the current scope (so that it's ready
  // to be used or closed again).
  prev_top_ = current->top;
  return result;
}

void LocalHandleScope::CloseScope(LocalHeap* local_heap, Address* prev_top) {
  LocalHandles* handles = local_heap->handles();

  if (HandleScopeUtils::CanDeleteExtensions(handles->scope_.top, prev_top)) {
    handles->RemoveUnusedBlocks();
  }

  handles->scope_.top = prev_top;

  Address* limit = HandleScopeUtils::BlockLimit(handles->scope_.top);
  HandleScopeUtils::UninitializeMemory(handles->scope_.top, limit);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HANDLES_LOCAL_HANDLES_INL_H_
