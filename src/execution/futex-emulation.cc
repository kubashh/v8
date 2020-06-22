// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/futex-emulation.h"

#include <limits>

#include "src/api/api-inl.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/execution/isolate.h"
#include "src/execution/vm-state-inl.h"
#include "src/handles/handles-inl.h"
#include "src/numbers/conversions.h"
#include "src/objects/bigint.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-promise-inl.h"
#include "src/objects/objects-inl.h"
#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

using AtomicsWaitEvent = v8::Isolate::AtomicsWaitEvent;

base::LazyMutex FutexEmulation::mutex_ = LAZY_MUTEX_INITIALIZER;
base::LazyInstance<FutexWaitList>::type FutexEmulation::wait_list_ =
    LAZY_INSTANCE_INITIALIZER;

base::LazyInstance<std::set<Isolate*>>::type
    FutexEmulation::isolates_resolve_task_scheduled_ =
        LAZY_INSTANCE_INITIALIZER;

void FutexWaitListNode::NotifyWake() {
  // Lock the FutexEmulation mutex before notifying. We know that the mutex
  // will have been unlocked if we are currently waiting on the condition
  // variable. The mutex will not be locked if FutexEmulation::Wait hasn't
  // locked it yet. In that case, we set the interrupted_
  // flag to true, which will be tested after the mutex locked by a future wait.
  base::MutexGuard lock_guard(FutexEmulation::mutex_.Pointer());
  // if not waiting, this will not have any effect.
  cond_.NotifyOne();
  interrupted_ = true;
}

class ResolveAsyncWaiterPromisesTask : public CancelableTask {
 public:
  explicit ResolveAsyncWaiterPromisesTask(Isolate* isolate)
      : CancelableTask(isolate), isolate_(isolate) {}

  void RunInternal() override {
    FutexEmulation::ResolveAsyncWaiterPromises(isolate_);
  }

 private:
  Isolate* isolate_;
};

class AsyncWaiterTimeoutTask : public CancelableTask {
 public:
  explicit AsyncWaiterTimeoutTask(Isolate* isolate) : CancelableTask(isolate) {}

  void RunInternal() override { FutexEmulation::HandleAsyncWaiterTimeouts(); }
};

void FutexWaitListNode::Notify(bool woken_up) {
  if (isolate_for_async_waiters_) {
    // Async waiter
    DCHECK(FLAG_harmony_atomics_waitasync);
    if (woken_up) {
      // Nullify the timeout time; this distinguishes timed out waiters from
      // woken up ones.
      timeout_time_ = base::TimeTicks();
    }

    FutexEmulation::mutex_.Pointer()->AssertHeld();
    if (FutexEmulation::isolates_resolve_task_scheduled_.Pointer()
            ->insert(isolate_for_async_waiters_)
            .second) {
      auto task = std::make_unique<ResolveAsyncWaiterPromisesTask>(
          isolate_for_async_waiters_);
      task_runner_->PostNonNestableTask(std::move(task));
    }
  } else {
    // Sync waiter
    cond_.NotifyOne();
  }
}

void FutexWaitList::AddNode(FutexWaitListNode* node) {
  DCHECK_NULL(node->prev_);
  DCHECK_NULL(node->next_);
  if (tail_) {
    tail_->next_ = node;
  } else {
    head_ = node;
  }

  node->prev_ = tail_;
  tail_ = node;
}

bool FutexWaitList::AddNodeToAsyncTimeoutList(FutexWaitListNode* node) {
  DCHECK_NULL(node->async_timeout_prev_);
  DCHECK_NULL(node->async_timeout_next_);
  if (!async_timeout_head_) {
    async_timeout_head_ = node;
    return true;
  }

  // Find the correct insertion place (between before_node and after_node). Keep
  // the list sorted in ascending order of timeout_time_.
  FutexWaitListNode* after_node = async_timeout_head_;
  FutexWaitListNode* before_node = nullptr;
  while (after_node != nullptr &&
         after_node->timeout_time_ < node->timeout_time_) {
    before_node = after_node;
    after_node = after_node->async_timeout_next_;
  }

  // Insert between before_node and after_node.
  node->async_timeout_next_ = after_node;
  if (after_node) {
    after_node->async_timeout_prev_ = node;
  }
  node->async_timeout_prev_ = before_node;
  if (before_node) {
    before_node->async_timeout_next_ = node;
  } else {
    async_timeout_head_ = node;
  }
  return (async_timeout_head_ == node);
}

void FutexWaitList::RemoveNode(FutexWaitListNode* node) {
  if (node->prev_) {
    node->prev_->next_ = node->next_;
  } else {
    head_ = node->next_;
  }

  if (node->next_) {
    node->next_->prev_ = node->prev_;
  } else {
    tail_ = node->prev_;
  }

  node->prev_ = node->next_ = nullptr;

  if (node->async_timeout_prev_) {
    DCHECK_NE(async_timeout_head_, node);
    node->async_timeout_prev_->async_timeout_next_ = node->async_timeout_next_;
  } else if (async_timeout_head_ == node) {
    async_timeout_head_ = node->async_timeout_next_;
  } else {
    // This node has no timeout.
    DCHECK_EQ(base::TimeTicks(), node->timeout_time_);
  }

  if (node->async_timeout_next_) {
    node->async_timeout_next_->async_timeout_prev_ = node->async_timeout_prev_;
  }

  node->async_timeout_prev_ = node->async_timeout_next_ = nullptr;
}

void AtomicsWaitWakeHandle::Wake() {
  // Adding a separate `NotifyWake()` variant that doesn't acquire the lock
  // itself would likely just add unnecessary complexity..
  // The split lock by itself isn’t an issue, as long as the caller properly
  // synchronizes this with the closing `AtomicsWaitCallback`.
  {
    base::MutexGuard lock_guard(FutexEmulation::mutex_.Pointer());
    stopped_ = true;
  }
  isolate_->futex_wait_list_node()->NotifyWake();
}

enum WaitReturnValue : int { kOk = 0, kNotEqual = 1, kTimedOut = 2 };

namespace {

Object WaitJsTranslateReturn(Isolate* isolate, Object res) {
  if (res.IsSmi()) {
    int val = Smi::ToInt(res);
    switch (val) {
      case WaitReturnValue::kOk:
        return ReadOnlyRoots(isolate).ok_string();
      case WaitReturnValue::kNotEqual:
        return ReadOnlyRoots(isolate).not_equal_string();
      case WaitReturnValue::kTimedOut:
        return ReadOnlyRoots(isolate).timed_out_string();
      default:
        UNREACHABLE();
    }
  }
  return res;
}

}  // namespace

Object FutexEmulation::WaitJs32(Isolate* isolate, WaitMode mode,
                                Handle<JSArrayBuffer> array_buffer, size_t addr,
                                int32_t value, double rel_timeout_ms) {
  Object res =
      Wait<int32_t>(isolate, mode, array_buffer, addr, value, rel_timeout_ms);
  return WaitJsTranslateReturn(isolate, res);
}

Object FutexEmulation::WaitJs64(Isolate* isolate, WaitMode mode,
                                Handle<JSArrayBuffer> array_buffer, size_t addr,
                                int64_t value, double rel_timeout_ms) {
  Object res =
      Wait<int64_t>(isolate, mode, array_buffer, addr, value, rel_timeout_ms);
  return WaitJsTranslateReturn(isolate, res);
}

Object FutexEmulation::WaitWasm32(Isolate* isolate,
                                  Handle<JSArrayBuffer> array_buffer,
                                  size_t addr, int32_t value,
                                  int64_t rel_timeout_ns) {
  return Wait<int32_t>(isolate, WaitMode::kSync, array_buffer, addr, value,
                       rel_timeout_ns >= 0, rel_timeout_ns);
}

Object FutexEmulation::WaitWasm64(Isolate* isolate,
                                  Handle<JSArrayBuffer> array_buffer,
                                  size_t addr, int64_t value,
                                  int64_t rel_timeout_ns) {
  return Wait<int64_t>(isolate, WaitMode::kSync, array_buffer, addr, value,
                       rel_timeout_ns >= 0, rel_timeout_ns);
}

template <typename T>
Object FutexEmulation::Wait(Isolate* isolate, WaitMode mode,
                            Handle<JSArrayBuffer> array_buffer, size_t addr,
                            T value, double rel_timeout_ms) {
  DCHECK_LT(addr, array_buffer->byte_length());

  bool use_timeout = rel_timeout_ms != V8_INFINITY;
  int64_t rel_timeout_ns = -1;

  if (use_timeout) {
    // Convert to nanoseconds.
    double timeout_ns = rel_timeout_ms *
                        base::Time::kNanosecondsPerMicrosecond *
                        base::Time::kMicrosecondsPerMillisecond;
    if (timeout_ns > static_cast<double>(std::numeric_limits<int64_t>::max())) {
      // 2**63 nanoseconds is 292 years. Let's just treat anything greater as
      // infinite.
      use_timeout = false;
    } else {
      rel_timeout_ns = static_cast<int64_t>(timeout_ns);
    }
  }
  return Wait(isolate, mode, array_buffer, addr, value, use_timeout,
              rel_timeout_ns);
}

namespace {
double WaitTimeoutInMs(double timeout_ns) {
  return timeout_ns < 0
             ? V8_INFINITY
             : timeout_ns / (base::Time::kNanosecondsPerMicrosecond *
                             base::Time::kMicrosecondsPerMillisecond);
}
}  // namespace

template <typename T>
Object FutexEmulation::Wait(Isolate* isolate, WaitMode mode,
                            Handle<JSArrayBuffer> array_buffer, size_t addr,
                            T value, bool use_timeout, int64_t rel_timeout_ns) {
  if (mode == WaitMode::kSync) {
    return WaitSync(isolate, array_buffer, addr, value, use_timeout,
                    rel_timeout_ns);
  }
  DCHECK_EQ(mode, WaitMode::kAsync);
  return WaitAsync(isolate, array_buffer, addr, value, use_timeout,
                   rel_timeout_ns);
}

template <typename T>
Object FutexEmulation::WaitSync(Isolate* isolate,
                                Handle<JSArrayBuffer> array_buffer, size_t addr,
                                T value, bool use_timeout,
                                int64_t rel_timeout_ns) {
  VMState<ATOMICS_WAIT> state(isolate);
  base::TimeDelta rel_timeout =
      base::TimeDelta::FromNanoseconds(rel_timeout_ns);

  // We have to convert the timeout back to double for the AtomicsWaitCallback.
  double rel_timeout_ms = WaitTimeoutInMs(static_cast<double>(rel_timeout_ns));

  AtomicsWaitWakeHandle stop_handle(isolate);
  isolate->RunAtomicsWaitCallback(AtomicsWaitEvent::kStartWait, array_buffer,
                                  addr, value, rel_timeout_ms, &stop_handle);

  if (isolate->has_scheduled_exception()) {
    return isolate->PromoteScheduledException();
  }

  Handle<Object> result;
  AtomicsWaitEvent callback_result = AtomicsWaitEvent::kWokenUp;

  do {  // Not really a loop, just makes it easier to break out early.
    base::MutexGuard lock_guard(mutex_.Pointer());
    std::shared_ptr<BackingStore> backing_store =
        array_buffer->GetBackingStore();
    DCHECK(backing_store);
    FutexWaitListNode* node = isolate->futex_wait_list_node();
    node->backing_store_ = backing_store;
    node->wait_addr_ = addr;
    node->waiting_ = true;

    // Reset node->waiting_ = false when leaving this scope (but while
    // still holding the lock).
    ResetWaitingOnScopeExit reset_waiting(node);

    T* p = reinterpret_cast<T*>(
        static_cast<int8_t*>(backing_store->buffer_start()) + addr);
    if (*p != value) {
      result = handle(Smi::FromInt(WaitReturnValue::kNotEqual), isolate);
      callback_result = AtomicsWaitEvent::kNotEqual;
      break;
    }

    base::TimeTicks timeout_time;
    base::TimeTicks current_time;

    if (use_timeout) {
      current_time = base::TimeTicks::Now();
      timeout_time = current_time + rel_timeout;
    }

    wait_list_.Pointer()->AddNode(node);
    VerifyFutexWaitList();

    while (true) {
      bool interrupted = node->interrupted_;
      node->interrupted_ = false;

      // Unlock the mutex here to prevent deadlock from lock ordering between
      // mutex_ and mutexes locked by HandleInterrupts.
      mutex_.Pointer()->Unlock();

      // Because the mutex is unlocked, we have to be careful about not dropping
      // an interrupt. The notification can happen in three different places:
      // 1) Before Wait is called: the notification will be dropped, but
      //    interrupted_ will be set to 1. This will be checked below.
      // 2) After interrupted has been checked here, but before mutex_ is
      //    acquired: interrupted is checked again below, with mutex_ locked.
      //    Because the wakeup signal also acquires mutex_, we know it will not
      //    be able to notify until mutex_ is released below, when waiting on
      //    the condition variable.
      // 3) After the mutex is released in the call to WaitFor(): this
      // notification will wake up the condition variable. node->waiting() will
      // be false, so we'll loop and then check interrupts.
      if (interrupted) {
        Object interrupt_object = isolate->stack_guard()->HandleInterrupts();
        if (interrupt_object.IsException(isolate)) {
          result = handle(interrupt_object, isolate);
          callback_result = AtomicsWaitEvent::kTerminatedExecution;
          mutex_.Pointer()->Lock();
          break;
        }
      }

      mutex_.Pointer()->Lock();

      if (node->interrupted_) {
        // An interrupt occurred while the mutex_ was unlocked. Don't wait yet.
        continue;
      }

      if (stop_handle.has_stopped()) {
        node->waiting_ = false;
        callback_result = AtomicsWaitEvent::kAPIStopped;
      }

      if (!node->waiting_) {
        result = handle(Smi::FromInt(WaitReturnValue::kOk), isolate);
        break;
      }

      // No interrupts, now wait.
      if (use_timeout) {
        current_time = base::TimeTicks::Now();
        if (current_time >= timeout_time) {
          result = handle(Smi::FromInt(WaitReturnValue::kTimedOut), isolate);
          callback_result = AtomicsWaitEvent::kTimedOut;
          break;
        }

        base::TimeDelta time_until_timeout = timeout_time - current_time;
        DCHECK_GE(time_until_timeout.InMicroseconds(), 0);
        bool wait_for_result =
            node->cond_.WaitFor(mutex_.Pointer(), time_until_timeout);
        USE(wait_for_result);
      } else {
        node->cond_.Wait(mutex_.Pointer());
      }

      // Spurious wakeup, interrupt or timeout.
    }

    wait_list_.Pointer()->RemoveNode(node);
    VerifyFutexWaitList();
  } while (false);

  isolate->RunAtomicsWaitCallback(callback_result, array_buffer, addr, value,
                                  rel_timeout_ms, nullptr);

  if (isolate->has_scheduled_exception()) {
    CHECK_NE(callback_result, AtomicsWaitEvent::kTerminatedExecution);
    result = handle(isolate->PromoteScheduledException(), isolate);
  }

  return *result;
}

template <typename T>
Object FutexEmulation::WaitAsync(Isolate* isolate,
                                 Handle<JSArrayBuffer> array_buffer,
                                 size_t addr, T value, bool use_timeout,
                                 int64_t rel_timeout_ns) {
  DCHECK(FLAG_harmony_atomics_waitasync);
  base::TimeDelta rel_timeout =
      base::TimeDelta::FromNanoseconds(rel_timeout_ns);

  Factory* factory = isolate->factory();
  Handle<Object> result = factory->NewJSObject(isolate->object_function());

  do {  // Not really a loop, just makes it easier to break out early.
    base::MutexGuard lock_guard(mutex_.Pointer());
    std::shared_ptr<BackingStore> backing_store =
        array_buffer->GetBackingStore();

    // 17. Let w be ! AtomicLoad(typedArray, i).
    T* p = reinterpret_cast<T*>(
        static_cast<int8_t*>(backing_store->buffer_start()) + addr);
    if (*p != value) {
      // 18. If v is not equal to w, then
      //   a. Perform LeaveCriticalSection(WL).
      //   ...
      //   c. Perform ! CreateDataPropertyOrThrow(resultObject, "async", false).
      //   d. Perform ! CreateDataPropertyOrThrow(resultObject, "value",
      //     "not-equal").
      //   e. Return resultObject.
      MaybeHandle<Object> return_value = Object::SetProperty(
          isolate, result, factory->async_string(), factory->false_value());
      DCHECK(!return_value.is_null());
      USE(return_value);
      return_value =
          Object::SetProperty(isolate, result, factory->value_string(),
                              factory->not_equal_string());
      DCHECK(!return_value.is_null());
      break;
    }

    if (use_timeout && rel_timeout_ns == 0) {
      // 19. If t is 0 and mode is async, then
      //   ...
      //   b. Perform LeaveCriticalSection(WL).
      //   c. Perform ! CreateDataPropertyOrThrow(resultObject, "async", false).
      //   d. Perform ! CreateDataPropertyOrThrow(resultObject, "value",
      //     "timed-out").
      //   e. Return resultObject.
      MaybeHandle<Object> return_value = Object::SetProperty(
          isolate, result, factory->async_string(), factory->false_value());
      DCHECK(!return_value.is_null());
      USE(return_value);
      return_value =
          Object::SetProperty(isolate, result, factory->value_string(),
                              factory->timed_out_string());
      DCHECK(!return_value.is_null());
      break;
    }

    FutexWaitListNode* node = new FutexWaitListNode(isolate);

    node->backing_store_ = backing_store;
    node->wait_addr_ = addr;
    node->waiting_ = true;
    auto v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
    Handle<JSObject> promise_capability = factory->NewJSPromise();
    v8::Local<v8::Promise> local_promise_capability =
        Utils::PromiseToLocal(promise_capability);
    node->promise_.Reset(v8_isolate, local_promise_capability);
    node->promise_.SetWeak();
    Handle<NativeContext> native_context(isolate->native_context());
    v8::Local<v8::Context> local_native_context =
        Utils::ToLocal(Handle<Context>::cast(native_context));
    node->native_context_.Reset(v8_isolate, local_native_context);
    node->native_context_.SetWeak();

    // Add the Promise into the NativeContext's wait_async_promises list, so
    // that the list keeps it alive.
    Handle<ArrayList> promises(native_context->wait_async_promises(), isolate);
    Handle<ArrayList> new_promises =
        ArrayList::Add(isolate, promises, promise_capability);
    native_context->set_wait_async_promises(*new_promises);

    wait_list_.Pointer()->AddNode(node);
    VerifyFutexWaitList();

    if (use_timeout) {
      node->timeout_time_ = base::TimeTicks::Now() + rel_timeout;
      bool new_low_timeout =
          wait_list_.Pointer()->AddNodeToAsyncTimeoutList(node);
      VerifyFutexWaitList();
      if (new_low_timeout) {
        auto task = std::make_unique<AsyncWaiterTimeoutTask>(
            node->isolate_for_async_waiters_);
        node->task_runner_->PostNonNestableDelayedTask(
            std::move(task), rel_timeout.InSecondsF());
      }
    }

    // 26. Perform ! CreateDataPropertyOrThrow(resultObject, "async", true).
    // 27. Perform ! CreateDataPropertyOrThrow(resultObject, "value",
    // promiseCapability.[[Promise]]).
    // 28. Return resultObject.
    MaybeHandle<Object> return_value = Object::SetProperty(
        isolate, result, factory->async_string(), factory->true_value());
    DCHECK(!return_value.is_null());
    USE(return_value);

    return_value = Object::SetProperty(isolate, result, factory->value_string(),
                                       promise_capability);
    DCHECK(!return_value.is_null());
    USE(return_value);

    break;
  } while (false);

  return *result;
}

Object FutexEmulation::Wake(Handle<JSArrayBuffer> array_buffer, size_t addr,
                            uint32_t num_waiters_to_wake) {
  DCHECK_LT(addr, array_buffer->byte_length());

  int waiters_woken = 0;
  std::shared_ptr<BackingStore> backing_store = array_buffer->GetBackingStore();

  base::MutexGuard lock_guard(mutex_.Pointer());
  FutexWaitListNode* node = wait_list_.Pointer()->head_;
  while (node && num_waiters_to_wake > 0) {
    bool delete_this_node = false;
    std::shared_ptr<BackingStore> node_backing_store =
        node->backing_store_.lock();

    if (node->waiting_) {
      if (backing_store.get() == node_backing_store.get() &&
          addr == node->wait_addr_) {
        node->waiting_ = false;
        node->Notify(true);
        if (num_waiters_to_wake != kWakeAll) {
          --num_waiters_to_wake;
        }
        waiters_woken++;
      } else if (node_backing_store.get() == nullptr) {
        // Backing store has been deleted and the node is still waiting. It's
        // never going to be woken up, so we can clean it up now.
        delete_this_node = true;
      }
    }

    if (delete_this_node) {
      node = DeleteAsyncWaiterNode(node);
    } else {
      node = node->next_;
    }
  }

  return Smi::FromInt(waiters_woken);
}

FutexWaitListNode* FutexEmulation::DeleteAsyncWaiterNode(
    FutexWaitListNode* node) {
  DCHECK(FLAG_harmony_atomics_waitasync);
  DCHECK_NOT_NULL(node->isolate_for_async_waiters_);
  Isolate* isolate = node->isolate_for_async_waiters_;
  auto v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  if (!node->promise_.IsEmpty()) {
    Handle<JSPromise> promise = Handle<JSPromise>::cast(
        Utils::OpenHandle(*node->promise_.Get(v8_isolate)));

    if (!node->native_context_.IsEmpty()) {
      Handle<NativeContext> native_context = Handle<NativeContext>::cast(
          Utils::OpenHandle(*node->native_context_.Get(v8_isolate)));

      // Remove the Promise from the NativeContext's set. Possible future
      // optimization: This is an inefficient algorithm. Replace with a more
      // efficient one if needed.
      Handle<ArrayList> promises(native_context->wait_async_promises(),
                                 isolate);
      int length = promises->Length();
      for (int i = 0; i < length; ++i) {
        if (promises->Get(i) == *promise) {
          if (i < length - 1) {
            promises->Set(i, promises->Get(length - 1));
            promises->Clear(length - 1, *isolate->factory()->undefined_value());
            promises->SetLength(length - 1);
          } else {
            promises->Clear(i, *isolate->factory()->undefined_value());
          }
          break;
        }
      }
    }
  } else {
    // NativeContext keeps the Promise alive; if the Promise is dead then
    // surely NativeContext is too.
    DCHECK(node->native_context_.IsEmpty());
  }

  FutexWaitListNode* next = node->next_;
  wait_list_.Pointer()->RemoveNode(node);
  VerifyFutexWaitList();
  delete node;
  return next;
}

void FutexEmulation::ResolvePromise(FutexWaitListNode* node) {
  auto v8_isolate =
      reinterpret_cast<v8::Isolate*>(node->isolate_for_async_waiters_);

  if (!node->promise_.IsEmpty()) {
    Handle<JSPromise> promise = Handle<JSPromise>::cast(
        Utils::OpenHandle(*node->promise_.Get(v8_isolate)));
    Handle<String> result_string;
    if (node->timeout_time_ != base::TimeTicks()) {
      result_string =
          node->isolate_for_async_waiters_->factory()->timed_out_string();
    } else {
      result_string = node->isolate_for_async_waiters_->factory()->ok_string();
    }
    MaybeHandle<Object> resolve_result =
        JSPromise::Resolve(promise, result_string);
    DCHECK(!resolve_result.is_null());
    USE(resolve_result);
  }
}

void FutexEmulation::ResolveAsyncWaiterPromises(Isolate* isolate) {
  DCHECK(FLAG_harmony_atomics_waitasync);

  HandleScope handle_scope(isolate);
  base::MutexGuard lock_guard(mutex_.Pointer());

  isolates_resolve_task_scheduled_.Pointer()->erase(isolate);

  FutexWaitListNode* node = wait_list_.Pointer()->head_;
  while (node) {
    if (node->isolate_for_async_waiters_ == isolate && !node->waiting_) {
      ResolvePromise(node);
      node = DeleteAsyncWaiterNode(node);
    } else {
      node = node->next_;
    }
  }
}

void FutexEmulation::HandleAsyncWaiterTimeouts() {
  DCHECK(FLAG_harmony_atomics_waitasync);

  base::MutexGuard lock_guard(mutex_.Pointer());
  FutexWaitListNode* node = wait_list_.Pointer()->async_timeout_head_;
  auto current_time = base::TimeTicks::Now();

  while (node) {
    DCHECK_NOT_NULL(node->isolate_for_async_waiters_);
    DCHECK_NE(base::TimeTicks(), node->timeout_time_);
    if (current_time > node->timeout_time_) {
      // Async waiter timed out.
      if (node->waiting_) {
        HandleScope handle_scope(node->isolate_for_async_waiters_);
        node->waiting_ = false;
        ResolvePromise(node);
        auto old_node = node;
        node = node->async_timeout_next_;
        DeleteAsyncWaiterNode(old_node);
      } else {
        node = node->async_timeout_next_;
      }
    } else {
      // No more timed out FutexWaitListNodes on the list. Schedule a new
      // timeout task. Possible future optimization: depending on waiter
      // insertion order, there might already be a task scheduled. In that case,
      // don't schedule a new one.
      auto rel_timeout = node->timeout_time_ - current_time;
      auto task = std::make_unique<AsyncWaiterTimeoutTask>(
          node->isolate_for_async_waiters_);
      node->task_runner_->PostNonNestableDelayedTask(std::move(task),
                                                     rel_timeout.InSecondsF());
      break;
    }
  }
}

void FutexEmulation::Cleanup(Isolate* isolate) {
  HandleScope handle_scope_(isolate);
  base::MutexGuard lock_guard(mutex_.Pointer());

  isolates_resolve_task_scheduled_.Pointer()->erase(isolate);

  FutexWaitListNode* node = wait_list_.Pointer()->head_;
  while (node) {
    std::shared_ptr<BackingStore> node_backing_store =
        node->backing_store_.lock();
    if (node->isolate_for_async_waiters_ == isolate) {
      node = DeleteAsyncWaiterNode(node);
    } else {
      node = node->next_;
    }
  }
}

Object FutexEmulation::NumWaitersForTesting(Handle<JSArrayBuffer> array_buffer,
                                            size_t addr) {
  DCHECK_LT(addr, array_buffer->byte_length());
  std::shared_ptr<BackingStore> backing_store = array_buffer->GetBackingStore();

  base::MutexGuard lock_guard(mutex_.Pointer());

  int waiters = 0;
  FutexWaitListNode* node = wait_list_.Pointer()->head_;
  while (node) {
    std::shared_ptr<BackingStore> node_backing_store =
        node->backing_store_.lock();
    if (backing_store.get() == node_backing_store.get() &&
        addr == node->wait_addr_ && node->waiting_) {
      waiters++;
    }

    node = node->next_;
  }

  return Smi::FromInt(waiters);
}

Object FutexEmulation::NumWaitersForTesting() {
  base::MutexGuard lock_guard(mutex_.Pointer());

  int waiters = 0;
  FutexWaitListNode* node = wait_list_.Pointer()->head_;
  while (node) {
    if (node->waiting_) {
      waiters++;
    }
    node = node->next_;
  }

  return Smi::FromInt(waiters);
}

Object FutexEmulation::NumUnresolvedAsyncPromisesForTesting(
    Handle<JSArrayBuffer> array_buffer, size_t addr) {
  DCHECK_LT(addr, array_buffer->byte_length());
  std::shared_ptr<BackingStore> backing_store = array_buffer->GetBackingStore();

  base::MutexGuard lock_guard(mutex_.Pointer());

  int waiters = 0;
  FutexWaitListNode* node = wait_list_.Pointer()->head_;
  while (node) {
    std::shared_ptr<BackingStore> node_backing_store =
        node->backing_store_.lock();
    if (backing_store.get() == node_backing_store.get() &&
        addr == node->wait_addr_ && !node->waiting_) {
      waiters++;
    }

    node = node->next_;
  }

  return Smi::FromInt(waiters);
}

void FutexEmulation::VerifyNode(FutexWaitListNode* node) {
#ifdef DEBUG
  if (node->next_) {
    DCHECK_NE(node, wait_list_.Pointer()->tail_);
    DCHECK_EQ(node, node->next_->prev_);
  } else {
    DCHECK_EQ(node, wait_list_.Pointer()->tail_);
  }
  if (node->prev_) {
    DCHECK_NE(node, wait_list_.Pointer()->head_);
    DCHECK_EQ(node, node->prev_->next_);
  } else {
    DCHECK_EQ(node, wait_list_.Pointer()->head_);
  }

  if (node->timeout_time_ != base::TimeTicks()) {
    DCHECK(FLAG_harmony_atomics_waitasync);
    DCHECK_NOT_NULL(node->isolate_for_async_waiters_);
  }

  if (node->async_timeout_next_) {
    DCHECK(FLAG_harmony_atomics_waitasync);
    DCHECK_NOT_NULL(node->isolate_for_async_waiters_);
    DCHECK_EQ(node, node->async_timeout_next_->async_timeout_prev_);
  }

  if (node->async_timeout_prev_) {
    DCHECK(FLAG_harmony_atomics_waitasync);
    DCHECK_NOT_NULL(node->isolate_for_async_waiters_);
    DCHECK_NE(node, wait_list_.Pointer()->async_timeout_head_);
    DCHECK_EQ(node, node->async_timeout_prev_->async_timeout_next_);
  } else if (node->timeout_time_ != base::TimeTicks()) {
    DCHECK_EQ(node, wait_list_.Pointer()->async_timeout_head_);
  }
#endif  // DEBUG
}

void FutexEmulation::VerifyFutexWaitList() {
#ifdef DEBUG
  FutexWaitListNode* node = wait_list_.Pointer()->head_;
  while (node) {
    VerifyNode(node);
    node = node->next_;
  }
  node = wait_list_.Pointer()->async_timeout_head_;
  while (node) {
    VerifyNode(node);
    node = node->async_timeout_next_;
  }
#endif  // DEBUG
}

}  // namespace internal
}  // namespace v8
