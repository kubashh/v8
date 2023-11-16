// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_TRACED_HANDLES_INL_H_
#define V8_HANDLES_TRACED_HANDLES_INL_H_

#include "src/handles/traced-handles.h"
#include "src/heap/heap-write-barrier-inl.h"

namespace v8::internal {

bool TracedHandles::IsCppGCHostOld(CppHeap& cpp_heap, Address host) const {
  DCHECK(host);
  DCHECK(cpp_heap.generational_gc_supported());
  auto* host_ptr = reinterpret_cast<void*>(host);
  auto* page = cppgc::internal::BasePage::FromInnerAddress(&cpp_heap, host_ptr);
  // TracedReference may be created on stack, in which case assume it's young
  // and doesn't need to be remembered, since it'll anyway be scanned.
  return page && !page->ObjectHeaderFromInnerAddress(host_ptr).IsYoung();
}

CppHeap* TracedHandles::GetCppHeapIfUnifiedYoungGC(Isolate* isolate) const {
  // TODO(v8:13475) Consider removing this check when unified-young-gen becomes
  // default.
  if (!v8_flags.cppgc_young_generation) return nullptr;
  auto* cpp_heap = CppHeap::From(isolate->heap()->cpp_heap());
  if (cpp_heap && cpp_heap->generational_gc_supported()) return cpp_heap;
  return nullptr;
}

bool TracedHandles::NeedsToBeRemembered(
    Tagged<Object> object, TracedNode* node, Address* slot,
    GlobalHandleStoreMode store_mode) const {
  DCHECK(!node->has_old_host());
  if (store_mode == GlobalHandleStoreMode::kInitializingStore) {
    // Don't record initializing stores.
    return false;
  }
  if (is_marking_) {
    // If marking is in progress, the marking barrier will be issued later.
    return false;
  }
  auto* cpp_heap = GetCppHeapIfUnifiedYoungGC(isolate_);
  if (!cpp_heap) return false;

  if (!ObjectInYoungGeneration(object)) return false;
  return IsCppGCHostOld(*cpp_heap, reinterpret_cast<Address>(slot));
}

bool TracedHandles::NeedsTrackingInYoungNodes(Tagged<Object> object,
                                              TracedNode* node) {
  return ObjectInYoungGeneration(object);  // && !node->is_in_young_list();
}

TracedNode* TracedNodeBlock::AllocateNode() {
  DCHECK(!IsFull());
  DCHECK_NE(first_free_node_, kInvalidFreeListNodeIndex);
  auto* node = at(first_free_node_);
  first_free_node_ = node->next_free();
  used_++;
  DCHECK(!node->is_in_use());
  return node;
}

TracedNode* TracedHandles::AllocateNode() {
  if (V8_UNLIKELY(!usable_blocks_.empty())) {
    RefillUsableNodeBlocks();
  }
  TracedNodeBlock* block = usable_blocks_.Front();
  auto* node = block->AllocateNode();
  if (V8_UNLIKELY(block->IsFull())) {
    usable_blocks_.Remove(block);
  }
  used_nodes_++;
  return node;
}

Handle<Object> TracedHandles::Create(Address value, Address* slot,
                                     GlobalHandleStoreMode store_mode) {
  Tagged<Object> object(value);
  auto* node = AllocateNode();
  bool needs_young_bit_update = false;
  if (NeedsTrackingInYoungNodes(object, node)) {
    needs_young_bit_update = true;
    auto& block = TracedNodeBlock::From(*node);
    if (block.SetYoung(node->index())) {
      DCHECK(!young_blocks_.ContainsSlow(&block));
      young_blocks_.Add(&block);
    }
  }

  const bool has_old_host = NeedsToBeRemembered(object, node, slot, store_mode);
  bool needs_black_allocation = false;
  if (is_marking_ && store_mode != GlobalHandleStoreMode::kInitializingStore) {
    needs_black_allocation = true;
    WriteBarrier::MarkingFromGlobalHandle(object);
  }
  return node->Publish(object, needs_young_bit_update, needs_black_allocation,
                       has_old_host);
}

}  // namespace v8::internal

#endif  // V8_HANDLES_TRACED_HANDLES_INL_H_
