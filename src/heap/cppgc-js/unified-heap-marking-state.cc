// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc-js/unified-heap-marking-state.h"

#include "src/base/logging.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/heap/heap-inl.h"
#include "src/heap/mark-compact.h"

namespace v8 {
namespace internal {

UnifiedHeapMarkingState::UnifiedHeapMarkingState(
    Heap* heap, MarkingWorklists::Local* local_marking_worklist,
    WeakTracedReferenceWorklist::Local& local_weak_traced_reference_worklist,
    cppgc::internal::CollectionType collection_type)
    : heap_(heap),
      has_shared_space_(heap && heap->isolate()->has_shared_space()),
      is_shared_space_isolate_(heap &&
                               heap->isolate()->is_shared_space_isolate()),
      track_retaining_path_(v8_flags.track_retaining_path),
      should_reclaim_weak_nodes_(
          v8_flags.reclaim_unmodified_wrappers &&
          (!v8_flags.reclaim_unmodified_wrappers_only_on_memory_reducing_gcs ||
           (heap && heap_->ShouldReduceMemory() &&
            (heap_->tracer()->GetCurrentCollector() ==
             GarbageCollector::MARK_COMPACTOR)))),
      mark_mode_(collection_type == cppgc::internal::CollectionType::kMinor
                     ? TracedHandles::MarkMode::kOnlyYoung
                     : TracedHandles::MarkMode::kAll),
      marking_state_(heap_ ? heap_->marking_state() : nullptr),
      local_marking_worklist_(local_marking_worklist),
      local_weak_traced_reference_worklist_(
          local_weak_traced_reference_worklist),
      embedder_root_handler_(heap ? heap->GetEmbedderRootsHandler() : nullptr) {
  DCHECK_IMPLIES(v8_flags.track_retaining_path,
                 !v8_flags.concurrent_marking && !v8_flags.parallel_marking);
  DCHECK_IMPLIES(heap_, marking_state_);
}

void UnifiedHeapMarkingState::Update(
    MarkingWorklists::Local* local_marking_worklist) {
  local_marking_worklist_ = local_marking_worklist;
  DCHECK_NOT_NULL(heap_);
}

}  // namespace internal
}  // namespace v8
