// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/async-hooks-wrapper.h"
#include "src/d8.h"

namespace v8 {

void AsyncWrap::Enable() { enabled = true; }

void AsyncWrap::Disable() { enabled = false; }

v8::Local<v8::Function> AsyncWrap::init_function() const {
  return init_function_.Get(isolate_);
}
void AsyncWrap::set_init_function(v8::Local<v8::Function> value) {
  init_function_.Reset(isolate_, value);
}
v8::Local<v8::Function> AsyncWrap::before_function() const {
  return before_function_.Get(isolate_);
}
void AsyncWrap::set_before_function(v8::Local<v8::Function> value) {
  before_function_.Reset(isolate_, value);
}
v8::Local<v8::Function> AsyncWrap::after_function() const {
  return after_function_.Get(isolate_);
}
void AsyncWrap::set_after_function(v8::Local<v8::Function> value) {
  after_function_.Reset(isolate_, value);
}
v8::Local<v8::Function> AsyncWrap::promiseResolve_function() const {
  return promiseResolve_function_.Get(isolate_);
}
void AsyncWrap::set_promiseResolve_function(v8::Local<v8::Function> value) {
  promiseResolve_function_.Reset(isolate_, value);
}

AsyncWrap* UnwrapHook(const v8::FunctionCallbackInfo<v8::Value>& args) {
  Isolate* isolate = args.GetIsolate();
  HandleScope scope(isolate);
  Local<Object> hook = args.This();
  Local<External> wrap = Local<External>::Cast(hook->GetInternalField(0));
  void* ptr = wrap->Value();
  return static_cast<AsyncWrap*>(ptr);
}

static void EnableHook(const v8::FunctionCallbackInfo<v8::Value>& args) {
  AsyncWrap* wrap = UnwrapHook(args);
  wrap->Enable();
}

static void DisableHook(const v8::FunctionCallbackInfo<v8::Value>& args) {
  AsyncWrap* wrap = UnwrapHook(args);
  wrap->Disable();
}

int AsyncHooks::GetExecutionAsyncId() const {
  return asyncContext.execution_async_id;
}

int AsyncHooks::GetTriggerAsyncId() const {
  return asyncContext.trigger_async_id;
}

Local<Object> AsyncHooks::CreateHook(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  Isolate* isolate = args.GetIsolate();
  EscapableHandleScope handle_scope(isolate);

  Local<Context> currentContext = isolate->GetCurrentContext();

  AsyncWrap* wrap = new AsyncWrap(isolate);

  CHECK(args[0]->IsObject());

  Local<Object> fn_obj = args[0].As<Object>();

#define SET_HOOK_FN(name)                                                   \
  Local<Value> name##_v =                                                   \
      fn_obj                                                                \
          ->Get(currentContext,                                             \
                String::NewFromUtf8(isolate, #name, NewStringType::kNormal) \
                    .ToLocalChecked())                                      \
          .ToLocalChecked();                                                \
  if (name##_v->IsFunction()) {                                             \
    wrap->set_##name##_function(name##_v.As<Function>());                   \
  }

  SET_HOOK_FN(init);
  SET_HOOK_FN(before);
  SET_HOOK_FN(after);
  SET_HOOK_FN(promiseResolve);
#undef SET_HOOK_FN

  async_wraps_.push_back(wrap);

  Local<Object> obj = async_hooks_templ.Get(isolate)
                          ->NewInstance(currentContext)
                          .ToLocalChecked();
  obj->SetInternalField(0, External::New(isolate, wrap));

  return handle_scope.Escape(obj);
}

void AsyncHooks::ShellPromiseHook(PromiseHookType type, Local<Promise> promise,
                                  Local<Value> parent) {
  AsyncHooks* hooks = Shell::GetAsyncHooks();

  HandleScope handle_scope(hooks->isolate_);

  Local<Context> currentContext = hooks->isolate_->GetCurrentContext();

  if (type == PromiseHookType::kInit) {
    ++hooks->asyncContext.execution_async_id;
    Local<Integer> async_id =
        Integer::New(hooks->isolate_, hooks->asyncContext.execution_async_id);

    promise->SetPrivate(currentContext,
                        hooks->async_id_smb.Get(hooks->isolate_), async_id);
    if (!parent.IsEmpty() && parent->IsPromise()) {
      Local<Promise> parent_promise = parent.As<Promise>();
      Local<Value> parent_async_id =
          parent_promise
              ->GetPrivate(hooks->isolate_->GetCurrentContext(),
                           hooks->async_id_smb.Get(hooks->isolate_))
              .ToLocalChecked();
      promise->SetPrivate(currentContext,
                          hooks->trigger_id_smb.Get(hooks->isolate_),
                          parent_async_id);
    } else {
      Local<Integer> trigger_id = Integer::New(hooks->isolate_, 0);
      promise->SetPrivate(currentContext,
                          hooks->trigger_id_smb.Get(hooks->isolate_),
                          trigger_id);
    }
  }

  for (AsyncWrap* wrap : hooks->async_wraps_) {
    PromiseHookDispatch(type, promise, parent, wrap, hooks);
  }
}

void AsyncHooks::Initialize() {
  HandleScope handle_scope(isolate_);

  async_hook_ctor.Reset(isolate_, FunctionTemplate::New(isolate_));
  async_hook_ctor.Get(isolate_)->SetClassName(
      String::NewFromUtf8(isolate_, "AsyncHook", NewStringType::kNormal)
          .ToLocalChecked());

  async_hooks_templ.Reset(isolate_,
                          async_hook_ctor.Get(isolate_)->InstanceTemplate());
  async_hooks_templ.Get(isolate_)->SetInternalFieldCount(1);
  async_hooks_templ.Get(isolate_)->Set(
      String::NewFromUtf8(isolate_, "enable"),
      FunctionTemplate::New(isolate_, EnableHook));
  async_hooks_templ.Get(isolate_)->Set(
      String::NewFromUtf8(isolate_, "disable"),
      FunctionTemplate::New(isolate_, DisableHook));

  async_id_smb.Reset(isolate_, Private::New(isolate_));
  trigger_id_smb.Reset(isolate_, Private::New(isolate_));

  isolate_->SetPromiseHook(ShellPromiseHook);
}

void AsyncHooks::Deinitialize() {
  isolate_->SetPromiseHook(nullptr);
  for (AsyncWrap* wrap : async_wraps_) {
    delete wrap;
  }
}

void AsyncHooks::PromiseHookDispatch(PromiseHookType type,
                                     Local<Promise> promise,
                                     Local<Value> parent, AsyncWrap* wrap,
                                     AsyncHooks* hooks) {
  if (!wrap->IsEnabled()) {
    return;
  }

  HandleScope handle_scope(hooks->isolate_);

  Local<Value> rcv = Undefined(hooks->isolate_);
  Local<Value> async_id =
      promise
          ->GetPrivate(hooks->isolate_->GetCurrentContext(),
                       hooks->async_id_smb.Get(hooks->isolate_))
          .ToLocalChecked();
  Local<Value> args[1] = {async_id};

  // Sacrifice the brevity for readability and debugfulness
  if (type == PromiseHookType::kInit) {
    if (!wrap->init_function().IsEmpty()) {
      Local<Value> initArgs[4] = {
          async_id,
          String::NewFromUtf8(hooks->isolate_, "PROMISE",
                              NewStringType::kNormal)
              .ToLocalChecked(),
          promise
              ->GetPrivate(hooks->isolate_->GetCurrentContext(),
                           hooks->trigger_id_smb.Get(hooks->isolate_))
              .ToLocalChecked(),
          promise};
      wrap->init_function()->Call(rcv, 4, initArgs);
    }
  } else if (type == PromiseHookType::kBefore) {
    if (!wrap->before_function().IsEmpty()) {
      wrap->before_function()->Call(rcv, 1, args);
    }
  } else if (type == PromiseHookType::kAfter) {
    if (!wrap->after_function().IsEmpty()) {
      wrap->after_function()->Call(rcv, 1, args);
    }
  } else if (type == PromiseHookType::kResolve) {
    if (!wrap->promiseResolve_function().IsEmpty()) {
      wrap->promiseResolve_function()->Call(rcv, 1, args);
    }
  }
}

}  // namespace v8
