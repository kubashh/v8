// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/task-queue.h"

#include "include/v8-platform.h"
#include "src/base/logging.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"

namespace v8 {
namespace platform {

TaskQueue::TaskQueue() : terminated_(false) {}

TaskQueue::~TaskQueue() {
  base::MutexGuard guard(&lock_);
  DCHECK(terminated_);
  DCHECK(task_queue_.empty());
}

void TaskQueue::Append(std::unique_ptr<Task> task) {
  base::MutexGuard guard(&lock_);
  DCHECK(!terminated_);
  task_queue_.push(std::move(task));
  process_queue_condition_var_.NotifyOne();
}

std::unique_ptr<Task> TaskQueue::GetNext() {
  for (;;) {
    {
      base::MutexGuard guard(&lock_);
      if (!task_queue_.empty()) {
        std::unique_ptr<Task> result = std::move(task_queue_.front());
        task_queue_.pop();
        return result;
      }
      if (terminated_) {
        process_queue_condition_var_.NotifyAll();
        return nullptr;
      }
    }
    process_queue_condition_var_.Wait(&lock_);
  }
}


void TaskQueue::Terminate() {
  base::MutexGuard guard(&lock_);
  DCHECK(!terminated_);
  terminated_ = true;
  process_queue_condition_var_.NotifyAll();
}

void TaskQueue::BlockUntilQueueEmptyForTesting() {
  for (;;) {
    {
      base::MutexGuard guard(&lock_);
      if (task_queue_.empty()) return;
    }
    base::OS::Sleep(base::TimeDelta::FromMilliseconds(5));
  }
}

}  // namespace platform
}  // namespace v8
