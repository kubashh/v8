// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_DELAYED_TASK_QUEUE_H_
#define V8_LIBPLATFORM_DELAYED_TASK_QUEUE_H_

#include <map>

#include "include/libplatform/libplatform-export.h"
#include "src/libplatform/task-queue.h"

namespace v8 {

class Task;

namespace platform {

// DelayedTaskQueue extends TaskQueue by providing queueing for delayed tasks
// that interleave with immediate tasks. It does not provide any guarantees
// about ordering of tasks except that it holds the ordering guarantee for
// immediate tasks that TaskQueue::Append() provides.
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
  // Returns nullptr if the queue is terminated. Will return either an immediate
  // task posted using Append() or a delayed task where the deadline has passed,
  // according to the |time_function| provided in the constructor.
  std::unique_ptr<Task> GetNext() override;

 private:
  std::unique_ptr<Task> PopTaskFromDelayedQueue();

  std::multimap<double, std::unique_ptr<Task>> delayed_task_queue_;
  TimeFunction time_function_;

  DISALLOW_COPY_AND_ASSIGN(DelayedTaskQueue);
};

}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_DELAYED_TASK_QUEUE_H_
