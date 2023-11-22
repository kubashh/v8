// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_BINARY_OP_ASSEMBLER_H_
#define V8_IC_BINARY_OP_ASSEMBLER_H_

#include <functional>

#include "src/codegen/code-stub-assembler.h"

namespace v8 {
namespace internal {

namespace compiler {
class CodeAssemblerState;
}  // namespace compiler

class BinaryOpAssembler : public CodeStubAssembler {
 public:
  explicit BinaryOpAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  template <typename T>
  TNode<T> Generate_AddWithFeedback(
      const LazyNode<Context>& context, TNode<T> left, TNode<T> right,
      TNode<UintPtrT> slot, const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi);

  template <typename T>
  TNode<T> Generate_SubtractWithFeedback(
      const LazyNode<Context>& context, TNode<T> left, TNode<T> right,
      TNode<UintPtrT> slot, const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi);

  template <typename T>
  TNode<T> Generate_MultiplyWithFeedback(
      const LazyNode<Context>& context, TNode<T> left, TNode<T> right,
      TNode<UintPtrT> slot, const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi);

  template <typename T>
  TNode<T> Generate_DivideWithFeedback(
      const LazyNode<Context>& context, TNode<T> dividend, TNode<T> divisor,
      TNode<UintPtrT> slot, const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi);

  template <typename T>
  TNode<T> Generate_ModulusWithFeedback(
      const LazyNode<Context>& context, TNode<T> dividend, TNode<T> divisor,
      TNode<UintPtrT> slot, const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi);

  template <typename T>
  TNode<T> Generate_ExponentiateWithFeedback(
      const LazyNode<Context>& context, TNode<T> base, TNode<T> exponent,
      TNode<UintPtrT> slot, const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi);

  TNode<Object> Generate_BitwiseOrWithFeedback(
      const LazyNode<Context>& context, TNode<Object> left, TNode<Object> right,
      TNode<UintPtrT> slot, const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    TNode<Object> result = Generate_BitwiseBinaryOpWithFeedback(
        Operation::kBitwiseOr, left, right, context, slot,
        maybe_feedback_vector, update_feedback_mode, rhs_known_smi);
    return result;
  }

  TNode<Object> Generate_BitwiseXorWithFeedback(
      const LazyNode<Context>& context, TNode<Object> left, TNode<Object> right,
      TNode<UintPtrT> slot, const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    TNode<Object> result = Generate_BitwiseBinaryOpWithFeedback(
        Operation::kBitwiseXor, left, right, context, slot,
        maybe_feedback_vector, update_feedback_mode, rhs_known_smi);

    return result;
  }

  TNode<Object> Generate_BitwiseAndWithFeedback(
      const LazyNode<Context>& context, TNode<Object> left, TNode<Object> right,
      TNode<UintPtrT> slot, const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    TNode<Object> result = Generate_BitwiseBinaryOpWithFeedback(
        Operation::kBitwiseAnd, left, right, context, slot,
        maybe_feedback_vector, update_feedback_mode, rhs_known_smi);

    return result;
  }

  TNode<Object> Generate_ShiftLeftWithFeedback(
      const LazyNode<Context>& context, TNode<Object> left, TNode<Object> right,
      TNode<UintPtrT> slot, const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    TNode<Object> result = Generate_BitwiseBinaryOpWithFeedback(
        Operation::kShiftLeft, left, right, context, slot,
        maybe_feedback_vector, update_feedback_mode, rhs_known_smi);

    return result;
  }

  TNode<Object> Generate_ShiftRightWithFeedback(
      const LazyNode<Context>& context, TNode<Object> left, TNode<Object> right,
      TNode<UintPtrT> slot, const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    TNode<Object> result = Generate_BitwiseBinaryOpWithFeedback(
        Operation::kShiftRight, left, right, context, slot,
        maybe_feedback_vector, update_feedback_mode, rhs_known_smi);

    return result;
  }

  TNode<Object> Generate_ShiftRightLogicalWithFeedback(
      const LazyNode<Context>& context, TNode<Object> left, TNode<Object> right,
      TNode<UintPtrT> slot, const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    TNode<Object> result = Generate_BitwiseBinaryOpWithFeedback(
        Operation::kShiftRightLogical, left, right, context, slot,
        maybe_feedback_vector, update_feedback_mode, rhs_known_smi);

    return result;
  }

  TNode<Object> Generate_BitwiseBinaryOpWithFeedback(
      Operation bitwise_op, TNode<Object> left, TNode<Object> right,
      const LazyNode<Context>& context, TNode<UintPtrT> slot,
      const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    return rhs_known_smi
               ? Generate_BitwiseBinaryOpWithSmiOperandAndOptionalFeedback(
                     bitwise_op, left, right, context, &slot,
                     &maybe_feedback_vector, update_feedback_mode)
               : Generate_BitwiseBinaryOpWithOptionalFeedback(
                     bitwise_op, left, right, context, &slot,
                     &maybe_feedback_vector, update_feedback_mode);
  }

  TNode<Object> Generate_BitwiseBinaryOp(Operation bitwise_op,
                                         TNode<Object> left,
                                         TNode<Object> right,
                                         TNode<Context> context) {
    return Generate_BitwiseBinaryOpWithOptionalFeedback(
        bitwise_op, left, right, [&] { return context; }, nullptr, nullptr,
        UpdateFeedbackMode::kOptionalFeedback);
  }

  TNode<NanBoxed> Generate_BitwiseOrWithFeedback(
      const LazyNode<Context>& context, TNode<NanBoxed> left,
      TNode<NanBoxed> right, TNode<UintPtrT> slot,
      const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    TNode<NanBoxed> result = Generate_BitwiseBinaryOpWithFeedback(
        Operation::kBitwiseOr, left, right, context, slot,
        maybe_feedback_vector, update_feedback_mode, rhs_known_smi);
    return result;
  }

  TNode<NanBoxed> Generate_BitwiseXorWithFeedback(
      const LazyNode<Context>& context, TNode<NanBoxed> left,
      TNode<NanBoxed> right, TNode<UintPtrT> slot,
      const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    TNode<NanBoxed> result = Generate_BitwiseBinaryOpWithFeedback(
        Operation::kBitwiseXor, left, right, context, slot,
        maybe_feedback_vector, update_feedback_mode, rhs_known_smi);

    return result;
  }

  TNode<NanBoxed> Generate_BitwiseAndWithFeedback(
      const LazyNode<Context>& context, TNode<NanBoxed> left,
      TNode<NanBoxed> right, TNode<UintPtrT> slot,
      const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    TNode<NanBoxed> result = Generate_BitwiseBinaryOpWithFeedback(
        Operation::kBitwiseAnd, left, right, context, slot,
        maybe_feedback_vector, update_feedback_mode, rhs_known_smi);

    return result;
  }

  TNode<NanBoxed> Generate_ShiftLeftWithFeedback(
      const LazyNode<Context>& context, TNode<NanBoxed> left,
      TNode<NanBoxed> right, TNode<UintPtrT> slot,
      const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    TNode<NanBoxed> result = Generate_BitwiseBinaryOpWithFeedback(
        Operation::kShiftLeft, left, right, context, slot,
        maybe_feedback_vector, update_feedback_mode, rhs_known_smi);

    return result;
  }

  TNode<NanBoxed> Generate_ShiftRightWithFeedback(
      const LazyNode<Context>& context, TNode<NanBoxed> left,
      TNode<NanBoxed> right, TNode<UintPtrT> slot,
      const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    TNode<NanBoxed> result = Generate_BitwiseBinaryOpWithFeedback(
        Operation::kShiftRight, left, right, context, slot,
        maybe_feedback_vector, update_feedback_mode, rhs_known_smi);

    return result;
  }

  TNode<NanBoxed> Generate_ShiftRightLogicalWithFeedback(
      const LazyNode<Context>& context, TNode<NanBoxed> left,
      TNode<NanBoxed> right, TNode<UintPtrT> slot,
      const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    TNode<NanBoxed> result = Generate_BitwiseBinaryOpWithFeedback(
        Operation::kShiftRightLogical, left, right, context, slot,
        maybe_feedback_vector, update_feedback_mode, rhs_known_smi);

    return result;
  }

  TNode<NanBoxed> Generate_BitwiseBinaryOpWithFeedback(
      Operation bitwise_op, TNode<NanBoxed> left, TNode<NanBoxed> right,
      const LazyNode<Context>& context, TNode<UintPtrT> slot,
      const LazyNode<HeapObject>& maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
    return rhs_known_smi
               ? Generate_BitwiseBinaryOpWithSmiOperandAndOptionalFeedback(
                     bitwise_op, left, NanUnboxObject(right), context, &slot,
                     &maybe_feedback_vector, update_feedback_mode)
               : Generate_BitwiseBinaryOpWithOptionalFeedback(
                     bitwise_op, left, right, context, &slot,
                     &maybe_feedback_vector, update_feedback_mode);
  }

  TNode<NanBoxed> Generate_BitwiseBinaryOp(Operation bitwise_op,
                                           TNode<NanBoxed> left,
                                           TNode<NanBoxed> right,
                                           TNode<Context> context) {
    return Generate_BitwiseBinaryOpWithOptionalFeedback(
        bitwise_op, left, right, [&] { return context; }, nullptr, nullptr,
        UpdateFeedbackMode::kOptionalFeedback);
  }

 private:
  using SmiOperation =
      std::function<TNode<Object>(TNode<Smi>, TNode<Smi>, TVariable<Smi>*)>;
  using FloatOperation =
      std::function<TNode<Float64T>(TNode<Float64T>, TNode<Float64T>)>;

  template <typename T>
  TNode<T> Generate_BinaryOperationWithFeedback(
      const LazyNode<Context>& context, TNode<T> left, TNode<T> right,
      TNode<UintPtrT> slot, const LazyNode<HeapObject>& maybe_feedback_vector,
      const SmiOperation& smiOperation, const FloatOperation& floatOperation,
      Operation op, UpdateFeedbackMode update_feedback_mode,
      bool rhs_known_smi);

  template <typename T>
  TNode<T> Generate_BitwiseBinaryOpWithOptionalFeedback(
      Operation bitwise_op, TNode<T> left, TNode<T> right,
      const LazyNode<Context>& context, TNode<UintPtrT>* slot,
      const LazyNode<HeapObject>* maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode);

  template <typename T>
  TNode<T> Generate_BitwiseBinaryOpWithSmiOperandAndOptionalFeedback(
      Operation bitwise_op, TNode<T> left, TNode<Object> right,
      const LazyNode<Context>& context, TNode<UintPtrT>* slot,
      const LazyNode<HeapObject>* maybe_feedback_vector,
      UpdateFeedbackMode update_feedback_mode);

  // Check if output is known to be Smi when both operands of bitwise operation
  // are Smi.
  bool IsBitwiseOutputKnownSmi(Operation bitwise_op) {
    switch (bitwise_op) {
      case Operation::kBitwiseAnd:
      case Operation::kBitwiseOr:
      case Operation::kBitwiseXor:
      case Operation::kShiftRight:
        return true;
      default:
        return false;
    }
  }
};

}  // namespace internal
}  // namespace v8

#endif  // V8_IC_BINARY_OP_ASSEMBLER_H_
