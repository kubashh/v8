// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/baseline.h"

#include <unordered_map>

#include "src/builtins/builtins-descriptors.h"
#include "src/codegen/assembler.h"
#include "src/codegen/interface-descriptors.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/common/globals.h"
#include "src/interpreter/bytecode-array-accessor.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/objects/code.h"
#include "src/objects/heap-object.h"
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
};

#define __ masm_.

namespace {
template <typename... Args>
struct ArgumentSettingHelper;

template <>
struct ArgumentSettingHelper<> {
  static void Set(BaselineAssembler* masm, CallInterfaceDescriptor descriptor,
                  int i) {}
};

template <typename Arg, typename... Args>
struct ArgumentSettingHelper<Arg, Args...> {
  static void Set(BaselineAssembler* masm, CallInterfaceDescriptor descriptor,
                  int i, Arg arg, Args... args) {
    if (i < descriptor.GetRegisterParameterCount()) {
      masm->Move(descriptor.GetRegisterParameter(i), arg);
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
};

template <typename... Args>
struct RuntimeArgumentSettingHelper;

template <>
struct RuntimeArgumentSettingHelper<> {
  static void Set(BaselineAssembler* masm) {}
};

template <typename Arg, typename... Args>
struct RuntimeArgumentSettingHelper<Arg, Args...> {
  static void Set(BaselineAssembler* masm, Arg arg, Args... args) {
    masm->Push(arg);
    RuntimeArgumentSettingHelper<Args...>::Set(masm, args...);
  }
};

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
      back_jumps_[accessor().GetJumpTargetOffset()] = Label();
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
    __ int3();

    __ bind(&stack_check_);
    __ int3();
  }

  Handle<Code> Build(Isolate* isolate) {
    CodeDesc desc;
    __ GetCode(isolate, &desc);
    return Factory::CodeBuilder(isolate, desc, CodeKind::SPARKPLUG).Build();
  }

  Operand RegisterOperand(int operand_index) {
    return __ RegisterFrameOperand(
        accessor().GetRegisterOperand(operand_index));
  }
  void LoadRegister(Register output, int operand_index) {
    __ movq(output, RegisterOperand(operand_index));
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
  Smi IndexAsSmi(int operand_index) {
    return Smi::FromInt(Index(operand_index));
  }
  Smi IntAsSmi(int operand_index) { return Smi::FromInt(Int(operand_index)); }
  void LoadIndexAsSmi(Register output, int operand_index) {
    __ Move(output, IndexAsSmi(operand_index));
  }
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

 private:
  void VisitSingleBytecode() {
    // TODO(leszeks): Merge forward and back labels maps maybe.
    auto forward_jump_label = forward_jumps_.find(accessor().current_offset());
    if (forward_jump_label != forward_jumps_.end()) {
      for (auto&& label : forward_jump_label->second) {
        __ bind(&label);
      }
      forward_jumps_.erase(forward_jump_label);
    }
    auto back_jump_label = back_jumps_.find(accessor().current_offset());
    if (back_jump_label != back_jumps_.end()) {
      __ bind(&back_jump_label->second);
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

  Label* BuildJumpLabel(int bytecode_offset) {
    std::vector<Label>& labels = forward_jumps_[bytecode_offset];
    labels.emplace_back();
    return &labels.back();
  }

  template <typename... Args>
  void CallBuiltin(Builtins::Name builtin, Args... args) {
    CallInterfaceDescriptor descriptor =
        Builtins::CallInterfaceDescriptorFor(builtin);
    DCHECK_EQ(descriptor.GetParameterCount(), sizeof...(args));
    ArgumentSettingHelper<Args...>::Set(&masm_, descriptor, 0, args...);
    if (descriptor.HasContextParameter()) {
      LoadContext(descriptor.ContextRegister());
    }
    __ CallBuiltin(builtin);
  }

  template <typename... Args>
  void CallRuntime(Runtime::FunctionId functionid, Args... args) {
    RuntimeArgumentSettingHelper<Args...>::Set(&masm_, args...);
    LoadContext(kContextRegister);
    __ CallRuntime(functionid);
  }

  // Returns into kInterpreterAccumulatorRegister
  void ToBoolean(Register reg) { CallBuiltin(Builtins::kToBoolean, reg); }

  const interpreter::BytecodeArrayAccessor& accessor() { return iterator_; }

  Isolate* isolate_;
  Handle<SharedFunctionInfo> shared_;
  Handle<BytecodeArray> bytecode_;
  BaselineAssembler masm_;
  interpreter::BytecodeArrayIterator iterator_;

  std::unordered_map<int, std::vector<Label>> forward_jumps_;
  std::unordered_map<int, Label> back_jumps_;

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
void BaselineCompiler::VisitLdaGlobal() { __ int3(); }
void BaselineCompiler::VisitLdaGlobalInsideTypeof() { __ int3(); }
void BaselineCompiler::VisitStaGlobal() { __ int3(); }
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
void BaselineCompiler::VisitLdaLookupGlobalSlot() { __ int3(); }
void BaselineCompiler::VisitLdaLookupSlotInsideTypeof() { __ int3(); }
void BaselineCompiler::VisitLdaLookupContextSlotInsideTypeof() { __ int3(); }
void BaselineCompiler::VisitLdaLookupGlobalSlotInsideTypeof() { __ int3(); }
void BaselineCompiler::VisitStaLookupSlot() { __ int3(); }
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
  CallBuiltin(Builtins::kLoadIC, RegisterOperand(0), Constant<Name>(1),
              IndexAsSmi(2), LoadWithVectorDescriptor::VectorRegister());
}
void BaselineCompiler::VisitLdaNamedPropertyNoFeedback() {
  CallBuiltin(Builtins::kGetProperty, RegisterOperand(0), Constant<Name>(1));
}
void BaselineCompiler::VisitLdaNamedPropertyFromSuper() {
  LoadFeedbackVector(LoadWithVectorDescriptor::VectorRegister());

  __ LoadPrototype(
      LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister(),
      kInterpreterAccumulatorRegister);

  CallBuiltin(Builtins::kLoadSuperIC, RegisterOperand(0),
              LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister(),
              Constant<Name>(1), IndexAsSmi(2),
              LoadWithVectorDescriptor::VectorRegister());
}
void BaselineCompiler::VisitLdaKeyedProperty() {
  LoadFeedbackVector(LoadWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kKeyedLoadIC, RegisterOperand(0),
              kInterpreterAccumulatorRegister, IndexAsSmi(1),
              LoadWithVectorDescriptor::VectorRegister());
}
void BaselineCompiler::VisitLdaModuleVariable() { __ int3(); }
void BaselineCompiler::VisitStaModuleVariable() { __ int3(); }
void BaselineCompiler::VisitStaNamedProperty() {
  LoadFeedbackVector(LoadWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kStoreIC, RegisterOperand(0), Constant<Name>(1),
              kInterpreterAccumulatorRegister, IndexAsSmi(2),
              LoadWithVectorDescriptor::VectorRegister());
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
  LoadFeedbackVector(LoadWithVectorDescriptor::VectorRegister());
  CallBuiltin(Builtins::kKeyedStoreIC, RegisterOperand(0), RegisterOperand(1),
              kInterpreterAccumulatorRegister, IndexAsSmi(2),
              LoadWithVectorDescriptor::VectorRegister());
}
void BaselineCompiler::VisitStaInArrayLiteral() {
  LoadFeedbackVector(StoreWithVectorDescriptor::VectorRegister());

  LoadContext(StoreDescriptor::ContextRegister());
  LoadRegister(StoreDescriptor::ReceiverRegister(), 0);
  LoadRegister(StoreDescriptor::NameRegister(), 1);
  LoadAccumulator(StoreDescriptor::ValueRegister());
  LoadIndexAsSmi(StoreDescriptor::SlotRegister(), 2);

  __ CallBuiltin(Builtins::kStoreInArrayLiteralIC);
}
void BaselineCompiler::VisitStaDataPropertyInLiteral() {
  CallRuntime(Runtime::kDefineDataPropertyInLiteral,
              RegisterOperand(0),               // object
              RegisterOperand(1),               // name
              kInterpreterAccumulatorRegister,  // value
              IntAsSmi(2),                      // flags
              IndexAsSmi(3));                   // slot
}
void BaselineCompiler::VisitCollectTypeProfile() { __ int3(); }
void BaselineCompiler::VisitAdd() { __ int3(); }
void BaselineCompiler::VisitSub() { __ int3(); }
void BaselineCompiler::VisitMul() { __ int3(); }
void BaselineCompiler::VisitDiv() { __ int3(); }
void BaselineCompiler::VisitMod() { __ int3(); }
void BaselineCompiler::VisitExp() { __ int3(); }
void BaselineCompiler::VisitBitwiseOr() { __ int3(); }
void BaselineCompiler::VisitBitwiseXor() { __ int3(); }
void BaselineCompiler::VisitBitwiseAnd() { __ int3(); }
void BaselineCompiler::VisitShiftLeft() { __ int3(); }
void BaselineCompiler::VisitShiftRight() { __ int3(); }
void BaselineCompiler::VisitShiftRightLogical() { __ int3(); }
void BaselineCompiler::VisitAddSmi() { __ int3(); }
void BaselineCompiler::VisitSubSmi() { __ int3(); }
void BaselineCompiler::VisitMulSmi() { __ int3(); }
void BaselineCompiler::VisitDivSmi() { __ int3(); }
void BaselineCompiler::VisitModSmi() { __ int3(); }
void BaselineCompiler::VisitExpSmi() { __ int3(); }
void BaselineCompiler::VisitBitwiseOrSmi() { __ int3(); }
void BaselineCompiler::VisitBitwiseXorSmi() { __ int3(); }
void BaselineCompiler::VisitBitwiseAndSmi() { __ int3(); }
void BaselineCompiler::VisitShiftLeftSmi() { __ int3(); }
void BaselineCompiler::VisitShiftRightSmi() { __ int3(); }
void BaselineCompiler::VisitShiftRightLogicalSmi() { __ int3(); }
void BaselineCompiler::VisitInc() { __ int3(); }
void BaselineCompiler::VisitDec() { __ int3(); }
void BaselineCompiler::VisitNegate() { __ int3(); }
void BaselineCompiler::VisitBitwiseNot() { __ int3(); }
void BaselineCompiler::VisitToBooleanLogicalNot() { __ int3(); }
void BaselineCompiler::VisitLogicalNot() { __ int3(); }
void BaselineCompiler::VisitTypeOf() { __ int3(); }
void BaselineCompiler::VisitDeletePropertyStrict() { __ int3(); }
void BaselineCompiler::VisitDeletePropertySloppy() { __ int3(); }
void BaselineCompiler::VisitGetSuperConstructor() { __ int3(); }
void BaselineCompiler::VisitCallAnyReceiver() { __ int3(); }
void BaselineCompiler::VisitCallProperty() { __ int3(); }
void BaselineCompiler::VisitCallProperty0() { __ int3(); }
void BaselineCompiler::VisitCallProperty1() { __ int3(); }
void BaselineCompiler::VisitCallProperty2() { __ int3(); }
void BaselineCompiler::VisitCallUndefinedReceiver() { __ int3(); }
void BaselineCompiler::VisitCallUndefinedReceiver0() { __ int3(); }
void BaselineCompiler::VisitCallUndefinedReceiver1() { __ int3(); }
void BaselineCompiler::VisitCallUndefinedReceiver2() { __ int3(); }
void BaselineCompiler::VisitCallNoFeedback() { __ int3(); }
void BaselineCompiler::VisitCallWithSpread() { __ int3(); }
void BaselineCompiler::VisitCallRuntime() { __ int3(); }
void BaselineCompiler::VisitCallRuntimeForPair() { __ int3(); }
void BaselineCompiler::VisitCallJSRuntime() { __ int3(); }
void BaselineCompiler::VisitInvokeIntrinsic() { __ int3(); }
void BaselineCompiler::VisitConstruct() { __ int3(); }
void BaselineCompiler::VisitConstructWithSpread() { __ int3(); }
void BaselineCompiler::VisitTestEqual() { __ int3(); }
void BaselineCompiler::VisitTestEqualStrict() { __ int3(); }
void BaselineCompiler::VisitTestLessThan() { __ int3(); }
void BaselineCompiler::VisitTestGreaterThan() { __ int3(); }
void BaselineCompiler::VisitTestLessThanOrEqual() { __ int3(); }
void BaselineCompiler::VisitTestGreaterThanOrEqual() { __ int3(); }
void BaselineCompiler::VisitTestReferenceEqual() { __ int3(); }
void BaselineCompiler::VisitTestInstanceOf() { __ int3(); }
void BaselineCompiler::VisitTestIn() { __ int3(); }
void BaselineCompiler::VisitTestUndetectable() { __ int3(); }
void BaselineCompiler::VisitTestNull() { __ int3(); }
void BaselineCompiler::VisitTestUndefined() { __ int3(); }
void BaselineCompiler::VisitTestTypeOf() { __ int3(); }
void BaselineCompiler::VisitToName() { __ int3(); }
void BaselineCompiler::VisitToNumber() { __ int3(); }
void BaselineCompiler::VisitToNumeric() { __ int3(); }
void BaselineCompiler::VisitToObject() { __ int3(); }
void BaselineCompiler::VisitToString() { __ int3(); }
void BaselineCompiler::VisitCreateRegExpLiteral() { __ int3(); }
void BaselineCompiler::VisitCreateArrayLiteral() { __ int3(); }
void BaselineCompiler::VisitCreateArrayFromIterable() { __ int3(); }
void BaselineCompiler::VisitCreateEmptyArrayLiteral() { __ int3(); }
void BaselineCompiler::VisitCreateObjectLiteral() { __ int3(); }
void BaselineCompiler::VisitCreateEmptyObjectLiteral() { __ int3(); }
void BaselineCompiler::VisitCloneObject() { __ int3(); }
void BaselineCompiler::VisitGetTemplateObject() { __ int3(); }
void BaselineCompiler::VisitCreateClosure() { __ int3(); }
void BaselineCompiler::VisitCreateBlockContext() {
  CallRuntime(Runtime::kPushBlockContext, Constant<ScopeInfo>(0));
  StoreContext(kInterpreterAccumulatorRegister);
}
void BaselineCompiler::VisitCreateCatchContext() { __ int3(); }
void BaselineCompiler::VisitCreateFunctionContext() { __ int3(); }
void BaselineCompiler::VisitCreateEvalContext() { __ int3(); }
void BaselineCompiler::VisitCreateWithContext() { __ int3(); }
void BaselineCompiler::VisitCreateMappedArguments() { __ int3(); }
void BaselineCompiler::VisitCreateUnmappedArguments() { __ int3(); }
void BaselineCompiler::VisitCreateRestParameter() { __ int3(); }
void BaselineCompiler::VisitJumpLoop() {
  DCHECK(back_jumps_.count(accessor().GetJumpTargetOffset()) == 1);
  __ j(always, &back_jumps_[accessor().GetJumpTargetOffset()]);
}
void BaselineCompiler::VisitJump() {
  __ j(always, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpConstant() {
  __ j(always, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNullConstant() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kNullValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNotNullConstant() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kNullValue);
  __ j(not_equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfUndefinedConstant() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNotUndefinedConstant() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  __ j(not_equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfUndefinedOrNullConstant() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kNullValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfTrueConstant() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfFalseConstant() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfJSReceiverConstant() {
  Label is_smi;
  __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);

  __ LoadAnyTaggedField(
      kScratchRegister,
      FieldOperand(kInterpreterAccumulatorRegister, HeapObject::kMapOffset));
  __ movw(kScratchRegister,
          FieldOperand(kScratchRegister, Map::kInstanceTypeOffset));
  __ Cmp(kScratchRegister, FIRST_JS_RECEIVER_TYPE);
  __ j(greater_equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));

  __ bind(&is_smi);
}
void BaselineCompiler::VisitJumpIfToBooleanTrueConstant() {
  {
    SaveAccumulatorScope accumulator_scope(this);
    ToBoolean(kInterpreterAccumulatorRegister);
    __ Move(kScratchRegister, kInterpreterAccumulatorRegister);
    __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
  }
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfToBooleanFalseConstant() {
  {
    SaveAccumulatorScope accumulator_scope(this);
    ToBoolean(kInterpreterAccumulatorRegister);
    __ Move(kScratchRegister, kInterpreterAccumulatorRegister);
    __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
  }
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfToBooleanTrue() {
  {
    SaveAccumulatorScope accumulator_scope(this);
    ToBoolean(kInterpreterAccumulatorRegister);
    __ Move(kScratchRegister, kInterpreterAccumulatorRegister);
    __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
  }
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfToBooleanFalse() {
  {
    SaveAccumulatorScope accumulator_scope(this);
    ToBoolean(kInterpreterAccumulatorRegister);
    __ Move(kScratchRegister, kInterpreterAccumulatorRegister);
    __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
  }
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfTrue() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfFalse() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNull() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kNullValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNotNull() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kNullValue);
  __ j(not_equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfUndefined() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNotUndefined() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  __ j(not_equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfUndefinedOrNull() {
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kNullValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfJSReceiver() {
  Label is_smi;
  __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);

  __ LoadAnyTaggedField(
      kScratchRegister,
      FieldOperand(kInterpreterAccumulatorRegister, HeapObject::kMapOffset));
  __ LoadAnyTaggedField(
      kScratchRegister,
      FieldOperand(kScratchRegister, Map::kInstanceTypeOffset));
  __ Cmp(kScratchRegister, FIRST_JS_RECEIVER_TYPE);
  __ j(greater_equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));

  __ bind(&is_smi);
}
void BaselineCompiler::VisitSwitchOnSmiNoFeedback() { __ int3(); }
void BaselineCompiler::VisitForInEnumerate() { __ int3(); }
void BaselineCompiler::VisitForInPrepare() { __ int3(); }
void BaselineCompiler::VisitForInContinue() { __ int3(); }
void BaselineCompiler::VisitForInNext() { __ int3(); }
void BaselineCompiler::VisitForInStep() { __ int3(); }
void BaselineCompiler::VisitSetPendingMessage() { __ int3(); }
void BaselineCompiler::VisitThrow() { __ int3(); }
void BaselineCompiler::VisitReThrow() { __ int3(); }
void BaselineCompiler::VisitReturn() {
  Register argc_reg = rcx;

  // Get the actual argument count.
  __ movq(argc_reg, Operand(rbp, StandardFrameConstants::kArgCOffset));

  __ movq(rsp, rbp);
  __ popq(rbp);

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
void BaselineCompiler::VisitThrowReferenceErrorIfHole() { __ int3(); }
void BaselineCompiler::VisitThrowSuperNotCalledIfHole() { __ int3(); }
void BaselineCompiler::VisitThrowSuperAlreadyCalledIfNotHole() { __ int3(); }
void BaselineCompiler::VisitThrowIfNotSuperConstructor() { __ int3(); }
void BaselineCompiler::VisitSwitchOnGeneratorState() { __ int3(); }
void BaselineCompiler::VisitSuspendGenerator() { __ int3(); }
void BaselineCompiler::VisitResumeGenerator() { __ int3(); }
void BaselineCompiler::VisitGetIterator() { __ int3(); }
void BaselineCompiler::VisitDebugger() { __ int3(); }
void BaselineCompiler::VisitIncBlockCounter() {
  CallRuntime(Runtime::kInlineIncBlockCounter, __ FunctionOperand(),
              Constant<HeapObject>(0));  // coverage array slot
}
void BaselineCompiler::VisitAbort() { __ int3(); }
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