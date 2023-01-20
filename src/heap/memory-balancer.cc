// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-balancer.h"

#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"

namespace v8 {
namespace internal {

void MemoryBalancer::Update() {
  if (v8_flags.memory_balancer && has_major_allocation_ && has_major_gc_) {
    size_t new_limit =
        live_memory_ +
        sqrt(live_memory_ * (major_allocation_bytes_ / major_allocation_time_) /
             (major_gc_bytes_ / major_gc_time_) /
             v8_flags.memory_balancer_c_value);
    UpdateHeapLimit(new_limit);
  }
}

// 2 MB of extra space.
// This allow the heap size to not decay to CurrentSizeOfObject()
// preventing gc to trigger if, after a long period of idleness,
// a small allocation appear.
constexpr size_t kMinHeapExtraSpace = 2 * MB;

void MemoryBalancer::UpdateHeapLimit(size_t new_limit) {
  new_limit = std::max<size_t>(live_memory_ + kMinHeapExtraSpace, new_limit) +
              heap_->new_space_->Capacity();
  heap_->old_generation_allocation_limit_ = new_limit;
  heap_->global_allocation_limit_ = new_limit + global_allocation_limit_delta_;
}

void MemoryBalancer::PostMemoryMeasurementTask() {
  auto taskrunner = V8::GetCurrentPlatform()->GetForegroundTaskRunner(
      reinterpret_cast<v8::Isolate*>(heap_->isolate()));
  taskrunner->PostDelayedTask(std::make_unique<MemoryMeasurementTask>(heap_),
                              1);
}

void MemoryBalancer::UpdateLiveMemoryMajorGC(size_t live_memory,
                                             double major_gc_bytes,
                                             double major_gc_time) {
  live_memory_ = live_memory;
  major_gc_bytes_ = (major_gc_bytes_ + major_gc_bytes) / 2;
  major_gc_time_ = (major_gc_time_ + major_gc_time) / 2;
  has_major_gc_ = true;
}

void MemoryBalancer::UpdateMajorAllocation(double major_allocation_bytes,
                                           double major_allocation_time) {
  double kDecayRate = 0.95;
  major_allocation_bytes_ = major_allocation_bytes_ * kDecayRate +
                            major_allocation_bytes * (1 - kDecayRate);
  major_allocation_time_ = major_allocation_time_ * kDecayRate +
                           major_allocation_time * (1 - kDecayRate);
  has_major_allocation_ = true;
}

void MemoryBalancer::NotifyGC(size_t pre_gc_memory, size_t post_gc_memory) {
  live_memory_ = post_gc_memory;
  last_M_update_time_ =
      heap_->MonotonicallyIncreasingTimeInMs() * kMillisecondsToNanoseconds;
  last_M_memory_ = post_gc_memory;
  if (heap_->tracer()->major_gc_time) {
    UpdateLiveMemoryMajorGC(
        post_gc_memory, pre_gc_memory,
        (heap_->tracer()->major_gc_time.value() + concurrent_gc_time_) *
            kSecondsToNanoseconds);
    heap_->tracer()->major_gc_time.reset();
  }
  if (heap_->tracer()->major_allocation_bytes_and_duration) {
    UpdateMajorAllocation(
        heap_->tracer()->major_allocation_bytes_and_duration.value().first,
        heap_->tracer()->major_allocation_bytes_and_duration.value().second *
            kSecondsToNanoseconds);
    heap_->tracer()->major_allocation_bytes_and_duration.reset();
  }
  Update();
  if (!allocation_measurer_started) {
    allocation_measurer_started = true;
    PostMemoryMeasurementTask();
  }
}

MemoryMeasurementTask::MemoryMeasurementTask(Heap* heap)
    : CancelableTask(heap->isolate()), heap_(heap) {}

void MemoryMeasurementTask::RunInternal() {
  auto time = heap_->MonotonicallyIncreasingTimeInMs() *
              MemoryBalancer::kMillisecondsToNanoseconds;
  auto memory = heap_->OldGenerationSizeOfObjects() +
                heap_->AllocatedExternalMemorySinceMarkCompact();
  heap_->mb->UpdateMajorAllocation(
      std::max<double>(0, memory - heap_->mb->last_M_memory_),
      time - heap_->mb->last_M_update_time_);
  heap_->mb->last_M_update_time_ = time;
  heap_->mb->last_M_memory_ = memory;
  heap_->mb->Update();
  heap_->mb->PostMemoryMeasurementTask();
}

}  // namespace internal
}  // namespace v8
