// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-assembler.h"

namespace v8 {
namespace internal {

using Node = compiler::Node;

Node* BuiltinsAssembler::BitwiseOp(Node* left32, Node* right32,
                                   Token::Value bitwise_op) {
  switch (bitwise_op) {
    case Token::BIT_AND:
      return ChangeInt32ToTagged(Word32And(left32, right32));
    case Token::BIT_OR:
      return ChangeInt32ToTagged(Word32Or(left32, right32));
    case Token::BIT_XOR:
      return ChangeInt32ToTagged(Word32Xor(left32, right32));
    case Token::SHL:
      return ChangeInt32ToTagged(
          Word32Shl(left32, Word32And(right32, Int32Constant(0x1f))));
    case Token::SAR:
      return ChangeInt32ToTagged(
          Word32Sar(left32, Word32And(right32, Int32Constant(0x1f))));
    case Token::SHR:
      return ChangeUint32ToTagged(
          Word32Shr(left32, Word32And(right32, Int32Constant(0x1f))));
    default:
      break;
  }
  UNREACHABLE();
}

void BuiltinsAssembler::TaggedToNumeric(Node* context, Node* value, Label* done,
                                        Variable* var_numeric) {
  TaggedToNumeric<Feedback::kNone>(context, value, done, var_numeric);
}

void BuiltinsAssembler::TaggedToNumericWithFeedback(Node* context, Node* value,
                                                    Label* done,
                                                    Variable* var_numeric,
                                                    Variable* var_feedback) {
  TaggedToNumeric<Feedback::kCollect>(context, value, done, var_numeric,
                                      var_feedback);
}

Node* BuiltinsAssembler::TruncateTaggedToWord32(Node* context, Node* value) {
  VARIABLE(var_result, MachineRepresentation::kWord32);
  Label done(this);
  TaggedToWord32OrBigIntImpl<Feedback::kNone, Object::Conversion::kToNumber>(
      context, value, &done, &var_result);
  BIND(&done);
  return var_result.value();
}

// Truncate {value} to word32 and jump to {if_number} if it is a Number,
// or find that it is a BigInt and jump to {if_bigint}.
void BuiltinsAssembler::TaggedToWord32OrBigInt(Node* context, Node* value,
                                               Label* if_number,
                                               Variable* var_word32,
                                               Label* if_bigint,
                                               Variable* var_bigint) {
  TaggedToWord32OrBigIntImpl<Feedback::kNone, Object::Conversion::kToNumeric>(
      context, value, if_number, var_word32, if_bigint, var_bigint);
}

// Truncate {value} to word32 and jump to {if_number} if it is a Number,
// or find that it is a BigInt and jump to {if_bigint}. In either case,
// store the type feedback in {var_feedback}.
void BuiltinsAssembler::TaggedToWord32OrBigIntWithFeedback(
    Node* context, Node* value, Label* if_number, Variable* var_word32,
    Label* if_bigint, Variable* var_bigint, Variable* var_feedback) {
  TaggedToWord32OrBigIntImpl<Feedback::kCollect,
                             Object::Conversion::kToNumeric>(
      context, value, if_number, var_word32, if_bigint, var_bigint,
      var_feedback);
}

template <BuiltinsAssembler::Feedback feedback>
void BuiltinsAssembler::TaggedToNumeric(Node* context, Node* value, Label* done,
                                        Variable* var_numeric,
                                        Variable* var_feedback) {
  var_numeric->Bind(value);
  Label if_smi(this), if_heapnumber(this), if_bigint(this), if_oddball(this);
  GotoIf(TaggedIsSmi(value), &if_smi);
  Node* map = LoadMap(value);
  GotoIf(IsHeapNumberMap(map), &if_heapnumber);
  Node* instance_type = LoadMapInstanceType(map);
  GotoIf(IsBigIntInstanceType(instance_type), &if_bigint);

  // {value} is not a Numeric yet.
  GotoIf(Word32Equal(instance_type, Int32Constant(ODDBALL_TYPE)), &if_oddball);
  var_numeric->Bind(CallBuiltin(Builtins::kNonNumberToNumeric, context, value));
  if (feedback == Feedback::kCollect) {
    var_feedback->Bind(SmiConstant(BinaryOperationFeedback::kAny));
  }
  Goto(done);

  BIND(&if_smi);
  if (feedback == Feedback::kCollect) {
    var_feedback->Bind(SmiConstant(BinaryOperationFeedback::kSignedSmall));
  }
  Goto(done);

  BIND(&if_heapnumber);
  if (feedback == Feedback::kCollect) {
    var_feedback->Bind(SmiConstant(BinaryOperationFeedback::kNumber));
  }
  Goto(done);

  BIND(&if_bigint);
  if (feedback == Feedback::kCollect) {
    var_feedback->Bind(SmiConstant(BinaryOperationFeedback::kBigInt));
  }
  Goto(done);

  BIND(&if_oddball);
  var_numeric->Bind(LoadObjectField(value, Oddball::kToNumberOffset));
  if (feedback == Feedback::kCollect) {
    var_feedback->Bind(SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
  }
  Goto(done);
}

template <BuiltinsAssembler::Feedback feedback, Object::Conversion conversion>
void BuiltinsAssembler::TaggedToWord32OrBigIntImpl(
    Node* context, Node* value, Label* if_number, Variable* var_word32,
    Label* if_bigint, Variable* var_bigint, Variable* var_feedback) {
  DCHECK(var_word32->rep() == MachineRepresentation::kWord32);
  DCHECK(var_bigint == nullptr ||
         var_bigint->rep() == MachineRepresentation::kTagged);
  DCHECK(var_feedback == nullptr ||
         var_feedback->rep() == MachineRepresentation::kTaggedSigned);

  // We might need to loop after conversion.
  VARIABLE(var_value, MachineRepresentation::kTagged, value);
  if (feedback == Feedback::kCollect) {
    var_feedback->Bind(SmiConstant(BinaryOperationFeedback::kNone));
  } else {
    DCHECK(var_feedback == nullptr);
  }
  Variable* loop_vars[] = {&var_value, var_feedback};
  int num_vars = feedback == Feedback::kCollect ? arraysize(loop_vars)
                                                : arraysize(loop_vars) - 1;
  Label loop(this, num_vars, loop_vars);
  Goto(&loop);
  BIND(&loop);
  {
    value = var_value.value();
    Label not_smi(this), is_heap_number(this), is_oddball(this),
        is_bigint(this);
    GotoIf(TaggedIsNotSmi(value), &not_smi);

    // {value} is a Smi.
    var_word32->Bind(SmiToWord32(value));
    if (feedback == Feedback::kCollect) {
      var_feedback->Bind(
          SmiOr(var_feedback->value(),
                SmiConstant(BinaryOperationFeedback::kSignedSmall)));
    }
    Goto(if_number);

    BIND(&not_smi);
    Node* map = LoadMap(value);
    GotoIf(IsHeapNumberMap(map), &is_heap_number);
    Node* instance_type = LoadMapInstanceType(map);
    if (conversion == Object::Conversion::kToNumeric) {
      GotoIf(IsBigIntInstanceType(instance_type), &is_bigint);
    }

    // Not HeapNumber (or BigInt if conversion == kToNumeric).
    {
      if (feedback == Feedback::kCollect) {
        // We do not require an Or with earlier feedback here because once we
        // convert the value to a Numeric, we cannot reach this path. We can
        // only reach this path on the first pass when the feedback is kNone.
        CSA_ASSERT(this, SmiEqual(var_feedback->value(),
                                  SmiConstant(BinaryOperationFeedback::kNone)));
      }
      GotoIf(Word32Equal(instance_type, Int32Constant(ODDBALL_TYPE)),
             &is_oddball);
      // Not an oddball either -> convert.
      auto builtin = conversion == Object::Conversion::kToNumeric
                         ? Builtins::kNonNumberToNumeric
                         : Builtins::kNonNumberToNumber;
      var_value.Bind(CallBuiltin(builtin, context, value));
      if (feedback == Feedback::kCollect) {
        var_feedback->Bind(SmiConstant(BinaryOperationFeedback::kAny));
      }
      Goto(&loop);

      BIND(&is_oddball);
      var_value.Bind(LoadObjectField(value, Oddball::kToNumberOffset));
      if (feedback == Feedback::kCollect) {
        var_feedback->Bind(
            SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
      }
      Goto(&loop);
    }

    BIND(&is_heap_number);
    var_word32->Bind(TruncateHeapNumberValueToWord32(value));
    if (feedback == Feedback::kCollect) {
      var_feedback->Bind(SmiOr(var_feedback->value(),
                               SmiConstant(BinaryOperationFeedback::kNumber)));
    }
    Goto(if_number);

    if (conversion == Object::Conversion::kToNumeric) {
      BIND(&is_bigint);
      var_bigint->Bind(value);
      if (feedback == Feedback::kCollect) {
        var_feedback->Bind(
            SmiOr(var_feedback->value(),
                  SmiConstant(BinaryOperationFeedback::kBigInt)));
      }
      Goto(if_bigint);
    }
  }
}

}  // namespace internal
}  // namespace v8
