// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MINOR_MARK_COMPACT_H_
#define V8_HEAP_MINOR_MARK_COMPACT_H_

#include <atomic>
#include <vector>

#include "src/common/globals.h"
#include "src/heap/ephemeron-remembered-set.h"
#include "src/heap/index-generator.h"
#include "src/heap/mark-compact-base.h"
#include "src/heap/marking-state.h"
#include "src/heap/marking-visitor.h"
#include "src/heap/marking-worklist.h"
#include "src/heap/parallel-work-item.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/sweeper.h"

namespace v8 {
namespace internal {

// Marking state that keeps live bytes locally in a fixed-size hashmap. Hashmap
// entries are evicted to the global counters on collision.
class YoungGenerationMarkingState final
    : public MarkingStateBase<YoungGenerationMarkingState, AccessMode::ATOMIC> {
 public:
  explicit YoungGenerationMarkingState(PtrComprCageBase cage_base)
      : MarkingStateBase(cage_base) {}
  V8_INLINE ~YoungGenerationMarkingState();

  const MarkingBitmap* bitmap(const MemoryChunk* chunk) const {
    return chunk->marking_bitmap();
  }

  V8_INLINE void IncrementLiveBytes(MemoryChunk* chunk, intptr_t by);

 private:
  static constexpr size_t kNumEntries = 128;
  static constexpr size_t kEntriesMask = kNumEntries - 1;
  std::array<std::pair<MemoryChunk*, size_t>, kNumEntries> live_bytes_data_;
};

class YoungGenerationMainMarkingVisitor final
    : public YoungGenerationMarkingVisitorBase<
          YoungGenerationMainMarkingVisitor, MarkingState> {
 public:
  YoungGenerationMainMarkingVisitor(
      Isolate* isolate, MarkingWorklists::Local* worklists_local,
      EphemeronRememberedSet::TableList::Local* ephemeron_table_list_local);

  ~YoungGenerationMainMarkingVisitor() override;

  YoungGenerationMainMarkingVisitor(const YoungGenerationMainMarkingVisitor&) =
      delete;
  YoungGenerationMainMarkingVisitor& operator=(
      const YoungGenerationMainMarkingVisitor&) = delete;

  template <typename TSlot>
  V8_INLINE void VisitPointersImpl(HeapObject host, TSlot start, TSlot end);

  YoungGenerationMarkingState* marking_state() { return &marking_state_; }

  template <typename TSlot>
  bool VisitObjectViaSlotInRemeberedSet(TSlot slot) {
    return VisitObjectViaSlot<ObjectVisitationMode::kVisitDirectly,
                              SlotTreatmentMode::kReadWrite>(slot);
  }

  V8_INLINE void Finalize();

 private:
  V8_INLINE bool ShortCutStrings(HeapObjectSlot slot, HeapObject* heap_object);

  YoungGenerationMarkingState marking_state_;
  PretenuringHandler::PretenuringFeedbackMap local_pretenuring_feedback_;
  const bool shortcut_strings_;

  friend class YoungGenerationMarkingVisitorBase<
      YoungGenerationMainMarkingVisitor, MarkingState>;
};

class YoungGenerationRememberedSetsMarkingWorklist {
 private:
  class MarkingItem;

 public:
  class Local {
   public:
    explicit Local(YoungGenerationRememberedSetsMarkingWorklist* handler)
        : handler_(handler) {}

    template <typename Visitor>
    bool ProcessNextItem(Visitor* visitor) {
      return handler_->ProcessNextItem(visitor, index_);
    }

   private:
    YoungGenerationRememberedSetsMarkingWorklist* const handler_;
    base::Optional<size_t> index_;
  };

  static std::vector<MarkingItem> CollectItems(Heap* heap);

  explicit YoungGenerationRememberedSetsMarkingWorklist(Heap* heap);
  ~YoungGenerationRememberedSetsMarkingWorklist();

  size_t RemainingRememberedSetsMarkingIteams() const {
    return remaining_remembered_sets_marking_items_.load(
        std::memory_order_relaxed);
  }

 private:
  class MarkingItem : public ParallelWorkItem {
   public:
    enum class SlotsType { kRegularSlots, kTypedSlots };

    MarkingItem(MemoryChunk* chunk, SlotsType slots_type, void* slot_set,
                void* background_slot_set = nullptr)
        : chunk_(chunk),
          slots_type_(slots_type),
          slot_set_(slot_set),
          background_slot_set_(background_slot_set) {}
    ~MarkingItem() = default;

    template <typename Visitor>
    void Process(Visitor* visitor);
    void MergeAndDeleteRememberedSets();

   private:
    inline Heap* heap() { return chunk_->heap(); }

    template <typename Visitor>
    void MarkUntypedPointers(Visitor* visitor);
    template <typename Visitor>
    void MarkTypedPointers(Visitor* visitor);
    template <typename Visitor, typename TSlot>
    V8_INLINE SlotCallbackResult CheckAndMarkObject(Visitor* visitor,
                                                    TSlot slot);

    template <typename TSlot>
    V8_INLINE void CheckOldToNewSlotForSharedUntyped(MemoryChunk* chunk,
                                                     TSlot slot);
    V8_INLINE void CheckOldToNewSlotForSharedTyped(MemoryChunk* chunk,
                                                   SlotType slot_type,
                                                   Address slot_address,
                                                   MaybeObject new_target);

    MemoryChunk* const chunk_;
    const SlotsType slots_type_;
    void* slot_set_;
    void* background_slot_set_;
  };
  template <typename Visitor>
  bool ProcessNextItem(Visitor* visitor, base::Optional<size_t>& index);

  std::vector<MarkingItem> remembered_sets_marking_items_;
  std::atomic_size_t remaining_remembered_sets_marking_items_;
  IndexGenerator remembered_sets_marking_index_generator_;
};

// Collector for young-generation only.
class MinorMarkCompactCollector final : public MarkCompactCollectorBase {
 public:
  static constexpr size_t kMaxParallelTasks = 8;

  static MinorMarkCompactCollector* From(MarkCompactCollectorBase* collector) {
    return static_cast<MinorMarkCompactCollector*>(collector);
  }

  explicit MinorMarkCompactCollector(Heap* heap);
  ~MinorMarkCompactCollector() final;

  void TearDown() final;
  void CollectGarbage() final;
  void StartMarking() final;

  void MakeIterable(Page* page, FreeSpaceTreatmentMode free_space_mode);

  void Finish() final;

  // Perform Wrapper Tracing if in use.
  void PerformWrapperTracing();

  EphemeronRememberedSet::TableList* ephemeron_table_list() const {
    return ephemeron_table_list_.get();
  }

  YoungGenerationRememberedSetsMarkingWorklist*
  remembered_sets_marking_handler() {
    DCHECK_NOT_NULL(remembered_sets_marking_handler_);
    return remembered_sets_marking_handler_.get();
  }

 private:
  class RootMarkingVisitor;

  static const int kNumMarkers = 8;
  static const int kMainMarker = 0;

  Sweeper* sweeper() { return sweeper_; }

  void MarkLiveObjects();
  void MarkLiveObjectsInParallel(RootMarkingVisitor* root_visitor,
                                 bool was_marked_incrementally);
  void DrainMarkingWorklist(YoungGenerationMainMarkingVisitor& visitor);
  void TraceFragmentation();
  void ClearNonLiveReferences();

  void Sweep();

  void FinishConcurrentMarking();

  // 'StartSweepNewSpace' and 'SweepNewLargeSpace' return true if any pages were
  // promoted.
  bool StartSweepNewSpace();
  bool SweepNewLargeSpace();

  std::unique_ptr<EphemeronRememberedSet::TableList> ephemeron_table_list_;
  std::unique_ptr<EphemeronRememberedSet::TableList::Local>
      local_ephemeron_table_list_;

  Sweeper* const sweeper_;
  std::unique_ptr<YoungGenerationRememberedSetsMarkingWorklist>
      remembered_sets_marking_handler_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MINOR_MARK_COMPACT_H_
