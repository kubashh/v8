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

void BaselineAssembler::Move(interpreter::Register output, Register source) {
  return movq(RegisterFrameOperand(output), source);
}
void BaselineAssembler::CallBuiltin(Builtins::Name builtin) {
  call(EntryFromBuiltinIndexAsOperand(builtin));
}

void BaselineAssembler::TailCallBuiltin(Builtins::Name builtin) {
  jmp(EntryFromBuiltinIndexAsOperand(builtin));
}

void BaselineAssembler::LoadTaggedPointerField(Register output, Register source,
                                               int offset) {
  MacroAssembler::LoadTaggedPointerField(output, FieldOperand(source, offset));
}
void BaselineAssembler::LoadTaggedSignedField(Register output, Register source,
                                              int offset) {
  MacroAssembler::LoadTaggedSignedField(output, FieldOperand(source, offset));
}
void BaselineAssembler::LoadTaggedAnyField(Register output, Register source,
                                           int offset) {
  LoadAnyTaggedField(output, FieldOperand(source, offset));
}
void BaselineAssembler::StoreTaggedSignedField(Register target, int offset,
                                               Smi value) {
  StoreTaggedField(FieldOperand(target, offset), Immediate(value));
}
void BaselineAssembler::StoreTaggedFieldWithWriteBarrier(Register target,
                                                         int offset,
                                                         Register value,
                                                         Register scratch) {
  DCHECK_NE(target, scratch);
  DCHECK_NE(value, scratch);
  StoreTaggedField(FieldOperand(target, offset), value);
  RecordWriteField(target, offset, value, scratch, kDontSaveFPRegs);
}
void BaselineAssembler::StoreTaggedFieldNoWriteBarrier(Register target,
                                                       int offset,
                                                       Register value) {
  StoreTaggedField(FieldOperand(target, offset), value);
}

void BaselineAssembler::AddSmi(Register lhs, Smi rhs) {
  addl(lhs, Immediate(rhs));
}
void BaselineAssembler::AddSmi(Register lhs, Register rhs) { addl(lhs, rhs); }
void BaselineAssembler::SubSmi(Register lhs, Smi rhs) {
  subl(lhs, Immediate(rhs));
}
void BaselineAssembler::SubSmi(Register lhs, Register rhs) { subl(lhs, rhs); }
void BaselineAssembler::MulSmi(Register lhs, Register rhs) {
  DCHECK_EQ(lhs, rax);
  DCHECK_NE(lhs, rhs);
  SmiUntag(lhs);
  mull(rhs);
}
void BaselineAssembler::BitwiseOrSmi(Register lhs, Smi rhs) {
  orl(lhs, Immediate(rhs));
}
void BaselineAssembler::BitwiseOrSmi(Register lhs, Register rhs) {
  orl(lhs, rhs);
}
void BaselineAssembler::BitwiseXorSmi(Register lhs, Smi rhs) {
  xorl(lhs, Immediate(rhs));
}
void BaselineAssembler::BitwiseXorSmi(Register lhs, Register rhs) {
  xorl(lhs, rhs);
}
void BaselineAssembler::BitwiseAndSmi(Register lhs, Smi rhs) {
  andl(lhs, Immediate(rhs));
}
void BaselineAssembler::BitwiseAndSmi(Register lhs, Register rhs) {
  andl(lhs, rhs);
}
void BaselineAssembler::ShiftLeftSmi(Register lhs, int32_t rhs) {
  shll(lhs, Immediate(rhs));
}
void BaselineAssembler::ShiftRightSmi(Register lhs, int32_t rhs) {
  sarl(lhs, Immediate(rhs));
}
void BaselineAssembler::ShiftRightLogicalSmi(Register lhs, int32_t rhs) {
  shrl(lhs, Immediate(rhs));
}

void BaselineAssembler::Switch(Register reg, int case_value_base,
                               Label** labels, int num_labels) {
  Label fallthrough, jump_table;
  if (case_value_base > 0) {
    subq(reg, Immediate(case_value_base));
  }
  cmpq(reg, Immediate(num_labels));
  j(above_equal, &fallthrough);
  leaq(kScratchRegister, MemOperand(&jump_table));
  jmp(MemOperand(kScratchRegister, reg, times_8, 0));
  // Emit the jump table inline, under the assumption that it's not too big.
  Align(kSystemPointerSize);
  bind(&jump_table);
  for (int i = 0; i < num_labels; ++i) {
    dq(labels[i]);
  }
  bind(&fallthrough);
}

#define __ masm_.

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
  // just increase the SP by stackframe size and continue
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
  __ Push(kJavaScriptCallNewTargetRegister);
  __ CallRuntime(Runtime::kStackGuard);
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
    __ decl(kScratchRegister);
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
    SaveAccumulatorScope accumulator_scope(masm);
    __ SmiTag(params_size);
    __ Push(params_size);

    __ Move(kContextRegister, Operand(rbp, InterpreterFrameConstants::kContextOffset));
    __ Push(Operand(rbp, InterpreterFrameConstants::kFunctionOffset));
    __ CallRuntime(Runtime::kBytecodeBudgetInterruptFromBytecode);

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
  __ Move(rsp, rbp);
  __ popq(rbp);

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
