// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MINOR_MARK_COMPACT_INL_H_
#define V8_HEAP_MINOR_MARK_COMPACT_INL_H_

#include <atomic>

#include "src/base/build_config.h"
#include "src/common/globals.h"
#include "src/heap/memory-chunk.h"
#include "src/heap/minor-mark-compact.h"
#include "src/heap/remembered-set-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/map.h"
#include "src/objects/string.h"
#include "src/roots/static-roots.h"

namespace v8 {
namespace internal {

template <typename Visitor>
bool YoungGenerationRememberedSetsMarkingWorklist::ProcessNextItem(
    Visitor* visitor, base::Optional<size_t>& index) {
  if (remaining_remembered_sets_marking_items_.load(
          std::memory_order_relaxed) == 0) {
    return false;
  }
  while (true) {
    if (index && (index < remembered_sets_marking_items_.size())) {
      auto& work_item = remembered_sets_marking_items_[*index];
      if (work_item.TryAcquire()) {
        remaining_remembered_sets_marking_items_.fetch_sub(
            1, std::memory_order_relaxed);
        work_item.Process(visitor);
        (*index)++;
        return true;
      }
    }
    index = remembered_sets_marking_index_generator_.GetNext();
    if (!index) return false;
  }
}

template <typename Visitor>
void YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::Process(
    Visitor* visitor) {
  CodePageHeaderModificationScope header_modification_scope(
      "Marking modifies the remembered sets in the page header");
  if (slots_type_ == SlotsType::kRegularSlots) {
    MarkUntypedPointers(visitor);
  } else {
    MarkTypedPointers(visitor);
  }
}

template <typename TSlot>
void YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::
    CheckOldToNewSlotForSharedUntyped(MemoryChunk* chunk, TSlot slot) {
  MaybeObject object = *slot;
  HeapObject heap_object;
  if (object.GetHeapObject(&heap_object) &&
      heap_object.InWritableSharedSpace()) {
    RememberedSet<OLD_TO_SHARED>::Insert<AccessMode::ATOMIC>(chunk,
                                                             slot.address());
  }
}

void YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::
    CheckOldToNewSlotForSharedTyped(MemoryChunk* chunk, SlotType slot_type,
                                    Address slot_address,
                                    MaybeObject new_target) {
  HeapObject heap_object;

  if (new_target.GetHeapObject(&heap_object) &&
      heap_object.InWritableSharedSpace()) {
    const uintptr_t offset = slot_address - chunk->address();
    DCHECK_LT(offset, static_cast<uintptr_t>(TypedSlotSet::kMaxOffset));
    RememberedSet<OLD_TO_SHARED>::InsertTyped(chunk, slot_type,
                                              static_cast<uint32_t>(offset));
  }
}

template <typename Visitor>
void YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::
    MarkUntypedPointers(Visitor* visitor) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
               "MarkingItem::MarkUntypedPointers");
  const bool record_old_to_shared_slots =
      chunk_->heap()->isolate()->has_shared_space();
  auto callback = [this, visitor,
                   record_old_to_shared_slots](MaybeObjectSlot slot) {
    SlotCallbackResult result = CheckAndMarkObject(visitor, slot);
    if (result == REMOVE_SLOT && record_old_to_shared_slots) {
      CheckOldToNewSlotForSharedUntyped(chunk_, slot);
    }
    return result;
  };
  if (SlotSet* slot_set = reinterpret_cast<SlotSet*>(slot_set_)) {
    const auto slot_count =
        RememberedSet<OLD_TO_NEW>::template Iterate<AccessMode::NON_ATOMIC>(
            slot_set, chunk_, callback, SlotSet::FREE_EMPTY_BUCKETS);
    if (slot_count == 0) {
      slot_set_ = nullptr;
      SlotSet::Delete(slot_set, chunk_->buckets());
    }
  }
  if (SlotSet* background_slot_set =
          reinterpret_cast<SlotSet*>(background_slot_set_)) {
    const auto slot_count =
        RememberedSet<OLD_TO_NEW_BACKGROUND>::template Iterate<
            AccessMode::NON_ATOMIC>(background_slot_set, chunk_, callback,
                                    SlotSet::FREE_EMPTY_BUCKETS);
    if (slot_count == 0) {
      background_slot_set_ = nullptr;
      SlotSet::Delete(background_slot_set, chunk_->buckets());
    }
  }
}

template <typename Visitor>
void YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::
    MarkTypedPointers(Visitor* visitor) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
               "MarkingItem::MarkTypedPointers");
  const bool record_old_to_shared_slots =
      chunk_->heap()->isolate()->has_shared_space();
  DCHECK_NULL(background_slot_set_);
  TypedSlotSet* typed_slot_set = reinterpret_cast<TypedSlotSet*>(slot_set_);
  DCHECK_NOT_NULL(typed_slot_set);
  const auto slot_count = RememberedSet<OLD_TO_NEW>::IterateTyped(
      typed_slot_set, [this, visitor, record_old_to_shared_slots](
                          SlotType slot_type, Address slot_address) {
        return UpdateTypedSlotHelper::UpdateTypedSlot(
            heap(), slot_type, slot_address,
            [this, visitor, record_old_to_shared_slots, slot_address,
             slot_type](FullMaybeObjectSlot slot) {
              SlotCallbackResult result = CheckAndMarkObject(visitor, slot);
              if (result == REMOVE_SLOT && record_old_to_shared_slots) {
                CheckOldToNewSlotForSharedTyped(chunk_, slot_type, slot_address,
                                                *slot);
              }
              return result;
            });
      });
  if (slot_count == 0) {
    slot_set_ = nullptr;
    delete typed_slot_set;
  }
}

template <typename Visitor, typename TSlot>
V8_INLINE SlotCallbackResult
YoungGenerationRememberedSetsMarkingWorklist::MarkingItem::CheckAndMarkObject(
    Visitor* visitor, TSlot slot) {
  static_assert(
      std::is_same<TSlot, FullMaybeObjectSlot>::value ||
          std::is_same<TSlot, MaybeObjectSlot>::value,
      "Only FullMaybeObjectSlot and MaybeObjectSlot are expected here");
  return visitor->VisitObjectViaSlotInRemeberedSet(slot) ? KEEP_SLOT
                                                         : REMOVE_SLOT;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MINOR_MARK_COMPACT_INL_H_
