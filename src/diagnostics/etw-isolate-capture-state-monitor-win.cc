// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/diagnostics/etw-isolate-capture-state-monitor-win.h"

#include <windows.h>

#include <iostream>

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"

namespace v8 {
namespace internal {

namespace {
class Debug {
 public:
  static Debug info;
};
Debug Debug::info;

std::ostream& operator<<(std::ostream& os, const Debug&) {
  os << "PID:" << ::GetCurrentProcessId() << "; TID:" << ::GetCurrentThreadId()
     << " ";
  return os;
}
}  // namespace

EtwIsolateCaptureStateMonitor::EtwIsolateCaptureStateMonitor(
    base::Mutex* mutex, size_t isolate_count)
    : mutex_(mutex), isolate_count_(isolate_count) {}

bool EtwIsolateCaptureStateMonitor::WaitFor(const base::TimeDelta& delta) {
  wait_started_ = base::TimeTicks::Now();
  base::TimeDelta remaining = delta;

  std::cout << Debug::info << "Waiting for " << isolate_count_
            << " isolates for up to " << remaining.InMilliseconds()
            << std::endl;
  while (cv_.WaitFor(mutex_, remaining)) {
    std::cout << Debug::info << "WaitFor woke up: " << isolate_count_
              << " isolates remaining " << std::endl;
    // If the predicate is satisfied, return true.
    if (isolate_count_ == 0) {
      return true;
    }

    // If the timeout has expired, return false.
    auto elapsed = base::TimeTicks::Now() - wait_started_;
    if (elapsed >= remaining) {
      std::cout << Debug::info << "Elapsed is " << elapsed.InMilliseconds()
                << " greater than reminaing " << remaining.InMilliseconds()
                << std::endl;
      return false;
    }

    // If the condition variable was woken up spuriously, adjust the timeout.
    remaining -= elapsed;
    std::cout << Debug::info << "New remaining " << remaining.InMilliseconds()
              << " resuming waiting" << std::endl;
  }

  // Propagate the WaitFor false return value (timeout before being notified) to
  // the caller.
  return false;
}

void EtwIsolateCaptureStateMonitor::Notify() {
  {
    std::cout << Debug::info << "Notify taking mutex"
              << " thread: " << ::GetCurrentThreadId() << std::endl;
    base::MutexGuard lock(mutex_);
    isolate_count_--;
    std::cout << Debug::info << "Got mutex and isolate count reduced to "
              << isolate_count_ << std::endl;
  }
  std::cout << Debug::info << "Released mutex preparing to notifyOne "
            << std::endl;
  cv_.NotifyOne();
  std::cout << Debug::info << "Finished notifyOne " << std::endl;
}

}  // namespace internal
}  // namespace v8
