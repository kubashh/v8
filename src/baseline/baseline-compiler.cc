// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/baseline-compiler.h"

#include <type_traits>
#include <unordered_map>

#include "src/builtins/builtins-constructor.h"
#include "src/builtins/builtins-descriptors.h"
#include "src/builtins/builtins.h"
#include "src/codegen/assembler.h"
#include "src/codegen/compiler.h"
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
#include "src/objects/shared-function-info-inl.h"
#include "src/roots/roots.h"

#if V8_TARGET_ARCH_IA32
#include "src/baseline/ia32/baseline-compiler-ia32-inl.h"
#elif V8_TARGET_ARCH_X64
#include "src/baseline/x64/baseline-compiler-x64-inl.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/baseline/arm64/baseline-compiler-arm64-inl.h"
#elif V8_TARGET_ARCH_ARM
#include "src/baseline/arm/baseline-compiler-arm-inl.h"
#elif V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
#include "src/baseline/ppc/baseline-compiler-ppc-inl.h"
#elif V8_TARGET_ARCH_MIPS
#include "src/baseline/mips/baseline-compiler-mips-inl.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/baseline/mips64/baseline-compiler-mips64-inl.h"
#elif V8_TARGET_ARCH_S390
#include "src/baseline/s390/baseline-compiler-s390-inl.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {

template <typename LocalIsolate>
Handle<ByteArray> BytecodeOffsetTableBuilder::ToBytecodeOffsetTable(
    LocalIsolate* isolate) {
  if (bytes_.empty()) return isolate->factory()->empty_byte_array();
  Handle<ByteArray> table = isolate->factory()->NewByteArray(
      static_cast<int>(bytes_.size()), AllocationType::kOld);
  MemCopy(table->GetDataStartAddress(), bytes_.data(), bytes_.size());
  return table;
}

MemOperand BaselineAssembler::ContextOperand() {
  return RegisterFrameOperand(interpreter::Register::current_context());
}
MemOperand BaselineAssembler::FunctionOperand() {
  return RegisterFrameOperand(interpreter::Register::function_closure());
}
MemOperand BaselineAssembler::FeedbackVectorOperand() {
  // We re-use the bytecode offset slot for the feedback vector.
  return RegisterFrameOperand(interpreter::Register::bytecode_offset());
}

void BaselineAssembler::Move(Register output, interpreter::Register source) {
  return Move(output, RegisterFrameOperand(source));
}
void BaselineAssembler::Move(Register output, RootIndex source) {
  return LoadRoot(output, source);
}

void BaselineAssembler::Push(interpreter::Register source) {
  return Push(RegisterFrameOperand(source));
}
void BaselineAssembler::Push(RootIndex source) { return PushRoot(source); }

void BaselineAssembler::LeaveFrame() {
  MacroAssembler::LeaveFrame(StackFrame::MANUAL);
}

void BaselineAssembler::LoadFixedArrayElement(Register output, Register array,
                                              int32_t index) {
  LoadTaggedAnyField(output, array,
                     FixedArray::kHeaderSize + index * kTaggedSize);
}

void BaselineAssembler::LoadPrototype(Register prototype, Register object) {
  LoadMap(prototype, object);
  LoadTaggedPointerField(prototype, prototype, Map::kPrototypeOffset);
}
void BaselineAssembler::LoadAccumulator(Register output) {
  Move(output, kInterpreterAccumulatorRegister);
}
void BaselineAssembler::PushAccumulator() {
  Push(kInterpreterAccumulatorRegister);
}
void BaselineAssembler::PopAccumulator() {
  Pop(kInterpreterAccumulatorRegister);
}
void BaselineAssembler::LoadContext(Register output) {
  LoadRegister(output, interpreter::Register::current_context());
}
void BaselineAssembler::LoadFunction(Register output) {
  LoadRegister(output, interpreter::Register::function_closure());
}
void BaselineAssembler::StoreContext(Register context) {
  StoreRegister(interpreter::Register::current_context(), context);
}
void BaselineAssembler::LoadRegister(Register output,
                                     interpreter::Register source) {
  Move(output, source);
}
void BaselineAssembler::StoreRegister(interpreter::Register output,
                                      Register value) {
  Move(output, value);
}
namespace {

#ifdef DEBUG
bool Clobbers(Register target, Register reg) { return target == reg; }
bool Clobbers(Register target, MemOperand op) {
  return op.AddressUsesRegister(target);
}
bool Clobbers(Register target, Handle<Object> handle) { return false; }
bool Clobbers(Register target, Smi smi) { return false; }
bool Clobbers(Register target, TaggedIndex index) { return false; }
bool Clobbers(Register target, Immediate imm) { return false; }
bool Clobbers(Register target, RootIndex index) { return false; }
bool Clobbers(Register target, interpreter::Register reg) { return false; }

// We don't know what's inside machine registers or operands, so assume they
// match.
bool MachineTypeMatches(MachineType type, Register reg) { return true; }
bool MachineTypeMatches(MachineType type, MemOperand reg) { return true; }
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
bool MachineTypeMatches(MachineType type, RootIndex index) {
  return type.IsTagged() && !type.IsTaggedSigned();
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
  static int Push(BaselineAssembler* masm, Arg arg) {
    masm->Push(arg);
    return 1;
  }
  static int PushReverse(BaselineAssembler* masm, Arg arg) {
    return Push(masm, arg);
  }
};

template <>
struct PushHelper<interpreter::RegisterList> {
  static int Push(BaselineAssembler* masm, interpreter::RegisterList list) {
    for (int reg_index = 0; reg_index < list.register_count(); ++reg_index) {
      masm->Push(masm->RegisterFrameOperand(list[reg_index]));
    }
    return list.register_count();
  }
  static int PushReverse(BaselineAssembler* masm,
                         interpreter::RegisterList list) {
    for (int reg_index = list.register_count() - 1; reg_index >= 0;
         --reg_index) {
      masm->Push(masm->RegisterFrameOperand(list[reg_index]));
    }
    return list.register_count();
  }
};

template <typename... Args>
struct PushAllHelper;
template <>
struct PushAllHelper<> {
  static int Push(BaselineAssembler* masm) { return 0; }
  static int PushReverse(BaselineAssembler* masm) { return 0; }
};
template <typename Arg, typename... Args>
struct PushAllHelper<Arg, Args...> {
  static int Push(BaselineAssembler* masm, Arg arg, Args... args) {
    int nargs = PushHelper<Arg>::Push(masm, arg);
    return nargs + PushAllHelper<Args...>::Push(masm, args...);
  }
  static int PushReverse(BaselineAssembler* masm, Arg arg, Args... args) {
    int nargs = PushAllHelper<Args...>::PushReverse(masm, args...);
    return nargs + PushHelper<Arg>::PushReverse(masm, arg);
  }
};

template <typename... Args>
int PushAll(BaselineAssembler* masm, Args... args) {
  return PushAllHelper<Args...>::Push(masm, args...);
}

template <typename... Args>
int PushAllReverse(BaselineAssembler* masm, Args... args) {
  return PushAllHelper<Args...>::PushReverse(masm, args...);
}

template <typename... Args>
struct ArgumentSettingHelper;

template <>
struct ArgumentSettingHelper<> {
  static void Set(BaselineAssembler* masm, CallInterfaceDescriptor descriptor,
                  int i) {}
  static void CheckSettingDoesntClobber(Register target, int arg_index) {}
};

template <typename Arg, typename... Args>
struct ArgumentSettingHelper<Arg, Args...> {
  static void Set(BaselineAssembler* masm, CallInterfaceDescriptor descriptor,
                  int i, Arg arg, Args... args) {
    if (i < descriptor.GetRegisterParameterCount()) {
      Register target = descriptor.GetRegisterParameter(i);
      ArgumentSettingHelper<Args...>::CheckSettingDoesntClobber(target, i + 1,
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
  static void CheckSettingDoesntClobber(Register target, int arg_index, Arg arg,
                                        Args... args) {
    DCHECK(!Clobbers(target, arg));
    ArgumentSettingHelper<Args...>::CheckSettingDoesntClobber(target, arg_index + 1,
                                                              args...);
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
      for (int reg_index = 0; reg_index < list.register_count();
           ++reg_index, ++i) {
        Register target = descriptor.GetRegisterParameter(i);
        ArgumentSettingHelper<Args...>::CheckSettingDoesntClobber(target, i + 1,
                                                                  args...);
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
  static void CheckSettingDoesntClobber(Register target, int arg_index,
                                        interpreter::RegisterList arg,
                                        Args... args) {
    ArgumentSettingHelper<Args...>::CheckSettingDoesntClobber(target, arg_index+1, args...);
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

class BaselineAssembler::SaveAccumulatorScope final {
 public:
  explicit SaveAccumulatorScope(BaselineAssembler* assembler)
      : assembler_(assembler) {
    assembler_->PushAccumulator();
  }

  ~SaveAccumulatorScope() { assembler_->PopAccumulator(); }

 private:
  BaselineAssembler* assembler_;
};

BaselineCompiler::BaselineCompiler(Isolate* isolate, int formal_parameter_count,
                                   Handle<BytecodeArray> bytecode)
    : isolate_(isolate),
      formal_parameter_count_(formal_parameter_count),
      bytecode_(bytecode),
      masm_(isolate, CodeObjectRequired::kNo),
      iterator_(bytecode_) {}

#define __ masm_.

void BaselineCompiler::GenerateCode() {
  HandlerTable table(*bytecode_);
  for (int i = 0; i < table.NumberOfRangeEntries(); ++i) {
    int handler_offset = table.GetRangeHandler(i);
    handler_offsets_.insert(handler_offset);
  }

  for (; !iterator_.done(); iterator_.Advance()) {
    PreVisitSingleBytecode();
  }
  iterator_.Reset();

  Prologue();
  for (; !iterator_.done(); iterator_.Advance()) {
    VisitSingleBytecode();
  }
}

Handle<Code> BaselineCompiler::Build(Isolate* isolate) {
  CodeDesc desc;
  __ GetCode(isolate, &desc);
  // Allocate the source position table.
  Handle<ByteArray> bytecode_offset_table =
      bytecode_offset_table_builder_.ToBytecodeOffsetTable(isolate);
  return Factory::CodeBuilder(isolate, desc, CodeKind::SPARKPLUG)
      .set_bytecode_offset_table(bytecode_offset_table)
      .Build();
}

void BaselineCompiler::Prologue() {
  __ Move(kInterpreterBytecodeArrayRegister, bytecode_);
  DCHECK_EQ(kJSFunctionRegister, kJavaScriptCallTargetRegister);
  CallBuiltin(Builtins::kBaselinePrologue, kContextRegister,
              kJSFunctionRegister, kJavaScriptCallArgCountRegister,
              kInterpreterBytecodeArrayRegister);

  FrameScope frame_scope(&masm_, StackFrame::MANUAL);
  __ Push(rbp);  // Caller's frame pointer.
  __ Move(rbp, rsp);

  // BaselinePrologue has already pushed everything on the stack,
  // just increase the SP by stackframe size anc continue
  __ subq(rsp, Immediate(kSystemPointerSize * 5));

  // Stack check. This folds the checks for both the interrupt stack limit
  // check and the real stack limit into one by just checking for the
  // interrupt limit. The interrupt limit is either equal to the real stack
  // limit or tighter. By ensuring we have space until that limit after
  // building the frame we can quickly precheck both at once.
  __ Move(kScratchRegister, rsp);
  __ subq(kScratchRegister, Immediate(bytecode_->frame_size()));
  __ cmpq(kScratchRegister,
          __ StackLimitAsOperand(StackLimitKind::kInterruptStackLimit));
  Label after_stack_check;
  __ j(above_equal, &after_stack_check, Label::kNear);

  __ RecordComment("[ Stack/interrupt check");
  // Save incoming new target or generator
  __ Push(rdx);
  __ CallRuntime(Runtime::kStackGuard);
  AddAsStartPosition();
  __ Pop(rdx);
  __ RecordComment("]");

  __ bind(&after_stack_check);

  PrologueFillFrame();
}

void BaselineCompiler::PrologueFillFrame() {
  __ RecordComment("[ Fill frame");
  // Inlined register frame fill
  interpreter::Register new_target_or_generator_register =
      bytecode_->incoming_new_target_or_generator_register();
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  int register_count = bytecode_->register_count();
  // Magic value
  const int kLoopUnrollSize = 8;
  const int new_target_index = new_target_or_generator_register.index();
  const bool has_new_target = new_target_index != kMaxInt;
  int i = 0;
  if (has_new_target) {
    DCHECK_LE(new_target_index, register_count);
    for (; i < new_target_index; i++) {
      __ Push(kInterpreterAccumulatorRegister);
    }
    // Push new_target_or_generator.
    __ Push(rdx);
    i++;
    // printf("%i\n", new_target_index);
  }
  if (register_count < 2 * kLoopUnrollSize) {
    // If the frame is small enough, just unroll the frame fill completely.
    for (; i < register_count; ++i) {
      __ Push(kInterpreterAccumulatorRegister);
    }
  } else {
    register_count -= i;
    i = 0;
    // Extract the first few registers to round to the unroll size.
    int first_registers = register_count % kLoopUnrollSize;
    for (; i < first_registers; ++i) {
      __ Push(kInterpreterAccumulatorRegister);
    }
    __ Move(kScratchRegister, Immediate(register_count / kLoopUnrollSize));
    Label loop;
    __ bind(&loop);
    for (int j = 0; j < kLoopUnrollSize; ++j) {
      __ Push(kInterpreterAccumulatorRegister);
    }
    __ decl(kScratchRegister);
    __ j(not_zero, &loop);
  }
  __ RecordComment("]");
}

interpreter::Register BaselineCompiler::RegisterOperand(int operand_index) {
  return accessor().GetRegisterOperand(operand_index);
}
void BaselineCompiler::LoadRegister(Register output, int operand_index) {
  __ LoadRegister(output, RegisterOperand(operand_index));
}
void BaselineCompiler::StoreRegister(int operand_index, Register value) {
  __ Move(RegisterOperand(operand_index), value);
}
void BaselineCompiler::StoreRegisterPair(int operand_index, Register val0,
                                         Register val1) {
  interpreter::Register reg0, reg1;
  std::tie(reg0, reg1) = accessor().GetRegisterPairOperand(operand_index);
  __ StoreRegister(reg0, val0);
  __ StoreRegister(reg1, val1);
}
template <typename Type>
Handle<Type> BaselineCompiler::Constant(int operand_index) {
  return Handle<Type>::cast(
      accessor().GetConstantForIndexOperand(operand_index, isolate_));
}
Smi BaselineCompiler::ConstantSmi(int operand_index) {
  return accessor().GetConstantAtIndexAsSmi(operand_index);
}
template <typename Type>
void BaselineCompiler::LoadConstant(Register output, int operand_index) {
  __ Move(output, Constant<Type>(operand_index));
}
uint32_t BaselineCompiler::Uint(int operand_index) {
  return accessor().GetUnsignedImmediateOperand(operand_index);
}
int32_t BaselineCompiler::Int(int operand_index) {
  return accessor().GetImmediateOperand(operand_index);
}
uint32_t BaselineCompiler::Index(int operand_index) {
  return accessor().GetIndexOperand(operand_index);
}
uint32_t BaselineCompiler::Flag(int operand_index) {
  return accessor().GetFlagOperand(operand_index);
}
uint32_t BaselineCompiler::RegisterCount(int operand_index) {
  return accessor().GetRegisterCountOperand(operand_index);
}
TaggedIndex BaselineCompiler::IndexAsTagged(int operand_index) {
  return TaggedIndex::FromIntptr(Index(operand_index));
}
Smi BaselineCompiler::IndexAsSmi(int operand_index) {
  return Smi::FromInt(Index(operand_index));
}
Smi BaselineCompiler::IntAsSmi(int operand_index) {
  return Smi::FromInt(Int(operand_index));
}
Smi BaselineCompiler::FlagAsSmi(int operand_index) {
  return Smi::FromInt(Flag(operand_index));
}

MemOperand BaselineCompiler::FeedbackVector() {
  return __ FeedbackVectorOperand();
}
void BaselineCompiler::LoadFeedbackVector(Register output) {
  __ RecordComment("[ LoadFeedbackVector");
  __ Move(output, __ FeedbackVectorOperand());
  if (__ emit_debug_code()) {
    DCHECK_NE(output, kScratchRegister);
    __ CmpObjectType(output, FEEDBACK_VECTOR_TYPE, kScratchRegister);
    __ Assert(equal, AbortReason::kExpectedFeedbackVector);
  }
  __ RecordComment("]");
}
void BaselineCompiler::LoadClosureFeedbackArray(Register output,
                                                Register closure) {
  LoadFeedbackVector(output);
  __ LoadTaggedPointerField(output, output,
                            FeedbackVector::kClosureFeedbackCellArrayOffset);
}
void BaselineCompiler::SelectBooleanConstant(Condition condition,
                                             Register output) {
  Label done, set_true;
  __ j(condition, &set_true, Label::kNear);
  __ LoadRoot(output, RootIndex::kFalseValue);
  __ jmp(&done, Label::kNear);
  __ bind(&set_true);
  __ LoadRoot(output, RootIndex::kTrueValue);
  __ bind(&done);
}

void BaselineCompiler::AddPosition() {
  bytecode_offset_table_builder_.AddPosition(__ pc_offset(),
                                             accessor().current_offset());
}
void BaselineCompiler::AddAsStartPosition() {
  bytecode_offset_table_builder_.AddStartPosition(__ pc_offset());
}

void BaselineCompiler::PreVisitSingleBytecode() {
  if (accessor().current_bytecode() == interpreter::Bytecode::kJumpLoop) {
    unlinked_labels_[accessor().GetJumpTargetOffset()] = Label();
  }
}

void BaselineCompiler::VisitSingleBytecode() {
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

  // Record positions of exception handlers.
  if (handler_offsets_.find(accessor().current_offset()) !=
      handler_offsets_.end()) {
    AddPosition();
  }

#ifdef V8_TRACE_IGNITION
  TraceBytecode(Runtime::kInterpreterTraceBytecodeEntry);
#endif

  switch (accessor().current_bytecode()) {
#define BYTECODE_CASE(name, ...)       \
  case interpreter::Bytecode::k##name: \
    __ RecordComment("[ " #name);      \
    Visit##name();                     \
    break;
    BYTECODE_LIST(BYTECODE_CASE)
#undef BYTECODE_CASE
  }
  __ RecordComment("]");

#ifdef V8_TRACE_IGNITION
  TraceBytecode(Runtime::kInterpreterTraceBytecodeExit);
#endif
}

#ifdef V8_TRACE_IGNITION
void BaselineCompiler::TraceBytecode(Runtime::FunctionId function_id) {
  BaselineAssembler::SaveAccumulatorScope accumulator_scope(&masm_);
  CallRuntime(
      function_id, bytecode_,
      Immediate(Smi::FromInt(BytecodeArray::kHeaderSize - kHeapObjectTag +
                             accessor().current_offset())),
      kInterpreterAccumulatorRegister, Immediate(Smi::FromInt(1)));
}
#endif

#define DECLARE_VISITOR(name, ...) void Visit##name();
BYTECODE_LIST(DECLARE_VISITOR)
#undef DECLARE_VISITOR

#define DECLARE_VISITOR(name, ...) \
  void VisitIntrinsic##name(interpreter::RegisterList args);
INTRINSICS_LIST(DECLARE_VISITOR)
#undef DECLARE_VISITOR

void BaselineCompiler::UpdateInterruptBudgetAndJumpToLabel(
    int weight, Label* label, Label* skip_interrupt_label) {
  __ RecordComment("[ Update Interrupt Budget");
  __ LoadFunction(kScratchRegister);
  __ LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                            JSFunction::kFeedbackCellOffset);

  __ addl(FieldOperand(kScratchRegister, FeedbackCell::kInterruptBudgetOffset),
          Immediate(weight));

  if (weight < 0) {
    // Use compare flags set by add
    // TODO(leszeks): This might be trickier cross-arch.
    __ j(greater_equal, skip_interrupt_label);
    BaselineAssembler::SaveAccumulatorScope accumulator_scope(&masm_);
    CallRuntime(Runtime::kBytecodeBudgetInterruptFromBytecode,
                __ FunctionOperand());
  }
  if (label) __ j(always, label);
  __ RecordComment("]");
}

void BaselineCompiler::UpdateInterruptBudgetAndDoInterpreterJump() {
  int weight = accessor().GetRelativeJumpTargetOffset();
  UpdateInterruptBudgetAndJumpToLabel(weight, BuildForwardJumpLabel(), nullptr);
}

void BaselineCompiler::UpdateInterruptBudgetAndDoInterpreterJumpIfRoot(
    RootIndex root) {
  Label dont_jump;
  __ JumpIfNotRoot(kInterpreterAccumulatorRegister, root, &dont_jump,
                   Label::kNear);
  UpdateInterruptBudgetAndDoInterpreterJump();
  __ bind(&dont_jump);
}

void BaselineCompiler::UpdateInterruptBudgetAndDoInterpreterJumpIfNotRoot(
    RootIndex root) {
  Label dont_jump;
  __ JumpIfRoot(kInterpreterAccumulatorRegister, root, &dont_jump,
                Label::kNear);
  UpdateInterruptBudgetAndDoInterpreterJump();
  __ bind(&dont_jump);
}

Label* BaselineCompiler::BuildForwardJumpLabel() {
  int target_offset = accessor().GetJumpTargetOffset();
  std::vector<Label>& labels = linked_labels_[target_offset];
  labels.emplace_back();
  return &labels.back();
}

template <typename... Args>
void BaselineCompiler::CallBuiltin(Builtins::Name builtin, Args... args) {
  __ RecordComment("[ CallBuiltin");
  CallInterfaceDescriptor descriptor =
      Builtins::CallInterfaceDescriptorFor(builtin);
  MoveArgumentsForDescriptor(&masm_, descriptor, args...);
  if (descriptor.HasContextParameter()) {
    __ LoadContext(descriptor.ContextRegister());
  }
  __ CallBuiltin(builtin);
  if (builtin != Builtins::kBaselinePrologue) AddPosition();
  __ RecordComment("]");
}

template <typename... Args>
void BaselineCompiler::TailCallBuiltin(Builtins::Name builtin, Args... args) {
  CallInterfaceDescriptor descriptor =
      Builtins::CallInterfaceDescriptorFor(builtin);
  MoveArgumentsForDescriptor(&masm_, descriptor, args...);
  if (descriptor.HasContextParameter()) {
    // The context interpreter register isn't ready yet, so load it from the
    // caller-passed context machine register.
    // TODO(leszeks): Automatically decide which context source to use.
    __ Move(descriptor.ContextRegister(), kContextRegister);
  }
  __ TailCallBuiltin(builtin);
}

template <typename... Args>
void BaselineCompiler::CallRuntime(Runtime::FunctionId function, Args... args) {
  __ LoadContext(kContextRegister);
  int nargs = PushAll(&masm_, args...);
  __ CallRuntime(function, nargs);
  AddPosition();
}

// Returns into kInterpreterAccumulatorRegister
void BaselineCompiler::JumpIfToBoolean(bool do_jump_if_true, Register reg,
                                       Label* label, Label::Distance distance) {
  Label end;
  Label::Distance end_distance = Label::kNear;

  Label* true_label = do_jump_if_true ? label : &end;
  Label::Distance true_distance = do_jump_if_true ? distance : end_distance;
  Label* false_label = do_jump_if_true ? &end : label;
  Label::Distance false_distance = do_jump_if_true ? end_distance : distance;

  // Fast paths for oddballs.
  __ JumpIfRoot(reg, RootIndex::kTrueValue, true_label, true_distance);
  __ JumpIfRoot(reg, RootIndex::kFalseValue, false_label, false_distance);
  __ JumpIfRoot(reg, RootIndex::kUndefinedValue, false_label, false_distance);
  __ JumpIfRoot(reg, RootIndex::kNullValue, false_label, false_distance);

  // Fast path for Smi.
  __ Cmp(reg, Smi::FromInt(0));
  __ j(equal, false_label, false_distance);
  __ JumpIfSmi(reg, true_label, true_distance);

  {
    BaselineAssembler::SaveAccumulatorScope accumulator_scope(&masm_);
    CallBuiltin(Builtins::kToBoolean, reg);
    __ Move(kScratchRegister, kInterpreterAccumulatorRegister);
  }
  __ JumpIfRoot(kScratchRegister, RootIndex::kTrueValue, true_label,
                true_distance);
  if (false_label != &end) __ jmp(false_label, false_distance);

  __ bind(&end);
}

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
  CallBuiltin(Builtins::kLoadGlobalIC,
              Constant<Name>(0),  // name
              IndexAsTagged(1),   // slot
              FeedbackVector());  // vector
}
void BaselineCompiler::VisitLdaGlobalInsideTypeof() {
  CallBuiltin(Builtins::kLoadGlobalICInsideTypeof,
              Constant<Name>(0),  // name
              IndexAsTagged(1),   // slot
              FeedbackVector());  // vector
}
void BaselineCompiler::VisitStaGlobal() {
  CallBuiltin(Builtins::kStoreGlobalIC,
              Constant<Name>(0),                // name
              kInterpreterAccumulatorRegister,  // value
              IndexAsTagged(1),                 // slot
              FeedbackVector());                // vector
}
void BaselineCompiler::VisitPushContext() {
  __ LoadContext(kScratchRegister);
  __ StoreContext(kInterpreterAccumulatorRegister);
  StoreRegister(0, kScratchRegister);
}
void BaselineCompiler::VisitPopContext() {
  LoadRegister(kScratchRegister, 0);
  __ StoreContext(kScratchRegister);
}
void BaselineCompiler::VisitLdaContextSlot() {
  LoadRegister(kScratchRegister, 0);
  int depth = Uint(2);
  for (; depth > 0; --depth) {
    __ LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                              Context::kPreviousOffset);
  }
  __ LoadTaggedAnyField(kInterpreterAccumulatorRegister, kScratchRegister,
                        Context::OffsetOfElementAt(Index(1)));
}
void BaselineCompiler::VisitLdaImmutableContextSlot() { VisitLdaContextSlot(); }
void BaselineCompiler::VisitLdaCurrentContextSlot() {
  __ LoadContext(kScratchRegister);
  __ LoadTaggedAnyField(kInterpreterAccumulatorRegister, kScratchRegister,
                        Context::OffsetOfElementAt(Index(0)));
}
void BaselineCompiler::VisitLdaImmutableCurrentContextSlot() {
  VisitLdaCurrentContextSlot();
}
void BaselineCompiler::VisitStaContextSlot() {
  LoadRegister(kScratchRegister, 0);
  int depth = Uint(2);
  for (; depth > 0; --depth) {
    __ LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                              Context::kPreviousOffset);
  }
  __ Move(r15, kInterpreterAccumulatorRegister);
  __ StoreTaggedFieldWithWriteBarrier(
      kScratchRegister,
      Context::OffsetOfElementAt(accessor().GetIndexOperand(1)), r15, r11);
}
void BaselineCompiler::VisitStaCurrentContextSlot() {
  __ LoadContext(kScratchRegister);
  __ Move(r15, kInterpreterAccumulatorRegister);
  __ StoreTaggedFieldWithWriteBarrier(
      kScratchRegister, Context::OffsetOfElementAt(Index(0)), r15, r11);
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
  uint32_t flags = Flag(1);
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
  CallBuiltin(Builtins::kLoadIC,
              RegisterOperand(0),  // object
              Constant<Name>(1),   // name
              IndexAsSmi(2),       // slot
              FeedbackVector());   // vector
}
void BaselineCompiler::VisitLdaNamedPropertyNoFeedback() {
  CallBuiltin(Builtins::kGetProperty, RegisterOperand(0), Constant<Name>(1));
}
void BaselineCompiler::VisitLdaNamedPropertyFromSuper() {
  __ LoadPrototype(
      LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister(),
      kInterpreterAccumulatorRegister);

  CallBuiltin(Builtins::kLoadSuperIC,
              RegisterOperand(0),  // object
              LoadWithReceiverAndVectorDescriptor::
                  LookupStartObjectRegister(),  // lookup start
              Constant<Name>(1),                // name
              IndexAsSmi(2),                    // slot
              FeedbackVector());                // vector
}
void BaselineCompiler::VisitLdaKeyedProperty() {
  CallBuiltin(Builtins::kKeyedLoadIC,
              RegisterOperand(0),               // object
              kInterpreterAccumulatorRegister,  // key
              IndexAsSmi(1),                    // slot
              FeedbackVector());                // vector
}

void BaselineCompiler::VisitLdaModuleVariable() {
  __ LoadContext(kScratchRegister);
  int depth = Uint(1);
  for (; depth > 0; --depth) {
    __ LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                              Context::kPreviousOffset);
  }
  __ LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                            Context::kExtensionOffset);
  int cell_index = Int(0);
  if (cell_index > 0) {
    __ LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                              SourceTextModule::kRegularExportsOffset);
    // The actual array index is (cell_index - 1).
    cell_index -= 1;
  } else {
    __ LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                              SourceTextModule::kRegularImportsOffset);
    // The actual array index is (-cell_index - 1).
    cell_index = -cell_index - 1;
  }
  __ LoadFixedArrayElement(kScratchRegister, kScratchRegister, cell_index);
  __ LoadTaggedAnyField(kInterpreterAccumulatorRegister, kScratchRegister,
                        Cell::kValueOffset);
}

void BaselineCompiler::VisitStaModuleVariable() {
  __ LoadContext(kScratchRegister);
  int depth = Uint(1);
  for (; depth > 0; --depth) {
    __ LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                              Context::kPreviousOffset);
  }
  __ LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                            Context::kExtensionOffset);
  int cell_index = Int(0);
  if (cell_index > 0) {
    __ LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                              SourceTextModule::kRegularExportsOffset);
    // The actual array index is (cell_index - 1).
    cell_index -= 1;
    __ LoadFixedArrayElement(kScratchRegister, kScratchRegister, cell_index);
    BaselineAssembler::SaveAccumulatorScope save_accumulator(&masm_);
    __ StoreTaggedFieldWithWriteBarrier(kScratchRegister, Cell::kValueOffset,
                                        kInterpreterAccumulatorRegister, r11);
  } else {
    // Not supported (probably never).
    CallRuntime(Runtime::kAbort,
                Smi::FromInt(static_cast<int>(
                    AbortReason::kUnsupportedModuleOperation)));
    __ Trap();
  }
}

void BaselineCompiler::VisitStaNamedProperty() {
  CallBuiltin(Builtins::kStoreIC,
              RegisterOperand(0),               // object
              Constant<Name>(1),                // name
              kInterpreterAccumulatorRegister,  // value
              IndexAsTagged(2),                 // slot
              FeedbackVector());                // vector
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
  CallBuiltin(Builtins::kKeyedStoreIC,
              RegisterOperand(0),               // object
              RegisterOperand(1),               // key
              kInterpreterAccumulatorRegister,  // value
              IndexAsTagged(2),                 // slot
              FeedbackVector());                // vector
}
void BaselineCompiler::VisitStaInArrayLiteral() {
  CallBuiltin(Builtins::kStoreInArrayLiteralIC,
              RegisterOperand(0),               // object
              RegisterOperand(1),               // name
              kInterpreterAccumulatorRegister,  // value
              IndexAsTagged(2),                 // slot
              FeedbackVector());                // vector
}
void BaselineCompiler::VisitStaDataPropertyInLiteral() {
  CallRuntime(Runtime::kDefineDataPropertyInLiteral,
              RegisterOperand(0),               // object
              RegisterOperand(1),               // name
              kInterpreterAccumulatorRegister,  // value
              FlagAsSmi(2),                     // flags
              FeedbackVector(),                 // feedback vector
              IndexAsTagged(3));                // slot
}
void BaselineCompiler::VisitCollectTypeProfile() {
  CallRuntime(Runtime::kCollectTypeProfile,
              IntAsSmi(0),                      // position
              kInterpreterAccumulatorRegister,  // value
              FeedbackVector());                // feedback vector
}
void BaselineCompiler::BuildBinop(
    Builtins::Name builtin_name, bool fast_path, bool check_overflow,
    std::function<void(Register, Register)> instruction) {
  __ RecordComment("[ Binop");
  Register left = kInterpreterAccumulatorRegister;
  Register right =
      Builtins::CallInterfaceDescriptorFor(builtin_name)
          .GetRegisterParameter(BinaryOp_WithFeedbackDescriptor::kRight);
  __ Move(right, kInterpreterAccumulatorRegister);
  LoadRegister(left, 0);

  // Fast path for Smi.
  Label builtin, done, builtin_reload_left;
  if (fast_path) {
    __ movl(rcx, right);
    __ orl(rcx, left);
    __ JumpIfNotSmi(rcx, &builtin);

    instruction(left, right);
    if (check_overflow) __ j(overflow, &builtin_reload_left);

    UpdateFeedback(1, BinaryOperationFeedback::kSignedSmall, &done);

    if (check_overflow) {
      __ bind(&builtin_reload_left);
      LoadRegister(left, 0);
    }
    __ bind(&builtin);
  }

  CallBuiltin(builtin_name, left,   // lhs
              right,                // rhs
              Immediate(Index(1)),  // slot
              FeedbackVector());    // vector
  __ bind(&done);
  __ RecordComment("]");
}
void BaselineCompiler::VisitAdd() {
  BuildBinop(Builtins::kAdd_WithFeedback, true, true,
             [&](Register lhs, Register rhs) { __ AddSmi(lhs, rhs); });
}
void BaselineCompiler::VisitSub() {
  BuildBinop(Builtins::kSubtract_WithFeedback, true, true,
             [&](Register lhs, Register rhs) { __ SubSmi(lhs, rhs); });
}
void BaselineCompiler::VisitMul() {
  // Fast path disabled for now since mull nukes rdx which is the fbv reg.
  BuildBinop(Builtins::kMultiply_WithFeedback, false, true,
             [&](Register lhs, Register rhs) { __ MulSmi(lhs, rhs); });
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
  BuildBinop(Builtins::kBitwiseOr_WithFeedback, false, true,
             [&](Register lhs, Register rhs) { __ BitwiseOrSmi(lhs, rhs); });
}
void BaselineCompiler::VisitBitwiseXor() {
  BuildBinop(Builtins::kBitwiseXor_WithFeedback, false, true,
             [&](Register lhs, Register rhs) { __ BitwiseXorSmi(lhs, rhs); });
}
void BaselineCompiler::VisitBitwiseAnd() {
  BuildBinop(Builtins::kBitwiseAnd_WithFeedback, false, true,
             [&](Register lhs, Register rhs) { __ BitwiseAndSmi(lhs, rhs); });
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
void BaselineCompiler::UpdateFeedback(int operand_index, int bit, Label* done) {
  Register feedback_vector = kInterpreterBytecodeArrayRegister;
  __ RecordComment("[ Update Feedback");
  // TODO(verwaest): Directly test the right bit in the feedback.
  int32_t slot_offset = FeedbackVector::kRawFeedbackSlotsOffset +
                        Index(operand_index) * kTaggedSize;
  LoadFeedbackVector(feedback_vector);
  __ LoadTaggedSignedField(kScratchRegister, feedback_vector, slot_offset);
  __ SmiUntag(kScratchRegister);
  __ testb(kScratchRegister, Immediate(bit));
  __ j(not_zero, done);
  __ orq(kScratchRegister, Immediate(bit));
  __ SmiTag(kScratchRegister);
  __ StoreTaggedFieldNoWriteBarrier(feedback_vector, slot_offset,
                                    kScratchRegister);
  __ jmp(done);
  __ RecordComment("]");
}

void BaselineCompiler::BuildBinopWithConstant(
    Builtins::Name builtin_name, bool fast_path, bool check_overflow,
    std::function<void(Register, int32_t)> instruction) {
  // Fast path for Smi.
  Label builtin, builtin_restore_accumulator, done;
  if (fast_path) {
    __ JumpIfNotSmi(kInterpreterAccumulatorRegister, &builtin);
    // Backup the accumulator in case we want to jump to the builtin.
    if (check_overflow) {
      __ movl(rcx, kInterpreterAccumulatorRegister);
    }

    instruction(kInterpreterAccumulatorRegister, Int(0));
    if (check_overflow) {
      __ j(overflow, &builtin_restore_accumulator, Label::kNear);
    }

    UpdateFeedback(1, BinaryOperationFeedback::kSignedSmall, &done);

    if (check_overflow) {
      __ bind(&builtin_restore_accumulator);
      __ movl(kInterpreterAccumulatorRegister, rcx);
    }
    __ bind(&builtin);
  }
  CallBuiltin(builtin_name, kInterpreterAccumulatorRegister,  // lhs
              IntAsSmi(0),                                    // rhs
              Immediate(Index(1)),                            // slot
              FeedbackVector());                              // vector
  if (fast_path) {
    __ bind(&done);
  }
}
void BaselineCompiler::VisitAddSmi() {
  BuildBinopWithConstant(
      Builtins::kAdd_WithFeedback, true, true,
      [&](Register lhs, int32_t rhs) { __ AddSmi(lhs, Smi::FromInt(rhs)); });
}
void BaselineCompiler::VisitSubSmi() {
  BuildBinopWithConstant(
      Builtins::kSubtract_WithFeedback, true, true,
      [&](Register lhs, int32_t rhs) { __ SubSmi(lhs, Smi::FromInt(rhs)); });
}
void BaselineCompiler::VisitMulSmi() {
  BuildBinopWithConstant(Builtins::kMultiply_WithFeedback);
}
void BaselineCompiler::VisitDivSmi() {
  BuildBinopWithConstant(Builtins::kDivide_WithFeedback);
}
void BaselineCompiler::VisitModSmi() {
  BuildBinopWithConstant(Builtins::kModulus_WithFeedback);
}
void BaselineCompiler::VisitExpSmi() {
  BuildBinopWithConstant(Builtins::kExponentiate_WithFeedback);
}
void BaselineCompiler::VisitBitwiseOrSmi() {
  BuildBinopWithConstant(Builtins::kBitwiseOr_WithFeedback, false, true,
                         [&](Register lhs, int32_t rhs) {
                           __ BitwiseOrSmi(lhs, Smi::FromInt(rhs));
                         });
}
void BaselineCompiler::VisitBitwiseXorSmi() {
  BuildBinopWithConstant(Builtins::kBitwiseXor_WithFeedback, false, true,
                         [&](Register lhs, int32_t rhs) {
                           __ BitwiseXorSmi(lhs, Smi::FromInt(rhs));
                         });
}
void BaselineCompiler::VisitBitwiseAndSmi() {
  BuildBinopWithConstant(Builtins::kBitwiseAnd_WithFeedback, false, true,
                         [&](Register lhs, int32_t rhs) {
                           __ BitwiseAndSmi(lhs, Smi::FromInt(rhs));
                         });
}
void BaselineCompiler::VisitShiftLeftSmi() {
  BuildBinopWithConstant(
      Builtins::kShiftLeft_WithFeedback, false, true,
      [&](Register lhs, int32_t rhs) { __ ShiftLeftSmi(lhs, rhs); });
}
void BaselineCompiler::VisitShiftRightSmi() {
  BuildBinopWithConstant(
      Builtins::kShiftRight_WithFeedback, false, true,
      [&](Register lhs, int32_t rhs) { __ ShiftRightSmi(lhs, rhs); });
}
void BaselineCompiler::VisitShiftRightLogicalSmi() {
  BuildBinopWithConstant(
      Builtins::kShiftRightLogical_WithFeedback, false, true,
      [&](Register lhs, int32_t rhs) { __ ShiftRightLogicalSmi(lhs, rhs); });
}
void BaselineCompiler::BuildUnop(Builtins::Name builtin_name) {
  CallBuiltin(builtin_name,
              kInterpreterAccumulatorRegister,  // value
              Immediate(Index(0)),              // slot
              FeedbackVector());                // vector
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
  __ Move(kScratchRegister, kInterpreterAccumulatorRegister);
  CallBuiltin(Builtins::kDeleteProperty, RegisterOperand(0), kScratchRegister,
              Smi::FromEnum(LanguageMode::kStrict));
}
void BaselineCompiler::VisitDeletePropertySloppy() {
  __ Move(kScratchRegister, kInterpreterAccumulatorRegister);
  CallBuiltin(Builtins::kDeleteProperty, RegisterOperand(0), kScratchRegister,
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
  CallBuiltin(builtin,
              RegisterOperand(0),    // kFunction
              Immediate(arg_count),  // kActualArgumentsCount
              Immediate(slot),       // kSlot
              FeedbackVector(),      // kMaybeFeedbackVector
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
  BuildCall(ConvertReceiverMode::kNullOrUndefined, Index(3), arg_count,
            RootIndex::kUndefinedValue, args);
}
void BaselineCompiler::VisitCallUndefinedReceiver0() {
  BuildCall(ConvertReceiverMode::kNullOrUndefined, Index(1), 0,
            RootIndex::kUndefinedValue);
}
void BaselineCompiler::VisitCallUndefinedReceiver1() {
  BuildCall(ConvertReceiverMode::kNullOrUndefined, Index(2), 1,
            RootIndex::kUndefinedValue, RegisterOperand(1));
}
void BaselineCompiler::VisitCallUndefinedReceiver2() {
  BuildCall(ConvertReceiverMode::kNullOrUndefined, Index(3), 2,
            RootIndex::kUndefinedValue, RegisterOperand(1), RegisterOperand(2));
}
void BaselineCompiler::VisitCallNoFeedback() {
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);
  uint32_t arg_count = args.register_count();
  CallBuiltin(Builtins::kCall_ReceiverIsAny,
              RegisterOperand(0),        // kFunction
              Immediate(arg_count - 1),  // kActualArgumentsCount
              args);
}
void BaselineCompiler::VisitCallWithSpread() {
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);

  // Do not push the spread argument
  interpreter::Register spread_register = args.last_register();
  args = args.Truncate(args.register_count() - 1);

  uint32_t arg_count = args.register_count() - 1;  // Remove receiver.

  CallBuiltin(Builtins::kCallWithSpread_WithFeedback,
              RegisterOperand(0),    // kFunction
              Immediate(arg_count),  // kActualArgumentsCount
              spread_register,       // kSpread
              Immediate(Index(3)),   // kSlot
              FeedbackVector(),      // kMaybeFeedbackVector
              args);
}
void BaselineCompiler::VisitCallRuntime() {
  CallRuntime(accessor().GetRuntimeIdOperand(0),
              accessor().GetRegisterListOperand(1));
}
void BaselineCompiler::VisitCallRuntimeForPair() {
  CallRuntime(accessor().GetRuntimeIdOperand(0),
              accessor().GetRegisterListOperand(1));
  StoreRegisterPair(3, kReturnRegister0, kReturnRegister1);
}
void BaselineCompiler::VisitCallJSRuntime() {
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);
  uint32_t arg_count = args.register_count();

  // Load context for LoadNativeContextSlot.
  __ LoadContext(kContextRegister);
  __ LoadNativeContextSlot(accessor().GetNativeContextIndexOperand(0),
                           kJavaScriptCallTargetRegister);
  CallBuiltin(Builtins::kCall_ReceiverIsNullOrUndefined,
              kJavaScriptCallTargetRegister,  // kFunction
              Immediate(arg_count),           // kActualArgumentsCount
              RootIndex::kUndefinedValue,     // kReceiver
              args);
}

void BaselineCompiler::VisitInvokeIntrinsic() {
  Runtime::FunctionId intrinsic_id = accessor().GetIntrinsicIdOperand(0);
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);
  switch (intrinsic_id) {
#define CASE(Name, ...)         \
  case Runtime::kInline##Name:  \
    VisitIntrinsic##Name(args); \
    break;
    INTRINSICS_LIST(CASE)
#undef CASE

    default:
      UNREACHABLE();
  }
}

void BaselineCompiler::VisitIntrinsicIsJSReceiver(
    interpreter::RegisterList args) {
  __ LoadRegister(kInterpreterAccumulatorRegister, args[0]);

  Label is_smi, not_receiver, done;
  __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);

  __ CmpInstanceType(kInterpreterAccumulatorRegister, FIRST_JS_RECEIVER_TYPE);
  __ j(less, &not_receiver);
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
  __ j(always, &done);

  __ bind(&is_smi);
  __ bind(&not_receiver);
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
  __ bind(&done);
}

void BaselineCompiler::VisitIntrinsicIsArray(interpreter::RegisterList args) {
  __ LoadRegister(kInterpreterAccumulatorRegister, args[0]);

  Label is_smi, not_array, done;
  __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);

  __ CmpInstanceType(kInterpreterAccumulatorRegister, JS_ARRAY_TYPE);
  __ j(not_equal, &not_array);
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
  __ j(always, &done);

  __ bind(&is_smi);
  __ bind(&not_array);
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
  __ bind(&done);
}

void BaselineCompiler::VisitIntrinsicIsSmi(interpreter::RegisterList args) {
  __ LoadRegister(kInterpreterAccumulatorRegister, args[0]);
  Condition smi = __ CheckSmi(kInterpreterAccumulatorRegister);
  SelectBooleanConstant(smi, kInterpreterAccumulatorRegister);
}

void BaselineCompiler::VisitIntrinsicCopyDataProperties(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kCopyDataProperties, args);
}

void BaselineCompiler::VisitIntrinsicCreateIterResultObject(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kCreateIterResultObject, args);
}

void BaselineCompiler::VisitIntrinsicHasProperty(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kHasProperty, args);
}

void BaselineCompiler::VisitIntrinsicToString(interpreter::RegisterList args) {
  CallBuiltin(Builtins::kToString, args);
}

void BaselineCompiler::VisitIntrinsicToLength(interpreter::RegisterList args) {
  CallBuiltin(Builtins::kToLength, args);
}

void BaselineCompiler::VisitIntrinsicToObject(interpreter::RegisterList args) {
  CallBuiltin(Builtins::kToObject, args);
}

void BaselineCompiler::VisitIntrinsicCall(interpreter::RegisterList args) {
  // First argument register contains the function target.
  __ LoadRegister(kJavaScriptCallTargetRegister, args.first_register());

  // The arguments for the target function are from the second runtime call
  // argument.
  args = args.PopLeft();

  uint32_t arg_count = args.register_count();
  CallBuiltin(Builtins::kCall_ReceiverIsAny,
              kJavaScriptCallTargetRegister,  // kFunction
              Immediate(arg_count - 1),       // kActualArgumentsCount
              args);
}

void BaselineCompiler::VisitIntrinsicCreateAsyncFromSyncIterator(
    interpreter::RegisterList args) {
  // TODO(leszeks): Add fast-path.
  CallRuntime(Runtime::kCreateAsyncFromSyncIterator, args);
}

void BaselineCompiler::VisitIntrinsicCreateJSGeneratorObject(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kCreateGeneratorObject, args);
}

void BaselineCompiler::VisitIntrinsicGeneratorGetResumeMode(
    interpreter::RegisterList args) {
  __ LoadRegister(kInterpreterAccumulatorRegister, args[0]);
  __ LoadTaggedAnyField(kInterpreterAccumulatorRegister,
                        kInterpreterAccumulatorRegister,
                        JSGeneratorObject::kResumeModeOffset);
}

void BaselineCompiler::VisitIntrinsicGeneratorClose(
    interpreter::RegisterList args) {
  __ LoadRegister(kInterpreterAccumulatorRegister, args[0]);
  __ StoreTaggedSignedField(kInterpreterAccumulatorRegister,
                            JSGeneratorObject::kContinuationOffset,
                            Smi::FromInt(JSGeneratorObject::kGeneratorClosed));
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
}

void BaselineCompiler::VisitIntrinsicGetImportMetaObject(
    interpreter::RegisterList args) {
  // TODO(leszeks): Add fast-path.
  CallRuntime(Runtime::kGetImportMetaObject, args);
}

void BaselineCompiler::VisitIntrinsicAsyncFunctionAwaitCaught(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kAsyncFunctionAwaitCaught, args);
}

void BaselineCompiler::VisitIntrinsicAsyncFunctionAwaitUncaught(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kAsyncFunctionAwaitUncaught, args);
}

void BaselineCompiler::VisitIntrinsicAsyncFunctionEnter(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kAsyncFunctionEnter, args);
}

void BaselineCompiler::VisitIntrinsicAsyncFunctionReject(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kAsyncFunctionReject, args);
}

void BaselineCompiler::VisitIntrinsicAsyncFunctionResolve(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kAsyncFunctionResolve, args);
}

void BaselineCompiler::VisitIntrinsicAsyncGeneratorAwaitCaught(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kAsyncGeneratorAwaitCaught, args);
}

void BaselineCompiler::VisitIntrinsicAsyncGeneratorAwaitUncaught(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kAsyncGeneratorAwaitUncaught, args);
}

void BaselineCompiler::VisitIntrinsicAsyncGeneratorReject(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kAsyncGeneratorReject, args);
}

void BaselineCompiler::VisitIntrinsicAsyncGeneratorResolve(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kAsyncGeneratorResolve, args);
}

void BaselineCompiler::VisitIntrinsicAsyncGeneratorYield(
    interpreter::RegisterList args) {
  CallBuiltin(Builtins::kAsyncGeneratorYield, args);
}

void BaselineCompiler::VisitConstruct() {
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);
  uint32_t arg_count = args.register_count();
  CallBuiltin(Builtins::kConstruct_WithFeedback,
              RegisterOperand(0),               // kFunction
              kInterpreterAccumulatorRegister,  // kNewTarget
              Immediate(arg_count),             // kActualArgumentsCount
              Immediate(Index(3)),              // kSlot
              FeedbackVector(),                 // kMaybeFeedbackVector
              RootIndex::kUndefinedValue,       // kReceiver
              args);
}
void BaselineCompiler::VisitConstructWithSpread() {
  interpreter::RegisterList args = accessor().GetRegisterListOperand(1);

  // Do not push the spread argument
  interpreter::Register spread_register = args.last_register();
  args = args.Truncate(args.register_count() - 1);

  uint32_t arg_count = args.register_count();

  Register new_target =
      Builtins::CallInterfaceDescriptorFor(
          Builtins::kConstructWithSpread_WithFeedback)
          .GetRegisterParameter(
              ConstructWithSpread_WithFeedbackDescriptor::kNewTarget);
  __ Move(new_target, kInterpreterAccumulatorRegister);

  CallBuiltin(Builtins::kConstructWithSpread_WithFeedback,
              RegisterOperand(0),          // kFunction
              new_target,                  // kNewTarget
              Immediate(arg_count),        // kActualArgumentsCount
              Immediate(Index(3)),         // kSlot
              FeedbackVector(),            // kMaybeFeedbackVector
              spread_register,             // kSpread
              RootIndex::kUndefinedValue,  // kReceiver
              args);
}
void BaselineCompiler::BuildCompare(Builtins::Name builtin_name,
                                    Condition condition) {
  LoadRegister(kScratchRegister, 0);

  // Fast path for Smi.
  Label builtin, done;
  __ movl(rcx, kScratchRegister);
  __ orl(rcx, kInterpreterAccumulatorRegister);
  __ JumpIfNotSmi(rcx, &builtin);

  __ SmiCompare(kScratchRegister, kInterpreterAccumulatorRegister);
  SelectBooleanConstant(condition, kInterpreterAccumulatorRegister);

  UpdateFeedback(1, CompareOperationFeedback::kSignedSmall, &done);

  __ bind(&builtin);

  Register right =
      Builtins::CallInterfaceDescriptorFor(builtin_name)
          .GetRegisterParameter(Compare_WithFeedbackDescriptor::kRight);
  __ Move(right, kInterpreterAccumulatorRegister);
  CallBuiltin(builtin_name, kScratchRegister,  // lhs
              right,                           // rhs
              Immediate(Index(1)),             // slot
              FeedbackVector());               // vector

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
  __ cmp_tagged(__ RegisterFrameOperand(RegisterOperand(0)),
                kInterpreterAccumulatorRegister);
  SelectBooleanConstant(equal, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitTestInstanceOf() {
  Register callable =
      Builtins::CallInterfaceDescriptorFor(Builtins::kInstanceOf_WithFeedback)
          .GetRegisterParameter(Compare_WithFeedbackDescriptor::kRight);
  __ Move(callable, kInterpreterAccumulatorRegister);
  CallBuiltin(Builtins::kInstanceOf_WithFeedback,
              RegisterOperand(0),   // object
              callable,             // callable
              Immediate(Index(1)),  // slot
              FeedbackVector());    // feedback vector
}
void BaselineCompiler::VisitTestIn() {
  CallBuiltin(Builtins::kKeyedHasIC, kInterpreterAccumulatorRegister,  // object
              RegisterOperand(0),                                      // name
              IndexAsSmi(1),                                           // slot
              FeedbackVector());  // feedback vector
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
#undef TYPEOF_FLAG_VALUE
#undef TYPEOF_CASE
}
void BaselineCompiler::VisitToName() {
  BaselineAssembler::SaveAccumulatorScope save_accumulator(&masm_);
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
  BaselineAssembler::SaveAccumulatorScope save_accumulator(&masm_);
  CallBuiltin(Builtins::kToObject, kInterpreterAccumulatorRegister);
  StoreRegister(0, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitToString() {
  // TODO(verwaest): Add fast inline path if it's already a string.
  CallBuiltin(Builtins::kToString, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitCreateRegExpLiteral() {
  CallBuiltin(Builtins::kCreateRegExpLiteral,
              FeedbackVector(),         // feedback vector
              IndexAsTagged(1),         // slot
              Constant<HeapObject>(0),  // pattern
              FlagAsSmi(2));            // flags
}
void BaselineCompiler::VisitCreateArrayLiteral() {
  uint32_t flags = Flag(2);
  if (flags &
      interpreter::CreateArrayLiteralFlags::FastCloneSupportedBit::kMask) {
    CallBuiltin(Builtins::kCreateShallowArrayLiteral,
                FeedbackVector(),          // feedback vector
                IndexAsTagged(1),          // slot
                Constant<HeapObject>(0));  // constant elements
  } else {
    int32_t flags_raw = static_cast<int32_t>(
        interpreter::CreateArrayLiteralFlags::FlagsBits::decode(flags));
    CallRuntime(Runtime::kCreateArrayLiteral,
                FeedbackVector(),          // feedback vector
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
  CallBuiltin(Builtins::kCreateEmptyArrayLiteral, FeedbackVector(),
              IndexAsTagged(0));
}
void BaselineCompiler::VisitCreateObjectLiteral() {
  uint32_t flags = Flag(2);
  int32_t flags_raw = static_cast<int32_t>(
      interpreter::CreateObjectLiteralFlags::FlagsBits::decode(flags));
  if (flags &
      interpreter::CreateObjectLiteralFlags::FastCloneSupportedBit::kMask) {
    CallBuiltin(Builtins::kCreateShallowObjectLiteral,
                FeedbackVector(),                           // feedback vector
                IndexAsTagged(1),                           // slot
                Constant<ObjectBoilerplateDescription>(0),  // boilerplate
                Smi::FromInt(flags_raw));                   // flags
  } else {
    CallRuntime(Runtime::kCreateObjectLiteral,
                FeedbackVector(),                           // feedback vector
                IndexAsTagged(1),                           // slot
                Constant<ObjectBoilerplateDescription>(0),  // boilerplate
                Smi::FromInt(flags_raw));                   // flags
  }
}
void BaselineCompiler::VisitCreateEmptyObjectLiteral() {
  CallBuiltin(Builtins::kCreateEmptyLiteralObject);
}
void BaselineCompiler::VisitCloneObject() {
  uint32_t flags = Flag(1);
  int32_t raw_flags =
      interpreter::CreateObjectLiteralFlags::FlagsBits::decode(flags);
  CallBuiltin(Builtins::kCloneObjectIC,
              RegisterOperand(0),       // source
              Smi::FromInt(raw_flags),  // flags
              IndexAsTagged(2),         // slot
              FeedbackVector());        // feedback_vector
}
void BaselineCompiler::VisitGetTemplateObject() {
  // TODO(leszeks): Just use the actual SFI.
  __ LoadFunction(kScratchRegister);
  __ LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                            JSFunction::kSharedFunctionInfoOffset);
  CallBuiltin(Builtins::kGetTemplateObject,
              kScratchRegister,         // shared function info
              Constant<HeapObject>(0),  // description
              Immediate(Index(1)),      // slot
              FeedbackVector());        // feedback_vector
}
void BaselineCompiler::VisitCreateClosure() {
  // TODO(verwaest): Use the feedback cell register expected by the builtin.
  __ LoadFunction(kScratchRegister);
  LoadClosureFeedbackArray(r11, kScratchRegister);

  uint32_t flags = Flag(2);
  if (interpreter::CreateClosureFlags::FastNewClosureBit::decode(flags)) {
    __ LoadFixedArrayElement(kScratchRegister, r11, Index(1));
    CallBuiltin(Builtins::kFastNewClosure, Constant<SharedFunctionInfo>(0),
                kScratchRegister);
  } else {
    Runtime::FunctionId function_id =
        interpreter::CreateClosureFlags::PretenuredBit::decode(flags)
            ? Runtime::kNewClosure_Tenured
            : Runtime::kNewClosure;
    __ LoadFixedArrayElement(kInterpreterAccumulatorRegister, r11, Index(1));
    CallRuntime(function_id, Constant<SharedFunctionInfo>(0),
                kInterpreterAccumulatorRegister);
  }
}
void BaselineCompiler::VisitCreateBlockContext() {
  CallRuntime(Runtime::kPushBlockContext, Constant<ScopeInfo>(0));
}
void BaselineCompiler::VisitCreateCatchContext() {
  CallRuntime(Runtime::kPushCatchContext,
              RegisterOperand(0),  // exception
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
  CallRuntime(Runtime::kPushWithContext,
              RegisterOperand(0),  // object
              Constant<ScopeInfo>(1));
}
void BaselineCompiler::VisitCreateMappedArguments() {
  // Check for duplicate parameters.
  Label done, call_builtin;
  __ LoadFunction(kScratchRegister);
  __ LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                            JSFunction::kSharedFunctionInfoOffset);
  __ LoadTaggedPointerField(kScratchRegister, kScratchRegister,
                            SharedFunctionInfo::kFlagsOffset);
  __ testq(kScratchRegister,
           Immediate(SharedFunctionInfo::HasDuplicateParametersBit::kMask));
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
  Label osr_not_armed;
  __ RecordComment("[ OSR Check Armed");
  Register osr_level = kScratchRegister;
  __ LoadRegister(osr_level, interpreter::Register::bytecode_array());
  __ movb(osr_level,
          FieldOperand(osr_level, BytecodeArray::kOsrNestingLevelOffset));
  int loop_depth = accessor().GetImmediateOperand(1);
  __ cmpb(osr_level, Immediate(loop_depth));
  __ j(below_equal, &osr_not_armed);
  CallBuiltin(Builtins::kBaselineOnStackReplacement);
  __ RecordComment("]");

  __ bind(&osr_not_armed);
  Label* label = &unlinked_labels_[accessor().GetJumpTargetOffset()];
  int weight = accessor().GetRelativeJumpTargetOffset();
  DCHECK_EQ(unlinked_labels_.count(accessor().GetJumpTargetOffset()), 1);
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
  Label is_smi, dont_jump;
  __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);

  __ CmpInstanceType(kInterpreterAccumulatorRegister, FIRST_JS_RECEIVER_TYPE);
  __ j(less, &dont_jump);
  UpdateInterruptBudgetAndDoInterpreterJump();

  __ bind(&is_smi);
  __ bind(&dont_jump);
}
void BaselineCompiler::VisitSwitchOnSmiNoFeedback() {
  interpreter::JumpTableTargetOffsets offsets =
      accessor().GetJumpTableTargetOffsets();

  if (offsets.size() == 0) return;

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
  CallBuiltin(Builtins::kForInPrepare, kInterpreterAccumulatorRegister,
              IndexAsTagged(1), FeedbackVector());
  interpreter::Register first = accessor().GetRegisterOperand(0);
  interpreter::Register second(first.index() + 1);
  interpreter::Register third(first.index() + 2);
  __ StoreRegister(second, kReturnRegister0);
  __ StoreRegister(third, kReturnRegister1);
}
void BaselineCompiler::VisitForInContinue() {
  LoadRegister(kInterpreterAccumulatorRegister, 0);
  LoadRegister(kScratchRegister, 1);
  __ cmp_tagged(kInterpreterAccumulatorRegister,
                __ RegisterFrameOperand(RegisterOperand(1)));
  SelectBooleanConstant(not_equal, kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitForInNext() {
  interpreter::Register cache_type, cache_array;
  std::tie(cache_type, cache_array) = accessor().GetRegisterPairOperand(2);
  CallBuiltin(Builtins::kForInNext,
              Immediate(Index(3)),  // vector slot
              RegisterOperand(0),   // object
              cache_array,          // cache array
              cache_type,           // cache type
              RegisterOperand(1),   // index
              FeedbackVector());    // feedback vector
}
void BaselineCompiler::VisitForInStep() {
  LoadRegister(kInterpreterAccumulatorRegister, 0);
  __ AddSmi(kInterpreterAccumulatorRegister, Smi::FromInt(1));
}
void BaselineCompiler::VisitSetPendingMessage() {
  __ Move(kScratchRegister,
          ExternalReference::address_of_pending_message_obj(isolate_));
  __ movq(rcx, kInterpreterAccumulatorRegister);
  __ movq(kInterpreterAccumulatorRegister, MemOperand(kScratchRegister, 0));
  __ movq(MemOperand(kScratchRegister, 0), rcx);
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
  int profiling_weight = accessor().current_offset();
  Label finished_interrupt_budget_update;
  UpdateInterruptBudgetAndJumpToLabel(-profiling_weight, nullptr,
                                      &finished_interrupt_budget_update);
  __ bind(&finished_interrupt_budget_update);
  __ RecordComment("[ Return");
  int parameter_count = bytecode_->parameter_count();

  // We must pop all arguments from the stack (including the receiver). This
  // number of arguments is given by max(1 + argc_reg, parameter_count).
  int parameter_count_without_receiver =
      parameter_count - 1;  // Exclude the receiver to simplify the
                            // computation. We'll account for it at the end.
  TailCallBuiltin(
      Builtins::kBaselineLeaveFrame,
      Immediate(parameter_count_without_receiver));
  __ RecordComment("]");
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
  __ JumpIfRoot(generator_object, RootIndex::kUndefinedValue, &fallthrough);

  __ LoadTaggedAnyField(r11, generator_object,
                        JSGeneratorObject::kContinuationOffset);
  __ StoreTaggedSignedField(
      generator_object, JSGeneratorObject::kContinuationOffset,
      Smi::FromInt(JSGeneratorObject::kGeneratorExecuting));

  __ LoadTaggedAnyField(kScratchRegister, generator_object,
                        JSGeneratorObject::kContextOffset);
  __ StoreContext(kScratchRegister);

  interpreter::JumpTableTargetOffsets offsets =
      accessor().GetJumpTableTargetOffsets();

  if (0 < offsets.size()) {
    DCHECK_EQ(0, (*offsets.begin()).case_value);

    std::unique_ptr<Label*[]> labels =
        std::make_unique<Label*[]>(offsets.size());
    for (const interpreter::JumpTableTargetOffset& offset : offsets) {
      labels[offset.case_value] = &unlinked_labels_[offset.target_offset];
    }
    __ SmiUntag(r11);
    __ Switch(r11, 0, labels.get(), offsets.size());
    // We should never fall through this switch.
    // TODO(leszeks): Maybe remove the fallthrough check in the Switch?
    __ Trap();
  }

  __ bind(&fallthrough);
}

void BaselineCompiler::VisitSuspendGenerator() {
  DCHECK_EQ(accessor().GetRegisterOperand(1), interpreter::Register(0));
  int register_count = RegisterCount(2);
  uint32_t suspend_id = Uint(3);

  Register generator_object = r12, parameters_and_registers_array = r11;
  LoadRegister(generator_object, 0);
  __ LoadTaggedPointerField(parameters_and_registers_array, generator_object,
                            JSGeneratorObject::kParametersAndRegistersOffset);

  for (int i = 0; i < formal_parameter_count_; ++i) {
    __ LoadRegister(kScratchRegister, interpreter::Register::FromParameterIndex(
                                          i + 1, bytecode_->parameter_count()));
    __ StoreTaggedFieldWithWriteBarrier(parameters_and_registers_array,
                                        FixedArray::OffsetOfElementAt(i),
                                        kScratchRegister, r14);
  }
  for (int i = 0; i < register_count; ++i) {
    __ LoadRegister(kScratchRegister, interpreter::Register(i));
    __ StoreTaggedFieldWithWriteBarrier(
        parameters_and_registers_array,
        FixedArray::OffsetOfElementAt(formal_parameter_count_ + i),
        kScratchRegister, r14);
  }

  __ LoadContext(kScratchRegister);
  __ StoreTaggedFieldWithWriteBarrier(generator_object,
                                      JSGeneratorObject::kContextOffset,
                                      kScratchRegister, r14);

  __ StoreTaggedSignedField(generator_object,
                            JSGeneratorObject::kContinuationOffset,
                            Smi::FromInt(suspend_id));

  __ StoreTaggedSignedField(generator_object,
                            JSGeneratorObject::kInputOrDebugPosOffset,
                            Smi::FromInt(accessor().current_offset()));
  VisitReturn();
}

void BaselineCompiler::VisitResumeGenerator() {
  DCHECK_EQ(accessor().GetRegisterOperand(1), interpreter::Register(0));
  int register_count = RegisterCount(2);

  Register generator_object = r12, parameters_and_registers_array = r11;
  LoadRegister(generator_object, 0);
  __ LoadTaggedPointerField(parameters_and_registers_array, generator_object,
                            JSGeneratorObject::kParametersAndRegistersOffset);

  for (int i = 0; i < register_count; ++i) {
    __ LoadTaggedAnyField(
        kScratchRegister, parameters_and_registers_array,
        FixedArray::OffsetOfElementAt(formal_parameter_count_ + i));
    __ StoreRegister(interpreter::Register(i), kScratchRegister);
  }

  __ LoadTaggedAnyField(kInterpreterAccumulatorRegister, generator_object,
                        JSGeneratorObject::kInputOrDebugPosOffset);
}

void BaselineCompiler::VisitGetIterator() {
  CallBuiltin(Builtins::kGetIteratorWithFeedback,
              RegisterOperand(0),  // receiver
              IndexAsTagged(1),    // load_slot
              IndexAsTagged(2),    // call_slot
              FeedbackVector());   // vector
}

void BaselineCompiler::VisitDebugger() {
  CallBuiltin(Builtins::kHandleDebuggerStatement);
}
void BaselineCompiler::VisitIncBlockCounter() {
  CallBuiltin(Builtins::kIncBlockCounter, __ FunctionOperand(),
              IndexAsSmi(0));  // coverage array slot
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

}  // namespace internal
}  // namespace v8
