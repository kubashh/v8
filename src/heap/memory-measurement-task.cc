// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-measurement-task.h"

#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"

namespace v8 {
namespace internal {

MemoryMeasurementTask::MemoryMeasurementTask(Heap* heap)
    : CancelableTask(heap->isolate()), heap_(heap) {}

void MemoryMeasurementTask::RunInternal() {
  auto time = heap_->MonotonicallyIncreasingTimeInMs() *
              Heap::MillisecondsToNanoseconds;
  auto memory = heap_->OldGenerationSizeOfObjects() +
                heap_->AllocatedExternalMemorySinceMarkCompact();
  heap_->UpdateMajorAllocation(
      std::max<double>(0, memory - heap_->last_M_memory),
      time - heap_->last_M_update_time);
  heap_->last_M_update_time = time;
  heap_->last_M_memory = memory;
  heap_->MembalancerUpdate();
  heap_->PostMemoryMeasurementTask();
}

}  // namespace internal
}  // namespace v8
