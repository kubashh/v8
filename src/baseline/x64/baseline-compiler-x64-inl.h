// Use of this source code is governed by a BSD-style license that can be
// Copyright 2021 the V8 project authors. All rights reserved.
// found in the LICENSE file.

#ifndef V8_BASELINE_X64_BASELINE_COMPILER_X64_INL_H_
#define V8_BASELINE_X64_BASELINE_COMPILER_X64_INL_H_

#include "src/baseline/baseline-compiler.h"
#include "src/codegen/interface-descriptors.h"
#include "src/codegen/x64/register-x64.h"
#include "src/objects/code-inl.h"

namespace v8 {
namespace internal {
namespace baseline {

namespace {

// TODO(verwaest): For now this avoids using kScratchRegister(==r10) since the
// macro-assembler doesn't use this scope and will conflict.
static constexpr Register kScratchRegisters[] = {r8, r9, r11, r12, r14, r15};
static constexpr int kNumScratchRegisters = arraysize(kScratchRegisters);

}  // namespace

class BaselineAssembler::ScratchRegisterScope {
 public:
  explicit ScratchRegisterScope(BaselineAssembler* assembler)
      : assembler_(assembler),
        prev_scope_(assembler->scratch_register_scope_),
        registers_used_(prev_scope_ == nullptr ? 0
                                               : prev_scope_->registers_used_) {
    assembler_->scratch_register_scope_ = this;
  }
  ~ScratchRegisterScope() { assembler_->scratch_register_scope_ = prev_scope_; }

  Register AcquireScratch() {
    DCHECK_LT(registers_used_, kNumScratchRegisters);
    return kScratchRegisters[registers_used_++];
  }

 private:
  BaselineAssembler* assembler_;
  ScratchRegisterScope* prev_scope_;
  int registers_used_;
};

// TODO(leszeks): Unify condition names in the MacroAssembler.
enum class Condition : uint8_t {
  kEqual = equal,
  kNotEqual = not_equal,
  kLessThan = less,
  kGreaterThan = greater,
  kLessThanEqual = less_equal,
  kGreaterThanEqual = greater_equal,
  kUnsignedLessThan = below,
  kUnsignedGreaterThan = above,
  kUnsignedLessThanEqual = below_equal,
  kUnsignedGreaterThanEqual = above_equal,
  kOverflow = overflow,
  kNoOverflow = no_overflow,
  kNotZero = not_zero,
};

internal::Condition AsMasmCondition(Condition cond) {
  return static_cast<internal::Condition>(cond);
}

namespace {

#define __ masm_->

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
void BaselineAssembler::Jump(Label* target, Label::Distance distance) {
  __ jmp(target, distance);
}
void BaselineAssembler::JumpIf(Condition cc, Label* target,
                          Label::Distance distance) {
  __ j(AsMasmCondition(cc), target, distance);
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
void BaselineAssembler::CompareByte(Register value, int32_t byte) {
  __ cmpb(value, Immediate(byte));
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
void BaselineAssembler::MoveMaybeSmi(Register output, Register source) {
  __ mov_tagged(output, source);
}
void BaselineAssembler::MoveSmi(Register output, Register source) {
  __ mov_tagged(output, source);
}
void BaselineAssembler::MoveByte(Register target, MemOperand operand) {
  __ movb(target, operand);
}

void BaselineAssembler::EnterFrame() {
  FrameScope frame_scope(masm_, StackFrame::MANUAL);
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

                                                         Register value) {
  BaselineAssembler::ScratchRegisterScope scratch_scope(this);
  Register scratch = scratch_scope.AcquireScratch();
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

void BaselineAssembler::AddToInterruptBudget(Register feedback_cell, int32_t weight) {
  __ addl(FieldOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset),
          Immediate(weight));
}

void BaselineAssembler::AddSmi(Register lhs, Smi rhs) {
  __ addl(lhs, Immediate(rhs));
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
void BaselineAssembler::BitwiseOrInt(Register lhs, int32_t rhs) {
  __ orl(lhs, Immediate(rhs));
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
void BaselineAssembler::Decrement(Register reg) { __ decl(reg); }

void BaselineAssembler::Switch(Register reg, int case_value_base,
                               Label** labels, int num_labels) {
  ScratchRegisterScope scope(this);
  Register table = scope.AcquireScratch();
  Label fallthrough, jump_table;
  if (case_value_base > 0) {
    __ subq(reg, Immediate(case_value_base));
  }
  __ cmpq(reg, Immediate(num_labels));
  __ j(above_equal, &fallthrough);
  __ leaq(table, MemOperand(&jump_table));
  __ jmp(MemOperand(table, reg, times_8, 0));
  // Emit the jump table inline, under the assumption that it's not too big.
  __ Align(kSystemPointerSize);
  __ bind(&jump_table);
  for (int i = 0; i < num_labels; ++i) {
    __ dq(labels[i]);
  }
  __ bind(&fallthrough);
}

#undef __

#define __ basm_.

void BaselineCompiler::Prologue() {
  __ EnterFrame();
  AddAsStartPosition();

  __ Move(kInterpreterBytecodeArrayRegister, bytecode_);
  DCHECK_EQ(kJSFunctionRegister, kJavaScriptCallTargetRegister);
  CallBuiltin(Builtins::kBaselinePrologue, kContextRegister,
              kJSFunctionRegister, kJavaScriptCallArgCountRegister,
              kInterpreterBytecodeArrayRegister);

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
    BaselineAssembler::ScratchRegisterScope scope(&basm_);
    Register scratch = scope.AcquireScratch();
    __ Move(scratch, register_count / kLoopUnrollSize);
    Label loop;
    __ Bind(&loop);
    for (int j = 0; j < kLoopUnrollSize; ++j) {
      __ Push(kInterpreterAccumulatorRegister);
    }
    __ Decrement(scratch);
    __ JumpIf(Condition::kNotZero, &loop);
  }
  __ RecordComment("]");
}

#undef __

#define __ basm.

void BaselineAssembler::EmitReturn(MacroAssembler* masm) {
  BaselineAssembler basm(masm);

  Register weight = BaselineLeaveFrameDescriptor::WeightRegister();
  Register params_size = BaselineLeaveFrameDescriptor::ParamsSizeRegister();

  BaselineAssembler::ScratchRegisterScope scope(&basm);
  Register scratch = scope.AcquireScratch();
  __ RecordComment("[ Update Interrupt Budget");
  __ LoadFunction(scratch);
  __ LoadTaggedPointerField(scratch, scratch, JSFunction::kFeedbackCellOffset);

  __ masm()->addl(FieldOperand(scratch, FeedbackCell::kInterruptBudgetOffset),
                  weight);

  // Use compare flags set by add
  // TODO(leszeks): This might be trickier cross-arch.
  Label skip_interrupt_label;
  __ JumpIf(Condition::kGreaterThanEqual, &skip_interrupt_label);
  {
    __ SmiTag(params_size);
    __ Push(params_size);
    __ Push(kInterpreterAccumulatorRegister);

    __ Move(kContextRegister, Operand(rbp, InterpreterFrameConstants::kContextOffset));
    __ Push(Operand(rbp, InterpreterFrameConstants::kFunctionOffset));
    __ CallRuntime(Runtime::kBytecodeBudgetInterruptFromBytecode, 1);

    __ Pop(kInterpreterAccumulatorRegister);
    __ Pop(params_size);
    __ SmiUntag(params_size);
  }
   __ RecordComment("]");

  __ Bind(&skip_interrupt_label);

  Register actual_params_size = scratch;
  // Compute the size of the actual parameters + receiver (in bytes).
  __ masm()->movq(actual_params_size,
                  Operand(rbp, StandardFrameConstants::kArgCOffset));

  // If actual is bigger than formal, then we should use it to free up the stack
  // arguments.
  Label corrected_args_count;
  __ masm()->cmpq(params_size, actual_params_size);
  __ JumpIf(Condition::kGreaterThanEqual, &corrected_args_count, Label::kNear);
  __ masm()->movq(params_size, actual_params_size);
  __ Bind(&corrected_args_count);

  // Leave the frame (also dropping the register file).
  __ LeaveFrame();

  // Drop receiver + arguments.
  Register return_pc = scratch;
  __ masm()->PopReturnAddressTo(return_pc);
  __ masm()->leaq(rsp, Operand(rsp, params_size, times_system_pointer_size,
                               kSystemPointerSize));
  __ masm()->PushReturnAddressFrom(return_pc);
  __ masm()->Ret();
}

#undef __

}  // namespace baseline  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_X64_BASELINE_COMPILER_X64_INL_H_
