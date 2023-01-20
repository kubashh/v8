// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_BALANCER_H_
#define V8_HEAP_MEMORY_BALANCER_H_

#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

class Heap;

class MemoryBalancer {
 public:
  Heap* heap_;

  constexpr static int kSecondsToNanoseconds = 1e9;
  constexpr static int kMillisecondsToNanoseconds = 1e6;

  explicit MemoryBalancer(Heap* heap) : heap_(heap) {}

  void UpdateLiveMemoryMajorGC(size_t live_memory, double major_gc_bytes,
                               double major_gc_time);
  void UpdateMajorAllocation(double major_allocation_bytes,
                             double major_allocation_time);
  // also touch global allocation limit
  void UpdateHeapLimit(size_t new_limit);
  void PostMemoryMeasurementTask();
  void Update();
  void NotifyGC(size_t pre_gc_memory, size_t post_gc_memory);

  size_t global_allocation_limit_delta_ = 0;
  size_t live_memory_ = 0;
  double major_allocation_bytes_ = 0;
  double major_allocation_time_ = 0;
  double major_gc_bytes_ = 0;
  double major_gc_time_ = 0;
  bool has_major_allocation_ = false;
  bool has_major_gc_ = false;
  size_t last_M_update_time_ = 0;
  double last_M_memory_ = 0;
  size_t concurrent_gc_time_ = 0;
  bool allocation_measurer_started = false;
};

class MemoryMeasurementTask : public CancelableTask {
 public:
  explicit MemoryMeasurementTask(Heap* heap);

  ~MemoryMeasurementTask() override = default;
  MemoryMeasurementTask(const MemoryMeasurementTask&) = delete;
  MemoryMeasurementTask& operator=(const MemoryMeasurementTask&) = delete;

 private:
  // v8::internal::CancelableTask overrides.
  void RunInternal() override;

  Heap* heap_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MEMORY_BALANCER_H_
