// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_STATE_INL_H_
#define V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_STATE_INL_H_

#include <atomic>

#include "include/v8-traced-handle.h"
#include "src/base/logging.h"
#include "src/handles/traced-handles.h"
#include "src/heap/cppgc-js/unified-heap-marking-state.h"
#include "src/heap/heap.h"
#include "src/heap/mark-compact.h"
#include "src/heap/marking-state-inl.h"
#include "src/heap/marking-worklist-inl.h"
#include "src/objects/js-objects.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

class BasicTracedReferenceExtractor final {
 public:
  static Address* GetObjectSlotForMarking(const TracedReferenceBase& ref) {
    return const_cast<Address*>(
        reinterpret_cast<const Address*>(ref.GetSlotThreadSafe()));
  }
};

template <UnifiedHeapMarkingState::MarkedNodeHandling marked_node_handling>
void UnifiedHeapMarkingState::MarkAndPush(
    const TracedReferenceBase& reference) {
  // The following code will crash with null pointer derefs when finding a
  // non-empty `TracedReferenceBase` when `CppHeap` is in detached mode.
  Address* traced_handle_location =
      BasicTracedReferenceExtractor::GetObjectSlotForMarking(reference);
  // We cannot assume that the reference is non-null as we may get here by
  // tracing an ephemeron which doesn't have early bailouts, see
  // `cppgc::Visitor::TraceEphemeron()` for non-Member values.
  if (!traced_handle_location) {
    return;
  }

  Tagged<Object> object;
  bool was_marked;
  std::tie(object, was_marked) =
      TracedHandles::TryMark(traced_handle_location, mark_mode_);
  if (!IsHeapObject(object)) {
    // The embedder is not aware of whether numbers are materialized as heap
    // objects are just passed around as Smis.
    return;
  }
  if constexpr (marked_node_handling == MarkedNodeHandling::kBailout) {
    if (!was_marked) return;
  } else {
    // marked_node_handling == MarkedNodeHandling::kVerify
    DCHECK(!was_marked);
  }

  Tagged<HeapObject> heap_object = HeapObject::cast(object);
  if (heap_object.InReadOnlySpace()) return;
  if (!ShouldMarkObject(heap_object)) return;

  if (should_reclaim_weak_nodes_) {
    bool is_in_atomic_pause =
        heap_->mark_compact_collector()->is_in_atomic_pause();
    if (TracedHandles::IsWeak(
            traced_handle_location, embedder_root_handler_,
            is_in_atomic_pause
                ? TracedHandles::WeaknessCompuationMode::kAtomic
                : TracedHandles::WeaknessCompuationMode::kConcurrent)) {
      if (!is_in_atomic_pause)
        local_weak_traced_reference_worklist_.Push(&reference);
      return;
    }
  }

  if (marking_state_->TryMark(heap_object)) {
    local_marking_worklist_->Push(heap_object);
  }
  if (V8_UNLIKELY(track_retaining_path_)) {
    heap_->AddRetainingRoot(Root::kTracedHandles, heap_object);
  }
}

bool UnifiedHeapMarkingState::ShouldMarkObject(
    Tagged<HeapObject> object) const {
  // Keep up-to-date with MarkCompactCollector::ShouldMarkObject.
  if (V8_LIKELY(!has_shared_space_)) return true;
  if (is_shared_space_isolate_) return true;
  return !object.InAnySharedSpace();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_STATE_INL_H_
