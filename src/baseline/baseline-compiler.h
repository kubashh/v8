// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_BASELINE_COMPILER_H_
#define V8_BASELINE_BASELINE_COMPILER_H_

#include <unordered_map>

#include "src/codegen/macro-assembler.h"
#include "src/handles/handles.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-register.h"
#include "src/interpreter/interpreter-intrinsics.h"
#include "src/objects/tagged-index.h"

namespace v8 {
namespace internal {

class BytecodeArray;

class BytecodeOffsetTableBuilder {
 public:
  void AddPosition(size_t pc_offset, size_t bytecode_offset) {
    // We should have written the start position first.
    DCHECK(!bytes_.empty());
    WriteUint(pc_offset - previous_pc_);
    WriteUint(bytecode_offset - previous_bytecode_);
    previous_pc_ = pc_offset;
    previous_bytecode_ = bytecode_offset;
  }

  void AddStartPosition(size_t pc_offset) {
    DCHECK(bytes_.empty());
    DCHECK_EQ(previous_pc_, 0);
    WriteUint(pc_offset);
    previous_pc_ = pc_offset;
  }

  template <typename LocalIsolate>
  Handle<ByteArray> ToBytecodeOffsetTable(LocalIsolate* isolate);

 private:
  void WriteUint(size_t n) {
    bool has_next;
    do {
      uint8_t v = n & ((1 << 7) - 1);
      n >>= 7;
      has_next = n != 0;
      v |= (has_next << 7);
      bytes_.push_back(v);
    } while (has_next);
  }

  size_t previous_pc_ = 0;
  size_t previous_bytecode_ = 0;
  std::vector<byte> bytes_;
};

class BaselineAssembler : public MacroAssembler {
 public:
  using MacroAssembler::MacroAssembler;

  Operand RegisterFrameOperand(interpreter::Register interpreter_register);
  Operand ContextOperand();
  Operand FunctionOperand();
  Operand FeedbackVectorOperand();

  void CallBuiltin(Builtins::Name builtin);

  void LoadTaggedPointerField(Register output, Register source, int offset);
  void LoadTaggedSignedField(Register output, Register source, int offset);
  void LoadTaggedAnyField(Register output, Register source, int offset);
  void StoreTaggedSignedField(Register target, int offset, Smi value);
  void StoreTaggedFieldWithWriteBarrier(Register target, int offset,
                                        Register value, Register scratch);
  void StoreTaggedFieldNoWriteBarrier(Register target, int offset,
                                      Register value);
  void LoadFixedArrayElement(Register output, Register array, int32_t index);
  void LoadPrototype(Register prototype, Register object);
  void LoadMapBitField(Register bitfield, Register map);

  Immediate AsSmiImmediate(int32_t value);

  void AddSmi(Register lhs, int32_t rhs);
  void AddSmi(Register lhs, Register rhs);
  void SubSmi(Register lhs, int32_t rhs);
  void SubSmi(Register lhs, Register rhs);
  void MulSmi(Register lhs, Register rhs);
  void BitwiseOrSmi(Register lhs, int32_t rhs);
  void BitwiseOrSmi(Register lhs, Register rhs);
  void BitwiseXorSmi(Register lhs, int32_t rhs);
  void BitwiseXorSmi(Register lhs, Register rhs);
  void BitwiseAndSmi(Register lhs, int32_t rhs);
  void BitwiseAndSmi(Register lhs, Register rhs);
  void ShiftLeftSmi(Register lhs, int32_t rhs);
  void ShiftRightSmi(Register lhs, int32_t rhs);
  void ShiftRightLogicalSmi(Register lhs, int32_t rhs);
  void LeaveFrame();

  void Switch(Register reg, int case_value_base, Label** labels,
              int num_labels);
  void Compare(Register lhs, Operand rhs);
};

class BaselineCompiler {
 public:
  explicit BaselineCompiler(Isolate* isolate, int formal_parameter_count,
                            Handle<BytecodeArray> bytecode);

  void GenerateCode();
  Handle<Code> Build(Isolate* isolate);

 private:
  class SaveAccumulatorScope;

  void Prologue();
  void PrologueFillFrame();
  void PrologueHandleOptimizationState(Register feedback_vector);

  void PreVisitSingleBytecode();
  void VisitSingleBytecode();

  void Epilogue();
  void EpilogueReturn();

  Operand UndefinedOperand();

  // Frame values
  void LoadFunction(Register output);
  void LoadContext(Register output);
  void StoreContext(Register context);

  // Accumulator
  void LoadAccumulator(Register output);
  void PushAccumulator();
  void PopAccumulator();

  // Register operands.
  Operand RegisterOperand(interpreter::Register reg);
  Operand RegisterOperand(int operand_index);
  void LoadRegister(Register output, interpreter::Register source);
  void LoadRegister(Register output, int operand_index);
  void StoreRegister(interpreter::Register output, Register value);
  void StoreRegister(int operand_index, Register value);
  void StoreRegisterPair(int operand_index, Register val0, Register val1);

  // Constant pool operands.
  template <typename Type>
  Handle<Type> Constant(int operand_index);
  Smi ConstantSmi(int operand_index);
  template <typename Type>
  void LoadConstant(Register output, int operand_index);

  // Immediate value operands.
  uint32_t Uint(int operand_index);
  int32_t Int(int operand_index);
  uint32_t Index(int operand_index);
  uint32_t Flag(int operand_index);
  uint32_t RegisterCount(int operand_index);
  TaggedIndex IndexAsTagged(int operand_index);
  Smi IndexAsSmi(int operand_index);
  Smi IntAsSmi(int operand_index);
  Smi FlagAsSmi(int operand_index);

  // Jump helpers.
  Label* BuildForwardJumpLabel();
  void UpdateInterruptBudgetAndJumpToLabel(int weight, Label* label,
                                           Label* skip_interrupt_label);
  void UpdateInterruptBudgetAndDoInterpreterJump();
  void UpdateInterruptBudgetAndDoInterpreterJumpIfRoot(RootIndex root);
  void UpdateInterruptBudgetAndDoInterpreterJumpIfNotRoot(RootIndex root);

  // Feedback vector.
  Operand FeedbackVector();
  void LoadFeedbackVector(Register output);
  void LoadClosureFeedbackArray(Register output, Register closure);
  void UpdateFeedback(int operand_index, int bit, Label* done);

  // Position mapping.
  void AddPosition();
  void AddAsStartPosition();

  // Misc. helpers.
  void SelectBooleanConstant(Condition condition, Register output);

  // Returns ToBoolean result into kInterpreterAccumulatorRegister.
  void JumpIfToBoolean(bool do_jump_if_true, Register reg, Label* label,
                       Label::Distance distance = Label::kFar);

  // Call helpers.
  template <typename... Args>
  void CallBuiltin(Builtins::Name builtin, Args... args);
  template <typename... Args>
  void CallRuntime(Runtime::FunctionId function, Args... args);

  template <typename... Args>
  void TailCallBuiltin(Builtins::Name builtin, Args... args);

  void BuildBinop(
      Builtins::Name builtin_name, bool fast_path = false,
      bool check_overflow = false,
      std::function<void(Register, Register)> instruction = [](Register,
                                                               Register) {});
  void BuildUnop(Builtins::Name builtin_name);
  void BuildCompare(Builtins::Name builtin_name, Condition condition);
  void BuildBinopWithSmi(
      Builtins::Name builtin_name, bool fast_path = false,
      bool check_overflow = false,
      std::function<void(Register, int32_t)> instruction = [](Register,
                                                              int32_t) {});

  template <typename... Args>
  void BuildCall(ConvertReceiverMode mode, uint32_t slot, uint32_t arg_count,
                 Args... args);

#ifdef V8_TRACE_IGNITION
  void TraceBytecode(Runtime::FunctionId function_id);
#endif

  // Single bytecode visitors.
#define DECLARE_VISITOR(name, ...) void Visit##name();
  BYTECODE_LIST(DECLARE_VISITOR)
#undef DECLARE_VISITOR

  // Intrinsic call visitors.
#define DECLARE_VISITOR(name, ...) \
  void VisitIntrinsic##name(interpreter::RegisterList args);
  INTRINSICS_LIST(DECLARE_VISITOR)
#undef DECLARE_VISITOR

  const interpreter::BytecodeArrayAccessor& accessor() { return iterator_; }

  Isolate* isolate_;
  const int formal_parameter_count_;
  Handle<BytecodeArray> bytecode_;
  BaselineAssembler masm_;
  interpreter::BytecodeArrayIterator iterator_;
  BytecodeOffsetTableBuilder bytecode_offset_table_builder_;

  std::unordered_map<int, std::vector<Label>> linked_labels_;
  std::unordered_map<int, Label> unlinked_labels_;
  std::unordered_set<int> handler_offsets_;

  // Epilogue stuff.
  Label return_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_BASELINE_H_