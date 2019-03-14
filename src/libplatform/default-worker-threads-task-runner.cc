// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/default-worker-threads-task-runner.h"

#include "src/base/platform/mutex.h"
#include "src/libplatform/worker-thread.h"

namespace v8 {
namespace platform {

DefaultWorkerThreadsTaskRunner::DefaultWorkerThreadsTaskRunner(
    uint32_t thread_pool_size, TimeFunction time_function)
    : queue_(time_function), time_function_(time_function) {
  for (uint32_t i = 0; i < thread_pool_size; ++i) {
    thread_pool_.push_back(base::make_unique<WorkerThread>(&queue_));
  }
}

DefaultWorkerThreadsTaskRunner::~DefaultWorkerThreadsTaskRunner() = default;

double DefaultWorkerThreadsTaskRunner::MonotonicallyIncreasingTime() {
  return time_function_();
}

void DefaultWorkerThreadsTaskRunner::Terminate() {
  base::MutexGuard guard(&lock_);
  terminated_ = true;
  queue_.Terminate();
  // Clearing the thread pool lets all worker threads join.
  thread_pool_.clear();
}

void DefaultWorkerThreadsTaskRunner::PostTask(std::unique_ptr<Task> task) {
  base::MutexGuard guard(&lock_);
  if (terminated_) return;
  queue_.Append(std::move(task));
}

void DefaultWorkerThreadsTaskRunner::PostDelayedTask(std::unique_ptr<Task> task,
                                                     double delay_in_seconds) {
  base::MutexGuard guard(&lock_);
  if (terminated_) return;
  queue_.AppendDelayed(std::move(task), delay_in_seconds);

  // DCHECK_GE(delay_in_seconds, 0.0);
  // base::MutexGuard guard(&lock_);
  // if (terminated_) return;
  // double deadline = MonotonicallyIncreasingTime() + delay_in_seconds;
  // delayed_task_queue_.push(std::make_pair(deadline, std::move(task)));
  // queue_.AppendDelayed(std::move(task), deadline);
}

void DefaultWorkerThreadsTaskRunner::PostIdleTask(
    std::unique_ptr<IdleTask> task) {
  // There are no idle worker tasks.
  UNREACHABLE();
}

bool DefaultWorkerThreadsTaskRunner::IdleTasksEnabled() {
  // There are no idle worker tasks.
  return false;
}

void DefaultWorkerThreadsTaskRunner::BlockUntilTasksCompleteForTesting() {
  queue_.BlockUntilQueueEmptyForTesting();
}

}  // namespace platform
}  // namespace v8
