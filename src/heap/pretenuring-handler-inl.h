// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_PRETENURING_HANDLER_INL_H_
#define V8_HEAP_PRETENURING_HANDLER_INL_H_

#include "src/heap/heap-inl.h"
#include "src/heap/memory-chunk.h"
#include "src/heap/pretenuring-handler.h"
#include "src/objects/allocation-site-inl.h"

namespace v8 {
namespace internal {

void PretenturingHandler::UpdateAllocationSite(
    Map map, HeapObject object, PretenuringFeedbackMap* pretenuring_feedback) {
  DCHECK_NE(pretenuring_feedback, &global_pretenuring_feedback_);
#ifdef DEBUG
  BasicMemoryChunk* chunk = BasicMemoryChunk::FromHeapObject(object);
  DCHECK_IMPLIES(chunk->IsToPage(),
                 v8_flags.minor_mc ||
                     chunk->IsFlagSet(MemoryChunk::PAGE_NEW_NEW_PROMOTION));
  DCHECK_IMPLIES(!chunk->InYoungGeneration(),
                 chunk->IsFlagSet(MemoryChunk::PAGE_NEW_OLD_PROMOTION));
#endif
  if (!v8_flags.allocation_site_pretenuring ||
      !AllocationSite::CanTrack(map.instance_type())) {
    return;
  }
  AllocationMemento memento_candidate =
      heap_->FindAllocationMemento<Heap::kForGC>(map, object);
  if (memento_candidate.is_null()) return;

  // Entering cached feedback is used in the parallel case. We are not allowed
  // to dereference the allocation site and rather have to postpone all checks
  // till actually merging the data.
  Address key = memento_candidate.GetAllocationSiteUnchecked();
  (*pretenuring_feedback)[AllocationSite::unchecked_cast(Object(key))]++;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_PRETENURING_HANDLER_INL_H_
