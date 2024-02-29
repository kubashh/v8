// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-atomics-synchronization.h"

#include "src/base/macros.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/base/platform/yield-processor.h"
#include "src/execution/isolate-inl.h"
#include "src/heap/parked-scope-inl.h"
#include "src/objects/js-atomics-synchronization-inl.h"
#include "src/objects/js-promise-inl.h"
#include "src/sandbox/external-pointer-inl.h"

namespace v8 {
namespace internal {

namespace {

// Set fulfill/reject handlers for a JSPrommise object.
Handle<JSPromise> SetPromiseThen(
    Isolate* isolate, Handle<JSPromise> promise,
    Handle<JSFunction> fulfill_handler,
    MaybeHandle<JSFunction> reject_handler = MaybeHandle<JSFunction>()) {
  Handle<Object> reject_handler_handle = isolate->factory()->undefined_value();
  MaybeLocal<Promise> local_then_promise;
  if (!reject_handler.is_null()) {
    reject_handler_handle = reject_handler.ToHandleChecked();
  }
  Handle<Object> argv[] = {fulfill_handler, reject_handler_handle};
  MaybeHandle<Object> then_result = Execution::CallBuiltin(
      isolate, isolate->promise_then(), promise, arraysize(argv), argv);

  Handle<JSPromise> then_promise =
      Handle<JSPromise>::cast(then_result.ToHandleChecked());
  return then_promise;
}

Handle<JSFunction> CreateFunctionFromBuiltin(Isolate* isolate, Builtin builtin,
                                             Handle<Context> context) {
  Factory* factory = isolate->factory();

  Handle<SharedFunctionInfo> info = factory->NewSharedFunctionInfoForBuiltin(
      isolate->factory()->empty_string(), builtin);
  info->set_language_mode(LanguageMode::kStrict);

  Handle<JSFunction> callback =
      Factory::JSFunctionBuilder{isolate, info, context}
          .set_map(isolate->strict_function_without_prototype_map())
          .Build();

  return callback;
}

void AddPromiseToNativeContext(Isolate* isolate, Handle<JSPromise> promise) {
  Handle<NativeContext> native_context(isolate->native_context());
  Handle<OrderedHashSet> promises(native_context->atomics_waitasync_promises(),
                                  isolate);
  promises = OrderedHashSet::Add(isolate, promises, promise).ToHandleChecked();
  native_context->set_atomics_waitasync_promises(*promises);
}

void RemovePromiseFromNativeContext(Isolate* isolate,
                                    Handle<JSPromise> promise) {
  Handle<OrderedHashSet> promises(
      isolate->native_context()->atomics_waitasync_promises(), isolate);
  bool was_deleted = OrderedHashSet::Delete(isolate, *promises, *promise);
  DCHECK(was_deleted);
  USE(was_deleted);
  promises = OrderedHashSet::Shrink(isolate, promises);
  isolate->native_context()->set_atomics_waitasync_promises(*promises);
}

Handle<JSObject> CreateResultObject(Isolate* isolate, Handle<Object> value,
                                    bool success) {
  Handle<JSObject> result =
      isolate->factory()->NewJSObject(isolate->object_function());
  Handle<Object> success_value = isolate->factory()->ToBoolean(success);
  JSObject::AddProperty(isolate, result, "value", value,
                        PropertyAttributes::NONE);
  JSObject::AddProperty(isolate, result, "success", success_value,
                        PropertyAttributes::NONE);
  return result;
}

template <typename T>
Global<T> GetWeakGlobal(Isolate* isolate, Local<T> object) {
  auto* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  v8::Global<T> global(v8_isolate, object);
  global.SetWeak();
  return global;
}

}  // namespace

namespace detail {

// To manage waiting threads, there is a process-wide doubly-linked intrusive
// list per waiter (i.e. mutex or condition variable). There is a per-thread
// node allocated on the stack when the thread goes to sleep during
// waiting.
//
// When compressing pointers (including when sandboxing), the access to the
// on-stack node is indirected through the shared external pointer table.
//
// TODO(v8:12547): Split out WaiterQueueNode and unittest it.
class V8_NODISCARD WaiterQueueNode {
 public:
  ~WaiterQueueNode() {
    // Since waiter queue nodes are allocated on the stack or the C++ heap,
    // they must be removed from the intrusive linked list once they go out of
    // scope, otherwise there will be dangling pointers.
    VerifyNotInList();
  }

  // Enqueues {new_tail}, mutating {head} to be the new head.
  static void Enqueue(WaiterQueueNode** head, WaiterQueueNode* new_tail) {
    DCHECK_NOT_NULL(head);
    new_tail->VerifyNotInList();
    WaiterQueueNode* current_head = *head;
    if (current_head == nullptr) {
      new_tail->next_ = new_tail;
      new_tail->prev_ = new_tail;
      *head = new_tail;
    } else {
      WaiterQueueNode* current_tail = current_head->prev_;
      current_tail->next_ = new_tail;
      current_head->prev_ = new_tail;
      new_tail->next_ = current_head;
      new_tail->prev_ = current_tail;
    }
  }

  // Dequeues the first waiter for which {matcher} returns true and returns it;
  // mutating {head} to be the new head.
  //
  // The queue lock must be held in the synchronization primitive that owns
  // this waiter queue when calling this method.
  template <typename Matcher>
  static WaiterQueueNode* DequeueMatching(WaiterQueueNode** head,
                                          const Matcher& matcher) {
    DCHECK_NOT_NULL(head);
    DCHECK_NOT_NULL(*head);
    WaiterQueueNode* original_head = *head;
    WaiterQueueNode* cur = *head;
    do {
      if (matcher(cur)) {
        WaiterQueueNode* next = cur->next_;
        if (next == cur) {
          // The queue contains exactly 1 node.
          *head = nullptr;
        } else {
          // The queue contains >1 nodes.
          if (cur == original_head) {
            // The matched node is the original head, so next is the new head.
            WaiterQueueNode* tail = original_head->prev_;
            next->prev_ = tail;
            tail->next_ = next;
            *head = next;
          } else {
            // The matched node is in the middle of the queue, so the head does
            // not need to be updated.
            cur->prev_->next_ = next;
            next->prev_ = cur->prev_;
          }
        }
        cur->SetNotInListForVerification();
        return cur;
      }
      cur = cur->next_;
    } while (cur != original_head);
    return nullptr;
  }

  static WaiterQueueNode* Dequeue(WaiterQueueNode** head) {
    return DequeueMatching(head, [](WaiterQueueNode* node) { return true; });
  }

  // Splits at most {count} nodes of the waiter list of into its own list and
  // returns it, mutating {head} to be the head of the back list.
  static WaiterQueueNode* Split(WaiterQueueNode** head, uint32_t count) {
    DCHECK_GT(count, 0);
    DCHECK_NOT_NULL(head);
    DCHECK_NOT_NULL(*head);
    WaiterQueueNode* front_head = *head;
    WaiterQueueNode* back_head = front_head;
    uint32_t actual_count = 0;
    while (actual_count < count) {
      back_head = back_head->next_;
      // The queue is shorter than the requested count, return the whole queue.
      if (back_head == front_head) {
        *head = nullptr;
        return front_head;
      }
      actual_count++;
    }
    WaiterQueueNode* front_tail = back_head->prev_;
    WaiterQueueNode* back_tail = front_head->prev_;

    // Fix up the back list (i.e. remainder of the list).
    back_head->prev_ = back_tail;
    back_tail->next_ = back_head;
    *head = back_head;

    // Fix up and return the front list (i.e. the dequeued list).
    front_head->prev_ = front_tail;
    front_tail->next_ = front_head;
    return front_head;
  }

  // This method must be called from a known waiter queue head. Incorrectly
  // encoded lists can cause this method to infinitely loop.
  static int LengthFromHead(WaiterQueueNode* head) {
    WaiterQueueNode* cur = head;
    int len = 0;
    do {
      len++;
      cur = cur->next_;
    } while (cur != head);
    return len;
  }

  virtual void Notify() = 0;

  uint32_t NotifyAllInList() {
    WaiterQueueNode* cur = this;
    uint32_t count = 0;
    do {
      WaiterQueueNode* next = cur->next_;
      cur->Notify();
      cur = next;
      count++;
    } while (cur != this);
    return count;
  }

  bool should_wait = false;
  Isolate* GetRequester() { return requester_; }

 protected:
  explicit WaiterQueueNode(Isolate* requester) : requester_(requester) {}

  void SetNotInListForVerification() {
#ifdef DEBUG
    next_ = prev_ = nullptr;
#endif  // DEBUG
  }

  Isolate* requester_;

  // The queue wraps around, e.g. the head's prev is the tail, and the tail's
  // next is the head.
  WaiterQueueNode* next_ = nullptr;
  WaiterQueueNode* prev_ = nullptr;

 private:
  void VerifyNotInList() {
    DCHECK_NULL(next_);
    DCHECK_NULL(prev_);
  }
};

class V8_NODISCARD SyncWaiterQueueNode final : public WaiterQueueNode {
 public:
  explicit SyncWaiterQueueNode(Isolate* requester)
      : WaiterQueueNode(requester) {}

  void Wait() {
    AllowGarbageCollection allow_before_parking;
    requester_->main_thread_local_heap()->BlockWhileParked([this]() {
      base::MutexGuard guard(&wait_lock_);
      while (should_wait) {
        wait_cond_var_.Wait(&wait_lock_);
      }
    });
  }

  // Returns false if timed out, true otherwise.
  bool WaitFor(const base::TimeDelta& rel_time) {
    bool result;
    AllowGarbageCollection allow_before_parking;
    requester_->main_thread_local_heap()->BlockWhileParked([this, rel_time,
                                                            &result]() {
      base::MutexGuard guard(&wait_lock_);
      base::TimeTicks current_time = base::TimeTicks::Now();
      base::TimeTicks timeout_time = current_time + rel_time;
      for (;;) {
        if (!should_wait) {
          result = true;
          return;
        }
        current_time = base::TimeTicks::Now();
        if (current_time >= timeout_time) {
          result = false;
          return;
        }
        base::TimeDelta time_until_timeout = timeout_time - current_time;
        bool wait_res = wait_cond_var_.WaitFor(&wait_lock_, time_until_timeout);
        USE(wait_res);
        // The wake up may have been spurious, so loop again.
      }
    });
    return result;
  }

  void Notify() override {
    base::MutexGuard guard(&wait_lock_);
    should_wait = false;
    wait_cond_var_.NotifyOne();
    SetNotInListForVerification();
  }

 private:
  base::Mutex wait_lock_;
  base::ConditionVariable wait_cond_var_;
};

template <typename NodeType, typename T>
class AsyncWaiterNotifyTask : public CancelableTask {
 public:
  AsyncWaiterNotifyTask(CancelableTaskManager* cancelable_task_manager,
                        std::shared_ptr<NodeType> node)
      : CancelableTask(cancelable_task_manager), node_(node) {}

  void RunInternal() override {
    if (node_->GetRequester()->cancelable_task_manager()->canceled()) return;
    T::HandleAsyncNotify(node_);
  }

 private:
  std::shared_ptr<NodeType> node_;
};

using AsyncLockNotifyTask =
    AsyncWaiterNotifyTask<AsyncLockWaiterQueueNode, JSAtomicsMutex>;
using AsyncWaitNotifyTask =
    AsyncWaiterNotifyTask<AsyncWaitWaiterQueueNode, JSAtomicsCondition>;

class V8_NODISCARD AsyncWaiterQueueNode : public WaiterQueueNode {
 public:
  ~AsyncWaiterQueueNode() {
    promise_.Reset();
    native_context_.Reset();
  }

  TaskRunner* task_runner() { return task_runner_.get(); }

  void set_timeout_task_id(CancelableTaskManager::Id timeout_task_id) {
    timeout_task_id_ = timeout_task_id;
  }

  Local<v8::Context> GetNativeContext() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    return native_context_.Get(v8_isolate);
  }

  Handle<JSPromise> GetPromise() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    Handle<JSPromise> promise = Utils::OpenHandle(*promise_.Get(v8_isolate));
    return promise;
  }

 protected:
  explicit AsyncWaiterQueueNode(Isolate* requester, Handle<JSPromise> promise)
      : WaiterQueueNode(requester) {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester);
    task_runner_ =
        V8::GetCurrentPlatform()->GetForegroundTaskRunner(v8_isolate);
    promise_ = GetWeakGlobal(requester, Utils::PromiseToLocal(promise));
    native_context_ =
        GetWeakGlobal(requester, Utils::ToLocal(requester->native_context()));
    timeout_task_id_ = CancelableTaskManager::kInvalidTaskId;
  }

  std::shared_ptr<TaskRunner> task_runner_;
  CancelableTaskManager::Id timeout_task_id_;
  Global<v8::Promise> promise_;
  Global<v8::Context> native_context_;
};

class V8_NODISCARD AsyncLockWaiterQueueNode final
    : public AsyncWaiterQueueNode {
 public:
  AsyncLockWaiterQueueNode(Isolate* requester, Handle<JSObject> js_mutex,
                           Handle<JSPromise> promise,
                           MaybeHandle<JSPromise> lock_async_promise)
      : AsyncWaiterQueueNode(requester, promise) {
    js_mutex_ = GetWeakGlobal(requester, Utils::ToLocal(js_mutex));
    if (!lock_async_promise.is_null()) {
      lock_async_promise_ = GetWeakGlobal(
          requester,
          Utils::PromiseToLocal(lock_async_promise.ToHandleChecked()));
    }
  }

  ~AsyncLockWaiterQueueNode() {
    // The lifetime of this node is shared by the notify task and the timeout
    // task. DCHECK that we don't have circular ownership.
    DCHECK(!notify_task_);
    js_mutex_.Reset();
  }

  void Notify() override {
    CancelableTaskManager* task_manager =
        GetRequester()->cancelable_task_manager();
    if (task_manager->canceled()) return;
    should_wait = false;
    // Post a task back to the thread that owns this node
    if (timeout_task_id_ != CancelableTaskManager::kInvalidTaskId) {
      task_manager->TryAbort(timeout_task_id_);
    }
    std::unique_ptr<AsyncLockNotifyTask> notify_task = nullptr;
    notify_task_.swap(notify_task);
    task_runner_->PostNonNestableTask(std::move(notify_task));
  }

  Handle<JSAtomicsMutex> GetJSMutex() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    Handle<JSAtomicsMutex> js_mutex = Handle<JSAtomicsMutex>::cast(
        Utils::OpenHandle(*js_mutex_.Get(v8_isolate)));
    return js_mutex;
  }

  Handle<JSPromise> GetLockAsyncPromise() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    Handle<JSPromise> lock_async_promise = Handle<JSPromise>::cast(
        Utils::OpenHandle(*lock_async_promise_.Get(v8_isolate)));
    return lock_async_promise;
  }

  void set_notify_task(std::unique_ptr<AsyncLockNotifyTask> task) {
    DCHECK(!notify_task_);
    notify_task_ = std::move(task);
  }

  void ClearNotifyTask() { notify_task_.reset(); }

 private:
  Global<v8::Object> js_mutex_;
  Global<v8::Promise> lock_async_promise_;
  std::unique_ptr<AsyncLockNotifyTask> notify_task_;
};

class AsyncWaitWaiterQueueNode final : public AsyncWaiterQueueNode {
 public:
  AsyncWaitWaiterQueueNode(Isolate* requester, Handle<JSObject> mutex,
                           Handle<JSPromise> promise,
                           Handle<JSAtomicsCondition> cv)
      : AsyncWaiterQueueNode(requester, promise) {
    cv_ = GetWeakGlobal(requester, Utils::ToLocal(Handle<JSObject>::cast(cv)));
  }

  ~AsyncWaitWaiterQueueNode() {
    // The lifetime of this node is shared by the notify task and the timeout
    // task. DCHECK that we don't have circular ownership.
    DCHECK(!notify_task_);
    cv_.Reset();
  }

  void Notify() override {
    CancelableTaskManager* task_manager =
        GetRequester()->cancelable_task_manager();
    if (task_manager->canceled()) return;
    // Post a task back to the thread that owns this node
    if (timeout_task_id_ != CancelableTaskManager::kInvalidTaskId) {
      task_manager->TryAbort(timeout_task_id_);
    }
    std::unique_ptr<AsyncWaitNotifyTask> notify_task = nullptr;
    notify_task_.swap(notify_task);
    task_runner_->PostNonNestableTask(std::move(notify_task));
  }

  Handle<JSAtomicsCondition> GetConditionVariable() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    Handle<JSAtomicsCondition> cv = Handle<JSAtomicsCondition>::cast(
        Utils::OpenHandle(*cv_.Get(v8_isolate)));
    return cv;
  }
  void set_notify_task(std::unique_ptr<AsyncWaitNotifyTask> task) {
    DCHECK(!notify_task_);
    notify_task_ = std::move(task);
  }

  void ClearNotifyTask() { notify_task_.reset(); }

 private:
  Global<v8::Object> cv_;
  std::unique_ptr<AsyncWaitNotifyTask> notify_task_;
};

template <typename NodeType, typename T>
class AsyncWaiterTimeoutTask : public CancelableTask {
 public:
  AsyncWaiterTimeoutTask(CancelableTaskManager* cancelable_task_manager,
                         std::shared_ptr<NodeType> node)
      : CancelableTask(cancelable_task_manager), node_(node) {}

  void RunInternal() override {
    if (node_->GetRequester()->cancelable_task_manager()->canceled()) return;
    T::HandleAsyncTimeout(node_);
  }

 private:
  std::shared_ptr<NodeType> node_;
};

}  // namespace detail

using detail::AsyncLockNotifyTask;
using detail::AsyncLockWaiterQueueNode;
using detail::AsyncWaitNotifyTask;
using detail::AsyncWaitWaiterQueueNode;
using detail::SyncWaiterQueueNode;
using detail::WaiterQueueNode;
using AsyncWaitTimeoutTask =
    detail::AsyncWaiterTimeoutTask<AsyncWaitWaiterQueueNode,
                                   JSAtomicsCondition>;
using AsyncLockTimeoutTask =
    detail::AsyncWaiterTimeoutTask<AsyncLockWaiterQueueNode, JSAtomicsMutex>;

// static
bool JSAtomicsMutex::TryLockExplicit(std::atomic<StateT>* state,
                                     StateT& expected) {
  // Try to lock a possibly contended mutex.
  expected = IsLockedField::update(expected, false);
  return state->compare_exchange_weak(
      expected, IsLockedField::update(expected, true),
      std::memory_order_acquire, std::memory_order_relaxed);
}

// static
bool JSAtomicsMutex::TryLockWaiterQueueExplicit(std::atomic<StateT>* state,
                                                StateT& expected) {
  // Try to acquire the queue lock.
  expected = IsWaiterQueueLockedField::update(expected, false);
  return state->compare_exchange_weak(
      expected, IsWaiterQueueLockedField::update(expected, true),
      std::memory_order_acquire, std::memory_order_relaxed);
}

bool JSAtomicsMutex::BackoffTryLock(Isolate* requester,
                                    Handle<JSAtomicsMutex> mutex,
                                    std::atomic<StateT>* state) {
  // The backoff algorithm is copied from PartitionAlloc's SpinningMutex.
  constexpr int kSpinCount = 64;
  constexpr int kMaxBackoff = 16;

  int tries = 0;
  int backoff = 1;
  StateT current_state = state->load(std::memory_order_relaxed);
  do {
    if (JSAtomicsMutex::TryLockExplicit(state, current_state)) return true;

    for (int yields = 0; yields < backoff; yields++) {
      YIELD_PROCESSOR;
      tries++;
    }

    backoff = std::min(kMaxBackoff, backoff << 1);
  } while (tries < kSpinCount);
  return false;
}

bool JSAtomicsMutex::MaybeEnqueueNode(Isolate* requester,
                                      Handle<JSAtomicsMutex> mutex,
                                      std::atomic<StateT>* state,
                                      WaiterQueueNode* this_waiter) {
  StateT current_state = state->load(std::memory_order_relaxed);
  for (;;) {
    if (IsLockedField::decode(current_state) &&
        TryLockWaiterQueueExplicit(state, current_state)) {
      break;
    }
    // Also check for the lock having been released by another thread during
    // attempts to acquire the queue lock.
    if (TryLockExplicit(state, current_state)) return false;
    YIELD_PROCESSOR;
  }

  // With the queue lock held, enqueue the requester onto the waiter queue.
  this_waiter->should_wait = true;
  WaiterQueueNode* waiter_head =
      mutex->DestructivelyGetWaiterQueueHead(requester);
  WaiterQueueNode::Enqueue(&waiter_head, this_waiter);

  // Enqueue a new waiter queue head and release the queue lock.
  DCHECK_EQ(state->load(),
            IsWaiterQueueLockedField::update(current_state, true));
  StateT new_state = IsWaiterQueueLockedField::update(current_state, false);
  new_state = mutex->SetWaiterQueueHead(requester, waiter_head, new_state);
  // The lock is held, just not by us, so don't set the current thread id as
  // the owner.
  DCHECK(IsLockedField::decode(current_state));
  DCHECK(!mutex->IsCurrentThreadOwner());
  new_state = IsLockedField::update(new_state, true);
  state->store(new_state, std::memory_order_release);
  return true;
}

// static
void JSAtomicsMutex::UnlockWaiterQueueWithNewState(std::atomic<StateT>* state,
                                                   StateT new_state) {
  // Set the new state without changing the "is locked" bit.
  DCHECK_EQ(IsLockedField::update(new_state, false), new_state);
  StateT expected = state->load(std::memory_order_relaxed);
  StateT desired;
  do {
    desired = IsLockedField::update(new_state, IsLockedField::decode(expected));
  } while (!state->compare_exchange_weak(
      expected, desired, std::memory_order_release, std::memory_order_relaxed));
}

bool JSAtomicsMutex::LockJSMutexOrDequeueTimedOutWaiter(
    Isolate* requester, std::atomic<StateT>* state,
    WaiterQueueNode* timed_out_waiter) {
  // First acquire the queue lock, which is itself a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);
  // There are no waiters, but the js mutex lock may be held by another thread.
  if (!HasWaitersField::decode(current_state)) return false;
  while (!TryLockWaiterQueueExplicit(state, current_state)) {
    YIELD_PROCESSOR;
  }

  WaiterQueueNode* waiter_head = DestructivelyGetWaiterQueueHead(requester);

  if (waiter_head == nullptr) {
    // The queue is empty but the js mutex lock may be held by another thread,
    // release the waiter queue bit without changing the "is locked" bit.
    DCHECK(!HasWaitersField::decode(current_state));
    UnlockWaiterQueueWithNewState(state, kUnlockedUncontended);
    return false;
  }

  WaiterQueueNode* dequeued_node = WaiterQueueNode::DequeueMatching(
      &waiter_head,
      [&](WaiterQueueNode* node) { return node == timed_out_waiter; });

  // Release the queue lock and install the new waiter queue head.
  DCHECK_EQ(state->load(),
            IsWaiterQueueLockedField::update(current_state, true));
  StateT new_state = kUnlockedUncontended;
  new_state = SetWaiterQueueHead(requester, waiter_head, new_state);

  if (!dequeued_node) {
    // The timed out waiter was not in the queue, so it must have been dequeued
    // and notified between the time this thread woke up and the time it
    // acquired the queue lock, so there is a risk that the next queue head is
    // never notified. Try to take the js mutex lock here, if we succeed, the
    // next node will be notified by this thread, otherwise, it will be notified
    // by the thread holding the lock now.

    // Since we use strong CAS below, we know that the js mutex lock will be
    // held by either this thread or another thread that can't go through the
    // unlock fast path because this thread is holding the waiter queue lock.
    // Hence, it is safe to always set the "is locked" bit in new_state.
    new_state = IsLockedField::update(new_state, true);
    DCHECK(!IsWaiterQueueLockedField::decode(new_state));
    current_state = IsLockedField::update(current_state, false);
    if (state->compare_exchange_strong(current_state, new_state,
                                       std::memory_order_acq_rel,
                                       std::memory_order_relaxed)) {
      // The CAS atomically released the waiter queue lock and acquired the js
      // mutex lock.
      return true;
    }

    DCHECK(IsLockedField::decode(state->load()));
    state->store(new_state, std::memory_order_release);
    return false;
  }

  UnlockWaiterQueueWithNewState(state, new_state);
  return false;
}

// static
bool JSAtomicsMutex::LockSlowPath(Isolate* requester,
                                  Handle<JSAtomicsMutex> mutex,
                                  std::atomic<StateT>* state,
                                  base::Optional<base::TimeDelta> timeout) {
  for (;;) {
    // Spin for a little bit to try to acquire the lock, so as to be fast under
    // microcontention.
    if (BackoffTryLock(requester, mutex, state)) return true;

    // At this point the lock is considered contended, so try to go to sleep and
    // put the requester thread on the waiter queue.

    // Allocate a waiter queue node on-stack, since this thread is going to
    // sleep and will be blocked anyway.
    SyncWaiterQueueNode this_waiter(requester);
    if (!MaybeEnqueueNode(requester, mutex, state, &this_waiter)) return true;

    bool rv;
    // Wait for another thread to release the lock and wake us up.
    if (timeout) {
      rv = this_waiter.WaitFor(*timeout);
      // Reload the state pointer after wake up in case of shared GC while
      // blocked.
      state = mutex->AtomicStatePtr();
      if (!rv) {
        // If timed out, remove ourself from the waiter list, which is usually
        // done by the thread performing the notifying.
        rv = mutex->LockJSMutexOrDequeueTimedOutWaiter(requester, state,
                                                       &this_waiter);
        return rv;
      }
    } else {
      this_waiter.Wait();
      // Reload the state pointer after wake up in case of shared GC while
      // blocked.
      state = mutex->AtomicStatePtr();
    }

    // After wake up we try to acquire the lock again by spinning, as the
    // contention at the point of going to sleep should not be correlated with
    // contention at the point of waking up.
  }
}

void JSAtomicsMutex::UnlockSlowPath(Isolate* requester,
                                    std::atomic<StateT>* state) {
  // The fast path unconditionally cleared the owner thread.
  DCHECK_EQ(ThreadId::Invalid().ToInteger(),
            AtomicOwnerThreadIdPtr()->load(std::memory_order_relaxed));

  // To wake a sleeping thread, first acquire the queue lock, which is itself
  // a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);
  while (!TryLockWaiterQueueExplicit(state, current_state)) {
    YIELD_PROCESSOR;
  }

  // Get the waiter queue head, which is guaranteed to be non-null because the
  // unlock fast path uses a strong CAS which does not allow spurious
  // failure. This is unlike the lock fast path, which uses a weak CAS.
  DCHECK(HasWaitersField::decode(current_state));
  WaiterQueueNode* waiter_head = DestructivelyGetWaiterQueueHead(requester);
  WaiterQueueNode* old_head = WaiterQueueNode::Dequeue(&waiter_head);

  // Release both the lock and the queue lock, and install the new waiter queue
  // head.
  StateT new_state = IsWaiterQueueLockedField::update(
      IsLockedField::update(current_state, false), false);
  new_state = SetWaiterQueueHead(requester, waiter_head, new_state);
  state->store(new_state, std::memory_order_release);

  old_head->Notify();
}

// static
bool JSAtomicsCondition::TryLockWaiterQueueExplicit(std::atomic<StateT>* state,
                                                    StateT& expected) {
  // Try to acquire the queue lock.
  expected = IsWaiterQueueLockedField::update(expected, false);
  return state->compare_exchange_weak(
      expected, IsWaiterQueueLockedField::update(expected, true),
      std::memory_order_acquire, std::memory_order_relaxed);
}

// The lockAsync flow is controlled by a series of chained promises, with
// async_lock_promise being the last in the chain and the return value of the
// API. The chain is as follows:
// 1. The lock promise, resolved when the lock is acquired.
// 2. The callback promise, a promise wrapper around the callable.
// 3. An unlock handler, which will unlock the mutex, this can be either:
// 3.1 The fulfill unlock handler, releases the lock and resolves
//     async_lock_promise with the result of the callback.
// 3.2 The reject unlock handler, releases the lock and rejects
//     async_lock_promise with the error from the callback.

// static
Handle<JSPromise> JSAtomicsMutex::LockOrQueuePromise(
    Isolate* isolate, Handle<JSAtomicsMutex> mutex,
    Handle<JSObject> run_under_lock, base::Optional<base::TimeDelta> timeout) {
  // Create a promise to be resolved when the lock is acquired.
  Handle<JSPromise> lock_promise = isolate->factory()->NewJSPromise();
  // Set the callable to be run in a microtask after acquiring the lock.
  Handle<JSPromise> callback_promise = SetPromiseThen(
      isolate, lock_promise, Handle<JSFunction>::cast(run_under_lock));
  // Unlock the mutex after the callable finished execution.
  Handle<JSPromise> lock_async_promise = isolate->factory()->NewJSPromise();
  SetAsyncUnlockHandlers(isolate, mutex, callback_promise, lock_async_promise);
  bool locked =
      AsyncLock(isolate, mutex, lock_promise, lock_async_promise, timeout);
  if (locked) {
    MaybeHandle<Object> result =
        JSPromise::Resolve(lock_promise, isolate->factory()->undefined_value());
    USE(result);
  } else {
    // If the promise is not resolved, keep it alive in a set in the native
    // context. The promise will be resolved and remove from the set in
    // `JSAtomicsMutex::HandleAsyncNotify` or
    // `JSAtomicsMutex::HandleAsyncTimeout`.
    AddPromiseToNativeContext(isolate, lock_promise);
  }

  return lock_async_promise;
}

// Set the fulfill/reject hanldlers that will release the lock for asyncLock.
void JSAtomicsMutex::SetAsyncUnlockHandlers(
    Isolate* isolate, Handle<JSAtomicsMutex> mutex,
    Handle<JSPromise> lock_promise, Handle<JSPromise> lock_async_promise) {
  Handle<ScopeInfo> scope_info = ScopeInfo::CreateForEmptyFunction(isolate);
  Handle<Context> handlers_context =
      isolate->factory()->NewBuiltinContext(isolate->native_context(), 2);
  handlers_context->set_scope_info(*scope_info);
  handlers_context->set(0, *mutex);
  handlers_context->set(1, *lock_async_promise);

  Handle<JSFunction> resolver_callback = CreateFunctionFromBuiltin(
      isolate, Builtin::kAtomicsMutexAsyncUnlockResolveHandler,
      handlers_context);
  Handle<JSFunction> reject_callback = CreateFunctionFromBuiltin(
      isolate, Builtin::kAtomicsMutexAsyncUnlockRejectHandler,
      handlers_context);

  Handle<JSPromise> then_promise =
      SetPromiseThen(isolate, lock_promise, resolver_callback, reject_callback);
  USE(then_promise);
}

// static
bool JSAtomicsMutex::AsyncLockSlowPath(
    Handle<JSAtomicsMutex> mutex, Isolate* isolate, Handle<JSPromise> promise,
    MaybeHandle<JSPromise> lock_async_promise, std::atomic<StateT>* state,
    base::Optional<base::TimeDelta> timeout) {
  // Spin for a little bit to try to acquire the lock, so as to be fast under
  // microcontention.
  if (BackoffTryLock(isolate, mutex, state)) {
    return true;
  }

  // At this point the lock is considered contended, create a new async waiter
  // node in the heap. We use a shared_ptr to share the lifetime of the node
  // between the notify task and the timeout task.
  auto this_waiter = std::make_shared<AsyncLockWaiterQueueNode>(
      isolate, mutex, promise, lock_async_promise);
  if (!MaybeEnqueueNode(isolate, mutex, state, this_waiter.get())) {
    return true;
  }

  // Create the notify task here so that both the notify task and the timeout
  // task can point to the same waiter shared_ptr.
  auto task = std::make_unique<AsyncLockNotifyTask>(
      isolate->cancelable_task_manager(), this_waiter);
  this_waiter->set_notify_task(std::move(task));

  if (timeout) {
    // Start a timer to run the `AsyncLockTimeoutTask` after the timeout.
    TaskRunner* taks_runner = this_waiter->task_runner();
    auto task = std::make_unique<AsyncLockTimeoutTask>(
        isolate->cancelable_task_manager(), this_waiter);
    this_waiter->set_timeout_task_id(task->id());
    taks_runner->PostNonNestableDelayedTask(std::move(task),
                                            timeout->InSecondsF());
  }
  return false;
}

// Try to take the lock or requeue and exiting node.
// static
bool JSAtomicsMutex::LockOrQueueAsyncNode(
    Isolate* isolate, Handle<JSAtomicsMutex> mutex,
    std::shared_ptr<AsyncLockWaiterQueueNode>& waiter) {
  std::atomic<StateT>* state = mutex->AtomicStatePtr();
  // Spin for a little bit to try to acquire the lock, so as to be fast under
  // microcontention.
  if (BackoffTryLock(isolate, mutex, state)) {
    return true;
  }

  if (!MaybeEnqueueNode(isolate, mutex, state, waiter.get())) {
    return true;
  }

  auto task = std::make_unique<AsyncLockNotifyTask>(
      isolate->cancelable_task_manager(), waiter);
  waiter->set_notify_task(std::move(task));

  return false;
}

bool JSAtomicsMutex::DequeueTimedOutAsyncWaiter(
    Isolate* requester, Handle<JSAtomicsMutex> mutex,
    std::atomic<StateT>* state, detail::WaiterQueueNode* timed_out_waiter) {
  // First acquire the queue lock, which is itself a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);
  // There are no waiters, but the js mutex lock may be held by another thread.
  if (!HasWaitersField::decode(current_state)) return false;
  while (!TryLockWaiterQueueExplicit(state, current_state)) {
    YIELD_PROCESSOR;
  }

  // Get the waiter queue head.
  WaiterQueueNode* waiter_head =
      mutex->DestructivelyGetWaiterQueueHead(requester);

  if (waiter_head == nullptr) {
    // The queue is empty but the js mutex lock may be held by another thread,
    // release the waiter queue bit without changing the "is locked" bit.
    DCHECK(!HasWaitersField::decode(current_state));
    UnlockWaiterQueueWithNewState(state, kUnlockedUncontended);
    return false;
  }

  WaiterQueueNode* dequeued_node = WaiterQueueNode::DequeueMatching(
      &waiter_head,
      [&](WaiterQueueNode* node) { return node == timed_out_waiter; });

  // Release the queue lock and install the new waiter queue head.
  DCHECK_EQ(state->load(),
            IsWaiterQueueLockedField::update(current_state, true));
  StateT new_state = kUnlockedUncontended;
  new_state = mutex->SetWaiterQueueHead(requester, waiter_head, new_state);

  UnlockWaiterQueueWithNewState(state, new_state);
  return dequeued_node != nullptr;
}

// static
void JSAtomicsMutex::HandleAsyncTimeout(
    std::shared_ptr<AsyncLockWaiterQueueNode>& node) {
  Isolate* isolate = node->GetRequester();
  HandleScope scope(isolate);
  v8::Context::Scope contextScope(node->GetNativeContext());
  Handle<JSAtomicsMutex> js_mutex = node->GetJSMutex();

  bool dequeued = JSAtomicsMutex::DequeueTimedOutAsyncWaiter(
      isolate, js_mutex, node->GetJSMutex()->AtomicStatePtr(), node.get());
  Handle<JSPromise> lock_promise = node->GetPromise();
  // If the waiter is no longer in the queue, then its corresponding notify
  // task is already in the event loop, do nothing for the timeout.
  if (dequeued) {
    Handle<JSPromise> lock_async_promise = node->GetLockAsyncPromise();
    Handle<JSObject> result = CreateResultObject(
        isolate, isolate->factory()->undefined_value(), false);
    auto resolve_result = JSPromise::Resolve(lock_async_promise, result);
    USE(resolve_result);
    node->ClearNotifyTask();
    RemovePromiseFromNativeContext(isolate, lock_promise);
  }
}

// static
void JSAtomicsMutex::HandleAsyncNotify(
    std::shared_ptr<AsyncLockWaiterQueueNode>& node) {
  if (node->GetRequester()->cancelable_task_manager()->canceled()) return;
  Isolate* isolate = node->GetRequester();
  HandleScope scope(isolate);
  v8::Context::Scope contextScope(node->GetNativeContext());
  Handle<JSAtomicsMutex> js_mutex = node->GetJSMutex();
  Handle<JSPromise> promise = node->GetPromise();
  bool locked = LockOrQueueAsyncNode(isolate, js_mutex, node);
  if (locked) {
    js_mutex->SetCurrentThreadAsOwner();
    auto resolve_result =
        JSPromise::Resolve(promise, isolate->factory()->undefined_value());
    USE(resolve_result);
    // Remove the lock promise from the native context
    RemovePromiseFromNativeContext(isolate, promise);
  }
}

// static
void JSAtomicsMutex::AsyncLockWithoutCallback(Isolate* requester,
                                              Handle<JSAtomicsMutex> mutex,
                                              Handle<JSPromise> promise) {
  bool locked = AsyncLock(requester, mutex, promise, MaybeHandle<JSPromise>());
  if (locked) {
    MaybeHandle<Object> result =
        JSPromise::Resolve(promise, requester->factory()->undefined_value());
    USE(result);
  } else {
    AddPromiseToNativeContext(requester, promise);
  }
}

// static
void JSAtomicsCondition::QueueWaiter(Isolate* requester,
                                     Handle<JSAtomicsCondition> cv,
                                     WaiterQueueNode* waiter) {
  // The state pointer should not be used outside of this block as a shared GC
  // may reallocate it after waiting.
  std::atomic<StateT>* state = cv->AtomicStatePtr();

  // Try to acquire the queue lock, which is itself a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);
  while (!TryLockWaiterQueueExplicit(state, current_state)) {
    YIELD_PROCESSOR;
  }

  // With the queue lock held, enqueue the requester onto the waiter queue.
  waiter->should_wait = true;
  WaiterQueueNode* waiter_head = cv->DestructivelyGetWaiterQueueHead(requester);
  WaiterQueueNode::Enqueue(&waiter_head, waiter);

  // Release the queue lock and install the new waiter queue head.
  DCHECK_EQ(state->load(),
            IsWaiterQueueLockedField::update(current_state, true));
  StateT new_state = IsWaiterQueueLockedField::update(current_state, false);
  new_state = cv->SetWaiterQueueHead(requester, waiter_head, new_state);
  state->store(new_state, std::memory_order_release);
}

// static
bool JSAtomicsCondition::WaitFor(Isolate* requester,
                                 Handle<JSAtomicsCondition> cv,
                                 Handle<JSAtomicsMutex> mutex,
                                 base::Optional<base::TimeDelta> timeout) {
  DisallowGarbageCollection no_gc;

  // Allocate a waiter queue node on-stack, since this thread is going to sleep
  // and will be blocked anyway.
  SyncWaiterQueueNode this_waiter(requester);

  JSAtomicsCondition::QueueWaiter(requester, cv, &this_waiter);

  // Release the mutex and wait for another thread to wake us up, reacquiring
  // the mutex upon wakeup.
  mutex->Unlock(requester);
  bool rv;
  if (timeout) {
    rv = this_waiter.WaitFor(*timeout);
    if (!rv) {
      // If timed out, remove ourself from the waiter list, which is usually
      // done by the thread performing the notifying.
      std::atomic<StateT>* state = cv->AtomicStatePtr();
      DequeueExplicit(requester, cv, state, [&](WaiterQueueNode** waiter_head) {
        return WaiterQueueNode::DequeueMatching(
            waiter_head,
            [&](WaiterQueueNode* node) { return node == &this_waiter; });
      });
    }
  } else {
    this_waiter.Wait();
    rv = true;
  }
  JSAtomicsMutex::Lock(requester, mutex);
  return rv;
}

// static
WaiterQueueNode* JSAtomicsCondition::DequeueExplicit(
    Isolate* requester, Handle<JSAtomicsCondition> cv,
    std::atomic<StateT>* state, const DequeueAction& action_under_lock) {
  // First acquire the queue lock, which is itself a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);

  if (!HasWaitersField::decode(current_state)) return nullptr;
  while (!TryLockWaiterQueueExplicit(state, current_state)) {
    YIELD_PROCESSOR;
  }

  // Get the waiter queue head.
  WaiterQueueNode* waiter_head = cv->DestructivelyGetWaiterQueueHead(requester);

  // There's no waiter to wake up, release the queue lock by setting it to the
  // empty state.
  if (waiter_head == nullptr) {
    DCHECK_EQ(state->load(),
              IsWaiterQueueLockedField::update(current_state, true));
    state->store(kEmptyState, std::memory_order_release);
    return nullptr;
  }

  WaiterQueueNode* old_head = action_under_lock(&waiter_head);

  // Release the queue lock and install the new waiter queue head.
  DCHECK_EQ(state->load(),
            IsWaiterQueueLockedField::update(current_state, true));
  StateT new_state = IsWaiterQueueLockedField::update(current_state, false);
  new_state = cv->SetWaiterQueueHead(requester, waiter_head, new_state);
  state->store(new_state, std::memory_order_release);

  return old_head;
}

// static
uint32_t JSAtomicsCondition::Notify(Isolate* requester,
                                    Handle<JSAtomicsCondition> cv,
                                    uint32_t count) {
  std::atomic<StateT>* state = cv->AtomicStatePtr();

  // Dequeue count waiters.
  WaiterQueueNode* old_head =
      DequeueExplicit(requester, cv, state, [=](WaiterQueueNode** waiter_head) {
        if (count == 1) {
          return WaiterQueueNode::Dequeue(waiter_head);
        }
        if (count == kAllWaiters) {
          WaiterQueueNode* rv = *waiter_head;
          *waiter_head = nullptr;
          return rv;
        }
        return WaiterQueueNode::Split(waiter_head, count);
      });

  // No waiters.
  if (old_head == nullptr) return 0;

  // Notify the waiters.
  if (count == 1) {
    old_head->Notify();
    return 1;
  }
  return old_head->NotifyAllInList();
}

Tagged<Object> JSAtomicsCondition::NumWaitersForTesting(Isolate* requester) {
  DisallowGarbageCollection no_gc;
  std::atomic<StateT>* state = AtomicStatePtr();
  StateT current_state = state->load(std::memory_order_relaxed);

  // There are no waiters.
  if (!HasWaitersField::decode(current_state)) return Smi::FromInt(0);

  int num_waiters;
  {
    // Take the queue lock.
    while (!TryLockWaiterQueueExplicit(state, current_state)) {
      YIELD_PROCESSOR;
    }

    // Get the waiter queue head.
    WaiterQueueNode* waiter_head = DestructivelyGetWaiterQueueHead(requester);
    num_waiters = WaiterQueueNode::LengthFromHead(waiter_head);

    // Release the queue lock and reinstall the same queue head by creating a
    // new state.
    DCHECK_EQ(state->load(),
              IsWaiterQueueLockedField::update(current_state, true));
    StateT new_state = IsWaiterQueueLockedField::update(current_state, false);
    new_state = SetWaiterQueueHead(requester, waiter_head, new_state);
    state->store(new_state, std::memory_order_release);
  }

  return Smi::FromInt(num_waiters);
}

// static
WaiterQueueNode* JSAtomicsCondition::RemoveTimedOutWaiter(
    Handle<JSAtomicsCondition> cv, AsyncWaitWaiterQueueNode* async_waiter) {
  Isolate* requester = async_waiter->GetRequester();
  std::atomic<StateT>* state = cv->AtomicStatePtr();

  return DequeueExplicit(
      requester, cv, state, [&](WaiterQueueNode** waiter_head) {
        return WaiterQueueNode::DequeueMatching(
            waiter_head,
            [&](WaiterQueueNode* node) { return node == async_waiter; });
      });
}

// The lockAsync flow is controlled 2 chained promises, with lock_promise being
// the return value of the API.
// 1. A wait promise, which will be resolved either in the notify task or in the
//    timeout task.
// 2. An lock promise, which will be resolved when the lock is acquired after
//    waiting.

// static
Handle<JSPromise> JSAtomicsCondition::WaitAsync(
    Isolate* requester, Handle<JSAtomicsCondition> cv,
    Handle<JSAtomicsMutex> mutex, base::Optional<base::TimeDelta> timeout) {
  Handle<JSPromise> wait_promise = requester->factory()->NewJSPromise();
  Handle<Context> handlers_context =
      requester->factory()->NewBuiltinContext(requester->native_context(), 2);
  Handle<JSFunction> lock_function = CreateFunctionFromBuiltin(
      requester, Builtin::kAtomicsConditionGetLock, handlers_context);
  handlers_context->set(0, *mutex);
  handlers_context->set(1, *cv);
  Handle<JSPromise> lock_promise =
      SetPromiseThen(requester, wait_promise, lock_function);

  // At this point the lock is considered contended, create a new async waiter
  // node in the heap. We use a shared_ptr to share the lifetime of the node
  // between the notify task and the timeout task.
  auto this_waiter = std::make_shared<AsyncWaitWaiterQueueNode>(
      requester, mutex, wait_promise, cv);
  QueueWaiter(requester, cv, this_waiter.get());

  // Create the notify task here so that both the notify task and the timeout
  // task can point to the same waiter shared_ptr.
  auto task = std::make_unique<AsyncWaitNotifyTask>(
      requester->cancelable_task_manager(), this_waiter);
  this_waiter->set_notify_task(std::move(task));
  if (timeout) {
    TaskRunner* taks_runner = this_waiter->task_runner();
    auto task = std::make_unique<AsyncWaitTimeoutTask>(
        requester->cancelable_task_manager(), this_waiter);
    this_waiter->set_timeout_task_id(task->id());
    taks_runner->PostNonNestableDelayedTask(std::move(task),
                                            timeout->InSecondsF());
  }
  mutex->Unlock(requester);
  // Keep the wait promise alive in the native context.
  AddPromiseToNativeContext(requester, wait_promise);
  return lock_promise;
}

// static
void JSAtomicsCondition::HandleAsyncTimeout(
    std::shared_ptr<AsyncWaitWaiterQueueNode>& node) {
  Isolate* isolate = node->GetRequester();
  HandleScope scope(isolate);
  WaiterQueueNode* waiter = JSAtomicsCondition::RemoveTimedOutWaiter(
      node->GetConditionVariable(), node.get());
  // If the waiter is not in the queue, the notify task is already in the event
  // loop, do nothing for the timeout.
  if (waiter) {
    // The node had not been notified yet, clear the notify task.
    node->ClearNotifyTask();
    JSAtomicsCondition::HandleAsyncNotify(node);
  }
}

// static
void JSAtomicsCondition::HandleAsyncNotify(
    std::shared_ptr<AsyncWaitWaiterQueueNode>& node) {
  Isolate* isolate = node->GetRequester();
  HandleScope scope(isolate);
  v8::Context::Scope contextScope(node->GetNativeContext());
  Handle<JSPromise> promise = node->GetPromise();
  MaybeHandle<Object> result =
      JSPromise::Resolve(promise, isolate->factory()->undefined_value());
  USE(result);
  RemovePromiseFromNativeContext(isolate, promise);
}

}  // namespace internal
}  // namespace v8
