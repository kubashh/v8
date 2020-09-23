// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_HEAP_CPPGC_TEST_PLATFORM_H_
#define V8_UNITTESTS_HEAP_CPPGC_TEST_PLATFORM_H_

#include <memory>
#include <vector>

#include "include/cppgc/platform.h"
#include "src/base/page-allocator.h"
#include "src/base/platform/platform.h"

namespace cppgc {
namespace internal {
namespace testing {

class TestJob;

class TestTaskRunner : public v8::TaskRunner {
 public:
  void PostTask(std::unique_ptr<v8::Task> task) override;
  void PostDelayedTask(std::unique_ptr<v8::Task> task, double) override;

  bool NonNestableTasksEnabled() const override { return true; }
  void PostNonNestableTask(std::unique_ptr<v8::Task> task) override;

  bool NonNestableDelayedTasksEnabled() const override { return true; }
  void PostNonNestableDelayedTask(std::unique_ptr<v8::Task> task,
                                  double) override;

  bool IdleTasksEnabled() override { return true; }
  void PostIdleTask(std::unique_ptr<v8::IdleTask> task) override;

  bool RunSingleTask();
  bool RunSingleIdleTask(double duration_in_seconds);

  void RunUntilIdle();

 private:
  std::vector<std::unique_ptr<v8::Task>> tasks_;
  std::vector<std::unique_ptr<v8::IdleTask>> idle_tasks_;
};

class TestPlatform : public Platform {
 public:
  class DisableBackgroundTasksScope {
   public:
    explicit DisableBackgroundTasksScope(TestPlatform*);
    ~DisableBackgroundTasksScope() V8_NOEXCEPT;

   private:
    TestPlatform* platform_;
  };

  TestPlatform();
  ~TestPlatform() V8_NOEXCEPT override;

  PageAllocator* GetPageAllocator() override { return &page_allocator_; }

  std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner() override {
    return foreground_task_runner_;
  }

  // TestPlatform does not support job priorities. All jobs would be assigned
  // the same priority regardless of the v8::TaskPriority parameter.
  std::unique_ptr<v8::JobHandle> PostJob(
      v8::TaskPriority, std::unique_ptr<v8::JobTask> job_task) override;

  double MonotonicallyIncreasingTime() override;

  void WaitAllForegroundTasks();
  void WaitAllBackgroundTasks();

 private:
  bool AreBackgroundTasksDisabled() const {
    return disabled_background_tasks_ > 0;
  }

  v8::base::PageAllocator page_allocator_;
  std::shared_ptr<TestTaskRunner> foreground_task_runner_;
  std::vector<std::shared_ptr<TestJob>> jobs_;
  size_t disabled_background_tasks_ = 0;
};

}  // namespace testing
}  // namespace internal
}  // namespace cppgc

#endif  // V8_UNITTESTS_HEAP_CPPGC_TEST_PLATFORM_H_
