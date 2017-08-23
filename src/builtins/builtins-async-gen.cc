// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-async-gen.h"
#include "src/builtins/builtins-utils-gen.h"

namespace v8 {
namespace internal {

using compiler::Node;
template <class A>
using TNode = compiler::TNode<A>;

namespace {
// Describe fields of Context associated with the AsyncIterator unwrap closure.
class ValueUnwrapContext {
 public:
  enum Fields { kDoneSlot = Context::MIN_CONTEXT_SLOTS, kLength };
};

}  // namespace

TF_BUILTIN(Await, AsyncBuiltinsAssembler) {
  Node* const context = Parameter(Descriptor::kContext);
  Node* const generator = Parameter(Descriptor::kGenerator);
  Node* const value = Parameter(Descriptor::kValue);
  Node* const is_caught = Parameter(Descriptor::kIsCaught);
  Node* const outer_promise = Parameter(Descriptor::kOuterPromise);
  Node* const on_resolve_shared = Parameter(Descriptor::kOnResolveSharedInfo);
  Node* const on_reject_shared = Parameter(Descriptor::kOnRejectSharedInfo);

  CSA_SLOW_ASSERT(this, TaggedIsGeneratorObject(generator));
  CSA_SLOW_ASSERT(this, IsBoolean(is_caught));
  CSA_SLOW_ASSERT(this, IsSharedFunctionInfo(on_resolve_shared));
  CSA_SLOW_ASSERT(this, IsSharedFunctionInfo(on_reject_shared));

  Node* const native_context = LoadNativeContext(context);

#ifdef DEBUG
  {
    Node* const map = LoadContextElement(
        native_context, Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX);
    Node* const instance_size = LoadMapInstanceSize(map);
    // Assert that the strict function map has an instance size is
    // JSFunction::kSize
    CSA_ASSERT(this, WordEqual(instance_size, IntPtrConstant(JSFunction::kSize /
                                                             kPointerSize)));
  }
#endif

#ifdef DEBUG
  {
    Node* const promise_fun =
        LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
    Node* const map =
        LoadObjectField(promise_fun, JSFunction::kPrototypeOrInitialMapOffset);
    Node* const instance_size = LoadMapInstanceSize(map);
    // Assert that the JSPromise map has an instance size is
    // JSPromise::kSize
    CSA_ASSERT(this,
               WordEqual(instance_size,
                         IntPtrConstant(JSPromise::kSizeWithEmbedderFields /
                                        kPointerSize)));
  }
#endif

  static const int kWrappedPromiseOffset =
      FixedArray::SizeFor(AwaitContext::kLength);
  static const int kThrowawayPromiseOffset =
      kWrappedPromiseOffset + JSPromise::kSizeWithEmbedderFields;
  static const int kResolveClosureOffset =
      kThrowawayPromiseOffset + JSPromise::kSizeWithEmbedderFields;
  static const int kRejectClosureOffset =
      kResolveClosureOffset + JSFunction::kSize;
  static const int kTotalSize = kRejectClosureOffset + JSFunction::kSize;

  Node* const base = AllocateInNewSpace(kTotalSize);
  Node* const closure_context = base;
  {
    // Initialize closure context
    InitializeFunctionContext(native_context, closure_context,
                              AwaitContext::kLength);
    StoreContextElementNoWriteBarrier(closure_context,
                                      AwaitContext::kGeneratorSlot, generator);
  }

  // Let promiseCapability be ! NewPromiseCapability(%Promise%).
  Node* const promise_fun =
      LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
  Node* const promise_map =
      LoadObjectField(promise_fun, JSFunction::kPrototypeOrInitialMapOffset);
  Node* const wrapped_value = InnerAllocate(base, kWrappedPromiseOffset);
  {
    // Initialize Promise
    StoreMapNoWriteBarrier(wrapped_value, promise_map);
    InitializeJSObjectFromMap(
        wrapped_value, promise_map,
        IntPtrConstant(JSPromise::kSizeWithEmbedderFields),
        EmptyFixedArrayConstant(), EmptyFixedArrayConstant());
    PromiseInit(wrapped_value);
  }

  Node* const throwaway = InnerAllocate(base, kThrowawayPromiseOffset);
  {
    // Initialize throwawayPromise
    StoreMapNoWriteBarrier(throwaway, promise_map);
    InitializeJSObjectFromMap(
        throwaway, promise_map,
        IntPtrConstant(JSPromise::kSizeWithEmbedderFields),
        EmptyFixedArrayConstant(), EmptyFixedArrayConstant());
    PromiseInit(throwaway);
  }

  Node* const on_resolve = InnerAllocate(base, kResolveClosureOffset);
  {
    // Initialize resolve handler
    InitializeNativeClosure(closure_context, native_context, on_resolve,
                            on_resolve_shared);
  }

  Node* const on_reject = InnerAllocate(base, kRejectClosureOffset);
  {
    // Initialize reject handler
    InitializeNativeClosure(closure_context, native_context, on_reject,
                            on_reject_shared);
  }

  {
    // Add PromiseHooks if needed
    Label next(this);
    GotoIfNot(IsPromiseHookEnabledOrDebugIsActive(), &next);
    CallRuntime(Runtime::kPromiseHookInit, context, wrapped_value,
                outer_promise);
    CallRuntime(Runtime::kPromiseHookInit, context, throwaway, wrapped_value);
    Goto(&next);
    BIND(&next);
  }

  // Perform ! Call(promiseCapability.[[Resolve]], undefined, « promise »).
  CallBuiltin(Builtins::kResolveNativePromise, context, wrapped_value, value);

  // The Promise will be thrown away and not handled, but it shouldn't trigger
  // unhandled reject events as its work is done
  PromiseSetHasHandler(throwaway);

  Label do_perform_promise_then(this);
  GotoIfNot(IsDebugActive(), &do_perform_promise_then);
  {
    Label common(this);
    GotoIf(TaggedIsSmi(value), &common);
    GotoIfNot(HasInstanceType(value, JS_PROMISE_TYPE), &common);
    {
      // Mark the reject handler callback to be a forwarding edge, rather
      // than a meaningful catch handler
      Node* const key =
          HeapConstant(factory()->promise_forwarding_handler_symbol());
      CallRuntime(Runtime::kSetProperty, context, on_reject, key,
                  TrueConstant(), SmiConstant(STRICT));

      // If the rejection will be caught syntactically, mark the promise as
      // handled.
      GotoIf(IsFalse(is_caught), &common);
      PromiseSetHandledHint(value);
    }

    Goto(&common);
    BIND(&common);
    // Mark the dependency to outer Promise in case the throwaway Promise is
    // found on the Promise stack
    CSA_SLOW_ASSERT(this, HasInstanceType(outer_promise, JS_PROMISE_TYPE));

    Node* const key = HeapConstant(factory()->promise_handled_by_symbol());
    CallRuntime(Runtime::kSetProperty, context, throwaway, key, outer_promise,
                SmiConstant(STRICT));
  }

  Goto(&do_perform_promise_then);
  BIND(&do_perform_promise_then);

  CallBuiltin(Builtins::kPerformNativePromiseThen, context, wrapped_value,
              on_resolve, on_reject, throwaway);

  Return(wrapped_value);
}

void AsyncBuiltinsAssembler::InitializeNativeClosure(Node* context,
                                                     Node* native_context,
                                                     Node* function,
                                                     Node* shared_info) {
  Node* const function_map = LoadContextElement(
      native_context, Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX);
  StoreMapNoWriteBarrier(function, function_map);
  StoreObjectFieldRoot(function, JSObject::kPropertiesOrHashOffset,
                       Heap::kEmptyFixedArrayRootIndex);
  StoreObjectFieldRoot(function, JSObject::kElementsOffset,
                       Heap::kEmptyFixedArrayRootIndex);
  StoreObjectFieldRoot(function, JSFunction::kFeedbackVectorOffset,
                       Heap::kUndefinedCellRootIndex);
  StoreObjectFieldRoot(function, JSFunction::kPrototypeOrInitialMapOffset,
                       Heap::kTheHoleValueRootIndex);

  CSA_ASSERT(this, IsSharedFunctionInfo(shared_info))
  StoreObjectFieldNoWriteBarrier(
      function, JSFunction::kSharedFunctionInfoOffset, shared_info);
  StoreObjectFieldNoWriteBarrier(function, JSFunction::kContextOffset, context);

  Node* const code =
      LoadObjectField(shared_info, SharedFunctionInfo::kCodeOffset);
  StoreObjectFieldNoWriteBarrier(function, JSFunction::kCodeOffset, code);
  StoreObjectFieldRoot(function, JSFunction::kNextFunctionLinkOffset,
                       Heap::kUndefinedValueRootIndex);
}

Node* AsyncBuiltinsAssembler::CreateUnwrapClosure(Node* native_context,
                                                  Node* done) {
  Node* const map = LoadContextElement(
      native_context, Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX);
  Node* const on_fulfilled_shared = LoadContextElement(
      native_context, Context::ASYNC_ITERATOR_VALUE_UNWRAP_SHARED_FUN);
  CSA_ASSERT(this,
             HasInstanceType(on_fulfilled_shared, SHARED_FUNCTION_INFO_TYPE));
  Node* const closure_context =
      AllocateAsyncIteratorValueUnwrapContext(native_context, done);
  return AllocateFunctionWithMapAndContext(map, on_fulfilled_shared,
                                           closure_context);
}

Node* AsyncBuiltinsAssembler::AllocateAsyncIteratorValueUnwrapContext(
    Node* native_context, Node* done) {
  CSA_ASSERT(this, IsNativeContext(native_context));
  CSA_ASSERT(this, IsBoolean(done));

  Node* const context =
      CreatePromiseContext(native_context, ValueUnwrapContext::kLength);
  StoreContextElementNoWriteBarrier(context, ValueUnwrapContext::kDoneSlot,
                                    done);
  return context;
}

TNode<BoolT> AsyncBuiltinsAssembler::TaggedIsGeneratorObject(
    Node* tagged_object) {
  TNode<BoolT> tagged_is_not_smi = TaggedIsNotSmi(tagged_object);
  TVARIABLE(BoolT, var_phi, Int32Constant(0));
  Label done(this), if_false(this);

  // If tagged is an Smi, return false
  GotoIf(tagged_is_smi, &done);

  TNode<Int32T> instance_type = LoadInstanceType(tagged_object);

  // If instance_type < JS_GENERATOR_OBJECT_TYPE, return false
  GotoIfNot(Int32GreaterThanOrEqual(instance_type,
                                    Int32Constant(FIRST_GENERATOR_OBJECT_TYPE)),
            &done);

  // Return instance_type <= JS_ASYNC_GENERATOR_OBJECT_TYPE
  var_phi = Int32LessThanOrEqual(instance_type,
                                 Int32Constant(LAST_GENERATOR_OBJECT_TYPE));
  Goto(&done);
  BIND(&done);
  return var_phi;
}

TF_BUILTIN(AsyncIteratorValueUnwrap, AsyncBuiltinsAssembler) {
  Node* const value = Parameter(Descriptor::kValue);
  Node* const context = Parameter(Descriptor::kContext);

  Node* const done = LoadContextElement(context, ValueUnwrapContext::kDoneSlot);
  CSA_ASSERT(this, IsBoolean(done));

  Node* const unwrapped_value =
      CallBuiltin(Builtins::kCreateIterResultObject, context, value, done);

  Return(unwrapped_value);
}

// Convenience helpers:
Node* AsyncBuiltinsAssembler::Await(Node* context, Node* generator, Node* value,
                                    Node* outer_promise, Node* is_caught,
                                    TNode<IntPtrT> on_resolve_context_index,
                                    TNode<IntPtrT> on_reject_context_index) {
  Node* native_context = LoadNativeContext(context);
  Node* on_resolve_shared =
      LoadContextElement(native_context, on_resolve_context_index);
  Node* on_reject_shared =
      LoadContextElement(native_context, on_reject_context_index);
  return CallBuiltin(Builtins::kAwait, context, generator, value, outer_promise,
                     is_caught, on_resolve_shared, on_reject_shared);
}

Node* AsyncBuiltinsAssembler::Await(Node* context, Node* generator, Node* value,
                                    Node* outer_promise, Node* is_caught,
                                    int on_resolve_context_index,
                                    int on_reject_context_index) {
  return Await(context, generator, value, outer_promise, is_caught,
               IntPtrConstant(on_resolve_context_index),
               IntPtrConstant(on_reject_context_index));
}

}  // namespace internal
}  // namespace v8
