// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/extensions/console-extension.h"

#include "include/v8-function.h"
#include "include/v8-isolate.h"
#include "include/v8-object.h"
#include "include/v8-persistent-handle.h"
#include "include/v8-primitive.h"
#include "include/v8-template.h"
#include "src/api/api-inl.h"
#include "src/debug/interface-types.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// Console

#define CONSOLE_METHOD_LIST(V)      \
  V(Debug, debug)                   \
  V(Error, error)                   \
  V(Info, info)                     \
  V(Log, log)                       \
  V(Warn, warn)                     \
  V(Dir, dir)                       \
  V(DirXml, dirXml)                 \
  V(Table, table)                   \
  V(Trace, trace)                   \
  V(Group, group)                   \
  V(GroupCollapsed, groupCollapsed) \
  V(GroupEnd, groupEnd)             \
  V(Clear, clear)                   \
  V(Count, count)                   \
  V(CountReset, countReset)         \
  V(Assert, assert)                 \
  V(Profile, profile)               \
  V(ProfileEnd, profileEnd)         \
  V(TimeLog, timeLog)

namespace {
void ConsoleCall(
    Isolate* isolate, const v8::FunctionCallbackInfo<v8::Value>& args,
    void (debug::ConsoleDelegate::*func)(const v8::debug::ConsoleCallArguments&,
                                         const v8::debug::ConsoleContext&)) {
  CHECK(!isolate->has_pending_exception());
  CHECK(!isolate->has_scheduled_exception());
  if (!isolate->console_delegate()) return;
  HandleScope scope(isolate);
  debug::ConsoleCallArguments wrapper(args);

  int context_id;
  Handle<String> context_name;
  if (args.Data()->IsObject()) {
    Handle<JSReceiver> target =
        Handle<JSReceiver>::cast(Utils::OpenHandle(*args.Data()));
    Handle<Object> context_id_obj = JSObject::GetDataProperty(
        target, isolate->factory()->console_context_id_symbol());
    context_id = context_id_obj->IsSmi()
                     ? Handle<Smi>::cast(context_id_obj)->value()
                     : 0;
    Handle<Object> context_name_obj = JSObject::GetDataProperty(
        target, isolate->factory()->console_context_name_symbol());
    context_name = context_name_obj->IsString()
                       ? Handle<String>::cast(context_name_obj)
                       : isolate->factory()->anonymous_string();
  } else {
    context_id = 0;
    context_name = isolate->factory()->anonymous_string();
  }
  (isolate->console_delegate()->*func)(
      wrapper,
      v8::debug::ConsoleContext(context_id, Utils::ToLocal(context_name)));
}

void LogTimerEvent(Isolate* isolate,
                   const v8::FunctionCallbackInfo<v8::Value>& args,
                   v8::LogEventStatus se) {
  if (!isolate->logger()->is_logging()) return;
  HandleScope scope(isolate);
  std::unique_ptr<char[]> name;
  const char* raw_name = "default";
  if (args.Length() > 1 && args[1]->IsString()) {
    // Try converting the first argument to a string.
    name = Utils::OpenHandle(*args[0].As<v8::String>())->ToCString();
    raw_name = name.get();
  }
  LOG(isolate, TimerEvent(se, raw_name));
}

#define CONSOLE_BUILTIN_IMPLEMENTATION(call, name)                             \
  static void Console##call(const v8::FunctionCallbackInfo<v8::Value>& args) { \
    Isolate* isolate = reinterpret_cast<Isolate*>(args.GetIsolate());          \
    ConsoleCall(isolate, args, &debug::ConsoleDelegate::call);                 \
  }
CONSOLE_METHOD_LIST(CONSOLE_BUILTIN_IMPLEMENTATION)
#undef CONSOLE_BUILTIN_IMPLEMENTATION

static void ConsoleTime(const v8::FunctionCallbackInfo<v8::Value>& args) {
  Isolate* isolate = reinterpret_cast<Isolate*>(args.GetIsolate());
  LogTimerEvent(isolate, args, v8::LogEventStatus::kStart);
  ConsoleCall(isolate, args, &debug::ConsoleDelegate::Time);
}

static void ConsoleTimeEnd(const v8::FunctionCallbackInfo<v8::Value>& args) {
  Isolate* isolate = reinterpret_cast<Isolate*>(args.GetIsolate());
  LogTimerEvent(isolate, args, v8::LogEventStatus::kEnd);
  ConsoleCall(isolate, args, &debug::ConsoleDelegate::TimeEnd);
}

static void ConsoleTimeStamp(const v8::FunctionCallbackInfo<v8::Value>& args) {
  Isolate* isolate = reinterpret_cast<Isolate*>(args.GetIsolate());
  LogTimerEvent(isolate, args, v8::LogEventStatus::kStamp);
  ConsoleCall(isolate, args, &debug::ConsoleDelegate::TimeStamp);
}

void InstallContextFunction(v8::Isolate* isolate, v8::Local<v8::Object> target,
                            const char* name, v8::FunctionCallback impl) {
  v8::Local<v8::Function> fun =
      v8::Function::New(isolate->GetCurrentContext(), impl, target, 1)
          .ToLocalChecked();
  v8::Local<v8::String> name_string =
      v8::String::NewFromOneByte(isolate,
                                 reinterpret_cast<const uint8_t*>(name),
                                 v8::NewStringType::kInternalized)
          .ToLocalChecked();
  fun->SetName(name_string);

  CHECK(
      target->Set(isolate->GetCurrentContext(), name_string, fun).ToChecked());
}

static void ConsoleContext(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  Isolate* i = reinterpret_cast<Isolate*>(isolate);
  Factory* const factory = i->factory();

  v8::HandleScope scope(isolate);
  v8::Local<v8::Object> context = v8::Object::New(isolate);

  int context_id = i->last_console_context_id() + 1;
  i->set_last_console_context_id(context_id);

  v8::Local<v8::Value> context_name = args[0];

  Handle<JSObject> data = Handle<JSObject>::cast(Utils::OpenHandle(*context));
  JSObject::AddProperty(i, data, factory->console_context_id_symbol(),
                        handle(Smi::FromInt(context_id), i), NONE);
  if (context_name->IsString()) {
    JSObject::AddProperty(i, data, factory->console_context_name_symbol(),
                          Utils::OpenHandle(*context_name), NONE);
  }

#define CONSOLE_BUILTIN_SETUP(call, name) \
  InstallContextFunction(isolate, context, #name, Console##call);
  CONSOLE_METHOD_LIST(CONSOLE_BUILTIN_SETUP)
#undef CONSOLE_BUILTIN_SETUP
  InstallContextFunction(isolate, context, "time", ConsoleTime);
  InstallContextFunction(isolate, context, "timeEnd", ConsoleTimeEnd);
  InstallContextFunction(isolate, context, "timeStamp", ConsoleTimeStamp);

  args.GetReturnValue().Set(context);
}

}  // namespace

#define CONSOLE_NATIVE_FUN_SETUP(call, name) "native function " #name "();"
#define CONSOLE_NATIVE_FUN_INSTALL(call, name) #name ","
const char* const ConsoleExtension::kSource =
    "(function() {"
    CONSOLE_METHOD_LIST(CONSOLE_NATIVE_FUN_SETUP)
    "native function context();"
    "native function time();"
    "native function timeEnd();"
    "native function timeStamp();"
    "Object.defineProperty(globalThis, 'console', {"
      "enumerable: false, configurable: true, value: {"
        CONSOLE_METHOD_LIST(CONSOLE_NATIVE_FUN_INSTALL)
        "context,"
        "time,"
        "timeEnd,"
        "timeStamp,"
    "}});"
    "})();";
#undef CONSOLE_NATIVE_FUN_SETUP
#undef CONSOLE_NATIVE_FUN_INSTALL

v8::Local<v8::FunctionTemplate> ConsoleExtension::GetNativeFunctionTemplate(
    v8::Isolate* isolate, v8::Local<v8::String> str) {
  v8::String::Utf8Value str_utf8(isolate, str);
  const char* method_name = *str_utf8;

#define CONSOLE_BUILTIN_SETUP(call, name)                     \
  if (strcmp(method_name, #name) == 0) {                      \
    return v8::FunctionTemplate::New(isolate, Console##call); \
  }
  CONSOLE_METHOD_LIST(CONSOLE_BUILTIN_SETUP)
#undef CONSOLE_BUILTIN_SETUP

  if (strcmp(method_name, "context") == 0) {
    return v8::FunctionTemplate::New(isolate, ConsoleContext);
  }
  if (strcmp(method_name, "time") == 0) {
    return v8::FunctionTemplate::New(isolate, ConsoleTime);
  }
  if (strcmp(method_name, "timeEnd") == 0) {
    return v8::FunctionTemplate::New(isolate, ConsoleTime);
  }
  if (strcmp(method_name, "timeStamp") == 0) {
    return v8::FunctionTemplate::New(isolate, ConsoleTimeStamp);
  }
  CHECK(false);
}

#undef CONSOLE_METHOD_LIST

}  // namespace internal
}  // namespace v8
