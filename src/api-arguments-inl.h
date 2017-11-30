// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_API_ARGUMENTS_INL_H_
#define V8_API_ARGUMENTS_INL_H_

#include "src/api-arguments.h"

#include "src/tracing/trace-event.h"
#include "src/vm-state-inl.h"

namespace v8 {
namespace internal {

#define SIDE_EFFECT_CHECK(ISOLATE, F, RETURN_TYPE)            \
  do {                                                        \
    if (ISOLATE->needs_side_effect_check() &&                 \
        !PerformSideEffectCheck(ISOLATE, FUNCTION_ADDR(F))) { \
      return Handle<RETURN_TYPE>();                           \
    }                                                         \
  } while (false)

#define FOR_EACH_CALLBACK_TABLE_MAPPING_1_NAME(F)   \
  F(NamedPropertyGetter, "get", v8::Value, Object)  \
  F(NamedPropertyQuery, "has", v8::Integer, Object) \
  F(NamedPropertyDeleter, "delete", v8::Boolean, Object)

#define WRITE_CALL_1_NAME(Function, type, ApiReturn, InternalReturn)         \
  Handle<InternalReturn> PropertyCallbackArguments::Call##Function(          \
      Object* callback, Handle<Name> name) {                                 \
    Isolate* isolate = this->isolate();                                      \
    RuntimeCallTimerScope timer(isolate,                                     \
                                &RuntimeCallStats::Function##Callback);      \
    DCHECK(!name->IsPrivate());                                              \
    Generic##Function##Callback f =                                          \
        ToCData<Generic##Function##Callback>(callback);                      \
    SIDE_EFFECT_CHECK(isolate, f, InternalReturn);                           \
    VMState<EXTERNAL> state(isolate);                                        \
    ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));             \
    PropertyCallbackInfo<ApiReturn> info(begin());                           \
    LOG(isolate,                                                             \
        ApiNamedPropertyAccess("interceptor-named-" type, holder(), *name)); \
    f(v8::Utils::ToLocal(name), info);                                       \
    return GetReturnValue<InternalReturn>(isolate);                          \
  }

FOR_EACH_CALLBACK_TABLE_MAPPING_1_NAME(WRITE_CALL_1_NAME)

#undef FOR_EACH_CALLBACK_TABLE_MAPPING_1_NAME
#undef WRITE_CALL_1_NAME

#define FOR_EACH_CALLBACK_TABLE_MAPPING_1_INDEX(F)    \
  F(IndexedPropertyGetter, "get", v8::Value, Object)  \
  F(IndexedPropertyQuery, "has", v8::Integer, Object) \
  F(IndexedPropertyDeleter, "delete", v8::Boolean, Object)

#define WRITE_CALL_1_INDEX(Function, type, ApiReturn, InternalReturn)   \
  Handle<InternalReturn> PropertyCallbackArguments::Call##Function(     \
      Object* callback, uint32_t index) {                               \
    Isolate* isolate = this->isolate();                                 \
    RuntimeCallTimerScope timer(isolate,                                \
                                &RuntimeCallStats::Function##Callback); \
    Function##Callback f = ToCData<Function##Callback>(callback);       \
    SIDE_EFFECT_CHECK(isolate, f, InternalReturn);                      \
    VMState<EXTERNAL> state(isolate);                                   \
    ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));        \
    PropertyCallbackInfo<ApiReturn> info(begin());                      \
    LOG(isolate, ApiIndexedPropertyAccess("interceptor-indexed-" type,  \
                                          holder(), index));            \
    f(index, info);                                                     \
    return GetReturnValue<InternalReturn>(isolate);                     \
  }

FOR_EACH_CALLBACK_TABLE_MAPPING_1_INDEX(WRITE_CALL_1_INDEX)

#undef FOR_EACH_CALLBACK_TABLE_MAPPING_1_INDEX
#undef WRITE_CALL_1_INDEX

Handle<Object> PropertyCallbackArguments::CallNamedPropertyDescriptor(
    Object* callback, Handle<Name> name) {
  return CallNamedPropertyGetter(callback, name);
}

Handle<Object> PropertyCallbackArguments::CallIndexedPropertyDescriptor(
    Object* callback, uint32_t index) {
  return CallIndexedPropertyGetter(callback, index);
}

Handle<Object> PropertyCallbackArguments::CallNamedPropertySetter(
    Object* callback, Handle<Name> name, Handle<Object> value) {
  GenericNamedPropertySetterCallback f =
      ToCData<GenericNamedPropertySetterCallback>(callback);
  return CallNamedPropertySetter(f, name, value);
}

Handle<Object> PropertyCallbackArguments::CallNamedPropertySetter(
    GenericNamedPropertySetterCallback f, Handle<Name> name,
    Handle<Object> value) {
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              &RuntimeCallStats::NamedPropertySetterCallback);
  DCHECK(!name->IsPrivate());
  SIDE_EFFECT_CHECK(isolate, f, Object);
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  PropertyCallbackInfo<v8::Value> info(begin());
  LOG(isolate,
      ApiNamedPropertyAccess("interceptor-named-set", holder(), *name));
  f(v8::Utils::ToLocal(name), v8::Utils::ToLocal(value), info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallNamedPropertyDefiner(
    Object* callback, Handle<Name> name, const v8::PropertyDescriptor& desc) {
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              &RuntimeCallStats::NamedPropertyDefinerCallback);
  DCHECK(!name->IsPrivate());
  GenericNamedPropertyDefinerCallback f =
      ToCData<GenericNamedPropertyDefinerCallback>(callback);
  SIDE_EFFECT_CHECK(isolate, f, Object);
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  PropertyCallbackInfo<v8::Value> info(begin());
  LOG(isolate,
      ApiNamedPropertyAccess("interceptor-named-define", holder(), *name));
  f(v8::Utils::ToLocal(name), desc, info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallIndexedPropertySetter(
    Object* callback, uint32_t index, Handle<Object> value) {
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              &RuntimeCallStats::IndexedPropertySetterCallback);
  IndexedPropertySetterCallback f =
      ToCData<IndexedPropertySetterCallback>(callback);
  SIDE_EFFECT_CHECK(isolate, f, Object);
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  PropertyCallbackInfo<v8::Value> info(begin());
  LOG(isolate,
      ApiIndexedPropertyAccess("interceptor-indexed-set", holder(), index));
  f(index, v8::Utils::ToLocal(value), info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallIndexedPropertyDefiner(
    Object* callback, uint32_t index, const v8::PropertyDescriptor& desc) {
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(
      isolate, &RuntimeCallStats::IndexedPropertyDefinerCallback);
  IndexedPropertyDefinerCallback f =
      ToCData<IndexedPropertyDefinerCallback>(callback);
  SIDE_EFFECT_CHECK(isolate, f, Object);
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  PropertyCallbackInfo<v8::Value> info(begin());
  LOG(isolate,
      ApiIndexedPropertyAccess("interceptor-indexed-define", holder(), index));
  f(index, desc, info);
  return GetReturnValue<Object>(isolate);
}

void PropertyCallbackArguments::CallAccessorNameSetter(Object* callback,
                                                       Handle<Name> name,
                                                       Handle<Object> value) {
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              &RuntimeCallStats::AccessorNameSetterCallback);
  AccessorNameSetterCallback f = ToCData<AccessorNameSetterCallback>(callback);
  if (isolate->needs_side_effect_check() &&
      !PerformSideEffectCheck(isolate, FUNCTION_ADDR(f))) {
    return;
  }
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  PropertyCallbackInfo<void> info(begin());
  LOG(isolate,
      ApiNamedPropertyAccess("interceptor-named-set", holder(), *name));
  f(v8::Utils::ToLocal(name), v8::Utils::ToLocal(value), info);
}

#undef SIDE_EFFECT_CHECK

}  // namespace internal
}  // namespace v8

#endif  // V8_API_ARGUMENTS_INL_H_
