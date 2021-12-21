// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api-inl.h"
#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/debug/interface-types.h"
#include "src/execution/execution.h"
#include "src/handles/maybe-handles.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// Console

#define CONSOLE_METHOD_LIST(V)         \
  V(Debug, debug, 1)                   \
  V(Error, error, 1)                   \
  V(Info, info, 1)                     \
  V(Log, log, 1)                       \
  V(Warn, warn, 1)                     \
  V(Dir, dir, 0)                       \
  V(DirXml, dirXml, 0)                 \
  V(Table, table, 0)                   \
  V(Trace, trace, 1)                   \
  V(Group, group, 0)                   \
  V(GroupCollapsed, groupCollapsed, 0) \
  V(GroupEnd, groupEnd, 0)             \
  V(Clear, clear, 0)                   \
  V(Count, count, 0)                   \
  V(CountReset, countReset, 0)         \
  V(Assert, assert, 2)                 \
  V(Profile, profile, 0)               \
  V(ProfileEnd, profileEnd, 0)         \
  V(TimeLog, timeLog, 0)

namespace {

// 2.2 Formatter(args) [https://console.spec.whatwg.org/#formatter]
//
// This implements the formatter operation defined in the Console
// specification to the degree that it makes sense for V8.  That
// means we primarily deal with %s, %i, %f, and %d, and any side
// effects caused by the type conversions, and we preserve the %o,
// %c, and %O specifiers and their parameters unchanged, and instead
// leave it to the debugger front-end to make sense of those.
//
// This implementation updates the |args| in-place and returns an
// appropriate view onto the |args| as |ConsoleCallArguments|.
//
// The |target_index| describes the position of the target string,
// which is different for example in case of `console.log` where
// it is 1 compared to `console.assert` where it is 2. If you pass
// 0 for |target_index|, this function is a no-op.
Maybe<debug::ConsoleCallArguments> Formatter(Isolate* isolate,
                                             BuiltinArguments& args,
                                             int target_index) {
  if (target_index == 0 || args.length() < target_index + 2 ||
      !args[target_index].IsString()) {
    return Just(debug::ConsoleCallArguments(args));
  }
  HandleScope scope(isolate);
  auto percent = isolate->factory()->LookupSingleCharacterStringFromCode('%');
  auto target = Handle<String>::cast(args.at(target_index));
  int offset = 0, index = target_index + 1, length = args.length();
  while (index < length) {
    auto current = args.at(index);
    offset = String::IndexOf(isolate, target, percent, offset);
    if (offset < 0 || offset == target->length() - 1) {
      break;
    }
    uint16_t specifier = target->Get(offset + 1, isolate);
    if (specifier == 'd' || specifier == 'f' || specifier == 'i') {
      if (current->IsSymbol()) {
        current = isolate->factory()->NaN_string();
      } else {
        Handle<Object> params[] = {current,
                                   isolate->factory()->NewNumberFromInt(10)};
        auto builtin = specifier == 'f' ? isolate->global_parse_float_fun()
                                        : isolate->global_parse_int_fun();
        if (!Execution::CallBuiltin(isolate, builtin,
                                    isolate->factory()->undefined_value(),
                                    arraysize(params), params)
                 .ToHandle(&current)) {
          return Nothing<debug::ConsoleCallArguments>();
        }
      }
    } else if (specifier == 's') {
      Handle<Object> params[] = {current};
      if (!Execution::CallBuiltin(isolate, isolate->string_function(),
                                  isolate->factory()->undefined_value(),
                                  arraysize(params), params)
               .ToHandle(&current)) {
        return Nothing<debug::ConsoleCallArguments>();
      }
    } else if (specifier == 'c' || specifier == 'o' || specifier == 'O') {
      // We leave the interpretation of %c (CSS), %o (optimally useful
      // formatting), and %O (generic JavaScript object formatting) to
      // the debugger front-end, and preserve these specifiers as well
      // as their arguments verbatim.
      index++;
      offset += 2;
      continue;
    } else {
      offset++;
      continue;
    }

    // Replace the |specifier| (including the '%' character) in |target|
    // with the |current| value converted to a string (the %parseInt% and
    // %parseFloat% builtin calls actually yield numbers).
    auto converted = Object::ToString(isolate, current).ToHandleChecked();
    auto prefix = isolate->factory()->NewProperSubString(target, 0, offset);
    auto suffix =
        isolate->factory()->NewSubString(target, offset + 2, target->length());
    if (!isolate->factory()
             ->NewConsString(prefix, converted)
             .ToHandle(&target)) {
      return Nothing<debug::ConsoleCallArguments>();
    }
    if (!isolate->factory()->NewConsString(target, suffix).ToHandle(&target)) {
      return Nothing<debug::ConsoleCallArguments>();
    }

    // Shift the remaining arguments, since we consumed |current|...
    for (int i = index; i < length - 1; ++i) args.set_at(i, args[i + 1]);
    // ...and reflect that change in the |length|.
    length--;
  }
  // Write back the |target| to the |args|.
  args.set_at(target_index, *target);
  return Just(debug::ConsoleCallArguments(args.address_of_first_argument(),
                                          length - 1));
}

MaybeHandle<Object> ConsoleCall(
    Isolate* isolate, BuiltinArguments& args,
    void (debug::ConsoleDelegate::*func)(const v8::debug::ConsoleCallArguments&,
                                         const v8::debug::ConsoleContext&),
    int target_index = 0) {
  if (!isolate->console_delegate()) {
    return isolate->factory()->undefined_value();
  }
  Handle<Object> context_id_obj = JSObject::GetDataProperty(
      args.target(), isolate->factory()->console_context_id_symbol());
  int context_id =
      context_id_obj->IsSmi() ? Handle<Smi>::cast(context_id_obj)->value() : 0;
  Handle<Object> context_name_obj = JSObject::GetDataProperty(
      args.target(), isolate->factory()->console_context_name_symbol());
  Handle<String> context_name = context_name_obj->IsString()
                                    ? Handle<String>::cast(context_name_obj)
                                    : isolate->factory()->anonymous_string();
  debug::ConsoleCallArguments wrapper;
  if (!Formatter(isolate, args, target_index).To(&wrapper)) {
    return MaybeHandle<Object>();
  }
  (isolate->console_delegate()->*func)(
      wrapper,
      v8::debug::ConsoleContext(context_id, Utils::ToLocal(context_name)));
  RETURN_EXCEPTION_IF_SCHEDULED_EXCEPTION(isolate, Object);
  return isolate->factory()->undefined_value();
}

void LogTimerEvent(Isolate* isolate, BuiltinArguments args,
                   v8::LogEventStatus se) {
  if (!isolate->logger()->is_logging()) return;
  HandleScope scope(isolate);
  std::unique_ptr<char[]> name;
  const char* raw_name = "default";
  if (args.length() > 1 && args[1].IsString()) {
    // Try converting the first argument to a string.
    name = args.at<String>(1)->ToCString();
    raw_name = name.get();
  }
  LOG(isolate, TimerEvent(se, raw_name));
}

}  // namespace

#define CONSOLE_BUILTIN_IMPLEMENTATION(call, name, target_index)           \
  BUILTIN(Console##call) {                                                 \
    HandleScope scope(isolate);                                            \
    RETURN_RESULT_OR_FAILURE(                                              \
        isolate, ConsoleCall(isolate, args, &debug::ConsoleDelegate::call, \
                             target_index));                               \
  }
CONSOLE_METHOD_LIST(CONSOLE_BUILTIN_IMPLEMENTATION)
#undef CONSOLE_BUILTIN_IMPLEMENTATION

BUILTIN(ConsoleTime) {
  LogTimerEvent(isolate, args, v8::LogEventStatus::kStart);
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(
      isolate, ConsoleCall(isolate, args, &debug::ConsoleDelegate::Time));
}

BUILTIN(ConsoleTimeEnd) {
  LogTimerEvent(isolate, args, v8::LogEventStatus::kEnd);
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(
      isolate, ConsoleCall(isolate, args, &debug::ConsoleDelegate::TimeEnd));
}

BUILTIN(ConsoleTimeStamp) {
  LogTimerEvent(isolate, args, v8::LogEventStatus::kStamp);
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(
      isolate, ConsoleCall(isolate, args, &debug::ConsoleDelegate::TimeStamp));
}

namespace {

void InstallContextFunction(Isolate* isolate, Handle<JSObject> target,
                            const char* name, Builtin builtin, int context_id,
                            Handle<Object> context_name) {
  Factory* const factory = isolate->factory();

  Handle<NativeContext> context(isolate->native_context());
  Handle<Map> map = isolate->sloppy_function_without_prototype_map();

  Handle<String> name_string =
      Name::ToFunctionName(isolate, factory->InternalizeUtf8String(name))
          .ToHandleChecked();
  Handle<SharedFunctionInfo> info =
      factory->NewSharedFunctionInfoForBuiltin(name_string, builtin);
  info->set_language_mode(LanguageMode::kSloppy);

  Handle<JSFunction> fun =
      Factory::JSFunctionBuilder{isolate, info, context}.set_map(map).Build();

  fun->shared().set_native(true);
  fun->shared().DontAdaptArguments();
  fun->shared().set_length(1);

  JSObject::AddProperty(isolate, fun, factory->console_context_id_symbol(),
                        handle(Smi::FromInt(context_id), isolate), NONE);
  if (context_name->IsString()) {
    JSObject::AddProperty(isolate, fun, factory->console_context_name_symbol(),
                          context_name, NONE);
  }
  JSObject::AddProperty(isolate, target, name_string, fun, NONE);
}

}  // namespace

BUILTIN(ConsoleContext) {
  HandleScope scope(isolate);

  Factory* const factory = isolate->factory();
  Handle<String> name = factory->InternalizeUtf8String("Context");
  Handle<SharedFunctionInfo> info =
      factory->NewSharedFunctionInfoForBuiltin(name, Builtin::kIllegal);
  info->set_language_mode(LanguageMode::kSloppy);

  Handle<JSFunction> cons =
      Factory::JSFunctionBuilder{isolate, info, isolate->native_context()}
          .Build();

  Handle<JSObject> prototype = factory->NewJSObject(isolate->object_function());
  JSFunction::SetPrototype(cons, prototype);

  Handle<JSObject> context = factory->NewJSObject(cons, AllocationType::kOld);
  DCHECK(context->IsJSObject());
  int id = isolate->last_console_context_id() + 1;
  isolate->set_last_console_context_id(id);

#define CONSOLE_BUILTIN_SETUP(call, name, target_index)                        \
  InstallContextFunction(isolate, context, #name, Builtin::kConsole##call, id, \
                         args.at(1));
  CONSOLE_METHOD_LIST(CONSOLE_BUILTIN_SETUP)
#undef CONSOLE_BUILTIN_SETUP
  InstallContextFunction(isolate, context, "time", Builtin::kConsoleTime, id,
                         args.at(1));
  InstallContextFunction(isolate, context, "timeEnd", Builtin::kConsoleTimeEnd,
                         id, args.at(1));
  InstallContextFunction(isolate, context, "timeStamp",
                         Builtin::kConsoleTimeStamp, id, args.at(1));

  return *context;
}

#undef CONSOLE_METHOD_LIST

}  // namespace internal
}  // namespace v8
