// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/baseline.h"

#include "src/codegen/assembler.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/common/globals.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/objects/code.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/objects/shared-function-info.h"

namespace v8 {
namespace internal {

class BaselineAssembler : public MacroAssembler {
 public:
  using MacroAssembler::MacroAssembler;
};

#define __ masm_.

class BaselineCompiler {
 public:
  explicit BaselineCompiler(Isolate* isolate, Handle<SharedFunctionInfo> shared)
      : isolate_(isolate),
        shared_(shared),
        masm_(isolate, CodeObjectRequired::kNo) {
    USE(isolate_);
  }

  void VisitSingleBytecode(const interpreter::BytecodeArrayAccessor& accessor) {
    switch (accessor.current_bytecode()) {
#define BYTECODE_CASE(name, ...)       \
  case interpreter::Bytecode::k##name: \
    return Visit##name(accessor);
      BYTECODE_LIST(BYTECODE_CASE)
#undef BYTECODE_CASE
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
  }

  Handle<Code> Build(Isolate* isolate) {
    CodeDesc desc;
    masm_.GetCode(isolate, &desc);
    return Factory::CodeBuilder(isolate, desc, CodeKind::SPARKPLUG).Build();
  }

 private:
#define DECLARE_VISITOR(name, ...) \
  void Visit##name(const interpreter::BytecodeArrayAccessor& accessor);
  BYTECODE_LIST(DECLARE_VISITOR)
#undef DECLARE_VISITOR

  Isolate* isolate_;
  Handle<SharedFunctionInfo> shared_;
  BaselineAssembler masm_;

  // Epilogue stuff.
  Label maybe_has_optimized_code_;
  Register optimization_state_ = Register::no_reg();
  Register feedback_vector_ = Register::no_reg();
};

void BaselineCompiler::VisitWide(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitExtraWide(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDebugBreakWide(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDebugBreakExtraWide(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDebugBreak0(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDebugBreak1(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDebugBreak2(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDebugBreak3(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDebugBreak4(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDebugBreak5(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDebugBreak6(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaZero(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaUndefined(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.LoadRoot(rax, RootIndex::kUndefinedValue);
}
void BaselineCompiler::VisitLdaNull(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaTheHole(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaTrue(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaFalse(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaGlobal(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaGlobalInsideTypeof(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitStaGlobal(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitPushContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitPopContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaImmutableContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaCurrentContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaImmutableCurrentContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitStaContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitStaCurrentContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaLookupSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaLookupContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaLookupGlobalSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaLookupSlotInsideTypeof(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaLookupContextSlotInsideTypeof(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaLookupGlobalSlotInsideTypeof(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitStaLookupSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdar(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitStar(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitMov(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaNamedProperty(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaNamedPropertyNoFeedback(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaNamedPropertyFromSuper(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaKeyedProperty(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLdaModuleVariable(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitStaModuleVariable(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitStaNamedProperty(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitStaNamedPropertyNoFeedback(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitStaNamedOwnProperty(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitStaKeyedProperty(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitStaInArrayLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitStaDataPropertyInLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCollectTypeProfile(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitAdd(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitSub(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitMul(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDiv(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitMod(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitExp(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitBitwiseOr(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitBitwiseXor(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitBitwiseAnd(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitShiftLeft(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitShiftRight(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitShiftRightLogical(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitAddSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitSubSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitMulSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDivSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitModSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitExpSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitBitwiseOrSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitBitwiseXorSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitBitwiseAndSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitShiftLeftSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitShiftRightSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitShiftRightLogicalSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitInc(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDec(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitNegate(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitBitwiseNot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitToBooleanLogicalNot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitLogicalNot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTypeOf(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDeletePropertyStrict(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDeletePropertySloppy(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitGetSuperConstructor(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallAnyReceiver(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallProperty(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallProperty0(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallProperty1(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallProperty2(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallUndefinedReceiver(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallUndefinedReceiver0(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallUndefinedReceiver1(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallUndefinedReceiver2(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallNoFeedback(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallWithSpread(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallRuntime(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallRuntimeForPair(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCallJSRuntime(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitInvokeIntrinsic(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitConstruct(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitConstructWithSpread(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTestEqual(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTestEqualStrict(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTestLessThan(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTestGreaterThan(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTestLessThanOrEqual(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTestGreaterThanOrEqual(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTestReferenceEqual(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTestInstanceOf(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTestIn(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTestUndetectable(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTestNull(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTestUndefined(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitTestTypeOf(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitToName(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitToNumber(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitToNumeric(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitToObject(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitToString(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateRegExpLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateArrayLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateArrayFromIterable(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateEmptyArrayLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateObjectLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateEmptyObjectLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCloneObject(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitGetTemplateObject(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateClosure(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateBlockContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateCatchContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateFunctionContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateEvalContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateWithContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateMappedArguments(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateUnmappedArguments(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitCreateRestParameter(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpLoop(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJump(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfNullConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfNotNullConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfUndefinedConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfNotUndefinedConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfUndefinedOrNullConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfTrueConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfFalseConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfJSReceiverConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfToBooleanTrueConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfToBooleanFalseConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfToBooleanTrue(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfToBooleanFalse(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfTrue(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfFalse(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfNull(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfNotNull(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfUndefined(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfNotUndefined(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfUndefinedOrNull(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitJumpIfJSReceiver(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitSwitchOnSmiNoFeedback(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitForInEnumerate(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitForInPrepare(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitForInContinue(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitForInNext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitForInStep(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitSetPendingMessage(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitThrow(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitReThrow(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitReturn(
    const interpreter::BytecodeArrayAccessor& accessor) {
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
void BaselineCompiler::VisitThrowReferenceErrorIfHole(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitThrowSuperNotCalledIfHole(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitThrowSuperAlreadyCalledIfNotHole(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitThrowIfNotSuperConstructor(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitSwitchOnGeneratorState(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitSuspendGenerator(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitResumeGenerator(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitGetIterator(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitDebugger(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitIncBlockCounter(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitAbort(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}
void BaselineCompiler::VisitIllegal(
    const interpreter::BytecodeArrayAccessor& accessor) {
  masm_.int3();
}

Handle<Code> CompileWithBaseline(Isolate* isolate,
                                 Handle<SharedFunctionInfo> shared) {
  BaselineCompiler compiler(isolate, shared);

  compiler.Prologue();

  Handle<BytecodeArray> bytecode = handle(shared->GetBytecodeArray(), isolate);
  interpreter::BytecodeArrayIterator iterator(bytecode);
  for (; !iterator.done(); iterator.Advance()) {
    compiler.VisitSingleBytecode(iterator);
  }

  compiler.Epilogue();

  return compiler.Build(isolate);
}

}  // namespace internal
}  // namespace v8