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
#include "src/codegen/machine-type.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/x64/register-x64.h"
#include "src/common/globals.h"
#include "src/interpreter/bytecode-array-accessor.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-flags.h"
#include "src/interpreter/bytecode-register.h"
#include "src/objects/code.h"
#include "src/objects/heap-object.h"
#include "src/objects/instance-type.h"
#include "src/objects/object-list-macros.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

class BytecodeOffsetTableBuilder {
 public:
  void AddPosition(size_t code_offset, size_t bytecode_offset) {
    // TODO
  }

  template <typename LocalIsolate>
  Handle<ByteArray> ToBytecodeOffsetTable(LocalIsolate* isolate) {
    if (bytes_.empty()) return isolate->factory()->empty_byte_array();
    Handle<ByteArray> table = isolate->factory()->NewByteArray(
        static_cast<int>(bytes_.size()), AllocationType::kOld);
    MemCopy(table->GetDataStartAddress(), bytes_.data(), bytes_.size());
    return table;
  }

 private:
  std::vector<byte> bytes_;
};

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
bool Clobbers(Register target, TaggedIndex index) { return false; }
bool Clobbers(Register target, Immediate imm) { return false; }

// We don't know what's inside machine registers or operands, so assume they
// match.
bool MachineTypeMatches(MachineType type, Register reg) { return true; }
bool MachineTypeMatches(MachineType type, Operand reg) { return true; }
bool MachineTypeMatches(MachineType type, Handle<HeapObject> handle) {
  return type.IsTagged() && !type.IsTaggedSigned();
}
bool MachineTypeMatches(MachineType type, Smi handle) {
  return type.IsTagged() && !type.IsTaggedPointer();
}
bool MachineTypeMatches(MachineType type, TaggedIndex handle) {
  // TaggedIndex doesn't have a separate type, so check for the same type as for
  // Smis.
  return type.IsTagged() && !type.IsTaggedPointer();
}
bool MachineTypeMatches(MachineType type, Immediate imm) {
  // 32-bit immediates can be used for 64-bit params -- they'll be
  // zero-extended.
  return type.representation() == MachineRepresentation::kWord32 ||
         type.representation() == MachineRepresentation::kWord64;
}
bool MachineTypeMatches(MachineType type, interpreter::Register reg) {
  return type.IsTagged();
}

template <typename... Args>
struct CheckArgsHelper;

template <>
struct CheckArgsHelper<> {
  static void Check(BaselineAssembler* masm, CallInterfaceDescriptor descriptor,
                    int i) {
    if (descriptor.AllowVarArgs()) {
      CHECK_GE(i, descriptor.GetParameterCount());
    } else {
      CHECK_EQ(i, descriptor.GetParameterCount());
    }
  }
};

template <typename Arg, typename... Args>
struct CheckArgsHelper<Arg, Args...> {
  static void Check(BaselineAssembler* masm, CallInterfaceDescriptor descriptor,
                    int i, Arg arg, Args... args) {
    if (i >= descriptor.GetParameterCount()) {
      CHECK(descriptor.AllowVarArgs());
      return;
    }
    CHECK(MachineTypeMatches(descriptor.GetParameterType(i), arg));
    CheckArgsHelper<Args...>::Check(masm, descriptor, i + 1, args...);
  }
};

template <typename... Args>
struct CheckArgsHelper<interpreter::RegisterList, Args...> {
  static void Check(BaselineAssembler* masm, CallInterfaceDescriptor descriptor,
                    int i, interpreter::RegisterList list, Args... args) {
    for (int reg_index = 0; reg_index < list.register_count();
         ++reg_index, ++i) {
      if (i >= descriptor.GetParameterCount()) {
        CHECK(descriptor.AllowVarArgs());
        return;
      }
      CHECK(
          MachineTypeMatches(descriptor.GetParameterType(i), list[reg_index]));
    }
    CheckArgsHelper<Args...>::Check(masm, descriptor, i, args...);
  }
};

template <typename... Args>
void CheckArgs(BaselineAssembler* masm, CallInterfaceDescriptor descriptor,
               Args... args) {
  CheckArgsHelper<Args...>::Check(masm, descriptor, 0, args...);
}

#else

template <typename... Args>
void CheckArgs(Args... args) {}

#endif

template <typename Arg>
struct PushHelper {
  static void Push(BaselineAssembler* masm, Arg arg) {
    masm->Push(arg);
  }
  static void PushReverse(BaselineAssembler* masm, Arg arg) {
    Push(masm, arg);
  }
};

template <>
struct PushHelper<interpreter::Register> {
  static void Push(BaselineAssembler* masm, interpreter::Register reg) {
    masm->Push(masm->RegisterFrameOperand(reg));
  }
  static void PushReverse(BaselineAssembler* masm, interpreter::Register reg) {
    Push(masm, reg);
  }
};

template <>
struct PushHelper<interpreter::RegisterList> {
  static void Push(BaselineAssembler* masm, interpreter::RegisterList list) {
    for (int reg_index = 0; reg_index < list.register_count(); ++reg_index) {
      masm->Push(masm->RegisterFrameOperand(list[reg_index]));
    }
  }
  static void PushReverse(BaselineAssembler* masm, interpreter::RegisterList list) {
    for (int reg_index = list.register_count() - 1; reg_index >= 0; --reg_index) {
      masm->Push(masm->RegisterFrameOperand(list[reg_index]));
    }
  }
};

template <typename... Args>
struct PushAllHelper;
template <>
struct PushAllHelper<> {
  static void Push(BaselineAssembler* masm) {}
  static void PushReverse(BaselineAssembler* masm) {}
};
template <typename Arg, typename... Args>
struct PushAllHelper<Arg, Args...> {
  static void Push(BaselineAssembler* masm, Arg arg, Args... args) {
    PushHelper<Arg>::Push(masm, arg);
    PushAllHelper<Args...>::Push(masm, args...);
  }
  static void PushReverse(BaselineAssembler* masm, Arg arg, Args... args) {
    PushAllHelper<Args...>::PushReverse(masm, args...);
    PushHelper<Arg>::PushReverse(masm, arg);
  }
};

template<typename... Args>
void PushAll(BaselineAssembler* masm, Args... args) {
  PushAllHelper<Args...>::Push(masm, args...);
}

template<typename... Args>
void PushAllReverse(BaselineAssembler* masm, Args... args) {
  PushAllHelper<Args...>::PushReverse(masm, args...);
}

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
      PushAll(masm, arg, args...);
    } else {
      PushAllReverse(masm, arg, args...);
    }
  }
  static void CheckSettingDoesntClobber(Register target, Arg arg,
                                        Args... args) {
    DCHECK(!Clobbers(target, arg));
    ArgumentSettingHelper<Args...>::CheckSettingDoesntClobber(target, args...);
  }
};

// Specialization for interpreter::Register that turns it into a RegisterFrameOperand.
template <typename... Args>
struct ArgumentSettingHelper<interpreter::Register, Args...> {
  static void Set(BaselineAssembler* masm, CallInterfaceDescriptor descriptor,
                  int i, interpreter::Register arg, Args... args) {
    ArgumentSettingHelper<Operand, Args...>::Set(
        masm, descriptor, i, masm->RegisterFrameOperand(arg), args...);
  }
  static void CheckSettingDoesntClobber(Register target,
                                        interpreter::Register arg,
                                        Args... args) {
    ArgumentSettingHelper<Args...>::CheckSettingDoesntClobber(target, args...);
  }
};

// Specialization for interpreter::RegisterList which iterates it.
template <typename... Args>
struct ArgumentSettingHelper<interpreter::RegisterList, Args...> {
  static void Set(BaselineAssembler* masm, CallInterfaceDescriptor descriptor,
                  int i, interpreter::RegisterList list, Args... args) {
    // Either all the values are in machine registers, or they're all on the
    // stack.
    // TODO(leszeks): Support splitting the register list over registers and
    // stack.
    if (i < descriptor.GetRegisterParameterCount()) {
      for (int reg_index = 0; reg_index < list.register_count(); ++reg_index, ++i) {
        Register target = descriptor.GetRegisterParameter(i);
        masm->Move(target, masm->RegisterFrameOperand(list[reg_index]));
      }
      ArgumentSettingHelper<Args...>::Set(masm, descriptor, i, args...);
    } else if (descriptor.GetStackArgumentOrder() ==
               StackArgumentOrder::kDefault) {
      PushAll(masm, list, args...);
    } else {
      PushAllReverse(masm, list, args...);
    }
  }
  static void CheckSettingDoesntClobber(Register target,
                                        interpreter::RegisterList arg,
                                        Args... args) {
    ArgumentSettingHelper<Args...>::CheckSettingDoesntClobber(target, args...);
  }
};

template <typename... Args>
void MoveArgumentsForDescriptor(BaselineAssembler* masm,
                                CallInterfaceDescriptor descriptor,
                                Args... args) {
  CheckArgs(masm, descriptor, args...);
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

    __ Push(bytecode_);
    __ Push(Smi::FromInt(BytecodeArray::kHeaderSize - kHeapObjectTag));

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
    interpreter::Register new_target_or_generator_register =
        bytecode_->incoming_new_target_or_generator_register();
    __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
    int register_count = shared_->GetBytecodeArray().register_count();
    // Magic value
    const int kLoopUnrollSize = 8;
    if (register_count < 2 * kLoopUnrollSize) {
      // If the frame is small enough, just unroll the frame fill completely.
      for (int i = 0; i < register_count; ++i) {
        if (i == new_target_or_generator_register.index()) {
          // If the bytecode array has a valid incoming new target or generator
          // object register, initialize it with incoming value which was passed
          // in rdx.
          __ Push(rdx);
        } else {
          __ Push(kInterpreterAccumulatorRegister);
        }
      }
    } else {
      // Extract the first few registers to round to the unroll size.
      int first_registers = register_count % kLoopUnrollSize;
      for (int i = 0; i < first_registers; ++i) {
        if (i == new_target_or_generator_register.index()) {
          // If the bytecode array has a valid incoming new target or generator
          // object register, initialize it with incoming value which was passed
          // in rdx.
          __ Push(rdx);
        } else {
          __ Push(kInterpreterAccumulatorRegister);
        }
      }
      __ Move(kScratchRegister, Immediate(register_count / kLoopUnrollSize));
      Label loop;
      __ bind(&loop);
      for (int i = 0; i < kLoopUnrollSize; ++i) {
        __ Push(kInterpreterAccumulatorRegister);
      }
      __ decl(kScratchRegister);
      __ j(not_zero, &loop);
      // Set the new target or generator object register manually if it wasn't
      // already pushed in the first registers.
      if (new_target_or_generator_register.is_valid() &&
          new_target_or_generator_register.index() >= first_registers) {
        StoreRegister(new_target_or_generator_register, rdx);
      }
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
    // Allocate the source position table.
    Handle<ByteArray> bytecode_offset_table =
        bytecode_offset_table_builder_.ToBytecodeOffsetTable(isolate);
    return Factory::CodeBuilder(isolate, desc, CodeKind::SPARKPLUG)
        .set_bytecode_offset_table(bytecode_offset_table)
        .Build();
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
  void StoreRegister(interpreter::Register output, Register value) {
    __ movq(__ RegisterFrameOperand(output), value);
  }
  void StoreRegister(int operand_index, Register value) {
    __ movq(RegisterOperand(operand_index), value);
  }
  void StoreRegisterPair(int operand_index, Register val0, Register val1) {
    interpreter::Register reg0, reg1;
    std::tie(reg0, reg1) = accessor().GetRegisterPairOperand(operand_index);
    __ movq(RegisterOperand(reg0), val0);
    __ movq(RegisterOperand(reg1), val1);
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
  uint32_t RegisterCount(int operand_index) {
    return accessor().GetRegisterCountOperand(operand_index);
  }
  TaggedIndex IndexAsTagged(int operand_index) {
    return TaggedIndex::FromIntptr(Index(operand_index));
  }
  Smi IndexAsSmi(int operand_index) {
    return Smi::FromInt(Index(operand_index));
  }
  Smi IntAsSmi(int operand_index) { return Smi::FromInt(Int(operand_index)); }
  Smi FlagAsSmi(int operand_index) { return Smi::FromInt(Flag(operand_index)); }
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
    MoveArgumentsForDescriptor(&masm_, descriptor, args...);
    if (descriptor.HasContextParameter()) {
      LoadContext(descriptor.ContextRegister());
    }
    __ CallBuiltin(builtin);
  }

  template <typename... Args>
  void CallRuntime(Runtime::FunctionId functionid, Args... args) {
    LoadContext(kContextRegister);
    PushAll(&masm_, args...);
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
  void UpdateFeedback(Register feedback_vector, int operand_index, int bit,
                      Label* done);

  template <typename... Args>
  void BuildCall(ConvertReceiverMode mode, uint32_t slot, uint32_t arg_count,
                 Args... args);

  const interpreter::BytecodeArrayAccessor& accessor() { return iterator_; }

  Isolate* isolate_;
  Handle<SharedFunctionInfo> shared_;
  Handle<BytecodeArray> bytecode_;
  BaselineAssembler masm_;
  interpreter::BytecodeArrayIterator iterator_;
  BytecodeOffsetTableBuilder bytecode_offset_table_builder_;

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
              IndexAsTagged(1),                             // slot
              LoadWithVectorDescriptor::VectorRegister());  // vector
}
void BaselineCompiler::VisitLdaGlobalInsideTypeof() {
  LoadFeedbackVector(LoadGlobalWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kLoadGlobalICInsideTypeof,
              Constant<Name>(0),                            // name
              IndexAsTagged(1),                             // slot
              LoadWithVectorDescriptor::VectorRegister());  // vector
}
void BaselineCompiler::VisitStaGlobal() {
  LoadFeedbackVector(StoreGlobalWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kStoreGlobalIC,
              Constant<Name>(0),                            // name
              kInterpreterAccumulatorRegister,              // value
              IndexAsTagged(1),                             // slot
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
              IndexAsTagged(2),                             // slot
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
              IndexAsTagged(2),                             // slot
              LoadWithVectorDescriptor::VectorRegister());  // vector
}
void BaselineCompiler::VisitLdaKeyedProperty() {
  LoadFeedbackVector(LoadWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kKeyedLoadIC, RegisterOperand(0),   // object
              kInterpreterAccumulatorRegister,              // key
              IndexAsTagged(1),                             // slot
              LoadWithVectorDescriptor::VectorRegister());  // vector
}

void BaselineCompiler::VisitLdaModuleVariable() {
  LoadContext(kScratchRegister);
  int depth = Uint(1);
  for (; depth > 0; --depth) {
    LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                           Context::kPreviousOffset);
  }
  LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                         Context::kExtensionOffset);
  int cell_index = Int(0);
  if (cell_index > 0) {
    LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                           SourceTextModule::kRegularExportsOffset);
    // The actual array index is (cell_index - 1).
    cell_index -= 1;
  } else {
    LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                           SourceTextModule::kRegularImportsOffset);
    // The actual array index is (-cell_index - 1).
    cell_index = -cell_index - 1;
  }
  LoadFixedArrayElement(kScratchRegister, kScratchRegister, cell_index);
  LoadTaggedAnyField(kInterpreterAccumulatorRegister,
                     kScratchRegister,
                     Cell::kValueOffset);
}

void BaselineCompiler::VisitStaModuleVariable() {
  LoadContext(kScratchRegister);
  int depth = Uint(1);
  for (; depth > 0; --depth) {
    LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                           Context::kPreviousOffset);
  }
  LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                         Context::kExtensionOffset);
  int cell_index = Int(0);
  if (cell_index > 0) {
    LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                           SourceTextModule::kRegularExportsOffset);
    // The actual array index is (cell_index - 1).
    cell_index -= 1;
    LoadFixedArrayElement(kScratchRegister, kScratchRegister, cell_index);
    StoreTaggedField(kScratchRegister, Cell::kValueOffset, kInterpreterAccumulatorRegister);
  } else {
    // Not supported (probably never).
    CallRuntime(Runtime::kAbort, Smi::FromInt(static_cast<int>(AbortReason::kUnsupportedModuleOperation)));
    __ Trap();
  }
}

void BaselineCompiler::VisitStaNamedProperty() {
  LoadFeedbackVector(StoreWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kStoreIC,
              RegisterOperand(0),                            // object
              Constant<Name>(1),                             // name
              kInterpreterAccumulatorRegister,               // value
              IndexAsTagged(2),                              // slot
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
              IndexAsTagged(2),                              // slot
              StoreWithVectorDescriptor::VectorRegister());  // vector
}
void BaselineCompiler::VisitStaInArrayLiteral() {
  LoadFeedbackVector(StoreWithVectorDescriptor::VectorRegister());

  CallBuiltin(Builtins::kStoreInArrayLiteralIC, RegisterOperand(0),  // object
              RegisterOperand(1),                                    // name
              kInterpreterAccumulatorRegister,                       // value
              IndexAsTagged(2),                                      // slot
              StoreWithVectorDescriptor::VectorRegister());          // vector
}
void BaselineCompiler::VisitStaDataPropertyInLiteral() {
  CallRuntime(Runtime::kDefineDataPropertyInLiteral,
              RegisterOperand(0),               // object
              RegisterOperand(1),               // name
              kInterpreterAccumulatorRegister,  // value
              // TODO(verwaest): This should probably be tagged.
              IntAsSmi(2),        // flags
              IndexAsTagged(3));  // slot
}
void BaselineCompiler::VisitCollectTypeProfile() {
  LoadFeedbackVector(kScratchRegister);
  CallRuntime(Runtime::kCollectTypeProfile,
              IntAsSmi(0),                      // position
              kInterpreterAccumulatorRegister,  // value
              kScratchRegister);                // feedback vector
}
void BaselineCompiler::BuildBinop(Builtins::Name builtin_name) {
  Register feedback_vector =
      Builtins::CallInterfaceDescriptorFor(builtin_name)
          .GetRegisterParameter(
              BinaryOp_WithFeedbackDescriptor::kMaybeFeedbackVector);
  LoadFeedbackVector(feedback_vector);
  Register right =
      Builtins::CallInterfaceDescriptorFor(builtin_name)
          .GetRegisterParameter(BinaryOp_WithFeedbackDescriptor::kRight);
  __ Move(right, kInterpreterAccumulatorRegister);
  CallBuiltin(builtin_name, RegisterOperand(0),  // lhs
              right,                             // rhs
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
              Immediate(Index(1)),                            // slot
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
  LoadFeedbackVector(r8);
  CallBuiltin(builtin,
              RegisterOperand(0),    // kFunction
              Immediate(arg_count),  // kActualArgumentsCount
              Immediate(slot),       // kSlot
              r8,                    // kMaybeFeedbackVector
              args...);              // Arguments
}
void BaselineCompiler::VisitCallAnyReceiver() {
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);
  uint32_t arg_count = args.register_count() - 1;  // Remove receiver.
  BuildCall(ConvertReceiverMode::kAny, Index(3), arg_count, args);
}
void BaselineCompiler::VisitCallProperty() {
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);
  uint32_t arg_count = args.register_count() - 1;  // Remove receiver.
  BuildCall(ConvertReceiverMode::kNotNullOrUndefined, Index(3), arg_count,
            args);
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
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);
  uint32_t arg_count = args.register_count();
  BuildCall(ConvertReceiverMode::kNullOrUndefined, Index(3), arg_count, UndefinedOperand(), args);
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
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);
  uint32_t arg_count = args.register_count();
  CallBuiltin(Builtins::kCall_ReceiverIsAny,
                             RegisterOperand(0),    // kFunction
                             Immediate(arg_count-1),// kActualArgumentsCount
                             args);
}
void BaselineCompiler::VisitCallWithSpread() {
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);

  // Do not push the spread argument
  interpreter::Register spread_register = args.last_register();
  args = args.Truncate(args.register_count() - 1);

  uint32_t arg_count = args.register_count() - 1;  // Remove receiver.

  LoadFeedbackVector(r8);
  CallBuiltin(Builtins::kCallWithSpread_WithFeedback,
                             RegisterOperand(0),        // kFunction
                             Immediate(arg_count),      // kActualArgumentsCount
                             spread_register,           // kSpread
                             Immediate(Index(3)),       // kSlot
                             r8,                        // kMaybeFeedbackVector
                             args);
}
void BaselineCompiler::VisitCallRuntime() {
  CallRuntime(accessor().GetRuntimeIdOperand(0), accessor().GetRegisterListOperand(1));
}
void BaselineCompiler::VisitCallRuntimeForPair() {
  CallRuntime(accessor().GetRuntimeIdOperand(0), accessor().GetRegisterListOperand(1));
  StoreRegisterPair(3, kReturnRegister0, kReturnRegister1);
}
void BaselineCompiler::VisitCallJSRuntime() {
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);
  uint32_t arg_count = args.register_count();

  __ LoadNativeContextSlot(accessor().GetNativeContextIndexOperand(0),
                           kJavaScriptCallTargetRegister);
  CallBuiltin(Builtins::kCall_ReceiverIsNullOrUndefined,
              kJavaScriptCallTargetRegister,  // kFunction
              Immediate(arg_count),           // kActualArgumentsCount
              UndefinedOperand(),             // kReceiver
              args);
}
void BaselineCompiler::VisitInvokeIntrinsic() {
  Runtime::FunctionId intrinsic_id = accessor().GetIntrinsicIdOperand(0);
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);
  switch (intrinsic_id) {
    // TODO(leszeks): Extract into per-inline function helpers.
    case Runtime::kInlineCreateJSGeneratorObject:
      CallBuiltin(Builtins::kCreateGeneratorObject, args);
      break;
    // case Runtime::kInlineCreateIterResultObject:
    //   DCHECK_EQ(arg_count, 2);
    //   CallBuiltin(Builtins::kCreateIterResultObject,
    //               __ RegisterFrameOperand(first_arg_register),
    //               __ RegisterFrameOperand(first_arg_register.index() + 1));
    //   break;
    case Runtime::kInlineGeneratorGetResumeMode:
      LoadRegister(kInterpreterAccumulatorRegister, args[0]);
      LoadTaggedAnyField(kInterpreterAccumulatorRegister,
                         kInterpreterAccumulatorRegister,
                         JSGeneratorObject::kResumeModeOffset);
      break;
    case Runtime::kInlineGeneratorClose:
      LoadRegister(kInterpreterAccumulatorRegister, args[0]);
      __ Move(kScratchRegister,
              Smi::FromInt(JSGeneratorObject::kGeneratorClosed));
      StoreTaggedField(kInterpreterAccumulatorRegister,
                       JSGeneratorObject::kContinuationOffset,
                       kScratchRegister);
      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
      break;

    default:
      CallRuntime(intrinsic_id, args);
      break;
  }
}
void BaselineCompiler::VisitConstruct() {
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);
  uint32_t arg_count = args.register_count();
  LoadFeedbackVector(r8);
  CallBuiltin(Builtins::kConstruct_WithFeedback,
                             RegisterOperand(0),    // kFunction
                             RegisterOperand(0),    // kNewTarget
                             Immediate(arg_count),  // kActualArgumentsCount
                             Immediate(Index(3)),   // kSlot
                             r8,                    // kMaybeFeedbackVector
                             UndefinedOperand(),    // kReceiver
                             args);
}
void BaselineCompiler::VisitConstructWithSpread() {
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);

  // Do not push the spread argument
  interpreter::Register spread_register = args.last_register();
  args = args.Truncate(args.register_count() - 1);

  uint32_t arg_count = args.register_count();

  LoadFeedbackVector(r8);
  CallBuiltin(Builtins::kConstructWithSpread_WithFeedback,
                             RegisterOperand(0),    // kFunction
                             RegisterOperand(0),    // kNewTarget
                             Immediate(arg_count),  // kActualArgumentsCount
                             Immediate(Index(3)),   // kSlot
                             r8,                    // kMaybeFeedbackVector
                             spread_register,       // kSpread
                             UndefinedOperand(),    // kReceiver
                             args);
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

  Register right =
      Builtins::CallInterfaceDescriptorFor(builtin_name)
          .GetRegisterParameter(Compare_WithFeedbackDescriptor::kRight);
  __ Move(right, kInterpreterAccumulatorRegister);
  CallBuiltin(builtin_name, kScratchRegister,  // lhs
              right,                           // rhs
              Immediate(Index(1)),             // slot
              feedback_vector);                // vector

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
void BaselineCompiler::VisitTestInstanceOf() {
  LoadFeedbackVector(kScratchRegister);
  Register callable =
      Builtins::CallInterfaceDescriptorFor(Builtins::kInstanceOf_WithFeedback)
          .GetRegisterParameter(Compare_WithFeedbackDescriptor::kRight);
  __ Move(callable, kInterpreterAccumulatorRegister);
  CallBuiltin(Builtins::kInstanceOf_WithFeedback, RegisterOperand(0),  // object
              callable,             // callable
              Immediate(Index(1)),  // slot
              kScratchRegister);    // feedback vector
}
void BaselineCompiler::VisitTestIn() {
  LoadFeedbackVector(LoadWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kKeyedHasIC, kInterpreterAccumulatorRegister,  // object
              RegisterOperand(0),                                      // name
              IndexAsTagged(1),                                        // slot
              LoadWithVectorDescriptor::VectorRegister());  // feedback vector
}
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
              IndexAsTagged(1),         // slot
              Constant<HeapObject>(0),  // pattern
              FlagAsSmi(2));            // flags
}
void BaselineCompiler::VisitCreateArrayLiteral() {
  uint32_t flags = Flag(2);
  Register feedback_vector = kScratchRegister;
  LoadFeedbackVector(feedback_vector);
  if (flags &
      interpreter::CreateArrayLiteralFlags::FastCloneSupportedBit::kMask) {
    CallBuiltin(Builtins::kCreateShallowArrayLiteral,
                feedback_vector,           // feedback vector
                IndexAsTagged(1),          // slot
                Constant<HeapObject>(0));  // constant elements
  } else {
    int32_t flags_raw = static_cast<int32_t>(
        interpreter::CreateArrayLiteralFlags::FlagsBits::decode(flags));
    CallRuntime(Runtime::kCreateArrayLiteral,
                feedback_vector,           // feedback vector
                IndexAsTagged(1),          // slot
                Constant<HeapObject>(0),   // constant elements
                Smi::FromInt(flags_raw));  // flags
  }
}
void BaselineCompiler::VisitCreateArrayFromIterable() {
  CallBuiltin(Builtins::kIterableToListWithSymbolLookup,
              kInterpreterAccumulatorRegister);  // iterable
}
void BaselineCompiler::VisitCreateEmptyArrayLiteral() {
  Register feedback_vector = kScratchRegister;
  LoadFeedbackVector(feedback_vector);
  CallBuiltin(Builtins::kCreateEmptyArrayLiteral, feedback_vector,
              IndexAsTagged(0));
}
void BaselineCompiler::VisitCreateObjectLiteral() {
  uint32_t flags = Flag(2);
  int32_t flags_raw = static_cast<int32_t>(
      interpreter::CreateObjectLiteralFlags::FlagsBits::decode(flags));
  Register feedback_vector = kScratchRegister;
  LoadFeedbackVector(feedback_vector);
  if (flags &
      interpreter::CreateObjectLiteralFlags::FastCloneSupportedBit::kMask) {
    CallBuiltin(Builtins::kCreateShallowObjectLiteral,
                feedback_vector,                            // feedback vector
                IndexAsTagged(1),                           // slot
                Constant<ObjectBoilerplateDescription>(0),  // boilerplate
                Smi::FromInt(flags_raw));                   // flags
  } else {
    CallRuntime(Runtime::kCreateObjectLiteral,
                feedback_vector,                            // feedback vector
                IndexAsTagged(1),                           // slot
                Constant<ObjectBoilerplateDescription>(0),  // boilerplate
                Smi::FromInt(flags_raw));                   // flags
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
              IndexAsTagged(2),                              // slot
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
              Immediate(Index(1)),               // slot
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
void BaselineCompiler::VisitCreateMappedArguments() {
  // Check for duplicate parameters.
  Label done, call_builtin;
  LoadFunction(kScratchRegister);
  LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                         JSFunction::kSharedFunctionInfoOffset);
  LoadTaggedPointerField(kScratchRegister, kScratchRegister, SharedFunctionInfo::kFlagsOffset);
  __ andq(kScratchRegister, Immediate(SharedFunctionInfo::HasDuplicateParametersBit::kMask));
  __ j(zero, &call_builtin);
  CallRuntime(Runtime::kNewSloppyArguments, __ FunctionOperand());
  __ jmp(&done);

  __ bind(&call_builtin);
  CallBuiltin(Builtins::kFastNewSloppyArguments, __ FunctionOperand());
  __ bind(&done);
}
void BaselineCompiler::VisitCreateUnmappedArguments() {
  CallBuiltin(Builtins::kFastNewStrictArguments, __ FunctionOperand());
}
void BaselineCompiler::VisitCreateRestParameter() {
  CallBuiltin(Builtins::kFastNewRestArguments, __ FunctionOperand());
}
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
  interpreter::JumpTableTargetOffsets offsets =
      accessor().GetJumpTableTargetOffsets();
  int case_value_base = (*offsets.begin()).case_value;

  std::unique_ptr<Label*[]> labels = std::make_unique<Label*[]>(offsets.size());
  for (const interpreter::JumpTableTargetOffset& offset : offsets) {
    labels[offset.case_value - case_value_base] =
        &unlinked_labels_[offset.target_offset];
  }
  __ SmiUntag(r12, kInterpreterAccumulatorRegister);
  __ Switch(r12, case_value_base, labels.get(), offsets.size());
}
void BaselineCompiler::VisitForInEnumerate() {
  CallBuiltin(Builtins::kForInEnumerate, RegisterOperand(0));
}
void BaselineCompiler::VisitForInPrepare() {
  StoreRegister(0, kInterpreterAccumulatorRegister);
  LoadFeedbackVector(kScratchRegister);
  CallBuiltin(Builtins::kForInPrepare, kInterpreterAccumulatorRegister,
              IndexAsTagged(1), kScratchRegister);
  interpreter::Register first = accessor().GetRegisterOperand(0);
  interpreter::Register second(first.index() + 1);
  interpreter::Register third(first.index() + 2);
  __ movq(RegisterOperand(second), kReturnRegister0);
  __ movq(RegisterOperand(third), kReturnRegister1);
}
void BaselineCompiler::VisitForInContinue() {
  LoadRegister(kInterpreterAccumulatorRegister, 0);
  LoadRegister(kScratchRegister, 1);
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

void BaselineCompiler::VisitSwitchOnGeneratorState() {
  Label fallthrough;

  Register generator_object = r12;
  LoadRegister(generator_object, 0);
  __ JumpIfRoot(generator_object, RootIndex::kUndefinedValue, &fallthrough,
                Label::kNear);

  LoadTaggedAnyField(r11, generator_object,
                     JSGeneratorObject::kContinuationOffset);
  __ Move(kScratchRegister,
          Smi::FromInt(JSGeneratorObject::kGeneratorExecuting));
  StoreTaggedField(generator_object, JSGeneratorObject::kContinuationOffset,
                   kScratchRegister);

  LoadTaggedAnyField(kScratchRegister, generator_object,
                     JSGeneratorObject::kContextOffset);
  StoreContext(kScratchRegister);

  interpreter::JumpTableTargetOffsets offsets =
      accessor().GetJumpTableTargetOffsets();
  DCHECK_EQ(0, (*offsets.begin()).case_value);

  std::unique_ptr<Label*[]> labels = std::make_unique<Label*[]>(offsets.size());
  for (const interpreter::JumpTableTargetOffset& offset : offsets) {
    labels[offset.case_value] = &unlinked_labels_[offset.target_offset];
  }
  __ SmiUntag(r11);
  __ Switch(r11, 0, labels.get(), offsets.size());
  // We should never fall through this switch.
  // TODO(leszeks): Maybe remove the fallthrough check in the Switch?
  __ Trap();

  __ bind(&fallthrough);
}

void BaselineCompiler::VisitSuspendGenerator() {
  DCHECK_EQ(accessor().GetRegisterOperand(1), interpreter::Register(0));
  int register_count = RegisterCount(2);
  uint32_t suspend_id = Uint(3);

  Register generator_object = r12, parameters_and_registers_array = r11;
  LoadRegister(generator_object, 0);
  LoadTaggedPointerField(parameters_and_registers_array, generator_object,
                         JSGeneratorObject::kParametersAndRegistersOffset);

  int formal_parameter_count = shared_->internal_formal_parameter_count();
  for (int i = 0; i < formal_parameter_count; ++i) {
    LoadRegister(kScratchRegister, interpreter::Register::FromParameterIndex(
                                       i, bytecode_->parameter_count()));
    StoreTaggedField(parameters_and_registers_array,
                     FixedArray::OffsetOfElementAt(i), kScratchRegister);
  }
  for (int i = 0; i < register_count; ++i) {
    LoadRegister(kScratchRegister, interpreter::Register(i));
    StoreTaggedField(parameters_and_registers_array,
                     FixedArray::OffsetOfElementAt(formal_parameter_count + i),
                     kScratchRegister);
  }

  LoadContext(kScratchRegister);
  StoreTaggedField(generator_object, JSGeneratorObject::kContextOffset,
                   kScratchRegister);

  __ Move(kScratchRegister, Smi::FromInt(suspend_id));
  StoreTaggedField(generator_object, JSGeneratorObject::kContinuationOffset,
                   kScratchRegister);

  __ Move(kScratchRegister, Smi::FromInt(accessor().current_offset()));
  StoreTaggedField(generator_object, JSGeneratorObject::kInputOrDebugPosOffset,
                   kScratchRegister);
  VisitReturn();
}

void BaselineCompiler::VisitResumeGenerator() {
  DCHECK_EQ(accessor().GetRegisterOperand(1), interpreter::Register(0));
  int register_count = RegisterCount(2);

  Register generator_object = r12, parameters_and_registers_array = r11;
  LoadRegister(generator_object, 0);
  LoadTaggedPointerField(parameters_and_registers_array, generator_object,
                         JSGeneratorObject::kParametersAndRegistersOffset);

  int formal_parameter_count = shared_->internal_formal_parameter_count();
  for (int i = 0; i < register_count; ++i) {
    LoadTaggedAnyField(
        kScratchRegister, parameters_and_registers_array,
        FixedArray::OffsetOfElementAt(formal_parameter_count + i));
    StoreRegister(interpreter::Register(i), kScratchRegister);
  }

  LoadTaggedAnyField(kInterpreterAccumulatorRegister, generator_object,
                     JSGeneratorObject::kInputOrDebugPosOffset);
}

void BaselineCompiler::VisitGetIterator() {
  // TODO(marja): figure out which register the feeback vector should be in.
  Register feedback_vector = kScratchRegister;
  LoadFeedbackVector(feedback_vector);
  CallBuiltin(Builtins::kGetIteratorWithFeedback,
              RegisterOperand(0),  // receiver
              IndexAsTagged(1),    // load_slot
              IndexAsTagged(2),    // call_slot
              feedback_vector);    // vector
}

void BaselineCompiler::VisitDebugger() {
  CallBuiltin(Builtins::kHandleDebuggerStatement);
}
void BaselineCompiler::VisitIncBlockCounter() {
  CallRuntime(Runtime::kInlineIncBlockCounter, __ FunctionOperand(),
              Constant<HeapObject>(0));  // coverage array slot
}
void BaselineCompiler::VisitAbort() {
  CallRuntime(Runtime::kAbort, Smi::FromInt(Index(0)));
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
