// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/default-worker-threads-task-runner.h"

#include <vector>

#include "include/v8-platform.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/semaphore.h"
#include "src/base/platform/time.h"
#include "src/libplatform/worker-thread.h"
#include "testing/gtest-support.h"

namespace v8 {
namespace platform {

class TestTask : public v8::Task {
 public:
  explicit TestTask(std::function<void()> f) : f_(std::move(f)) {}

  void Run() override { f_(); }

 private:
  std::function<void()> f_;
};

double RealTime() {
  return base::TimeTicks::HighResolutionNow().ToInternalValue() /
         static_cast<double>(base::Time::kMicrosecondsPerSecond);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, PostTaskOrder) {
  DefaultWorkerThreadsTaskRunner runner(1, RealTime);

  std::vector<int> order;
  base::Semaphore semaphore(0);

  std::unique_ptr<TestTask> task1 =
      base::make_unique<TestTask>([&] { order.push_back(1); });
  std::unique_ptr<TestTask> task2 =
      base::make_unique<TestTask>([&] { order.push_back(2); });
  std::unique_ptr<TestTask> task3 = base::make_unique<TestTask>([&] {
    order.push_back(3);
    semaphore.Signal();
  });

  runner.PostTask(std::move(task1));
  runner.PostTask(std::move(task2));
  runner.PostTask(std::move(task3));

  semaphore.Wait();

  runner.Terminate();
  ASSERT_EQ(3UL, order.size());
  ASSERT_EQ(1, order[0]);
  ASSERT_EQ(2, order[1]);
  ASSERT_EQ(3, order[2]);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, PostDelayedTaskOrder) {
  DefaultWorkerThreadsTaskRunner runner(1, RealTime);

  std::vector<int> order;
  base::Semaphore semaphore(0);

  std::unique_ptr<TestTask> task1 = base::make_unique<TestTask>([&] {
    order.push_back(1);
    semaphore.Signal();
  });
  std::unique_ptr<TestTask> task2 =
      base::make_unique<TestTask>([&] { order.push_back(2); });
  std::unique_ptr<TestTask> task3 =
      base::make_unique<TestTask>([&] { order.push_back(3); });

  runner.PostDelayedTask(std::move(task1), 0.1);
  runner.PostTask(std::move(task2));
  runner.PostTask(std::move(task3));

  semaphore.Wait();

  runner.Terminate();
  ASSERT_EQ(3UL, order.size());
  ASSERT_EQ(2, order[0]);
  ASSERT_EQ(3, order[1]);
  ASSERT_EQ(1, order[2]);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, PostDelayedTaskOrder2) {
  DefaultWorkerThreadsTaskRunner runner(1, RealTime);

  std::vector<int> order;
  base::Semaphore semaphore(0);

  std::unique_ptr<TestTask> task1 = base::make_unique<TestTask>([&] {
    order.push_back(1);
    semaphore.Signal();
  });
  std::unique_ptr<TestTask> task2 =
      base::make_unique<TestTask>([&] { order.push_back(2); });
  std::unique_ptr<TestTask> task3 =
      base::make_unique<TestTask>([&] { order.push_back(3); });

  runner.PostDelayedTask(std::move(task1), 0.3);
  runner.PostDelayedTask(std::move(task2), 0.1);
  runner.PostDelayedTask(std::move(task3), 0.2);

  semaphore.Wait();

  runner.Terminate();
  ASSERT_EQ(3UL, order.size());
  ASSERT_EQ(2, order[0]);
  ASSERT_EQ(3, order[1]);
  ASSERT_EQ(1, order[2]);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, PostAfterTerminate) {
  DefaultWorkerThreadsTaskRunner runner(1, RealTime);

  std::vector<int> order;
  base::Semaphore task1_semaphore(0);
  base::Semaphore task2_semaphore(0);
  base::Semaphore task3_semaphore(0);

  std::unique_ptr<TestTask> task1 = base::make_unique<TestTask>([&] {
    order.push_back(1);
    task1_semaphore.Signal();
  });
  std::unique_ptr<TestTask> task2 = base::make_unique<TestTask>([&] {
    order.push_back(2);
    task2_semaphore.Signal();
  });
  std::unique_ptr<TestTask> task3 = base::make_unique<TestTask>([&] {
    order.push_back(3);
    task3_semaphore.Signal();
  });

  runner.PostTask(std::move(task1));
  runner.PostDelayedTask(std::move(task2), 0.1);

  task1_semaphore.Wait();
  ASSERT_EQ(1UL, order.size());
  ASSERT_EQ(1, order[0]);

  runner.Terminate();
  bool signalled =
      task2_semaphore.WaitFor(base::TimeDelta::FromMilliseconds(200));
  ASSERT_FALSE(signalled);
  ASSERT_EQ(1UL, order.size());
  ASSERT_EQ(1, order[0]);

  runner.PostTask(std::move(task3));
  signalled = task3_semaphore.WaitFor(base::TimeDelta::FromMilliseconds(100));
  ASSERT_FALSE(signalled);
  ASSERT_EQ(1UL, order.size());
  ASSERT_EQ(1, order[0]);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, NoIdleTasks) {
  DefaultWorkerThreadsTaskRunner runner(1, RealTime);

  ASSERT_FALSE(runner.IdleTasksEnabled());
  runner.Terminate();
}

}  // namespace platform
}  // namespace v8
