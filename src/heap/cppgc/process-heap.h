// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_PROCESS_HEAP_H_
#define V8_HEAP_CPPGC_PROCESS_HEAP_H_

#include <vector>

#include "src/base/platform/mutex.h"

namespace cppgc {
namespace internal {

class HeapBase;

extern v8::base::LazyMutex g_process_mutex;

constexpr bool kRegistryIsEnabled =
#if defined(DEBUG)
    true;
#else   // !DEBUG
    false;
#endif  // !DEBUG

class DisabledHeapRegistryBase {
 public:
  class Subscription final {
   public:
    explicit Subscription(HeapBase&) {}
  };
};

class EnabledHeapRegistryBase {
 public:
  class Subscription final {
   public:
    inline explicit Subscription(HeapBase&);
    inline ~Subscription();

   private:
    HeapBase& heap_;
  };

  static HeapBase* TryFromManagedPointer(const void* needle);

 private:
  static void RegisterHeap(HeapBase&);
  static void UnregisterHeap(HeapBase&);
};

class HeapRegistry final
    : public std::conditional<kRegistryIsEnabled, EnabledHeapRegistryBase,
                              DisabledHeapRegistryBase>::type {};

EnabledHeapRegistryBase::Subscription::Subscription(HeapBase& heap)
    : heap_(heap) {
  EnabledHeapRegistryBase::RegisterHeap(heap_);
}

EnabledHeapRegistryBase::Subscription::~Subscription() {
  EnabledHeapRegistryBase::UnregisterHeap(heap_);
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_PROCESS_HEAP_H_
