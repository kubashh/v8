// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CONCURRENT_MARKING_H_
#define V8_HEAP_CONCURRENT_MARKING_H_

#include <memory>

#include "include/v8-platform.h"
#include "src/base/atomic-utils.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/heap/marking-visitor.h"
#include "src/heap/marking-worklist.h"
#include "src/heap/memory-measurement.h"
#include "src/heap/slot-set.h"
#include "src/heap/spaces.h"
#include "src/init/v8.h"
#include "src/tasks/cancelable-task.h"
#include "src/utils/allocation.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

class Heap;
class Isolate;
class NonAtomicMarkingState;
class MemoryChunk;
class WeakObjects;

struct MemoryChunkData {
  intptr_t live_bytes;
  std::unique_ptr<TypedSlots> typed_slots;
};

using MemoryChunkDataMap =
    std::unordered_map<MemoryChunk*, MemoryChunkData, MemoryChunk::Hasher>;

class V8_EXPORT_PRIVATE ConcurrentMarking {
 public:
  // When the scope is entered, the concurrent marking tasks
  // are preempted and are not looking at the heap objects, concurrent marking
  // is resumed when the scope is exited.
  class V8_NODISCARD PauseScope {
   public:
    explicit PauseScope(ConcurrentMarking* concurrent_marking);
    ~PauseScope();

   private:
    ConcurrentMarking* const concurrent_marking_;
    const bool resume_on_exit_;
  };

  ConcurrentMarking(Heap* heap, MarkingWorklists* marking_worklists,
                    WeakObjects* weak_objects);

  // Schedules asynchronous job to perform concurrent marking at |priority|.
  // Objects in the heap should not be moved while these are active (can be
  // stopped safely via Stop() or PauseScope).
  void ScheduleJob(TaskPriority priority = TaskPriority::kUserVisible);

  // Waits for scheduled job to complete.
  void Join();
  // Preempts ongoing job ASAP. Returns true if concurrent marking was in
  // progress, false otherwise.
  bool Pause();

  // Schedules asynchronous job to perform concurrent marking at |priority| if
  // not already running, otherwise adjusts the number of workers running job
  // and the priority if different from the default kUserVisible.
  void RescheduleJobIfNeeded(
      TaskPriority priority = TaskPriority::kUserVisible);
  // Flushes native context sizes to the given table of the main thread.
  void FlushNativeContexts(NativeContextStats* main_stats);
  // Flushes memory chunk data using the given marking state.
  void FlushMemoryChunkData(NonAtomicMarkingState* marking_state);
  // This function is called for a new space page that was cleared after
  // scavenge and is going to be re-used.
  void ClearMemoryChunkData(MemoryChunk* chunk);

  // Checks if all threads are stopped.
  bool IsStopped();

  size_t TotalMarkedBytes();

  void set_another_ephemeron_iteration(bool another_ephemeron_iteration) {
    another_ephemeron_iteration_.store(another_ephemeron_iteration);
  }
  bool another_ephemeron_iteration() {
    return another_ephemeron_iteration_.load();
  }

 private:
  struct TaskState {
    size_t marked_bytes = 0;
    MemoryChunkDataMap memory_chunk_data;
    NativeContextInferrer native_context_inferrer;
    NativeContextStats native_context_stats;
    char cache_line_padding[64];
  };
  class JobTask;
  void Run(JobDelegate* delegate, base::EnumSet<CodeFlushMode> code_flush_mode,
           unsigned mark_compact_epoch, bool should_keep_ages_unchanged);
  size_t GetMaxConcurrency(size_t worker_count);

  std::unique_ptr<JobHandle> job_handle_;
  Heap* const heap_;
  MarkingWorklists* const marking_worklists_;
  WeakObjects* const weak_objects_;
  std::vector<std::unique_ptr<TaskState>> task_state_;
  std::atomic<size_t> total_marked_bytes_{0};
  std::atomic<bool> another_ephemeron_iteration_{false};
};

// Helper class for storing in-object slot addresses and values.
class SlotSnapshot {
 public:
  SlotSnapshot() : number_of_slots_(0) {}
  SlotSnapshot(const SlotSnapshot&) = delete;
  SlotSnapshot& operator=(const SlotSnapshot&) = delete;
  int number_of_slots() const { return number_of_slots_; }
  ObjectSlot slot(int i) const { return snapshot_[i].first; }
  Object value(int i) const { return snapshot_[i].second; }
  void clear() { number_of_slots_ = 0; }
  void add(ObjectSlot slot, Object value) {
    snapshot_[number_of_slots_++] = {slot, value};
  }

 private:
  static const int kMaxSnapshotSize = JSObject::kMaxInstanceSize / kTaggedSize;
  int number_of_slots_;
  std::pair<ObjectSlot, Object> snapshot_[kMaxSnapshotSize];
};

class ConcurrentMarkingState final
    : public MarkingStateBase<ConcurrentMarkingState, AccessMode::ATOMIC> {
 public:
  ConcurrentMarkingState(PtrComprCageBase cage_base,
                         MemoryChunkDataMap* memory_chunk_data)
      : MarkingStateBase(cage_base), memory_chunk_data_(memory_chunk_data) {}

  ConcurrentBitmap<AccessMode::ATOMIC>* bitmap(
      const BasicMemoryChunk* chunk) const {
    return chunk->marking_bitmap<AccessMode::ATOMIC>();
  }

  void IncrementLiveBytes(MemoryChunk* chunk, intptr_t by) {
    (*memory_chunk_data_)[chunk].live_bytes += by;
  }

  // The live_bytes and SetLiveBytes methods of the marking state are
  // not used by the concurrent marker.

 private:
  MemoryChunkDataMap* memory_chunk_data_;
};

class YoungGenerationConcurrentMarkingVisitor final
    : public YoungGenerationMarkingVisitorBase<
          YoungGenerationConcurrentMarkingVisitor, ConcurrentMarkingState> {
 public:
  YoungGenerationConcurrentMarkingVisitor(
      Heap* heap, MarkingWorklists::Local* worklists_local,
      MemoryChunkDataMap* memory_chunk_data);

  bool is_shared_heap();

  void SynchronizePageAccess(HeapObject heap_object);

  template <typename T>
  static V8_INLINE T Cast(HeapObject object) {
    return T::cast(object);
  }

  // Used by utility functions
  void MarkObject(HeapObject host, HeapObject object);

  // HeapVisitor overrides to implement the snapshotting protocol.

  bool AllowDefaultJSObjectVisit();

  int VisitJSObject(Map map, JSObject object);

  int VisitJSObjectFast(Map map, JSObject object);

  int VisitJSExternalObject(Map map, JSExternalObject object);

#if V8_ENABLE_WEBASSEMBLY
  int VisitWasmInstanceObject(Map map, WasmInstanceObject object);
  int VisitWasmSuspenderObject(Map map, WasmSuspenderObject object);
#endif  // V8_ENABLE_WEBASSEMBLY

  int VisitJSWeakCollection(Map map, JSWeakCollection object);

  int VisitJSFinalizationRegistry(Map map, JSFinalizationRegistry object);

  int VisitConsString(Map map, ConsString object);

  int VisitSlicedString(Map map, SlicedString object);
  int VisitThinString(Map map, ThinString object);

  int VisitSeqOneByteString(Map map, SeqOneByteString object);
  int VisitSeqTwoByteString(Map map, SeqTwoByteString object);
  void VisitMapPointer(HeapObject host);
  // HeapVisitor override.

  bool ShouldVisit(HeapObject object);

  bool ShouldVisitUnaccounted(HeapObject object);
  template <typename TSlot>
  void RecordSlot(HeapObject object, TSlot slot, HeapObject target);

  SlotSnapshot* slot_snapshot();

  ConcurrentMarkingState* marking_state();

 private:
  template <typename T>
  int VisitLeftTrimmableArray(Map map, T object);
  ConcurrentMarkingState marking_state_;
  SlotSnapshot slot_snapshot_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CONCURRENT_MARKING_H_
