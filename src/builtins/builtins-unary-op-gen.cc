// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/ic/unary-op-assembler.h"

namespace v8 {
namespace internal {

#define UNARY_OP_BUILTIN_GENERATOR(Name)                          \
  Node* operand = Parameter(Descriptor::kOperand);                \
  Node* slot = Parameter(Descriptor::kSlot);                      \
  Node* vector = Parameter(Descriptor::kVector);                  \
  Node* context = Parameter(Descriptor::kContext);                \
  Node* result = Generate_##Name(context, operand, slot, vector); \
  Return(result);

TF_BUILTIN(BitwiseNotWithFeedback, UnaryOpAssembler) {
  UNARY_OP_BUILTIN_GENERATOR(BitwiseNotWithFeedback);
}

TF_BUILTIN(NegateWithFeedback, UnaryOpAssembler) {
  UNARY_OP_BUILTIN_GENERATOR(NegateWithFeedback);
}

TF_BUILTIN(IncrementWithFeedback, UnaryOpAssembler) {
  UNARY_OP_BUILTIN_GENERATOR(IncWithFeedback);
}

TF_BUILTIN(DecrementWithFeedback, UnaryOpAssembler) {
  UNARY_OP_BUILTIN_GENERATOR(DecWithFeedback);
}

#undef UNARY_OP_BUILTIN_GENERATOR

}  // namespace internal
}  // namespace v8
