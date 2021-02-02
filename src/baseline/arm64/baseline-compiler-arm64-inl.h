// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_ARM64_BASELINE_COMPILER_ARM64_INL_H_
#define V8_BASELINE_ARM64_BASELINE_COMPILER_ARM64_INL_H_

#include "src/baseline/baseline-compiler.h"

namespace v8 {
namespace internal {
namespace baseline {

class BaselineAssembler::ScratchRegisterScope {
 public:
  explicit ScratchRegisterScope(BaselineAssembler* assembler)
      : wrapped_scope_(assembler->masm()) {
    // We don't use the in-assembler scoping mechanism, since
    // UseScratchRegisterScope does it for us.
    DCHECK_NULL(assembler->scratch_register_scope_);
  }

  Register AcquireScratch() { return wrapped_scope_.AcquireX(); }

 private:
  UseScratchRegisterScope wrapped_scope_;
};

// TODO(leszeks): Unify condition names in the MacroAssembler.
enum class Condition : uint8_t {
  kEqual = eq,
  kNotEqual = ne,

  kLessThan = lt,
  kGreaterThan = gt,
  kLessThanEqual = le,
  kGreaterThanEqual = ge,

  kUnsignedLessThan = lo,
  kUnsignedGreaterThan = hi,
  kUnsignedLessThanEqual = ls,
  kUnsignedGreaterThanEqual = hs,

  kOverflow = vs,
  kNoOverflow = vc,

  kZero = eq,
  kNotZero = ne,
};

internal::Condition AsMasmCondition(Condition cond) {
  return static_cast<internal::Condition>(cond);
}

namespace {

#ifdef DEBUG
bool Clobbers(Register target, MemOperand op) {
  return op.base() == target || op.regoffset() == target;
}
#endif

}  // namespace

#define __ masm_->

MemOperand BaselineAssembler::RegisterFrameOperand(
    interpreter::Register interpreter_register) {
  return MemOperand(sp, interpreter_register.ToOperand() * kSystemPointerSize);
}

void BaselineAssembler::Jump(Label* target, Label::Distance distance) {
  __ B(target);
}
void BaselineAssembler::JumpIf(Condition cc, Label* target, Label::Distance) {
  __ B(AsMasmCondition(cc), target);
}
void BaselineAssembler::JumpIfRoot(Register value, RootIndex index,
                                   Label* target, Label::Distance) {
  __ JumpIfRoot(value, index, target);
}
void BaselineAssembler::JumpIfNotRoot(Register value, RootIndex index,
                                      Label* target, Label::Distance) {
  __ JumpIfNotRoot(value, index, target);
}
void BaselineAssembler::JumpIfSmi(Register value, Label* target,
                                  Label::Distance) {
  __ JumpIfSmi(value, target);
}
void BaselineAssembler::JumpIfNotSmi(Register value, Label* target,
                                     Label::Distance) {
  __ JumpIfNotSmi(value, target);
}

void BaselineAssembler::CallBuiltin(Builtins::Name builtin) {
  ScratchRegisterScope temps(this);
  Register temp = temps.AcquireScratch();
  __ LoadEntryFromBuiltinIndex(builtin, temp);
  __ Call(temp);
}

void BaselineAssembler::TailCallBuiltin(Builtins::Name builtin) {
  ScratchRegisterScope temps(this);
  Register temp = temps.AcquireScratch();
  __ LoadEntryFromBuiltinIndex(builtin, temp);
  __ Jump(temp);
}

void BaselineAssembler::TestAndBranchIfAllClear(Register value, int mask,
                                                Label* target,
                                                Label::Distance) {
  __ Tst(value, Immediate(mask));
  JumpIf(Condition::kZero, target);
}
void BaselineAssembler::TestAndBranchIfAnySet(Register value, int mask,
                                              Label* target, Label::Distance) {
  __ Tst(value, Immediate(mask));
  JumpIf(Condition::kNotZero, target);
}

void BaselineAssembler::CmpObjectType(Register object,
                                      InstanceType instance_type,
                                      Register map) {
  ScratchRegisterScope temps(this);
  Register type = temps.AcquireScratch();
  __ CompareObjectType(object, map, type, instance_type);
}
void BaselineAssembler::CmpInstanceType(Register value,
                                        InstanceType instance_type) {
  ScratchRegisterScope temps(this);
  Register type = temps.AcquireScratch();
  __ CompareInstanceType(value, type, instance_type);
}
void BaselineAssembler::Cmp(Register value, Smi smi) { __ Cmp(value, smi); }
void BaselineAssembler::ComparePointer(Register value, MemOperand operand) {
  ScratchRegisterScope temps(this);
  Register tmp = temps.AcquireScratch();
  __ Ldr(tmp, operand);
  __ Cmp(value, tmp);
}
void BaselineAssembler::SmiCompare(Register lhs, Register rhs) {
  __ AssertSmi(lhs);
  __ AssertSmi(rhs);
  __ CmpTagged(lhs, rhs);
}
void BaselineAssembler::CompareTagged(Register value, MemOperand operand) {
  ScratchRegisterScope temps(this);
  Register tmp = temps.AcquireScratch();
  __ Ldr(tmp, operand);
  __ CmpTagged(value, tmp);
}
void BaselineAssembler::CompareTagged(MemOperand operand, Register value) {
  ScratchRegisterScope temps(this);
  Register tmp = temps.AcquireScratch();
  __ Ldr(tmp, operand);
  __ CmpTagged(tmp, value);
}
void BaselineAssembler::CompareByte(Register value, int32_t byte) {
  __ Cmp(value, Immediate(byte));
}

void BaselineAssembler::Move(interpreter::Register output, Register source) {
  Move(RegisterFrameOperand(output), source);
}
void BaselineAssembler::Move(Register output, TaggedIndex value) {
  __ Mov(output, Immediate(value.ptr()));
}
void BaselineAssembler::Move(MemOperand output, Register source) {
  __ Str(source, output);
}
void BaselineAssembler::Move(Register output, ExternalReference reference) {
  __ Mov(output, Operand(reference));
}
void BaselineAssembler::Move(Register output, Handle<HeapObject> value) {
  __ Mov(output, Operand(value));
}
void BaselineAssembler::Move(Register output, int32_t value) {
  __ Mov(output, Immediate(value));
}
void BaselineAssembler::MoveMaybeSmi(Register output, Register source) {
  __ Mov(output, source);
}
void BaselineAssembler::MoveSmi(Register output, Register source) {
  __ Mov(output, source);
}

void BaselineAssembler::Push(MemOperand operand) {
  ScratchRegisterScope temps(this);
  Register tmp = temps.AcquireScratch();
  __ Push(tmp);
}

void BaselineAssembler::LoadTaggedPointerField(Register output, Register source,
                                               int offset) {
  __ LoadTaggedPointerField(output, FieldMemOperand(source, offset));
}
void BaselineAssembler::LoadTaggedSignedField(Register output, Register source,
                                              int offset) {
  __ LoadTaggedSignedField(output, FieldMemOperand(source, offset));
}
void BaselineAssembler::LoadTaggedAnyField(Register output, Register source,
                                           int offset) {
  __ LoadAnyTaggedField(output, FieldMemOperand(source, offset));
}
void BaselineAssembler::LoadByteField(Register output, Register source,
                                      int offset) {
  __ Ldrb(output, FieldMemOperand(source, offset));
}
void BaselineAssembler::StoreTaggedSignedField(Register target, int offset,
                                               Smi value) {
  ScratchRegisterScope temps(this);
  Register tmp = temps.AcquireScratch();
  __ Mov(tmp, Operand(value));
  __ StoreTaggedField(tmp, FieldMemOperand(target, offset));
}
void BaselineAssembler::StoreTaggedFieldWithWriteBarrier(Register target,
                                                         int offset,
                                                         Register value) {
  __ StoreTaggedField(value, FieldMemOperand(target, offset));
  __ RecordWriteField(target, offset, value, kLRHasNotBeenSaved,
                      kDontSaveFPRegs);
}
void BaselineAssembler::StoreTaggedFieldNoWriteBarrier(Register target,
                                                       int offset,
                                                       Register value) {
  __ StoreTaggedField(value, FieldMemOperand(target, offset));
}

void BaselineAssembler::AddToInterruptBudget(int32_t weight) {
  ScratchRegisterScope scratch_scope(this);
  Register feedback_cell = scratch_scope.AcquireScratch();
  LoadFunction(feedback_cell);
  LoadTaggedPointerField(feedback_cell, feedback_cell,
                         JSFunction::kFeedbackCellOffset);

  Register interrupt_budget = scratch_scope.AcquireScratch().W();
  __ Ldr(interrupt_budget,
         FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  // Remember to set flags as part of the add!
  __ Adds(interrupt_budget, interrupt_budget, weight);
  __ Str(interrupt_budget,
         FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
}

void BaselineAssembler::AddToInterruptBudget(Register weight) {
  ScratchRegisterScope scratch_scope(this);
  Register feedback_cell = scratch_scope.AcquireScratch();
  LoadFunction(feedback_cell);
  LoadTaggedPointerField(feedback_cell, feedback_cell,
                         JSFunction::kFeedbackCellOffset);

  Register interrupt_budget = scratch_scope.AcquireScratch().W();
  __ Ldr(interrupt_budget,
         FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  // Remember to set flags as part of the add!
  __ Adds(interrupt_budget, interrupt_budget, weight.W());
  __ Str(interrupt_budget,
         FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
}

void BaselineAssembler::AddSmi(Register lhs, Smi rhs) {
  __ Add(lhs.W(), lhs.W(), Immediate(rhs));
}
void BaselineAssembler::AddSmi(Register lhs, Register rhs) {
  __ Add(lhs.W(), lhs.W(), rhs.W());
}
void BaselineAssembler::SubSmi(Register lhs, Smi rhs) {
  __ Sub(lhs.W(), lhs.W(), Immediate(rhs));
}
void BaselineAssembler::SubSmi(Register lhs, Register rhs) {
  __ Sub(lhs.W(), lhs.W(), rhs);
}
void BaselineAssembler::MulSmi(Register lhs, Register rhs) {
  __ SmiUntag(rhs);
  __ Mul(lhs.W(), lhs.W(), rhs.W());
}
void BaselineAssembler::BitwiseOrSmi(Register lhs, Smi rhs) {
  __ Orr(lhs.W(), lhs.W(), Immediate(rhs));
}
void BaselineAssembler::BitwiseOrSmi(Register lhs, Register rhs) {
  __ Orr(lhs.W(), lhs.W(), rhs);
}
void BaselineAssembler::BitwiseXorSmi(Register lhs, Smi rhs) {
  __ Eor(lhs.W(), lhs.W(), Immediate(rhs));
}
void BaselineAssembler::BitwiseXorSmi(Register lhs, Register rhs) {
  __ Eor(lhs.W(), lhs.W(), rhs);
}
void BaselineAssembler::BitwiseAndSmi(Register lhs, Smi rhs) {
  __ And(lhs.W(), lhs.W(), Immediate(rhs));
}
void BaselineAssembler::BitwiseAndSmi(Register lhs, Register rhs) {
  __ And(lhs.W(), lhs.W(), rhs);
}
void BaselineAssembler::ShiftLeftSmi(Register lhs, int32_t rhs) {
  __ Lsl(lhs.W(), lhs.W(), rhs);
}
void BaselineAssembler::ShiftRightSmi(Register lhs, int32_t rhs) {
  __ Asr(lhs.W(), lhs.W(), rhs);
}
void BaselineAssembler::ShiftRightLogicalSmi(Register lhs, int32_t rhs) {
  __ Lsr(lhs.W(), lhs.W(), rhs);
}

void BaselineAssembler::Switch(Register reg, int case_value_base,
                               Label** labels, int num_labels) {
  Label fallthrough;
  if (case_value_base > 0) {
    __ Sub(reg, reg, Immediate(case_value_base));
  }

  // Mostly copied from code-generator-arm64.cc
  ScratchRegisterScope scope(this);
  Register temp = scope.AcquireScratch();
  Label table;
  __ Cmp(reg, num_labels);
  JumpIf(Condition::kUnsignedGreaterThanEqual, &fallthrough);
  __ Adr(temp, &table);
  int entry_size_log2 = 2;
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  ++entry_size_log2;  // Account for BTI.
#endif
  __ Add(temp, temp, Operand(reg, UXTW, entry_size_log2));
  __ Br(temp);
  {
    TurboAssembler::BlockPoolsScope block_pools(masm_, num_labels * kInstrSize);
    __ Bind(&table);
    for (int i = 0; i < num_labels; ++i) {
      __ JumpTarget();
      __ B(labels[i]);
    }
    __ JumpTarget();
    __ Bind(&fallthrough);
  }
}

#undef __

#define __ basm_.

void BaselineCompiler::Prologue() {
  __ masm()->Mov(kInterpreterBytecodeArrayRegister, Operand(bytecode_));
  DCHECK_EQ(kJSFunctionRegister, kJavaScriptCallTargetRegister);
  CallBuiltin(Builtins::kBaselinePrologue, kContextRegister,
              kJSFunctionRegister, kJavaScriptCallArgCountRegister,
              kInterpreterBytecodeArrayRegister);

  __ masm()->AssertSpAligned();
  PrologueFillFrame();
  __ masm()->AssertSpAligned();
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
    for (; i + 2 <= new_target_index; i += 2) {
      __ masm()->Push(kInterpreterAccumulatorRegister,
                      kInterpreterAccumulatorRegister);
    }
    if (i == new_target_index) {
      __ masm()->Push(kJavaScriptCallNewTargetRegister,
                      kInterpreterAccumulatorRegister);
    } else {
      __ masm()->Push(kInterpreterAccumulatorRegister,
                      kJavaScriptCallNewTargetRegister);
    }
    i += 2;
  }
  if (register_count < 2 * kLoopUnrollSize) {
    // If the frame is small enough, just unroll the frame fill completely.
    for (; i < register_count; i += 2) {
      __ masm()->Push(kInterpreterAccumulatorRegister,
                      kInterpreterAccumulatorRegister);
    }
  } else {
    BaselineAssembler::ScratchRegisterScope temps(&basm_);
    Register scratch = temps.AcquireScratch();

    register_count -= i;
    i = 0;
    // Extract the first few registers to round to the unroll size.
    int first_registers = register_count % kLoopUnrollSize;
    for (; i < first_registers; i += 2) {
      __ masm()->Push(kInterpreterAccumulatorRegister,
                      kInterpreterAccumulatorRegister);
    }
    __ Move(scratch, register_count / kLoopUnrollSize);
    Label loop;
    __ Bind(&loop);
    for (int j = 0; j < kLoopUnrollSize; j += 2) {
      __ masm()->Push(kInterpreterAccumulatorRegister,
                      kInterpreterAccumulatorRegister);
    }
    __ masm()->Subs(scratch, scratch, 2);
    __ JumpIf(Condition::kGreaterThan, &loop);
  }
  __ RecordComment("]");
}

#undef __

#define __ basm.

void BaselineAssembler::EmitReturn(MacroAssembler* masm) {
  BaselineAssembler basm(masm);

  Register weight = BaselineLeaveFrameDescriptor::WeightRegister();
  Register params_size = BaselineLeaveFrameDescriptor::ParamsSizeRegister();
  __ RecordComment("[ Update Interrupt Budget");
  __ AddToInterruptBudget(weight);

  // Use compare flags set by add
  Label skip_interrupt_label;
  __ JumpIf(Condition::kGreaterThanEqual, &skip_interrupt_label);
  {
    __ SmiTag(params_size);
    __ masm()->Push(params_size, kInterpreterAccumulatorRegister);

    __ Move(kContextRegister,
            MemOperand(sp, InterpreterFrameConstants::kContextOffset));
    __ Move(kJSFunctionRegister,
            MemOperand(sp, InterpreterFrameConstants::kFunctionOffset));
    __ masm()->PushArgument(kJSFunctionRegister);
    __ CallRuntime(Runtime::kBytecodeBudgetInterruptFromBytecode, 1);

    __ masm()->Pop(kInterpreterAccumulatorRegister, params_size);
    __ SmiUntag(params_size);
  }
  __ RecordComment("]");

  __ Bind(&skip_interrupt_label);

  BaselineAssembler::ScratchRegisterScope temps(&basm);
  Register actual_params_size = temps.AcquireScratch();
  // Compute the size of the actual parameters + receiver (in bytes).
  __ Move(actual_params_size,
          MemOperand(sp, StandardFrameConstants::kArgCOffset));

  // If actual is bigger than formal, then we should use it to free up the stack
  // arguments.
  Label corrected_args_count;
  __ masm()->Cmp(params_size, actual_params_size);
  __ JumpIf(Condition::kGreaterThanEqual, &corrected_args_count);
  __ masm()->Mov(params_size, actual_params_size);
  __ Bind(&corrected_args_count);

  // Leave the frame (also dropping the register file).
  __ LeaveFrame();

  // Drop receiver + arguments.
  if (__ emit_debug_code()) {
    __ masm()->Tst(params_size, kSystemPointerSize - 1);
    __ masm()->Check(eq, AbortReason::kUnexpectedValue);
  }
  __ masm()->Lsr(params_size, params_size, kSystemPointerSizeLog2);
  __ masm()->DropArguments(params_size);
  __ masm()->Ret();
}

#undef __

}  // namespace baseline  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_ARM64_BASELINE_COMPILER_ARM64_INL_H_
