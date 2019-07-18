// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_UNARY_OP_ASSEMBLER_H_
#define V8_IC_UNARY_OP_ASSEMBLER_H_

#include <functional>
#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

namespace compiler {
class CodeAssemblerState;
}

class UnaryOpAssembler : public CodeStubAssembler {
 public:
  typedef compiler::Node Node;

  explicit UnaryOpAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  Node* Generate_BitwiseNotWithFeedback(Node* context, Node* operand,
                                        Node* slot_index,
                                        Node* feedback_vector);

  Node* Generate_NegateWithFeedback(Node* context, Node* operand,
                                    Node* slot_index, Node* feedback_vector);

  Node* Generate_IncWithFeedback(Node* context, Node* operand, Node* slot_index,
                                 Node* feedback_vector);

  Node* Generate_DecWithFeedback(Node* context, Node* operand, Node* slot_index,
                                 Node* feedback_vector);

 private:
  typedef std::function<Node*(Node*, Variable*, Label*, Variable*)>
      SmiOperation;
  typedef std::function<Node*(Node*)> FloatOperation;
  typedef std::function<Node*(Node*)> BigIntOperation;

  Node* Generate_UnaryOpWithFeedback(Node* context, Node* operand,
                                     Node* slot_index, Node* feedback_vector,
                                     const SmiOperation& smiOperation,
                                     const FloatOperation& floatOperation,
                                     const BigIntOperation& bigIntOperation);
  Node* Generate_IncDecWithFeedback(Operation operation, Node* context,
                                    Node* operand, Node* slot_index,
                                    Node* feedback_vector);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_IC_UNARY_OP_ASSEMBLER_H_
