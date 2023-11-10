// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_TRACED_HANDLES_H_
#define V8_HANDLES_TRACED_HANDLES_H_

#include "include/v8-embedder-heap.h"
#include "include/v8-traced-handle.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/objects/objects.h"
#include "src/objects/visitors.h"

namespace v8::internal {

class CppHeap;
class Isolate;
class TracedHandles;

template <typename T, typename NodeAccessor>
class DoublyLinkedList final {
  template <typename U>
  class IteratorImpl final
      : public base::iterator<std::forward_iterator_tag, U> {
   public:
    explicit IteratorImpl(U* object) : object_(object) {}
    IteratorImpl(const IteratorImpl& other) V8_NOEXCEPT
        : object_(other.object_) {}
    U* operator*() { return object_; }
    bool operator==(const IteratorImpl& rhs) const {
      return rhs.object_ == object_;
    }
    bool operator!=(const IteratorImpl& rhs) const { return !(*this == rhs); }
    inline IteratorImpl& operator++() {
      object_ = ListNodeFor(object_)->next;
      return *this;
    }
    inline IteratorImpl operator++(int) {
      IteratorImpl tmp(*this);
      operator++();
      return tmp;
    }

   private:
    U* object_;
  };

 public:
  using Iterator = IteratorImpl<T>;
  using ConstIterator = IteratorImpl<const T>;

  struct ListNode {
    T* prev = nullptr;
    T* next = nullptr;
  };

  T* Front() { return front_; }

  void PushFront(T* object) {
    DCHECK(!Contains(object));
    ListNodeFor(object)->next = front_;
    if (front_) {
      ListNodeFor(front_)->prev = object;
    }
    front_ = object;
    size_++;
  }

  void PopFront() {
    DCHECK(!Empty());
    if (ListNodeFor(front_)->next) {
      ListNodeFor(ListNodeFor(front_)->next)->prev = nullptr;
    }
    front_ = ListNodeFor(front_)->next;
    size_--;
  }

  void Remove(T* object) {
    DCHECK(Contains(object));
    auto& next_object = ListNodeFor(object)->next;
    auto& prev_object = ListNodeFor(object)->prev;
    if (front_ == object) {
      front_ = next_object;
    }
    if (next_object) {
      ListNodeFor(next_object)->prev = prev_object;
    }
    if (prev_object) {
      ListNodeFor(prev_object)->next = next_object;
    }
    next_object = nullptr;
    prev_object = nullptr;
    size_--;
  }

  bool Contains(T* object) const {
    if (front_ == object) return true;
    auto* list_node = ListNodeFor(object);
    return list_node->prev || list_node->next;
  }

  size_t Size() const { return size_; }
  bool Empty() const { return size_ == 0; }

  Iterator begin() { return Iterator(front_); }
  Iterator end() { return Iterator(nullptr); }
  ConstIterator begin() const { return ConstIterator(front_); }
  ConstIterator end() const { return ConstIterator(nullptr); }

 private:
  static ListNode* ListNodeFor(T* object) {
    return NodeAccessor::GetListNode(object);
  }
  static const ListNode* ListNodeFor(const T* object) {
    return NodeAccessor::GetListNode(const_cast<T*>(object));
  }

  T* front_ = nullptr;
  size_t size_ = 0;
};

class TracedNode final {
 public:
#ifdef V8_HOST_ARCH_64_BIT
  using IndexType = uint16_t;
#else   // !V8_HOST_ARCH_64_BIT
  using IndexType = uint8_t;
#endif  // !V8_HOST_ARCH_64_BIT

  static TracedNode* FromLocation(Address* location) {
    return reinterpret_cast<TracedNode*>(location);
  }

  static const TracedNode* FromLocation(const Address* location) {
    return reinterpret_cast<const TracedNode*>(location);
  }

  TracedNode(IndexType, IndexType);

  IndexType index() const { return index_; }

  bool is_weak() const { return IsWeak::decode(flags_); }
  void set_weak(bool v) { flags_ = IsWeak::update(flags_, v); }

  template <AccessMode access_mode = AccessMode::NON_ATOMIC>
  bool is_in_use() const {
    if constexpr (access_mode == AccessMode::NON_ATOMIC) {
      return IsInUse::decode(flags_);
    }
    const auto flags =
        reinterpret_cast<const std::atomic<uint8_t>&>(flags_).load(
            std::memory_order_relaxed);
    return IsInUse::decode(flags);
  }
  void set_is_in_use(bool v) { flags_ = IsInUse::update(flags_, v); }

  bool is_in_young_list() const { return IsInYoungList::decode(flags_); }
  void set_is_in_young_list(bool v) {
    flags_ = IsInYoungList::update(flags_, v);
  }

  IndexType next_free() const { return next_free_index_; }
  void set_next_free(IndexType next_free_index) {
    next_free_index_ = next_free_index;
  }
  void set_class_id(uint16_t class_id) { class_id_ = class_id; }

  template <AccessMode access_mode = AccessMode::NON_ATOMIC>
  void set_markbit() {
    if constexpr (access_mode == AccessMode::NON_ATOMIC) {
      flags_ = Markbit::update(flags_, true);
      return;
    }
    std::atomic<uint8_t>& atomic_flags =
        reinterpret_cast<std::atomic<uint8_t>&>(flags_);
    const uint8_t new_value =
        Markbit::update(atomic_flags.load(std::memory_order_relaxed), true);
    atomic_flags.fetch_or(new_value, std::memory_order_relaxed);
  }

  template <AccessMode access_mode = AccessMode::NON_ATOMIC>
  bool markbit() const {
    if constexpr (access_mode == AccessMode::NON_ATOMIC) {
      return Markbit::decode(flags_);
    }
    const auto flags =
        reinterpret_cast<const std::atomic<uint8_t>&>(flags_).load(
            std::memory_order_relaxed);
    return Markbit::decode(flags);
  }

  void clear_markbit() { flags_ = Markbit::update(flags_, false); }

  bool has_old_host() const { return HasOldHost::decode(flags_); }
  void set_has_old_host(bool v) { flags_ = HasOldHost::update(flags_, v); }

  template <AccessMode access_mode = AccessMode::NON_ATOMIC>
  void set_raw_object(Address value) {
    if constexpr (access_mode == AccessMode::NON_ATOMIC) {
      object_ = value;
    } else {
      reinterpret_cast<std::atomic<Address>*>(&object_)->store(
          value, std::memory_order_relaxed);
    }
  }
  Address raw_object() const { return object_; }
  Tagged<Object> object() const { return Tagged<Object>(object_); }
  Handle<Object> handle() { return Handle<Object>(&object_); }
  FullObjectSlot location() { return FullObjectSlot(&object_); }

  Handle<Object> Publish(Tagged<Object> object, bool needs_young_bit_update,
                         bool needs_black_allocation, bool has_old_host);
  void Release();

 private:
  using IsInUse = base::BitField8<bool, 0, 1>;
  using IsInYoungList = IsInUse::Next<bool, 1>;
  using IsWeak = IsInYoungList::Next<bool, 1>;
  // The markbit is the exception as it can be set from the main and marker
  // threads at the same time.
  using Markbit = IsWeak::Next<bool, 1>;
  using HasOldHost = Markbit::Next<bool, 1>;

  Address object_ = kNullAddress;
  union {
    // When a node is in use, the user can specify a class id.
    uint16_t class_id_;
    // When a node is not in use, this index is used to build the free list.
    IndexType next_free_index_;
  };
  IndexType index_;
  uint8_t flags_ = 0;
};

class TracedNodeBlock final {
  struct OverallListNode {
    static auto* GetListNode(TracedNodeBlock* block) {
      return &block->overall_list_node_;
    }
  };

  struct UsableListNode {
    static auto* GetListNode(TracedNodeBlock* block) {
      return &block->usable_list_node_;
    }
  };

  class NodeIteratorImpl final
      : public base::iterator<std::forward_iterator_tag, TracedNode> {
   public:
    explicit NodeIteratorImpl(TracedNodeBlock* block) : block_(block) {}
    NodeIteratorImpl(TracedNodeBlock* block,
                     TracedNode::IndexType current_index)
        : block_(block), current_index_(current_index) {}
    NodeIteratorImpl(const NodeIteratorImpl& other) V8_NOEXCEPT
        : block_(other.block_),
          current_index_(other.current_index_) {}

    TracedNode* operator*() { return block_->at(current_index_); }
    bool operator==(const NodeIteratorImpl& rhs) const {
      return rhs.block_ == block_ && rhs.current_index_ == current_index_;
    }
    bool operator!=(const NodeIteratorImpl& rhs) const {
      return !(*this == rhs);
    }
    inline NodeIteratorImpl& operator++() {
      current_index_++;
      return *this;
    }
    inline NodeIteratorImpl operator++(int) {
      NodeIteratorImpl tmp(*this);
      operator++();
      return tmp;
    }

   private:
    TracedNodeBlock* block_;
    TracedNode::IndexType current_index_ = 0;
  };

 public:
  using OverallList = DoublyLinkedList<TracedNodeBlock, OverallListNode>;
  using UsableList = DoublyLinkedList<TracedNodeBlock, UsableListNode>;
  using Iterator = NodeIteratorImpl;

#if defined(V8_USE_ADDRESS_SANITIZER)
  static constexpr size_t kMinCapacity = 1;
  static constexpr size_t kMaxCapacity = 1;
#else  // !defined(V8_USE_ADDRESS_SANITIZER)
#ifdef V8_HOST_ARCH_64_BIT
  static constexpr size_t kMinCapacity = 256;
#else   // !V8_HOST_ARCH_64_BIT
  static constexpr size_t kMinCapacity = 128;
#endif  // !V8_HOST_ARCH_64_BIT
  static constexpr size_t kMaxCapacity =
      std::numeric_limits<TracedNode::IndexType>::max() - 1;
#endif  // !defined(V8_USE_ADDRESS_SANITIZER)

  static constexpr TracedNode::IndexType kInvalidFreeListNodeIndex = -1;

  static_assert(kMinCapacity <= kMaxCapacity);
  static_assert(kInvalidFreeListNodeIndex > kMaxCapacity);

  static TracedNodeBlock* Create(TracedHandles&, OverallList&, UsableList&);
  static void Delete(TracedNodeBlock*);

  static TracedNodeBlock& From(TracedNode& node);
  static const TracedNodeBlock& From(const TracedNode& node);

  V8_INLINE TracedNode* AllocateNode();
  void FreeNode(TracedNode*);

  TracedNode* at(TracedNode::IndexType index) {
    return &(reinterpret_cast<TracedNode*>(this + 1)[index]);
  }
  const TracedNode* at(TracedNode::IndexType index) const {
    return const_cast<TracedNodeBlock*>(this)->at(index);
  }

  const void* nodes_begin_address() const { return at(0); }
  const void* nodes_end_address() const { return at(capacity_); }

  TracedHandles& traced_handles() const { return traced_handles_; }

  Iterator begin() { return Iterator(this); }
  Iterator end() { return Iterator(this, capacity_); }

  bool IsFull() const { return used_ == capacity_; }
  bool IsEmpty() const { return used_ == 0; }

  size_t size_bytes() const {
    return sizeof(*this) + capacity_ * sizeof(TracedNode);
  }

 private:
  TracedNodeBlock(TracedHandles&, OverallList&, UsableList&,
                  TracedNode::IndexType);

  OverallList::ListNode overall_list_node_;
  UsableList::ListNode usable_list_node_;
  TracedHandles& traced_handles_;
  TracedNode::IndexType used_ = 0;
  const TracedNode::IndexType capacity_ = 0;
  TracedNode::IndexType first_free_node_ = 0;
};

// TracedHandles hold handles that must go through cppgc's tracing methods. The
// handles do otherwise not keep their pointees alive.
class V8_EXPORT_PRIVATE TracedHandles final {
 public:
  enum class MarkMode : uint8_t { kOnlyYoung, kAll };

  static void Destroy(Address* location);
  static void Copy(const Address* const* from, Address** to);
  static void Move(Address** from, Address** to);

  static Tagged<Object> Mark(Address* location, MarkMode mark_mode);
  static Tagged<Object> MarkConservatively(Address* inner_location,
                                           Address* traced_node_block_base,
                                           MarkMode mark_mode);

  static bool IsValidInUseNode(Address* location);

  explicit TracedHandles(Isolate*);
  ~TracedHandles();

  TracedHandles(const TracedHandles&) = delete;
  TracedHandles& operator=(const TracedHandles&) = delete;

  V8_INLINE Handle<Object> Create(Address value, Address* slot,
                                  GlobalHandleStoreMode store_mode);

  using NodeBounds = std::vector<std::pair<const void*, const void*>>;
  const NodeBounds GetNodeBounds() const;

  void SetIsMarking(bool);
  void SetIsSweepingOnMutatorThread(bool);

  // Updates the list of young nodes that is maintained separately.
  void UpdateListOfYoungNodes();
  // Clears the list of young nodes, assuming that the young generation is
  // empty.
  void ClearListOfYoungNodes();

  // Deletes empty blocks. Sweeping must not be running.
  void DeleteEmptyBlocks();

  void ResetDeadNodes(WeakSlotCallbackWithHeap should_reset_handle);
  void ResetYoungDeadNodes(WeakSlotCallbackWithHeap should_reset_handle);

  // Computes whether young weak objects should be considered roots for young
  // generation garbage collections  or just be treated weakly. Per default
  // objects are considered as roots. Objects are treated not as root when both
  // - `JSObject::IsUnmodifiedApiObject` returns true;
  // - the `EmbedderRootsHandler` also does not consider them as roots;
  void ComputeWeaknessForYoungObjects();

  void ProcessYoungObjects(RootVisitor* v,
                           WeakSlotCallbackWithHeap should_reset_handle);

  void Iterate(RootVisitor*);
  void IterateYoung(RootVisitor*);
  void IterateYoungRoots(RootVisitor*);
  void IterateAndMarkYoungRootsWithOldHosts(RootVisitor*);
  void IterateYoungRootsWithOldHostsForTesting(RootVisitor*);

  size_t used_node_count() const { return used_nodes_; }
  size_t used_size_bytes() const { return sizeof(TracedNode) * used_nodes_; }
  size_t total_size_bytes() const { return block_size_bytes_; }

  bool HasYoung() const { return !young_nodes_.empty(); }

  void Destroy(TracedNodeBlock& node_block, TracedNode& node);
  void Copy(const TracedNode& from_node, Address** to);
  void Move(TracedNode& from_node, Address** from, Address** to);

 private:
  V8_INLINE bool IsCppGCHostOld(CppHeap& cpp_heap, Address host) const;
  V8_INLINE CppHeap* GetCppHeapIfUnifiedYoungGC(Isolate* isolate) const;
  V8_INLINE bool NeedsTrackingInYoungNodes(Tagged<Object> object,
                                           TracedNode* node);
  V8_INLINE bool NeedsToBeRemembered(Tagged<Object> value, TracedNode* node,
                                     Address* slot,
                                     GlobalHandleStoreMode store_mode) const;

  V8_INLINE TracedNode* AllocateNode();
  void FreeNode(TracedNode*);

  TracedNodeBlock::OverallList blocks_;
  TracedNodeBlock::UsableList usable_blocks_;
  // List of young nodes. May refer to nodes in `blocks_`, `usable_blocks_`, and
  // `empty_block_candidates_`.
  std::vector<TracedNode*> young_nodes_;
  // Empty blocks that are still referred to from `young_nodes_`.
  std::vector<TracedNodeBlock*> empty_block_candidates_;
  // Fully empty blocks that are neither referenced from any stale references in
  // destructors nor from young nodes.
  std::vector<TracedNodeBlock*> empty_blocks_;
  Isolate* isolate_;
  bool is_marking_ = false;
  bool is_sweeping_on_mutator_thread_ = false;
  size_t used_nodes_ = 0;
  size_t block_size_bytes_ = 0;
};

}  // namespace v8::internal

#endif  // V8_HANDLES_TRACED_HANDLES_H_
