// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/platform/platform.h"
#include "src/heap/barrier.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {
namespace heap {

TEST(OneshotBarrier, InitializeNotDone) {
  OneshotBarrier barrier;
  EXPECT_FALSE(barrier.Done());
}

TEST(OneshotBarrier, DoneAfterWait_Sequential) {
  OneshotBarrier barrier;
  barrier.Start();
  barrier.Wait();
  EXPECT_TRUE(barrier.Done());
}

namespace {

class ThreadWaitingOnBarrier final : public base::Thread {
 public:
  ThreadWaitingOnBarrier()
      : base::Thread(Options("ThreadWaitingOnBarrier")),
        barrier_(nullptr) {}

  void Initialize(OneshotBarrier* barrier) {
    barrier_ = barrier;
  }

  void Run() final {
    barrier_->Wait();
  }
 private:
  OneshotBarrier* barrier_;
};

}  // namespace

TEST(OneshotBarrier, DoneAfterWait_Concurrent) {
  const int kThreadCount = 2;
  OneshotBarrier barrier;
  ThreadWaitingOnBarrier threads[kThreadCount];
  for (int i = 0; i < kThreadCount; i++) {
    threads[i].Initialize(&barrier);
    // All threads need to call Wait() to be done.
    barrier.Start();
  }
  for (int i = 0; i < kThreadCount; i++) {
    threads[i].Start();
  }
  for (int i = 0; i < kThreadCount; i++) {
    threads[i].Join();
  }
  EXPECT_TRUE(barrier.Done());
}

TEST(OneshotBarrier, EarlyFinish_Concurrent) {
  const int kThreadCount = 2;
  OneshotBarrier barrier;
  ThreadWaitingOnBarrier threads[kThreadCount];
  // Test that one thread that actually finishes processing work before other
  // threads call Start() will move the barrier in Done state.
  barrier.Start();
  barrier.Wait();
  EXPECT_TRUE(barrier.Done());
  for (int i = 0; i < kThreadCount; i++) {
    threads[i].Initialize(&barrier);
    // All threads need to call Wait() to be done.
    barrier.Start();
  }
  for (int i = 0; i < kThreadCount; i++) {
    threads[i].Start();
  }
  for (int i = 0; i < kThreadCount; i++) {
    threads[i].Join();
  }
  EXPECT_TRUE(barrier.Done());
}

namespace {

class CountingThread final : public base::Thread {
 public:
  CountingThread()
      : base::Thread(Options("CountingThread")),
        barrier_(0),
        counter_(nullptr),
        even_(false),
        wakeups_(0) {}

  void Initialize(OneshotBarrier* barrier, base::Mutex* mutex, size_t* counter,
   size_t limit, bool even) {
    barrier_ = barrier;
    mutex_ = mutex;
    counter_ = counter;
    limit_ = limit;
    even_ = even;
  }

  void Run() final {
    while (true) {
      mutex_->Lock();
      if (*counter_ >= limit_) {
        break;
      } else if ((even_ && ((*counter_) % 2 == 0)) || 
          (!even_ && ((*counter_) % 2 == 1))) {
        *counter_ += 1;
        barrier_->NotifyAll();
      }
      mutex_->Unlock();
      barrier_->Wait();
      wakeups_++;
    }
    //barrier_->Wait();
  }

  size_t wakeups() const { return wakeups_; }
 private:
  OneshotBarrier* barrier_;
  base::Mutex* mutex_;
  size_t* counter_;
  size_t limit_;
  bool even_;
  size_t wakeups_;
};

}  // namespace

TEST(OneshotBarrier, Wakeups_Concurrent) {
  const size_t kCounterLimit = 173;
  OneshotBarrier barrier;
  base::Mutex mutex;
  size_t counter = 0;
  CountingThread even_counting_thread;
  CountingThread odd_counting_thread;
  even_counting_thread.Initialize(&barrier,&mutex, &counter, kCounterLimit, true);
  odd_counting_thread.Initialize(&barrier,&mutex, &counter, kCounterLimit, false);
  barrier.Start();
  barrier.Start();
  EXPECT_FALSE(barrier.Done());
  even_counting_thread.Start();
  odd_counting_thread.Start();
  even_counting_thread.Join();
  odd_counting_thread.Join();
  EXPECT_TRUE(barrier.Done());
  printf("even: %zu\n", even_counting_thread.wakeups());
  printf("odd: %zu\n", odd_counting_thread.wakeups());
}

}  // namespace heap
}  // namespace internal
}  // namespace v8