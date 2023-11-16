// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/handles/traced-handles.h"

#include <limits>

#include "include/v8-internal.h"
#include "include/v8-traced-handle.h"
#include "src/base/logging.h"
#include "src/base/platform/memory.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/handles/traced-handles-inl.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/objects.h"
#include "src/objects/visitors.h"

namespace v8::internal {

class TracedHandlesImpl;

class TracedNodeBlock;

TracedNode::TracedNode(IndexType index, IndexType next_free_index)
    : next_free_index_(next_free_index), index_(index) {
  static_assert(offsetof(TracedNode, class_id_) ==
                Internals::kTracedNodeClassIdOffset);
  // TracedNode size should stay within 2 words.
  static_assert(sizeof(TracedNode) <= (2 * kSystemPointerSize));
  DCHECK(!is_in_use());
  DCHECK(!is_weak());
  DCHECK(!markbit());
  DCHECK(!has_old_host());
}

// Publishes all internal state to be consumed by other threads.
Handle<Object> TracedNode::Publish(Tagged<Object> object,
                                   bool needs_young_bit_update,
                                   bool needs_black_allocation,
                                   bool has_old_host) {
  DCHECK(!is_in_use());
  DCHECK(!is_weak());
  DCHECK(!markbit());
  set_class_id(0);
  if (needs_black_allocation) {
    set_markbit();
  }
  if (has_old_host) {
    fprintf(stderr, "has old host! %u\n", index());
    set_has_old_host(true);
  }
  set_is_in_use(true);
  reinterpret_cast<std::atomic<Address>*>(&object_)->store(
      object.ptr(), std::memory_order_release);
  return Handle<Object>(&object_);
}

void TracedNode::Release() {
  DCHECK(is_in_use());
  flags_ = kAllFlagsCleared;
  DCHECK(!is_in_use());
  DCHECK(!is_weak());
  DCHECK(!markbit());
  DCHECK(!has_old_host());
  set_raw_object(kGlobalHandleZapValue);
}

TracedNodeBlock::TracedNodeBlock(TracedHandles& traced_handles)
    : traced_handles_(traced_handles) {
  for (TracedNode::IndexType i = 0; i < (kCapacity - 1); i++) {
    new (at(i)) TracedNode(i, i + 1);
  }
  new (at(kCapacity - 1)) TracedNode(kCapacity - 1, kInvalidFreeListNodeIndex);
  CHECK(!HasYoungNodes());
}

// static
TracedNodeBlock& TracedNodeBlock::From(TracedNode& node) {
  TracedNode* first_node = &node - node.index();
  return *reinterpret_cast<TracedNodeBlock*>(
      reinterpret_cast<uint8_t*>(first_node) -
      offsetof(TracedNodeBlock, nodes_));
}

// static
const TracedNodeBlock& TracedNodeBlock::From(const TracedNode& node) {
  return From(const_cast<TracedNode&>(node));
}

void TracedNodeBlock::FreeNode(TracedNode* node) {
  DCHECK(node->is_in_use());
  node->Release();
  DCHECK(!node->is_in_use());
  node->set_next_free(first_free_node_);
  first_free_node_ = node->index();
  if (IsYoung(node->index())) {
    ClearYoung(node->index());
  }
  used_--;
}

CppHeap* GetCppHeapIfUnifiedYoungGC(Isolate* isolate) {
  // TODO(v8:13475) Consider removing this check when unified-young-gen becomes
  // default.
  if (!v8_flags.cppgc_young_generation) return nullptr;
  auto* cpp_heap = CppHeap::From(isolate->heap()->cpp_heap());
  if (cpp_heap && cpp_heap->generational_gc_supported()) return cpp_heap;
  return nullptr;
}

void SetSlotThreadSafe(Address** slot, Address* val) {
  reinterpret_cast<std::atomic<Address*>*>(slot)->store(
      val, std::memory_order_relaxed);
}

void TracedHandles::RefillUsableNodeBlocks() {
  DCHECK(usable_blocks_.empty());
  TracedNodeBlock* block = nullptr;
  if (empty_blocks_.empty()) {
    block = new TracedNodeBlock(*this);
    block_size_bytes_ += block->size_bytes();
  } else {
    block = empty_blocks_.back();
    empty_blocks_.pop_back();
  }
  usable_blocks_.Add(block);
  blocks_.Add(block);
  num_blocks_++;
  DCHECK(block->IsEmpty());
  DCHECK_EQ(usable_blocks_.Front(), block);
  DCHECK(!usable_blocks_.empty());
}

void TracedHandles::FreeNode(TracedNode* node) {
  auto& block = TracedNodeBlock::From(*node);
  if (V8_UNLIKELY(block.IsFull() &&
                  TracedNodeBlock::UsableListTraits::non_empty(&block))) {
    usable_blocks_.Add(&block);
  }
  const bool block_has_young_nodes = block.HasYoungNodes();
  block.FreeNode(node);
  if (block_has_young_nodes && !block.HasYoungNodes()) {
    DCHECK(young_blocks_.ContainsSlow(&block));
    young_blocks_.Remove(&block);
  }
  if (V8_UNLIKELY(block.IsEmpty())) {
    usable_blocks_.Remove(&block);
    blocks_.Remove(&block);
    num_blocks_--;
    empty_blocks_.push_back(&block);
  }
  used_nodes_--;
}

void TracedHandles::Destroy(TracedNodeBlock& node_block, TracedNode& node) {
  DCHECK_IMPLIES(is_marking_, !is_sweeping_on_mutator_thread_);
  DCHECK_IMPLIES(is_sweeping_on_mutator_thread_, !is_marking_);

  // If sweeping on the mutator thread is running then the handle destruction
  // may be a result of a Reset() call from a destructor. The node will be
  // reclaimed on the next cycle.
  //
  // This allows v8::TracedReference::Reset() calls from destructors on
  // objects that may be used from stack and heap.
  if (is_sweeping_on_mutator_thread_) {
    return;
  }

  if (is_marking_) {
    // Incremental/concurrent marking is running. This also covers the scavenge
    // case which prohibits eagerly reclaiming nodes when marking is on during a
    // scavenge.
    //
    // On-heap traced nodes are released in the atomic pause in
    // `IterateWeakRootsForPhantomHandles()` when they are discovered as not
    // marked. Eagerly clear out the object here to avoid needlessly marking it
    // from this point on. The node will be reclaimed on the next cycle.
    node.set_raw_object<AccessMode::ATOMIC>(kNullAddress);
    return;
  }

  // In case marking and sweeping are off, the handle may be freed immediately.
  // Note that this includes also the case when invoking the first pass
  // callbacks during the atomic pause which requires releasing a node fully.
  FreeNode(&node);
}

void TracedHandles::Copy(const TracedNode& from_node, Address** to) {
  DCHECK_NE(kGlobalHandleZapValue, from_node.raw_object());
  Handle<Object> o =
      Create(from_node.raw_object(), reinterpret_cast<Address*>(to),
             GlobalHandleStoreMode::kAssigningStore);
  SetSlotThreadSafe(to, o.location());
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) {
    Object::ObjectVerify(Tagged<Object>(**to), isolate_);
  }
#endif  // VERIFY_HEAP
}

void TracedHandles::Move(TracedNode& from_node, Address** from, Address** to) {
  DCHECK(from_node.is_in_use());

  // Deal with old "to".
  auto* to_node = TracedNode::FromLocation(*to);
  DCHECK_IMPLIES(*to, to_node->is_in_use());
  DCHECK_IMPLIES(*to, kGlobalHandleZapValue != to_node->raw_object());
  DCHECK_NE(kGlobalHandleZapValue, from_node.raw_object());
  if (*to) {
    auto& to_node_block = TracedNodeBlock::From(*to_node);
    Destroy(to_node_block, *to_node);
  }

  // Set "to" to "from".
  SetSlotThreadSafe(to, *from);
  to_node = &from_node;

  // Deal with new "to"
  DCHECK_NOT_NULL(*to);
  DCHECK_EQ(*from, *to);
  if (is_marking_) {
    // Write barrier needs to cover node as well as object.
    to_node->set_markbit<AccessMode::ATOMIC>();
    WriteBarrier::MarkingFromGlobalHandle(to_node->object());
  } else if (auto* cpp_heap = GetCppHeapIfUnifiedYoungGC(isolate_)) {
    const bool object_is_young_and_not_yet_recorded =
        !from_node.has_old_host() &&
        ObjectInYoungGeneration(from_node.object());
    if (object_is_young_and_not_yet_recorded &&
        IsCppGCHostOld(*cpp_heap, reinterpret_cast<Address>(to))) {
      from_node.set_has_old_host(true);
    }
  }
  SetSlotThreadSafe(from, nullptr);
}

void TracedHandles::SetIsMarking(bool value) {
  DCHECK_EQ(is_marking_, !value);
  is_marking_ = value;
}

void TracedHandles::SetIsSweepingOnMutatorThread(bool value) {
  DCHECK_EQ(is_sweeping_on_mutator_thread_, !value);
  is_sweeping_on_mutator_thread_ = value;
}

const TracedHandles::NodeBounds TracedHandles::GetNodeBounds() const {
  TracedHandles::NodeBounds block_bounds;
  block_bounds.reserve(num_blocks_);
  for (const auto* block : blocks_) {
    block_bounds.push_back(
        {block->nodes_begin_address(), block->nodes_end_address()});
  }
  std::sort(block_bounds.begin(), block_bounds.end(),
            [](const auto& pair1, const auto& pair2) {
              return pair1.first < pair2.first;
            });
  return block_bounds;
}

void TracedHandles::DeleteEmptyBlocks() {
  // Keep one node block around for fast allocation/deallocation patterns.
  if (empty_blocks_.size() <= 1) return;

  for (size_t i = 1; i < empty_blocks_.size(); i++) {
    auto* block = empty_blocks_[i];
    DCHECK(block->IsEmpty());
    DCHECK_GE(block_size_bytes_, block->size_bytes());
    block_size_bytes_ -= block->size_bytes();
    delete block;
  }
  empty_blocks_.resize(1);
  empty_blocks_.shrink_to_fit();
}

void TracedHandles::ResetDeadNodes(
    WeakSlotCallbackWithHeap should_reset_handle) {
  // Manual iteration as the block may be deleted in `FreeNode()`.
  for (auto it = blocks_.begin(); it != blocks_.end();) {
    auto* block = *(it++);
    for (auto* node : *block) {
      if (!node->is_in_use()) continue;

      // Detect unreachable nodes first.
      if (!node->markbit()) {
        FreeNode(node);
        continue;
      }

      node->set_has_old_host(false);

      // Node was reachable. Clear the markbit for the next GC.
      node->clear_markbit();
      // TODO(v8:13141): Turn into a DCHECK after some time.
      CHECK(!should_reset_handle(isolate_->heap(), node->location()));
    }
  }
}

void TracedHandles::ResetYoungDeadNodes(
    WeakSlotCallbackWithHeap should_reset_handle) {
  for (auto it = young_blocks_.begin(); it != young_blocks_.end();) {
    auto* block = *(it++);
    for (auto* node : *block) {
      if (!block->IsYoung(node->index())) continue;

      DCHECK_IMPLIES(node->has_old_host(), node->markbit());

      if (!node->is_in_use()) continue;

      if (!node->markbit()) {
        FreeNode(node);
        continue;
      }

      if (ObjectInYoungGeneration(node->object())) {
        node->set_has_old_host(true);
      } else {
        if (block->ClearYoung(node->index())) {
          CHECK(young_blocks_.ContainsSlow(block));
          young_blocks_.Remove(block);
        }
      }

      // Node was reachable. Clear the markbit for the next GC.
      node->clear_markbit();

      // TODO(v8:13141): Turn into a DCHECK after some time.
      CHECK(!should_reset_handle(isolate_->heap(), node->location()));
    }
  }
}

namespace {
void ComputeWeaknessForYoungObject(EmbedderRootsHandler* handler,
                                   TracedNode* node) {
  DCHECK(!node->is_weak());
  bool is_unmodified_api_object =
      JSObject::IsUnmodifiedApiObject(node->location());
  if (is_unmodified_api_object) {
    v8::Value* value = ToApi<v8::Value>(node->handle());
    node->set_weak(!handler->IsRoot(
        *reinterpret_cast<v8::TracedReference<v8::Value>*>(&value)));
  }
}
}  // namespace

void TracedHandles::ComputeWeaknessForYoungObjects() {
  if (!v8_flags.reclaim_unmodified_wrappers) return;

  // Treat all objects as roots during incremental marking to avoid corrupting
  // marking worklists.
  DCHECK_IMPLIES(v8_flags.minor_ms, !is_marking_);
  if (is_marking_) return;

  auto* const handler = isolate_->heap()->GetEmbedderRootsHandler();
  if (!handler) return;

  for (auto* block : young_blocks_) {
    for (auto* node : *block) {
      if (!node->is_in_use()) continue;

      if (!block->IsYoung(node->index())) continue;

      ComputeWeaknessForYoungObject(handler, node);
    }
  }
}

void TracedHandles::ProcessYoungObjects(
    RootVisitor* visitor, WeakSlotCallbackWithHeap should_reset_handle) {
  if (!v8_flags.reclaim_unmodified_wrappers) return;

  auto* const handler = isolate_->heap()->GetEmbedderRootsHandler();
  if (!handler) return;

  // ResetRoot should not trigger allocations in CppGC.
  if (auto* cpp_heap = CppHeap::From(isolate_->heap()->cpp_heap())) {
    cpp_heap->EnterDisallowGCScope();
    cpp_heap->EnterNoGCScope();
  }

  for (auto it = young_blocks_.begin(); it != young_blocks_.end();) {
    auto* block = *(it++);
    for (auto* node : *block) {
      if (!block->IsYoung(node->index())) continue;

      const bool should_reset =
          should_reset_handle(isolate_->heap(), node->location());
      CHECK_IMPLIES(!node->is_weak(), !should_reset);
      if (should_reset) {
        CHECK(!is_marking_);
        v8::Value* value = ToApi<v8::Value>(node->handle());
        handler->ResetRoot(
            *reinterpret_cast<v8::TracedReference<v8::Value>*>(&value));
        // We cannot check whether a node is in use here as the reset behavior
        // depends on whether incremental marking is running when reclaiming
        // young objects.
      } else {
        // Node may still be in the young generation. The C++ object is always
        // promoted so it will have an old host.
        if (ObjectInYoungGeneration(node->object())) {
          node->set_has_old_host(true);
        } else {
          if (block->ClearYoung(node->index())) {
            CHECK(young_blocks_.ContainsSlow(block));
            young_blocks_.Remove(block);
          }
        }
        if (node->is_weak()) {
          node->set_weak(false);
          if (visitor) {
            visitor->VisitRootPointer(Root::kGlobalHandles, nullptr,
                                      node->location());
          }
        }
      }
    }
  }

  if (auto* cpp_heap = CppHeap::From(isolate_->heap()->cpp_heap())) {
    cpp_heap->LeaveNoGCScope();
    cpp_heap->LeaveDisallowGCScope();
  }
}

void TracedHandles::Iterate(RootVisitor* visitor) {
  for (auto* block : blocks_) {
    for (auto* node : *block) {
      if (!node->is_in_use()) continue;

      visitor->VisitRootPointer(Root::kTracedHandles, nullptr,
                                node->location());
    }
  }
}

void TracedHandles::IterateYoung(RootVisitor* visitor) {
  for (auto* block : young_blocks_) {
    for (auto* node : *block) {
      if (!block->IsYoung(node->index())) continue;

      if (!node->is_in_use()) continue;

      visitor->VisitRootPointer(Root::kTracedHandles, nullptr,
                                node->location());
    }
  }
}

void TracedHandles::IterateYoungRoots(RootVisitor* visitor) {
  for (auto* block : young_blocks_) {
    for (auto* node : *block) {
      if (!block->IsYoung(node->index())) continue;

      if (!node->is_in_use()) continue;

      CHECK_IMPLIES(is_marking_, !node->is_weak());

      if (node->is_weak()) continue;

      visitor->VisitRootPointer(Root::kTracedHandles, nullptr,
                                node->location());
    }
  }
}

void TracedHandles::IterateAndMarkYoungRootsWithOldHosts(RootVisitor* visitor) {
  for (auto* block : young_blocks_) {
    for (auto* node : *block) {
      if (!block->IsYoung(node->index())) continue;

      if (!node->is_in_use()) continue;
      if (!node->has_old_host()) continue;

      CHECK_IMPLIES(is_marking_, !node->is_weak());

      if (node->is_weak()) continue;

      node->set_markbit();
      CHECK(ObjectInYoungGeneration(node->object()));
      visitor->VisitRootPointer(Root::kTracedHandles, nullptr,
                                node->location());
    }
  }
}

void TracedHandles::IterateYoungRootsWithOldHostsForTesting(
    RootVisitor* visitor) {
  for (auto* block : young_blocks_) {
    for (auto* node : *block) {
      if (!block->IsYoung(node->index())) continue;
      if (!node->is_in_use()) continue;
      if (!node->has_old_host()) continue;

      CHECK_IMPLIES(is_marking_, !node->is_weak());

      if (node->is_weak()) continue;

      visitor->VisitRootPointer(Root::kTracedHandles, nullptr,
                                node->location());
    }
  }
}

TracedHandles::TracedHandles(Isolate* isolate) : isolate_(isolate) {}

TracedHandles::~TracedHandles() {
  size_t block_size_bytes = 0;
  while (!blocks_.empty()) {
    auto* block = blocks_.Front();
    blocks_.PopFront();
    block_size_bytes += block->size_bytes();
    delete block;
  }
  for (auto* block : empty_blocks_) {
    block_size_bytes += block->size_bytes();
    delete block;
  }
  USE(block_size_bytes);
  DCHECK_EQ(block_size_bytes, block_size_bytes_);
}

// static
void TracedHandles::Destroy(Address* location) {
  if (!location) return;

  auto* node = TracedNode::FromLocation(location);
  auto& node_block = TracedNodeBlock::From(*node);
  auto& traced_handles = node_block.traced_handles();
  traced_handles.Destroy(node_block, *node);
}

// static
void TracedHandles::Copy(const Address* const* from, Address** to) {
  DCHECK_NOT_NULL(*from);
  DCHECK_NULL(*to);

  const TracedNode* from_node = TracedNode::FromLocation(*from);
  const auto& node_block = TracedNodeBlock::From(*from_node);
  auto& traced_handles = node_block.traced_handles();
  traced_handles.Copy(*from_node, to);
}

// static
void TracedHandles::Move(Address** from, Address** to) {
  // Fast path for moving from an empty reference.
  if (!*from) {
    Destroy(*to);
    SetSlotThreadSafe(to, nullptr);
    return;
  }

  TracedNode* from_node = TracedNode::FromLocation(*from);
  auto& node_block = TracedNodeBlock::From(*from_node);
  auto& traced_handles = node_block.traced_handles();
  traced_handles.Move(*from_node, from, to);
}

namespace {
Tagged<Object> MarkObject(Tagged<Object> obj, TracedNode& node,
                          TracedHandles::MarkMode mark_mode) {
  if (mark_mode == TracedHandles::MarkMode::kOnlyYoung &&
      !TracedNodeBlock::From(node).IsYoung(node.index())) {
    return Smi::zero();
  }
  node.set_markbit<AccessMode::ATOMIC>();
  // Being in the young list, the node may still point to an old object, in
  // which case we want to keep the node marked, but not follow the reference.
  if (mark_mode == TracedHandles::MarkMode::kOnlyYoung &&
      !ObjectInYoungGeneration(obj))
    return Smi::zero();
  return obj;
}
}  // namespace

// static
Tagged<Object> TracedHandles::Mark(Address* location, MarkMode mark_mode) {
  // The load synchronizes internal bitfields that are also read atomically
  // from the concurrent marker. The counterpart is `TracedNode::Publish()`.
  Tagged<Object> object =
      Tagged<Object>(reinterpret_cast<std::atomic<Address>*>(location)->load(
          std::memory_order_acquire));
  auto* node = TracedNode::FromLocation(location);
  DCHECK(node->is_in_use<AccessMode::ATOMIC>());
  return MarkObject(object, *node, mark_mode);
}

// static
Tagged<Object> TracedHandles::MarkConservatively(
    Address* inner_location, Address* traced_node_block_base,
    MarkMode mark_mode) {
  // Compute the `TracedNode` address based on its inner pointer.
  const ptrdiff_t delta = reinterpret_cast<uintptr_t>(inner_location) -
                          reinterpret_cast<uintptr_t>(traced_node_block_base);
  const auto index = delta / sizeof(TracedNode);
  TracedNode& node =
      reinterpret_cast<TracedNode*>(traced_node_block_base)[index];
  // `MarkConservatively()` runs concurrently with marking code. Reading
  // state concurrently to setting the markbit is safe.
  if (!node.is_in_use<AccessMode::ATOMIC>()) return Smi::zero();
  return MarkObject(node.object(), node, mark_mode);
}

bool TracedHandles::IsValidInUseNode(Address* location) {
  TracedNode* node = TracedNode::FromLocation(location);
  // This method is called after mark bits have been cleared.
  DCHECK(!node->markbit<AccessMode::NON_ATOMIC>());
  CHECK_IMPLIES(node->is_in_use<AccessMode::NON_ATOMIC>(),
                node->raw_object() != kGlobalHandleZapValue);
  CHECK_IMPLIES(!node->is_in_use<AccessMode::NON_ATOMIC>(),
                node->raw_object() == kGlobalHandleZapValue);
  return node->is_in_use<AccessMode::NON_ATOMIC>();
}

}  // namespace v8::internal
