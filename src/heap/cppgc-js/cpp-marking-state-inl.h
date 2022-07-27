// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_JS_CPP_MARKING_STATE_INL_H_
#define V8_HEAP_CPPGC_JS_CPP_MARKING_STATE_INL_H_

#include "src/heap/cppgc-js/cpp-marking-state.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/embedder-tracing-inl.h"
#include "src/objects/embedder-data-slot.h"
#include "src/objects/js-objects.h"

namespace v8 {
namespace internal {

bool CppMarkingState::ExtractEmbedderDataSnapshot(
    Map map, JSObject object, EmbedderDataSnapshot& snapshot) {
  if (JSObject::GetEmbedderFieldCount(map) < 2) return false;

  EmbedderDataSlot::EmbedderDataSlotSnapshot slot_snapshot;
  EmbedderDataSlot::PopulateEmbedderDataSnapshot(
      map, object, wrapper_descriptor_.wrappable_instance_index, slot_snapshot);
  // Check whether snapshot is valid.
  const EmbedderDataSlot instance_slot(slot_snapshot);
  bool valid = instance_slot.ToAlignedPointer(isolate_, &snapshot);
#if defined(CPPGC_CAGED_HEAP)
  // On 64-bit builds we have to check whether the snapshot captured a valid
  // pointer.
  valid &= cppgc::internal::CagedHeapBase::IsWithinCage(snapshot);
  Address low = static_cast<uintptr_t>(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(snapshot)));
  valid &= low != 0;
#endif  // defined(CPPGC_CAGED_HEAP)
  return valid;
}

void CppMarkingState::MarkAndPush(const EmbedderDataSnapshot& snapshot) {
  marking_state_.MarkAndPush(
      cppgc::internal::HeapObjectHeader::FromObject(snapshot));
}

void CppMarkingState::MarkAndPush(cppgc::internal::HeapObjectHeader& header) {
  marking_state_.MarkAndPush(header);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CPPGC_JS_CPP_MARKING_STATE_INL_H_
