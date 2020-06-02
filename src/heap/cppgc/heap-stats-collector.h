// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_HEAP_STATS_COLLECTOR_H_
#define V8_HEAP_CPPGC_HEAP_STATS_COLLECTOR_H_

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "src/base/macros.h"

namespace cppgc {
namespace internal {

// Sink for various time and memory statistics.
class V8_EXPORT_PRIVATE HeapStatsCollector final {
 public:
  // POD to hold interesting data accumulated during a garbage collection cycle.
  //
  // The event is always fully populated when looking at previous events but
  // may only be partially populated when looking at the current event.
  struct Event final {
    // Marked bytes collected during marking.
    size_t marked_bytes = 0;
  };

  class AllocationObserver {
   public:
    // Called after observing at least
    // HeapStatsCollector::kAllocationThresholdBytes changed bytes through
    // allocation or explicit free. Reports both, negative and positive
    // increments, to allow observer to decide whether absolute values or only
    // the deltas is interesting.
    //
    // May trigger GC.
    virtual void AllocatedObjectSizeIncreased(size_t) = 0;
    virtual void AllocatedObjectSizeDecreased(size_t) = 0;
  };

  // Observers are implemented using virtual calls. Avoid notifications below
  // reasonably interesting sizes.
  static constexpr size_t kAllocationThresholdBytes = 1024;

  HeapStatsCollector() = default;
  HeapStatsCollector(const HeapStatsCollector&) = delete;
  HeapStatsCollector& operator=(const HeapStatsCollector&) = delete;

  void RegisterObserver(AllocationObserver*);
  void UnregisterObserver(AllocationObserver*);

  void IncreaseAllocatedObjectSize(size_t);
  void DecreaseAllocatedObjectSize(size_t);
  // Safepoints should only be invoked when garabge collections are possible.
  // This is necessary as increments and decrements are reported as close to
  // their actual allocation/reclamation as possible.
  void AllocatedObjectSizeSafepoint();

  // Indicates a new garbage collection cycle.
  void NotifyMarkingStarted();
  // Indicates that marking of the current garbage collection cycle is
  // completed.
  void NotifyMarkingCompleted(size_t marked_bytes);
  // Indicates the end of a garbage collection cycle. This means that sweeping
  // is finished at this point.
  const Event& NotifySweepingCompleted();

  // Size of live objects in bytes  on the heap. Based on marked bytes in the
  // GC previous cycle and newly allocated bytes since the previous cycle.
  size_t allocated_object_size() const;

 private:
  // Invokes |callback| for all registered observers.
  template <typename Callback>
  void ForAllAllocationObservers(Callback callback);

  void AllocatedObjectSizeSafepointImpl();

  // Allocated bytes since the last garbage collection. These bytes are reset
  // after marking as they are accounted in marked_bytes then. May be negative
  // in case an object was explicitly freed that was marked as live in the
  // previous cycle.
  int64_t allocated_bytes_since_prev_gc_ = 0;
  int64_t pos_delta_allocated_bytes_since_prev_gc_ = 0;
  int64_t neg_delta_allocated_bytes_since_prev_gc_ = 0;

  // vector to allow fast iteration of observers. Register/Unregisters only
  // happens on startup/teardown.
  std::vector<AllocationObserver*> allocation_observers_;

  bool in_gc_ = false;

  // The event being filled by the current GC cycle between NotifyMarkingStarted
  // and NotifySweepingFinished.
  Event current_;
  // The previous GC event which is populated at NotifySweepingFinished.
  Event previous_;
  // The previous marking event which is populated at NotifyMarkingFinished.
  // This event allows refering to the previous marking cycle before sweeping
  // is finished.
  Event& previous_marking_event_ = current_;
};

template <typename Callback>
void HeapStatsCollector::ForAllAllocationObservers(Callback callback) {
  for (AllocationObserver* observer : allocation_observers_) {
    callback(observer);
  }
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_HEAP_STATS_COLLECTOR_H_
