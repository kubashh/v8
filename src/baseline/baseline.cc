// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/baseline.h"

#include <type_traits>
#include <unordered_map>

#include "src/builtins/builtins-descriptors.h"
#include "src/codegen/assembler.h"
#include "src/codegen/interface-descriptors.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/common/globals.h"
#include "src/interpreter/bytecode-array-accessor.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-flags.h"
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

#ifdef DEBUG
bool Clobbers(Register target, Register reg) { return target == reg; }
bool Clobbers(Register target, Operand op) {
  return op.AddressUsesRegister(target);
}
bool Clobbers(Register target, Handle<Object> handle) { return false; }
bool Clobbers(Register target, Smi smi) { return false; }
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
    LoadContext(kContextRegister);
    ITERATE_PACK(__ Push(args));
    __ CallRuntime(functionid);
  }

  // Returns into kInterpreterAccumulatorRegister
  void ToBoolean(Register reg) { CallBuiltin(Builtins::kToBoolean, reg); }

  void BuildBinop(Builtins::Name builtin_name);
  void BuildCompare(Builtins::Name builtin_name);
  void BuildBinopWithSmi(Builtins::Name builtin_name);

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
void BaselineCompiler::VisitLdaModuleVariable() { __ int3(); }
void BaselineCompiler::VisitStaModuleVariable() { __ int3(); }
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
void BaselineCompiler::VisitCollectTypeProfile() { __ int3(); }
void BaselineCompiler::BuildBinop(Builtins::Name builtin_name) {
  Register feedback_vector =
      Builtins::CallInterfaceDescriptorFor(builtin_name)
          .GetRegisterParameter(
              BinaryOp_WithFeedbackDescriptor::kMaybeFeedbackVector);
  LoadFeedbackVector(feedback_vector);
  CallBuiltin(builtin_name, RegisterOperand(0),  // lhs
              kInterpreterAccumulatorRegister,   // rhs
              IndexAsSmi(1),                     // slot
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
void BaselineCompiler::BuildBinopWithSmi(Builtins::Name builtin_name) {
  Register feedback_vector =
      Builtins::CallInterfaceDescriptorFor(builtin_name)
          .GetRegisterParameter(
              BinaryOp_WithFeedbackDescriptor::kMaybeFeedbackVector);
  LoadFeedbackVector(feedback_vector);
  CallBuiltin(builtin_name, kInterpreterAccumulatorRegister,  // lhs
              IndexAsSmi(0),                                  // rhs
              IndexAsSmi(1),                                  // slot
              feedback_vector);                               // vector
}
void BaselineCompiler::VisitAddSmi() {
  BuildBinopWithSmi(Builtins::kAdd_WithFeedback);
}
void BaselineCompiler::VisitSubSmi() {
  BuildBinopWithSmi(Builtins::kSubtract_WithFeedback);
}
void BaselineCompiler::VisitMulSmi() {
  BuildBinopWithSmi(Builtins::kMultiply_WithFeedback);
}
void BaselineCompiler::VisitDivSmi() {
  BuildBinopWithSmi(Builtins::kDivide_WithFeedback);
}
void BaselineCompiler::VisitModSmi() {
  BuildBinopWithSmi(Builtins::kModulus_WithFeedback);
}
void BaselineCompiler::VisitExpSmi() {
  BuildBinopWithSmi(Builtins::kExponentiate_WithFeedback);
}
void BaselineCompiler::VisitBitwiseOrSmi() {
  BuildBinopWithSmi(Builtins::kBitwiseOr_WithFeedback);
}
void BaselineCompiler::VisitBitwiseXorSmi() {
  BuildBinopWithSmi(Builtins::kBitwiseXor_WithFeedback);
}
void BaselineCompiler::VisitBitwiseAndSmi() {
  BuildBinopWithSmi(Builtins::kBitwiseAnd_WithFeedback);
}
void BaselineCompiler::VisitShiftLeftSmi() {
  BuildBinopWithSmi(Builtins::kShiftLeft_WithFeedback);
}
void BaselineCompiler::VisitShiftRightSmi() {
  BuildBinopWithSmi(Builtins::kShiftRight_WithFeedback);
}
void BaselineCompiler::VisitShiftRightLogicalSmi() {
  BuildBinopWithSmi(Builtins::kShiftRightLogical_WithFeedback);
}
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
void BaselineCompiler::BuildCompare(Builtins::Name builtin_name) {
  Register feedback_vector =
      Builtins::CallInterfaceDescriptorFor(builtin_name)
          .GetRegisterParameter(
              Compare_WithFeedbackDescriptor::kMaybeFeedbackVector);
  LoadFeedbackVector(feedback_vector);
  CallBuiltin(builtin_name, RegisterOperand(0),  // lhs
              kInterpreterAccumulatorRegister,   // rhs
              IndexAsSmi(1),                     // slot
              feedback_vector);                  // vector
}
void BaselineCompiler::VisitTestEqual() {
  BuildCompare(Builtins::kEqual_WithFeedback);
}
void BaselineCompiler::VisitTestEqualStrict() {
  BuildCompare(Builtins::kStrictEqual_WithFeedback);
}
void BaselineCompiler::VisitTestLessThan() {
  BuildCompare(Builtins::kLessThan_WithFeedback);
}
void BaselineCompiler::VisitTestGreaterThan() {
  BuildCompare(Builtins::kGreaterThan_WithFeedback);
}
void BaselineCompiler::VisitTestLessThanOrEqual() {
  BuildCompare(Builtins::kLessThanOrEqual_WithFeedback);
}
void BaselineCompiler::VisitTestGreaterThanOrEqual() {
  BuildCompare(Builtins::kGreaterThanOrEqual_WithFeedback);
}
void BaselineCompiler::VisitTestReferenceEqual() {}
void BaselineCompiler::VisitTestInstanceOf() {}
void BaselineCompiler::VisitTestIn() {}
void BaselineCompiler::VisitTestUndetectable() {}
void BaselineCompiler::VisitTestNull() {}
void BaselineCompiler::VisitTestUndefined() {}
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
void BaselineCompiler::VisitThrowReferenceErrorIfHole() {
  Label done;
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kTheHoleValue);
  __ j(not_equal, &done);
  CallRuntime(Runtime::kThrowAccessedUninitializedVariable, Constant<Name>(0));
  // Unreachable.
  __ Trap();
  __ bind(&done);
}
void BaselineCompiler::VisitThrowSuperNotCalledIfHole() {
  Label done;
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kTheHoleValue);
  __ j(not_equal, &done);
  CallRuntime(Runtime::kThrowSuperNotCalled);
  // Unreachable.
  __ Trap();
  __ bind(&done);
}
void BaselineCompiler::VisitThrowSuperAlreadyCalledIfNotHole() {
  Label done;
  __ CompareRoot(kInterpreterAccumulatorRegister, RootIndex::kTheHoleValue);
  __ j(equal, &done);
  CallRuntime(Runtime::kThrowSuperAlreadyCalledError);
  // Unreachable.
  __ Trap();
  __ bind(&done);
}
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