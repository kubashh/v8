// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_FUTEX_EMULATION_H_
#define V8_EXECUTION_FUTEX_EMULATION_H_

#include <stdint.h>

#include <set>

#include "include/v8.h"
#include "src/base/atomicops.h"
#include "src/base/lazy-instance.h"
#include "src/base/macros.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/utils/allocation.h"

// Support for emulating futexes, a low-level synchronization primitive. They
// are natively supported by Linux, but must be emulated for other platforms.
// This library emulates them on all platforms using mutexes and condition
// variables for consistency.
//
// This is used by the Futex API defined in the SharedArrayBuffer draft spec,
// found here: https://github.com/tc39/ecmascript_sharedmem

namespace v8 {

namespace base {
class TimeDelta;
}  // namespace base

namespace internal {

class BackingStore;
template <typename T>
class Handle;
class Isolate;
class JSArrayBuffer;

class AtomicsWaitWakeHandle {
 public:
  explicit AtomicsWaitWakeHandle(Isolate* isolate) : isolate_(isolate) {}

  void Wake();
  inline bool has_stopped() const { return stopped_; }

 private:
  Isolate* isolate_;
  bool stopped_ = false;
};

class FutexWaitListNode {
 public:
  explicit FutexWaitListNode(Isolate* isolate = nullptr)
      : isolate_for_async_waiters_(isolate) {
    if (isolate) {
      task_runner_ = V8::GetCurrentPlatform()->GetForegroundTaskRunner(
          reinterpret_cast<v8::Isolate*>(isolate));
    }
  }

  void NotifyWake();

  void Notify(bool woken_up);

 private:
  friend class FutexEmulation;
  friend class FutexWaitList;
  friend class ResetWaitingOnScopeExit;

  // Set only for async FutexWaitListNodes.
  Isolate* isolate_for_async_waiters_;
  std::shared_ptr<TaskRunner> task_runner_;

  base::ConditionVariable cond_;
  // prev_, next_ async_timeout_prev_, and async_timeout_next_ are protected by
  // FutexEmulation::mutex_.
  FutexWaitListNode* prev_ = nullptr;
  FutexWaitListNode* next_ = nullptr;

  // For maintaining a linked list for async FutexWaitListNodes, ordered by
  // ascending timeout.
  FutexWaitListNode* async_timeout_prev_ = nullptr;
  FutexWaitListNode* async_timeout_next_ = nullptr;

  std::weak_ptr<BackingStore> backing_store_;
  size_t wait_addr_ = 0;
  // waiting_ and interrupted_ are protected by FutexEmulation::mutex_
  // if this node is currently contained in FutexEmulation::wait_list_
  // or an AtomicsWaitWakeHandle has access to it.
  bool waiting_ = false;
  bool interrupted_ = false;

  // Only for async FutexWaitListNodes. Weak Persistent handle. Must not be
  // synchronously resolved by a non-owner Isolate.
  v8::Persistent<v8::Promise> promise_;

  // Only for async FutexWaitListNodes. Weak Persistent handle.
  v8::Persistent<v8::Context> native_context_;

  // Only for async FutexWaitListNodes. Used for processing async timeouts.
  base::TimeTicks timeout_time_;

  DISALLOW_COPY_AND_ASSIGN(FutexWaitListNode);
};

class FutexWaitList {
 public:
  FutexWaitList() {}

  void AddNode(FutexWaitListNode* node);
  void RemoveNode(FutexWaitListNode* node);

  // Adds 'node' to the async timeout list which is sorted in ascending order of
  // the timeout. Returns true if the added node has the lowest timeout.
  bool AddNodeToAsyncTimeoutList(FutexWaitListNode* node);

 private:
  friend class FutexEmulation;

  FutexWaitListNode* head_ = nullptr;
  FutexWaitListNode* tail_ = nullptr;

  FutexWaitListNode* async_timeout_head_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(FutexWaitList);
};

class ResetWaitingOnScopeExit {
 public:
  explicit ResetWaitingOnScopeExit(FutexWaitListNode* node) : node_(node) {}
  ~ResetWaitingOnScopeExit() { node_->waiting_ = false; }

 private:
  FutexWaitListNode* node_;

  DISALLOW_COPY_AND_ASSIGN(ResetWaitingOnScopeExit);
};

class FutexEmulation : public AllStatic {
 public:
  enum WaitMode { kSync = 0, kAsync };

  // Pass to Wake() to wake all waiters.
  static const uint32_t kWakeAll = UINT32_MAX;

  // Check that array_buffer[addr] == value, and return "not-equal" if not. If
  // they are equal, block execution on |isolate|'s thread until woken via
  // |Wake|, or when the time given in |rel_timeout_ms| elapses. Note that
  // |rel_timeout_ms| can be Infinity.
  // If woken, return "ok", otherwise return "timed-out". The initial check and
  // the decision to wait happen atomically.
  static Object WaitJs32(Isolate* isolate, WaitMode mode,
                         Handle<JSArrayBuffer> array_buffer, size_t addr,
                         int32_t value, double rel_timeout_ms);

  // An version of WaitJs32 for int64_t values.
  static Object WaitJs64(Isolate* isolate, WaitMode mode,
                         Handle<JSArrayBuffer> array_buffer, size_t addr,
                         int64_t value, double rel_timeout_ms);

  // Same as WaitJs above except it returns 0 (ok), 1 (not equal) and 2 (timed
  // out) as expected by Wasm.
  static Object WaitWasm32(Isolate* isolate, Handle<JSArrayBuffer> array_buffer,
                           size_t addr, int32_t value, int64_t rel_timeout_ns);

  // Same as Wait32 above except it checks for an int64_t value in the
  // array_buffer.
  static Object WaitWasm64(Isolate* isolate, Handle<JSArrayBuffer> array_buffer,
                           size_t addr, int64_t value, int64_t rel_timeout_ns);

  // Wake |num_waiters_to_wake| threads that are waiting on the given |addr|.
  // |num_waiters_to_wake| can be kWakeAll, in which case all waiters are
  // woken. The rest of the waiters will continue to wait. The return value is
  // the number of woken waiters.
  static Object Wake(Handle<JSArrayBuffer> array_buffer, size_t addr,
                     uint32_t num_waiters_to_wake);

  // Resolve the Promises of the async waiters which belong to |isolate| and
  // are no longer waiting.
  static void ResolveAsyncWaiterPromises(Isolate* isolate);

  // Find timed out async waiters and schedule tasks for resolving their
  // Promises.
  static void HandleAsyncWaiterTimeouts();

  // Cleanup async waiters related to |isolate|.
  static void Cleanup(Isolate* isolate);

  // Return the number of threads or async waiters waiting on |addr|. Should
  // only be used for testing.
  static Object NumWaitersForTesting(Handle<JSArrayBuffer> array_buffer,
                                     size_t addr);

  // Return the total number of threads or async waiters waiting. Should only be
  // used for testing.
  static Object NumWaitersForTesting();

  // Return the number of async waiters which were waiting for |addr| and are
  // now waiting for the Promises to be resolved. Should only be used for
  // testing.
  static Object NumUnresolvedAsyncPromisesForTesting(
      Handle<JSArrayBuffer> array_buffer, size_t addr);

  static void VerifyNode(FutexWaitListNode* node);
  static void VerifyFutexWaitList();

 private:
  friend class FutexWaitListNode;
  friend class AtomicsWaitWakeHandle;

  template <typename T>
  static Object Wait(Isolate* isolate, WaitMode mode,
                     Handle<JSArrayBuffer> array_buffer, size_t addr, T value,
                     double rel_timeout_ms);

  template <typename T>
  static Object Wait(Isolate* isolate, WaitMode mode,
                     Handle<JSArrayBuffer> array_buffer, size_t addr, T value,
                     bool use_timeout, int64_t rel_timeout_ns);

  template <typename T>
  static Object WaitSync(Isolate* isolate, Handle<JSArrayBuffer> array_buffer,
                         size_t addr, T value, bool use_timeout,
                         int64_t rel_timeout_ns);

  template <typename T>
  static Object WaitAsync(Isolate* isolate, Handle<JSArrayBuffer> array_buffer,
                          size_t addr, T value, bool use_timeout,
                          int64_t rel_timeout_ns);

  // Delete 'node' and do the associated cleanups. Returns the next node in
  // the wait list.
  static FutexWaitListNode* DeleteAsyncWaiterNode(FutexWaitListNode* node);

  // `mutex_` protects the composition of `wait_list_` (i.e. no elements may be
  // added or removed without holding this mutex), as well as the `waiting_`
  // and `interrupted_` fields for each individual list node that is currently
  // part of the list. It must be the mutex used together with the `cond_`
  // condition variable of such nodes.
  static base::LazyMutex mutex_;
  static base::LazyInstance<FutexWaitList>::type wait_list_;
  static base::LazyInstance<std::set<Isolate*>>::type
      isolates_resolve_task_scheduled_;
};
}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_FUTEX_EMULATION_H_
