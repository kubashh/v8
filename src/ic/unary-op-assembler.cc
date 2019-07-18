// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ic/unary-op-assembler.h"

#include "src/globals.h"

namespace v8 {
namespace internal {

using compiler::Node;

Node* UnaryOpAssembler::Generate_BitwiseNotWithFeedback(Node* context,
                                                        Node* operand,
                                                        Node* slot_index,
                                                        Node* feedback_vector) {
  VARIABLE(var_word32, MachineRepresentation::kWord32);
  VARIABLE(var_feedback, MachineRepresentation::kTaggedSigned);
  VARIABLE(var_bigint, MachineRepresentation::kTagged);
  VARIABLE(result, MachineRepresentation::kTagged);
  Label if_number(this), if_bigint(this, Label::kDeferred), end(this);
  TaggedToWord32OrBigIntWithFeedback(context, operand, &if_number, &var_word32,
                                     &if_bigint, &var_bigint, &var_feedback);

  // Number case.
  BIND(&if_number);
  result.Bind(ChangeInt32ToTagged(Signed(Word32Not(var_word32.value()))));
  Node* result_type = SelectSmiConstant(TaggedIsSmi(result.value()),
                                        BinaryOperationFeedback::kSignedSmall,
                                        BinaryOperationFeedback::kNumber);
  UpdateFeedback(SmiOr(result_type, var_feedback.value()), feedback_vector,
                 slot_index);
  Goto(&end);

  // BigInt case.
  BIND(&if_bigint);
  UpdateFeedback(SmiConstant(BinaryOperationFeedback::kBigInt), feedback_vector,
                 slot_index);
  result.Bind(CallRuntime(Runtime::kBigIntUnaryOp, context, var_bigint.value(),
                          SmiConstant(Operation::kBitwiseNot)));
  Goto(&end);

  BIND(&end);
  return result.value();
}

Node* UnaryOpAssembler::Generate_UnaryOpWithFeedback(
    Node* context, Node* operand, Node* slot_index, Node* feedback_vector,
    const SmiOperation& smiOperation, const FloatOperation& floatOperation,
    const BigIntOperation& bigIntOperation) {
  VARIABLE(var_value, MachineRepresentation::kTagged, operand);

  VARIABLE(var_result, MachineRepresentation::kTagged);
  VARIABLE(var_float_value, MachineRepresentation::kFloat64);
  VARIABLE(var_feedback, MachineRepresentation::kTaggedSigned,
           SmiConstant(BinaryOperationFeedback::kNone));
  Variable* loop_vars[] = {&var_value, &var_feedback};
  Label start(this, arraysize(loop_vars), loop_vars), end(this);
  Label do_float_op(this, &var_float_value);
  Goto(&start);
  // We might have to try again after ToNumeric conversion.
  BIND(&start);
  {
    Label if_smi(this), if_heapnumber(this), if_bigint(this);
    Label if_oddball(this), if_other(this);
    Node* value = var_value.value();
    GotoIf(TaggedIsSmi(value), &if_smi);
    Node* map = LoadMap(value);
    GotoIf(IsHeapNumberMap(map), &if_heapnumber);
    Node* instance_type = LoadMapInstanceType(map);
    GotoIf(IsBigIntInstanceType(instance_type), &if_bigint);
    Branch(InstanceTypeEqual(instance_type, ODDBALL_TYPE), &if_oddball,
           &if_other);

    BIND(&if_smi);
    {
      var_result.Bind(
          smiOperation(value, &var_feedback, &do_float_op, &var_float_value));
      Goto(&end);
    }

    BIND(&if_heapnumber);
    {
      var_float_value.Bind(LoadHeapNumberValue(value));
      Goto(&do_float_op);
    }

    BIND(&if_bigint);
    {
      var_result.Bind(bigIntOperation(value));
      CombineFeedback(&var_feedback, BinaryOperationFeedback::kBigInt);
      Goto(&end);
    }

    BIND(&if_oddball);
    {
      // We do not require an Or with earlier feedback here because once we
      // convert the value to a number, we cannot reach this path. We can
      // only reach this path on the first pass when the feedback is kNone.
      CSA_ASSERT(this, SmiEqual(var_feedback.value(),
                                SmiConstant(BinaryOperationFeedback::kNone)));
      OverwriteFeedback(&var_feedback,
                        BinaryOperationFeedback::kNumberOrOddball);
      var_value.Bind(LoadObjectField(value, Oddball::kToNumberOffset));
      Goto(&start);
    }

    BIND(&if_other);
    {
      // We do not require an Or with earlier feedback here because once we
      // convert the value to a number, we cannot reach this path. We can
      // only reach this path on the first pass when the feedback is kNone.
      CSA_ASSERT(this, SmiEqual(var_feedback.value(),
                                SmiConstant(BinaryOperationFeedback::kNone)));
      OverwriteFeedback(&var_feedback, BinaryOperationFeedback::kAny);
      var_value.Bind(
          CallBuiltin(Builtins::kNonNumberToNumeric, context, value));
      Goto(&start);
    }
  }

  BIND(&do_float_op);
  {
    CombineFeedback(&var_feedback, BinaryOperationFeedback::kNumber);
    var_result.Bind(
        AllocateHeapNumberWithValue(floatOperation(var_float_value.value())));
    Goto(&end);
  }

  BIND(&end);
  UpdateFeedback(var_feedback.value(), feedback_vector, slot_index);
  return var_result.value();
}

Node* UnaryOpAssembler::Generate_NegateWithFeedback(Node* context,
                                                    Node* operand,
                                                    Node* slot_index,
                                                    Node* feedback_vector) {
  auto smiFunction = [=](Node* smi_value, Variable* var_feedback,
                         Label* do_float_op, Variable* var_float) {
    VARIABLE(var_result, MachineRepresentation::kTagged);
    Label if_zero(this), if_min_smi(this), end(this);
    // Return -0 if operand is 0.
    GotoIf(SmiEqual(smi_value, SmiConstant(0)), &if_zero);

    // Special-case the minimum Smi to avoid overflow.
    GotoIf(SmiEqual(smi_value, SmiConstant(Smi::kMinValue)), &if_min_smi);

    // Else simply subtract operand from 0.
    CombineFeedback(var_feedback, BinaryOperationFeedback::kSignedSmall);
    var_result.Bind(SmiSub(SmiConstant(0), smi_value));
    Goto(&end);

    BIND(&if_zero);
    CombineFeedback(var_feedback, BinaryOperationFeedback::kNumber);
    var_result.Bind(MinusZeroConstant());
    Goto(&end);

    BIND(&if_min_smi);
    var_float->Bind(SmiToFloat64(smi_value));
    Goto(do_float_op);

    BIND(&end);
    return var_result.value();
  };

  auto floatFunction = [=](Node* float_value) {
    return Float64Neg(float_value);
  };

  auto bigIntFunction = [=](Node* bigint_value) {
    return CallRuntime(Runtime::kBigIntUnaryOp, context, bigint_value,
                       SmiConstant(Operation::kNegate));
  };

  return Generate_UnaryOpWithFeedback(context, operand, slot_index,
                                      feedback_vector, smiFunction,
                                      floatFunction, bigIntFunction);
}

Node* UnaryOpAssembler::Generate_IncDecWithFeedback(Operation operation,
                                                    Node* context,
                                                    Node* operand,
                                                    Node* slot_index,
                                                    Node* feedback_vector) {
  DCHECK(operation == Operation::kIncrement ||
         operation == Operation::kDecrement);
  auto smiFunction = [=](Node* smi_value, Variable* var_feedback,
                         Label* do_float_op, Variable* var_float) {
    // Try fast Smi operation first.
    Node* value = BitcastTaggedToWord(smi_value);
    Node* one = BitcastTaggedToWord(SmiConstant(1));
    Node* pair = operation == Operation::kIncrement
                     ? IntPtrAddWithOverflow(value, one)
                     : IntPtrSubWithOverflow(value, one);
    Node* overflow = Projection(1, pair);

    // Check if the Smi operation overflowed.
    Label if_overflow(this), if_notoverflow(this);
    Branch(overflow, &if_overflow, &if_notoverflow);

    BIND(&if_overflow);
    {
      var_float->Bind(SmiToFloat64(smi_value));
      Goto(do_float_op);
    }

    BIND(&if_notoverflow);
    CombineFeedback(var_feedback, BinaryOperationFeedback::kSignedSmall);
    return BitcastWordToTaggedSigned(Projection(0, pair));
  };

  auto floatFunction = [=](Node* float_value) {
    return operation == Operation::kIncrement
               ? Float64Add(float_value, Float64Constant(1.0))
               : Float64Sub(float_value, Float64Constant(1.0));
  };

  auto bigIntFunction = [=](Node* bigint_value) {
    return CallRuntime(Runtime::kBigIntUnaryOp, context, bigint_value,
                       SmiConstant(operation));
  };

  return Generate_UnaryOpWithFeedback(context, operand, slot_index,
                                      feedback_vector, smiFunction,
                                      floatFunction, bigIntFunction);
}

Node* UnaryOpAssembler::Generate_IncWithFeedback(Node* context, Node* operand,
                                                 Node* slot_index,
                                                 Node* feedback_vector) {
  return Generate_IncDecWithFeedback(Operation::kIncrement, context, operand,
                                     slot_index, feedback_vector);
}

Node* UnaryOpAssembler::Generate_DecWithFeedback(Node* context, Node* operand,
                                                 Node* slot_index,
                                                 Node* feedback_vector) {
  return Generate_IncDecWithFeedback(Operation::kDecrement, context, operand,
                                     slot_index, feedback_vector);
}

}  // namespace internal
}  // namespace v8
