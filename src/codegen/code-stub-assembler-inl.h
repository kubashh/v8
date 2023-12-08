// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_CODE_STUB_ASSEMBLER_INL_H_
#define V8_CODEGEN_CODE_STUB_ASSEMBLER_INL_H_

#include <functional>

#include "src/builtins/builtins-inl.h"
#include "src/codegen/code-stub-assembler.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

template <class... TArgs>
TNode<Object> CodeStubAssembler::Call(TNode<Context> context,
                                      TNode<Object> callable,
                                      TNode<JSReceiver> receiver,
                                      TArgs... args) {
  IncumbentHint incumbent_hint = Builtins::GetIncumbentMode(state()->builtin());
  return CallJS(
      Builtins::Call(incumbent_hint, ConvertReceiverMode::kNotNullOrUndefined),
      context, callable, /* new_target */ {}, receiver, args...);
}

template <class... TArgs>
TNode<Object> CodeStubAssembler::Call(TNode<Context> context,
                                      TNode<Object> callable,
                                      TNode<Object> receiver, TArgs... args) {
  IncumbentHint incumbent_hint = Builtins::GetIncumbentMode(state()->builtin());
  if (IsUndefinedConstant(receiver) || IsNullConstant(receiver)) {
    return CallJS(
        Builtins::Call(incumbent_hint, ConvertReceiverMode::kNullOrUndefined),
        context, callable, /* new_target */ {}, receiver, args...);
  }
  return CallJS(Builtins::Call(incumbent_hint, ConvertReceiverMode::kAny),
                context, callable, /* new_target */ {}, receiver, args...);
}

// template <class T, class... TArgs>
// TNode<T> CodeStubAssembler::CallRuntime(Runtime::FunctionId function,
//                                         TNode<Object> context, TArgs... args)
//                                         {
//   IncumbentTrackingScope call_scope(this, Builtins::RuntimeCEntry(1),
//   context); return CodeAssembler::CallRuntime<T>(function, context, args...);
// }

// template <class... TArgs>
// void CodeStubAssembler::TailCallRuntime(Runtime::FunctionId function,
//                                         TNode<Object> context, TArgs... args)
//                                         {
//   CodeAssembler::TailCallRuntime(function, context, args...);
// }

// template <class... TArgs>
// void CodeStubAssembler::TailCallRuntime(Runtime::FunctionId function,
//                                         TNode<Int32T> arity,
//                                         TNode<Object> context, TArgs... args)
//                                         {
//   CodeAssembler::TailCallRuntime(function, arity, context, args...);
// }

// template <typename T, class... TArgs>
// TNode<T> CodeStubAssembler::CallBuiltin(Builtin builtin, TNode<Object>
// context,
//                                         TArgs... args) {
//   IncumbentTrackingScope call_scope(this, builtin, context);
//   return CodeAssembler::CallBuiltin<T>(builtin, context, args...);
// }

// template <class... TArgs>
// void CodeStubAssembler::CallBuiltinVoid(Builtin builtin, TNode<Object>
// context,
//                                         TArgs... args) {
//   IncumbentTrackingScope call_scope(this, builtin, context);
//   CodeAssembler::CallBuiltinVoid(builtin, context, args...);
// }

// template <class... TArgs>
// void CodeStubAssembler::TailCallBuiltin(Builtin builtin, TNode<Object>
// context,
//                                         TArgs... args) {
//   CodeAssembler::TailCallBuiltin(builtin, context, args...);
// }

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_CODE_STUB_ASSEMBLER_INL_H_
