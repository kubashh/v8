// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_ASSEMBLER_H_
#define V8_BUILTINS_BUILTINS_ASSEMBLER_H_

#include "src/code-stub-assembler.h"
#include "src/objects.h"

namespace v8 {
namespace internal {

class BuiltinsAssembler : public CodeStubAssembler {
 public:
  using Node = compiler::Node;

  explicit BuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  Node* BitwiseOp(Node* left32, Node* right32, Token::Value bitwise_op);

  void TaggedToNumeric(Node* context, Node* value, Label* done,
                       Variable* var_numeric);
  void TaggedToNumericWithFeedback(Node* context, Node* value, Label* done,
                                   Variable* var_numeric,
                                   Variable* var_feedback);

  Node* TruncateTaggedToWord32(Node* context, Node* value);
  void TaggedToWord32OrBigInt(Node* context, Node* value, Label* if_number,
                              Variable* var_word32, Label* if_bigint,
                              Variable* var_bigint);
  void TaggedToWord32OrBigIntWithFeedback(
      Node* context, Node* value, Label* if_number, Variable* var_word32,
      Label* if_bigint, Variable* var_bigint, Variable* var_feedback);

 private:
  enum class Feedback { kCollect, kNone };
  template <Feedback feedback>
  void TaggedToNumeric(Node* context, Node* value, Label* done,
                       Variable* var_numeric, Variable* var_feedback = nullptr);

  template <Feedback feedback, Object::Conversion conversion>
  void TaggedToWord32OrBigIntImpl(Node* context, Node* value, Label* if_number,
                                  Variable* var_word32,
                                  Label* if_bigint = nullptr,
                                  Variable* var_bigint = nullptr,
                                  Variable* var_feedback = nullptr);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_ASSEMBLER_H_
