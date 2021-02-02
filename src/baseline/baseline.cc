// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/baseline.h"

#include <unordered_map>

#include "src/codegen/assembler.h"
#include "src/codegen/interface-descriptors.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/x64/register-x64.h"
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

  void JumpIfToBoolean(Register reg, Label* if_true, Label* if_false) {
    // Check true and false
    CompareRoot(reg, RootIndex::kFalseValue);
    j(equal, if_false);
    CompareRoot(reg, RootIndex::kTrueValue);
    j(equal, if_true);

    // Check if {value} is a Smi or a HeapObject.
    Label if_notsmi;
    JumpIfNotSmi(reg, &if_notsmi);
    {
      Cmp(reg, Smi::FromInt(0));
      j(equal, if_false);
      j(always, if_true);
    }
    bind(&if_notsmi);

    // TODO(leszeks): Pass in scratch reg for map.

    LoadTaggedPointerField(rcx, FieldOperand(reg, HeapObject::kMapOffset));

    testb(FieldOperand(rcx, Map::kBitFieldOffset),
          Immediate(Map::Bits1::IsUndetectableBit::kMask));
    j(not_zero, if_false);

    movw(rcx, FieldOperand(rcx, Map::kInstanceTypeOffset));

    Label if_notheapnumber;
    Cmp(kScratchRegister, InstanceType::HEAP_NUMBER_TYPE);
    j(not_equal, &if_notheapnumber);
    {
      movq(xmm1, FieldOperand(reg, HeapNumber::kValueOffset));
      xorpd(kScratchDoubleReg, kScratchDoubleReg);
      ucomisd(xmm1, kScratchDoubleReg);
      j(not_equal, if_true);
      j(always, if_false);
    }
    bind(&if_notheapnumber);

    Label if_notbigint;
    Cmp(kScratchRegister, InstanceType::BIG_INT_BASE_TYPE);
    j(not_equal, &if_notbigint);
    {
      // TODO(leszeks): Bigint
      int3();
    }
    bind(&if_notbigint);

    j(always, if_true);
  }
};

#define __ masm_.

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

  void Prologue() {
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

    // Stack checks
    __ movq(rax, rsp);
    __ subq(rax, Immediate(shared_->GetBytecodeArray().frame_size()));
    __ cmpq(rax, __ StackLimitAsOperand(StackLimitKind::kInterruptStackLimit));
    __ j(below, &stack_check_);

    // Inlined register frame fill
    __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
    for (int i = 0; i < shared_->GetBytecodeArray().frame_size(); ++i) {
      __ Push(kInterpreterAccumulatorRegister);
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

void BaselineCompiler::VisitLdaZero() { __ Move(rax, Smi::FromInt(0)); }
void BaselineCompiler::VisitLdaSmi() {
  Smi constant = Smi::FromInt(accessor().GetImmediateOperand(0));
  __ Move(rax, constant);
}
void BaselineCompiler::VisitLdaUndefined() {
  __ LoadRoot(rax, RootIndex::kUndefinedValue);
}
void BaselineCompiler::VisitLdaNull() {
  __ LoadRoot(rax, RootIndex::kNullValue);
}
void BaselineCompiler::VisitLdaTheHole() {
  __ LoadRoot(rax, RootIndex::kTheHoleValue);
}
void BaselineCompiler::VisitLdaTrue() {
  __ LoadRoot(rax, RootIndex::kTrueValue);
}
void BaselineCompiler::VisitLdaFalse() {
  __ LoadRoot(rax, RootIndex::kFalseValue);
}
void BaselineCompiler::VisitLdaConstant() {
  Handle<HeapObject> constant = Handle<HeapObject>::cast(
      accessor().GetConstantForIndexOperand(0, isolate_));
  __ Move(rax, constant);
}
void BaselineCompiler::VisitLdaGlobal() { __ int3(); }
void BaselineCompiler::VisitLdaGlobalInsideTypeof() { __ int3(); }
void BaselineCompiler::VisitStaGlobal() { __ int3(); }
void BaselineCompiler::VisitPushContext() {
  __ movq(kScratchRegister, __ ContextOperand());
  __ movq(__ ContextOperand(), rax);
  __ movq(__ RegisterFrameOperand(accessor().GetRegisterOperand(0)),
          kScratchRegister);
}
void BaselineCompiler::VisitPopContext() {
  __ movq(kScratchRegister,
          __ RegisterFrameOperand(accessor().GetRegisterOperand(0)));
  __ movq(__ ContextOperand(), kScratchRegister);
}
void BaselineCompiler::VisitLdaContextSlot() {
  __ movq(kScratchRegister,
          __ RegisterFrameOperand(accessor().GetRegisterOperand(0)));
  int depth = accessor().GetUnsignedImmediateOperand(2);
  for (; depth > 0; --depth) {
    __ LoadTaggedPointerField(
        kScratchRegister,
        FieldOperand(kScratchRegister, Context::kPreviousOffset));
  }
  __ LoadAnyTaggedField(
      rax, FieldOperand(kScratchRegister, Context::OffsetOfElementAt(
                                              accessor().GetIndexOperand(1))));
}
void BaselineCompiler::VisitLdaImmutableContextSlot() { VisitLdaContextSlot(); }
void BaselineCompiler::VisitLdaCurrentContextSlot() {
  __ movq(kScratchRegister, __ ContextOperand());
  __ LoadAnyTaggedField(
      rax, FieldOperand(kScratchRegister, Context::OffsetOfElementAt(
                                              accessor().GetIndexOperand(0))));
}
void BaselineCompiler::VisitLdaImmutableCurrentContextSlot() {
  VisitLdaCurrentContextSlot();
}
void BaselineCompiler::VisitStaContextSlot() {
  __ movq(kScratchRegister,
          __ RegisterFrameOperand(accessor().GetRegisterOperand(0)));
  int depth = accessor().GetUnsignedImmediateOperand(2);
  for (; depth > 0; --depth) {
    __ LoadTaggedPointerField(
        kScratchRegister,
        FieldOperand(kScratchRegister, Context::kPreviousOffset));
  }
  __ StoreTaggedField(
      FieldOperand(kScratchRegister,
                   Context::OffsetOfElementAt(accessor().GetIndexOperand(1))),
      rax);
}
void BaselineCompiler::VisitStaCurrentContextSlot() {
  __ movq(kScratchRegister, __ ContextOperand());
  __ StoreTaggedField(
      FieldOperand(kScratchRegister,
                   Context::OffsetOfElementAt(accessor().GetIndexOperand(0))),
      rax);
}
void BaselineCompiler::VisitLdaLookupSlot() {
  __ Push(
      Handle<Name>::cast(accessor().GetConstantForIndexOperand(0, isolate_)));
  __ movq(CallInterfaceDescriptor::ContextRegister(), __ ContextOperand());
  __ CallRuntime(Runtime::kLoadLookupSlot);
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
  __ movq(rax, __ RegisterFrameOperand(accessor().GetRegisterOperand(0)));
}
void BaselineCompiler::VisitStar() {
  __ movq(__ RegisterFrameOperand(accessor().GetRegisterOperand(0)), rax);
}
void BaselineCompiler::VisitMov() {
  __ movq(kScratchRegister,
          __ RegisterFrameOperand(accessor().GetRegisterOperand(0)));
  __ movq(__ RegisterFrameOperand(accessor().GetRegisterOperand(1)),
          kScratchRegister);
}
void BaselineCompiler::VisitLdaNamedProperty() {
  __ movq(kScratchRegister, __ FunctionOperand());
  // Loads the feedback vector cell.
  // TODO(verwaest): Remove this indirection by doing a map-check on the
  // JSFunction::kFeedback entry instead.
  __ LoadTaggedPointerField(kScratchRegister, FieldOperand(kScratchRegister, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedPointerField(LoadWithVectorDescriptor::VectorRegister(), FieldOperand(kScratchRegister, FeedbackCell::kValueOffset));

  __ movq(LoadDescriptor::ContextRegister(), __ ContextOperand());
  __ movq(LoadDescriptor::ReceiverRegister(),
          __ RegisterFrameOperand(accessor().GetRegisterOperand(0)));
  __ Move(
      LoadDescriptor::NameRegister(),
      Handle<Name>::cast(accessor().GetConstantForIndexOperand(1, isolate_)));
  __ Move(LoadDescriptor::SlotRegister(),
          Smi::FromInt(accessor().GetIndexOperand(2)));

  __ CallBuiltin(Builtins::kLoadIC);
}
void BaselineCompiler::VisitLdaNamedPropertyNoFeedback() { __ int3(); }
void BaselineCompiler::VisitLdaNamedPropertyFromSuper() {
  __ movq(kScratchRegister, __ FunctionOperand());
  // Loads the feedback vector cell.
  // TODO(verwaest): Remove this indirection by doing a map-check on the
  // JSFunction::kFeedback entry instead.
  __ LoadTaggedPointerField(kScratchRegister, FieldOperand(kScratchRegister, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedPointerField(LoadWithVectorDescriptor::VectorRegister(), FieldOperand(kScratchRegister, FeedbackCell::kValueOffset));

  __ LoadMap(kScratchRegister, rax);
  __ LoadTaggedPointerField(LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister(),
          FieldOperand(kScratchRegister, Map::kPrototypeOffset));

  __ movq(LoadDescriptor::ContextRegister(), __ ContextOperand());
  __ movq(LoadDescriptor::ReceiverRegister(),
          __ RegisterFrameOperand(accessor().GetRegisterOperand(0)));
  __ Move(
      LoadDescriptor::NameRegister(),
      Handle<Name>::cast(accessor().GetConstantForIndexOperand(1, isolate_)));
  __ Move(LoadDescriptor::SlotRegister(),
          Smi::FromInt(accessor().GetIndexOperand(2)));

  __ CallBuiltin(Builtins::kLoadSuperIC);
}
void BaselineCompiler::VisitLdaKeyedProperty() {
  __ movq(kScratchRegister, __ FunctionOperand());
  // Loads the feedback vector cell.
  // TODO(verwaest): Remove this indirection by doing a map-check on the
  // JSFunction::kFeedback entry instead.
  __ LoadTaggedPointerField(kScratchRegister, FieldOperand(kScratchRegister, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedPointerField(LoadWithVectorDescriptor::VectorRegister(), FieldOperand(kScratchRegister, FeedbackCell::kValueOffset));

  __ movq(LoadDescriptor::ContextRegister(), __ ContextOperand());
  __ movq(LoadDescriptor::ReceiverRegister(),
          __ RegisterFrameOperand(accessor().GetRegisterOperand(0)));
  __ Move(LoadDescriptor::NameRegister(), rax);
  __ Move(LoadDescriptor::SlotRegister(),
          Smi::FromInt(accessor().GetIndexOperand(1)));

  __ CallBuiltin(Builtins::kKeyedLoadIC);
}
void BaselineCompiler::VisitLdaModuleVariable() { __ int3(); }
void BaselineCompiler::VisitStaModuleVariable() { __ int3(); }
void BaselineCompiler::VisitStaNamedProperty() { __ int3(); }
void BaselineCompiler::VisitStaNamedPropertyNoFeedback() { __ int3(); }
void BaselineCompiler::VisitStaNamedOwnProperty() { __ int3(); }
void BaselineCompiler::VisitStaKeyedProperty() { __ int3(); }
void BaselineCompiler::VisitStaInArrayLiteral() { __ int3(); }
void BaselineCompiler::VisitStaDataPropertyInLiteral() { __ int3(); }
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
  Handle<ScopeInfo> scope_info = Handle<ScopeInfo>::cast(
      accessor().GetConstantForIndexOperand(0, isolate_));
  __ Push(scope_info);
  __ CallRuntime(Runtime::kPushBlockContext);
  __ movq(__ ContextOperand(), rax);
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
  __ CompareRoot(rax, RootIndex::kNullValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNotNullConstant() {
  __ CompareRoot(rax, RootIndex::kNullValue);
  __ j(not_equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfUndefinedConstant() {
  __ CompareRoot(rax, RootIndex::kUndefinedValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNotUndefinedConstant() {
  __ CompareRoot(rax, RootIndex::kUndefinedValue);
  __ j(not_equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfUndefinedOrNullConstant() {
  __ CompareRoot(rax, RootIndex::kUndefinedValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
  __ CompareRoot(rax, RootIndex::kNullValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfTrueConstant() {
  __ CompareRoot(rax, RootIndex::kTrueValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfFalseConstant() {
  __ CompareRoot(rax, RootIndex::kFalseValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfJSReceiverConstant() {
  Label is_smi;
  __ JumpIfSmi(rax, &is_smi, Label::kNear);

  __ LoadAnyTaggedField(kScratchRegister,
                        FieldOperand(rax, HeapObject::kMapOffset));
  __ movw(kScratchRegister,
          FieldOperand(kScratchRegister, Map::kInstanceTypeOffset));
  __ Cmp(kScratchRegister, FIRST_JS_RECEIVER_TYPE);
  __ j(greater_equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));

  __ bind(&is_smi);
}
void BaselineCompiler::VisitJumpIfToBooleanTrueConstant() {
  Label if_false;
  __ JumpIfToBoolean(rax, BuildJumpLabel(accessor().GetJumpTargetOffset()),
                     &if_false);
  __ bind(&if_false);
}
void BaselineCompiler::VisitJumpIfToBooleanFalseConstant() {
  Label if_true;
  __ JumpIfToBoolean(rax, &if_true,
                     BuildJumpLabel(accessor().GetJumpTargetOffset()));
  __ bind(&if_true);
}
void BaselineCompiler::VisitJumpIfToBooleanTrue() {
  Label if_false;
  __ JumpIfToBoolean(rax, BuildJumpLabel(accessor().GetJumpTargetOffset()),
                     &if_false);
  __ bind(&if_false);
}
void BaselineCompiler::VisitJumpIfToBooleanFalse() {
  Label if_true;
  __ JumpIfToBoolean(rax, &if_true,
                     BuildJumpLabel(accessor().GetJumpTargetOffset()));
  __ bind(&if_true);
}
void BaselineCompiler::VisitJumpIfTrue() {
  __ CompareRoot(rax, RootIndex::kTrueValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfFalse() {
  __ CompareRoot(rax, RootIndex::kFalseValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNull() {
  __ CompareRoot(rax, RootIndex::kNullValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNotNull() {
  __ CompareRoot(rax, RootIndex::kNullValue);
  __ j(not_equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfUndefined() {
  __ CompareRoot(rax, RootIndex::kUndefinedValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNotUndefined() {
  __ CompareRoot(rax, RootIndex::kUndefinedValue);
  __ j(not_equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfUndefinedOrNull() {
  __ CompareRoot(rax, RootIndex::kUndefinedValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
  __ CompareRoot(rax, RootIndex::kNullValue);
  __ j(equal, BuildJumpLabel(accessor().GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfJSReceiver() {
  Label is_smi;
  __ JumpIfSmi(rax, &is_smi, Label::kNear);

  __ LoadAnyTaggedField(kScratchRegister,
                        FieldOperand(rax, HeapObject::kMapOffset));
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
  __ Push(__ FunctionOperand());
  Handle<HeapObject> coverage_array_slot =
      Handle<HeapObject>::cast(accessor().GetConstantAtIndex(0, isolate_));
  __ Push(coverage_array_slot);
  __ CallRuntime(Runtime::kInlineIncBlockCounter);
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