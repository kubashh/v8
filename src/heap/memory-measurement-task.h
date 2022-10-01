// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_MEASUREMENT_TASK_H_
#define V8_HEAP_MEMORY_MEASUREMENT_TASK_H_

#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

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

#endif  // V8_HEAP_MEMORY_MEASUREMENT_TASK_H_
