// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_ASYNC_CONTEXT_H
#define INCLUDE_V8_ASYNC_CONTEXT_H

#include "v8-object.h"  // NOLINT(build/include_directory)
#include "v8config.h"   // NOLINT(build/include_directory)

namespace v8 {

class V8_EXPORT AsyncLocal : public Object {
 public:
  class V8_EXPORT V8_NODISCARD Scope {
   public:
    Scope(Local<AsyncLocal> async_local, Local<Value> value);

    ~Scope();

    // Prevent copying of Scope objects.
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

   private:
    internal::Isolate* isolate_ = nullptr;
    Local<Data> previous_snapshot_;
  };

  static Local<AsyncLocal> New(Isolate* isolate);

  Local<String> Name();

  Local<Value> DefaultValue();

  MaybeLocal<Value> GetValue();

  V8_INLINE Local<Value> GetValueOrDefault() {
    MaybeLocal<Value> value = GetValue();
    if (value.IsEmpty()) {
      return DefaultValue();
    } else {
      return value.ToLocalChecked();
    }
  }

  V8_INLINE static AsyncLocal* Cast(Value* value) {
#ifdef V8_ENABLE_CHECKS
    CheckCast(value);
#endif
    return static_cast<AsyncLocal*>(value);
  }

 private:
  AsyncLocal();
  static void CheckCast(Value* obj);
};

class V8_EXPORT AsyncSnapshot : public Object {
 public:
  class V8_EXPORT V8_NODISCARD Scope {
   public:
    explicit Scope(Local<AsyncSnapshot> async_snapshot);

    ~Scope();

    // Prevent copying of Scope objects.
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

   private:
    internal::Isolate* isolate_ = nullptr;
    Local<Data> previous_snapshot_;
  };

  static Local<AsyncSnapshot> New(Isolate* isolate);

  V8_INLINE static AsyncSnapshot* Cast(Value* value) {
#ifdef V8_ENABLE_CHECKS
    CheckCast(value);
#endif
    return static_cast<AsyncSnapshot*>(value);
  }

 private:
  AsyncSnapshot();
  static void CheckCast(Value* obj);
};

}  // namespace v8

#endif  // INCLUDE_V8_ASYNC_CONTEXT_H
