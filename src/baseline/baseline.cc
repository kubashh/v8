// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/baseline.h"

#include <type_traits>
#include <unordered_map>

#include "src/builtins/builtins-constructor.h"
#include "src/builtins/builtins-descriptors.h"
#include "src/codegen/assembler.h"
#include "src/codegen/interface-descriptors.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/x64/assembler-x64.h"
#include "src/codegen/x64/register-x64.h"
#include "src/common/globals.h"
#include "src/interpreter/bytecode-array-accessor.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-flags.h"
#include "src/objects/code.h"
#include "src/objects/heap-object.h"
#include "src/objects/instance-type.h"
#include "src/objects/object-list-macros.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

class BaselineAssembler : public MacroAssembler {
 public:
  using MacroAssembler::MacroAssembler;

  Operand RegisterFrameOperand(int register_index) {
    return Operand(rbp, register_index * kSystemPointerSize);
  }
  Operand RegisterFrameOperand(interpreter::Register interpreter_register) {
    return RegisterFrameOperand(interpreter_register.ToOperand());
  }
  Operand ContextOperand() {
    return RegisterFrameOperand(interpreter::Register::current_context());
  }
  Operand FunctionOperand() {
    return RegisterFrameOperand(interpreter::Register::function_closure());
  }

  void LoadPrototype(Register prototype, Register object) {
    LoadMap(prototype, object);
    LoadTaggedPointerField(prototype,
                           FieldOperand(prototype, Map::kPrototypeOffset));
  }
  void LoadMapBitField(Register bitfield, Register map) {
    LoadAnyTaggedField(bitfield, FieldOperand(map, Map::kBitFieldOffset));
  }
  void AddSmi(Register lhs, int32_t rhs) {
    Move(kScratchRegister, Smi::FromInt(rhs));
    addq(lhs, kScratchRegister);
  }
  void SubSmi(Register lhs, int32_t rhs) {
    Move(kScratchRegister, Smi::FromInt(rhs));
    subq(lhs, kScratchRegister);
  }
  void BitwiseOrSmi(Register lhs, int32_t rhs) {
    Move(kScratchRegister, Smi::FromInt(rhs));
    orq(lhs, kScratchRegister);
  }
  void BitwiseXorSmi(Register lhs, int32_t rhs) {
    Move(kScratchRegister, Smi::FromInt(rhs));
    xorq(lhs, kScratchRegister);
  }
  void BitwiseAndSmi(Register lhs, int32_t rhs) {
    Move(kScratchRegister, Smi::FromInt(rhs));
    andq(lhs, kScratchRegister);
  }
  void ShiftLeftSmi(Register lhs, int32_t rhs) { shlq(lhs, Immediate(rhs)); }
  void ShiftRightSmi(Register lhs, int32_t rhs) { sarq(lhs, Immediate(rhs)); }
  void ShiftRightLogicalSmi(Register lhs, int32_t rhs) {
    shrq(lhs, Immediate(rhs));
  }
  void LeaveFrame() {
    movq(rsp, rbp);
    popq(rbp);
  }

  void Switch(Register reg, int case_value_base, Label** labels,
              int num_labels) {
    Label fallthrough, jump_table;
    if (case_value_base > 0) {
      subq(reg, Immediate(case_value_base));
    }
    cmpq(reg, Immediate(num_labels));
    j(above, &fallthrough);
    leaq(kScratchRegister, Operand(&jump_table));
    jmp(Operand(kScratchRegister, reg, times_8, 0));
    // Emit the jump table inline, under the assumption that it's not too big.
    Align(kSystemPointerSize);
    bind(&jump_table);
    for (int i = 0; i < num_labels; ++i) {
      dq(labels[i]);
    }
    bind(&fallthrough);
  }
  void Compare(Register lhs, Operand rhs) { cmpq(lhs, rhs); }
};

#define __ masm_.

namespace {

#ifdef DEBUG
bool Clobbers(Register target, Register reg) { return target == reg; }
bool Clobbers(Register target, Operand op) {
  return op.AddressUsesRegister(target);
}
bool Clobbers(Register target, Handle<Object> handle) { return false; }
bool Clobbers(Register target, Smi smi) { return false; }
bool Clobbers(Register target, Immediate imm) { return false; }
#endif

template <typename... Args>
struct ArgumentSettingHelper;

template <>
struct ArgumentSettingHelper<> {
  static void Set(BaselineAssembler* masm, CallInterfaceDescriptor descriptor,
                  int i) {}
  static void CheckSettingDoesntClobber(Register target) {}
};

template <typename Arg, typename... Args>
struct ArgumentSettingHelper<Arg, Args...> {
  static void Set(BaselineAssembler* masm, CallInterfaceDescriptor descriptor,
                  int i, Arg arg, Args... args) {
    if (i < descriptor.GetRegisterParameterCount()) {
      Register target = descriptor.GetRegisterParameter(i);
      ArgumentSettingHelper<Args...>::CheckSettingDoesntClobber(target,
                                                                args...);
      masm->Move(target, arg);
      ArgumentSettingHelper<Args...>::Set(masm, descriptor, i + 1, args...);
    } else if (descriptor.GetStackArgumentOrder() ==
               StackArgumentOrder::kDefault) {
      masm->Push(arg);
      ArgumentSettingHelper<Args...>::Set(masm, descriptor, i + 1, args...);
    } else {
      ArgumentSettingHelper<Args...>::Set(masm, descriptor, i + 1, args...);
      masm->Push(arg);
    }
  }
  static void CheckSettingDoesntClobber(Register target, Arg arg,
                                        Args... args) {
    DCHECK(!Clobbers(target, arg));
    ArgumentSettingHelper<Args...>::CheckSettingDoesntClobber(target, args...);
  }
};

template <typename... Args>
void MoveArgumentsForDescriptor(BaselineAssembler* masm,
                                CallInterfaceDescriptor descriptor,
                                Args... args) {
  ArgumentSettingHelper<Args...>::Set(masm, descriptor, 0, args...);
}

}  // namespace

class BaselineCompiler {
 public:
  explicit BaselineCompiler(Isolate* isolate, Handle<SharedFunctionInfo> shared)
      : isolate_(isolate),
        shared_(shared),
        bytecode_(shared->GetBytecodeArray(), isolate),
        masm_(isolate, CodeObjectRequired::kNo),
        iterator_(bytecode_) {}

  void PreVisitSingleBytecode() {
    if (accessor().current_bytecode() == interpreter::Bytecode::kJumpLoop) {
      unlinked_labels_[accessor().GetJumpTargetOffset()] = Label();
    }
  }

  class SaveAccumulatorScope final {
   public:
    explicit SaveAccumulatorScope(BaselineCompiler* compiler)
        : compiler_(compiler) {
      compiler_->PushAccumulator();
    }

    ~SaveAccumulatorScope() { compiler_->PopAccumulator(); }

   private:
    BaselineCompiler* compiler_;
  };

  void
  Prologue() {
    Register closure = rdi;

    // Load the feedback vector from the closure.
    Register feedback_vector = rbx;
    __ LoadTaggedPointerField(
        feedback_vector,
        FieldOperand(closure, JSFunction::kFeedbackCellOffset));
    __ LoadTaggedPointerField(
        feedback_vector, FieldOperand(feedback_vector, Cell::kValueOffset));

    // Read off the optimization state in the feedback vector.
    Register optimization_state = rcx;
    __ movl(optimization_state,
            FieldOperand(feedback_vector, FeedbackVector::kFlagsOffset));

    // Check if there is optimized code or a optimization marker that needs to
    // be processed.
    __ testl(
        optimization_state,
        Immediate(
            FeedbackVector::kHasOptimizedCodeOrCompileOptimizedMarkerMask));
    optimization_state_ = optimization_state;
    feedback_vector_ = feedback_vector;
    __ j(not_zero, &maybe_has_optimized_code_);

    FrameScope frame_scope(&masm_, StackFrame::MANUAL);
    __ pushq(rbp);  // Caller's frame pointer.
    __ movq(rbp, rsp);
    __ Push(kContextRegister);                 // Callee's context.
    __ Push(kJavaScriptCallTargetRegister);    // Callee's JS function.
    __ Push(kJavaScriptCallArgCountRegister);  // Actual argument count.

    // Stack check. This folds the checks for both the interrupt stack limit
    // check and the real stack limit into one by just checking for the
    // interrupt limit. The interrupt limit is either equal to the real stack
    // limit or tighter. By ensuring we have space until that limit after
    // building the frame we can quickly precheck both at once.
    __ movq(kScratchRegister, rsp);
    __ subq(kScratchRegister,
            Immediate(shared_->GetBytecodeArray().frame_size()));
    __ cmpq(kScratchRegister,
            __ StackLimitAsOperand(StackLimitKind::kInterruptStackLimit));
    __ j(below, &stack_check_);

    // Inlined register frame fill
    __ LoadRoot(kScratchRegister, RootIndex::kUndefinedValue);
    for (int i = 0; i < shared_->GetBytecodeArray().frame_size(); ++i) {
      __ Push(kScratchRegister);
    }
  }

  void VisitBytecodes() {
    for (; !iterator_.done(); iterator_.Advance()) {
      PreVisitSingleBytecode();
    }
    iterator_.Reset();
    for (; !iterator_.done(); iterator_.Advance()) {
      VisitSingleBytecode();
    }
  }

  void Epilogue() {
    __ bind(&maybe_has_optimized_code_);
    Register optimized_code_entry = optimization_state_;
    __ LoadAnyTaggedField(
        optimized_code_entry,
        FieldOperand(feedback_vector_,
                     FeedbackVector::kMaybeOptimizedCodeOffset));
    // TailCallOptimizedCodeSlot(masm_, optimized_code_entry, r11, r15);
    __ Trap();

    __ bind(&stack_check_);
    __ Trap();
  }

  Handle<Code> Build(Isolate* isolate) {
    CodeDesc desc;
    __ GetCode(isolate, &desc);
    return Factory::CodeBuilder(isolate, desc, CodeKind::SPARKPLUG).Build();
  }
  Operand RegisterOperand(interpreter::Register reg) {
    return __ RegisterFrameOperand(reg);
  }
  Operand RegisterOperand(int operand_index) {
    return RegisterOperand(accessor().GetRegisterOperand(operand_index));
  }
  Operand UndefinedOperand() {
    return Operand(kRootRegister, __ RootRegisterOffsetForRootIndex(
                                      RootIndex::kUndefinedValue));
  }

  void LoadRegister(Register output, interpreter::Register source) {
    __ movq(output, RegisterOperand(source));
  }
  void LoadRegister(Register output, int operand_index) {
    LoadRegister(output, accessor().GetRegisterOperand(operand_index));
  }
  void PushRegister(int operand_index) {
    __ Push(RegisterOperand(operand_index));
  }
  void StoreRegister(int operand_index, Register value) {
    __ movq(RegisterOperand(operand_index), value);
  }
  template <typename Type>
  Handle<Type> Constant(int operand_index) {
    return Handle<Type>::cast(
        accessor().GetConstantForIndexOperand(operand_index, isolate_));
  }
  Smi ConstantSmi(int operand_index) {
    return accessor().GetConstantAtIndexAsSmi(operand_index);
  }
  template <typename Type>
  void LoadConstant(Register output, int operand_index) {
    __ Move(output, Constant<Type>(operand_index));
  }
  template <typename Type>
  void PushConstant(int operand_index) {
    __ Push(Constant<Type>(operand_index));
  }
  void LoadAccumulator(Register output) {
    __ movq(output, kInterpreterAccumulatorRegister);
  }
  void PushAccumulator() { __ Push(kInterpreterAccumulatorRegister); }
  void PopAccumulator() { __ Pop(kInterpreterAccumulatorRegister); }
  void LoadContext(Register output) { __ movq(output, __ ContextOperand()); }
  void LoadFunction(Register output) { __ movq(output, __ FunctionOperand()); }
  void StoreContext(Register context) { __ movq(__ ContextOperand(), context); }
  uint32_t Uint(int operand_index) {
    return accessor().GetUnsignedImmediateOperand(operand_index);
  }
  int32_t Int(int operand_index) {
    return accessor().GetImmediateOperand(operand_index);
  }
  uint32_t Index(int operand_index) {
    return accessor().GetIndexOperand(operand_index);
  }
  uint32_t Flag(int operand_index) {
    return accessor().GetFlagOperand(operand_index);
  }
  Smi IndexAsSmi(int operand_index) {
    return Smi::FromInt(Index(operand_index));
  }
  Smi IntAsSmi(int operand_index) { return Smi::FromInt(Int(operand_index)); }
  void LoadIndexAsSmi(Register output, int operand_index) {
    __ Move(output, IndexAsSmi(operand_index));
  }
  Smi FlagAsSmi(int operand_index) { return Smi::FromInt(Flag(operand_index)); }
  void PushIndexAsSmi(int operand_index) { __ Push(IndexAsSmi(operand_index)); }
  void PushIntAsSmi(int operand_index) { __ Push(IntAsSmi(operand_index)); }
  void LoadTaggedPointerField(Register output, Register source, int offset) {
    __ LoadTaggedPointerField(output, FieldOperand(source, offset));
  }
  void LoadTaggedAnyField(Register output, Register source, int offset) {
    __ LoadAnyTaggedField(output, FieldOperand(source, offset));
  }
  void StoreTaggedField(Register target, int offset, Register value) {
    __ StoreTaggedField(FieldOperand(target, offset), value);
  }
  void LoadFeedbackVector(Register output) {
    LoadFunction(output);
    // Loads the feedback vector cell.
    // TODO(verwaest): Remove this indirection by doing a map-check on the
    // JSFunction::kFeedback entry instead.
    LoadTaggedPointerField(output, output, JSFunction::kFeedbackCellOffset);
    LoadTaggedPointerField(output, output, FeedbackCell::kValueOffset);
  }
  void LoadClosureFeedbackArray(Register output, Register closure) {
    LoadTaggedPointerField(output, closure, JSFunction::kFeedbackCellOffset);
    LoadTaggedPointerField(output, output, FeedbackCell::kValueOffset);
    Label done;
    __ CmpInstanceType(output, CLOSURE_FEEDBACK_CELL_ARRAY_TYPE);
    __ j(equal, &done);
    LoadTaggedPointerField(output, output,
                           FeedbackVector::kClosureFeedbackCellArrayOffset);
    __ bind(&done);
  }
  void LoadFixedArrayElement(Register output, Register array, int32_t index) {
    LoadTaggedAnyField(output, array,
                       FixedArray::kHeaderSize + index * kTaggedSize);
  }
  void SelectBooleanConstant(Condition condition, Register output) {
    Label done, set_true;
    __ j(condition, &set_true, Label::kNear);
    __ LoadRoot(output, RootIndex::kFalseValue);
    __ jmp(&done, Label::kNear);
    __ bind(&set_true);
    __ LoadRoot(output, RootIndex::kTrueValue);
    __ bind(&done);
  }

 private:
  void VisitSingleBytecode() {
    // Bind labels for this offset that have already been linked to a
    // jump (i.e. forward jumps, excluding jump tables).
    auto linked_labels_for_offset =
        linked_labels_.find(accessor().current_offset());
    if (linked_labels_for_offset != linked_labels_.end()) {
      for (auto&& label : linked_labels_for_offset->second) {
        __ bind(&label);
      }
      // Since the labels are linked, we can discard them.
      linked_labels_.erase(linked_labels_for_offset);
    }
    // Iterate over labels for this offset that have already not yet been linked
    // to a jump (i.e. backward jumps and jump table entries).
    auto unlinked_labels_for_offset =
        unlinked_labels_.find(accessor().current_offset());
    if (unlinked_labels_for_offset != unlinked_labels_.end()) {
      __ bind(&unlinked_labels_for_offset->second);
    }

    switch (accessor().current_bytecode()) {
#define BYTECODE_CASE(name, ...)       \
  case interpreter::Bytecode::k##name: \
    __ RecordComment(#name);           \
    return Visit##name();
      BYTECODE_LIST(BYTECODE_CASE)
#undef BYTECODE_CASE
    }
  }

#define DECLARE_VISITOR(name, ...) void Visit##name();
  BYTECODE_LIST(DECLARE_VISITOR)
#undef DECLARE_VISITOR

  void UpdateInterruptBudgetAndJumpToLabel(int weight, Label* label,
                                           Label* skip_interrupt_label) {
    LoadFunction(kScratchRegister);
    LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                           JSFunction::kFeedbackCellOffset);

    __ addq(
        FieldOperand(kScratchRegister, FeedbackCell::kInterruptBudgetOffset),
        Immediate(weight));

    if (weight < 0) {
      // Use compare flags set by add
      // TODO(leszeks): This might be trickier cross-arch.
      __ j(greater_equal, skip_interrupt_label);
      CallRuntime(Runtime::kBytecodeBudgetInterruptFromBytecode,
                  __ FunctionOperand());
    }
    __ j(always, label);
  }

  void UpdateInterruptBudgetAndDoInterpreterJump() {
    int weight = accessor().GetRelativeJumpTargetOffset();
    UpdateInterruptBudgetAndJumpToLabel(weight, BuildForwardJumpLabel(),
                                        nullptr);
  }

  void UpdateInterruptBudgetAndDoInterpreterJumpIfRoot(RootIndex root) {
    Label dont_jump;
    __ JumpIfNotRoot(kInterpreterAccumulatorRegister, root, &dont_jump,
                     Label::kNear);
    UpdateInterruptBudgetAndDoInterpreterJump();
    __ bind(&dont_jump);
  }

  void UpdateInterruptBudgetAndDoInterpreterJumpIfNotRoot(RootIndex root) {
    Label dont_jump;
    __ JumpIfRoot(kInterpreterAccumulatorRegister, root, &dont_jump,
                  Label::kNear);
    UpdateInterruptBudgetAndDoInterpreterJump();
    __ bind(&dont_jump);
  }

  Label* BuildForwardJumpLabel() {
    int target_offset = accessor().GetJumpTargetOffset();
    std::vector<Label>& labels = linked_labels_[target_offset];
    labels.emplace_back();
    return &labels.back();
  }

  template <typename... Args>
  void CallBuiltin(Builtins::Name builtin, Args... args) {
    CallInterfaceDescriptor descriptor =
        Builtins::CallInterfaceDescriptorFor(builtin);
    DCHECK_EQ(descriptor.GetParameterCount(), sizeof...(args));
    MoveArgumentsForDescriptor(&masm_, descriptor, args...);
    if (descriptor.HasContextParameter()) {
      LoadContext(descriptor.ContextRegister());
    }
    __ CallBuiltin(builtin);
  }

  template <typename... Args>
  void CallRuntime(Runtime::FunctionId functionid, Args... args) {
    LoadContext(kContextRegister);
    ITERATE_PACK(__ Push(args));
    __ CallRuntime(functionid);
  }

  // Returns into kInterpreterAccumulatorRegister
  void JumpIfToBoolean(bool do_jump_if_true, Register reg, Label* label,
                       Label::Distance distance = Label::kFar) {
    Label if_notsmi, if_notoddball;
    Label end;

    // Fast path for Smi.
    __ JumpIfNotSmi(reg, &if_notsmi);
    {
      __ Cmp(reg, Smi::FromInt(0));
      __ j(do_jump_if_true ? not_equal : equal, label, distance);
      __ j(always, &end, Label::kNear);
    }

    __ bind(&if_notsmi);
    __ LoadMap(kScratchRegister, reg);

    // Fast path for undetectable maps.
    __ testb(FieldOperand(kScratchRegister, Map::kBitFieldOffset),
             Immediate(Map::Bits1::IsUndetectableBit::kMask));
    if (do_jump_if_true) {
      __ j(not_zero, &end, Label::kNear);
    } else {
      __ j(not_zero, label, distance);
    }

    // Fast path for Oddballs.
    __ CmpInstanceType(kScratchRegister, ODDBALL_TYPE);
    __ j(not_equal, &if_notoddball);
    // Only the true value oddball ToBooleans to true.
    // TODO(leszeks): DCHECK this.
    // #define CHECK_ONLY_TRUE_IS_TOBOOLEAN_TRUE(Name, name)      \
//   DCHECK_EQ(                                               \
//       isolate_->factory()->name()->BooleanValue(isolate_), \
//       *isolate_->factory()->name() ==
    //       *isolate_->factory()->true_value());
    //     ODDBALL_LIST(CHECK_ONLY_TRUE_IS_TOBOOLEAN_TRUE)
    // #undef CHECK_ONLY_TRUE_IS_TOBOOLEAN_TRUE

    if (do_jump_if_true) {
      __ JumpIfRoot(reg, RootIndex::kTrueValue, label, distance);
    } else {
      __ JumpIfNotRoot(reg, RootIndex::kTrueValue, label, distance);
    }
    __ j(always, &end, Label::kNear);

    // Call builtin for heap numbers and bigints
    Label call_builtin;
    __ bind(&if_notoddball);
    __ CmpInstanceType(kScratchRegister, HEAP_NUMBER_TYPE);
    __ j(equal, &call_builtin);
    __ CmpInstanceType(kScratchRegister, BIG_INT_BASE_TYPE);
    __ j(equal, &call_builtin);

    // Everything else is true.
    if (do_jump_if_true) {
      __ j(not_zero, label, distance);
    } else {
      __ j(not_zero, &end, Label::kNear);
    }

    __ bind(&call_builtin);
    {
      SaveAccumulatorScope accumulator_scope(this);
      CallBuiltin(Builtins::kToBoolean, reg);
      __ Move(kScratchRegister, kInterpreterAccumulatorRegister);
    }
    if (do_jump_if_true) {
      __ JumpIfRoot(kScratchRegister, RootIndex::kTrueValue, label, distance);
    } else {
      __ JumpIfNotRoot(kScratchRegister, RootIndex::kTrueValue, label,
                       distance);
    }

    __ bind(&end);
  }

  void BuildBinop(Builtins::Name builtin_name);
  void BuildUnop(Builtins::Name builtin_name);
  void BuildCompare(Builtins::Name builtin_name, Condition condition);
  void BuildBinopWithSmi(Builtins::Name builtin_name, bool fast_path,
                         bool check_overflow,
                         std::function<void(Register, int32_t)> instruction);
  void Typeof(Register type_string, Register value);
  void UpdateFeedback(Register feedback_vector, int operand_index, int bit,
                      Label* done);

  void PushCallArgs(Operand first_argument, uint32_t arg_count);
  template <typename... Args>
  void BuildCall(ConvertReceiverMode mode, uint32_t slot, uint32_t arg_count,
                 Args... args);

  const interpreter::BytecodeArrayAccessor& accessor() { return iterator_; }

  Isolate* isolate_;
  Handle<SharedFunctionInfo> shared_;
  Handle<BytecodeArray> bytecode_;
  BaselineAssembler masm_;
  interpreter::BytecodeArrayIterator iterator_;

  std::unordered_map<int, std::vector<Label>> linked_labels_;
  std::unordered_map<int, Label> unlinked_labels_;

  // Epilogue stuff.
  Label maybe_has_optimized_code_;
  Label stack_check_;
  Register optimization_state_ = Register::no_reg();
  Register feedback_vector_ = Register::no_reg();
};

void BaselineCompiler::VisitLdaZero() {
  __ Move(kInterpreterAccumulatorRegister, Smi::FromInt(0));
}
void BaselineCompiler::VisitLdaSmi() {
  Smi constant = Smi::FromInt(accessor().GetImmediateOperand(0));
  __ Move(kInterpreterAccumulatorRegister, constant);
}
void BaselineCompiler::VisitLdaUndefined() {
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
}
void BaselineCompiler::VisitLdaNull() {
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kNullValue);
}
void BaselineCompiler::VisitLdaTheHole() {
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTheHoleValue);
}
void BaselineCompiler::VisitLdaTrue() {
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
}
void BaselineCompiler::VisitLdaFalse() {
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
}
void BaselineCompiler::VisitLdaConstant() {
  LoadConstant<HeapObject>(kInterpreterAccumulatorRegister, 0);
}
void BaselineCompiler::VisitLdaGlobal() {
  LoadFeedbackVector(LoadGlobalWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kLoadGlobalIC,
              Constant<Name>(0),                            // name
              IndexAsSmi(1),                                // slot
              LoadWithVectorDescriptor::VectorRegister());  // vector
}
void BaselineCompiler::VisitLdaGlobalInsideTypeof() {
  LoadFeedbackVector(LoadGlobalWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kLoadGlobalICInsideTypeof,
              Constant<Name>(0),                            // name
              IndexAsSmi(1),                                // slot
              LoadWithVectorDescriptor::VectorRegister());  // vector
}
void BaselineCompiler::VisitStaGlobal() {
  LoadFeedbackVector(StoreGlobalWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kStoreGlobalIC,
              Constant<Name>(0),                            // name
              kInterpreterAccumulatorRegister,              // value
              IndexAsSmi(1),                                // slot
              LoadWithVectorDescriptor::VectorRegister());  // vector
}
void BaselineCompiler::VisitPushContext() {
  LoadContext(kScratchRegister);
  StoreContext(kInterpreterAccumulatorRegister);
  StoreRegister(0, kScratchRegister);
}
void BaselineCompiler::VisitPopContext() {
  LoadRegister(kScratchRegister, 0);
  StoreContext(kScratchRegister);
}
void BaselineCompiler::VisitLdaContextSlot() {
  LoadRegister(kScratchRegister, 0);
  int depth = Uint(2);
  for (; depth > 0; --depth) {
    LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                           Context::kPreviousOffset);
  }
  LoadTaggedAnyField(kInterpreterAccumulatorRegister, kScratchRegister,
                     Context::OffsetOfElementAt(Index(1)));
}
void BaselineCompiler::VisitLdaImmutableContextSlot() { VisitLdaContextSlot(); }
void BaselineCompiler::VisitLdaCurrentContextSlot() {
  LoadContext(kScratchRegister);
  LoadTaggedAnyField(kInterpreterAccumulatorRegister, kScratchRegister,
                     Context::OffsetOfElementAt(Index(0)));
}
void BaselineCompiler::VisitLdaImmutableCurrentContextSlot() {
  VisitLdaCurrentContextSlot();
}
void BaselineCompiler::VisitStaContextSlot() {
  LoadRegister(kScratchRegister, 0);
  int depth = Uint(2);
  for (; depth > 0; --depth) {
    LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                           Context::kPreviousOffset);
  }
  StoreTaggedField(kScratchRegister,
                   Context::OffsetOfElementAt(accessor().GetIndexOperand(1)),
                   kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitStaCurrentContextSlot() {
  LoadContext(kScratchRegister);
  StoreTaggedField(kScratchRegister, Context::OffsetOfElementAt(Index(0)),
                   kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitLdaLookupSlot() {
  CallRuntime(Runtime::kLoadLookupSlot, Constant<Name>(0));
}
void BaselineCompiler::VisitLdaLookupContextSlot() {
  // TODO(verwaest): Add fast path if there are no extensions.
  VisitLdaLookupSlot();
}
void BaselineCompiler::VisitLdaLookupGlobalSlot() {
  // TODO(verwaest): Add fast path if there are no extensions.
  VisitLdaLookupSlot();
}
void BaselineCompiler::VisitLdaLookupSlotInsideTypeof() {
  CallRuntime(Runtime::kLoadLookupSlotInsideTypeof, Constant<Name>(0));
}
void BaselineCompiler::VisitLdaLookupContextSlotInsideTypeof() {
  // TODO(verwaest): Add fast path if there are no extensions.
  VisitLdaLookupSlotInsideTypeof();
}
void BaselineCompiler::VisitLdaLookupGlobalSlotInsideTypeof() {
  // TODO(verwaest): Add fast path if there are no extensions.
  VisitLdaLookupSlotInsideTypeof();
}
void BaselineCompiler::VisitStaLookupSlot() {
  uint32_t flags = Uint(1);
  Runtime::FunctionId function_id;
  if (flags & interpreter::StoreLookupSlotFlags::LanguageModeBit::kMask) {
    function_id = Runtime::kStoreLookupSlot_Strict;
  } else if (flags &
             interpreter::StoreLookupSlotFlags::LookupHoistingModeBit::kMask) {
    function_id = Runtime::kStoreLookupSlot_SloppyHoisting;
  } else {
    function_id = Runtime::kStoreLookupSlot_Sloppy;
  }
  CallRuntime(function_id, Constant<Name>(0),    // name
              kInterpreterAccumulatorRegister);  // value
}
void BaselineCompiler::VisitLdar() {
  LoadRegister(kInterpreterAccumulatorRegister, 0);
}
void BaselineCompiler::VisitStar() {
  StoreRegister(0, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitMov() {
  LoadRegister(kScratchRegister, 0);
  StoreRegister(1, kScratchRegister);
}
void BaselineCompiler::VisitLdaNamedProperty() {
  LoadFeedbackVector(LoadWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kLoadIC, RegisterOperand(0),        // object
              Constant<Name>(1),                            // name
              IndexAsSmi(2),                                // slot
              LoadWithVectorDescriptor::VectorRegister());  // vector
}
void BaselineCompiler::VisitLdaNamedPropertyNoFeedback() {
  CallBuiltin(Builtins::kGetProperty, RegisterOperand(0), Constant<Name>(1));
}
void BaselineCompiler::VisitLdaNamedPropertyFromSuper() {
  LoadFeedbackVector(LoadWithVectorDescriptor::VectorRegister());

  __ LoadPrototype(
      LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister(),
      kInterpreterAccumulatorRegister);

  CallBuiltin(Builtins::kLoadSuperIC, RegisterOperand(0),  // object
              LoadWithReceiverAndVectorDescriptor::
                  LookupStartObjectRegister(),              // lookup start
              Constant<Name>(1),                            // name
              IndexAsSmi(2),                                // slot
              LoadWithVectorDescriptor::VectorRegister());  // vector
}
void BaselineCompiler::VisitLdaKeyedProperty() {
  LoadFeedbackVector(LoadWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kKeyedLoadIC, RegisterOperand(0),   // object
              kInterpreterAccumulatorRegister,              // key
              IndexAsSmi(1),                                // slot
              LoadWithVectorDescriptor::VectorRegister());  // vector
}
void BaselineCompiler::VisitLdaModuleVariable() { __ Trap(); }
void BaselineCompiler::VisitStaModuleVariable() { __ Trap(); }
void BaselineCompiler::VisitStaNamedProperty() {
  LoadFeedbackVector(StoreWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kStoreIC,
              RegisterOperand(0),                            // object
              Constant<Name>(1),                             // name
              kInterpreterAccumulatorRegister,               // value
              IndexAsSmi(2),                                 // slot
              StoreWithVectorDescriptor::VectorRegister());  // vector
}
void BaselineCompiler::VisitStaNamedPropertyNoFeedback() {
  CallRuntime(Runtime::kSetNamedProperty,
              RegisterOperand(0),                // object
              Constant<Name>(1),                 // name
              kInterpreterAccumulatorRegister);  // value
}
void BaselineCompiler::VisitStaNamedOwnProperty() {
  // TODO(ishell): Currently we use StoreOwnIC only for storing properties that
  // already exist in the boilerplate therefore we can use StoreIC.
  VisitStaNamedProperty();
}
void BaselineCompiler::VisitStaKeyedProperty() {
  LoadFeedbackVector(StoreWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kKeyedStoreIC, RegisterOperand(0),   // object
              RegisterOperand(1),                            // key
              kInterpreterAccumulatorRegister,               // value
              IndexAsSmi(2),                                 // slot
              StoreWithVectorDescriptor::VectorRegister());  // vector
}
void BaselineCompiler::VisitStaInArrayLiteral() {
  LoadFeedbackVector(StoreWithVectorDescriptor::VectorRegister());

  CallBuiltin(Builtins::kStoreInArrayLiteralIC, RegisterOperand(0),  // object
              RegisterOperand(1),                                    // name
              kInterpreterAccumulatorRegister,                       // value
              IndexAsSmi(2),                                         // slot
              StoreWithVectorDescriptor::VectorRegister());          // vector
}
void BaselineCompiler::VisitStaDataPropertyInLiteral() {
  CallRuntime(Runtime::kDefineDataPropertyInLiteral,
              RegisterOperand(0),               // object
              RegisterOperand(1),               // name
              kInterpreterAccumulatorRegister,  // value
              IntAsSmi(2),                      // flags
              IndexAsSmi(3));                   // slot
}
void BaselineCompiler::VisitCollectTypeProfile() { __ Trap(); }
void BaselineCompiler::BuildBinop(Builtins::Name builtin_name) {
  Register feedback_vector =
      Builtins::CallInterfaceDescriptorFor(builtin_name)
          .GetRegisterParameter(
              BinaryOp_WithFeedbackDescriptor::kMaybeFeedbackVector);
  LoadFeedbackVector(feedback_vector);
  CallBuiltin(builtin_name, RegisterOperand(0),  // lhs
              kInterpreterAccumulatorRegister,   // rhs
              Immediate(Index(1)),               // slot
              feedback_vector);                  // vector
}
void BaselineCompiler::VisitAdd() { BuildBinop(Builtins::kAdd_WithFeedback); }
void BaselineCompiler::VisitSub() {
  BuildBinop(Builtins::kSubtract_WithFeedback);
}
void BaselineCompiler::VisitMul() {
  BuildBinop(Builtins::kMultiply_WithFeedback);
}
void BaselineCompiler::VisitDiv() {
  BuildBinop(Builtins::kDivide_WithFeedback);
}
void BaselineCompiler::VisitMod() {
  BuildBinop(Builtins::kModulus_WithFeedback);
}
void BaselineCompiler::VisitExp() {
  BuildBinop(Builtins::kExponentiate_WithFeedback);
}
void BaselineCompiler::VisitBitwiseOr() {
  BuildBinop(Builtins::kBitwiseOr_WithFeedback);
}
void BaselineCompiler::VisitBitwiseXor() {
  BuildBinop(Builtins::kBitwiseXor_WithFeedback);
}
void BaselineCompiler::VisitBitwiseAnd() {
  BuildBinop(Builtins::kBitwiseAnd_WithFeedback);
}
void BaselineCompiler::VisitShiftLeft() {
  BuildBinop(Builtins::kShiftLeft_WithFeedback);
}
void BaselineCompiler::VisitShiftRight() {
  BuildBinop(Builtins::kShiftRight_WithFeedback);
}
void BaselineCompiler::VisitShiftRightLogical() {
  BuildBinop(Builtins::kShiftRightLogical_WithFeedback);
}
void BaselineCompiler::UpdateFeedback(Register feedback_vector,
                                      int operand_index, int bit, Label* done) {
  // TODO(verwaest): Directly test the right bit in the feedback.
  int32_t slot_offset = FeedbackVector::kRawFeedbackSlotsOffset +
                        Index(operand_index) * kTaggedSize;
  LoadTaggedAnyField(kScratchRegister, feedback_vector, slot_offset);
  __ SmiUntag(kScratchRegister);
  __ testb(kScratchRegister, Immediate(bit));
  __ j(not_zero, done);
  __ orq(kScratchRegister, Immediate(bit));
  __ SmiTag(kScratchRegister);
  StoreTaggedField(feedback_vector, slot_offset, kScratchRegister);
  __ jmp(done);
}

void BaselineCompiler::BuildBinopWithSmi(
    Builtins::Name builtin_name, bool fast_path, bool check_overflow,
    std::function<void(Register, int32_t)> instruction) {
  Register feedback_vector =
      Builtins::CallInterfaceDescriptorFor(builtin_name)
          .GetRegisterParameter(
              BinaryOp_WithFeedbackDescriptor::kMaybeFeedbackVector);
  LoadFeedbackVector(feedback_vector);
  // Fast path for Smi.
  Label builtin, done;
  if (fast_path) {
    __ JumpIfNotSmi(kInterpreterAccumulatorRegister, &builtin);

    instruction(kInterpreterAccumulatorRegister, Int(0));
    if (check_overflow) {
      __ j(overflow, &builtin);
    }

    UpdateFeedback(feedback_vector, 1, BinaryOperationFeedback::kSignedSmall,
                   &done);

    __ bind(&builtin);
  }
  CallBuiltin(builtin_name, kInterpreterAccumulatorRegister,  // lhs
              IntAsSmi(0),                                    // rhs
              IndexAsSmi(1),                                  // slot
              feedback_vector);                               // vector
  if (fast_path) {
    __ bind(&done);
  }
}
void BaselineCompiler::VisitAddSmi() {
  BuildBinopWithSmi(Builtins::kAdd_WithFeedback, true, true,
                    [&](Register lhs, int32_t rhs) { __ AddSmi(lhs, rhs); });
}
void BaselineCompiler::VisitSubSmi() {
  BuildBinopWithSmi(Builtins::kSubtract_WithFeedback, true, true,
                    [&](Register lhs, int32_t rhs) { __ SubSmi(lhs, rhs); });
}
void BaselineCompiler::VisitMulSmi() {
  BuildBinopWithSmi(Builtins::kMultiply_WithFeedback, false, false,
                    [&](Register lhs, int32_t rhs) {});
}
void BaselineCompiler::VisitDivSmi() {
  BuildBinopWithSmi(Builtins::kDivide_WithFeedback, false, false,
                    [&](Register lhs, int32_t rhs) {});
}
void BaselineCompiler::VisitModSmi() {
  BuildBinopWithSmi(Builtins::kModulus_WithFeedback, false, false,
                    [&](Register lhs, int32_t rhs) {});
}
void BaselineCompiler::VisitExpSmi() {
  BuildBinopWithSmi(Builtins::kExponentiate_WithFeedback, false, false,
                    [&](Register lhs, int32_t rhs) {});
}
void BaselineCompiler::VisitBitwiseOrSmi() {
  BuildBinopWithSmi(
      Builtins::kBitwiseOr_WithFeedback, false, true,
      [&](Register lhs, int32_t rhs) { __ BitwiseOrSmi(lhs, rhs); });
}
void BaselineCompiler::VisitBitwiseXorSmi() {
  BuildBinopWithSmi(
      Builtins::kBitwiseXor_WithFeedback, false, true,
      [&](Register lhs, int32_t rhs) { __ BitwiseXorSmi(lhs, rhs); });
}
void BaselineCompiler::VisitBitwiseAndSmi() {
  BuildBinopWithSmi(
      Builtins::kBitwiseAnd_WithFeedback, false, true,
      [&](Register lhs, int32_t rhs) { __ BitwiseAndSmi(lhs, rhs); });
}
void BaselineCompiler::VisitShiftLeftSmi() {
  BuildBinopWithSmi(
      Builtins::kShiftLeft_WithFeedback, false, true,
      [&](Register lhs, int32_t rhs) { __ ShiftLeftSmi(lhs, rhs); });
}
void BaselineCompiler::VisitShiftRightSmi() {
  BuildBinopWithSmi(
      Builtins::kShiftRight_WithFeedback, false, true,
      [&](Register lhs, int32_t rhs) { __ ShiftRightSmi(lhs, rhs); });
}
void BaselineCompiler::VisitShiftRightLogicalSmi() {
  BuildBinopWithSmi(
      Builtins::kShiftRightLogical_WithFeedback, false, true,
      [&](Register lhs, int32_t rhs) { __ ShiftRightLogicalSmi(lhs, rhs); });
}
void BaselineCompiler::BuildUnop(Builtins::Name builtin_name) {
  Register feedback_vector =
      Builtins::CallInterfaceDescriptorFor(builtin_name)
          .GetRegisterParameter(
              UnaryOp_WithFeedbackDescriptor::kMaybeFeedbackVector);
  LoadFeedbackVector(feedback_vector);
  CallBuiltin(builtin_name,
              kInterpreterAccumulatorRegister,  // value
              Immediate(Index(0)),              // slot
              feedback_vector);                 // vector
}
void BaselineCompiler::VisitInc() {
  BuildUnop(Builtins::kIncrement_WithFeedback);
}
void BaselineCompiler::VisitDec() {
  BuildUnop(Builtins::kDecrement_WithFeedback);
}
void BaselineCompiler::VisitNegate() {
  BuildUnop(Builtins::kNegate_WithFeedback);
}
void BaselineCompiler::VisitBitwiseNot() {
  BuildUnop(Builtins::kBitwiseNot_WithFeedback);
}
void BaselineCompiler::VisitToBooleanLogicalNot() {
  Label done, set_false;
  JumpIfToBoolean(true, kInterpreterAccumulatorRegister, &set_false,
                  Label::kNear);
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
  __ jmp(&done, Label::kNear);
  __ bind(&set_false);
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
  __ bind(&done);
}
void BaselineCompiler::VisitLogicalNot() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
  SelectBooleanConstant(not_equal, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::Typeof(Register type_string, Register value) {
  Label return_number, return_function, return_undefined, return_object,
      return_string, return_bigint, return_symbol, if_oddball, done;
  // Number
  __ JumpIfSmi(value, &return_number);

  Register map = kScratchRegister;
  __ LoadMap(map, value);
  Register heap_number_map = r11;
  __ LoadRoot(heap_number_map, RootIndex::kHeapNumberMap);
  __ cmpq(map, heap_number_map);
  __ j(equal, &return_number);

  // Load instance type.
  Register instance_type = r11;
  __ movzxwq(instance_type, FieldOperand(map, Map::kInstanceTypeOffset));
  __ cmpq(instance_type, Immediate(ODDBALL_TYPE));
  __ j(equal, &if_oddball);

  Register callable_or_undetectable_mask = r8;
  __ LoadMapBitField(callable_or_undetectable_mask, map);
  __ andq(callable_or_undetectable_mask,
          Immediate(Map::Bits1::IsCallableBit::kMask |
                    Map::Bits1::IsUndetectableBit::kMask));

  // Function
  __ cmpq(callable_or_undetectable_mask,
          Immediate(Map::Bits1::IsCallableBit::kMask));
  __ j(equal, &return_function);

  // Undefined
  __ cmpq(callable_or_undetectable_mask, Immediate(0));
  __ j(not_equal, &return_undefined);

  // Object
  __ cmpq(instance_type, Immediate(FIRST_JS_RECEIVER_TYPE));
  __ j(greater_equal, &return_object);

  // String
  __ cmpq(instance_type, Immediate(FIRST_NONSTRING_TYPE));
  __ j(less, &return_string);

  // Bigint
  __ cmpq(instance_type, Immediate(BIGINT_TYPE));
  __ j(equal, &return_bigint);

  // Symbol
  __ cmpq(instance_type, Immediate(SYMBOL_TYPE));
  __ j(equal, &return_symbol);

  // Unreachable
  __ int3();

  __ bind(&return_number);
  __ LoadRoot(type_string, RootIndex::knumber_string);
  __ jmp(&done);

  __ bind(&return_function);
  __ LoadRoot(type_string, RootIndex::kfunction_string);
  __ jmp(&done);

  __ bind(&return_undefined);
  __ LoadRoot(type_string, RootIndex::kundefined_string);
  __ jmp(&done);

  __ bind(&return_object);
  __ LoadRoot(type_string, RootIndex::kobject_string);
  __ jmp(&done);

  __ bind(&return_string);
  __ LoadRoot(type_string, RootIndex::kstring_string);
  __ jmp(&done);

  __ bind(&return_bigint);
  __ LoadRoot(type_string, RootIndex::kbigint_string);
  __ jmp(&done);

  __ bind(&return_symbol);
  __ LoadRoot(type_string, RootIndex::ksymbol_string);
  __ jmp(&done);

  __ bind(&if_oddball);
  __ LoadTaggedPointerField(type_string,
                            FieldOperand(value, Oddball::kTypeOfOffset));
  __ jmp(&done);

  __ bind(&done);

}
void BaselineCompiler::VisitTypeOf() {
  CallBuiltin(Builtins::kTypeof, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitDeletePropertyStrict() {
  CallBuiltin(Builtins::kDeleteProperty, RegisterOperand(0),
              kInterpreterAccumulatorRegister,
              Smi::FromEnum(LanguageMode::kStrict));
}
void BaselineCompiler::VisitDeletePropertySloppy() {
  CallBuiltin(Builtins::kDeleteProperty, RegisterOperand(0),
              kInterpreterAccumulatorRegister,
              Smi::FromEnum(LanguageMode::kSloppy));
}
void BaselineCompiler::VisitGetSuperConstructor() {
  __ LoadPrototype(kScratchRegister, kInterpreterAccumulatorRegister);
  StoreRegister(0, kScratchRegister);
}
void BaselineCompiler::PushCallArgs(Operand first_argument,
                                    uint32_t arg_count) {
  __ leaq(kScratchRegister, first_argument);
  for (uint32_t i = 0; i < arg_count; i++) {
    __ Push(
        Operand(kScratchRegister, (i - arg_count + 1) * kSystemPointerSize));
  }
}
template <typename... Args>
void BaselineCompiler::BuildCall(ConvertReceiverMode mode, uint32_t slot,
                                 uint32_t arg_count, Args... args) {
  Builtins::Name builtin;
  switch (mode) {
    case ConvertReceiverMode::kAny:
      builtin = Builtins::kCall_ReceiverIsAny_WithFeedback;
      break;
    case ConvertReceiverMode::kNullOrUndefined:
      builtin = Builtins::kCall_ReceiverIsNullOrUndefined_WithFeedback;
      break;
    case ConvertReceiverMode::kNotNullOrUndefined:
      builtin = Builtins::kCall_ReceiverIsNotNullOrUndefined_WithFeedback;
      break;
    default:
      UNREACHABLE();
  }
  CallInterfaceDescriptor descriptor =
      Builtins::CallInterfaceDescriptorFor(builtin);
  LoadFeedbackVector(r8);
  MoveArgumentsForDescriptor(&masm_, descriptor,
                             RegisterOperand(0),    // kFunction
                             Immediate(arg_count),  // kActualArgumentsCount
                             Immediate(slot),       // kSlot
                             r8,                    // kMaybeFeedbackVector
                             args...);              // Arguments
  if (descriptor.HasContextParameter()) {
    LoadContext(descriptor.ContextRegister());
  }
  __ CallBuiltin(builtin);
}
void BaselineCompiler::VisitCallAnyReceiver() {
  uint32_t arg_count = accessor().GetRegisterCountOperand(2);
  PushCallArgs(RegisterOperand(1), arg_count);
  BuildCall(ConvertReceiverMode::kAny, Index(3),
            arg_count - 1);  // Remove receiver.
}
void BaselineCompiler::VisitCallProperty() {
  uint32_t arg_count = accessor().GetRegisterCountOperand(2);
  PushCallArgs(RegisterOperand(1), arg_count);
  BuildCall(ConvertReceiverMode::kNullOrUndefined, Index(3),
            arg_count - 1);  // Remove receiver.
}
void BaselineCompiler::VisitCallProperty0() {
  BuildCall(ConvertReceiverMode::kNotNullOrUndefined, Index(2), 0,
            RegisterOperand(1));
}
void BaselineCompiler::VisitCallProperty1() {
  BuildCall(ConvertReceiverMode::kNotNullOrUndefined, Index(3), 1,
            RegisterOperand(1), RegisterOperand(2));
}
void BaselineCompiler::VisitCallProperty2() {
  BuildCall(ConvertReceiverMode::kNotNullOrUndefined, Index(4), 2,
            RegisterOperand(1), RegisterOperand(2), RegisterOperand(3));
}
void BaselineCompiler::VisitCallUndefinedReceiver() {
  uint32_t arg_count = accessor().GetRegisterCountOperand(2);
  PushCallArgs(RegisterOperand(1), arg_count);
  BuildCall(ConvertReceiverMode::kNullOrUndefined, Index(3), arg_count,
            UndefinedOperand());
}
void BaselineCompiler::VisitCallUndefinedReceiver0() {
  BuildCall(ConvertReceiverMode::kNullOrUndefined, Index(1), 0,
            UndefinedOperand());
}
void BaselineCompiler::VisitCallUndefinedReceiver1() {
  BuildCall(ConvertReceiverMode::kNullOrUndefined, Index(2), 1,
            UndefinedOperand(), RegisterOperand(1));
}
void BaselineCompiler::VisitCallUndefinedReceiver2() {
  BuildCall(ConvertReceiverMode::kNullOrUndefined, Index(3), 2,
            UndefinedOperand(), RegisterOperand(1), RegisterOperand(2));
}
void BaselineCompiler::VisitCallNoFeedback() {
  uint32_t arg_count = accessor().GetRegisterCountOperand(2);
  PushCallArgs(RegisterOperand(1), arg_count);
  CallInterfaceDescriptor descriptor =
      Builtins::CallInterfaceDescriptorFor(Builtins::kCall_ReceiverIsAny);
  MoveArgumentsForDescriptor(&masm_, descriptor,
                             RegisterOperand(0),    // kFunction
                             Immediate(arg_count-1));  // kActualArgumentsCount
  if (descriptor.HasContextParameter()) {
    LoadContext(descriptor.ContextRegister());
  }
  __ CallBuiltin(Builtins::kCall_ReceiverIsAny);
}
void BaselineCompiler::VisitCallWithSpread() {
  uint32_t arg_count = accessor().GetRegisterCountOperand(2);
  PushCallArgs(RegisterOperand(1),
               arg_count - 1);  // Do not push the spread argument.
  Operand spread =
      Operand(kScratchRegister, (-arg_count + 1) * kSystemPointerSize);
  CallInterfaceDescriptor descriptor = Builtins::CallInterfaceDescriptorFor(
      Builtins::kCallWithSpread_WithFeedback);
  LoadFeedbackVector(r8);
  MoveArgumentsForDescriptor(&masm_, descriptor,
                             RegisterOperand(0),        // kFunction
                             Immediate(arg_count - 2),  // kActualArgumentsCount
                             spread,                    // kSpread
                             Immediate(Index(3)),       // kSlot
                             r8);                       // kMaybeFeedbackVector
  if (descriptor.HasContextParameter()) {
    LoadContext(descriptor.ContextRegister());
  }
  __ CallBuiltin(Builtins::kCallWithSpread_WithFeedback);
}
void BaselineCompiler::VisitCallRuntime() {
  uint32_t arg_count = accessor().GetRegisterCountOperand(2);
  __ leaq(kScratchRegister, RegisterOperand(1));
  for (uint32_t i = 0; i < arg_count; i++) {
    __ Push(Operand(kScratchRegister, -i * kSystemPointerSize));
  }
  LoadContext(kContextRegister);
  __ CallRuntime(accessor().GetRuntimeIdOperand(0));
}
void BaselineCompiler::VisitCallRuntimeForPair() {
  uint32_t arg_count = accessor().GetRegisterCountOperand(2);
  __ leaq(kScratchRegister, RegisterOperand(1));
  for (uint32_t i = 0; i < arg_count; i++) {
    __ Push(Operand(kScratchRegister, -i * kSystemPointerSize));
  }
  LoadContext(kContextRegister);
  __ CallRuntime(accessor().GetRuntimeIdOperand(0));
  __ leaq(kScratchRegister, RegisterOperand(3));
  __ movq(Operand(kScratchRegister, 0), kReturnRegister0);
  __ movq(Operand(kScratchRegister, -kSystemPointerSize), kReturnRegister1);
}
void BaselineCompiler::VisitCallJSRuntime() {
  uint32_t arg_count = accessor().GetRegisterCountOperand(2);
  PushCallArgs(RegisterOperand(1), arg_count);
  __ LoadNativeContextSlot(accessor().GetNativeContextIndexOperand(0), rdi);
  LoadFeedbackVector(r8);
  CallInterfaceDescriptor descriptor = Builtins::CallInterfaceDescriptorFor(
      Builtins::kCall_ReceiverIsNullOrUndefined_WithFeedback);
  MoveArgumentsForDescriptor(&masm_, descriptor,
                             rdi,                   // kFunction
                             Immediate(arg_count),  // kActualArgumentsCount
                             Immediate(Index(arg_count + 1)),  // kSlot
                             r8,                   // kMaybeFeedbackVector
                             UndefinedOperand());  // kReceiver
  if (descriptor.HasContextParameter()) {
    LoadContext(descriptor.ContextRegister());
  }
  __ CallBuiltin(Builtins::kCall_ReceiverIsNullOrUndefined_WithFeedback);
}
void BaselineCompiler::VisitInvokeIntrinsic() {
  uint32_t arg_count = accessor().GetRegisterCountOperand(2);
  __ leaq(kScratchRegister, RegisterOperand(1));
  for (uint32_t i = 0; i < arg_count; i++) {
    __ Push(Operand(kScratchRegister, -i * kSystemPointerSize));
  }
  LoadContext(kContextRegister);
  __ CallRuntime(accessor().GetIntrinsicIdOperand(0));
}
void BaselineCompiler::VisitConstruct() {
  uint32_t arg_count = accessor().GetRegisterCountOperand(2);
  PushCallArgs(RegisterOperand(1), arg_count);
  CallInterfaceDescriptor descriptor =
      Builtins::CallInterfaceDescriptorFor(Builtins::kConstruct_WithFeedback);
  LoadFeedbackVector(r8);
  MoveArgumentsForDescriptor(&masm_, descriptor,
                             RegisterOperand(0),    // kFunction
                             RegisterOperand(0),    // kNewTarget
                             Immediate(arg_count),  // kActualArgumentsCount
                             Immediate(Index(3)),   // kSlot
                             r8,                    // kMaybeFeedbackVector
                             UndefinedOperand());   // kReceiver
  if (descriptor.HasContextParameter()) {
    LoadContext(descriptor.ContextRegister());
  }
  __ CallBuiltin(Builtins::kConstruct_WithFeedback);
}
void BaselineCompiler::VisitConstructWithSpread() {
  uint32_t arg_count = accessor().GetRegisterCountOperand(2);
  PushCallArgs(RegisterOperand(1),
               arg_count - 1);  // Do not push the spread argument.
  Operand spread =
      Operand(kScratchRegister, (-arg_count + 1) * kSystemPointerSize);
  CallInterfaceDescriptor descriptor = Builtins::CallInterfaceDescriptorFor(
      Builtins::kConstructWithSpread_WithFeedback);
  LoadFeedbackVector(r8);
  MoveArgumentsForDescriptor(&masm_, descriptor,
                             RegisterOperand(0),        // kFunction
                             RegisterOperand(0),        // kNewTarget
                             Immediate(arg_count - 1),  // kActualArgumentsCount
                             Immediate(Index(3)),       // kSlot
                             r8,                        // kMaybeFeedbackVector
                             spread,                    // kSpread
                             UndefinedOperand());       // kReceiver
  if (descriptor.HasContextParameter()) {
    LoadContext(descriptor.ContextRegister());
  }
  __ CallBuiltin(Builtins::kConstructWithSpread_WithFeedback);
}
void BaselineCompiler::BuildCompare(Builtins::Name builtin_name,
                                    Condition condition) {
  Register feedback_vector =
      Builtins::CallInterfaceDescriptorFor(builtin_name)
          .GetRegisterParameter(
              Compare_WithFeedbackDescriptor::kMaybeFeedbackVector);
  LoadFeedbackVector(feedback_vector);

  // Fast path for Smi.
  Label builtin, done;
  __ movq(kScratchRegister, RegisterOperand(0));
  __ movq(rcx, kScratchRegister);
  __ orq(rcx, kInterpreterAccumulatorRegister);
  __ JumpIfNotSmi(rcx, &builtin);

  __ cmpq(kScratchRegister, kInterpreterAccumulatorRegister);
  SelectBooleanConstant(condition, kInterpreterAccumulatorRegister);

  UpdateFeedback(feedback_vector, 1, CompareOperationFeedback::kSignedSmall,
                 &done);

  __ bind(&builtin);
  CallBuiltin(builtin_name, kScratchRegister,   // lhs
              kInterpreterAccumulatorRegister,  // rhs
              IndexAsSmi(1),                    // slot
              feedback_vector);                 // vector

  __ bind(&done);
}
void BaselineCompiler::VisitTestEqual() {
  BuildCompare(Builtins::kEqual_WithFeedback, equal);
}
void BaselineCompiler::VisitTestEqualStrict() {
  BuildCompare(Builtins::kStrictEqual_WithFeedback, equal);
}
void BaselineCompiler::VisitTestLessThan() {
  BuildCompare(Builtins::kLessThan_WithFeedback, less);
}
void BaselineCompiler::VisitTestGreaterThan() {
  BuildCompare(Builtins::kGreaterThan_WithFeedback, greater);
}
void BaselineCompiler::VisitTestLessThanOrEqual() {
  BuildCompare(Builtins::kLessThanOrEqual_WithFeedback, less_equal);
}
void BaselineCompiler::VisitTestGreaterThanOrEqual() {
  BuildCompare(Builtins::kGreaterThanOrEqual_WithFeedback, greater_equal);
}
void BaselineCompiler::VisitTestReferenceEqual() {
  __ cmpq(RegisterOperand(0), kInterpreterAccumulatorRegister);
  SelectBooleanConstant(equal, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitTestInstanceOf() { __ Trap(); }
void BaselineCompiler::VisitTestIn() { __ Trap(); }
void BaselineCompiler::VisitTestUndetectable() {
  Label done, set_false;
  __ JumpIfSmi(kInterpreterAccumulatorRegister, &set_false, Label::kNear);

  __ LoadMap(kScratchRegister, kInterpreterAccumulatorRegister);

  __ testb(FieldOperand(kScratchRegister, Map::kBitFieldOffset),
           Immediate(Map::Bits1::IsUndetectableBit::kMask));
  __ j(zero, &set_false, Label::kNear);
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
  __ jmp(&done, Label::kNear);

  __ bind(&set_false);
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
  __ bind(&done);
}
void BaselineCompiler::VisitTestNull() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kNullValue);
  SelectBooleanConstant(equal, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitTestUndefined() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  SelectBooleanConstant(equal, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitTestTypeOf() {
  uint32_t literal_flag = Flag(0);
  CallBuiltin(Builtins::kTypeof, kInterpreterAccumulatorRegister);

#define TYPEOF_FLAG_VALUE(type_name)                                          \
  static_cast<                                                                \
      std::underlying_type<interpreter::TestTypeOfFlags::LiteralFlag>::type>( \
      interpreter::TestTypeOfFlags::LiteralFlag::k##type_name)
#define TYPEOF_COMPARE(type_name)                   \
  __ CompareRoot(kInterpreterAccumulatorRegister,   \
                 RootIndex::k##type_name##_string); \
  SelectBooleanConstant(equal, kInterpreterAccumulatorRegister);

#define TYPEOF_CASE(type_upper, type_lower) \
  case TYPEOF_FLAG_VALUE(type_upper):       \
    TYPEOF_COMPARE(type_lower);             \
    break;

  switch (literal_flag) {
    default:
      __ Trap();
      break;
      TYPEOF_LITERAL_LIST(TYPEOF_CASE)
  }

#undef TYPEOF_COMPARE
#undef TYPEOF_FLAG_FALUE
#undef TYPEOF_CASE
}
void BaselineCompiler::VisitToName() {
  SaveAccumulatorScope save_accumulator(this);
  CallBuiltin(Builtins::kToName, kInterpreterAccumulatorRegister);
  StoreRegister(0, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitToNumber() {
  // TODO(leszeks): Record feedback.
  CallBuiltin(Builtins::kToNumber, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitToNumeric() {
  // TODO(leszeks): Record feedback.
  CallBuiltin(Builtins::kToNumeric, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitToObject() {
  SaveAccumulatorScope save_accumulator(this);
  CallBuiltin(Builtins::kToObject, kInterpreterAccumulatorRegister);
  StoreRegister(0, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitToString() {
  // TODO(verwaest): Add fast inline path if it's already a string.
  CallBuiltin(Builtins::kToString, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitCreateRegExpLiteral() {
  Register feedback_vector = kScratchRegister;
  LoadFeedbackVector(feedback_vector);
  CallBuiltin(Builtins::kCreateRegExpLiteral,
              feedback_vector,          // feedback vector
              IndexAsSmi(1),            // slot
              Constant<HeapObject>(0),  // pattern
              FlagAsSmi(2));            // flags
}
void BaselineCompiler::VisitCreateArrayLiteral() {
  uint32_t flags = Flag(2);
  Register feedback_vector = kScratchRegister;
  LoadFeedbackVector(feedback_vector);
  if (flags &
      interpreter::CreateArrayLiteralFlags::FastCloneSupportedBit::kMask) {
    CallBuiltin(Builtins::kCreateShallowArrayLiteral, feedback_vector,
                IndexAsSmi(1), Constant<HeapObject>(0));
  } else {
    int32_t flags_raw = static_cast<int32_t>(
        interpreter::CreateArrayLiteralFlags::FlagsBits::decode(flags));
    CallRuntime(Runtime::kCreateArrayLiteral, feedback_vector, IndexAsSmi(1),
                Constant<HeapObject>(0), Smi::FromInt(flags_raw));
  }
}
void BaselineCompiler::VisitCreateArrayFromIterable() { __ Trap(); }
void BaselineCompiler::VisitCreateEmptyArrayLiteral() {
  Register feedback_vector = kScratchRegister;
  LoadFeedbackVector(feedback_vector);
  CallBuiltin(Builtins::kCreateEmptyArrayLiteral, feedback_vector,
              IndexAsSmi(0));
}
void BaselineCompiler::VisitCreateObjectLiteral() {
  uint32_t flags = Flag(2);
  int32_t flags_raw = static_cast<int32_t>(
      interpreter::CreateObjectLiteralFlags::FlagsBits::decode(flags));
  Register feedback_vector = kScratchRegister;
  LoadFeedbackVector(feedback_vector);
  if (flags &
      interpreter::CreateObjectLiteralFlags::FastCloneSupportedBit::kMask) {
    CallBuiltin(Builtins::kCreateShallowObjectLiteral, feedback_vector,
                IndexAsSmi(1), Constant<ObjectBoilerplateDescription>(0),
                Smi::FromInt(flags_raw));
  } else {
    CallRuntime(Runtime::kCreateObjectLiteral, feedback_vector, IndexAsSmi(1),
                Constant<ObjectBoilerplateDescription>(0),
                Smi::FromInt(flags_raw));
  }
}
void BaselineCompiler::VisitCreateEmptyObjectLiteral() {
  CallBuiltin(Builtins::kCreateEmptyLiteralObject);
}
void BaselineCompiler::VisitCloneObject() {
  // TODO(verwaest): Directly use the right register if possible.
  LoadFeedbackVector(kScratchRegister);
  uint32_t flags = Flag(1);
  int32_t raw_flags =
      interpreter::CreateObjectLiteralFlags::FlagsBits::decode(flags);
  CallBuiltin(Builtins::kCloneObjectIC, RegisterOperand(0),  // source
              Smi::FromInt(raw_flags),                       // flags
              IndexAsSmi(2),                                 // slot
              kScratchRegister);                             // feedback_vector
}
void BaselineCompiler::VisitGetTemplateObject() {
  // TODO(verwaest): Directly use the right register if possible.
  LoadFeedbackVector(kInterpreterAccumulatorRegister);
  LoadFunction(kScratchRegister);
  LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                         JSFunction::kSharedFunctionInfoOffset);
  CallBuiltin(Builtins::kGetTemplateObject,
              kScratchRegister,                  // shared function info
              Constant<HeapObject>(0),           // description
              IndexAsSmi(1),                     // slot
              kInterpreterAccumulatorRegister);  // feedback_vector
}
void BaselineCompiler::VisitCreateClosure() {
  // TODO(verwaest): Use the feedback cell register expected by the builtin.
  LoadFunction(kScratchRegister);
  LoadClosureFeedbackArray(kScratchRegister, kScratchRegister);
  LoadFixedArrayElement(kScratchRegister, kScratchRegister, Index(1));
  uint32_t flags = Flag(2);
  if (flags & interpreter::CreateClosureFlags::FastNewClosureBit::kMask) {
    CallBuiltin(Builtins::kFastNewClosure, Constant<SharedFunctionInfo>(0),
                kScratchRegister);
  } else {
    Runtime::FunctionId function_id =
        (flags & interpreter::CreateClosureFlags::PretenuredBit::kMask)
            ? Runtime::kNewClosure_Tenured
            : Runtime::kNewClosure;
    CallRuntime(function_id, Constant<SharedFunctionInfo>(0), kScratchRegister);
  }
}
void BaselineCompiler::VisitCreateBlockContext() {
  CallRuntime(Runtime::kPushBlockContext, Constant<ScopeInfo>(0));
}
void BaselineCompiler::VisitCreateCatchContext() {
  CallRuntime(Runtime::kPushCatchContext, RegisterOperand(0),  // exception
              Constant<ScopeInfo>(1));
}
void BaselineCompiler::VisitCreateFunctionContext() {
  Handle<ScopeInfo> info = Constant<ScopeInfo>(0);
  uint32_t slot_count = Uint(1);
  if (slot_count < static_cast<uint32_t>(
                       ConstructorBuiltins::MaximumFunctionContextSlots())) {
    DCHECK_EQ(info->scope_type(), ScopeType::FUNCTION_SCOPE);
    CallBuiltin(Builtins::kFastNewFunctionContextFunction, info,
                Immediate(slot_count));
  } else {
    CallRuntime(Runtime::kNewFunctionContext, Constant<ScopeInfo>(0));
  }
}
void BaselineCompiler::VisitCreateEvalContext() {
  Handle<ScopeInfo> info = Constant<ScopeInfo>(0);
  uint32_t slot_count = Uint(1);
  if (slot_count < static_cast<uint32_t>(
                       ConstructorBuiltins::MaximumFunctionContextSlots())) {
    DCHECK_EQ(info->scope_type(), ScopeType::EVAL_SCOPE);
    CallBuiltin(Builtins::kFastNewFunctionContextEval, info,
                Immediate(slot_count));
  } else {
    CallRuntime(Runtime::kNewFunctionContext, Constant<ScopeInfo>(0));
  }
}
void BaselineCompiler::VisitCreateWithContext() {
  CallRuntime(Runtime::kPushWithContext, RegisterOperand(0),  // object
              Constant<ScopeInfo>(1));
}
void BaselineCompiler::VisitCreateMappedArguments() { __ Trap(); }
void BaselineCompiler::VisitCreateUnmappedArguments() { __ Trap(); }
void BaselineCompiler::VisitCreateRestParameter() { __ Trap(); }
void BaselineCompiler::VisitJumpLoop() {
  int weight = accessor().GetRelativeJumpTargetOffset();
  DCHECK_EQ(unlinked_labels_.count(accessor().GetJumpTargetOffset()), 1);
  Label* label = &unlinked_labels_[accessor().GetJumpTargetOffset()];
  // We can pass in the same label twice since it's a back edge and thus already
  // bound.
  DCHECK(label->is_bound());
  UpdateInterruptBudgetAndJumpToLabel(weight, label, label);
}
void BaselineCompiler::VisitJump() {
  UpdateInterruptBudgetAndDoInterpreterJump();
}
void BaselineCompiler::VisitJumpConstant() { VisitJump(); }
void BaselineCompiler::VisitJumpIfNullConstant() { VisitJumpIfNull(); }
void BaselineCompiler::VisitJumpIfNotNullConstant() { VisitJumpIfNotNull(); }
void BaselineCompiler::VisitJumpIfUndefinedConstant() {
  VisitJumpIfUndefined();
}
void BaselineCompiler::VisitJumpIfNotUndefinedConstant() {
  VisitJumpIfNotUndefined();
}
void BaselineCompiler::VisitJumpIfUndefinedOrNullConstant() {
  VisitJumpIfUndefinedOrNull();
}
void BaselineCompiler::VisitJumpIfTrueConstant() { VisitJumpIfTrue(); }
void BaselineCompiler::VisitJumpIfFalseConstant() { VisitJumpIfFalse(); }
void BaselineCompiler::VisitJumpIfJSReceiverConstant() {
  VisitJumpIfJSReceiver();
}
void BaselineCompiler::VisitJumpIfToBooleanTrueConstant() {
  VisitJumpIfToBooleanTrue();
}
void BaselineCompiler::VisitJumpIfToBooleanFalseConstant() {
  VisitJumpIfToBooleanFalse();
}
void BaselineCompiler::VisitJumpIfToBooleanTrue() {
  Label dont_jump;
  JumpIfToBoolean(false, kInterpreterAccumulatorRegister, &dont_jump,
                  Label::kNear);
  UpdateInterruptBudgetAndDoInterpreterJump();
  __ bind(&dont_jump);
}
void BaselineCompiler::VisitJumpIfToBooleanFalse() {
  Label dont_jump;
  JumpIfToBoolean(true, kInterpreterAccumulatorRegister, &dont_jump,
                  Label::kNear);
  UpdateInterruptBudgetAndDoInterpreterJump();
  __ bind(&dont_jump);
}
void BaselineCompiler::VisitJumpIfTrue() {
  UpdateInterruptBudgetAndDoInterpreterJumpIfRoot(RootIndex::kTrueValue);
}
void BaselineCompiler::VisitJumpIfFalse() {
  UpdateInterruptBudgetAndDoInterpreterJumpIfRoot(RootIndex::kFalseValue);
}
void BaselineCompiler::VisitJumpIfNull() {
  UpdateInterruptBudgetAndDoInterpreterJumpIfRoot(RootIndex::kNullValue);
}
void BaselineCompiler::VisitJumpIfNotNull() {
  UpdateInterruptBudgetAndDoInterpreterJumpIfNotRoot(RootIndex::kNullValue);
}
void BaselineCompiler::VisitJumpIfUndefined() {
  UpdateInterruptBudgetAndDoInterpreterJumpIfRoot(RootIndex::kUndefinedValue);
}
void BaselineCompiler::VisitJumpIfNotUndefined() {
  UpdateInterruptBudgetAndDoInterpreterJumpIfNotRoot(
      RootIndex::kUndefinedValue);
}
void BaselineCompiler::VisitJumpIfUndefinedOrNull() {
  Label do_jump, dont_jump;
  __ JumpIfRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue,
                &do_jump);
  __ JumpIfNotRoot(kInterpreterAccumulatorRegister, RootIndex::kNullValue,
                   &dont_jump, Label::kNear);
  __ bind(&do_jump);
  UpdateInterruptBudgetAndDoInterpreterJump();
  __ bind(&dont_jump);
}
void BaselineCompiler::VisitJumpIfJSReceiver() {
  UpdateInterruptBudgetAndDoInterpreterJump();
  Label not_smi, dont_jump;
  __ JumpIfSmi(kInterpreterAccumulatorRegister, &not_smi, Label::kNear);

  __ CmpInstanceType(kInterpreterAccumulatorRegister, FIRST_JS_RECEIVER_TYPE);
  __ j(less, &dont_jump);
  UpdateInterruptBudgetAndDoInterpreterJump();

  __ bind(&not_smi);
  __ bind(&dont_jump);
}
void BaselineCompiler::VisitSwitchOnSmiNoFeedback() {
  uint32_t table_length = Uint(1);
  int case_value_base = Int(2);

  std::unique_ptr<Label*[]> labels = std::make_unique<Label*[]>(table_length);
  for (const interpreter::JumpTableTargetOffset& offset :
       accessor().GetJumpTableTargetOffsets()) {
    labels[offset.case_value - case_value_base] =
        &unlinked_labels_[offset.target_offset];
  }
  __ SmiUntag(r12, kInterpreterAccumulatorRegister);
  __ Switch(r12, case_value_base, labels.get(), table_length);
}
void BaselineCompiler::VisitForInEnumerate() {
  CallBuiltin(Builtins::kForInEnumerate, RegisterOperand(0));
}
void BaselineCompiler::VisitForInPrepare() {
  LoadFeedbackVector(kScratchRegister);
  CallBuiltin(Builtins::kForInPrepare, kInterpreterAccumulatorRegister,
              IndexAsSmi(1), kScratchRegister);
  __ leaq(kScratchRegister, RegisterOperand(0));
  __ movq(Operand(kScratchRegister, 0), kReturnRegister0);
  __ movq(Operand(kScratchRegister, -kSystemPointerSize), kReturnRegister1);
  __ int3();
}
void BaselineCompiler::VisitForInContinue() {
  LoadRegister(kInterpreterAccumulatorRegister, 0);
  __ Compare(kInterpreterAccumulatorRegister, RegisterOperand(1));
  SelectBooleanConstant(not_equal, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitForInNext() {
  interpreter::Register cache_type, cache_array;
  std::tie(cache_type, cache_array) = accessor().GetRegisterPairOperand(2);
  LoadFeedbackVector(kScratchRegister);
  CallBuiltin(Builtins::kForInNext,
              Immediate(Index(3)),           // vector slot
              RegisterOperand(0),            // object
              RegisterOperand(cache_array),  // cache array
              RegisterOperand(cache_type),   // cache type
              RegisterOperand(1),            // index
              kScratchRegister);             // feedback vector
}
void BaselineCompiler::VisitForInStep() {
  LoadRegister(kInterpreterAccumulatorRegister, 0);
  __ AddSmi(kInterpreterAccumulatorRegister, 1);
}
void BaselineCompiler::VisitSetPendingMessage() {
  __ Move(kScratchRegister, ExternalReference::address_of_pending_message_obj(isolate_));
  __ movq(rcx, kInterpreterAccumulatorRegister);
  __ movq(kInterpreterAccumulatorRegister, Operand(kScratchRegister, 0));
  __ movq(Operand(kScratchRegister, 0), rcx);
}
void BaselineCompiler::VisitThrow() {
  CallRuntime(Runtime::kThrow, kInterpreterAccumulatorRegister);
  __ Trap();
}
void BaselineCompiler::VisitReThrow() {
  CallRuntime(Runtime::kReThrow, kInterpreterAccumulatorRegister);
  __ Trap();
}
void BaselineCompiler::VisitReturn() {
  const int kFirstBytecodeOffset = BytecodeArray::kHeaderSize - kHeapObjectTag;
  int profiling_weight = accessor().current_offset() - kFirstBytecodeOffset;
  Label finished_interrupt_budget_update1;
  Label finished_interrupt_budget_update2;
  UpdateInterruptBudgetAndJumpToLabel(-profiling_weight,
                                      &finished_interrupt_budget_update1,
                                      &finished_interrupt_budget_update2);
  __ bind(&finished_interrupt_budget_update1);
  __ bind(&finished_interrupt_budget_update2);

  Register argc_reg = rcx;

  LoadRegister(argc_reg, interpreter::Register::argument_count());
  __ LeaveFrame();

  int parameter_count = shared_->GetBytecodeArray().parameter_count();

  // We must pop all arguments from the stack (including the receiver). This
  // number of arguments is given by max(1 + argc_reg, parameter_count).
  int parameter_count_without_receiver =
      parameter_count - 1;  // Exclude the receiver to simplify the
                            // computation. We'll account for it at the end.
  Label mismatch_return;
  Register scratch_reg = r10;
  DCHECK_NE(argc_reg, scratch_reg);
  __ cmpq(argc_reg, Immediate(parameter_count_without_receiver));
  __ j(greater, &mismatch_return, Label::kNear);
  __ Ret(parameter_count * kSystemPointerSize, scratch_reg);
  __ bind(&mismatch_return);
  __ PopReturnAddressTo(scratch_reg);
  __ leaq(rsp, Operand(rsp, argc_reg, times_system_pointer_size,
                       kSystemPointerSize));  // Also pop the receiver.
  // We use a return instead of a jump for better return address prediction.
  __ PushReturnAddressFrom(scratch_reg);
  __ Ret();
}
void BaselineCompiler::VisitThrowReferenceErrorIfHole() {
  Label done;
  __ JumpIfNotRoot(kInterpreterAccumulatorRegister, RootIndex::kTheHoleValue,
                   &done);
  CallRuntime(Runtime::kThrowAccessedUninitializedVariable, Constant<Name>(0));
  // Unreachable.
  __ Trap();
  __ bind(&done);
}
void BaselineCompiler::VisitThrowSuperNotCalledIfHole() {
  Label done;
  __ JumpIfNotRoot(kInterpreterAccumulatorRegister, RootIndex::kTheHoleValue,
                   &done);
  CallRuntime(Runtime::kThrowSuperNotCalled);
  // Unreachable.
  __ Trap();
  __ bind(&done);
}
void BaselineCompiler::VisitThrowSuperAlreadyCalledIfNotHole() {
  Label done;
  __ JumpIfRoot(kInterpreterAccumulatorRegister, RootIndex::kTheHoleValue,
                &done);
  CallRuntime(Runtime::kThrowSuperAlreadyCalledError);
  // Unreachable.
  __ Trap();
  __ bind(&done);
}
void BaselineCompiler::VisitThrowIfNotSuperConstructor() {
  LoadRegister(kScratchRegister, 0);
  __ LoadMap(kScratchRegister, kScratchRegister);
  Label done;
  __ testb(FieldOperand(kScratchRegister, Map::kBitFieldOffset),
           Immediate(Map::Bits1::IsUndetectableBit::kMask));
  __ j(zero, &done);
  CallRuntime(Runtime::kThrowNotSuperConstructor, RegisterOperand(0),
              __ FunctionOperand());

  __ bind(&done);
}
void BaselineCompiler::VisitSwitchOnGeneratorState() { __ Trap(); }
void BaselineCompiler::VisitSuspendGenerator() { __ Trap(); }
void BaselineCompiler::VisitResumeGenerator() { __ Trap(); }

void BaselineCompiler::VisitGetIterator() {
  // TODO(marja): figure out which register the feeback vector should be in.
  Register feedback_vector = kScratchRegister;
  LoadFeedbackVector(feedback_vector);
  CallBuiltin(Builtins::kGetIteratorWithFeedback,
    RegisterOperand(0),  // receiver
    IndexAsSmi(1),  // load_slot
    IndexAsSmi(2),  // call_slot
    feedback_vector);  // vector
}

void BaselineCompiler::VisitDebugger() {
  CallBuiltin(Builtins::kHandleDebuggerStatement);
}
void BaselineCompiler::VisitIncBlockCounter() {
  CallRuntime(Runtime::kInlineIncBlockCounter, __ FunctionOperand(),
              Constant<HeapObject>(0));  // coverage array slot
}
void BaselineCompiler::VisitAbort() {
  CallRuntime(Runtime::kAbort, IndexAsSmi(0));
  __ Trap();
}
void BaselineCompiler::VisitWide() {
  // Consumed by the BytecodeArrayIterator.
  UNREACHABLE();
}
void BaselineCompiler::VisitExtraWide() {
  // Consumed by the BytecodeArrayIterator.
  UNREACHABLE();
}
void BaselineCompiler::VisitIllegal() {
  // Not emitted in valid bytecode.
  UNREACHABLE();
}
#define DEBUG_BREAK(Name, ...) \
  void BaselineCompiler::Visit##Name() { UNREACHABLE(); }
DEBUG_BREAK_BYTECODE_LIST(DEBUG_BREAK)
#undef DEBUG_BREAK

Handle<Code> CompileWithBaseline(Isolate* isolate,
                                 Handle<SharedFunctionInfo> shared) {
  BaselineCompiler compiler(isolate, shared);

  compiler.Prologue();
  compiler.VisitBytecodes();
  compiler.Epilogue();

  return compiler.Build(isolate);
}

}  // namespace internal
}  // namespace v8
