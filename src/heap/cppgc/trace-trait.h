// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_TRACE_TRAIT_H_
#define V8_HEAP_CPPGC_TRACE_TRAIT_H_

#include "include/cppgc/type-traits.h"
#include "src/heap/cppgc/gc-info-table.h"
#include "src/heap/cppgc/heap-page.h"

namespace cppgc {
namespace internal {

inline TraceDescriptor TraceTraitFromInnerAddressImpl::GetTraceDescriptor(
    const void* address) {
  // address is guaranteed to be on a normal page because this is used only for
  // mixins.
  const HeapObjectHeader& header =
      BasePage::FromPayload(address)
          ->ObjectHeaderFromInnerAddress<HeapObjectHeader::AccessMode::kAtomic>(
              address);
  return {header.Payload(),
          GlobalGCInfoTable::GCInfoFromIndex(
              header.GetGCInfoIndex<HeapObjectHeader::AccessMode::kAtomic>())
              .trace};
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_TRACE_TRAIT_H_
