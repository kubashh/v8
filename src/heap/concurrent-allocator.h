// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CONCURRENT_ALLOCATOR_H_
#define V8_HEAP_CONCURRENT_ALLOCATOR_H_

#include "src/common/globals.h"
#include "src/heap/heap.h"
#include "src/heap/spaces.h"
#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

class LocalHeap;

class StressConcurrentAllocatorTask : public CancelableTask {
 public:
  explicit StressConcurrentAllocatorTask(Isolate* isolate)
      : CancelableTask(isolate), isolate_(isolate) {}

  void RunInternal() override;

  // Schedules task on background thread
  static void Schedule(Isolate* isolate);

 private:
  Isolate* isolate_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CONCURRENT_ALLOCATOR_H_
