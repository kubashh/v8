// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_HANDLES_INL_H_
#define V8_HANDLES_HANDLES_INL_H_

#include "src/base/sanitizer/msan.h"
#include "src/execution/isolate.h"
#include "src/execution/local-isolate.h"
#include "src/handles/handles.h"
#include "src/handles/local-handles-inl.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

class LocalHeap;

HandleBase::HandleBase(Address object, Isolate* isolate)
    : location_(HandleScope::CreateHandle(isolate, object)) {}

HandleBase::HandleBase(Address object, LocalIsolate* isolate)
    : location_(LocalHandleScope::GetHandle(isolate->heap(), object)) {}

HandleBase::HandleBase(Address object, LocalHeap* local_heap)
    : location_(LocalHandleScope::GetHandle(local_heap, object)) {}

bool HandleBase::is_identical_to(const HandleBase that) const {
  SLOW_DCHECK((this->location_ == nullptr || this->IsDereferenceAllowed()) &&
              (that.location_ == nullptr || that.IsDereferenceAllowed()));
  if (this->location_ == that.location_) return true;
  if (this->location_ == nullptr || that.location_ == nullptr) return false;
  return Object(*this->location_) == Object(*that.location_);
}

// Allocate a new handle for the object, do not canonicalize.
template <typename T>
Handle<T> Handle<T>::New(T object, Isolate* isolate) {
  return Handle(HandleScope::CreateHandle(isolate, object.ptr()));
}

template <typename T>
template <typename S>
const Handle<T> Handle<T>::cast(Handle<S> that) {
  T::cast(*FullObjectSlot(that.location()));
  return Handle<T>(that.location_);
}

template <typename T>
Handle<T>::Handle(T object, Isolate* isolate)
    : HandleBase(object.ptr(), isolate) {}

template <typename T>
Handle<T>::Handle(T object, LocalIsolate* isolate)
    : HandleBase(object.ptr(), isolate) {}

template <typename T>
Handle<T>::Handle(T object, LocalHeap* local_heap)
    : HandleBase(object.ptr(), local_heap) {}

template <typename T>
V8_INLINE Handle<T> handle(T object, Isolate* isolate) {
  return Handle<T>(object, isolate);
}

template <typename T>
V8_INLINE Handle<T> handle(T object, LocalIsolate* isolate) {
  return Handle<T>(object, isolate);
}

template <typename T>
V8_INLINE Handle<T> handle(T object, LocalHeap* local_heap) {
  return Handle<T>(object, local_heap);
}

template <typename T>
inline std::ostream& operator<<(std::ostream& os, Handle<T> handle) {
  return os << Brief(*handle);
}

HandleScope::HandleScope(Isolate* isolate) {
  HandleScopeData* data = isolate->handle_scope_data();
  isolate_ = isolate;
  prev_top_ = data->top;
  data->top = HandleScopeUtils::OpenHandleScope(data->top);
}

HandleScope::HandleScope(HandleScope&& other) V8_NOEXCEPT
    : isolate_(other.isolate_),
      prev_top_(other.prev_top_) {
  other.isolate_ = nullptr;
}

HandleScope::~HandleScope() {
  if (V8_UNLIKELY(isolate_ == nullptr)) return;
  CloseScope(isolate_, prev_top_);
}

HandleScope& HandleScope::operator=(HandleScope&& other) V8_NOEXCEPT {
  if (isolate_ == nullptr) {
    isolate_ = other.isolate_;
  } else {
    DCHECK_EQ(isolate_, other.isolate_);
    CloseScope(isolate_, prev_top_);
  }
  prev_top_ = other.prev_top_;
  other.isolate_ = nullptr;
  return *this;
}

void HandleScope::CloseScope(Isolate* isolate, Address* prev_top) {
#ifdef DEBUG
  int before = v8_flags.check_handle_count ? NumberOfHandles(isolate) : 0;
#endif
  DCHECK_NOT_NULL(isolate);
  HandleScopeData* current = isolate->handle_scope_data();

  std::swap(current->top, prev_top);
  Address* limit = prev_top;
  if (V8_UNLIKELY(
          HandleScopeUtils::CanDeleteExtensions(prev_top, current->top))) {
    DeleteExtensions(isolate);
    limit = HandleScopeUtils::BlockLimit(current->top);
  }
  HandleScopeUtils::UninitializeMemory(current->top, limit);
#ifdef DEBUG
  int after = v8_flags.check_handle_count ? NumberOfHandles(isolate) : 0;
  DCHECK_LT(after - before, kCheckHandleThreshold);
  DCHECK_LT(before, kCheckHandleThreshold);
#endif
}

template <typename T>
Handle<T> HandleScope::CloseAndEscape(Handle<T> handle_value) {
  HandleScopeData* current = isolate_->handle_scope_data();
  T value = *handle_value;
  // Throw away all handles in the current scope.
  CloseScope(isolate_, prev_top_);
  // Allocate one handle in the parent scope.
  DCHECK(!HandleScopeUtils::IsSealed(current->top));
  Handle<T> result(value, isolate_);
  // Reinitialize the current scope (so that it's ready
  // to be used or closed again).
  prev_top_ = current->top;
  return result;
}

Address* HandleScope::CreateHandle(Isolate* isolate, Address value) {
  DCHECK(AllowHandleAllocation::IsAllowed());
  DCHECK(isolate->main_thread_local_heap()->IsRunning());
  DCHECK_WITH_MSG(isolate->thread_id() == ThreadId::Current(),
                  "main-thread handle can only be created on the main thread.");
  HandleScopeData* data = isolate->handle_scope_data();
  // Set the value, update the current next field, create a new block if
  // necessary and return the result.
  static_assert(sizeof(Address*) == sizeof(Address));
  Address* result = data->top++;
  *result = value;
  if (V8_UNLIKELY(HandleScopeUtils::MayNeedExtend(data->top))) {
    data->top = Extend(isolate);
  }
  return result;
}

#ifdef DEBUG
inline SealHandleScope::SealHandleScope(Isolate* isolate) : isolate_(isolate) {
  // Make sure the current thread is allowed to create handles to begin with.
  DCHECK(AllowHandleAllocation::IsAllowed());
  HandleScopeData* current = isolate_->handle_scope_data();
  prev_top_ = current->top;
  current->top = HandleScopeUtils::SealHandleScope(current->top);
}

inline SealHandleScope::~SealHandleScope() {
  // Restore state in current handle scope to re-enable handle
  // allocations.
  HandleScopeData* current = isolate_->handle_scope_data();
  // Check that no handles were created in the sealed scope.
  DCHECK_EQ(HandleScopeUtils::OpenHandleScope(prev_top_),
            HandleScopeUtils::OpenHandleScope(current->top));
  current->top = prev_top_;
}

#endif

}  // namespace internal
}  // namespace v8

#endif  // V8_HANDLES_HANDLES_INL_H_
