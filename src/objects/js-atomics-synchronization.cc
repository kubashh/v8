// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-atomics-synchronization.h"

#include "include/v8-function.h"
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

Handle<JSPromise> SetPromiseThen(Isolate* isolate, Handle<JSPromise> promise,
                                 Handle<JSFunction> callable) {
  Handle<NativeContext> context(isolate->native_context());
  v8::Local<v8::Context> local_native_context = Utils::ToLocal(context);
  v8::Local<v8::Function> local_callable = Utils::ToLocal(callable);
  v8::Local<v8::Promise> local_promise =
      Utils::PromiseToLocal(Handle<JSObject>::cast(promise));
  MaybeLocal<Promise> local_then_promise =
      local_promise->Then(local_native_context, local_callable, local_callable);
  Handle<JSPromise> then_promise =
      Utils::OpenHandle(*local_then_promise.ToLocalChecked());
  return then_promise;
}
Handle<JSPromise> SetAsyncUnlockThen(Isolate* isolate,
                                     Handle<JSAtomicsMutex> mutex,
                                     Handle<JSPromise> promise) {
  Factory* factory = isolate->factory();
  Handle<NativeContext> context(isolate->native_context());

  Handle<SharedFunctionInfo> info = factory->NewSharedFunctionInfoForBuiltin(
      isolate->factory()->empty_string(), Builtin::kAtomicsMutexAsyncUnlock);
  info->set_language_mode(LanguageMode::kStrict);

  Handle<JSFunction> resolver_callback =
      Factory::JSFunctionBuilder{isolate, info, context}
          .set_map(isolate->strict_function_without_prototype_map())
          .Build();

  JSObject::AddProperty(isolate, resolver_callback, "Lock",
                        Handle<Object>::cast(mutex), PropertyAttributes::NONE);

  Handle<JSPromise> then_promise =
      SetPromiseThen(isolate, promise, resolver_callback);
  return then_promise;
}
}  // namespace

namespace detail {

// To manage waiting threads, there is a process-wide doubly-linked intrusive
// list per waiter (i.e. mutex or condition variable). There is a per-thread
// node allocated on the stack when the thread goes to sleep during
// waiting.
//
// WaiterQueueNodes have the following invariants.
//
// 1. A WaiterQueueNode is on at most one waiter list at a time, since waiting
//    puts the thread to sleep while awaiting wakeup (i.e. a mutex unlock or a
//    condition variable notify).
//
// 2. Similarly, a WaiterQueueNode is encoded as the state field on at most one
//    JSSynchronizationPrimitive.
//
// When compressing pointers (including when sandboxing), the access to the
// on-stack node is indirected through the shared external pointer table. This
// relaxes the alignment requirement for the state field to be 4 bytes on all
// architectures. In the case of sandboxing this also improves security. Since
// the WaiterQueueNode is per-thread, there is one external pointer per
// main-thread Isolate.
//
// When compressing pointers WaiterQueueNodes have the following additional
// invariants.
//
// 3. If a WaiterQueueNode is encoded as a JSSynchronizationPrimitive's state
//    (i.e. a synchronization primitive has blocked some main thread Isolate,
//    and that main thread is the head of the waiter list), the Isolate's
//    external pointer points to that WaiterQueueNode. Otherwise the external
//    pointer points to null.
class V8_NODISCARD WaiterQueueNode {
 public:
  explicit WaiterQueueNode(Isolate* requester) : requester_(requester) {}

  template <typename T>
  static typename T::StateT EncodeHead(Isolate* requester,
                                       WaiterQueueNode* head) {
#ifdef V8_COMPRESS_POINTERS
    if (head == nullptr) return 0;

    if (DEBUG_BOOL) {
      // See invariant 3 above.
      Address old = requester->shared_external_pointer_table().Exchange(
          head->external_pointer_handle_, reinterpret_cast<Address>(head),
          kWaiterQueueNodeTag);
      DCHECK_EQ(kNullAddress, old);
      USE(old);
    } else {
      requester->shared_external_pointer_table().Set(
          head->external_pointer_handle_, reinterpret_cast<Address>(head),
          kWaiterQueueNodeTag);
    }

    auto state =
        static_cast<typename T::StateT>(head->external_pointer_handle_);
#else
    auto state = base::bit_cast<typename T::StateT>(head);
#endif  // V8_COMPRESS_POINTERS

    DCHECK_EQ(0, state & T::kLockBitsMask);
    return state;
  }

  // Decode a WaiterQueueNode from the state. This is a destructive operation
  // when sandboxing external pointers to prevent reuse.
  template <typename T>
  static WaiterQueueNode* DestructivelyDecodeHead(Isolate* requester,
                                                  typename T::StateT state) {
#ifdef V8_COMPRESS_POINTERS
    ExternalPointerHandle handle =
        static_cast<ExternalPointerHandle>(state & T::kWaiterQueueHeadMask);
    if (handle == 0) return nullptr;
    // The external pointer is cleared after decoding to prevent reuse by
    // multiple synchronization primitives in case of heap corruption.
    return reinterpret_cast<WaiterQueueNode*>(
        requester->shared_external_pointer_table().Exchange(
            handle, kNullAddress, kWaiterQueueNodeTag));
#else
    return base::bit_cast<WaiterQueueNode*>(state & T::kWaiterQueueHeadMask);
#endif  // V8_COMPRESS_POINTERS
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
        if (next == original_head) {
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
  void SetNotInListForVerificationWrapper() { SetNotInListForVerification(); }

  Isolate* requester_;
#ifdef V8_COMPRESS_POINTERS
  ExternalPointerHandle external_pointer_handle_;
#endif

 private:
  void VerifyNotInList() {
    DCHECK_NULL(next_);
    DCHECK_NULL(prev_);
  }

  void SetNotInListForVerification() {
#ifdef DEBUG
    next_ = prev_ = nullptr;
#endif
  }

  // The queue wraps around, e.g. the head's prev is the tail, and the tail's
  // next is the head.
  WaiterQueueNode* next_ = nullptr;
  WaiterQueueNode* prev_ = nullptr;
};

class V8_NODISCARD SyncWaiterQueueNode final : public WaiterQueueNode {
 public:
  explicit SyncWaiterQueueNode(Isolate* requester)
      : WaiterQueueNode(requester) {
#ifdef V8_COMPRESS_POINTERS
    external_pointer_handle_ =
        requester->GetOrCreateWaiterQueueNodeExternalPointer();
#endif  // V8_COMPRESS_POINTERS
  }

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
    base::MutexGuard guard(GetWaitLock());
    should_wait = false;
    GetWaitConditionVariable()->NotifyOne();
    SetNotInListForVerificationWrapper();
  }

 private:
  base::Mutex* GetWaitLock() { return &wait_lock_; }
  base::ConditionVariable* GetWaitConditionVariable() {
    return &wait_cond_var_;
  }

  base::Mutex wait_lock_;
  base::ConditionVariable wait_cond_var_;
};

template <typename T>
class AsyncWaiterNotifyTask : public CancelableTask {
 public:
  AsyncWaiterNotifyTask(CancelableTaskManager* cancelable_task_manager, T* node)
      : CancelableTask(cancelable_task_manager), node_(node) {}

  void RunInternal() override;

 private:
  std::unique_ptr<T> node_;
};

class V8_NODISCARD AsyncWaiterQueueNode : public WaiterQueueNode {
 public:
  Handle<JSAtomicsMutex> GetMutex() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    Handle<JSAtomicsMutex> mutex_lock = Handle<JSAtomicsMutex>::cast(
        Utils::OpenHandle(*mutex_.Get(v8_isolate)));
    return mutex_lock;
  }

  Handle<JSPromise> GetPromise() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    Handle<JSPromise> promise = Utils::OpenHandle(*promise_.Get(v8_isolate));
    return promise;
  }

  TaskRunner* task_runner() { return task_runner_.get(); }

  void set_timeout_task_id(CancelableTaskManager::Id timeout_task_id) {
    timeout_task_id_ = timeout_task_id;
  }

  ~AsyncWaiterQueueNode() {
    promise_.Reset();
    mutex_.Reset();
  }

 protected:
  explicit AsyncWaiterQueueNode(Isolate* requester, Handle<JSObject> mutex,
                                Handle<JSPromise> promise)
      : WaiterQueueNode(requester) {
#ifdef V8_COMPRESS_POINTERS
    external_pointer_handle_ =
        requester->CreateWaiterQueueNodeExternalPointer();
#endif  // V8_COMPRESS_POINTERS

    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester);
    task_runner_ =
        V8::GetCurrentPlatform()->GetForegroundTaskRunner(v8_isolate);
    // We need to keep the mutex, callback, and promise alive until the task
    // is run.
    v8::Local<v8::Object> local_mutex = Utils::ToLocal(mutex);
    mutex_.Reset(v8_isolate, local_mutex);
    v8::Local<v8::Promise> local_promise = Utils::PromiseToLocal(promise);
    promise_.Reset(v8_isolate, local_promise);

    timeout_task_id_ = CancelableTaskManager::kInvalidTaskId;
  }
  std::shared_ptr<TaskRunner> task_runner_;
  v8::Global<v8::Object> mutex_;
  v8::Global<v8::Promise> promise_;
  CancelableTaskManager::Id timeout_task_id_;
};

class AsyncLockWaiterQueueNode final : public AsyncWaiterQueueNode {
 public:
  AsyncLockWaiterQueueNode(Isolate* requester, Handle<JSObject> mutex,
                           Handle<JSPromise> promise)
      : AsyncWaiterQueueNode(requester, mutex, promise) {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester);
    v8::Local<v8::Context> native_context =
        Utils::ToLocal(requester->native_context());
    native_context_.Reset(v8_isolate, native_context);
  }

  ~AsyncLockWaiterQueueNode() { native_context_.Reset(); }

  void Notify() override {
    if (GetRequester()->cancelable_task_manager()->canceled()) return;
    // Post a task back to the thread that owns this node
    should_wait = false;
    auto task =
        std::make_unique<AsyncWaiterNotifyTask<AsyncLockWaiterQueueNode>>(
            GetRequester()->cancelable_task_manager(), this);
    task_runner_->PostNonNestableTask(std::move(task));
  }

  Local<v8::Context> native_context() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    return native_context_.Get(v8_isolate);
  }

  Handle<Context> native_context_handle() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    return Utils::OpenHandle(*native_context_.Get(v8_isolate));
  }

 private:
  Global<v8::Context> native_context_;
};

class AsyncWaitWaiterQueueNode final : public AsyncWaiterQueueNode {
 public:
  AsyncWaitWaiterQueueNode(Isolate* requester, Handle<JSObject> mutex,
                           Handle<JSPromise> promise,
                           Handle<JSAtomicsCondition> cv)
      : AsyncWaiterQueueNode(requester, mutex, promise) {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester);
    v8::Local<v8::Context> native_context =
        Utils::ToLocal(requester->native_context());
    native_context_.Reset(v8_isolate, native_context);
    v8::Local<v8::Object> local_cv = Utils::ToLocal(Handle<JSObject>::cast(cv));
    cv_.Reset(v8_isolate, local_cv);
  }

  ~AsyncWaitWaiterQueueNode() { native_context_.Reset(); }

  void Notify() override {
    CancelableTaskManager* task_manager =
        GetRequester()->cancelable_task_manager();
    if (task_manager->canceled()) return;
    // Post a task back to the thread that owns this node
    if (timeout_task_id_ != CancelableTaskManager::kInvalidTaskId) {
      task_manager->TryAbort(timeout_task_id_);
    }
    auto task =
        std::make_unique<AsyncWaiterNotifyTask<AsyncWaitWaiterQueueNode>>(
            task_manager, this);
    task_runner_->PostNonNestableTask(std::move(task));
  }

  Handle<JSAtomicsCondition> GetConditionVariable() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    Handle<JSAtomicsCondition> cv = Handle<JSAtomicsCondition>::cast(
        Utils::OpenHandle(*cv_.Get(v8_isolate)));
    return cv;
  }

  Local<v8::Context> native_context() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    return native_context_.Get(v8_isolate);
  }

  Handle<Context> native_context_handle() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    return Utils::OpenHandle(*native_context_.Get(v8_isolate));
  }

 private:
  Global<v8::Context> native_context_;
  Global<v8::Object> cv_;
};

template <typename T>
class AsyncTimeoutTask : public CancelableTask {
 public:
  AsyncTimeoutTask(CancelableTaskManager* cancelable_task_manager, T* node)
      : CancelableTask(cancelable_task_manager), node_(node) {}

  void RunInternal() override {
    if (node_->GetRequester()->cancelable_task_manager()->canceled()) return;
    Isolate* isolate = node_->GetRequester();
    HandleScope scope(isolate);
    WaiterQueueNode* waiter = JSAtomicsCondition::RemoveTimedOutWaiter(
        node_->GetConditionVariable(), node_);
    if (waiter) {
      JSAtomicsCondition::HandleAsyncNotify(node_);
      delete node_;
    }
  }

 private:
  T* node_;
};

template <>
void AsyncWaiterNotifyTask<AsyncLockWaiterQueueNode>::RunInternal() {
  JSAtomicsMutex::HandleAsyncNotify(node_.get());
}

template <>
void AsyncWaiterNotifyTask<AsyncWaitWaiterQueueNode>::RunInternal() {
  JSAtomicsCondition::HandleAsyncNotify(node_.get());
}

}  // namespace detail

using detail::AsyncLockWaiterQueueNode;
using detail::AsyncWaitWaiterQueueNode;
using detail::SyncWaiterQueueNode;
using detail::WaiterQueueNode;
using AsyncWaitTimeoutTask = detail::AsyncTimeoutTask<AsyncWaitWaiterQueueNode>;
using AsyncLockTimeoutTask = detail::AsyncTimeoutTask<AsyncLockWaiterQueueNode>;

// static
Handle<JSPromise> JSAtomicsMutex::LockOrQueuePromise(
    Isolate* isolate, Handle<JSAtomicsMutex> mutex,
    Handle<JSObject> run_under_lock) {
  Handle<JSPromise> promise = isolate->factory()->NewJSPromise();
  Handle<JSPromise> callable_then = SetPromiseThen(
      isolate, promise, Handle<JSFunction>::cast(run_under_lock));
  Handle<JSPromise> unlock_then =
      SetAsyncUnlockThen(isolate, mutex, callable_then);
  if (AsyncLock(isolate, mutex, promise)) {
    MaybeHandle<Object> result =
        JSPromise::Resolve(promise, isolate->factory()->undefined_value());
    USE(result);
  }
  return unlock_then;
}

// static
bool JSAtomicsMutex::AsyncLockSlowPath(Handle<JSAtomicsMutex> mutex,
                                       Isolate* isolate,
                                       Handle<JSPromise> promise,
                                       std::atomic<StateT>* state) {
  // Spin for a little bit to try to acquire the lock, so as to be fast under
  // microcontention.
  if (SpinningMutexTryLock(isolate, mutex, state)) {
    mutex->SetCurrentThreadAsOwner();
    return true;
  }

  // At this point the lock is considered contended, so try to go to sleep and
  // put the requester thread on the waiter queue.

  std::unique_ptr<AsyncLockWaiterQueueNode> this_waiter =
      std::make_unique<AsyncLockWaiterQueueNode>(isolate, mutex, promise);
  if (!MaybeEnqueueNode(isolate, mutex, state, this_waiter.get())) {
    mutex->SetCurrentThreadAsOwner();
    return true;
  }
  this_waiter.release();
  return false;
}

// static
void JSAtomicsMutex::HandleAsyncNotify(AsyncLockWaiterQueueNode* node) {
  if (node->GetRequester()->cancelable_task_manager()->canceled()) return;
  Isolate* isolate = node->GetRequester();
  HandleScope scope(isolate);

  Handle<JSAtomicsMutex> mutex_lock = node->GetMutex();
  Handle<JSPromise> promise = node->GetPromise();

  v8::Context::Scope contextScope(node->native_context());

  if (AsyncLock(isolate, mutex_lock, promise)) {
    MaybeHandle<Object> result =
        JSPromise::Resolve(promise, isolate->factory()->undefined_value());
    USE(result);
  }
}

// static
bool JSAtomicsMutex::TryLockExplicit(std::atomic<StateT>* state,
                                     StateT& expected) {
  // Try to lock a possibly contended mutex.
  expected &= ~kIsLockedBit;
  return state->compare_exchange_weak(expected, expected | kIsLockedBit,
                                      std::memory_order_acquire,
                                      std::memory_order_relaxed);
}

// static
bool JSAtomicsMutex::TryLockWaiterQueueExplicit(std::atomic<StateT>* state,
                                                StateT& expected) {
  // Try to acquire the queue lock.
  expected &= ~kIsWaiterQueueLockedBit;
  return state->compare_exchange_weak(
      expected, expected | kIsWaiterQueueLockedBit, std::memory_order_acquire,
      std::memory_order_relaxed);
}

bool JSAtomicsMutex::SpinningMutexTryLock(Isolate* requester,
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
    if ((current_state & kIsLockedBit) &&
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
      WaiterQueueNode::DestructivelyDecodeHead<JSAtomicsMutex>(requester,
                                                               current_state);
  WaiterQueueNode::Enqueue(&waiter_head, this_waiter);

  // Release the queue lock and install the new waiter queue head by
  // creating a new state.
  DCHECK_EQ(state->load(), current_state | kIsWaiterQueueLockedBit);
  StateT new_state =
      WaiterQueueNode::EncodeHead<JSAtomicsMutex>(requester, waiter_head);
  // The lock is held, just not by us, so don't set the current thread id as
  // the owner.
  DCHECK(current_state & kIsLockedBit);
  DCHECK(!mutex->IsCurrentThreadOwner());
  new_state |= kIsLockedBit;
  state->store(new_state, std::memory_order_release);
  return true;
}

// static
void JSAtomicsMutex::UnlockWaiterQueueWithNewState(std::atomic<StateT>* state,
                                                   StateT new_state) {
  // Set the new state without changing the `kIsLockedBit` bit.
  DCHECK_EQ(new_state & kQueueMask, new_state);
  StateT expected = state->load(std::memory_order_relaxed);
  StateT desired;
  do {
    desired = new_state | (expected & kIsLockedBit);
  } while (!state->compare_exchange_weak(
      expected, desired, std::memory_order_release, std::memory_order_relaxed));
}

// static
bool JSAtomicsMutex::LockJSMutexOrDequeueTimedOutWaiter(
    Isolate* requester, std::atomic<StateT>* state,
    WaiterQueueNode* timed_out_waiter) {
  // First acquire the queue lock, which is itself a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);
  // There are no waiters, but the js mutex lock may be held by another thread.
  if (!(current_state & kQueueMask)) return false;
  while (!TryLockWaiterQueueExplicit(state, current_state)) {
    YIELD_PROCESSOR;
  }

  // Get the waiter queue head.
  WaiterQueueNode* waiter_head =
      WaiterQueueNode::DestructivelyDecodeHead<JSAtomicsCondition>(
          requester, current_state);

  if (waiter_head == nullptr) {
    // The queue is empty but the js mutex lock may be held by another thread,
    // release the waiter queue bit without changing `kIsLockedBit`.
    DCHECK_EQ(current_state & kQueueMask, 0);
    UnlockWaiterQueueWithNewState(state, kUnlocked);
    return false;
  }

  WaiterQueueNode* dequeued_node = WaiterQueueNode::DequeueMatching(
      &waiter_head,
      [&](WaiterQueueNode* node) { return node == timed_out_waiter; });

  // Release the queue lock and install the new waiter queue head by creating a
  // new state.
  DCHECK_EQ(state->load(), current_state | kIsWaiterQueueLockedBit);
  StateT new_state =
      WaiterQueueNode::EncodeHead<JSAtomicsCondition>(requester, waiter_head);

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
    // Hence, it is safe to always set the `kIsLockedBit` bit in new_state.
    new_state |= kIsLockedBit;
    DCHECK_EQ(new_state & kIsWaiterQueueLockedBit, 0);
    current_state &= ~kIsLockedBit;
    if (state->compare_exchange_strong(current_state, new_state,
                                       std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
      // The CAS atomically released the waiter queue lock and acquired the js
      // mutex lock.
      return true;
    }

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
    if (SpinningMutexTryLock(requester, mutex, state)) return;

    // At this point the lock is considered contended, so try to go to sleep and
    // put the requester thread on the waiter queue.

    // Allocate a waiter queue node on-stack, since this thread is going to
    // sleep and will be blocked anyway.
    SyncWaiterQueueNode this_waiter(requester);
    if (!MaybeEnqueueNode(requester, mutex, state, &this_waiter)) return;

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
        rv = LockJSMutexOrDequeueTimedOutWaiter(requester, state, &this_waiter);
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
  WaiterQueueNode* waiter_head =
      WaiterQueueNode::DestructivelyDecodeHead<JSAtomicsMutex>(requester,
                                                               current_state);
  WaiterQueueNode* old_head = WaiterQueueNode::Dequeue(&waiter_head);

  // Release both the lock and the queue lock and also install the new waiter
  // queue head by creating a new state.
  StateT new_state =
      WaiterQueueNode::EncodeHead<JSAtomicsMutex>(requester, waiter_head);
  state->store(new_state, std::memory_order_release);

  old_head->Notify();
}

// static
bool JSAtomicsCondition::TryLockWaiterQueueExplicit(std::atomic<StateT>* state,
                                                    StateT& expected) {
  // Try to acquire the queue lock.
  expected &= ~kIsWaiterQueueLockedBit;
  return state->compare_exchange_weak(
      expected, expected | kIsWaiterQueueLockedBit, std::memory_order_acquire,
      std::memory_order_relaxed);
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
  WaiterQueueNode* waiter_head =
      WaiterQueueNode::DestructivelyDecodeHead<JSAtomicsCondition>(
          requester, current_state);
  WaiterQueueNode::Enqueue(&waiter_head, waiter);

  // Release the queue lock and install the new waiter queue head by creating
  // a new state.
  DCHECK_EQ(state->load(), current_state | kIsWaiterQueueLockedBit);
  StateT new_state =
      WaiterQueueNode::EncodeHead<JSAtomicsCondition>(requester, waiter_head);
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
      DequeueExplicit(requester, state, [&](WaiterQueueNode** waiter_head) {
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
    Isolate* requester, std::atomic<StateT>* state,
    const DequeueAction& action_under_lock) {
  // First acquire the queue lock, which is itself a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);
  // There are no waiters.
  if (current_state == kEmptyState) return nullptr;
  while (!TryLockWaiterQueueExplicit(state, current_state)) {
    YIELD_PROCESSOR;
  }

  // Get the waiter queue head.
  WaiterQueueNode* waiter_head =
      WaiterQueueNode::DestructivelyDecodeHead<JSAtomicsCondition>(
          requester, current_state);

  // There's no waiter to wake up, release the queue lock by setting it to the
  // empty state.
  if (waiter_head == nullptr) {
    DCHECK_EQ(state->load(), current_state | kIsWaiterQueueLockedBit);
    state->store(kEmptyState, std::memory_order_release);
    return nullptr;
  }

  WaiterQueueNode* old_head = action_under_lock(&waiter_head);

  // Release the queue lock and install the new waiter queue head by creating a
  // new state.
  DCHECK_EQ(state->load(), current_state | kIsWaiterQueueLockedBit);
  StateT new_state =
      WaiterQueueNode::EncodeHead<JSAtomicsCondition>(requester, waiter_head);
  state->store(new_state, std::memory_order_release);

  return old_head;
}

uint32_t JSAtomicsCondition::Notify(Isolate* requester, uint32_t count) {
  std::atomic<StateT>* state = AtomicStatePtr();

  // Dequeue count waiters.
  WaiterQueueNode* old_head =
      DequeueExplicit(requester, state, [=](WaiterQueueNode** waiter_head) {
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

Tagged<Object> JSAtomicsCondition::NumWaitersForTesting(Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  std::atomic<StateT>* state = AtomicStatePtr();
  StateT current_state = state->load(std::memory_order_relaxed);

  // There are no waiters.
  if (current_state == kEmptyState) return Smi::FromInt(0);

  int num_waiters;
  {
    // Take the queue lock.
    while (!TryLockWaiterQueueExplicit(state, current_state)) {
      YIELD_PROCESSOR;
    }

    // Get the waiter queue head.
    WaiterQueueNode* waiter_head =
        WaiterQueueNode::DestructivelyDecodeHead<JSAtomicsCondition>(
            isolate, current_state);
    num_waiters = WaiterQueueNode::LengthFromHead(waiter_head);

    // Release the queue lock and reinstall the same queue head by creating a
    // new state.
    DCHECK_EQ(state->load(), current_state | kIsWaiterQueueLockedBit);
    StateT new_state =
        WaiterQueueNode::EncodeHead<JSAtomicsCondition>(isolate, waiter_head);
    state->store(new_state, std::memory_order_release);
  }

  return Smi::FromInt(num_waiters);
}

// static
Handle<JSPromise> JSAtomicsCondition::WaitAsync(
    Isolate* requester, Handle<JSAtomicsCondition> cv,
    Handle<JSAtomicsMutex> mutex, base::Optional<base::TimeDelta> timeout) {
  Handle<JSPromise> promise = requester->factory()->NewJSPromise();
  std::unique_ptr<AsyncWaitWaiterQueueNode> this_waiter =
      std::make_unique<AsyncWaitWaiterQueueNode>(requester, mutex, promise, cv);
  QueueWaiter(requester, cv, this_waiter.get());
  if (timeout) {
    TaskRunner* taks_runner = this_waiter->task_runner();
    auto task = std::make_unique<AsyncWaitTimeoutTask>(
        requester->cancelable_task_manager(), this_waiter.get());
    this_waiter->set_timeout_task_id(task->id());
    taks_runner->PostNonNestableDelayedTask(std::move(task),
                                            timeout->InSecondsF());
  }
  this_waiter.release();

  mutex->Unlock(requester);
  return promise;
}

// static
void JSAtomicsCondition::HandleAsyncNotify(AsyncWaitWaiterQueueNode* node) {
  if (node->GetRequester()->cancelable_task_manager()->canceled()) return;
  Isolate* isolate = node->GetRequester();
  HandleScope scope(isolate);

  Handle<JSAtomicsMutex> mutex_lock = node->GetMutex();
  Handle<JSPromise> promise = node->GetPromise();
  Handle<Context> native_context = node->native_context_handle();
  Handle<JSGlobalObject> global(native_context->global_object(), isolate);
  Handle<JSReceiver> atomics_object = Handle<JSReceiver>::cast(
      JSReceiver::GetProperty(isolate, global, "Atomics").ToHandleChecked());
  Handle<JSReceiver> mutex_object = Handle<JSReceiver>::cast(
      JSReceiver::GetProperty(isolate, atomics_object, "Mutex")
          .ToHandleChecked());
  Handle<JSFunction> lock_async_function = Handle<JSFunction>::cast(
      JSReceiver::GetProperty(isolate, mutex_object, "lockAsync")
          .ToHandleChecked());

  Handle<SharedFunctionInfo> info =
      isolate->factory()->NewSharedFunctionInfoForBuiltin(
          isolate->factory()->empty_string(),
          Builtin::kAtomicsConditionLockCallback);
  info->set_language_mode(LanguageMode::kStrict);

  v8::Context::Scope contextScope(node->native_context());
  Handle<JSFunction> resolver_callback =
      Factory::JSFunctionBuilder{isolate, info, isolate->native_context()}
          .set_map(isolate->strict_function_without_prototype_map())
          .Build();

  JSObject::AddProperty(isolate, resolver_callback, "cvPromise",
                        Handle<Object>::cast(promise),
                        PropertyAttributes::NONE);

  Handle<Object> argv[] = {mutex_lock, resolver_callback};
  MaybeHandle<Object> result =
      Execution::Call(isolate, lock_async_function,
                      isolate->factory()->undefined_value(), 2, argv);
  USE(result);
}

WaiterQueueNode* JSAtomicsCondition::RemoveTimedOutWaiter(
    Handle<JSAtomicsCondition> cv, AsyncWaitWaiterQueueNode* async_waiter) {
  Isolate* requester = async_waiter->GetRequester();
  std::atomic<StateT>* state = cv->AtomicStatePtr();

  return DequeueExplicit(requester, state, [&](WaiterQueueNode** waiter_head) {
    return WaiterQueueNode::DequeueMatching(
        waiter_head,
        [&](WaiterQueueNode* node) { return node == async_waiter; });
  });
}

}  // namespace internal
}  // namespace v8
