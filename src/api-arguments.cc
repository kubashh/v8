// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api-arguments.h"

#include "src/api-arguments-inl.h"

namespace v8 {
namespace internal {

PropertyCallbackArguments::PropertyCallbackArguments(Isolate* isolate,
                                                     Object* data, Object* self,
                                                     JSObject* holder,
                                                     ShouldThrow should_throw)
    : Super(isolate) {
  ObjectSlot values = this->begin();
  values.store(T::kThisIndex, self);
  values.store(T::kHolderIndex, holder);
  values.store(T::kDataIndex, data);
  values.store(T::kIsolateIndex, reinterpret_cast<Object*>(isolate));
  values.store(T::kShouldThrowOnErrorIndex,
               Smi::FromInt(should_throw == kThrowOnError ? 1 : 0));

  // Here the hole is set as default value.
  // It cannot escape into js as it's removed in Call below.
  HeapObject* the_hole = ReadOnlyRoots(isolate).the_hole_value();
  values.store(T::kReturnValueDefaultValueIndex, the_hole);
  values.store(T::kReturnValueIndex, the_hole);
  DCHECK(values[T::kHolderIndex]->IsHeapObject());
  DCHECK(values[T::kIsolateIndex]->IsSmi());
}

FunctionCallbackArguments::FunctionCallbackArguments(
    internal::Isolate* isolate, internal::Object* data,
    internal::HeapObject* callee, internal::Object* holder,
    internal::HeapObject* new_target, internal::Address* argv, int argc)
    : Super(isolate), argv_(argv), argc_(argc) {
  ObjectSlot values = begin();
  values.store(T::kDataIndex, data);
  values.store(T::kHolderIndex, holder);
  values.store(T::kNewTargetIndex, new_target);
  values.store(T::kIsolateIndex, reinterpret_cast<internal::Object*>(isolate));
  // Here the hole is set as default value.
  // It cannot escape into js as it's remove in Call below.
  HeapObject* the_hole = ReadOnlyRoots(isolate).the_hole_value();
  values.store(T::kReturnValueDefaultValueIndex, the_hole);
  values.store(T::kReturnValueIndex, the_hole);
  DCHECK(values[T::kHolderIndex]->IsHeapObject());
  DCHECK(values[T::kIsolateIndex]->IsSmi());
}

}  // namespace internal
}  // namespace v8
