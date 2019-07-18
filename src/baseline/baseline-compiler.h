// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_BASELINE_COMPILER_H_
#define V8_BASELINE_BASELINE_COMPILER_H_

#include "src/base/macros.h"
#include "src/code-stub-assembler.h"
#include "src/handles.h"
#include "src/interpreter/bytecode-register.h"
#include "src/interpreter/bytecodes.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class BaselineCompilationJob;
class BaselineCompilationInfo;
class BytecodeArray;
class HandlerTable;

namespace interpreter {

class BytecodeArrayIterator;

}  // namespace interpreter

namespace compiler {

class Node;
}

namespace baseline {

class JumpTargetAnalysis;

// Mid-tier baseline compiler.
class BaselineCompiler final : public CodeStubAssembler {
 public:
  explicit BaselineCompiler(compiler::CodeAssemblerState* state,
                            Handle<BytecodeArray> bytecode_array,
                            Handle<FeedbackVector> feedback_vector,
                            bool as_duplicate_parameters);

  // Creates a compilation job which will generate baseline code for function
  // in compilation_info.
  static BaselineCompilationJob* NewCompilationJob(
      uintptr_t stack_limit, Isolate* isolate,
      BaselineCompilationInfo* compilation_info);

  bool Compile();

 private:
  Node* LoadRegister(interpreter::Register reg);
  void StoreRegister(interpreter::Register reg, Node* value);
  Node* GetContext();
  Node* LoadFeedbackVector();

  void IncrementInvokationCount();
  void BuildStackFrame();
  void SaveBytecodeOffset();
  void BuildBailout();

  Node* MaybeBuildInlineUnaryOp(Builtins::Name builtin_id, Node* operand,
                                FeedbackSlot slot);
  Node* MaybeBuildInlineBinaryOp(Builtins::Name builtin_id, Node* left,
                                 Node* right, FeedbackSlot slot);
  Node* MaybeBuildInlineLoadNamedProperty(Node* reciever, TNode<Name> name,
                                          FeedbackSlot slot);

  void BuildUnaryOp(Builtins::Name builtin_id);
  void BuildBinaryOp(Builtins::Name builtin_id);
  void BuildSmiBinaryOp(Builtins::Name builtin_id);
  void BuildTest(Builtins::Name builtin_id);

  void BuildLdaLookupContextSlot(TypeofMode typeof_mode);
  void BuildLdaLookupGlobalSlot(TypeofMode typeof_mode);

  void BuildJump();
  void BuildJumpIf(bool jump_if_true);
  void BuildJumpIfToBoolean(bool jump_if_true);
  void VisitJumpIfIs(Node* value, bool jump_if_equal);

  Node* BuildCall(ConvertReceiverMode receiver_mode, Node* target,
                  interpreter::Register args, uint32_t arg_count);
  Node* BuildCall(ConvertReceiverMode receiver_mode);

  template <size_t... I>
  Node* BuildCall(ConvertReceiverMode receiver_mode, std::index_sequence<I...>);
  template <size_t I>
  Node* BuildCall(ConvertReceiverMode receiver_mode) {
    return BuildCall(receiver_mode, std::make_index_sequence<I>{});
  }

  Node* BuildCallRuntime();

  Node* BuildIntrinsicIsInstanceType(int type, interpreter::Register arg);
  Node* BuildIntrinsicIsJSReceiver(interpreter::Register arg);
  Node* BuildIntrinsicAsStubCall(Builtins::Name name,
                                 interpreter::Register first_arg_reg,
                                 uint32_t reg_count);
  Node* BuildIntrinsicLoadObjectField(interpreter::Register arg, int offset);
  Node* BuildIntrinsicGeneratorClose(interpreter::Register arg);
  Node* BuildIntrinsicGetImportMetaObject();
  Node* BuildIntrinsicCall(interpreter::Register first_arg_reg,
                           uint32_t reg_count);

  void BuildUpdateInterruptBudget(int32_t delta);

#define DECLARE_VISIT_BYTECODE(name, ...) void Visit##name();
  BYTECODE_LIST(DECLARE_VISIT_BYTECODE)
#undef DECLARE_VISIT_BYTECODE

  void AbortIfWordNotEqual(Node* lhs, Node* rhs, AbortReason abort_reason);
  void CallPrologue();
  void CallEpilogue(Node* result);

  void ExitThenEnterExceptionHandlers(int current_offset);
  void BuildDummyHandlerJump(int handler_offset);

  int register_count() const { return register_count_; }
  int parameter_count() const { return parameter_count_; }
  bool has_duplicate_parameters() const { return has_duplicate_parameters_; }

  Handle<BytecodeArray> bytecode_array() const { return bytecode_array_; }

  interpreter::BytecodeArrayIterator* bytecode_iterator() const {
    return bytecode_iterator_;
  }
  void set_bytecode_iterator(interpreter::BytecodeArrayIterator* iterator) {
    bytecode_iterator_ = iterator;
  }

  Handle<FeedbackVector> feedback_vector() const { return feedback_vector_; }

  JumpTargetAnalysis* jump_targets() { return jump_targets_; }
  void set_jump_targets(JumpTargetAnalysis* jump_targets) {
    jump_targets_ = jump_targets;
  }

  Isolate* isolate_;
  Handle<BytecodeArray> bytecode_array_;
  JumpTargetAnalysis* jump_targets_;

  interpreter::BytecodeArrayIterator* bytecode_iterator_;

  int register_count_;
  int parameter_count_;
  bool has_duplicate_parameters_;

  Variable accumulator_;
  std::vector<std::unique_ptr<Variable>> registers_;

  Handle<FeedbackVector> feedback_vector_;

  // An abstract representation for an exception handler that is being
  // entered and exited while the graph builder is iterating over the
  // underlying bytecode. The exception handlers within the bytecode are
  // well scoped, hence will form a stack during iteration.
  struct ExceptionHandler {
    int start_offset_;      // Start offset of the handled area in the bytecode.
    int end_offset_;        // End offset of the handled area in the bytecode.
    int handler_offset_;    // Handler entry offset within the bytecode.
    int context_register_;  // Index of register holding handler context.
  };

  // Exception handlers currently entered by the iteration.
  ZoneStack<ExceptionHandler> exception_handlers_;
  int current_exception_handler_;

  // Debug stack verification
  bool disable_stack_check_across_call_;
  Node* stack_pointer_before_call_;

  Node* frame_pointer_;
  Node* feedback_vector_node_;

  bool aborted_;
};

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_BASELINE_COMPILER_H_
