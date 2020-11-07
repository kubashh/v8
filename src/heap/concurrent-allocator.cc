// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/concurrent-allocator.h"

#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/local-heap.h"
#include "src/heap/marking.h"
#include "src/heap/memory-chunk.h"

namespace v8 {
namespace internal {

void StressConcurrentAllocatorTask::RunInternal() {
  Heap* heap = isolate_->heap();
  LocalHeap local_heap(heap, ThreadKind::kBackground);
  UnparkedScope unparked_scope(&local_heap);

  const int kNumIterations = 2000;
  const int kSmallObjectSize = 10 * kTaggedSize;
  const int kMediumObjectSize = 8 * KB;
  const int kLargeObjectSize =
      static_cast<int>(MemoryChunk::kPageSize -
                       MemoryChunkLayout::ObjectStartOffsetInDataPage());

  for (int i = 0; i < kNumIterations; i++) {
    // Isolate tear down started, stop allocation...
    if (heap->gc_state() == Heap::TEAR_DOWN) return;

    Address address = local_heap.AllocateRawOrFail(
        kSmallObjectSize, AllocationType::kOld, AllocationOrigin::kRuntime,
        AllocationAlignment::kWordAligned);
    heap->CreateFillerObjectAtBackground(
        address, kSmallObjectSize, ClearFreedMemoryMode::kDontClearFreedMemory);
    local_heap.Safepoint();

    address = local_heap.AllocateRawOrFail(
        kMediumObjectSize, AllocationType::kOld, AllocationOrigin::kRuntime,
        AllocationAlignment::kWordAligned);
    heap->CreateFillerObjectAtBackground(
        address, kMediumObjectSize,
        ClearFreedMemoryMode::kDontClearFreedMemory);
    local_heap.Safepoint();

    address = local_heap.AllocateRawOrFail(
        kLargeObjectSize, AllocationType::kOld, AllocationOrigin::kRuntime,
        AllocationAlignment::kWordAligned);
    heap->CreateFillerObjectAtBackground(
        address, kLargeObjectSize, ClearFreedMemoryMode::kDontClearFreedMemory);
    local_heap.Safepoint();
  }

  Schedule(isolate_);
}

// static
void StressConcurrentAllocatorTask::Schedule(Isolate* isolate) {
  CHECK(FLAG_local_heaps && FLAG_concurrent_allocation);
  auto task = std::make_unique<StressConcurrentAllocatorTask>(isolate);
  const double kDelayInSeconds = 0.1;
  V8::GetCurrentPlatform()->CallDelayedOnWorkerThread(std::move(task),
                                                      kDelayInSeconds);
}

}  // namespace internal
}  // namespace v8
