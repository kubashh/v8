// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ASYNC_HOOKS_WRAPPER_H_
#define V8_ASYNC_HOOKS_WRAPPER_H_

#include "include/v8.h"
#include "src/objects.h"

namespace v8 {

struct AsyncContext {
  int execution_async_id;
  int trigger_async_id;
};

class AsyncWrap {
 public:
  explicit AsyncWrap(Isolate* isolate) { isolate_ = isolate; }
  void Enable();
  void Disable();
  bool IsEnabled() const { return enabled; }

  inline v8::Local<v8::Function> init_function() const;
  inline void set_init_function(v8::Local<v8::Function> value);
  inline v8::Local<v8::Function> before_function() const;
  inline void set_before_function(v8::Local<v8::Function> value);
  inline v8::Local<v8::Function> after_function() const;
  inline void set_after_function(v8::Local<v8::Function> value);
  inline v8::Local<v8::Function> promiseResolve_function() const;
  inline void set_promiseResolve_function(v8::Local<v8::Function> value);

 private:
  Isolate* isolate_;

  Persistent<v8::Function> init_function_;
  Persistent<v8::Function> before_function_;
  Persistent<v8::Function> after_function_;
  Persistent<v8::Function> promiseResolve_function_;

  bool enabled;
};

class Isolate;

class AsyncHooks {
 public:
  explicit AsyncHooks(Isolate* isolate) {
    isolate_ = isolate;
    asyncContext.execution_async_id = 1;
    asyncContext.trigger_async_id = 0;
    Initialize();
  }
  ~AsyncHooks() { Deinitialize(); }

  int GetExecutionAsyncId() const;
  int GetTriggerAsyncId() const;

  Local<Object> CreateHook(const v8::FunctionCallbackInfo<v8::Value>& args);

 private:
  std::vector<AsyncWrap*> async_wraps_;
  Isolate* isolate_;
  Persistent<FunctionTemplate> async_hook_ctor;
  Persistent<ObjectTemplate> async_hooks_templ;
  Persistent<Private> async_id_smb;
  Persistent<Private> trigger_id_smb;

  void Initialize();
  void Deinitialize();

  static void ShellPromiseHook(PromiseHookType type, Local<Promise> promise,
                               Local<Value> parent);
  static void PromiseHookDispatch(PromiseHookType type, Local<Promise> promise,
                                  Local<Value> parent, AsyncWrap* wrap,
                                  AsyncHooks* hooks);

  AsyncContext asyncContext;
};

}  // namespace v8

#endif  // V8_ASYNC_HOOKS_WRAPPER_H_
