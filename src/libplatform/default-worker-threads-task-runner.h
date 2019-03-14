// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_DEFAULT_WORKER_THREADS_TASK_RUNNER_H_
#define V8_LIBPLATFORM_DEFAULT_WORKER_THREADS_TASK_RUNNER_H_

#include "include/v8-platform.h"
#include "src/libplatform/delayed-task-queue.h"
#include "testing/gtest/include/gtest/gtest_prod.h"

namespace v8 {
namespace platform {

class Thread;

class WorkerThread;

class V8_PLATFORM_EXPORT DefaultWorkerThreadsTaskRunner
    : public NON_EXPORTED_BASE(TaskRunner) {
 public:
  using TimeFunction = double (*)();

  DefaultWorkerThreadsTaskRunner(uint32_t thread_pool_size,
                                 TimeFunction time_function);

  ~DefaultWorkerThreadsTaskRunner() override;

  void Terminate();

  double MonotonicallyIncreasingTime();

  // v8::TaskRunner implementation.
  void PostTask(std::unique_ptr<Task> task) override;

  void PostDelayedTask(std::unique_ptr<Task> task,
                       double delay_in_seconds) override;

  void PostIdleTask(std::unique_ptr<IdleTask> task) override;

  bool IdleTasksEnabled() override;

 private:
  FRIEND_TEST(DefaultWorkerThreadsTaskRunnerUnittest, PostTaskOrder);
  FRIEND_TEST(DefaultWorkerThreadsTaskRunnerUnittest, PostDelayedTaskOrder);
  FRIEND_TEST(DefaultWorkerThreadsTaskRunnerUnittest, PostDelayedTaskOrder2);
  FRIEND_TEST(DefaultWorkerThreadsTaskRunnerUnittest, PostAfterTerminate);

  void BlockUntilTasksCompleteForTesting();

  bool terminated_ = false;
  base::Mutex lock_;
  DelayedTaskQueue queue_;
  std::vector<std::unique_ptr<WorkerThread>> thread_pool_;
  TimeFunction time_function_;
};

}  // namespace platform
}  // namespace v8
#endif  // V8_LIBPLATFORM_DEFAULT_WORKER_THREADS_TASK_RUNNER_H_
