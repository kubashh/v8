// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-async.h"
#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/code-factory.h"
#include "src/code-stub-assembler.h"
#include "src/frames-inl.h"

namespace v8 {
namespace internal {

namespace {

// Describe fields of Context associated with the AsyncIterator unwrap closure.
class ValueUnwrapContext {
 public:
  enum Fields { kDoneSlot = Context::MIN_CONTEXT_SLOTS, kLength };
};

Handle<Context> CreateAsyncIteratorValueUnwrapContext(Isolate* isolate,
                                                      bool done) {
  Handle<Context> native_context = isolate->native_context();
  Handle<FixedArray> fixedarray =
      isolate->factory()->NewFixedArray(ValueUnwrapContext::kLength);
  fixedarray->set_map_no_write_barrier(isolate->heap()->function_context_map());

  Handle<Context> context = Handle<Context>::cast(fixedarray);
  context->set(Context::CLOSURE_INDEX, native_context->closure(),
               SKIP_WRITE_BARRIER);
  context->set(Context::PREVIOUS_INDEX, isolate->heap()->undefined_value(),
               SKIP_WRITE_BARRIER);
  context->set(Context::EXTENSION_INDEX, isolate->heap()->the_hole_value(),
               SKIP_WRITE_BARRIER);
  context->set(Context::NATIVE_CONTEXT_INDEX, *native_context,
               SKIP_WRITE_BARRIER);

  context->set(ValueUnwrapContext::kDoneSlot, isolate->heap()->ToBoolean(done),
               SKIP_WRITE_BARRIER);
  return context;
}

typedef std::function<void(Handle<JSPromise> promise)> IfMethodUndefined;
Object* AsyncFromSyncIteratorMethod(Isolate* isolate, Handle<Object> receiver,
                                    Handle<Object> arg,
                                    Handle<Name> method_name,
                                    IfMethodUndefined if_method_undefined,
                                    const char* op_name) {
  Factory* const factory = isolate->factory();
  Handle<JSPromise> promise = JSPromise::New(isolate);
  Handle<Object> error;

  do {
    if (V8_UNLIKELY(!receiver->IsJSAsyncFromSyncIterator())) {
      Handle<String> operation = factory->NewStringFromAsciiChecked(op_name);
      error = factory->NewTypeError(
          MessageTemplate::kIncompatibleMethodReceiver, operation);
      break;
    }

    Handle<JSAsyncFromSyncIterator> iterator =
        Handle<JSAsyncFromSyncIterator>::cast(receiver);

    Handle<JSReceiver> sync_iterator(iterator->sync_iterator());
    Handle<Object> step;

    if (!JSReceiver::GetProperty(sync_iterator, method_name).ToHandle(&step)) {
      DCHECK(isolate->has_pending_exception());
      error = handle(isolate->pending_exception(), isolate);
      isolate->clear_pending_exception();
      break;
    }

    if (if_method_undefined) {
      if (step->IsNullOrUndefined(isolate)) {
        if_method_undefined(promise);
        return *promise;
      }
    }

    Handle<Object> iter_result_obj;
    Handle<Object> argv[] = {arg};
    if (!Execution::Call(isolate, step, sync_iterator, arraysize(argv), argv)
             .ToHandle(&iter_result_obj)) {
      DCHECK(isolate->has_pending_exception());
      error = handle(isolate->pending_exception(), isolate);
      isolate->clear_pending_exception();
      break;
    }

    if (V8_UNLIKELY(!iter_result_obj->IsJSReceiver())) {
      error = factory->NewTypeError(MessageTemplate::kIteratorResultNotAnObject,
                                    iter_result_obj);
      break;
    }

    Handle<JSReceiver> iter_result = Handle<JSReceiver>::cast(iter_result_obj);

    Handle<Object> value;
    if (!JSReceiver::GetProperty(iter_result, factory->value_string())
             .ToHandle(&value)) {
      DCHECK(isolate->has_pending_exception());
      error = handle(isolate->pending_exception(), isolate);
      isolate->clear_pending_exception();
      break;
    }

    Handle<Object> done;
    if (!JSReceiver::GetProperty(iter_result, factory->done_string())
             .ToHandle(&done)) {
      DCHECK(isolate->has_pending_exception());
      error = handle(isolate->pending_exception(), isolate);
      isolate->clear_pending_exception();
      break;
    }

    Handle<JSPromise> wrapper = JSPromise::New(isolate);
    wrapper->Resolve(value);

    Handle<Context> context =
        CreateAsyncIteratorValueUnwrapContext(isolate, done->BooleanValue());
    Handle<JSFunction> on_resolve = factory->NewFunctionFromSharedFunctionInfo(
        isolate->strict_function_without_prototype_map(),
        isolate->async_iterator_value_unwrap_shared_fun(), context);

    JSPromise::PerformPromiseThen(
        isolate, wrapper, on_resolve, factory->undefined_value(), promise,
        factory->undefined_value(), factory->undefined_value());

    return *promise;
  } while (0);

  DCHECK(!error.is_null());
  promise->Reject(error);
  return *promise;
}
}  // namespace

// https://tc39.github.io/proposal-async-iteration/
// Section #sec-%asyncfromsynciteratorprototype%.next
BUILTIN(AsyncFromSyncIteratorPrototypeNext) {
  HandleScope scope(isolate);

  const char* method_name = "[Async-from-Sync Iterator].prototype.next";
  return AsyncFromSyncIteratorMethod(
      isolate, args.receiver(), args.atOrUndefined(isolate, 1),
      isolate->factory()->next_string(), IfMethodUndefined(), method_name);
}

// https://tc39.github.io/proposal-async-iteration/
// Section #sec-%asyncfromsynciteratorprototype%.return
BUILTIN(AsyncFromSyncIteratorPrototypeReturn) {
  HandleScope scope(isolate);

  Handle<Object> return_value = args.atOrUndefined(isolate, 1);

  // clang-format off
  IfMethodUndefined if_return_undefined =
      [isolate, return_value](Handle<JSPromise> promise) {
        promise->Resolve(
            isolate->factory()->NewJSIteratorResult(return_value, true));
      };
  // clang-format on

  const char* method_name = "[Async-from-Sync Iterator].prototype.return";
  return AsyncFromSyncIteratorMethod(isolate, args.receiver(), return_value,
                                     isolate->factory()->return_string(),
                                     if_return_undefined, method_name);
}

// https://tc39.github.io/proposal-async-iteration/
// Section #sec-%asyncfromsynciteratorprototype%.throw
BUILTIN(AsyncFromSyncIteratorPrototypeThrow) {
  HandleScope scope(isolate);

  Handle<Object> throw_value = args.atOrUndefined(isolate, 1);

  // clang-format off
  IfMethodUndefined if_throw_undefined =
      [isolate, throw_value](Handle<JSPromise> promise) {
        promise->Reject(throw_value);
      };
  // clang-format on

  const char* method_name = "[Async-from-Sync Iterator].prototype.throw";
  return AsyncFromSyncIteratorMethod(isolate, args.receiver(), throw_value,
                                     isolate->factory()->throw_string(),
                                     if_throw_undefined, method_name);
}

TF_BUILTIN(AsyncIteratorValueUnwrap, AsyncBuiltinsAssembler) {
  Node* const value = Parameter(1);
  Node* const context = Parameter(4);

  Node* const done = LoadContextElement(context, ValueUnwrapContext::kDoneSlot);
  CSA_ASSERT(this, IsBoolean(done));

  Node* const unwrapped_value = CallStub(
      CodeFactory::CreateIterResultObject(isolate()), context, value, done);

  Return(unwrapped_value);
}

}  // namespace internal
}  // namespace v8
