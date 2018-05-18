// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/elements.h"
#include "src/heap/factory.h"
#include "src/messages.h"
#include "src/objects-inl.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {

#define CHECK_RECEIVER(method)                                              \
  Handle<Object> receiver = args.at<Object>(0);                             \
  if (!receiver->IsJSDataView()) {                                          \
    THROW_NEW_ERROR_RETURN_FAILURE(                                         \
        isolate,                                                            \
        NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,          \
                     isolate->factory()->NewStringFromAsciiChecked(method), \
                     receiver));                                            \
  }                                                                         \
  Handle<JSDataView> data_view = Handle<JSDataView>::cast(receiver);

template <typename T>
MaybeHandle<Object> GetViewValue(Isolate* isolate, Handle<JSDataView> data_view,
                                 Handle<Object> request_index,
                                 bool is_little_endian, const char* method);

#define DATA_VIEW_PROTOTYPE_GET(Type, type)                  \
  RUNTIME_FUNCTION(Runtime_DataViewGet##Type) {              \
    HandleScope scope(isolate);                              \
    CHECK_RECEIVER("DataView.prototype.get" #Type);          \
    Handle<Object> byte_offset = args.at<Object>(1);         \
    Handle<Object> is_little_endian = args.at<Object>(2);    \
    Handle<Object> result;                                   \
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(                      \
        isolate, result,                                     \
        GetViewValue<type>(isolate, data_view, byte_offset,  \
                           is_little_endian->BooleanValue(), \
                           "DataView.prototype.get" #Type)); \
    return *result;                                          \
  }

DATA_VIEW_PROTOTYPE_GET(Int8, int8_t)
DATA_VIEW_PROTOTYPE_GET(Uint8, uint8_t)
DATA_VIEW_PROTOTYPE_GET(Int16, int16_t)
DATA_VIEW_PROTOTYPE_GET(Uint16, uint16_t)
DATA_VIEW_PROTOTYPE_GET(Int32, int32_t)
DATA_VIEW_PROTOTYPE_GET(Uint32, uint32_t)
DATA_VIEW_PROTOTYPE_GET(Float32, float)
DATA_VIEW_PROTOTYPE_GET(Float64, double)
DATA_VIEW_PROTOTYPE_GET(BigInt64, int64_t)
DATA_VIEW_PROTOTYPE_GET(BigUint64, uint64_t)
#undef DATA_VIEW_PROTOTYPE_GET

template <typename T>
MaybeHandle<Object> SetViewValue(Isolate* isolate, Handle<JSDataView> data_view,
                                 Handle<Object> request_index,
                                 bool is_little_endian, Handle<Object> value,
                                 const char* method);

#define DATA_VIEW_PROTOTYPE_SET(Type, type)                         \
  RUNTIME_FUNCTION(Runtime_DataViewSet##Type) {                     \
    HandleScope scope(isolate);                                     \
    CHECK_RECEIVER("DataView.prototype.set" #Type);                 \
    Handle<Object> byte_offset = args.at<Object>(1);                \
    Handle<Object> value = args.at<Object>(2);                      \
    Handle<Object> is_little_endian = args.at<Object>(3);           \
    Handle<Object> result;                                          \
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(                             \
        isolate, result,                                            \
        SetViewValue<type>(isolate, data_view, byte_offset,         \
                           is_little_endian->BooleanValue(), value, \
                           "DataView.prototype.set" #Type));        \
    return *result;                                                 \
  }

DATA_VIEW_PROTOTYPE_SET(Int8, int8_t)
DATA_VIEW_PROTOTYPE_SET(Uint8, uint8_t)
DATA_VIEW_PROTOTYPE_SET(Int16, int16_t)
DATA_VIEW_PROTOTYPE_SET(Uint16, uint16_t)
DATA_VIEW_PROTOTYPE_SET(Int32, int32_t)
DATA_VIEW_PROTOTYPE_SET(Uint32, uint32_t)
DATA_VIEW_PROTOTYPE_SET(Float32, float)
DATA_VIEW_PROTOTYPE_SET(Float64, double)
DATA_VIEW_PROTOTYPE_SET(BigInt64, int64_t)
DATA_VIEW_PROTOTYPE_SET(BigUint64, uint64_t)
#undef DATA_VIEW_PROTOTYPE_SET

#undef CHECK_RECEIVER
}  // namespace internal
}  // namespace v8
