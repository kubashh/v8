// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/ic/binary-op-assembler.h"

namespace v8 {
namespace internal {

#define BINARY_OP_PARAMETERS()                   \
  Node* lhs = Parameter(Descriptor::kLeft);      \
  Node* rhs = Parameter(Descriptor::kRight);     \
  Node* slot = Parameter(Descriptor::kSlot);     \
  Node* vector = Parameter(Descriptor::kVector); \
  Node* context = Parameter(Descriptor::kContext)

#define BINARY_OP_BUILTIN_GENERATOR(Name)                                 \
  BINARY_OP_PARAMETERS();                                                 \
  Node* result = Generate_##Name(context, lhs, rhs, slot, vector, false); \
  Return(result);

#define BITWISE_OP_BUILTIN_GENERATOR(Name, Op)                               \
  BINARY_OP_PARAMETERS();                                                    \
  Node* result = Generate_BitwiseBinaryOpWithFeedback(Op, context, lhs, rhs, \
                                                      slot, vector);         \
  Return(result);

TF_BUILTIN(AddWithFeedback, BinaryOpAssembler) {
  BINARY_OP_BUILTIN_GENERATOR(AddWithFeedback);
}

TF_BUILTIN(SubtractWithFeedback, BinaryOpAssembler) {
  BINARY_OP_BUILTIN_GENERATOR(SubtractWithFeedback);
}

TF_BUILTIN(MultiplyWithFeedback, BinaryOpAssembler) {
  BINARY_OP_BUILTIN_GENERATOR(MultiplyWithFeedback);
}

TF_BUILTIN(DivideWithFeedback, BinaryOpAssembler) {
  BINARY_OP_BUILTIN_GENERATOR(DivideWithFeedback);
}

TF_BUILTIN(ModulusWithFeedback, BinaryOpAssembler) {
  BINARY_OP_BUILTIN_GENERATOR(ModulusWithFeedback);
}

TF_BUILTIN(ExponentiateWithFeedback, BinaryOpAssembler) {
  BINARY_OP_BUILTIN_GENERATOR(ExponentiateWithFeedback);
}

TF_BUILTIN(BitwiseAndWithFeedback, BinaryOpAssembler) {
  BITWISE_OP_BUILTIN_GENERATOR(BitwiseAndWithFeedback, Operation::kBitwiseAnd);
}

TF_BUILTIN(BitwiseOrWithFeedback, BinaryOpAssembler) {
  BITWISE_OP_BUILTIN_GENERATOR(BitwiseOrWithFeedback, Operation::kBitwiseOr);
}

TF_BUILTIN(BitwiseXorWithFeedback, BinaryOpAssembler) {
  BITWISE_OP_BUILTIN_GENERATOR(BitwiseXorWithFeedback, Operation::kBitwiseXor);
}

TF_BUILTIN(ShiftLeftWithFeedback, BinaryOpAssembler) {
  BITWISE_OP_BUILTIN_GENERATOR(ShiftLeftWithFeedback, Operation::kShiftLeft);
}

TF_BUILTIN(ShiftRightWithFeedback, BinaryOpAssembler) {
  BITWISE_OP_BUILTIN_GENERATOR(ShiftRightWithFeedback, Operation::kShiftRight);
}

TF_BUILTIN(ShiftRightLogicalWithFeedback, BinaryOpAssembler) {
  BITWISE_OP_BUILTIN_GENERATOR(ShiftRightLogicalWithFeedback,
                               Operation::kShiftRightLogical);
}

#undef BINARY_OP_PARAMETERS
#undef BINARY_OP_BUILTIN_GENERATOR
#undef BITWISE_OP_BUILTIN_GENERATOR

}  // namespace internal
}  // namespace v8
