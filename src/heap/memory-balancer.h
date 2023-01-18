// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_BALANCER_H_
#define V8_HEAP_MEMORY_BALANCER_H_

#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

struct MemoryBalancer {
  std::atomic<size_t> live_memory{0};
  std::atomic<double> major_allocation_bytes{0};
  std::atomic<double> major_allocation_time{0};
  std::atomic<double> major_gc_bytes{0};
  std::atomic<double> major_gc_time{0};
  std::atomic<bool> has_major_allocation{false};
  std::atomic<bool> has_major_gc{false};
  std::atomic<size_t> last_M_update_time{0};
  std::atomic<double> last_M_memory{0};
  std::atomic<size_t> concurrent_gc_time{0};
  void UpdateLiveMemoryMajorGC(size_t live_memory, double major_gc_bytes,
                               double major_gc_time) {
    this->live_memory = live_memory;
    this->major_gc_bytes = (this->major_gc_bytes + major_gc_bytes) / 2;
    this->major_gc_time = (this->major_gc_time + major_gc_time) / 2;
    has_major_gc = true;
  }
  void UpdateMajorAllocation(double major_allocation_bytes,
                             double major_allocation_time) {
    double kDecayRate = 0.95;
    this->major_allocation_bytes = this->major_allocation_bytes * kDecayRate +
                                   major_allocation_bytes * (1 - kDecayRate);
    this->major_allocation_time = this->major_allocation_time * kDecayRate +
                                  major_allocation_time * (1 - kDecayRate);
    has_major_allocation = true;
  }
};

class Heap;
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
