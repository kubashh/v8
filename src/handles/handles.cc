// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/handles/handles.h"

#include "src/api/api.h"
#include "src/base/logging.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/execution/isolate.h"
#include "src/execution/thread-id.h"
#include "src/handles/maybe-handles.h"
#include "src/objects/objects-inl.h"
#include "src/roots/roots-inl.h"
#include "src/utils/address-map.h"
#include "src/utils/identity-map.h"

#ifdef V8_ENABLE_MAGLEV
#include "src/maglev/maglev-concurrent-dispatcher.h"
#endif  // V8_ENABLE_MAGLEV

#ifdef DEBUG
// For GetIsolateFromWritableHeapObject.
#include "src/heap/heap-write-barrier-inl.h"
#endif

namespace v8 {
namespace internal {

// Handles should be trivially copyable so that they can be efficiently passed
// by value. If they are not trivially copyable, they cannot be passed in
// registers.
ASSERT_TRIVIALLY_COPYABLE(HandleBase);
ASSERT_TRIVIALLY_COPYABLE(Handle<Object>);
ASSERT_TRIVIALLY_COPYABLE(MaybeHandle<Object>);

#ifdef V8_ENABLE_CONSERVATIVE_STACK_SCANNING

ASSERT_TRIVIALLY_COPYABLE(DirectHandle<Object>);
ASSERT_TRIVIALLY_COPYABLE(DirectMaybeHandle<Object>);

#endif  // V8_ENABLE_CONSERVATIVE_STACK_SCANNING

#ifdef DEBUG
bool HandleBase::IsDereferenceAllowed() const {
  DCHECK_NOT_NULL(location_);
  Object object(*location_);
  if (object.IsSmi()) return true;
  HeapObject heap_object = HeapObject::cast(object);
  if (IsReadOnlyHeapObject(heap_object)) return true;
  Isolate* isolate = GetIsolateFromWritableObject(heap_object);
  RootIndex root_index;
  if (isolate->roots_table().IsRootHandleLocation(location_, &root_index) &&
      RootsTable::IsImmortalImmovable(root_index)) {
    return true;
  }
  if (isolate->IsBuiltinTableHandleLocation(location_)) return true;
  if (!AllowHandleDereference::IsAllowed()) return false;

  // Allocations in the shared heap may be dereferenced by multiple threads.
  if (heap_object.InWritableSharedSpace()) return true;

  // Deref is explicitly allowed from any thread. Used for running internal GC
  // epilogue callbacks in the safepoint after a GC.
  if (AllowHandleDereferenceAllThreads::IsAllowed()) return true;

  LocalHeap* local_heap = isolate->CurrentLocalHeap();

  // Local heap can't access handles when parked
  if (!local_heap->IsHandleDereferenceAllowed()) {
    StdoutStream{} << "Cannot dereference handle owned by "
                   << "non-running local heap\n";
    return false;
  }

  // We are pretty strict with handle dereferences on background threads: A
  // background local heap is only allowed to dereference its own local or
  // persistent handles.
  if (!local_heap->is_main_thread()) {
    // The current thread owns the handle and thus can dereference it.
    return local_heap->ContainsPersistentHandle(location_) ||
           local_heap->ContainsLocalHandle(location_);
  }
  // If LocalHeap::Current() is null, we're on the main thread -- if we were to
  // check main thread HandleScopes here, we should additionally check the
  // main-thread LocalHeap.
  DCHECK_EQ(ThreadId::Current(), isolate->thread_id());

  // TODO(leszeks): Check if the main thread owns this handle.
  return true;
}

#ifdef V8_ENABLE_CONSERVATIVE_STACK_SCANNING

template <typename T>
bool DirectHandle<T>::IsDereferenceAllowed() const {
  DCHECK_NE(obj_, kTaggedNullAddress);
  Object object(obj_);
  if (object.IsSmi()) return true;
  HeapObject heap_object = HeapObject::cast(object);
  if (IsReadOnlyHeapObject(heap_object)) return true;
  Isolate* isolate = GetIsolateFromWritableObject(heap_object);
  if (!AllowHandleDereference::IsAllowed()) return false;

  // Allocations in the shared heap may be dereferenced by multiple threads.
  if (heap_object.InWritableSharedSpace()) return true;

  LocalHeap* local_heap = isolate->CurrentLocalHeap();

  // Local heap can't access handles when parked
  if (!local_heap->IsHandleDereferenceAllowed()) {
    StdoutStream{} << "Cannot dereference handle owned by "
                   << "non-running local heap\n";
    return false;
  }

  // If LocalHeap::Current() is null, we're on the main thread -- if we were to
  // check main thread HandleScopes here, we should additionally check the
  // main-thread LocalHeap.
  DCHECK_EQ(ThreadId::Current(), isolate->thread_id());

  return true;
}

#endif  // V8_ENABLE_CONSERVATIVE_STACK_SCANNING

#endif  // DEBUG

// static
Address* HandleScopeUtils::AllocateBlock() {
  return static_cast<Address*>(
      AlignedAllocWithRetry(kHandleBlockByteSize, kHandleBlockAlignment));
}

// static
void HandleScopeUtils::FreeBlock(Address* block) { AlignedFree(block); }

// static
Address* HandleScopeUtils::AddBlock(Isolate* isolate) {
  HandleScopeImplementer* impl = isolate->handle_scope_implementer();

  Address* result = impl->GetSpareOrNewBlock();
  impl->blocks()->push_back(result);

  return result;
}

// static
void HandleScopeUtils::UninitializeMemory(Address* start, Address* end) {
  start = OpenHandleScope(start);
  end = OpenHandleScope(end);
#ifdef ENABLE_HANDLE_ZAPPING
  ZapRange(start, end);
#endif
  MSAN_ALLOCATED_UNINITIALIZED_MEMORY(
      start, static_cast<size_t>(reinterpret_cast<Address>(end) -
                                 reinterpret_cast<Address>(start)));
}

#ifdef ENABLE_HANDLE_ZAPPING
// static
void HandleScopeUtils::ZapRange(Address* start, Address* end) {
  DCHECK_LE(end - start, kHandleBlockSize);
  for (Address* p = start; p != end; p++) {
    *p = static_cast<Address>(kHandleZapValue);
  }
}
#endif  // ENABLE_HANDLE_ZAPPING

// static
int HandleScope::NumberOfHandles(Isolate* isolate) {
  HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  int n = static_cast<int>(impl->blocks()->size());
  DCHECK_GT(n, 0);
  return ((n - 1) * HandleScopeUtils::kHandleBlockSize) +
         static_cast<int>(
             (isolate->handle_scope_data()->top - impl->blocks()->back()));
}

// static
Address* HandleScope::Extend(Isolate* isolate) {
  HandleScopeData* current = isolate->handle_scope_data();

  DCHECK(HandleScopeUtils::MayNeedExtend(current->top));
  // Make sure there's at least one scope on the stack and that the
  // top of the scope stack isn't a barrier.
  if (!Utils::ApiCheck(!HandleScopeUtils::IsSealed(current->top),
                       "v8::HandleScope::CreateHandle()",
                       "Cannot create a handle without a HandleScope")) {
    return nullptr;
  }
  return HandleScopeUtils::AddBlock(isolate);
}

// static
void HandleScope::DeleteExtensions(Isolate* isolate) {
  HandleScopeData* current = isolate->handle_scope_data();
  Address* limit = HandleScopeUtils::OpenHandleScope(current->top);
  isolate->handle_scope_implementer()->DeleteExtensions(limit);
}

Address HandleScope::current_top_address(Isolate* isolate) {
  return reinterpret_cast<Address>(&isolate->handle_scope_data()->top);
}

}  // namespace internal
}  // namespace v8
