// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_CALL_ASSEMBLER_H_
#define V8_IC_CALL_ASSEMBLER_H_

#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

namespace compiler {
class CodeAssemblerState;
}

class CallAssembler : public CodeStubAssembler {
 public:
  typedef compiler::Node Node;

  explicit CallAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  // Increment the call count for a CALL_IC or construct call.
  // The call count is located at feedback_vector[slot_id + 1].
  void IncrementCallCount(compiler::Node* feedback_vector,
                          compiler::Node* slot_id);

  // Collect the callable |target| feedback for either a CALL_IC or
  // an INSTANCEOF_IC in the |feedback_vector| at |slot_id|.
  void CollectCallableFeedback(compiler::Node* target, compiler::Node* context,
                               compiler::Node* feedback_vector,
                               compiler::Node* slot_id);

  // Collect CALL_IC feedback for |target| function in the
  // |feedback_vector| at |slot_id|, and the call counts in
  // the |feedback_vector| at |slot_id+1|.
  void CollectCallFeedback(compiler::Node* target, compiler::Node* context,
                           compiler::Node* feedback_vector,
                           compiler::Node* slot_id);

  // Collect construct feedback in |feedback_vector| at |slot_id| and call
  // counts in the |feedback_vector| at |slot_id+1|. Jumps to
  // |call_construct_array| if the target is the array constructor, otherwise
  // jumps to |call_construct|.
  void CollectConstructFeedback(Node* target, Node* context, Node* new_target,
                                Node* slot_id, Node* feedback_vector,
                                Variable* var_site, Label* call_construct_array,
                                Label* call_construct);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_IC_CALL_ASSEMBLER_H_
