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
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class BytecodeArray;

namespace baseline {

enum class Condition : uint8_t;

class BytecodeOffsetTableBuilder {
 public:
  void AddPosition(size_t pc_offset, size_t bytecode_offset) {
    WriteUint(pc_offset - previous_pc_);
    WriteUint(bytecode_offset - previous_bytecode_);
    previous_pc_ = pc_offset;
    previous_bytecode_ = bytecode_offset;
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

class BaselineAssembler {
 public:
  class ScratchRegisterScope;

  explicit BaselineAssembler(MacroAssembler* masm) : masm_(masm) {}
  MemOperand RegisterFrameOperand(interpreter::Register interpreter_register);
  MemOperand ContextOperand();
  MemOperand FunctionOperand();
  MemOperand FeedbackVectorOperand();

  void GetCode(Isolate* isolate, CodeDesc* desc);
  int pc_offset() const;
  bool emit_debug_code() const;
  void Assert(Condition condition, AbortReason reason);
  void RecordComment(const char* string);
  void Trap();

  void Bind(Label* label);
  void JumpIf(Condition cc, Label* target,
              Label::Distance distance = Label::kFar);
  void Jump(Label* target, Label::Distance distance = Label::kFar);
  void JumpIfRoot(Register value, RootIndex index, Label* target,
                  Label::Distance distance = Label::kFar);
  void JumpIfNotRoot(Register value, RootIndex index, Label* target,
                     Label ::Distance distance = Label::kFar);
  void JumpIfSmi(Register value, Label* target,
                 Label::Distance distance = Label::kFar);
  void JumpIfNotSmi(Register value, Label* target,
                    Label::Distance distance = Label::kFar);
  void TestAndBranchIfAllClear(Register value, int mask, Label* target,
                               Label::Distance distance = Label::kFar);
  void TestAndBranchIfAnySet(Register value, int mask, Label* target,
                             Label::Distance distance = Label::kFar);

  void CmpObjectType(Register object, InstanceType instance_type, Register map);
  void CmpInstanceType(Register value, InstanceType instance_type);
  void Cmp(Register value, Smi smi);
  void ComparePointer(Register value, MemOperand operand);
  Condition CheckSmi(Register value);
  void SmiCompare(Register lhs, Register rhs);
  void CompareTagged(Register value, MemOperand operand);
  void CompareTagged(MemOperand operand, Register value);
  void CompareByte(Register value, int32_t byte);

  void LoadMap(Register output, Register value);
  void LoadRoot(Register output, RootIndex index);
  void LoadNativeContextSlot(Register output, uint32_t index);

  void Move(Register output, Register source);
  void Move(Register output, MemOperand operand);
  void Move(Register output, Smi value);
  void Move(Register output, TaggedIndex value);
  void Move(Register output, interpreter::Register source);
  void Move(interpreter::Register output, Register source);
  void Move(Register output, RootIndex source);
  void Move(MemOperand output, Register source);
  void Move(Register output, ExternalReference reference);
  void Move(Register output, Handle<HeapObject> value);
  void Move(Register output, int32_t immediate);
  void MoveMaybeSmi(Register output, Register source);
  void MoveSmi(Register output, Register source);

  void Push(Register reg);
  void Push(MemOperand operand);
  void Push(interpreter::Register source);
  void Push(RootIndex source);
  void Push(TaggedIndex value);
  void Push(Smi value);
  void Push(Handle<HeapObject> object);
  void Push(int32_t immediate);
  void Pop(Register reg);

  void CallBuiltin(Builtins::Name builtin);
  void TailCallBuiltin(Builtins::Name builtin);
  void CallRuntime(Runtime::FunctionId function, int nargs);

  void LoadTaggedPointerField(Register output, Register source, int offset);
  void LoadTaggedSignedField(Register output, Register source, int offset);
  void LoadTaggedAnyField(Register output, Register source, int offset);
  void LoadByteField(Register output, Register source, int offset);
  void StoreTaggedSignedField(Register target, int offset, Smi value);
  void StoreTaggedFieldWithWriteBarrier(Register target, int offset,
                                        Register value);
  void StoreTaggedFieldNoWriteBarrier(Register target, int offset,
                                      Register value);
  void LoadFixedArrayElement(Register output, Register array, int32_t index);
  void LoadPrototype(Register prototype, Register object);

  // Loads the feedback cell from the function, and sets flags on add so that
  // we can compare afterward.
  void AddToInterruptBudget(int32_t weight);
  void AddToInterruptBudget(Register weight);

  void AddSmi(Register lhs, Smi rhs);
  void AddSmi(Register lhs, Register rhs);
  void SubSmi(Register lhs, Smi rhs);
  void SubSmi(Register lhs, Register rhs);
  void SubPointer(Register lhs, Immediate rhs);
  void MulSmi(Register lhs, Register rhs);
  void BitwiseOrSmi(Register lhs, Smi rhs);
  void BitwiseOrSmi(Register lhs, Register rhs);
  void BitwiseXorSmi(Register lhs, Smi rhs);
  void BitwiseXorSmi(Register lhs, Register rhs);
  void BitwiseAndSmi(Register lhs, Smi rhs);
  void BitwiseAndSmi(Register lhs, Register rhs);
  void ShiftLeftSmi(Register lhs, int32_t rhs);
  void ShiftRightSmi(Register lhs, int32_t rhs);
  void ShiftRightLogicalSmi(Register lhs, int32_t rhs);
  void BitwiseOrInt(Register lhs, int32_t immediate);
  void Decrement(Register value);

  void SmiTag(Register value);
  void SmiUntag(Register value);
  void SmiUntag(Register output, Register value);

  void LeaveFrame();

  void Switch(Register reg, int case_value_base, Label** labels,
              int num_labels);

  // Register operands.
  void LoadRegister(Register output, interpreter::Register source);
  void StoreRegister(interpreter::Register output, Register value);

  // Accumulator
  void LoadAccumulator(Register output);
  void PushAccumulator();
  void PopAccumulator();

  // Frame values
  void LoadFunction(Register output);
  void LoadContext(Register output);
  void StoreContext(Register context);

  static void EmitReturn(MacroAssembler* masm);

  MacroAssembler* masm() { return masm_; }

 private:
  MacroAssembler* masm_;
  ScratchRegisterScope* scratch_register_scope_ = nullptr;
};

class SaveAccumulatorScope final {
 public:
  explicit SaveAccumulatorScope(BaselineAssembler* assembler)
      : assembler_(assembler) {
    assembler_->Push(kInterpreterAccumulatorRegister);
  }

  ~SaveAccumulatorScope() { assembler_->Pop(kInterpreterAccumulatorRegister); }

 private:
  BaselineAssembler* assembler_;
};

class BaselineCompiler {
 public:
  explicit BaselineCompiler(Isolate* isolate, int formal_parameter_count,
                            Handle<BytecodeArray> bytecode);

  void GenerateCode();
  Handle<Code> Build(Isolate* isolate);

 private:
  void Prologue();
  void PrologueFillFrame();
  void PrologueHandleOptimizationState(Register feedback_vector);

  void PreVisitSingleBytecode();
  void VisitSingleBytecode();

  // Register operands.
  interpreter::Register RegisterOperand(int operand_index);
  void LoadRegister(Register output, int operand_index);
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
  Label* NewLabel();
  Label* BuildForwardJumpLabel();
  void UpdateInterruptBudgetAndJumpToLabel(int weight, Label* label,
                                           Label* skip_interrupt_label);
  void UpdateInterruptBudgetAndDoInterpreterJump();
  void UpdateInterruptBudgetAndDoInterpreterJumpIfRoot(RootIndex root);
  void UpdateInterruptBudgetAndDoInterpreterJumpIfNotRoot(RootIndex root);

  // Feedback vector.
  MemOperand FeedbackVector();
  void LoadFeedbackVector(Register output);
  void LoadClosureFeedbackArray(Register output, Register closure);

  // Position mapping.
  void AddPosition();

  // Misc. helpers.

  // Select the root boolean constant based on the jump in the given
  // `jump_func` -- the function should jump to the given label if we want to
  // select "true", otherwise it should fall through.
  void SelectBooleanConstant(
      Register output, std::function<void(Label*, Label::Distance)> jump_func);

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
  void BuildCompare(Builtins::Name builtin_name);
  void BuildBinopWithConstant(Builtins::Name builtin_name);

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
  MacroAssembler masm_;
  BaselineAssembler basm_;
  interpreter::BytecodeArrayIterator iterator_;
  BytecodeOffsetTableBuilder bytecode_offset_table_builder_;
  Zone zone_;

  // TODO(leszeks): Consider using a sorted vector or similar, instead of a map.
  ZoneMap<int, ZoneVector<Label*>> linked_labels_;
  ZoneMap<int, Label*> unlinked_labels_;
  ZoneSet<int> handler_offsets_;
};

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_BASELINE_H_

//