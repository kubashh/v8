// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_DELAYED_TASK_QUEUE_H_
#define V8_LIBPLATFORM_DELAYED_TASK_QUEUE_H_

#include <map>

#include "include/libplatform/libplatform-export.h"
#include "src/libplatform/task-queue.h"
#include "testing/gtest/include/gtest/gtest_prod.h"

namespace v8 {

class Task;

namespace platform {

class V8_PLATFORM_EXPORT DelayedTaskQueue : public TaskQueue {
 public:
  using TimeFunction = double (*)();

  explicit DelayedTaskQueue(TimeFunction time_function);
  ~DelayedTaskQueue() = default;

  double MonotonicallyIncreasingTime();

  // Appends a delayed task to the queue. There is no ordering guarantee
  // provided regarding delayed tasks, both with respect to other delayed tasks
  // and non-delayed tasks that were appended using Append().
  void AppendDelayed(std::unique_ptr<Task> task, double delay_in_seconds);

  // Returns the next task to process. Blocks if no task is available.
  // Returns nullptr if the queue is terminated.
  std::unique_ptr<Task> GetNext() override;

 private:
  friend class DefaultWorkerThreadsTaskRunner;
  void BlockUntilQueueEmptyForTesting();

  std::unique_ptr<Task> PopTaskFromDelayedQueue();

  std::multimap<double, std::unique_ptr<Task>> delayed_task_queue_;
  TimeFunction time_function_;

  DISALLOW_COPY_AND_ASSIGN(DelayedTaskQueue);
};

}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_DELAYED_TASK_QUEUE_H_
