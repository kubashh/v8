// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_MARKER_UTILS_H_
#define V8_HEAP_CPPGC_MARKER_UTILS_H_

#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/visitor.h"

namespace cppgc {
namespace internal {

template <size_t kDeadlineCheckInterval, typename WorklistLocal,
          typename Callback, typename Predicate>
bool DrainWorklistWithPredicate(Predicate should_yield,
                                WorklistLocal& worklist_local,
                                Callback callback) {
  if (worklist_local.IsLocalAndGlobalEmpty()) return true;
  // For concurrent markers, should_yield also reports marked bytes.
  if (should_yield()) return false;
  size_t processed_callback_count = kDeadlineCheckInterval;
  typename WorklistLocal::ItemType item;
  while (worklist_local.Pop(&item)) {
    callback(item);
    if (--processed_callback_count == 0) {
      if (should_yield()) {
        return false;
      }
      processed_callback_count = kDeadlineCheckInterval;
    }
  }
  return true;
}

template <HeapObjectHeader::AccessMode mode>
void TraceMarkedObject(Visitor* visitor, const HeapObjectHeader* header) {
  DCHECK(header);
  DCHECK(!header->IsInConstruction<mode>());
  DCHECK(header->IsMarked<mode>());
  const GCInfo& gcinfo =
      GlobalGCInfoTable::GCInfoFromIndex(header->GetGCInfoIndex<mode>());
  gcinfo.trace(visitor, header->Payload());
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_MARKER_UTILS_H_
