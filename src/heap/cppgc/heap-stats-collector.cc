// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap-stats-collector.h"

#include <algorithm>
#include <cmath>

#include "src/base/logging.h"

namespace cppgc {
namespace internal {

// static
constexpr size_t HeapStatsCollector::kAllocationThresholdBytes;

void HeapStatsCollector::RegisterObserver(AllocationObserver* observer) {
  DCHECK_EQ(allocation_observers_.end(),
            std::find(allocation_observers_.begin(),
                      allocation_observers_.end(), observer));
  allocation_observers_.push_back(observer);
}

void HeapStatsCollector::UnregisterObserver(AllocationObserver* observer) {
  auto it = std::find(allocation_observers_.begin(),
                      allocation_observers_.end(), observer);
  DCHECK_NE(allocation_observers_.end(), it);
  allocation_observers_.erase(it);
}

void HeapStatsCollector::IncreaseAllocatedObjectSize(size_t bytes) {
  // The current GC may not have been started. This is ok as recording considers
  // the whole time range between garbage collections.
  pos_delta_allocated_bytes_since_prev_gc_ += bytes;
}

void HeapStatsCollector::DecreaseAllocatedObjectSize(size_t bytes) {
  // See IncreaseAllocatedObjectSize for lifetime of the counter.
  neg_delta_allocated_bytes_since_prev_gc_ += bytes;
}

void HeapStatsCollector::AllocatedObjectSizeSafepoint() {
  if (std::abs(pos_delta_allocated_bytes_since_prev_gc_ -
               neg_delta_allocated_bytes_since_prev_gc_) >=
      static_cast<int64_t>(kAllocationThresholdBytes)) {
    AllocatedObjectSizeSafepointImpl();
  }
}

void HeapStatsCollector::AllocatedObjectSizeSafepointImpl() {
  allocated_bytes_since_prev_gc_ +=
      static_cast<int64_t>(pos_delta_allocated_bytes_since_prev_gc_) -
      static_cast<int64_t>(neg_delta_allocated_bytes_since_prev_gc_);

  // These observer methods may start or finalize GC. In case they trigger a
  // final GC pause, the delta counters are reset there and the following
  // observer calls are called with '0' updates.
  ForAllAllocationObservers([this](AllocationObserver* observer) {
    // Recompute delta here so that a GC finalization is able to clear the
    // delta for other observer calls.
    int64_t delta = pos_delta_allocated_bytes_since_prev_gc_ -
                    neg_delta_allocated_bytes_since_prev_gc_;
    if (delta < 0) {
      observer->AllocatedObjectSizeDecreased(static_cast<size_t>(-delta));
    } else {
      observer->AllocatedObjectSizeIncreased(static_cast<size_t>(delta));
    }
  });
  pos_delta_allocated_bytes_since_prev_gc_ = 0;
  neg_delta_allocated_bytes_since_prev_gc_ = 0;
}

void HeapStatsCollector::NotifyMarkingStarted() {
  DCHECK(!in_gc_);
  in_gc_ = true;
}

void HeapStatsCollector::NotifyMarkingCompleted(size_t marked_bytes) {
  DCHECK(in_gc_);
  current_.marked_bytes = marked_bytes;
  allocated_bytes_since_prev_gc_ = 0;
  pos_delta_allocated_bytes_since_prev_gc_ = 0;
  neg_delta_allocated_bytes_since_prev_gc_ = 0;
  previous_marking_event_ = current_;
}

const HeapStatsCollector::Event& HeapStatsCollector::NotifySweepingCompleted() {
  DCHECK(in_gc_);
  in_gc_ = false;

  previous_ = std::move(current_);
  current_ = Event();
  previous_marking_event_ = previous_;
  return previous_;
}

size_t HeapStatsCollector::allocated_object_size() const {
  DCHECK_GE(static_cast<int64_t>(previous_marking_event_.marked_bytes) +
                allocated_bytes_since_prev_gc_,
            0);
  return static_cast<size_t>(
      static_cast<int64_t>(previous_marking_event_.marked_bytes) +
      allocated_bytes_since_prev_gc_);
}

}  // namespace internal
}  // namespace cppgc
