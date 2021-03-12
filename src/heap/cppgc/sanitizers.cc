// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/sanitizers.h"

#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/heap-space.h"

namespace cppgc {
namespace internal {

#ifdef V8_USE_ADDRESS_SANITIZER

void AsanAllowAccessToContiguousContainer(const void* payload) {
  const auto* base_page = BasePage::FromPayload(payload);
  const auto* base_space = base_page->is_large()
                               ? LargePage::From(base_page)->original_space()
                               : NormalPage::From(base_page)->space();
  if (base_space->needs_asan_contiguous_container_annotations()) {
    const size_t object_size =
        base_page->is_large()
            ? LargePage::From(base_page)->ObjectSize()
            : HeapObjectHeader::FromPayload(payload).ObjectSize();
    ASAN_ANNOTATE_CONTIGUOUS_CONTAINER(reinterpret_cast<ConstAddress>(payload),
                                       object_size, 0, object_size);
  }
}

#endif  // V8_USE_ADDRESS_SANITIZER

}  // namespace internal
}  // namespace cppgc
