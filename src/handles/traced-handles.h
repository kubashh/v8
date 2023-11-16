// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_TRACED_HANDLES_H_
#define V8_HANDLES_TRACED_HANDLES_H_

#include "include/v8-traced-handle.h"
#include "src/base/doubly-threaded-list.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/objects/objects.h"
#include "src/objects/visitors.h"

namespace v8::internal {

class CppHeap;
class Isolate;
class TracedHandles;
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

  TracedNode() = default;
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

  // bool is_in_young_list() const { return IsInYoungList::decode(flags_); }
  // void set_is_in_young_list(bool v) {
  //   flags_ = IsInYoungList::update(flags_, v);
  // }

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
  using IsWeak = IsInUse::Next<bool, 1>;
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
  struct OverallListTraits {
    static TracedNodeBlock*** prev(TracedNodeBlock* tnb) {
      return &tnb->overall_list_node_.prev_;
    }
    static TracedNodeBlock** next(TracedNodeBlock* tnb) {
      return &tnb->overall_list_node_.next_;
    }
    static bool non_empty(TracedNodeBlock* tnb) { return tnb != nullptr; }
  };

  struct UsableListTraits {
    static TracedNodeBlock*** prev(TracedNodeBlock* tnb) {
      return &tnb->usable_list_node_.prev_;
    }
    static TracedNodeBlock** next(TracedNodeBlock* tnb) {
      return &tnb->usable_list_node_.next_;
    }
    static bool non_empty(TracedNodeBlock* tnb) { return tnb != nullptr; }
  };

  struct YoungListTraits {
    static TracedNodeBlock*** prev(TracedNodeBlock* tnb) {
      return &tnb->young_list_node_.prev_;
    }
    static TracedNodeBlock** next(TracedNodeBlock* tnb) {
      return &tnb->young_list_node_.next_;
    }
    static bool non_empty(TracedNodeBlock* tnb) { return tnb != nullptr; }
  };

  using OverallList =
      v8::base::DoublyThreadedList<TracedNodeBlock*, OverallListTraits>;
  using UsableList =
      v8::base::DoublyThreadedList<TracedNodeBlock*, UsableListTraits>;
  using YoungList =
      v8::base::DoublyThreadedList<TracedNodeBlock*, YoungListTraits>;
  using Iterator = NodeIteratorImpl;

#if defined(V8_USE_ADDRESS_SANITIZER)
  static constexpr size_t kCapacity = 1;
#else  // !defined(V8_USE_ADDRESS_SANITIZER)
#ifdef V8_HOST_ARCH_64_BIT
  static constexpr size_t kCapacity = 256;
#else   // !V8_HOST_ARCH_64_BIT
  static constexpr size_t kCapacity = 128;
#endif  // !V8_HOST_ARCH_64_BIT
#endif  // !defined(V8_USE_ADDRESS_SANITIZER)

  static constexpr TracedNode::IndexType kInvalidFreeListNodeIndex = -1;

  static_assert(kInvalidFreeListNodeIndex > kCapacity);

  static TracedNodeBlock* Create(TracedHandles&, OverallList&, UsableList&);
  static void Delete(TracedNodeBlock*);

  static TracedNodeBlock& From(TracedNode& node);
  static const TracedNodeBlock& From(const TracedNode& node);

  V8_INLINE TracedNode* AllocateNode();
  void FreeNode(TracedNode*);

  TracedNode* at(TracedNode::IndexType index) {
    DCHECK_LT(index, kCapacity);
    return &(nodes_[index]);
  }
  const TracedNode* at(TracedNode::IndexType index) const {
    return const_cast<TracedNodeBlock*>(this)->at(index);
  }

  const void* nodes_begin_address() const { return at(0); }
  const void* nodes_end_address() const { return &(nodes_[kCapacity]); }

  TracedHandles& traced_handles() const { return traced_handles_; }

  Iterator begin() { return Iterator(this); }
  Iterator end() { return Iterator(this, kCapacity); }

  bool IsFull() const { return used_ == kCapacity; }
  bool IsEmpty() const { return used_ == 0; }

  bool IsYoung(TracedNode::IndexType index) const {
    return young_nodes_bits_[index / CHAR_BIT] & (1u << (index % CHAR_BIT));
  }
  bool SetYoung(TracedNode::IndexType index) {
    DCHECK(!IsYoung(index));
    DCHECK_GT(used_young_ + 1, used_young_);
    young_nodes_bits_[index / CHAR_BIT] |= (1u << (index % CHAR_BIT));
    return used_young_++ == 0;
  }
  bool ClearYoung(TracedNode::IndexType index) {
    DCHECK(IsYoung(index));
    DCHECK_LT(used_young_ - 1, used_young_);
    young_nodes_bits_[index / CHAR_BIT] &= ~(1u << (index % CHAR_BIT));
    return --used_young_ == 0;
  }
  bool HasYoungNodes() const { return used_young_ > 0; }

  size_t size_bytes() const { return sizeof(*this); }

 private:
  struct ListNode {
    TracedNodeBlock** prev_;
    TracedNodeBlock* next_;
  };

  TracedNodeBlock(TracedHandles&, OverallList&, UsableList&);

  ListNode overall_list_node_;
  ListNode usable_list_node_;
  ListNode young_list_node_;

  TracedHandles& traced_handles_;
  TracedNode::IndexType used_ = 0;
  TracedNode::IndexType used_young_ = 0;
  TracedNode::IndexType first_free_node_ = 0;
  uint8_t young_nodes_bits_[std::max(size_t{1}, kCapacity / CHAR_BIT)] = {0};
  TracedNode nodes_[kCapacity];
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

  bool HasYoung() const { return !young_blocks_.empty(); }

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

  // List of blocks that are non-empty.
  TracedNodeBlock::OverallList blocks_;
  size_t num_blocks_ = 0;
  // List of blocks that are non-empty and usable (can be allocated on).
  TracedNodeBlock::UsableList usable_blocks_;
  // List of blocks that contain young nodes.
  TracedNodeBlock::YoungList young_blocks_;
  // Vector of fully empty blocks that are neither referenced from any stale
  // references  (in e.g. destructors).
  std::vector<TracedNodeBlock*> empty_blocks_;
  Isolate* isolate_;
  bool is_marking_ = false;
  bool is_sweeping_on_mutator_thread_ = false;
  size_t used_nodes_ = 0;
  size_t block_size_bytes_ = 0;
};

}  // namespace v8::internal

#endif  // V8_HANDLES_TRACED_HANDLES_H_
