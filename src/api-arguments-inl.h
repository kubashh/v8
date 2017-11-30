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

#define FOR_EACH_CALLBACK_TABLE_MAPPING_1_NAME(F) \
  F(Query, "query", v8::Integer, Object)          \
  F(Deleter, "deleter", v8::Boolean, Object)

#define WRITE_CALL_1_NAME(Function, type, ApiReturn, InternalReturn)           \
  Handle<InternalReturn> PropertyCallbackArguments::CallNamed##Function(       \
      Handle<InterceptorInfo> interceptor, Handle<Name> name) {                \
    DCHECK(interceptor->is_named());                                           \
    DCHECK(!name->IsPrivate());                                                \
    DCHECK_IMPLIES(name->IsSymbol(), interceptor->can_intercept_symbols());    \
    Isolate* isolate = this->isolate();                                        \
    RuntimeCallTimerScope timer(isolate,                                       \
                                &RuntimeCallStats::Named##Function##Callback); \
    DCHECK(!name->IsPrivate());                                                \
    GenericNamedProperty##Function##Callback f =                               \
        ToCData<GenericNamedProperty##Function##Callback>(                     \
            interceptor->type());                                              \
    SIDE_EFFECT_CHECK(isolate, f, InternalReturn);                             \
    VMState<EXTERNAL> state(isolate);                                          \
    ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));               \
    PropertyCallbackInfo<ApiReturn> info(begin());                             \
    LOG(isolate,                                                               \
        ApiNamedPropertyAccess("interceptor-named-" type, holder(), *name));   \
    f(v8::Utils::ToLocal(name), info);                                         \
    return GetReturnValue<InternalReturn>(isolate);                            \
  }

FOR_EACH_CALLBACK_TABLE_MAPPING_1_NAME(WRITE_CALL_1_NAME)

#undef FOR_EACH_CALLBACK_TABLE_MAPPING_1_NAME
#undef WRITE_CALL_1_NAME

#define FOR_EACH_CALLBACK_TABLE_MAPPING_1_INDEX(F) \
  F(Query, "query", v8::Integer, Object)           \
  F(Deleter, "deleter", v8::Boolean, Object)

#define WRITE_CALL_1_INDEX(Function, type, ApiReturn, InternalReturn)      \
  Handle<InternalReturn> PropertyCallbackArguments::CallIndexed##Function( \
      Handle<InterceptorInfo> interceptor, uint32_t index) {               \
    DCHECK(!interceptor->is_named());                                      \
    Isolate* isolate = this->isolate();                                    \
    RuntimeCallTimerScope timer(                                           \
        isolate, &RuntimeCallStats::Indexed##Function##Callback);          \
    IndexedProperty##Function##Callback f =                                \
        ToCData<IndexedProperty##Function##Callback>(interceptor->type()); \
    SIDE_EFFECT_CHECK(isolate, f, InternalReturn);                         \
    VMState<EXTERNAL> state(isolate);                                      \
    ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));           \
    PropertyCallbackInfo<ApiReturn> info(begin());                         \
    LOG(isolate, ApiIndexedPropertyAccess("interceptor-indexed-" type,     \
                                          holder(), index));               \
    f(index, info);                                                        \
    return GetReturnValue<InternalReturn>(isolate);                        \
  }

FOR_EACH_CALLBACK_TABLE_MAPPING_1_INDEX(WRITE_CALL_1_INDEX)

#undef FOR_EACH_CALLBACK_TABLE_MAPPING_1_INDEX
#undef WRITE_CALL_1_INDEX

Handle<Object> PropertyCallbackArguments::CallNamedGetter(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK(interceptor->is_named());
  DCHECK_IMPLIES(name->IsSymbol(), interceptor->can_intercept_symbols());
  DCHECK(!name->IsPrivate());
  GenericNamedPropertyGetterCallback f =
      ToCData<GenericNamedPropertyGetterCallback>(interceptor->getter());
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate, &RuntimeCallStats::NamedGetterCallback);
  LOG(isolate,
      ApiNamedPropertyAccess("interceptor-named-getter", holder(), *name));
  return CallNamedGetterCallback(f, name);
}

Handle<Object> PropertyCallbackArguments::CallNamedDescriptor(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK(interceptor->is_named());
  DCHECK_IMPLIES(name->IsSymbol(), interceptor->can_intercept_symbols());
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              &RuntimeCallStats::NamedDescriptorCallback);
  LOG(isolate,
      ApiNamedPropertyAccess("interceptor-named-descriptor", holder(), *name));
  GenericNamedPropertyDescriptorCallback f =
      ToCData<GenericNamedPropertyDescriptorCallback>(
          interceptor->descriptor());
  return CallNamedGetterCallback(f, name);
}

Handle<Object> PropertyCallbackArguments::CallNamedGetterCallback(
    GenericNamedPropertyGetterCallback f, Handle<Name> name) {
  DCHECK(!name->IsPrivate());
  Isolate* isolate = this->isolate();
  SIDE_EFFECT_CHECK(isolate, f, Object);
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  PropertyCallbackInfo<v8::Value> info(begin());
  f(v8::Utils::ToLocal(name), info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallNamedSetter(
    Handle<InterceptorInfo> interceptor, Handle<Name> name,
    Handle<Object> value) {
  DCHECK_IMPLIES(name->IsSymbol(), interceptor->can_intercept_symbols());
  GenericNamedPropertySetterCallback f =
      ToCData<GenericNamedPropertySetterCallback>(interceptor->setter());
  return CallNamedSetterCallback(f, name, value);
}

Handle<Object> PropertyCallbackArguments::CallNamedSetterCallback(
    GenericNamedPropertySetterCallback f, Handle<Name> name,
    Handle<Object> value) {
  DCHECK(!name->IsPrivate());
  Isolate* isolate = this->isolate();
  SIDE_EFFECT_CHECK(isolate, f, Object);
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  PropertyCallbackInfo<v8::Value> info(begin());
  f(v8::Utils::ToLocal(name), v8::Utils::ToLocal(value), info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallNamedDefiner(
    Handle<InterceptorInfo> interceptor, Handle<Name> name,
    const v8::PropertyDescriptor& desc) {
  DCHECK(interceptor->is_named());
  DCHECK(!name->IsPrivate());
  DCHECK_IMPLIES(name->IsSymbol(), interceptor->can_intercept_symbols());
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate, &RuntimeCallStats::NamedDefinerCallback);
  GenericNamedPropertyDefinerCallback f =
      ToCData<GenericNamedPropertyDefinerCallback>(interceptor->definer());
  SIDE_EFFECT_CHECK(isolate, f, Object);
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  PropertyCallbackInfo<v8::Value> info(begin());
  LOG(isolate,
      ApiNamedPropertyAccess("interceptor-named-define", holder(), *name));
  f(v8::Utils::ToLocal(name), desc, info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallIndexedSetter(
    Handle<InterceptorInfo> interceptor, uint32_t index, Handle<Object> value) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              &RuntimeCallStats::IndexedSetterCallback);
  IndexedPropertySetterCallback f =
      ToCData<IndexedPropertySetterCallback>(interceptor->setter());
  SIDE_EFFECT_CHECK(isolate, f, Object);
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  PropertyCallbackInfo<v8::Value> info(begin());
  LOG(isolate,
      ApiIndexedPropertyAccess("interceptor-indexed-set", holder(), index));
  f(index, v8::Utils::ToLocal(value), info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallIndexedDefiner(
    Handle<InterceptorInfo> interceptor, uint32_t index,
    const v8::PropertyDescriptor& desc) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              &RuntimeCallStats::IndexedDefinerCallback);
  IndexedPropertyDefinerCallback f =
      ToCData<IndexedPropertyDefinerCallback>(interceptor->definer());
  SIDE_EFFECT_CHECK(isolate, f, Object);
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  PropertyCallbackInfo<v8::Value> info(begin());
  LOG(isolate,
      ApiIndexedPropertyAccess("interceptor-indexed-define", holder(), index));
  f(index, desc, info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallIndexedGetter(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate, &RuntimeCallStats::NamedGetterCallback);
  LOG(isolate,
      ApiIndexedPropertyAccess("interceptor-indexed-getter", holder(), index));
  IndexedPropertyGetterCallback f =
      ToCData<IndexedPropertyGetterCallback>(interceptor->getter());
  return CallIndexedGetterCallback(f, index);
}

Handle<Object> PropertyCallbackArguments::CallIndexedDescriptor(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              &RuntimeCallStats::IndexedDescriptorCallback);
  LOG(isolate, ApiIndexedPropertyAccess("interceptor-indexed-descriptor",
                                        holder(), index));
  IndexedPropertyDescriptorCallback f =
      ToCData<IndexedPropertyDescriptorCallback>(interceptor->descriptor());
  return CallIndexedGetterCallback(f, index);
}

Handle<Object> PropertyCallbackArguments::CallIndexedGetterCallback(
    IndexedPropertyGetterCallback f, uint32_t index) {
  Isolate* isolate = this->isolate();
  SIDE_EFFECT_CHECK(isolate, f, Object);
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  PropertyCallbackInfo<v8::Value> info(begin());
  f(index, info);
  return GetReturnValue<Object>(isolate);
}

// -------------------------------------------------------------------------
// Accessors

Handle<Object> PropertyCallbackArguments::CallAccessorGetter(
    Handle<AccessorInfo> info, Handle<Name> name) {
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              &RuntimeCallStats::AccessorGetterCallback);
  LOG(isolate, ApiNamedPropertyAccess("accessor-getter", holder(), *name));
  AccessorNameGetterCallback f =
      ToCData<AccessorNameGetterCallback>(info->getter());
  return CallNamedGetterCallback(f, name);
}

void PropertyCallbackArguments::CallAccessorSetter(Handle<AccessorInfo> info,
                                                   Handle<Name> name,
                                                   Handle<Object> value) {
  Isolate* isolate = this->isolate();
  RuntimeCallTimerScope timer(isolate,
                              &RuntimeCallStats::AccessorSetterCallback);
  AccessorNameSetterCallback f =
      ToCData<AccessorNameSetterCallback>(info->setter());
  if (isolate->needs_side_effect_check() &&
      !PerformSideEffectCheck(isolate, FUNCTION_ADDR(f))) {
    return;
  }
  VMState<EXTERNAL> state(isolate);
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  PropertyCallbackInfo<void> callback_info(begin());
  LOG(isolate, ApiNamedPropertyAccess("accessor-setter", holder(), *name));
  f(v8::Utils::ToLocal(name), v8::Utils::ToLocal(value), callback_info);
}

#undef SIDE_EFFECT_CHECK

}  // namespace internal
}  // namespace v8

#endif  // V8_API_ARGUMENTS_INL_H_
