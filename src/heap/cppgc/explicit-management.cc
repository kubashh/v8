// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/explicit-management.h"

#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"

namespace cppgc {
namespace internal {

namespace {

struct ObjectModifier {
  bool can_modify;
  BasePage& page;
};

ObjectModifier CanModifyObject(void* object) {
  // object is guaranteed to be of type GarbageCollected, so getting the
  // BasePage is okay for regular and large objects.
  auto* base_page = BasePage::FromPayload(object);
  auto* heap = base_page->heap();
  // Whenever the GC is active, avoid modifying the object as it may mess with
  // state that the GC needs.
  const bool cannot_modify = heap->in_atomic_pause() || heap->marker() ||
                             heap->sweeper().IsSweepingInProgress();
  return {!cannot_modify, *base_page};
}

}  // namespace

bool TryFree(void* object) {
  auto result = CanModifyObject(object);
  if (!result.can_modify) {
    return false;
  }
  auto& header = HeapObjectHeader::FromPayload(object);
  header.Finalize();

  auto& base_page = result.page;
  if (base_page.is_large()) {
    base_page.space()->RemovePage(&base_page);
    base_page.heap()->stats_collector()->NotifyExplicitFree(
        LargePage::From(&base_page)->PayloadSize());
    LargePage::Destroy(LargePage::From(&base_page));
  } else {
    const size_t header_size = header.GetSize();
    auto* normal_page = NormalPage::From(&base_page);
    auto& normal_space = *static_cast<NormalPageSpace*>(base_page.space());
    auto& lab = normal_space.linear_allocation_buffer();
    if (header.PayloadEnd() == lab.start()) {
      lab.Set(reinterpret_cast<Address>(&header), lab.size() + header_size);
      normal_page->object_start_bitmap().ClearBit(lab.start());
    } else {
      base_page.heap()->stats_collector()->NotifyExplicitFree(header_size);
      normal_space.free_list().Add({&header, header_size});
    }
    SET_MEMORY_INACCESSIBLE(&header, header_size);
  }
  return true;
}

}  // namespace internal
}  // namespace cppgc
