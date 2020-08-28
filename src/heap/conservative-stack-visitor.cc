// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/conservative-stack-visitor.h"

#include "src/execution/isolate-utils-inl.h"
#include "src/heap/large-spaces.h"
#include "src/heap/paged-spaces-inl.h"

namespace v8 {
namespace internal {

ConservativeStackVisitor::ConservativeStackVisitor(Isolate* isolate,
                                                   RootVisitor* delegate)
    : isolate_(isolate), delegate_(delegate) {}

void ConservativeStackVisitor::VisitPointer(const void* pointer) {
  VisitConservativelyIfPointer(pointer);
}

bool ConservativeStackVisitor::CheckPage(Address address, MemoryChunk* page) {
  if (address < page->area_start() || address >= page->area_end()) return false;

  auto base_ptr = page->object_start_bitmap()->FindBasePtr(address);
  if (base_ptr == 0) {
    return false;
  }

  // At this point, base_ptr *must* refer to the valid object. We check if
  // |address| resides inside the object or beyond it in unused memory.
  auto obj = HeapObject::FromAddress(base_ptr);
  auto obj_end = obj.address() + obj.Size();

  if (address >= reinterpret_cast<Address>(obj_end)) {
    // |address| points to unused memory.
    return false;
  }

  // TODO(jakehughes) Pinning is only required for the marking visitor. Other
  // visitors (such as verify visitor) could work without pining. This should
  // be moved to delegate_
  page->SetFlag(BasicMemoryChunk::Flag::PINNED);

  base_ptr += kHeapObjectTag;

  auto root = FullObjectSlot(reinterpret_cast<Address>(&base_ptr));
  delegate_->VisitRootPointer(Root::kHandleScope, nullptr, root);
  DCHECK(root == FullObjectSlot(reinterpret_cast<Address>(&base_ptr)));
  return true;
}

void ConservativeStackVisitor::VisitConservativelyIfPointer(
    const void* pointer) {
  auto address = reinterpret_cast<Address>(pointer);
  if (address > isolate_->heap()->old_space()->top() ||
      address < isolate_->heap()->old_space()->limit()) {
    return;
  }

  for (Page* page : *isolate_->heap()->old_space()) {
    if (CheckPage(address, page)) {
      return;
    }
  }

  for (LargePage* page : *isolate_->heap()->lo_space()) {
    if (address > page->area_start() && address < page->area_end()) {
      auto ptr = page->GetObject().address() + kHeapObjectTag;

      auto root = FullObjectSlot(&ptr);
      delegate_->VisitRootPointer(Root::kHandleScope, nullptr, root);
      DCHECK(root == FullObjectSlot(&ptr));
      return;
    }
  }
}

}  // namespace internal
}  // namespace v8
