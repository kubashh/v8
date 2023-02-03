// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-balancer.h"

#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"

namespace v8 {
namespace internal {

void MemoryBalancer::Update() {
  if (v8_flags.memory_balancer && major_allocation_ && major_gc_) {
    size_t new_limit =
        live_memory_ +
        sqrt(live_memory_ * (major_allocation_.value().rate()) /
             (major_gc_.value().rate()) / v8_flags.memory_balancer_c_value);
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
  heap_->set_old_generation_allocation_limit(new_limit);
  heap_->global_allocation_limit_ = new_limit + external_allocation_limit_;
}

void MemoryBalancer::PostMemoryMeasurementTask() {
  auto taskrunner = V8::GetCurrentPlatform()->GetForegroundTaskRunner(
      reinterpret_cast<v8::Isolate*>(heap_->isolate()));
  taskrunner->PostDelayedTask(std::make_unique<MemoryMeasurementTask>(this), 1);
}

void MemoryBalancer::UpdateLiveMemory(size_t live_memory) {
  live_memory_ = live_memory;
}

void MemoryBalancer::UpdateMajorGC(double major_gc_bytes,
                                   double major_gc_duration) {
  major_gc_duration *= kSecondsToNanoseconds;
  if (!major_gc_) {
    major_gc_ = SmoothedBytesAndDuration{major_gc_bytes, major_gc_duration};
  } else {
    major_gc_->Update(major_gc_bytes, major_gc_duration, 0.5);
  }
}

void MemoryBalancer::UpdateMajorAllocation(double major_allocation_bytes,
                                           double major_allocation_duration) {
  major_allocation_bytes *= kSecondsToNanoseconds;
  if (!major_allocation_) {
    major_allocation_ = SmoothedBytesAndDuration{major_allocation_bytes,
                                                 major_allocation_duration};
  } else {
    major_allocation_->Update(major_allocation_bytes, major_allocation_duration,
                              0.95);
  }
}

void MemoryBalancer::NotifyGC() {
  last_m_update_time_ =
      heap_->MonotonicallyIncreasingTimeInMs() * kMillisecondsToNanoseconds;
  last_m_memory_ = heap_->OldGenerationSizeOfObjects();
  if (!allocation_measurer_started) {
    allocation_measurer_started = true;
    PostMemoryMeasurementTask();
  }
}

MemoryMeasurementTask::MemoryMeasurementTask(MemoryBalancer* mb)
    : CancelableTask(mb->heap_->isolate()), mb_(mb) {}

void MemoryMeasurementTask::RunInternal() {
  auto time = mb_->heap_->MonotonicallyIncreasingTimeInMs() *
              MemoryBalancer::kMillisecondsToNanoseconds;
  auto memory = mb_->heap_->OldGenerationSizeOfObjects() +
                mb_->heap_->AllocatedExternalMemorySinceMarkCompact();
  mb_->UpdateMajorAllocation(std::max<double>(0, memory - mb_->last_m_memory_),
                             time - mb_->last_m_update_time_);
  mb_->last_m_update_time_ = time;
  mb_->last_m_memory_ = memory;
  mb_->Update();
  mb_->PostMemoryMeasurementTask();
}

}  // namespace internal
}  // namespace v8
