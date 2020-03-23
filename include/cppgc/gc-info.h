// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_GC_INFO_H_
#define INCLUDE_CPPGC_GC_INFO_H_

#include <stdint.h>

#include <atomic>

#include "include/cppgc/finalizer-trait.h"
#include "include/v8config.h"

namespace cppgc {
namespace internal {

using GCInfoIndex = uint16_t;

// GCInfo contains metadata for objects that are instantiated from classes that
// inherit from GarbageCollected.
struct GCInfo final {
  const FinalizationCallback finalize;
  const bool has_v_table;
};

class V8_EXPORT GCInfoTableProxy final {
 public:
  static GCInfoIndex EnsureGCInfoIndex(const GCInfo&,
                                       std::atomic<GCInfoIndex>*);
};

// Trait determines how the garbage collector treats objects wrt. to traversing,
// finalization, and naming.
template <typename T>
struct GCInfoTrait {
  static GCInfoIndex Index() {
    static_assert(sizeof(T), "T must be fully defined");
    static const GCInfo kGCInfo = {FinalizerTrait<T>::kCallback,
                                   std::is_polymorphic<T>::value};
    // This is more complicated than using threadsafe initialization, but this
    // is instantiated many times (once for every GC type).
    static std::atomic<GCInfoIndex> gc_info_index{0};
    GCInfoIndex index = gc_info_index.load(std::memory_order_acquire);
    if (!index) {
      index = GCInfoTableProxy::EnsureGCInfoIndex(kGCInfo, &gc_info_index);
    }
    return index;
  }
};

}  // namespace internal
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_GC_INFO_H_
