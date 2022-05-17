// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/testing.h"

#include "src/api/api-inl.h"
#include "src/api/api-natives.h"
#include "src/common/globals.h"
#include "src/execution/isolate-inl.h"
#include "src/heap/factory.h"
#include "src/objects/backing-store.h"
#include "src/objects/js-objects.h"
#include "src/objects/templates.h"

namespace v8 {
namespace internal {

#ifdef V8_EXPOSE_MEMORY_CORRUPTION_API

namespace {

// Memory.getAddressOf(object) -> Number
void MemoryGetAddressOf(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();

  if (args.Length() == 0) {
    isolate->ThrowError("First argument must be provided");
    return;
  }

  Handle<Object> arg = Utils::OpenHandle(*args[0]);
  if (!arg->IsHeapObject()) {
    isolate->ThrowError("First argument must be a HeapObject");
    return;
  }

  // HeapObjects must be allocated inside the pointer compression cage so their
  // address relative to the start of the sandbox can be obtained simply by
  // taking the lowest 32 bits of the absolute address.
  uint32_t address = static_cast<uint32_t>(HeapObject::cast(*arg).address());
  args.GetReturnValue().Set(v8::Integer::NewFromUnsigned(isolate, address));
}

// Memory.getSizeOf(object) -> Number
void MemoryGetSizeOf(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();

  if (args.Length() == 0) {
    isolate->ThrowError("First argument must be provided");
    return;
  }

  Handle<Object> arg = Utils::OpenHandle(*args[0]);
  if (!arg->IsHeapObject()) {
    isolate->ThrowError("First argument must be a HeapObject");
    return;
  }

  int size = HeapObject::cast(*arg).Size();
  args.GetReturnValue().Set(v8::Integer::New(isolate, size));
}

Handle<FunctionTemplateInfo> NewFunctionTemplate(Isolate* isolate,
                                                 FunctionCallback func) {
  // Use the API functions here as they are more convenient to use.
  v8::Isolate* api_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  Local<FunctionTemplate> function_template = FunctionTemplate::New(
      api_isolate, func, {}, {}, 0, ConstructorBehavior::kThrow,
      SideEffectType::kHasSideEffect);
  return v8::Utils::OpenHandle(*function_template);
}

void InstallFunction(Isolate* isolate, Handle<JSObject> holder,
                     FunctionCallback func, const char* name,
                     int num_parameters) {
  Factory* factory = isolate->factory();
  Handle<String> function_name = factory->NewStringFromAsciiChecked(name);
  Handle<FunctionTemplateInfo> function_template =
      NewFunctionTemplate(isolate, func);
  Handle<JSFunction> function =
      ApiNatives::InstantiateFunction(function_template, function_name)
          .ToHandleChecked();
  function->shared().set_length(1);
  JSObject::AddProperty(isolate, holder, function_name, function, NONE);
}

}  // namespace

// static
void MemoryCorruptionApi::Install(Isolate* isolate) {
  Handle<JSGlobalObject> global = isolate->global_object();
  Factory* factory = isolate->factory();

  // Setup the special Memory object that provides read/write access to the
  // entire sandbox address space.
  Handle<String> name = factory->NewStringFromAsciiChecked("Memory");
  auto sandbox = GetProcessWideSandbox();
  CHECK_LE(sandbox->size(), kMaxSafeIntegerUint64);
  std::unique_ptr<BackingStore> memory = BackingStore::WrapAllocation(
      isolate, reinterpret_cast<void*>(sandbox->base()), sandbox->size(),
      SharedFlag::kNotShared, /* free_on_destruct */ false);
  CHECK(memory);
  Handle<JSArrayBuffer> memory_buffer =
      factory->NewJSArrayBuffer(std::move(memory));
  Handle<JSDataView> memory_view =
      factory->NewJSDataView(memory_buffer, 0, sandbox->size());

  // Install the getAddressOf and getSizeOf methods on the Memory object.
  InstallFunction(isolate, memory_view, MemoryGetAddressOf, "getAddressOf", 1);
  InstallFunction(isolate, memory_view, MemoryGetSizeOf, "getSizeOf", 1);

  // Install the Memory object as property on the global object.
  JSObject::AddProperty(isolate, global, name, memory_view, DONT_ENUM);
}

#endif  // V8_EXPOSE_MEMORY_CORRUPTION_API

}  // namespace internal
}  // namespace v8
