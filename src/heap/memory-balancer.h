// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_BALANCER_H_
#define V8_HEAP_MEMORY_BALANCER_H_

#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

class Heap;

// The class that implment memory balancer.
// Listen to allocation/garbage collection events,
// and smooth them using exponentially weighted moving average(EWMA).
// spawn heartbeat task that monitor allocation rate.
// calculate heap limit and update it accordingly.
class MemoryBalancer {
 public:
  Heap* heap_;

  constexpr static int kSecondsToNanoseconds = 1e9;
  constexpr static int kMillisecondsToNanoseconds = 1e6;

  explicit MemoryBalancer(Heap* heap) : heap_(heap) {}

  void UpdateLiveMemory(size_t live_memory);
  void UpdateMajorGC(double major_gc_bytes, double major_gc_duration);
  void UpdateMajorAllocation(double major_allocation_bytes,
                             double major_allocation_duration);
  // Also touch global allocation limit
  void UpdateHeapLimit(size_t new_limit);
  void PostMemoryMeasurementTask();
  void Update();
  void NotifyGC();

  // Live memory estimate of the heap, obtained at the last major garbage
  // collection.
  size_t live_memory_ = 0;

  // We want to set the old_generation_allocation_limit our way,
  // but when we do so we are taking memory from the external heap,
  // because global allocation limit is shared between old generation and
  // external heap. we thus calculate the external heap limit and keep it
  // unchanged, by 'patching' the global_allocation_limit_. a more principaled
  // solution is to manage the external heap also using membalancer. Reviewer -
  // I can also change heap.h and replace global_allocation_limit_ with an
  // external_allocation_limit_. then I dont have to do this patching anymore.
  // What's your thought?
  size_t external_allocation_limit_ = 0;

  struct SmoothedBytesAndDuration {
    double bytes;
    double duration;
    void Update(double bytes, double duration, double decay_rate) {
      this->bytes = this->bytes * decay_rate + bytes * (1 - decay_rate);
      this->duration =
          this->duration * decay_rate + duration * (1 - decay_rate);
    }
    double rate() const { return bytes / duration; }
  };

  // Our estimate of major allocation rate, and major gc speed.
  base::Optional<SmoothedBytesAndDuration> major_allocation_;
  base::Optional<SmoothedBytesAndDuration> major_gc_;

  // MemoryMeasurementTask use the diff between last observed time/memory and
  // current time/memory to calculate the allocation rate.
  size_t last_m_update_time_ = 0;
  double last_m_memory_ = 0;
  bool allocation_measurer_started = false;
};

class MemoryMeasurementTask : public CancelableTask {
 public:
  explicit MemoryMeasurementTask(MemoryBalancer* mb);

  ~MemoryMeasurementTask() override = default;
  MemoryMeasurementTask(const MemoryMeasurementTask&) = delete;
  MemoryMeasurementTask& operator=(const MemoryMeasurementTask&) = delete;

 private:
  // v8::internal::CancelableTask overrides.
  void RunInternal() override;

  MemoryBalancer* mb_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MEMORY_BALANCER_H_
