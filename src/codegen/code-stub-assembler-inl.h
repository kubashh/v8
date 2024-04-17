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
                                      ConvertReceiverMode mode,
                                      TNode<Object> receiver, TArgs... args) {
  if (IsUndefinedConstant(receiver) || IsNullConstant(receiver)) {
    DCHECK_NE(mode, ConvertReceiverMode::kNotNullOrUndefined);
    return CallJS(Builtins::Call(ConvertReceiverMode::kNullOrUndefined),
                  context, callable, /* new_target */ {}, receiver, args...);
  }
  DCheckReceiver(mode, receiver);
  return CallJS(Builtins::Call(mode), context, callable, /* new_target */ {},
                receiver, args...);
}

template <class... TArgs>
TNode<Object> CodeStubAssembler::Call(TNode<Context> context,
                                      TNode<Object> callable,
                                      TNode<JSReceiver> receiver,
                                      TArgs... args) {
  return Call(context, callable, ConvertReceiverMode::kNotNullOrUndefined,
              receiver, args...);
}

template <class... TArgs>
TNode<Object> CodeStubAssembler::Call(TNode<Context> context,
                                      TNode<Object> callable,
                                      TNode<Object> receiver, TArgs... args) {
  return Call(context, callable, ConvertReceiverMode::kAny, receiver, args...);
}

template <class... TArgs>
TNode<Object> CodeStubAssembler::CallFunction(TNode<Context> context,
                                              TNode<JSFunction> callable,
                                              ConvertReceiverMode mode,
                                              TNode<Object> receiver,
                                              TArgs... args) {
  if (IsUndefinedConstant(receiver) || IsNullConstant(receiver)) {
    DCHECK_NE(mode, ConvertReceiverMode::kNotNullOrUndefined);
    return CallJS(Builtins::CallFunction(ConvertReceiverMode::kNullOrUndefined),
                  context, callable, /* new_target */ {}, receiver, args...);
  }
  DCheckReceiver(mode, receiver);
  return CallJS(Builtins::CallFunction(mode), context, callable,
                /* new_target */ {}, receiver, args...);
}

template <class... TArgs>
TNode<Object> CodeStubAssembler::CallFunction(TNode<Context> context,
                                              TNode<JSFunction> callable,
                                              TNode<JSReceiver> receiver,
                                              TArgs... args) {
  return CallFunction(context, callable,
                      ConvertReceiverMode::kNotNullOrUndefined, receiver,
                      args...);
}

template <class... TArgs>
TNode<Object> CodeStubAssembler::CallFunction(TNode<Context> context,
                                              TNode<JSFunction> callable,
                                              TNode<Object> receiver,
                                              TArgs... args) {
  return CallFunction(context, callable, ConvertReceiverMode::kAny, receiver,
                      args...);
}

}  // namespace internal
}  // namespace v8
#endif  // V8_CODEGEN_CODE_STUB_ASSEMBLER_INL_H_
