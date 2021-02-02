// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_X64_BASELINE_COMPILER_X64_H_
#define V8_BASELINE_X64_BASELINE_COMPILER_X64_H_

#include "src/baseline/baseline-compiler.h"
#include "src/objects/code-inl.h"

namespace v8 {
namespace internal {

namespace {

#define __ masm_.

#ifdef DEBUG
bool Clobbers(Register target, MemOperand op) {
  return op.AddressUsesRegister(target);
}
#endif

}  // namespace

MemOperand BaselineAssembler::RegisterFrameOperand(
    interpreter::Register interpreter_register) {
  return MemOperand(rbp, interpreter_register.ToOperand() * kSystemPointerSize);
}
void BaselineAssembler::GetCode(Isolate* isolate, CodeDesc* desc) {
  __ GetCode(isolate, desc);
}
int BaselineAssembler::pc_offset() const { return __ pc_offset(); }
bool BaselineAssembler::emit_debug_code() const { return __ emit_debug_code(); }
void BaselineAssembler::Assert(Condition condition, AbortReason reason) {
  __ Assert(condition, reason);
}
void BaselineAssembler::RecordComment(const char* string) {
  __ RecordComment(string);
}
void BaselineAssembler::Trap() { __ Trap(); }
Operand BaselineAssembler::StackLimitAsOperand(StackLimitKind kind) {
  return __ StackLimitAsOperand(kind);
}
void BaselineAssembler::CallRuntime(Runtime::FunctionId function, int nargs) {
  __ CallRuntime(function, nargs);
}

void BaselineAssembler::bind(Label* label) { __ bind(label); }
void BaselineAssembler::j(Condition cc, Label* target,
                          Label::Distance distance) {
  __ j(cc, target, distance);
}
void BaselineAssembler::jmp(Label* target, Label::Distance distance) {
  __ jmp(target, distance);
}
void BaselineAssembler::TestAndBranchIfAllClear(Register value, int mask,
                                                Label* target,
                                                Label::Distance distance) {
  if ((mask & 0xff) == mask) {
    __ testb(value, Immediate(mask));
  } else {
    __ testl(value, Immediate(mask));
  }
  __ j(zero, target, distance);
}
void BaselineAssembler::TestAndBranchIfAnySet(Register value, int mask,
                                              Label* target,
                                              Label::Distance distance) {
  if ((mask & 0xff) == mask) {
    __ testb(value, Immediate(mask));
  } else {
    __ testl(value, Immediate(mask));
  }
  __ j(not_zero, target, distance);
}

void BaselineAssembler::CmpObjectType(Register object,
                                      InstanceType instance_type,
                                      Register map) {
  __ CmpObjectType(object, instance_type, map);
}
void BaselineAssembler::CmpInstanceType(Register value,
                                        InstanceType instance_type) {
  __ CmpInstanceType(value, instance_type);
}
void BaselineAssembler::Cmp(Register value, Smi smi) { __ Cmp(value, smi); }
void BaselineAssembler::ComparePointer(Register value, Operand operand) {
  __ cmpq(value, operand);
}
Condition BaselineAssembler::CheckSmi(Register value) {
  return __ CheckSmi(value);
}
void BaselineAssembler::SmiCompare(Register lhs, Register rhs) {
  __ SmiCompare(lhs, rhs);
}
// cmp_tagged
void BaselineAssembler::CompareTagged(Register value, Operand operand) {
  __ cmp_tagged(value, operand);
}
void BaselineAssembler::CompareTagged(Operand operand, Register value) {
  __ cmp_tagged(operand, value);
}
void BaselineAssembler::CompareByte(Register value, Immediate immediate) {
  __ cmpb(value, immediate);
}

void BaselineAssembler::Move(interpreter::Register output, Register source) {
  return __ movq(RegisterFrameOperand(output), source);
}
void BaselineAssembler::Move(Register output, TaggedIndex value) {
  __ Move(output, value);
}
void BaselineAssembler::Move(Operand output, Register source) {
  __ movq(output, source);
}
void BaselineAssembler::Move(Register output, ExternalReference reference) {
  __ Move(output, reference);
}
void BaselineAssembler::Move(Register output, Handle<HeapObject> value) {
  __ Move(output, value);
}
void BaselineAssembler::Move(Register output, Immediate immediate) {
  __ Move(output, immediate);
}
void BaselineAssembler::MoveMaybeSmi(Register output, Register source) {
  __ movl(output, source);
}
void BaselineAssembler::MoveSmi(Register output, Register source) {
  __ movl(output, source);
}
void BaselineAssembler::MoveByte(Register target, MemOperand operand) {
  __ movb(target, operand);
}

void BaselineAssembler::EnterFrame() {
  FrameScope frame_scope(&masm_, StackFrame::MANUAL);
  __ Push(rbp);  // Caller's frame pointer.
  __ Move(rbp, rsp);
}
void BaselineAssembler::LoadTaggedPointerField(Register output, Register source,
                                               int offset) {
  __ LoadTaggedPointerField(output, FieldOperand(source, offset));
}
void BaselineAssembler::LoadTaggedSignedField(Register output, Register source,
                                              int offset) {
  __ LoadTaggedSignedField(output, FieldOperand(source, offset));
}
void BaselineAssembler::LoadTaggedAnyField(Register output, Register source,
                                           int offset) {
  __ LoadAnyTaggedField(output, FieldOperand(source, offset));
}
void BaselineAssembler::StoreTaggedSignedField(Register target, int offset,
                                               Smi value) {
  __ StoreTaggedField(FieldOperand(target, offset), Immediate(value));
}
void BaselineAssembler::StoreTaggedFieldWithWriteBarrier(Register target,
                                                         int offset,
                                                         Register value,
                                                         Register scratch) {
  DCHECK_NE(target, scratch);
  DCHECK_NE(value, scratch);
  __ StoreTaggedField(FieldOperand(target, offset), value);
  __ RecordWriteField(target, offset, value, scratch, kDontSaveFPRegs);
}
void BaselineAssembler::StoreTaggedFieldNoWriteBarrier(Register target,
                                                       int offset,
                                                       Register value) {
  __ StoreTaggedField(FieldOperand(target, offset), value);
}

void BaselineAssembler::AddSmi(Register lhs, Smi rhs) {
  __ addl(lhs, Immediate(rhs));
}
void BaselineAssembler::AddSmi(Operand lhs, Immediate rhs) {
  __ addl(lhs, rhs);
}
void BaselineAssembler::AddSmi(Register lhs, Register rhs) {
  __ addl(lhs, rhs);
}
void BaselineAssembler::SubSmi(Register lhs, Smi rhs) {
  __ subl(lhs, Immediate(rhs));
}
void BaselineAssembler::SubSmi(Register lhs, Register rhs) {
  __ subl(lhs, rhs);
}
void BaselineAssembler::SubPointer(Register value, Immediate immediate) {
  __ subq(value, immediate);
}
void BaselineAssembler::MulSmi(Register lhs, Register rhs) {
  DCHECK_EQ(lhs, rax);
  DCHECK_NE(lhs, rhs);
  SmiUntag(lhs);
  __ mull(rhs);
}
void BaselineAssembler::BitwiseOrSmi(Register lhs, Smi rhs) {
  __ orl(lhs, Immediate(rhs));
}
void BaselineAssembler::BitwiseOrSmi(Register lhs, Register rhs) {
  __ orl(lhs, rhs);
}
void BaselineAssembler::BitwiseOrInt(Register lhs, Immediate rhs) {
  __ orl(lhs, rhs);
}
void BaselineAssembler::BitwiseXorSmi(Register lhs, Smi rhs) {
  __ xorl(lhs, Immediate(rhs));
}
void BaselineAssembler::BitwiseXorSmi(Register lhs, Register rhs) {
  __ xorl(lhs, rhs);
}
void BaselineAssembler::BitwiseAndSmi(Register lhs, Smi rhs) {
  __ andl(lhs, Immediate(rhs));
}
void BaselineAssembler::BitwiseAndSmi(Register lhs, Register rhs) {
  __ andl(lhs, rhs);
}
void BaselineAssembler::ShiftLeftSmi(Register lhs, int32_t rhs) {
  __ shll(lhs, Immediate(rhs));
}
void BaselineAssembler::ShiftRightSmi(Register lhs, int32_t rhs) {
  __ sarl(lhs, Immediate(rhs));
}
void BaselineAssembler::ShiftRightLogicalSmi(Register lhs, int32_t rhs) {
  __ shrl(lhs, Immediate(rhs));
}
void BaselineAssembler::Decrement(Register) { __ decl(kScratchRegister); }

void BaselineAssembler::Switch(Register reg, int case_value_base,
                               Label** labels, int num_labels) {
  Label fallthrough, jump_table;
  if (case_value_base > 0) {
    __ subq(reg, Immediate(case_value_base));
  }
  __ cmpq(reg, Immediate(num_labels));
  __ j(above_equal, &fallthrough);
  __ leaq(kScratchRegister, MemOperand(&jump_table));
  __ jmp(MemOperand(kScratchRegister, reg, times_8, 0));
  // Emit the jump table inline, under the assumption that it's not too big.
  __ Align(kSystemPointerSize);
  __ bind(&jump_table);
  for (int i = 0; i < num_labels; ++i) {
    __ dq(labels[i]);
  }
  __ bind(&fallthrough);
}

void BaselineCompiler::Prologue() {
  __ EnterFrame();

  __ Move(kInterpreterBytecodeArrayRegister, bytecode_);

  DCHECK_EQ(kJSFunctionRegister, kJavaScriptCallTargetRegister);
  CallBuiltin(Builtins::kBaselinePrologue, kContextRegister,
              kJSFunctionRegister, kJavaScriptCallArgCountRegister,
              kInterpreterBytecodeArrayRegister);

  // Stack check. This folds the checks for both the interrupt stack limit
  // check and the real stack limit into one by just checking for the
  // interrupt limit. The interrupt limit is either equal to the real stack
  // limit or tighter. By ensuring we have space until that limit after
  // building the frame we can quickly precheck both at once.
  __ Move(kScratchRegister, rsp);
  __ SubPointer(kScratchRegister, Immediate(bytecode_->frame_size()));
  __ ComparePointer(
      kScratchRegister,
      __ StackLimitAsOperand(StackLimitKind::kInterruptStackLimit));
  Label after_stack_check;
  __ j(above_equal, &after_stack_check, Label::kNear);

  __ RecordComment("[ Stack/interrupt check");
  // Save incoming new target or generator
  __ Push(kJavaScriptCallNewTargetRegister);
  __ CallRuntime(Runtime::kStackGuard, 0);
  AddAsStartPosition();
  __ Pop(kJavaScriptCallNewTargetRegister);
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
    __ Push(kJavaScriptCallNewTargetRegister);
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
    __ Decrement(kScratchRegister);
    __ j(not_zero, &loop);
  }
  __ RecordComment("]");
}

#undef __

#define __ masm->

void BaselineAssembler::EmitReturn(MacroAssembler* masm) {
  Register weight = BaselineLeaveFrameDescriptor::WeightRegister();
  Register params_size = BaselineLeaveFrameDescriptor::ParamsSizeRegister();
  __ RecordComment("[ Update Interrupt Budget");
  __ Move(kScratchRegister, Operand(rbp, InterpreterFrameConstants::kFunctionOffset));
  __ LoadTaggedPointerField(kScratchRegister, FieldOperand(kScratchRegister, JSFunction::kFeedbackCellOffset));

  __ addl(FieldOperand(kScratchRegister, FeedbackCell::kInterruptBudgetOffset),
          weight);

  // Use compare flags set by add
  // TODO(leszeks): This might be trickier cross-arch.
  Label skip_interrupt_label;
  __ j(greater_equal, &skip_interrupt_label);
  {
    __ SmiTag(params_size);
    __ Push(params_size);
    __ Push(kInterpreterAccumulatorRegister);

    __ Move(kContextRegister, Operand(rbp, InterpreterFrameConstants::kContextOffset));
    __ Push(Operand(rbp, InterpreterFrameConstants::kFunctionOffset));
    __ CallRuntime(Runtime::kBytecodeBudgetInterruptFromBytecode);

    __ Pop(kInterpreterAccumulatorRegister);
    __ Pop(params_size);
    __ SmiUntag(params_size);
  }
   __ RecordComment("]");

  __ bind(&skip_interrupt_label);

  Register actual_params_size = kScratchRegister;
  // Compute the size of the actual parameters + receiver (in bytes).
  __ movq(actual_params_size,
          Operand(rbp, StandardFrameConstants::kArgCOffset));

  // If actual is bigger than formal, then we should use it to free up the stack
  // arguments.
  Label corrected_args_count;
  __ cmpq(params_size, actual_params_size);
  __ j(greater_equal, &corrected_args_count, Label::kNear);
  __ movq(params_size, actual_params_size);
  __ bind(&corrected_args_count);

  // Leave the frame (also dropping the register file).
  __ LeaveFrame(StackFrame::MANUAL);

  // Drop receiver + arguments.
  Register return_pc = kScratchRegister;
  __ PopReturnAddressTo(return_pc);
  __ leaq(rsp, Operand(rsp, params_size, times_system_pointer_size, kSystemPointerSize));
  __ PushReturnAddressFrom(return_pc);
  __ Ret();
}

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_X64_BASELINE_COMPILER_X64_H_
