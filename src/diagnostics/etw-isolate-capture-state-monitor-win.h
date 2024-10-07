// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DIAGNOSTICS_ETW_ISOLATE_CAPTURE_STATE_MONITOR_WIN_H_
#define V8_DIAGNOSTICS_ETW_ISOLATE_CAPTURE_STATE_MONITOR_WIN_H_

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"

namespace v8 {
namespace internal {

// This class allows the thread that receives callbacks for the v8 ETW provider
// to wait for isolates to emit the state necessary to decode JS stacks in ETW
// when state capture is requested.
class EtwIsolateCaptureStateMonitor {
 public:
  EtwIsolateCaptureStateMonitor(base::Mutex* mutex, size_t isolate_count);
  EtwIsolateCaptureStateMonitor(const EtwIsolateCaptureStateMonitor&) = delete;
  EtwIsolateCaptureStateMonitor& operator=(
      const EtwIsolateCaptureStateMonitor&) = delete;

  // Waits for the specified time delta or until Notify is called isolate_count
  // times, whichever occurs first. Returns true if Notify was called
  // isolate_count times, false if the timeout expired. Mutex must be owned
  // prior to calling this method. Unlocks the mutex and waits this thread on
  // the internal condition variable. Upon waking, reacquires the mutex to check
  // if the internal isolate_count has reached 0.  If so, returns true;
  // otherwise, goes back to waiting on the condition variable.
  bool WaitFor(const base::TimeDelta& delta);

  // Called by an isolate thread after it emits state necessary to decode JS
  // stacks in ETW. Acquires the mutex to update the internal isolate_count_ and
  // then releases it to notify the underlying condition variable and wake up
  // one waiting thread.
  void Notify();

 private:
  // Must be held prior to calling WaitFor.
  // Also used to sychronize access when reading/writing the isolate_count_.
  base::Mutex* mutex_;
  // Tracks the number of isolates which have yet to the emit state necessary to
  // decode JS stacks in ETW.
  size_t isolate_count_;
  // Used to signal when all isolates have emitted the state necessary to decode
  // JS stacks in ETW.
  base::ConditionVariable cv_;
  // Used to track when WaitFor started and how much of the original timeout
  // remains when recovering from spurious wakeups.
  base::TimeTicks wait_started_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DIAGNOSTICS_ETW_ISOLATE_CAPTURE_STATE_MONITOR_WIN_H_
