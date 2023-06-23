// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_ASYNC_CONTEXT_H
#define INCLUDE_V8_ASYNC_CONTEXT_H

#include "v8-object.h"  // NOLINT(build/include_directory)
#include "v8config.h"   // NOLINT(build/include_directory)

namespace v8 {

class V8_EXPORT AsyncContext {
 public:
  AsyncContext() = delete;
  AsyncContext(const AsyncContext&) = delete;
  AsyncContext& operator=(const AsyncContext&) = delete;
  ~AsyncContext() = delete;

  class V8_EXPORT Variable : public Object {
   public:
    class V8_EXPORT V8_NODISCARD Scope {
     public:
      Scope(Local<Variable> async_local, Local<Value> value);

      ~Scope();

      // Prevent copying of Scope objects.
      Scope(const Scope&) = delete;
      Scope& operator=(const Scope&) = delete;

     private:
      internal::Isolate* isolate_ = nullptr;
      Local<Data> previous_snapshot_;
    };

    static Local<Variable> New(Isolate* isolate);

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

    V8_INLINE static Variable* Cast(Value* value) {
#ifdef V8_ENABLE_CHECKS
      CheckCast(value);
#endif
      return static_cast<Variable*>(value);
    }

   private:
    Variable();
    static void CheckCast(Value* obj);
  };

  class V8_EXPORT Snapshot : public Object {
   public:
    class V8_EXPORT V8_NODISCARD Scope {
     public:
      explicit Scope(Local<Snapshot> async_snapshot);

      ~Scope();

      // Prevent copying of Scope objects.
      Scope(const Scope&) = delete;
      Scope& operator=(const Scope&) = delete;

     private:
      internal::Isolate* isolate_ = nullptr;
      Local<Data> previous_snapshot_;
    };

    static Local<Snapshot> New(Isolate* isolate);

    V8_INLINE static Snapshot* Cast(Value* value) {
#ifdef V8_ENABLE_CHECKS
      CheckCast(value);
#endif
      return static_cast<Snapshot*>(value);
    }

   private:
    Snapshot();
    static void CheckCast(Value* obj);
  };
};

}  // namespace v8

#endif  // INCLUDE_V8_ASYNC_CONTEXT_H
