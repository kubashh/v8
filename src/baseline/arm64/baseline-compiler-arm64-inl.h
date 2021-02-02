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

void BaselineAssembler::Move(interpreter::Register output, Register source) {
  return __ Str(source, RegisterFrameOperand(output));
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
  __ B(hs, &fallthrough);
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
  FrameScope frame_scope(&masm_, StackFrame::MANUAL);
  __ masm()->Push<TurboAssembler::kSignLR>(lr, fp);
  __ masm()->Mov(fp, sp);

  __ masm()->Mov(kInterpreterBytecodeArrayRegister, Operand(bytecode_));
  DCHECK_EQ(kJSFunctionRegister, kJavaScriptCallTargetRegister);
  CallBuiltin(Builtins::kBaselinePrologue, kContextRegister,
              kJSFunctionRegister, kJavaScriptCallArgCountRegister,
              kInterpreterBytecodeArrayRegister);

  __ masm()->AssertSpAligned();

  // Stack check. This folds the checks for both the interrupt stack limit
  // check and the real stack limit into one by just checking for the
  // interrupt limit. The interrupt limit is either equal to the real stack
  // limit or tighter. By ensuring we have space until that limit after
  // building the frame we can quickly precheck both at once.
  __ masm()->Sub(x10, sp, Immediate(bytecode_->frame_size()));
  {
    BaselineAssembler::ScratchRegisterScope temps(this);
    Register scratch = temps.AcquireScratch();
    __ masm()->LoadStackLimit(scratch, StackLimitKind::kInterruptStackLimit);
    __ masm()->Cmp(x10, scratch);
  }
  Label after_stack_check;
  __ masm()->B(hs, &after_stack_check);

  __ RecordComment("[ Stack/interrupt check");
  // Save incoming new target or generator
  __ masm()->Push(padreg, kJavaScriptCallNewTargetRegister);
  __ CallRuntime(Runtime::kStackGuard, 0);
  AddAsStartPosition();
  __ masm()->Pop(kJavaScriptCallNewTargetRegister, padreg);
  __ RecordComment("]");

  __ bind(&after_stack_check);

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
    __ masm()->Mov(scratch, Immediate(register_count / kLoopUnrollSize));
    Label loop;
    __ bind(&loop);
    for (int j = 0; j < kLoopUnrollSize; j += 2) {
      __ masm()->Push(kInterpreterAccumulatorRegister,
                      kInterpreterAccumulatorRegister);
    }
    __ masm()->Subs(scratch, scratch, 2);
    __ B(gt, &loop);
  }
  __ RecordComment("]");
}

#undef __

#define __ basm.

void BaselineAssembler::EmitReturn(MacroAssembler* masm) {
  BaselineAssembler basm(masm);
  BaselineAssembler::ScratchRegisterScope temps(&basm);
  Register scratch = temps.AcquireScratch();

  Register weight = BaselineLeaveFrameDescriptor::WeightRegister();
  Register params_size = BaselineLeaveFrameDescriptor::ParamsSizeRegister();
  __ RecordComment("[ Update Interrupt Budget");
  __ Move(scratch, MemOperand(sp, InterpreterFrameConstants::kFunctionOffset));
  __ LoadTaggedPointerField(
      scratch, FieldMemOperand(scratch, JSFunction::kFeedbackCellOffset));

  Register interrupt_budget = temps.AcquireScratch().W();
  __ masm()->Ldr(
      interrupt_budget,
      FieldMemOperand(scratch, FeedbackCell::kInterruptBudgetOffset));
  __ masm()->Adds(interrupt_budget, interrupt_budget, weight);
  __ masm()->Str(
      interrupt_budget,
      FieldMemOperand(scratch, FeedbackCell::kInterruptBudgetOffset));

  // Use compare flags set by add
  Label skip_interrupt_label;
  __ B(ge, &skip_interrupt_label);
  {
    __ SmiTag(params_size);
    __ masm()->Push(params_size, kInterpreterAccumulatorRegister);

    __ Move(kContextRegister,
            MemOperand(sp, InterpreterFrameConstants::kContextOffset));
    __ Move(kJSFunctionRegister,
            MemOperand(sp, InterpreterFrameConstants::kFunctionOffset));
    __ masm()->Push(padreg, kJSFunctionRegister);
    __ CallRuntime(Runtime::kBytecodeBudgetInterruptFromBytecode, 1);
    __ masm()->Pop(padreg);

    __ masm()->Pop(kInterpreterAccumulatorRegister, params_size);
    __ SmiUntag(params_size);
  }
  __ RecordComment("]");

  __ bind(&skip_interrupt_label);

  Register actual_params_size = scratch;
  // Compute the size of the actual parameters + receiver (in bytes).
  __ Move(actual_params_size,
          MemOperand(sp, StandardFrameConstants::kArgCOffset));

  // If actual is bigger than formal, then we should use it to free up the stack
  // arguments.
  Label corrected_args_count;
  __ masm()->Cmp(params_size, actual_params_size);
  __ masm()->B(ge, &corrected_args_count);
  __ masm()->Mov(params_size, actual_params_size);
  __ bind(&corrected_args_count);

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
