// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/baseline.h"

#include <unordered_map>

#include "src/codegen/assembler.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/x64/register-x64.h"
#include "src/common/globals.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/codegen/interface-descriptors.h"
#include "src/objects/code.h"
#include "src/objects/heap-object.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/objects/shared-function-info.h"

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
    Cmp(reg, isolate()->factory()->false_value());
    j(equal, if_false);
    Cmp(reg, isolate()->factory()->true_value());
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
        masm_(isolate, CodeObjectRequired::kNo) {
    USE(isolate_);
  }

  void PreVisitSingleBytecode(
      const interpreter::BytecodeArrayAccessor& accessor) {
    if (accessor.current_bytecode() == interpreter::Bytecode::kJumpLoop) {
      back_jumps_[accessor.GetJumpTargetOffset()] = Label();
    }
  }

  void VisitSingleBytecode(const interpreter::BytecodeArrayAccessor& accessor) {
    // TODO(leszeks): Merge forward and back labels.
    auto forward_jump_label = forward_jumps_.find(accessor.current_offset());
    if (forward_jump_label != forward_jumps_.end()) {
      for (auto&& label : forward_jump_label->second) {
        __ bind(&label);
      }
      forward_jumps_.erase(forward_jump_label);
    }
    auto back_jump_label = back_jumps_.find(accessor.current_offset());
    if (back_jump_label != back_jumps_.end()) {
      __ bind(&back_jump_label->second);
    }

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
#define DECLARE_VISITOR(name, ...) \
  void Visit##name(const interpreter::BytecodeArrayAccessor& accessor);
  BYTECODE_LIST(DECLARE_VISITOR)
#undef DECLARE_VISITOR

  Label* BuildJumpLabel(int bytecode_offset) {
    std::vector<Label>& labels = forward_jumps_[bytecode_offset];
    labels.emplace_back();
    return &labels.back();
  }

  Isolate* isolate_;
  Handle<SharedFunctionInfo> shared_;
  BaselineAssembler masm_;

  std::unordered_map<int, std::vector<Label>> forward_jumps_;
  std::unordered_map<int, Label> back_jumps_;

  // Epilogue stuff.
  Label maybe_has_optimized_code_;
  Label stack_check_;
  Register optimization_state_ = Register::no_reg();
  Register feedback_vector_ = Register::no_reg();
};

void BaselineCompiler::VisitLdaZero(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Move(rax, Smi::FromInt(0));
}
void BaselineCompiler::VisitLdaSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  Smi constant = Smi::FromInt(accessor.GetImmediateOperand(0));
  __ Move(rax, constant);
}
void BaselineCompiler::VisitLdaUndefined(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ LoadRoot(rax, RootIndex::kUndefinedValue);
}
void BaselineCompiler::VisitLdaNull(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ LoadRoot(rax, RootIndex::kNullValue);
}
void BaselineCompiler::VisitLdaTheHole(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ LoadRoot(rax, RootIndex::kTheHoleValue);
}
void BaselineCompiler::VisitLdaTrue(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ LoadRoot(rax, RootIndex::kTrueValue);
}
void BaselineCompiler::VisitLdaFalse(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ LoadRoot(rax, RootIndex::kFalseValue);
}
void BaselineCompiler::VisitLdaConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  Handle<HeapObject> constant = Handle<HeapObject>::cast(
      accessor.GetConstantForIndexOperand(0, isolate_));
  __ Move(rax, constant);
}
void BaselineCompiler::VisitLdaGlobal(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitLdaGlobalInsideTypeof(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitStaGlobal(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitPushContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ movq(kScratchRegister, __ ContextOperand());
  __ movq(__ ContextOperand(), rax);
  __ movq(__ RegisterFrameOperand(accessor.GetRegisterOperand(0)),
          kScratchRegister);
}
void BaselineCompiler::VisitPopContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ movq(kScratchRegister,
          __ RegisterFrameOperand(accessor.GetRegisterOperand(0)));
  __ movq(__ ContextOperand(), kScratchRegister);
}
void BaselineCompiler::VisitLdaContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ movq(kScratchRegister,
          __ RegisterFrameOperand(accessor.GetRegisterOperand(0)));
  int depth = accessor.GetUnsignedImmediateOperand(2);
  for (; depth > 0; --depth) {
    __ LoadTaggedPointerField(
        kScratchRegister,
        FieldOperand(kScratchRegister, Context::kPreviousOffset));
  }
  __ LoadAnyTaggedField(
      rax, FieldOperand(kScratchRegister, Context::OffsetOfElementAt(
                                              accessor.GetIndexOperand(1))));
}
void BaselineCompiler::VisitLdaImmutableContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  VisitLdaContextSlot(accessor);
}
void BaselineCompiler::VisitLdaCurrentContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ movq(kScratchRegister, __ ContextOperand());
  __ LoadAnyTaggedField(
      rax, FieldOperand(kScratchRegister, Context::OffsetOfElementAt(
                                              accessor.GetIndexOperand(0))));
}
void BaselineCompiler::VisitLdaImmutableCurrentContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  VisitLdaCurrentContextSlot(accessor);
}
void BaselineCompiler::VisitStaContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ movq(kScratchRegister,
          __ RegisterFrameOperand(accessor.GetRegisterOperand(0)));
  int depth = accessor.GetUnsignedImmediateOperand(2);
  for (; depth > 0; --depth) {
    __ LoadTaggedPointerField(
        kScratchRegister,
        FieldOperand(kScratchRegister, Context::kPreviousOffset));
  }
  __ StoreTaggedField(
      FieldOperand(kScratchRegister,
                   Context::OffsetOfElementAt(accessor.GetIndexOperand(1))),
      rax);
}
void BaselineCompiler::VisitStaCurrentContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ movq(kScratchRegister, __ ContextOperand());
  __ StoreTaggedField(
      FieldOperand(kScratchRegister,
                   Context::OffsetOfElementAt(accessor.GetIndexOperand(0))),
      rax);
}
void BaselineCompiler::VisitLdaLookupSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitLdaLookupContextSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitLdaLookupGlobalSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitLdaLookupSlotInsideTypeof(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitLdaLookupContextSlotInsideTypeof(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitLdaLookupGlobalSlotInsideTypeof(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitStaLookupSlot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitLdar(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ movq(rax, __ RegisterFrameOperand(accessor.GetRegisterOperand(0)));
}
void BaselineCompiler::VisitStar(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ movq(__ RegisterFrameOperand(accessor.GetRegisterOperand(0)), rax);
}
void BaselineCompiler::VisitMov(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ movq(kScratchRegister,
          __ RegisterFrameOperand(accessor.GetRegisterOperand(0)));
  __ movq(__ RegisterFrameOperand(accessor.GetRegisterOperand(1)),
          kScratchRegister);
}
void BaselineCompiler::VisitLdaNamedProperty(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ movq(kScratchRegister, __ FunctionOperand());
  // Loads the feedback vector cell.
  // TODO(verwaest): Remove this indirection by doing a map-check on the
  // JSFunction::kFeedback entry instead.
  __ LoadTaggedPointerField(kScratchRegister, FieldOperand(kScratchRegister, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedPointerField(LoadWithVectorDescriptor::VectorRegister(), FieldOperand(kScratchRegister, FeedbackCell::kValueOffset));

  __ movq(LoadDescriptor::ContextRegister(), __ ContextOperand());
  __ movq(LoadDescriptor::ReceiverRegister(), __ RegisterFrameOperand(accessor.GetRegisterOperand(0)));
  __ Move(LoadDescriptor::NameRegister(), Handle<Name>::cast(accessor.GetConstantForIndexOperand(1, isolate_)));
  __ Move(LoadDescriptor::SlotRegister(), Smi::FromInt(accessor.GetIndexOperand(2)));

  __ CallBuiltin(Builtins::kLoadIC);
}
void BaselineCompiler::VisitLdaNamedPropertyNoFeedback(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitLdaNamedPropertyFromSuper(
    const interpreter::BytecodeArrayAccessor& accessor) {
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
  __ movq(LoadDescriptor::ReceiverRegister(), __ RegisterFrameOperand(accessor.GetRegisterOperand(0)));
  __ Move(LoadDescriptor::NameRegister(), Handle<Name>::cast(accessor.GetConstantForIndexOperand(1, isolate_)));
  __ Move(LoadDescriptor::SlotRegister(), Smi::FromInt(accessor.GetIndexOperand(2)));

  __ CallBuiltin(Builtins::kLoadSuperIC);
}
void BaselineCompiler::VisitLdaKeyedProperty(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ movq(kScratchRegister, __ FunctionOperand());
  // Loads the feedback vector cell.
  // TODO(verwaest): Remove this indirection by doing a map-check on the
  // JSFunction::kFeedback entry instead.
  __ LoadTaggedPointerField(kScratchRegister, FieldOperand(kScratchRegister, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedPointerField(LoadWithVectorDescriptor::VectorRegister(), FieldOperand(kScratchRegister, FeedbackCell::kValueOffset));

  __ movq(LoadDescriptor::ContextRegister(), __ ContextOperand());
  __ movq(LoadDescriptor::ReceiverRegister(), __ RegisterFrameOperand(accessor.GetRegisterOperand(0)));
  __ Move(LoadDescriptor::NameRegister(), rax);
  __ Move(LoadDescriptor::SlotRegister(), Smi::FromInt(accessor.GetIndexOperand(1)));

  __ CallBuiltin(Builtins::kKeyedLoadIC);
}
void BaselineCompiler::VisitLdaModuleVariable(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitStaModuleVariable(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitStaNamedProperty(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitStaNamedPropertyNoFeedback(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitStaNamedOwnProperty(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitStaKeyedProperty(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitStaInArrayLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitStaDataPropertyInLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCollectTypeProfile(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitAdd(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitSub(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitMul(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitDiv(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitMod(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitExp(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitBitwiseOr(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitBitwiseXor(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitBitwiseAnd(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitShiftLeft(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitShiftRight(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitShiftRightLogical(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitAddSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitSubSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitMulSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitDivSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitModSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitExpSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitBitwiseOrSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitBitwiseXorSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitBitwiseAndSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitShiftLeftSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitShiftRightSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitShiftRightLogicalSmi(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitInc(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitDec(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitNegate(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitBitwiseNot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitToBooleanLogicalNot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitLogicalNot(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTypeOf(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitDeletePropertyStrict(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitDeletePropertySloppy(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitGetSuperConstructor(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallAnyReceiver(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallProperty(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallProperty0(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallProperty1(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallProperty2(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallUndefinedReceiver(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallUndefinedReceiver0(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallUndefinedReceiver1(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallUndefinedReceiver2(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallNoFeedback(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallWithSpread(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallRuntime(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallRuntimeForPair(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCallJSRuntime(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitInvokeIntrinsic(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitConstruct(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitConstructWithSpread(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTestEqual(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTestEqualStrict(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTestLessThan(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTestGreaterThan(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTestLessThanOrEqual(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTestGreaterThanOrEqual(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTestReferenceEqual(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTestInstanceOf(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTestIn(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTestUndetectable(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTestNull(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTestUndefined(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitTestTypeOf(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitToName(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitToNumber(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitToNumeric(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitToObject(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitToString(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateRegExpLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateArrayLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateArrayFromIterable(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateEmptyArrayLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateObjectLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateEmptyObjectLiteral(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCloneObject(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitGetTemplateObject(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateClosure(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateBlockContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  Handle<ScopeInfo> scope_info =
      Handle<ScopeInfo>::cast(accessor.GetConstantForIndexOperand(0, isolate_));
  __ Push(scope_info);
  __ CallRuntime(Runtime::kPushBlockContext);
  __ movq(__ ContextOperand(), rax);
}
void BaselineCompiler::VisitCreateCatchContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateFunctionContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateEvalContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateWithContext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateMappedArguments(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateUnmappedArguments(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitCreateRestParameter(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitJumpLoop(
    const interpreter::BytecodeArrayAccessor& accessor) {
  DCHECK(back_jumps_.count(accessor.GetJumpTargetOffset()) == 1);
  __ j(always, &back_jumps_[accessor.GetJumpTargetOffset()]);
}
void BaselineCompiler::VisitJump(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ j(always, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ j(always, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNullConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->null_value());
  __ j(equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNotNullConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->null_value());
  __ j(not_equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfUndefinedConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->undefined_value());
  __ j(equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNotUndefinedConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->undefined_value());
  __ j(not_equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfUndefinedOrNullConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->undefined_value());
  __ j(equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
  __ Cmp(rax, isolate_->factory()->null_value());
  __ j(equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfTrueConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->true_value());
  __ j(equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfFalseConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->false_value());
  __ j(equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfJSReceiverConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  Label is_smi;
  __ JumpIfSmi(rax, &is_smi, Label::kNear);

  __ LoadAnyTaggedField(kScratchRegister,
                        FieldOperand(rax, HeapObject::kMapOffset));
  __ movw(kScratchRegister,
          FieldOperand(kScratchRegister, Map::kInstanceTypeOffset));
  __ Cmp(kScratchRegister, FIRST_JS_RECEIVER_TYPE);
  __ j(greater_equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));

  __ bind(&is_smi);
}
void BaselineCompiler::VisitJumpIfToBooleanTrueConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  Label if_false;
  __ JumpIfToBoolean(rax, BuildJumpLabel(accessor.GetJumpTargetOffset()),
                     &if_false);
  __ bind(&if_false);
}
void BaselineCompiler::VisitJumpIfToBooleanFalseConstant(
    const interpreter::BytecodeArrayAccessor& accessor) {
  Label if_true;
  __ JumpIfToBoolean(rax, &if_true,
                     BuildJumpLabel(accessor.GetJumpTargetOffset()));
  __ bind(&if_true);
}
void BaselineCompiler::VisitJumpIfToBooleanTrue(
    const interpreter::BytecodeArrayAccessor& accessor) {
  Label if_false;
  __ JumpIfToBoolean(rax, BuildJumpLabel(accessor.GetJumpTargetOffset()),
                     &if_false);
  __ bind(&if_false);
}
void BaselineCompiler::VisitJumpIfToBooleanFalse(
    const interpreter::BytecodeArrayAccessor& accessor) {
  Label if_true;
  __ JumpIfToBoolean(rax, &if_true,
                     BuildJumpLabel(accessor.GetJumpTargetOffset()));
  __ bind(&if_true);
}
void BaselineCompiler::VisitJumpIfTrue(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->true_value());
  __ j(equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfFalse(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->false_value());
  __ j(equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNull(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->null_value());
  __ j(equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNotNull(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->null_value());
  __ j(not_equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfUndefined(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->undefined_value());
  __ j(equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfNotUndefined(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->undefined_value());
  __ j(not_equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfUndefinedOrNull(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Cmp(rax, isolate_->factory()->undefined_value());
  __ j(equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
  __ Cmp(rax, isolate_->factory()->null_value());
  __ j(equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));
}
void BaselineCompiler::VisitJumpIfJSReceiver(
    const interpreter::BytecodeArrayAccessor& accessor) {
  Label is_smi;
  __ JumpIfSmi(rax, &is_smi, Label::kNear);

  __ LoadAnyTaggedField(kScratchRegister,
                        FieldOperand(rax, HeapObject::kMapOffset));
  __ LoadAnyTaggedField(
      kScratchRegister,
      FieldOperand(kScratchRegister, Map::kInstanceTypeOffset));
  __ Cmp(kScratchRegister, FIRST_JS_RECEIVER_TYPE);
  __ j(greater_equal, BuildJumpLabel(accessor.GetJumpTargetOffset()));

  __ bind(&is_smi);
}
void BaselineCompiler::VisitSwitchOnSmiNoFeedback(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitForInEnumerate(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitForInPrepare(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitForInContinue(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitForInNext(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitForInStep(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitSetPendingMessage(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitThrow(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitReThrow(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
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
  __ int3();
}
void BaselineCompiler::VisitThrowSuperNotCalledIfHole(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitThrowSuperAlreadyCalledIfNotHole(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitThrowIfNotSuperConstructor(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitSwitchOnGeneratorState(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitSuspendGenerator(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitResumeGenerator(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitGetIterator(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitDebugger(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitIncBlockCounter(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ Push(__ FunctionOperand());
  Handle<HeapObject> coverage_array_slot =
      Handle<HeapObject>::cast(accessor.GetConstantAtIndex(0, isolate_));
  __ Push(coverage_array_slot);
  __ CallRuntime(Runtime::kInlineIncBlockCounter);
}
void BaselineCompiler::VisitAbort(
    const interpreter::BytecodeArrayAccessor& accessor) {
  __ int3();
}
void BaselineCompiler::VisitWide(
    const interpreter::BytecodeArrayAccessor& accessor) {
  // Consumed by the BytecodeArrayIterator.
  UNREACHABLE();
}
void BaselineCompiler::VisitExtraWide(
    const interpreter::BytecodeArrayAccessor& accessor) {
  // Consumed by the BytecodeArrayIterator.
  UNREACHABLE();
}
void BaselineCompiler::VisitIllegal(
    const interpreter::BytecodeArrayAccessor& accessor) {
  // Not emitted in valid bytecode.
  UNREACHABLE();
}
#define DEBUG_BREAK(Name, ...)                              \
  void BaselineCompiler::Visit##Name(                       \
      const interpreter::BytecodeArrayAccessor& accessor) { \
    UNREACHABLE();                                          \
  }
DEBUG_BREAK_BYTECODE_LIST(DEBUG_BREAK)
#undef DEBUG_BREAK

Handle<Code> CompileWithBaseline(Isolate* isolate,
                                 Handle<SharedFunctionInfo> shared) {
  BaselineCompiler compiler(isolate, shared);

  compiler.Prologue();

  Handle<BytecodeArray> bytecode = handle(shared->GetBytecodeArray(), isolate);
  {
    interpreter::BytecodeArrayIterator iterator(bytecode);
    for (; !iterator.done(); iterator.Advance()) {
      compiler.PreVisitSingleBytecode(iterator);
    }
  }
  {
    interpreter::BytecodeArrayIterator iterator(bytecode);
    for (; !iterator.done(); iterator.Advance()) {
      compiler.VisitSingleBytecode(iterator);
    }
  }

  compiler.Epilogue();

  return compiler.Build(isolate);
}

}  // namespace internal
}  // namespace v8