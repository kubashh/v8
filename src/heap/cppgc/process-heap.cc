// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/process-heap.h"

#include <algorithm>

#include "src/base/lazy-instance.h"
#include "src/base/platform/mutex.h"
#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/page-memory.h"

namespace cppgc {
namespace internal {

v8::base::LazyMutex g_process_mutex = LAZY_MUTEX_INITIALIZER;

v8::base::LazyInstance<std::vector<HeapBase*>>::type heap_registry =
    LAZY_INSTANCE_INITIALIZER;

// static
void EnabledHeapRegistryBase::RegisterHeap(HeapBase& heap) {
  v8::base::MutexGuard guard(g_process_mutex.Pointer());

  DCHECK_EQ(heap_registry.Pointer()->end(),
            std::find(heap_registry.Pointer()->begin(),
                      heap_registry.Pointer()->end(), &heap));
  heap_registry.Pointer()->push_back(&heap);
}

// static
void EnabledHeapRegistryBase::UnregisterHeap(HeapBase& heap) {
  v8::base::MutexGuard guard(g_process_mutex.Pointer());

  const auto pos = std::find(heap_registry.Pointer()->begin(),
                             heap_registry.Pointer()->end(), &heap);
  DCHECK_NE(heap_registry.Pointer()->end(), pos);
  heap_registry.Pointer()->erase(pos);
}

// static
HeapBase* EnabledHeapRegistryBase::TryFromManagedPointer(const void* needle) {
  v8::base::MutexGuard guard(g_process_mutex.Pointer());

  for (auto* heap : *heap_registry.Pointer()) {
    const auto address =
        heap->page_backend()->Lookup(reinterpret_cast<ConstAddress>(needle));
    if (address) return heap;
  }
  return nullptr;
}

}  // namespace internal
}  // namespace cppgc
