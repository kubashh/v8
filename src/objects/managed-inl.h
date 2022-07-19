// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_MANAGED_INL_H_
#define V8_OBJECTS_MANAGED_INL_H_

#include "src/handles/global-handles-inl.h"
#include "src/objects/managed.h"

namespace v8 {
namespace internal {

// static
template <class CppType, ExternalPointerTag tag>
template <typename... Args>
Handle<Managed<CppType, tag>> Managed<CppType, tag>::Allocate(
    Isolate* isolate, size_t estimated_size, Args&&... args) {
  return FromSharedPtr(isolate, estimated_size,
                       std::make_shared<CppType>(std::forward<Args>(args)...));
}

// static
template <class CppType, ExternalPointerTag tag>
Handle<Managed<CppType, tag>> Managed<CppType, tag>::FromRawPtr(
    Isolate* isolate, size_t estimated_size, CppType* ptr) {
  return FromSharedPtr(isolate, estimated_size, std::shared_ptr<CppType>{ptr});
}

// static
template <class CppType, ExternalPointerTag tag>
Handle<Managed<CppType, tag>> Managed<CppType, tag>::FromUniquePtr(
    Isolate* isolate, size_t estimated_size,
    std::unique_ptr<CppType> unique_ptr) {
  return FromSharedPtr(isolate, estimated_size, std::move(unique_ptr));
}

// static
template <class CppType, ExternalPointerTag tag>
Handle<Managed<CppType, tag>> Managed<CppType, tag>::FromSharedPtr(
    Isolate* isolate, size_t estimated_size,
    std::shared_ptr<CppType> shared_ptr) {
  reinterpret_cast<v8::Isolate*>(isolate)
      ->AdjustAmountOfExternalAllocatedMemory(estimated_size);
  auto destructor = new ManagedPtrDestructor(
      estimated_size, new std::shared_ptr<CppType>{std::move(shared_ptr)},
      Destructor);
  Handle<Managed<CppType, tag>> handle =
      Handle<Managed<CppType, tag>>::cast(isolate->factory()->NewForeign<tag>(
          reinterpret_cast<Address>(destructor)));
  Handle<Object> global_handle = isolate->global_handles()->Create(*handle);
  destructor->global_handle_location_ = global_handle.location();
  GlobalHandles::MakeWeak(destructor->global_handle_location_, destructor,
                          &ManagedObjectFinalizer,
                          v8::WeakCallbackType::kParameter);
  isolate->RegisterManagedPtrDestructor(destructor);
  return handle;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_MANAGED_INL_H_
